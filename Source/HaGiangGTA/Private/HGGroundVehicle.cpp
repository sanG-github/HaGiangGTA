#include "HGGroundVehicle.h"

#include "Components/BoxComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/SplineComponent.h"
#include "EngineUtils.h"
#include "GameFramework/SpringArmComponent.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "HaGiangGTA.h"
#include "HGHappyWaterSubsystem.h"
#include "HGMissionSubsystem.h"
#include "HGCowActor.h"
#include "HGRoadSplineActor.h"
#include "HGViewpointActor.h"
#include "HGWantedSubsystem.h"
#include "HGWaterBody.h"

namespace
{
	/** Fraction of the half length at which front/rear ground probes are placed. */
	constexpr float ProbeLengthFraction = 0.8f;
	/** Required "downward pull" (in g) at a crest before the vehicle leaves the ground. */
	constexpr float CrestLaunchG = 2.0f;
	/** Below this speed the vehicle never launches off a crest. */
	constexpr float MinLaunchSpeed = 900.f;
	/** Steering authority left while airborne. */
	constexpr float AirSteerFactor = 0.25f;
}

AHGGroundVehicle::FTrafficStats AHGGroundVehicle::TrafficStats;
int32 AHGGroundVehicle::ViewpointStops = 0;

AHGGroundVehicle::AHGGroundVehicle()
{
}

void AHGGroundVehicle::BeginPlay()
{
	Super::BeginPlay();
	HeadingYaw = GetActorRotation().Yaw;
	// The chase camera's pitch is driven in UpdateChaseCamera (part of the slope only), not inherited.
	SpringArm->bInheritPitch = false;
	ChaseCamPitchCurrent = ChaseCamPitch;
}

void AHGGroundVehicle::ResetToSpawn()
{
	Super::ResetToSpawn();
	HeadingYaw = SpawnTransform.Rotator().Yaw;
	Speed = 0.f;
	VerticalVelocity = 0.f;
	YawRateDeg = 0.f;
	bGrounded = false;
	GroundNormal = FVector::UpVector;
}

bool AHGGroundVehicle::TraceGround(const FVector& From, FHitResult& OutHit) const
{
	const float HalfZ = Collision->GetScaledBoxExtent().Z;
	const FVector Start = From + FVector(0.f, 0.f, MaxStepUp + HalfZ);
	const FVector End = From - FVector(0.f, 0.f, 50000.f);

	// The seated driver has collision disabled, so ignoring ourselves is enough.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(HGGroundVehicle), /*bTraceComplex*/ false, this);
	return GetWorld()->LineTraceSingleByChannel(OutHit, Start, End, ECC_Visibility, Params);
}

void AHGGroundVehicle::UpdateSpeed(float DeltaSeconds, float Throttle, bool bHandbrake, bool bBoost, const FVector& MoveDir)
{
	float Nitro = 1.f;
	if (IsPlayerControlled())
	{
		if (const UHGHappyWaterSubsystem* HW = GetWorld()->GetSubsystem<UHGHappyWaterSubsystem>())
		{
			Nitro = HW->GetSpeedMultiplier();
		}
	}
	const float Boost = (bBoost ? BoostMultiplier : 1.f) * SpeedMultiplier * Nitro;
	const float TopSpeed = MaxForwardSpeed * Boost * SurfaceScale;

	// Drive and brakes act through the tyres: nothing while airborne. (Throttle used to keep adding speed in
	// the air, so a bike wedged off the ground showed 60+ km/h on the speedo while standing still.)
	if (bGrounded)
	{
		if (Throttle > KINDA_SMALL_NUMBER)
		{
			if (Speed < -10.f)
			{
				Speed += BrakeDeceleration * Throttle * DeltaSeconds;
			}
			else
			{
				// Engine curve: full pull from a standstill fading to nothing at top speed (a 110 cc Win runs
				// out of breath, it doesn't hit a wall).
				const float Frac = FMath::Clamp(Speed / FMath::Max(TopSpeed, 1.f), 0.f, 1.f);
				Speed += Acceleration * Boost * (1.f - Frac * Frac) * Throttle * DeltaSeconds;
			}
		}
		else if (Throttle < -KINDA_SMALL_NUMBER)
		{
			Speed += (Speed > 10.f ? BrakeDeceleration : Acceleration) * Throttle * DeltaSeconds;
		}
		else
		{
			Speed = FMath::FInterpConstantTo(Speed, 0.f, DeltaSeconds, CoastDeceleration);
		}

		if (bHandbrake)
		{
			Speed = FMath::FInterpConstantTo(Speed, 0.f, DeltaSeconds, BrakeDeceleration * 0.6f);
		}
		if (Surface == EHGSurface::OffRoad)
		{
			Speed = FMath::FInterpConstantTo(Speed, 0.f, DeltaSeconds, bGripTyres ? OffRoadDrag * 0.5f : OffRoadDrag);
		}
		// Gravity component along the slope (MoveDir.Z > 0 means uphill).
		Speed += GetWorld()->GetGravityZ() * MoveDir.Z * SlopeGravityScale * DeltaSeconds;

		if (Speed > TopSpeed)
		{
			Speed = FMath::FInterpConstantTo(Speed, TopSpeed, DeltaSeconds, OverSpeedDeceleration);
		}
	}
	Speed = FMath::Max(Speed, -MaxReverseSpeed);
}

void AHGGroundVehicle::MoveWithCollision(const FVector& Delta, const FVector& MoveDir)
{
	if (Delta.IsNearlyZero())
	{
		return;
	}

	const float WalkableZ = FMath::Cos(FMath::DegreesToRadians(MaxWalkableSlopeDeg));

	// SweepMove pushes out of whatever the box starts inside (a corner in the terrain bank after a landing or
	// while the roll catches up with a cambered road) before moving: a sweep that starts penetrating is
	// blocked at t = 0 every frame, which froze the bike for good ("stuck, speedo still 19 km/h").
	FHitResult Hit;
	SweepMove(Delta, Hit);
	bMoveBlocked = Hit.bBlockingHit;
	if (!Hit.bBlockingHit)
	{
		return;
	}
	LastBlocker = Hit.GetActor();
	LastBlockPoint = Hit.ImpactPoint;
	// AI on its lane: the road is validated to lie on the landscape (profile <= 0.3 m), so a box corner touching
	// the cut bank on a sag or a crest is not a wall. Sliding off it wedged trucks for good ("blocked by
	// Landscape_HaGiang, grounded 0" -> re-placed four times in a row); carry on and let SnapToGround settle.
	if (bAutoDrive && !IsPlayerControlled() && FMath::Abs(LastCrossTrack) < 200.f && Hit.GetActor()
		&& Hit.GetActor()->GetClass()->GetName().Contains(TEXT("Landscape")))
	{
		SetActorLocation(GetActorLocation() + Delta * (1.f - Hit.Time), false, nullptr, ETeleportType::None);
		bMoveBlocked = false;
		return;
	}

	FVector Remaining = Delta * (1.f - Hit.Time);

	if (Hit.ImpactNormal.Z >= WalkableZ)
	{
		if (!bGrounded)
		{
			return; // landed; SnapToGround settles the height
		}
		// Bumped into the toe of a ramp / small step: lift, then continue forward.
		FHitResult UpHit;
		AddActorWorldOffset(FVector(0.f, 0.f, MaxStepUp), true, &UpHit);
		Remaining.Z = FMath::Max(Remaining.Z, 0.f);
		FHitResult FwdHit;
		AddActorWorldOffset(Remaining, true, &FwdHit);
		if (!FwdHit.bBlockingHit || FwdHit.ImpactNormal.Z >= WalkableZ)
		{
			return;
		}
		Hit = FwdHit;
		Remaining = Remaining * (1.f - FwdHit.Time);
	}

	if (IsPlayerControlled() && GetWorld()->GetTimeSeconds() - LastBlockLogTime > 1.0)
	{
		LastBlockLogTime = GetWorld()->GetTimeSeconds();
		UE_LOG(LogHaGiang, Log, TEXT("[HG_VEH] %s blocked by %s (normal z %.2f) at %.0f km/h"), *GetName(),
			*GetNameSafe(Hit.GetActor()), Hit.ImpactNormal.Z, Speed * 0.036f);
	}
	// Wall / steep face: slide along it and lose speed proportional to the impact angle.
	const FVector Slide = FVector::VectorPlaneProject(Remaining, Hit.ImpactNormal);
	AddActorWorldOffset(Slide, true);

	const float Impact = FMath::Abs(FVector::DotProduct(MoveDir, Hit.ImpactNormal));
	if (IsPlayerControlled())
	{
		if (UHGWantedSubsystem* Wanted = GetWorld()->GetSubsystem<UHGWantedSubsystem>())
		{
			Wanted->ReportImpact(Hit.GetActor(), Hit.GetComponent(), FMath::Abs(Speed) * FMath::Max(Impact, 0.3f));
		}
	}
	Speed *= (1.f - 0.85f * Impact);

	if (!bGrounded && Hit.ImpactNormal.Z < -0.5f)
	{
		VerticalVelocity = FMath::Min(VerticalVelocity, 0.f); // ceiling
	}
}

void AHGGroundVehicle::SnapToGround(float DeltaSeconds, const FVector& MoveDir)
{
	DeltaSeconds = FMath::Max(DeltaSeconds, KINDA_SMALL_NUMBER);
	const float HalfX = Collision->GetScaledBoxExtent().X;
	const float HalfZ = Collision->GetScaledBoxExtent().Z;
	const float WalkableZ = FMath::Cos(FMath::DegreesToRadians(MaxWalkableSlopeDeg));
	const FVector FlatFwd = FRotator(0.f, HeadingYaw, 0.f).Vector();
	const FVector FlatRight = FRotator(0.f, HeadingYaw + 90.f, 0.f).Vector();
	const FVector Loc = GetActorLocation();

	FHitResult FrontHit, RearHit;
	const bool bFront = TraceGround(Loc + FlatFwd * HalfX * ProbeLengthFraction, FrontHit);
	const bool bRear = TraceGround(Loc - FlatFwd * HalfX * ProbeLengthFraction, RearHit);

	auto Takeoff = [&]()
	{
		if (bGrounded)
		{
			VerticalVelocity = MoveDir.Z * Speed;
			bGrounded = false;
		}
	};

	if (!bFront && !bRear)
	{
		Takeoff();
		return;
	}

	float GroundZ;
	FVector NewNormal;
	if (bFront && bRear)
	{
		GroundZ = 0.5f * (FrontHit.ImpactPoint.Z + RearHit.ImpactPoint.Z);
		const FVector Axis = FrontHit.ImpactPoint - RearHit.ImpactPoint;
		FVector PitchNormal = FVector::CrossProduct(Axis, FlatRight).GetSafeNormal();
		if (PitchNormal.Z < 0.f)
		{
			PitchNormal *= -1.f;
		}
		const FVector AvgNormal = (FrontHit.ImpactNormal + RearHit.ImpactNormal).GetSafeNormal();
		NewNormal = (2.f * PitchNormal + AvgNormal).GetSafeNormal();
		// A long vehicle on a hairpin can have one probe out over the cut bank: land / stay on the probe that
		// is on the road instead of hovering "airborne" over the steep one (trucks wedged and hopped forward).
		if (NewNormal.Z < WalkableZ && (FrontHit.ImpactNormal.Z >= WalkableZ) != (RearHit.ImpactNormal.Z >= WalkableZ))
		{
			const FHitResult& Good = FrontHit.ImpactNormal.Z >= WalkableZ ? FrontHit : RearHit;
			GroundZ = Good.ImpactPoint.Z;
			NewNormal = Good.ImpactNormal;
		}
	}
	else
	{
		const FHitResult& Only = bFront ? FrontHit : RearHit;
		GroundZ = Only.ImpactPoint.Z;
		NewNormal = Only.ImpactNormal;
	}

	const float DesiredZ = GroundZ + HalfZ + GroundClearance;
	const float Gap = Loc.Z - DesiredZ; // > 0 : above the ground
	const bool bWalkable = NewNormal.Z >= WalkableZ;

	if (bGrounded)
	{
		if (!bWalkable || Gap > GroundSnapDistance + MaxStepUp)
		{
			Takeoff();
			return;
		}
		// Ballistic crest test: following the new slope would need more than
		// CrestLaunchG of downward acceleration -> leave the ground.
		const FVector NewDir = FVector::VectorPlaneProject(FlatFwd, NewNormal).GetSafeNormal();
		const float RequiredAccel = (NewDir.Z * Speed - MoveDir.Z * Speed) / DeltaSeconds;
		if (Speed > MinLaunchSpeed && RequiredAccel < GetWorld()->GetGravityZ() * CrestLaunchG)
		{
			Takeoff();
			return;
		}
	}
	else
	{
		// Land when the ground is within this frame's fall (not only within 2 cm): otherwise a vehicle whose
		// sweep is stopped by the slope just above the probe points hovers "airborne" forever.
		const float LandGap = FMath::Max(2.f, -VerticalVelocity * DeltaSeconds + 2.f);
		if (!bWalkable || Gap > LandGap || VerticalVelocity > 0.f)
		{
			return;
		}
		// Landing scrubs speed depending on how hard we hit.
		Speed *= FMath::GetMappedRangeValueClamped(FVector2D(0.f, -2500.f), FVector2D(1.f, 0.75f), VerticalVelocity);
	}

	SetActorLocation(FVector(Loc.X, Loc.Y, DesiredZ), false, nullptr, ETeleportType::None);
	GroundComponent = (bFront ? FrontHit : RearHit).GetComponent();
	bGrounded = true;
	VerticalVelocity = 0.f;
	GroundNormal = NewNormal;
}

void AHGGroundVehicle::UpdateOrientation(float DeltaSeconds)
{
	const FVector FlatFwd = FRotator(0.f, HeadingYaw, 0.f).Vector();

	FRotator Target;
	if (bGrounded)
	{
		const FVector Fwd = FVector::VectorPlaneProject(FlatFwd, GroundNormal).GetSafeNormal();
		Target = FRotationMatrix::MakeFromXZ(Fwd, GroundNormal).Rotator();
	}
	else
	{
		const float FlightPitch = FMath::RadiansToDegrees(FMath::Atan2(VerticalVelocity, FMath::Max(FMath::Abs(Speed), 1.f)));
		Target = FRotator(FMath::Clamp(FlightPitch * 0.5f, -25.f, 25.f), HeadingYaw, 0.f);
	}

	FRotator NewRot = FMath::RInterpTo(GetActorRotation(), Target, DeltaSeconds, AlignInterpSpeed);
	NewRot.Yaw = Target.Yaw; // heading is authoritative
	SetActorRotation(NewRot, ETeleportType::None);
}

void AHGGroundVehicle::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// AI far from the player (beyond its 300 m draw distance) ticks at 10 Hz: same physics with bigger steps.
	if (bAutoDrive && !IsPlayerControlled())
	{
		FarCheckTimer -= DeltaSeconds;
		if (FarCheckTimer <= 0.f)
		{
			FarCheckTimer = 1.f;
			// Distance to the camera (not the pawn: flycam, review cameras).
			const APlayerController* PC = GetWorld()->GetFirstPlayerController();
			bFarLOD = PC && PC->PlayerCameraManager
				&& FVector::DistSquared(PC->PlayerCameraManager->GetCameraLocation(), GetActorLocation()) > FMath::Square(45000.f);
			SetActorTickInterval(bFarLOD ? 0.1f : 0.f);
		}
	}
	else if (bFarLOD)
	{
		bFarLOD = false;
		SetActorTickInterval(0.f);
	}
	const float Dt = FMath::Min(DeltaSeconds, bFarLOD ? 0.12f : 1.f / 20.f);
	// A parked vehicle (no rider, no AI) that has come to rest stays put: with the handbrake fighting the slope
	// term every frame a parked bike crept downhill onto the Doc Tham Ma road and held up the traffic at 250 m.
	if (!Controller && !bAutoDrive)
	{
		ParkedRestTime = (bGrounded && FMath::Abs(Speed) < 20.f) ? ParkedRestTime + Dt : 0.f;
		if (ParkedRestTime > 1.5f)
		{
			Speed = 0.f;
			return;
		}
	}
	else
	{
		ParkedRestTime = 0.f;
	}
	if (TurnState == EHGTurnState::Turning)
	{
		TickTurnaround(Dt);
		return;
	}
	// Far from the camera (> 450 m): ride the lane line kinematically. Nobody sees it, and simulated physics at
	// 10 Hz sent bikes 15-24 m off the Ta Lang descent (re-placed); this is also cheaper on the game thread.
	if (bFarLOD && bAutoDrive && AutoDriveRoad && !IsPlayerControlled())
	{
		TickFarLOD(Dt);
		return;
	}

	float Throttle = ReadAxis(ThrottleAction);
	float Steer = ReadAxis(SteerAction);
	bool bHandbrake = ReadButton(HandbrakeAction) || (!Controller && bGrounded && !bAutoDrive);
	const bool bBoost = ReadButton(BoostAction);
	if (bAutoDrive && (!IsPlayerControlled() || bAutoDriveWithPlayer))
	{
		ComputeAutoDriveInput(Dt, Throttle, Steer, bHandbrake);
	}
	else if (IsPlayerControlled())
	{
		if (const UHGHappyWaterSubsystem* HW = GetWorld()->GetSubsystem<UHGHappyWaterSubsystem>())
		{
			if (HW->AreControlsInverted())
			{
				Steer = -Steer; // rượu ngô: the road is spinning
			}
		}
		// Keys are digital: ramp the bars in over ~0.25 s like a rider leaning in, instead of snapping.
		SmoothedSteer = FMath::FInterpTo(SmoothedSteer, Steer, Dt, SteerResponse);
		Steer = SmoothedSteer;
	}
	LastThrottle = Throttle;
	UpdateSurface(Dt);

	const float SpeedAlpha = FMath::Clamp(FMath::Abs(Speed) / 600.f, 0.f, 1.f);
	const float HighSpeedScale = FMath::Lerp(1.f, HighSpeedSteerScale, FMath::Clamp(FMath::Abs(Speed) / MaxForwardSpeed, 0.f, 1.f));
	const float HandbrakeScale = bHandbrake ? 1.6f : 1.f;
	const float AirScale = bGrounded ? 1.f : AirSteerFactor;
	YawRateDeg = Steer * SteerRateDeg * SpeedAlpha * HighSpeedScale * HandbrakeScale * AirScale * FMath::Sign(Speed);
	HeadingYaw = FRotator::NormalizeAxis(HeadingYaw + YawRateDeg * Dt);

	const FVector FlatFwd = FRotator(0.f, HeadingYaw, 0.f).Vector();
	const FVector MoveDir = bGrounded ? FVector::VectorPlaneProject(FlatFwd, GroundNormal).GetSafeNormal() : FlatFwd;

	UpdateSpeed(Dt, Throttle, bHandbrake, bBoost, MoveDir);

	FVector Delta = MoveDir * Speed * Dt;
	if (!bGrounded)
	{
		VerticalVelocity += GetWorld()->GetGravityZ() * Dt;
		Delta.Z += VerticalVelocity * Dt;
	}

	const FVector Before = GetActorLocation();
	bMoveBlocked = false;
	MoveWithCollision(Delta, MoveDir);
	if (bMoveBlocked)
	{
		// Blocked: the speed is what we actually covered, so the speedo and engine note stop when we do.
		const float Along = FVector::DotProduct(GetActorLocation() - Before, MoveDir) / Dt;
		Speed = Speed > 0.f ? FMath::Min(Speed, FMath::Max(Along, 0.f)) : FMath::Max(Speed, FMath::Min(Along, 0.f));
	}
	SnapToGround(Dt, MoveDir);
	UpdateOrientation(Dt);
	UpdateVisuals(Dt, Steer);
	UpdateChaseCamera(Dt);

	// AI that keeps the throttle open without getting anywhere (wedged on a bank, nosed into a rock):
	// put it back on its lane a few metres on.
	if (bAutoDrive && AutoDriveRoad && (!IsPlayerControlled() || bAutoDriveWithPlayer))
	{
		const bool bNoProgress = FVector::Dist(Before, GetActorLocation()) < 30.f * Dt;
		StuckTime = (LastThrottle > 0.3f && bNoProgress) ? StuckTime + Dt : 0.f;
		if (StuckTime > 2.f)
		{
			StuckTime = 0.f;
			++TrafficStats.Unsticks;
			if (TrafficStats.Unsticks <= 40)
			{
				UE_LOG(LogHaGiang, Display, TEXT("[HG_VEH] unstick %s at road %.0f m (dir %d, %s, grounded %d, blocked %d by %s at lateral %+.2f m; we are at %+.2f m, lane %+.2f m)"), *GetName(),
					RoadDistance / 100.f, AutoDriveDirection, *SpeedReason, bGrounded, bMoveBlocked, LastBlocker.IsValid() ? *LastBlocker->GetActorNameOrLabel() : TEXT("-"),
					[this]()
					{
						const USplineComponent* Sp = AutoDriveRoad->GetSpline();
						const FVector T = Sp->GetDirectionAtDistanceAlongSpline(RoadDistance, ESplineCoordinateSpace::World).GetSafeNormal2D() * float(AutoDriveDirection);
						return FVector::DotProduct(LastBlockPoint - Sp->GetLocationAtDistanceAlongSpline(RoadDistance, ESplineCoordinateSpace::World),
							FVector::CrossProduct(FVector::UpVector, T).GetSafeNormal()) / 100.f;
					}(), LastLateral / 100.f, GetLaneOffsetAt(RoadDistance) / 100.f);
			}
			// Third time in a row at the same spot, or at the end of the road (the dock spur ends at the slipway):
			// turn round instead of hopping forward again.
			const double Now = GetWorld()->GetTimeSeconds();
			UnstickStreak = Now - LastUnstickTime < 15.0 ? UnstickStreak + 1 : 1;
			LastUnstickTime = Now;
			const float RoadLen = AutoDriveRoad->GetRoadLength();
			const bool bNearEnd = (AutoDriveDirection > 0 && RoadDistance > RoadLen - 8000.f) || (AutoDriveDirection < 0 && RoadDistance < 8000.f);
			if (bNearEnd || UnstickStreak >= 3)
			{
				UnstickStreak = 0;
				StartAutoDrive(AutoDriveRoad, RoadDistance, -AutoDriveDirection, AutoDriveCruise * 0.036f, AutoDriveLane);
			}
			else
			{
				StartAutoDrive(AutoDriveRoad, RoadDistance + 400.f * AutoDriveDirection, AutoDriveDirection, AutoDriveCruise * 0.036f, AutoDriveLane);
			}
		}
	}

	// Splash-down: a bike that lands in the river sinks; the rider bails out and swims,
	// the bike is recovered to its last spawn point a few seconds later.
	float SurfaceZ = 0.f;
	if (AHGWaterBody::FindSurface(GetWorld(), GetActorLocation(), SurfaceZ) && GetActorLocation().Z < SurfaceZ + 20.f)
	{
		SubmergedTime += Dt;
		Speed = FMath::FInterpTo(Speed, 0.f, Dt, 3.f);
		if (SubmergedTime > 0.25f && GetDriver())
		{
			ExitVehicle();
		}
		if (SubmergedTime > 6.f && !GetDriver())
		{
			SubmergedTime = 0.f;
			ResetToSpawn();
		}
	}
	else
	{
		SubmergedTime = 0.f;
	}

	if (GetActorLocation().Z < SpawnTransform.GetLocation().Z - KillDepth)
	{
		ResetToSpawn();
	}
}

// ---------------------------------------------------------------------------
// AI auto-drive
// ---------------------------------------------------------------------------

void AHGGroundVehicle::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	if (bAutoDrive && !bAutoDriveWithPlayer && NewController && NewController->IsPlayerController())
	{
		StopAutoDrive(); // the player stole it
	}
}

void AHGGroundVehicle::StartAutoDrive(AHGRoadSplineActor* Road, float DistanceCm, int32 Direction, float CruiseKmh, float LaneOffsetCm)
{
	if (!Road || !Road->GetSpline())
	{
		return;
	}
	USplineComponent* Spline = Road->GetSpline();
	if (AutoDriveRoad != Road)
	{
		bTurnPointsValid = false;
	}
	AutoDriveRoad = Road;
	AutoDriveDirection = Direction >= 0 ? 1 : -1;
	AutoDriveCruise = CruiseKmh / 0.036f;
	AutoDriveLane = LaneOffsetCm;
	RoadDistance = FMath::Clamp(DistanceCm, 0.f, Spline->GetSplineLength());

	PlaceOnRoad(Road, RoadDistance, AutoDriveDirection, GetLaneOffsetAt(RoadDistance));
	Speed = AutoDriveCruise * 0.5f;
	SpawnTransform = GetActorTransform();

	const bool bWas = bAutoDrive;
	bAutoDrive = true;
	if (!bWas)
	{
		OnAutoDriveChanged(true);
	}
}

void AHGGroundVehicle::PlaceOnRoad(AHGRoadSplineActor* Road, float DistanceCm, int32 Direction, float LaneOffsetCm)
{
	if (!Road || !Road->GetSpline())
	{
		return;
	}
	const USplineComponent* Spline = Road->GetSpline();
	const float D = FMath::Clamp(DistanceCm, 0.f, Spline->GetSplineLength());
	const FVector Tangent = Spline->GetDirectionAtDistanceAlongSpline(D, ESplineCoordinateSpace::World).GetSafeNormal2D() * float(Direction >= 0 ? 1 : -1);
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Tangent).GetSafeNormal();
	const FVector Loc = Spline->GetLocationAtDistanceAlongSpline(D, ESplineCoordinateSpace::World)
		+ Right * LaneOffsetCm + FVector(0.f, 0.f, Collision->GetScaledBoxExtent().Z + GroundClearance + 20.f);
	const FRotator Rot(0.f, Tangent.Rotation().Yaw, 0.f);
	SetActorLocationAndRotation(Loc, Rot, false, nullptr, ETeleportType::TeleportPhysics);
	HeadingYaw = Rot.Yaw;
	Speed = 0.f;
	YawRateDeg = 0.f;
	SmoothedSteer = 0.f;
	bGrounded = false;
	VerticalVelocity = 0.f;
	GroundNormal = FVector::UpVector;
	StuckTime = 0.f;
	LaneShift = LaneShiftTarget = 0.f;
	OvertakeTarget = nullptr;
	GoAroundTime = BlockedTime = 0.f;
	bYielding = false;
	TurnState = EHGTurnState::None;
	bQueuedForTurn = false;
	SnapCamera();
}

AHGRoadSplineActor* AHGGroundVehicle::FindNearestRoad(const FVector& Location, float& OutLateralCm, float& OutDistanceCm) const
{
	AHGRoadSplineActor* Best = nullptr;
	OutLateralCm = BIG_NUMBER;
	OutDistanceCm = 0.f;
	for (TActorIterator<AHGRoadSplineActor> It(GetWorld()); It; ++It)
	{
		if (!It->GetSpline())
		{
			continue;
		}
		const float D = It->GetDistanceAlongRoad(Location);
		const FVector P = It->GetSpline()->GetLocationAtDistanceAlongSpline(D, ESplineCoordinateSpace::World);
		const float Lat = FVector::Dist2D(P, Location);
		if (Lat < OutLateralCm)
		{
			OutLateralCm = Lat;
			OutDistanceCm = D;
			Best = *It;
		}
	}
	return Best;
}

void AHGGroundVehicle::RecoverToRoad()
{
	float Lateral = 0.f, Dist = 0.f;
	AHGRoadSplineActor* Road = FindNearestRoad(GetActorLocation(), Lateral, Dist);
	if (!Road || Lateral > 30000.f)
	{
		ResetToSpawn();
		return;
	}
	// Face whichever way along the road is closer to where we were heading; keep right (Vietnam).
	const FVector Tangent = Road->GetSpline()->GetDirectionAtDistanceAlongSpline(Dist, ESplineCoordinateSpace::World);
	const int32 Dir = FVector::DotProduct(Tangent, FRotator(0.f, HeadingYaw, 0.f).Vector()) >= 0.f ? 1 : -1;
	const float HalfWidth = Road->GetWidthAtDistance(Dist) * 0.5f;
	PlaceOnRoad(Road, Dist, Dir, FMath::Clamp(HalfWidth - 100.f, 0.f, 150.f));
}

void AHGGroundVehicle::UpdateSurface(float DeltaSeconds)
{
	if (!IsPlayerControlled())
	{
		Surface = EHGSurface::Asphalt;
		SurfaceScale = 1.f;
		bPlayerRoadTracked = false;
		bGripTyres = false;
		return;
	}
	SurfaceTimer -= DeltaSeconds;
	if (SurfaceTimer <= 0.f && bGrounded)
	{
		SurfaceTimer = 0.2f;
		// The road meshes float a few cm over the carved landscape, which carries the collision: on the
		// landscape but off the road's width = off-road. Any other ground (pads, bridges, the dock) is paved.
		const UPrimitiveComponent* Ground = GroundComponent.Get();
		const bool bOnLandscape = Ground && Ground->GetOwner() && Ground->GetOwner()->GetClass()->GetName().Contains(TEXT("Landscape"));
		float Lateral = 0.f, Dist = 0.f;
		const AHGRoadSplineActor* Road = FindNearestRoad(GetActorLocation(), Lateral, Dist);
		const bool bOnRoad = Road && Lateral <= Road->GetWidthAtDistance(Dist) * 0.5f + 60.f;
		// Let the AI see the player as traffic: road distance, direction of travel, side of the road.
		bPlayerRoadTracked = Road && Lateral < 1500.f;
		if (bPlayerRoadTracked)
		{
			const USplineComponent* Sp = Road->GetSpline();
			const FVector T = Sp->GetDirectionAtDistanceAlongSpline(Dist, ESplineCoordinateSpace::World).GetSafeNormal2D();
			const FVector Heading = FRotator(0.f, HeadingYaw, 0.f).Vector() * (Speed < 0.f ? -1.f : 1.f);
			PlayerTravelDir = FVector::DotProduct(Heading, T) >= 0.f ? 1 : -1;
			const FVector R = FVector::CrossProduct(FVector::UpVector, T * float(PlayerTravelDir)).GetSafeNormal();
			LastLateral = FVector::DotProduct(GetActorLocation() - Sp->GetLocationAtDistanceAlongSpline(Dist, ESplineCoordinateSpace::World), R);
			RoadDistance = Dist;
		}
		if (bOnRoad)
		{
			Surface = (Road->SecondaryFromDistance >= 0.f && Dist >= Road->SecondaryFromDistance) ? EHGSurface::Concrete : EHGSurface::Asphalt;
		}
		else
		{
			Surface = bOnLandscape ? EHGSurface::OffRoad : EHGSurface::Asphalt;
		}
	}
	// Grip tyres (mission list upgrade, player only): 90 % of top speed on the concrete, 65 % off the road.
	bGripTyres = IsPlayerControlled() && UHGMissionSubsystem::PlayerHasGripTyres(GetWorld());
	const float ConcreteScale = bGripTyres ? FMath::Max(ConcreteSpeedScale, 0.9f) : ConcreteSpeedScale;
	const float OffScale = bGripTyres ? FMath::Max(OffRoadSpeedScale, 0.65f) : OffRoadSpeedScale;
	const float Target = Surface == EHGSurface::Concrete ? ConcreteScale : Surface == EHGSurface::OffRoad ? OffScale : 1.f;
	SurfaceScale = FMath::FInterpTo(SurfaceScale, Target, DeltaSeconds, 2.f);
}

void AHGGroundVehicle::UpdateChaseCamera(float DeltaSeconds)
{
	if (!IsPlayerControlled() || !SpringArm || !Camera)
	{
		return;
	}
	const float Slope = bGrounded ? GetActorRotation().Pitch : 0.f;
	ChaseCamPitchCurrent = FMath::FInterpTo(ChaseCamPitchCurrent, ChaseCamPitch + ChaseCamSlopeFollow * Slope, DeltaSeconds, 3.f);
	UpdateOrbitCamera(DeltaSeconds, ChaseCamPitchCurrent, -60.f, 10.f);
	// Round 6A: a little shake on rough ground (concrete slab joints, gravel / dirt), growing with speed.
	{
		const float Rough = Surface == EHGSurface::OffRoad ? 1.f : Surface == EHGSurface::Concrete ? 0.45f : 0.08f;
		const float Amp = Rough * FMath::Clamp(FMath::Abs(Speed) / 1500.f, 0.f, 1.f) * (bGrounded ? 3.f : 0.f);
		ShakeClock += DeltaSeconds * (6.f + FMath::Abs(Speed) / 120.f);
		Camera->SetRelativeLocation(FVector(0.f, FMath::PerlinNoise1D(ShakeClock) * Amp, FMath::PerlinNoise1D(ShakeClock + 37.f) * Amp * 1.4f));
		LastShakeAmp = Amp;
	}
	const float SpeedFrac = FMath::Clamp(FMath::Abs(Speed) / FMath::Max(MaxForwardSpeed, 1.f), 0.f, 1.f);
	Camera->SetFieldOfView(FMath::FInterpTo(Camera->FieldOfView, FMath::Lerp(ChaseFovRest, ChaseFovFast, SpeedFrac * SpeedFrac), DeltaSeconds, 2.f));
}

void AHGGroundVehicle::StopAutoDrive()
{
	if (!bAutoDrive)
	{
		return;
	}
	bAutoDrive = false;
	AutoDriveLeader = nullptr;
	OnAutoDriveChanged(false);
}

float AHGGroundVehicle::GetLaneOffsetAt(float DistanceCm) const
{
	if (!AutoDriveRoad)
	{
		return AutoDriveLane;
	}
	// Keep the whole body (not just the centre) LaneEdgeMargin inside the edge: a 2.5 m truck on a 1.7 m lane
	// offset overhangs a 6 m road, and a bike on 1.7 m + 0.6 m convoy stagger is off the 4 m Ta Lang concrete.
	const float HalfWidth = Collision ? Collision->GetScaledBoxExtent().Y : 0.f;
	// Narrow down 15 m before the road does (the lane change lags the 20 m width blend otherwise).
	const float Width = FMath::Min(AutoDriveRoad->GetWidthAtDistance(DistanceCm),
		AutoDriveRoad->GetWidthAtDistance(DistanceCm + 1500.f * float(AutoDriveDirection)));
	const float MaxLane = FMath::Max(Width * 0.5f - LaneEdgeMargin - HalfWidth, 0.f);
	return FMath::Clamp(AutoDriveLane + LaneShift, -MaxLane, MaxLane);
}

float AHGGroundVehicle::GetMinRadiusAhead(float LookCm, float StepCm) const
{
	if (!AutoDriveRoad)
	{
		return BIG_NUMBER;
	}
	const USplineComponent* Spline = AutoDriveRoad->GetSpline();
	const float Len = Spline->GetSplineLength();
	float MinRadius = BIG_NUMBER;
	const float Step = FMath::Max(StepCm, 50.f);
	FVector Prev = Spline->GetDirectionAtDistanceAlongSpline(RoadDistance, ESplineCoordinateSpace::World);
	for (float D = Step; D <= LookCm; D += Step)
	{
		const float At = FMath::Clamp(RoadDistance + D * AutoDriveDirection, 0.f, Len);
		const FVector Dir = Spline->GetDirectionAtDistanceAlongSpline(At, ESplineCoordinateSpace::World);
		const float Angle = FMath::Acos(FMath::Clamp(FVector::DotProduct(Prev.GetSafeNormal2D(), Dir.GetSafeNormal2D()), -1.f, 1.f));
		if (Angle > KINDA_SMALL_NUMBER)
		{
			MinRadius = FMath::Min(MinRadius, Step / Angle);
		}
		Prev = Dir;
	}
	return MinRadius;
}

void AHGGroundVehicle::ScanTraffic(AHGGroundVehicle*& OutAhead, float& OutAheadGap, float& OutOncomingGap) const
{
	OutAhead = nullptr;
	OutAheadGap = BIG_NUMBER;
	OutOncomingGap = BIG_NUMBER;
	for (TActorIterator<AHGGroundVehicle> It(GetWorld()); It; ++It)
	{
		AHGGroundVehicle* V = *It;
		if (V == this || !V->HasRoadPosition())
		{
			continue;
		}
		const float Gap = (V->GetRoadDistance() - RoadDistance) * float(AutoDriveDirection);
		if (Gap < 0.f || Gap > 15000.f)
		{
			continue;
		}
		if (V->GetTravelDirection() == AutoDriveDirection)
		{
			// Same way: "ahead of us" only if it is in the lane we are in now (an overtaken bike in the right
			// lane doesn't block us in the left one).
			if (FMath::Abs(V->GetLateralOffset() - LastLateral) < 170.f && Gap < OutAheadGap)
			{
				OutAheadGap = Gap;
				OutAhead = V;
			}
		}
		else
		{
			OutOncomingGap = FMath::Min(OutOncomingGap, Gap);
		}
	}
}

void AHGGroundVehicle::ComputeAutoDriveInput(float DeltaSeconds, float& OutThrottle, float& OutSteer, bool& OutHandbrake)
{
	OutThrottle = 0.f;
	OutSteer = 0.f;
	OutHandbrake = false;
	if (!AutoDriveRoad)
	{
		return;
	}
	USplineComponent* Spline = AutoDriveRoad->GetSpline();
	const float Len = Spline->GetSplineLength();

	// Track our distance along the road: integrate, re-sync to the true closest point a few times a second.
	RoadDistance += Speed * DeltaSeconds * AutoDriveDirection;
	RoadResyncTimer -= DeltaSeconds;
	if (RoadResyncTimer <= 0.f)
	{
		RoadResyncTimer = 0.25f;
		RoadDistance = AutoDriveRoad->GetDistanceAlongRoad(GetActorLocation());
	}
	// Every tick: one Newton step (project onto the road tangent) so the cross-track error below is exact.
	{
		const FVector P = Spline->GetLocationAtDistanceAlongSpline(RoadDistance, ESplineCoordinateSpace::World);
		const FVector T = Spline->GetDirectionAtDistanceAlongSpline(RoadDistance, ESplineCoordinateSpace::World).GetSafeNormal2D();
		RoadDistance = FMath::Clamp(RoadDistance + FVector::DotProduct((GetActorLocation() - P) * FVector(1.f, 1.f, 0.f), T), 0.f, Len);
	}

	// End of the road (or of our stretch of it): loop or turn around.
	const float RangeLo = AutoDriveRangeMin >= 0.f ? FMath::Max(AutoDriveRangeMin, 1500.f) : 1500.f;
	const float RangeHi = AutoDriveRangeMax >= 0.f ? FMath::Min(AutoDriveRangeMax, Len - 1500.f) : Len - 1500.f;
	if (LoopRestartDistance >= 0.f)
	{
		if ((AutoDriveDirection > 0 && RoadDistance > RangeHi) || (AutoDriveDirection < 0 && RoadDistance < RangeLo))
		{
			StartAutoDrive(AutoDriveRoad, LoopRestartDistance, AutoDriveDirection, AutoDriveCruise * 0.036f, AutoDriveLane);
			return;
		}
	}
	// Turning round: not a teleport into the other lane any more (that dropped the leader of a convoy nose to
	// nose with its own riders - the Tham Ma "dead stop" - and trucks into the bank). Each end of the stretch
	// has a turn-round spot (wide, straight, level; lay-by ground for trucks); the vehicle stops there, waits
	// until both lanes are clear and drives a U-turn arc across the road.
	if (!bTurnPointsValid && LoopRestartDistance < 0.f)
	{
		bTurnPointsValid = true;
		TurnPointLo = FindTurnPoint(RangeLo, +1);
		TurnPointHi = FindTurnPoint(RangeHi, -1);
	}
	const float TurnAt = AutoDriveDirection > 0 ? TurnPointHi : TurnPointLo;
	const float ToTurn = LoopRestartDistance >= 0.f ? BIG_NUMBER : (TurnAt - RoadDistance) * float(AutoDriveDirection);
	bQueuedForTurn = false;
	if (TurnState == EHGTurnState::None && ToTurn < 150.f)
	{
		TurnState = EHGTurnState::Waiting;
		TurnWaitTime = 0.f;
		TrafficScanTimer = 0.f;
	}
	if (TurnState == EHGTurnState::Waiting)
	{
		TurnWaitTime += DeltaSeconds;
		TrafficStats.LongestTurnWait = FMath::Max(TrafficStats.LongestTurnWait, TurnWaitTime);
		OutThrottle = FMath::Clamp(-Speed / 200.f, -1.f, 1.f);
		OutHandbrake = FMath::Abs(Speed) < 60.f;
		SpeedReason = FString::Printf(TEXT("turn-round wait %.0f s"), TurnWaitTime);
		TrafficScanTimer -= DeltaSeconds;
		if (TrafficScanTimer <= 0.f)
		{
			TrafficScanTimer = 0.25f;
			if (FMath::Abs(Speed) < 80.f && IsTurnClear())
			{
				BeginTurnaround();
			}
		}
		return;
	}

	// Lane keeping. This used to chase a point 13-17 m ahead on the lane, which cuts the inside of every bend
	// by R - sqrt(R^2 - L^2) (4.6 m on a 30 m curve; off the road on the 8 m Ta Lang hairpins and the 4 m
	// concrete). Now: steer for the road heading slightly ahead (covers the steering lag) minus a cross-track
	// correction towards our lane line (Stanley controller).
	const float Dir = float(AutoDriveDirection);
	const FVector RoadP = Spline->GetLocationAtDistanceAlongSpline(RoadDistance, ESplineCoordinateSpace::World);
	const FVector RoadT = Spline->GetDirectionAtDistanceAlongSpline(RoadDistance, ESplineCoordinateSpace::World).GetSafeNormal2D() * Dir;
	const FVector Right = FVector::CrossProduct(FVector::UpVector, RoadT).GetSafeNormal();
	const float CrossTrack = FVector::DotProduct(GetActorLocation() - (RoadP + Right * GetLaneOffsetAt(RoadDistance)), Right); // > 0: right of lane
	LastCrossTrack = CrossTrack;
	LastLateral = FVector::DotProduct(GetActorLocation() - RoadP, Right);
	// Curvature-aware preview: on the 9 m hairpins a full-length preview turned the bars in a whole bike length
	// early and the rider cut onto the inside kerb (chevron boards, 267 m / 661 m - re-placed). Shorter preview
	// in tight bends, and a much stiffer pull back once the body is within 30 cm of the road edge.
	const float NearRadius = GetMinRadiusAhead(1500.f, 300.f);
	const float PreviewScale = FMath::Clamp(NearRadius / 2500.f, 0.35f, 1.f);
	const float PreviewD = FMath::Clamp(RoadDistance + Dir * FMath::Max(FMath::Abs(Speed) * LanePreviewSeconds * PreviewScale, 120.f), 0.f, Len);
	const FVector Ahead = Spline->GetDirectionAtDistanceAlongSpline(PreviewD, ESplineCoordinateSpace::World) * Dir;
	const float EdgeRoom = AutoDriveRoad->GetWidthAtDistance(RoadDistance) * 0.5f - Collision->GetScaledBoxExtent().Y - FMath::Abs(LastLateral);
	const float Gain = EdgeRoom < 30.f ? LaneKeepGain * 3.f : LaneKeepGain;
	const float DesiredYaw = Ahead.Rotation().Yaw - FMath::RadiansToDegrees(FMath::Atan2(Gain * CrossTrack, FMath::Abs(Speed) + 300.f));
	const float YawError = FRotator::NormalizeAxis(DesiredYaw - HeadingYaw);
	OutSteer = FMath::Clamp(YawError / 22.f, -1.f, 1.f);

	// Speed: cruise, slowed for curves (v = sqrt(a * R)) and by the vehicle ahead.
	float TargetSpeed = AutoDriveCruise;
	// Radius of our lane, assuming it is on the inside of the bend (conservative); 3 m steps resolve the
	// 8 m Ta Lang hairpins that 6 m steps under-sample.
	const float Radius = FMath::Max(GetMinRadiusAhead(3600.f, 300.f) - FMath::Abs(GetLaneOffsetAt(RoadDistance)), 150.f);
	TargetSpeed = FMath::Min(TargetSpeed, FMath::Sqrt(AutoDriveLateralAccel * Radius));
	SpeedReason = TargetSpeed < AutoDriveCruise ? TEXT("curve") : TEXT("cruise");
	if (ToTurn < 5000.f)
	{
		const float Stop = FMath::Sqrt(2.f * 250.f * FMath::Max(ToTurn - 100.f, 0.f)) + 80.f;
		if (Stop < TargetSpeed)
		{
			TargetSpeed = Stop;
			SpeedReason = TEXT("turn-round ahead");
		}
	}

	// ---- Traffic: overtaking, yielding on hairpins, going round stopped things (4 Hz scan) ----
	TrafficScanTimer -= DeltaSeconds;
	if (TrafficScanTimer <= 0.f)
	{
		TrafficScanTimer = 0.25f;
		AHGGroundVehicle* AheadV = nullptr;
		ScanTraffic(AheadV, CachedAheadGap, CachedOncomingGap);
		CachedAhead = AheadV;
		const bool bNarrow = AutoDriveRoad->GetWidthAtDistance(RoadDistance) < 500.f;
		// Round 6A: a beep before a blind hairpin, as riders and drivers do on every bend in Ha Giang (convoy
		// leaders and trucks only - not eight bikes in a row).
		if (!AutoDriveLeader.IsValid() && Speed > 300.f && GetMinRadiusAhead(4000.f, 300.f) < 1500.f && FMath::FRand() < 0.2f)
		{
			SoundHorn();
		}

		// Overtake a slower vehicle in our lane (not our own convoy leader) on a clear, open stretch: the left
		// lane must be free of oncoming traffic for 150 m and the road ahead no tighter than R 40 m.
		if (AHGGroundVehicle* Target = OvertakeTarget.Get())
		{
			const float G = (Target->GetRoadDistance() - RoadDistance) * Dir;
			const bool bPassed = G < -1200.f;
			const bool bAbort = CachedOncomingGap < 7000.f && G > -400.f;
			if (bPassed || bAbort || OvertakeTime > 15.f)
			{
				OvertakeTarget = nullptr;
				LaneShiftTarget = 0.f;
			}
		}
		else if (AheadV && AheadV != AutoDriveLeader.Get() && !AheadV->IsWaitingForTurn() && !AheadV->IsTurningAround() && !bNarrow && CachedAheadGap < 3500.f
			&& AheadV->GetSpeed() < AutoDriveCruise - 250.f && CachedOncomingGap > 15000.f
			&& GetMinRadiusAhead(9000.f, 600.f) > 4000.f && GoAroundTime <= 0.f)
		{
			OvertakeTarget = AheadV;
			OvertakeTime = 0.f;
			LaneShiftTarget = -2.f * AutoDriveLane;   // mirror into the left lane
			++TrafficStats.Overtakes;
		}

		// Hairpin etiquette: with oncoming traffic within 70 m and a hairpin coming, the one going downhill
		// slows to walking pace and hugs its edge; the one climbing keeps its momentum.
		const bool bWasYielding = bYielding;
		bYielding = false;
		if (CachedOncomingGap < 7000.f && GetMinRadiusAhead(5000.f, 300.f) < 1500.f && !OvertakeTarget.IsValid())
		{
			const float ZNow = Spline->GetLocationAtDistanceAlongSpline(RoadDistance, ESplineCoordinateSpace::World).Z;
			const float ZAhead = Spline->GetLocationAtDistanceAlongSpline(FMath::Clamp(RoadDistance + Dir * 2000.f, 0.f, Len), ESplineCoordinateSpace::World).Z;
			bYielding = ZAhead < ZNow - 50.f;
		}
		if (bYielding && !bWasYielding)
		{
			++TrafficStats.Yields;
		}
		if (!OvertakeTarget.IsValid() && GoAroundTime <= 0.f)
		{
			LaneShiftTarget = bYielding ? 80.f : 0.f;
		}
	}
	OvertakeTime += DeltaSeconds;
	GoAroundTime = FMath::Max(0.f, GoAroundTime - DeltaSeconds);
	bHeadOnSqueeze = bHeadOnSqueeze && GoAroundTime > 0.f;
	if (GoAroundTime <= 0.f && !OvertakeTarget.IsValid() && !bYielding)
	{
		LaneShiftTarget = 0.f;
	}
	// Round 6A: Easy Rider convoys pull over at the viewpoints for ~18 s - riders off, photos, back on, ride on.
	// The leader decides (half the passes); its riders stop behind it on the edge of the road.
	if (!bViewpointsCached)
	{
		bViewpointsCached = true;
		for (TActorIterator<AHGViewpointActor> It(GetWorld()); It; ++It)
		{
			const float D = AutoDriveRoad->GetDistanceAlongRoad(It->GetActorLocation());
			const FVector P = Spline->GetLocationAtDistanceAlongSpline(D, ESplineCoordinateSpace::World);
			if (FVector::Dist2D(P, It->GetActorLocation()) < 6000.f && D > 3000.f && D < Len - 20000.f)
			{
				ViewpointRoadD.Add(D);
				ViewpointLook.Add(It->OrbitCenter.IsNearlyZero() ? It->GetActorLocation() : It->OrbitCenter);
			}
		}
	}
	if (ViewpointStopTime > 0.f)
	{
		ViewpointStopTime -= DeltaSeconds;
		if (ViewpointStopTime <= 0.f)
		{
			OnViewpointStop(false, FVector::ZeroVector);
		}
	}
	else if (WantsViewpointStops() && TurnState == EHGTurnState::None && !IsPlayerControlled())
	{
		const AHGGroundVehicle* L = AutoDriveLeader.Get();
		const bool bLeaderStopping = L && L->IsViewpointStopping() && L->GetAutoDriveDirection() == AutoDriveDirection
			&& (L->GetRoadDistance() - RoadDistance) * Dir < ConvoyGap * 2.5f;
		for (int32 i = 0; i < ViewpointRoadD.Num(); ++i)
		{
			const float VpAhead = (ViewpointRoadD[i] - RoadDistance) * Dir;
			if (i == LastViewpointStop || VpAhead > 600.f || VpAhead < -2500.f)
			{
				continue;
			}
			LastViewpointStop = i;
			const bool bStop = bLeaderStopping || (!L && FMath::FRand() < 0.5f);
			if (bStop)
			{
				ViewpointStopTime = bLeaderStopping ? FMath::Max(L->ViewpointStopTime - 1.f, 6.f) : 18.f;
				++ViewpointStops;
				OnViewpointStop(true, ViewpointLook[i]);
			}
			break;
		}
	}
	if (ViewpointStopTime > 0.f)
	{
		LaneShiftTarget = 250.f;   // pull in to the edge (clamped by GetLaneOffsetAt)
	}
	LaneShift = FMath::FInterpConstantTo(LaneShift, LaneShiftTarget, DeltaSeconds, 140.f);

	if (OvertakeTarget.IsValid())
	{
		TargetSpeed = FMath::Max(TargetSpeed, FMath::Min(AutoDriveCruise * 1.2f, FMath::Sqrt(AutoDriveLateralAccel * Radius)));
	}
	if (bYielding)
	{
		TargetSpeed = FMath::Min(TargetSpeed, 350.f);   // ~12 km/h
		SpeedReason = TEXT("yield");
	}

	// A cow or goat our box ran into beside the probe (it stood at the lane edge): shoo it off too (a truck sat
	// behind the 3.24 km herd at 0 km/h with "cruise" as its reason).
	if (bMoveBlocked && FMath::Abs(Speed) < 60.f)
	{
		if (AHGCowActor* Cow = Cast<AHGCowActor>(LastBlocker.Get()))
		{
			const float CowLat = FVector::DotProduct(Cow->GetActorLocation() - RoadP, Right);
			Cow->ShooOff(Right * (CowLat >= 0.f ? 1.f : -1.f), 4.f);
		}
	}
	if (ViewpointStopTime > 0.f)
	{
		TargetSpeed = 0.f;
		SpeedReason = TEXT("viewpoint stop");
	}
	else if (const AHGGroundVehicle* Leader = AutoDriveLeader.Get())
	{
		if (Leader->IsAutoDriving() && Leader->GetAutoDriveDirection() == AutoDriveDirection && !OvertakeTarget.IsValid())
		{
			const float Gap = (Leader->GetRoadDistance() - RoadDistance) * AutoDriveDirection;
			const float Desired = ConvoyGap + FMath::Abs(Speed) * 0.5f;
			if (Gap > 0.f)
			{
				// Close up when far behind, back off when too close.
				TargetSpeed = FMath::Clamp(Leader->GetSpeed() + (Gap - Desired) * 0.8f, 0.f, AutoDriveCruise * 1.25f);
				bQueuedForTurn = (Leader->IsWaitingForTurn() || Leader->IsTurningAround()) && Gap < Desired + 1500.f;
				SpeedReason = FString::Printf(TEXT("leader %s gap %.0f m (%s)"), *Leader->GetName(), Gap / 100.f, *Leader->GetSpeedReason().Left(40));
			}
		}
	}

	// Anything (player, cows, people, traffic) on our lane ahead: brake. The probe follows the lane line
	// (two legs) instead of a straight line from the nose, which on every bend pointed at bikes parked on the
	// outside shoulder and tourists at the viewpoints and stopped convoys dead there.
	const float Probe = 600.f + FMath::Abs(Speed) * 0.9f;
	const float ActorAboveRoad = Collision->GetScaledBoxExtent().Z + GroundClearance;
	auto LanePoint = [&](float AheadCm)
	{
		const float At = FMath::Clamp(RoadDistance + Dir * AheadCm, 0.f, Len);
		const FVector T = Spline->GetDirectionAtDistanceAlongSpline(At, ESplineCoordinateSpace::World).GetSafeNormal2D() * Dir;
		const FVector R = FVector::CrossProduct(FVector::UpVector, T).GetSafeNormal();
		return Spline->GetLocationAtDistanceAlongSpline(At, ESplineCoordinateSpace::World) + R * GetLaneOffsetAt(At) + FVector(0.f, 0.f, ActorAboveRoad);
	};
	const float NoseCm = Collision->GetScaledBoxExtent().X + 50.f;
	const FVector P0 = GetActorLocation() + GetActorForwardVector() * NoseCm;
	const FVector P1 = LanePoint(NoseCm + Probe * 0.5f);
	const FVector P2 = LanePoint(NoseCm + Probe);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(HGAutoDriveProbe), false, this);
	FHitResult Hit;
	// As wide as the vehicle (trucks: a cow at the lane edge was outside a 70 cm probe and inside the 2.5 m box).
	const float ProbeRadius = FMath::Max(70.f, Collision->GetScaledBoxExtent().Y + 10.f);
	bool bHit = GetWorld()->SweepSingleByObjectType(Hit, P0, P1, FQuat::Identity, FCollisionObjectQueryParams(ECC_Pawn), FCollisionShape::MakeSphere(ProbeRadius), Params);
	float HitDist = bHit ? Hit.Distance : 0.f;
	if (!bHit)
	{
		bHit = GetWorld()->SweepSingleByObjectType(Hit, P1, P2, FQuat::Identity, FCollisionObjectQueryParams(ECC_Pawn), FCollisionShape::MakeSphere(ProbeRadius), Params);
		HitDist = FVector::Dist(P0, P1) + (bHit ? Hit.Distance : 0.f);
	}
	if (bHit)
	{
		const float Block = FMath::Clamp(HitDist / Probe, 0.f, 1.f);
		if (AutoDriveCruise * Block * Block < TargetSpeed)
		{
			TargetSpeed = AutoDriveCruise * Block * Block;
			SpeedReason = FString::Printf(TEXT("probe %s at %.1f m"), Hit.GetActor() ? *Hit.GetActor()->GetActorNameOrLabel() : TEXT("?"), HitDist / 100.f);
		}
		OnAutoDriveObstacle(Hit.GetActor());
		// A cow standing in the lane: after 2 s it is shooed off to its own side of the road (it used to stand there
		// while a truck or a whole convoy waited - 2 "dead stops" at the 3.24 km herd).
		if (AHGCowActor* Cow = Cast<AHGCowActor>(Hit.GetActor()))
		{
			CowWaitTime = FMath::Abs(Speed) < 150.f ? CowWaitTime + DeltaSeconds : 0.f;
			if (CowWaitTime > 2.f)
			{
				const float CowLat = FVector::DotProduct(Cow->GetActorLocation() - RoadP, Right);
				Cow->ShooOff(Right * (CowLat >= 0.f ? 1.f : -1.f), FMath::Max(2.f, (AutoDriveRoad->GetWidthAtDistance(RoadDistance) * 0.5f - FMath::Abs(CowLat)) / 100.f + 2.5f));
				CowWaitTime = 0.f;
			}
		}
		if (IsPlayerControlled() && GetWorld()->GetTimeSeconds() - LastBlockLogTime > 1.0)
		{
			LastBlockLogTime = GetWorld()->GetTimeSeconds();
			UE_LOG(LogHaGiang, Log, TEXT("[HG_VEH] %s auto-drive braking for %s at %.0f m"), *GetName(), *GetNameSafe(Hit.GetActor()), HitDist / 100.f);
		}
		// Something standing still in our lane (a parked bike, a tourist taking a photo, a stalled vehicle):
		// after 3 s go round it on the left if nothing is coming, like a rider would.
		const AActor* Obstacle = Hit.GetActor();
		const bool bStill = Obstacle && Obstacle->GetVelocity().Size() < 50.f;
		BlockedTime = (bStill && FMath::Abs(Speed) < 150.f) ? BlockedTime + DeltaSeconds : 0.f;
		// Nose to nose with an oncoming vehicle that is also stopped (one of them strayed into the other's lane on a
		// hairpin): both squeeze to their own edge, which opens the gap - going "round" to the left would only meet
		// head-on again, and waiting for the other never ends.
		const AHGGroundVehicle* Other = Cast<AHGGroundVehicle>(Obstacle);
		if (Other && (Other->IsWaitingForTurn() || Other->IsTurningAround()))
		{
			// Queued behind someone turning round: wait in line (going round would drive into its arc).
			bQueuedForTurn = true;
			BlockedTime = 0.f;
		}
		const bool bHeadOn = Other && Other->HasRoadPosition() && Other->GetTravelDirection() != AutoDriveDirection;
		// Going round a small still obstacle (a parked bike, a person) needs less clear road than a truck-sized one.
		const bool bSmallObstacle = Obstacle && Obstacle->GetComponentsBoundingBox().GetExtent().Size2D() < 200.f;
		if (BlockedTime > 2.f && bHeadOn && GoAroundTime <= 0.f)
		{
			GoAroundTime = 8.f;
			LaneShiftTarget = 250.f;   // clamped to the road edge by GetLaneOffsetAt
			BlockedTime = 0.f;
			bHeadOnSqueeze = true;
			++TrafficStats.GoArounds;
		}
		// Squeezing past: a bike can only move sideways by rolling forward, and the probe (which still sees the
		// other one) held both at 0 km/h with their wheels turned - the Tham Ma hairpin deadlock. Creep at
		// walking pace; the collision sweep keeps them from overlapping.
		if (bHeadOn && bHeadOnSqueeze && GoAroundTime > 0.f && TargetSpeed < 150.f)
		{
			TargetSpeed = 150.f;
			SpeedReason = FString::Printf(TEXT("squeeze past %s"), *Other->GetName());
		}
		else if (BlockedTime > 3.f && CachedOncomingGap > (bSmallObstacle ? 3500.f : 6000.f) && GoAroundTime <= 0.f)
		{
			GoAroundTime = 6.f;
			// Round it in the mirror lane (lateral -lane), not 1.5 m past it: that put bikes on the inside kerb of
			// the 9 m hairpins, into the chevron boards.
			LaneShiftTarget = -2.f * FMath::Abs(AutoDriveLane);
			BlockedTime = 0.f;
			++TrafficStats.GoArounds;
		}
	}
	else
	{
		BlockedTime = 0.f;
	}

	const float SpeedError = TargetSpeed - Speed;
	OutThrottle = FMath::Clamp(SpeedError / 300.f, -1.f, 1.f);
	OutHandbrake = TargetSpeed < 30.f && FMath::Abs(Speed) < 60.f;
}

// ---------------------------------------------------------------------------
// AI turning round
// ---------------------------------------------------------------------------

float AHGGroundVehicle::FindTurnPoint(float FromCm, int32 InwardDir) const
{
	if (!AutoDriveRoad)
	{
		return FromCm;
	}
	const bool bLong = Collision->GetScaledBoxExtent().X > 200.f;   // trucks
	// Every vehicle with the same stretch and size gets the same answer: cache it (ambient traffic re-ranges often).
	static TMap<FString, float> Cache;
	const FString Key = FString::Printf(TEXT("%s|%d|%d|%d"), *AutoDriveRoad->GetName(), FMath::RoundToInt(FromCm), InwardDir, bLong ? 1 : 0);
	if (const float* Found = Cache.Find(Key))
	{
		return *Found;
	}
	const USplineComponent* Sp = AutoDriveRoad->GetSpline();
	const float Len = Sp->GetSplineLength();
	const float NeedWidth = bLong ? 650.f : 420.f;
	const float NeedRadius = bLong ? 6000.f : 2000.f;
	const float Half = bLong ? 2000.f : 1000.f;
	const float MaxGrade = bLong ? 0.07f : 0.10f;
	// Ground beside the lane that has to be level with the road: trucks sweep their nose and tail over the
	// shoulders in the middle of the turn (a lay-by); bikes stay on the asphalt.
	const float Side = bLong ? 550.f : 330.f;
	const float FlatTol = bLong ? 120.f : 80.f;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(HGTurnPoint), false, this);
	float Result = FromCm;
	for (int32 Pass = 0; Pass < 2 && Result == FromCm; ++Pass)
	{
		for (float K = 0.f; K <= 60000.f; K += 500.f)
		{
			const float D = FromCm + float(InwardDir) * K;
			if (D - Half < 0.f || D + Half > Len)
			{
				continue;
			}
			if (AutoDriveRoad->GetWidthAtDistance(D) < NeedWidth)
			{
				continue;
			}
			const FVector T0 = Sp->GetDirectionAtDistanceAlongSpline(D - Half, ESplineCoordinateSpace::World).GetSafeNormal2D();
			const FVector T1 = Sp->GetDirectionAtDistanceAlongSpline(D, ESplineCoordinateSpace::World).GetSafeNormal2D();
			const FVector T2 = Sp->GetDirectionAtDistanceAlongSpline(D + Half, ESplineCoordinateSpace::World).GetSafeNormal2D();
			const float Turn = FMath::Acos(FMath::Clamp(FVector::DotProduct(T0, T1), -1.f, 1.f)) + FMath::Acos(FMath::Clamp(FVector::DotProduct(T1, T2), -1.f, 1.f));
			if (Turn > KINDA_SMALL_NUMBER && 2.f * Half / Turn < NeedRadius)
			{
				continue;
			}
			const FVector P0 = Sp->GetLocationAtDistanceAlongSpline(D - Half, ESplineCoordinateSpace::World);
			const FVector P1 = Sp->GetLocationAtDistanceAlongSpline(D, ESplineCoordinateSpace::World);
			const FVector P2 = Sp->GetLocationAtDistanceAlongSpline(D + Half, ESplineCoordinateSpace::World);
			if (FMath::Abs(P1.Z - P0.Z) > MaxGrade * Half || FMath::Abs(P2.Z - P1.Z) > MaxGrade * Half)
			{
				continue;
			}
			bool bFlat = true;
			if (Pass == 0)
			{
				const FVector R = FVector::CrossProduct(FVector::UpVector, T1).GetSafeNormal();
				for (const float Lat : { -Side, Side })
				{
					for (const float Along : { -500.f, 0.f, 500.f })
					{
						const FVector At = P1 + R * Lat + T1 * Along;
						FHitResult Hit;
						if (!GetWorld()->LineTraceSingleByChannel(Hit, At + FVector(0.f, 0.f, 1500.f), At - FVector(0.f, 0.f, 3000.f), ECC_Visibility, Params)
							|| FMath::Abs(Hit.ImpactPoint.Z - At.Z) > FlatTol)
						{
							bFlat = false;
							break;
						}
					}
					if (!bFlat)
					{
						break;
					}
				}
			}
			if (bFlat)
			{
				Result = D;
				break;
			}
		}
	}
	UE_LOG(LogHaGiang, Log, TEXT("[HG_VEH] turn-round spot for %s: %.0f m (stretch end %.0f m)"), bLong ? TEXT("trucks") : TEXT("bikes"), Result / 100.f, FromCm / 100.f);
	Cache.Add(Key, Result);
	return Result;
}

FVector AHGGroundVehicle::TurnPathPoint(float S, FVector* OutTangent) const
{
	// Half an ellipse: TurnReach forward, TurnShift to the left; heading turns from TurnFwd to -TurnFwd.
	const float Th = PI * FMath::Clamp(S, 0.f, 1.f);
	if (OutTangent)
	{
		*OutTangent = TurnFwd * (TurnReach * FMath::Cos(Th)) + TurnLeft * (0.5f * TurnShift * FMath::Sin(Th));
	}
	return TurnOrigin + TurnFwd * (TurnReach * FMath::Sin(Th)) + TurnLeft * (0.5f * TurnShift * (1.f - FMath::Cos(Th)));
}

bool AHGGroundVehicle::IsTurnClear()
{
	const bool bLong = Collision->GetScaledBoxExtent().X > 200.f;
	TurnOrigin = GetActorLocation();
	TurnFwd = FRotator(0.f, HeadingYaw, 0.f).Vector();
	TurnLeft = -FVector::CrossProduct(FVector::UpVector, TurnFwd).GetSafeNormal();
	// From where we stand (right of centre) to our lane line on the other side.
	TurnShift = FMath::Max(LastLateral + FMath::Abs(GetLaneOffsetAt(RoadDistance)), 150.f);
	TurnReach = bLong ? 750.f : 280.f;
	TurnDuration = bLong ? 9.f : 3.2f;

	const float Dir = float(AutoDriveDirection);
	for (TActorIterator<AHGGroundVehicle> It(GetWorld()); It; ++It)
	{
		const AHGGroundVehicle* V = *It;
		if (V == this || !V->HasRoadPosition())
		{
			continue;
		}
		const float G = (V->GetRoadDistance() - RoadDistance) * Dir;
		if (V->IsTurningAround() && FMath::Abs(G) < 4000.f)
		{
			return false;   // one U-turn at a time on a stretch of road
		}
		if (V->GetTravelDirection() != AutoDriveDirection)
		{
			// Coming down the lane we turn into: let it pass (or, stopped, be well clear of the arc).
			const bool bMoving = FMath::Abs(V->GetSpeed()) > 150.f;
			if (G > -1500.f && G < (bMoving ? 7000.f : TurnReach + 1500.f))
			{
				return false;
			}
		}
		else if (G > 50.f && G < 3000.f)
		{
			return false;   // someone just ahead in our lane: the arc crosses it
		}
	}
	// People, cows, the player on foot: anything on the arc.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(HGTurnClear), false, this);
	const float Rad = Collision->GetScaledBoxExtent().Y + 80.f;
	for (const float S : { 0.25f, 0.5f, 0.75f, 1.f })
	{
		if (GetWorld()->OverlapAnyTestByObjectType(TurnPathPoint(S, nullptr), FQuat::Identity, FCollisionObjectQueryParams(ECC_Pawn), FCollisionShape::MakeSphere(Rad), Params))
		{
			return false;
		}
	}
	return true;
}

void AHGGroundVehicle::BeginTurnaround()
{
	TurnState = EHGTurnState::Turning;
	TurnTime = 0.f;
	Speed = 0.f;
	LaneShift = LaneShiftTarget = 0.f;
	OvertakeTarget = nullptr;
	if (TrafficStats.UTurns < 60)
	{
		UE_LOG(LogHaGiang, Log, TEXT("[HG_VEH] %s turns round at road %.0f m after waiting %.1f s"), *GetName(), RoadDistance / 100.f, TurnWaitTime);
	}
}

void AHGGroundVehicle::TickTurnaround(float DeltaSeconds)
{
	TurnTime += DeltaSeconds;
	const float A = FMath::Clamp(TurnTime / TurnDuration, 0.f, 1.f);
	const float S = A * A * (3.f - 2.f * A);
	FVector Tangent;
	const FVector P = TurnPathPoint(S, &Tangent);
	const FVector Before = GetActorLocation();
	float Z = Before.Z;
	FHitResult Hit;
	if (TraceGround(FVector(P.X, P.Y, Before.Z), Hit))
	{
		Z = Hit.ImpactPoint.Z + Collision->GetScaledBoxExtent().Z + GroundClearance;
		GroundNormal = Hit.ImpactNormal;
		bGrounded = true;
	}
	SetActorLocation(FVector(P.X, P.Y, Z), false, nullptr, ETeleportType::None);
	if (!Tangent.IsNearlyZero())
	{
		HeadingYaw = Tangent.Rotation().Yaw;
	}
	Speed = FVector::Dist2D(Before, GetActorLocation()) / FMath::Max(DeltaSeconds, KINDA_SMALL_NUMBER);
	LastThrottle = 0.2f;
	UpdateOrientation(DeltaSeconds);
	UpdateVisuals(DeltaSeconds, -0.8f);
	UpdateChaseCamera(DeltaSeconds);
	if (A >= 1.f)
	{
		TurnState = EHGTurnState::None;
		AutoDriveDirection = -AutoDriveDirection;
		RoadDistance = AutoDriveRoad ? AutoDriveRoad->GetDistanceAlongRoad(GetActorLocation()) : RoadDistance;
		RoadResyncTimer = 0.25f;
		Speed = 150.f;
		YawRateDeg = 0.f;
		BlockedTime = GoAroundTime = 0.f;
		StuckTime = 0.f;
		bYielding = false;
		++TrafficStats.UTurns;
	}
}

void AHGGroundVehicle::TickFarLOD(float DeltaSeconds)
{
	USplineComponent* Spline = AutoDriveRoad->GetSpline();
	const float Len = Spline->GetSplineLength();
	if (!bTurnPointsValid)
	{
		float Unused0, Unused1;
		ComputeAutoDriveInput(0.f, Unused0, Unused1, bFarHandbrakeScratch);   // sets up the turn points
	}
	// Speed: cruise, curves, the convoy leader.
	float Target = FMath::Min(AutoDriveCruise, FMath::Sqrt(AutoDriveLateralAccel * FMath::Max(GetMinRadiusAhead(2400.f, 600.f), 150.f)));
	if (const AHGGroundVehicle* Leader = AutoDriveLeader.Get())
	{
		const float Gap = (Leader->GetRoadDistance() - RoadDistance) * float(AutoDriveDirection);
		if (Leader->GetAutoDriveDirection() == AutoDriveDirection && Gap > 0.f && Gap < ConvoyGap * 2.f)
		{
			Target = FMath::Min(Target, FMath::Max(Leader->GetSpeed() + (Gap - ConvoyGap) * 0.8f, 0.f));
		}
	}
	Speed = FMath::FInterpConstantTo(Speed, Target, DeltaSeconds, 400.f);
	RoadDistance = FMath::Clamp(RoadDistance + Speed * DeltaSeconds * float(AutoDriveDirection), 0.f, Len);
	// End of the stretch out of sight: turn round on the spot (no one is there to see the arc).
	const float TurnAt = AutoDriveDirection > 0 ? TurnPointHi : TurnPointLo;
	if (LoopRestartDistance < 0.f && (TurnAt - RoadDistance) * float(AutoDriveDirection) < 0.f)
	{
		AutoDriveDirection = -AutoDriveDirection;
		++TrafficStats.UTurns;
	}
	const FVector T = Spline->GetDirectionAtDistanceAlongSpline(RoadDistance, ESplineCoordinateSpace::World) * float(AutoDriveDirection);
	const FVector R = FVector::CrossProduct(FVector::UpVector, T.GetSafeNormal2D()).GetSafeNormal();
	LaneShift = LaneShiftTarget = 0.f;
	const FVector P = Spline->GetLocationAtDistanceAlongSpline(RoadDistance, ESplineCoordinateSpace::World) + R * GetLaneOffsetAt(RoadDistance)
		+ FVector(0.f, 0.f, Collision->GetScaledBoxExtent().Z + GroundClearance + 15.f);   // spline = road surface (validated <= 0.3 m)
	HeadingYaw = T.Rotation().Yaw;
	SetActorLocationAndRotation(P, FRotator(T.Rotation().Pitch, HeadingYaw, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
	LastLateral = GetLaneOffsetAt(RoadDistance);
	LastCrossTrack = 0.f;
	bGrounded = true;
	VerticalVelocity = 0.f;
	StuckTime = 0.f;
	BlockedTime = 0.f;
	TurnState = EHGTurnState::None;
	bQueuedForTurn = false;
}
