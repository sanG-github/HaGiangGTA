#include "HGGroundVehicle.h"

#include "Components/BoxComponent.h"
#include "Components/SplineComponent.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "HGHappyWaterSubsystem.h"
#include "HGRoadSplineActor.h"
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

AHGGroundVehicle::AHGGroundVehicle()
{
}

void AHGGroundVehicle::BeginPlay()
{
	Super::BeginPlay();
	HeadingYaw = GetActorRotation().Yaw;
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
	const float TopSpeed = MaxForwardSpeed * Boost;

	if (Throttle > KINDA_SMALL_NUMBER)
	{
		Speed += (Speed < -10.f ? BrakeDeceleration : Acceleration * Boost) * Throttle * DeltaSeconds;
	}
	else if (Throttle < -KINDA_SMALL_NUMBER)
	{
		Speed += (Speed > 10.f ? BrakeDeceleration : Acceleration) * Throttle * DeltaSeconds;
	}
	else if (bGrounded)
	{
		Speed = FMath::FInterpConstantTo(Speed, 0.f, DeltaSeconds, CoastDeceleration);
	}

	if (bGrounded)
	{
		if (bHandbrake)
		{
			Speed = FMath::FInterpConstantTo(Speed, 0.f, DeltaSeconds, BrakeDeceleration * 0.6f);
		}
		// Gravity component along the slope (MoveDir.Z > 0 means uphill).
		Speed += GetWorld()->GetGravityZ() * MoveDir.Z * SlopeGravityScale * DeltaSeconds;
	}

	if (Speed > TopSpeed)
	{
		Speed = FMath::FInterpConstantTo(Speed, TopSpeed, DeltaSeconds, BrakeDeceleration * 0.5f);
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

	FHitResult Hit;
	AddActorWorldOffset(Delta, true, &Hit);
	if (!Hit.bBlockingHit)
	{
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
		if (!bWalkable || Gap > 2.f || VerticalVelocity > 0.f)
		{
			return;
		}
		// Landing scrubs speed depending on how hard we hit.
		Speed *= FMath::GetMappedRangeValueClamped(FVector2D(0.f, -2500.f), FVector2D(1.f, 0.75f), VerticalVelocity);
	}

	SetActorLocation(FVector(Loc.X, Loc.Y, DesiredZ), false, nullptr, ETeleportType::None);
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

	const float Dt = FMath::Min(DeltaSeconds, 1.f / 20.f);

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
	}
	LastThrottle = Throttle;

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

	MoveWithCollision(Delta, MoveDir);
	SnapToGround(Dt, MoveDir);
	UpdateOrientation(Dt);
	UpdateVisuals(Dt, Steer);

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
	AutoDriveRoad = Road;
	AutoDriveDirection = Direction >= 0 ? 1 : -1;
	AutoDriveCruise = CruiseKmh / 0.036f;
	AutoDriveLane = LaneOffsetCm;
	RoadDistance = FMath::Clamp(DistanceCm, 0.f, Spline->GetSplineLength());

	const FVector Tangent = Spline->GetDirectionAtDistanceAlongSpline(RoadDistance, ESplineCoordinateSpace::World) * float(AutoDriveDirection);
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Tangent).GetSafeNormal();
	const FVector Loc = Spline->GetLocationAtDistanceAlongSpline(RoadDistance, ESplineCoordinateSpace::World)
		+ Right * GetLaneOffsetAt(RoadDistance) + FVector(0.f, 0.f, Collision->GetScaledBoxExtent().Z + GroundClearance + 20.f);
	const FRotator Rot(0.f, Tangent.Rotation().Yaw, 0.f);
	SetActorLocationAndRotation(Loc, Rot, false, nullptr, ETeleportType::TeleportPhysics);
	HeadingYaw = Rot.Yaw;
	Speed = AutoDriveCruise * 0.5f;
	bGrounded = false;
	VerticalVelocity = 0.f;
	SpawnTransform = GetActorTransform();

	const bool bWas = bAutoDrive;
	bAutoDrive = true;
	if (!bWas)
	{
		OnAutoDriveChanged(true);
	}
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
	return FMath::Sign(AutoDriveLane) * FMath::Min(FMath::Abs(AutoDriveLane), MaxLane);
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
	const bool bAtEnd = (AutoDriveDirection > 0 && RoadDistance > RangeHi) || (AutoDriveDirection < 0 && RoadDistance < RangeLo);
	if (bAtEnd)
	{
		if (LoopRestartDistance >= 0.f)
		{
			StartAutoDrive(AutoDriveRoad, LoopRestartDistance, AutoDriveDirection, AutoDriveCruise * 0.036f, AutoDriveLane);
		}
		else
		{
			StartAutoDrive(AutoDriveRoad, RoadDistance, -AutoDriveDirection, AutoDriveCruise * 0.036f, AutoDriveLane);
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
	const float PreviewD = FMath::Clamp(RoadDistance + Dir * FMath::Max(FMath::Abs(Speed) * LanePreviewSeconds, 150.f), 0.f, Len);
	const FVector Ahead = Spline->GetDirectionAtDistanceAlongSpline(PreviewD, ESplineCoordinateSpace::World) * Dir;
	const float DesiredYaw = Ahead.Rotation().Yaw - FMath::RadiansToDegrees(FMath::Atan2(LaneKeepGain * CrossTrack, FMath::Abs(Speed) + 300.f));
	const float YawError = FRotator::NormalizeAxis(DesiredYaw - HeadingYaw);
	OutSteer = FMath::Clamp(YawError / 22.f, -1.f, 1.f);

	// Speed: cruise, slowed for curves (v = sqrt(a * R)) and by the vehicle ahead.
	float TargetSpeed = AutoDriveCruise;
	// Radius of our lane, assuming it is on the inside of the bend (conservative); 3 m steps resolve the
	// 8 m Ta Lang hairpins that 6 m steps under-sample.
	const float Radius = FMath::Max(GetMinRadiusAhead(3600.f, 300.f) - FMath::Abs(GetLaneOffsetAt(RoadDistance)), 150.f);
	TargetSpeed = FMath::Min(TargetSpeed, FMath::Sqrt(AutoDriveLateralAccel * Radius));

	if (const AHGGroundVehicle* Leader = AutoDriveLeader.Get())
	{
		if (Leader->IsAutoDriving() && Leader->GetAutoDriveDirection() == AutoDriveDirection)
		{
			const float Gap = (Leader->GetRoadDistance() - RoadDistance) * AutoDriveDirection;
			const float Desired = ConvoyGap + FMath::Abs(Speed) * 0.5f;
			if (Gap > 0.f)
			{
				// Close up when far behind, back off when too close.
				TargetSpeed = FMath::Clamp(Leader->GetSpeed() + (Gap - Desired) * 0.8f, 0.f, AutoDriveCruise * 1.25f);
			}
		}
	}

	// Anything (player, cows, other traffic) right in front: brake.
	FHitResult Hit;
	const FVector Fwd = GetActorForwardVector();
	const FVector Start = GetActorLocation() + Fwd * (Collision->GetScaledBoxExtent().X + 50.f);
	const float Probe = 600.f + FMath::Abs(Speed) * 0.9f;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(HGAutoDriveProbe), false, this);
	if (GetWorld()->SweepSingleByObjectType(Hit, Start, Start + Fwd * Probe, FQuat::Identity,
		FCollisionObjectQueryParams(ECC_Pawn), FCollisionShape::MakeSphere(90.f), Params))
	{
		const float Block = FMath::Clamp(Hit.Distance / Probe, 0.f, 1.f);
		TargetSpeed = FMath::Min(TargetSpeed, AutoDriveCruise * Block * Block);
		OnAutoDriveObstacle(Hit.GetActor());
	}

	const float SpeedError = TargetSpeed - Speed;
	OutThrottle = FMath::Clamp(SpeedError / 300.f, -1.f, 1.f);
	OutHandbrake = TargetSpeed < 30.f && FMath::Abs(Speed) < 60.f;
}
