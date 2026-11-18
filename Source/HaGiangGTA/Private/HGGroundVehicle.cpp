#include "HGGroundVehicle.h"

#include "Components/BoxComponent.h"
#include "Components/SplineComponent.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "HGHappyWaterSubsystem.h"
#include "HGRoadSplineActor.h"

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
	if (bAutoDrive && !IsPlayerControlled())
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
	if (bAutoDrive && NewController && NewController->IsPlayerController())
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
		+ Right * AutoDriveLane + FVector(0.f, 0.f, Collision->GetScaledBoxExtent().Z + GroundClearance + 20.f);
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

float AHGGroundVehicle::GetMinRadiusAhead(float LookCm) const
{
	if (!AutoDriveRoad)
	{
		return BIG_NUMBER;
	}
	const USplineComponent* Spline = AutoDriveRoad->GetSpline();
	const float Len = Spline->GetSplineLength();
	float MinRadius = BIG_NUMBER;
	const float Step = 600.f;
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

	// End of the road: loop or turn around.
	const bool bAtEnd = (AutoDriveDirection > 0 && RoadDistance > Len - 1500.f) || (AutoDriveDirection < 0 && RoadDistance < 1500.f);
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

	// Pure pursuit toward a point ahead on our lane.
	const float Look = AutoDriveLookahead + FMath::Abs(Speed) * 0.35f;
	const float TargetD = FMath::Clamp(RoadDistance + Look * AutoDriveDirection, 0.f, Len);
	const FVector Tangent = Spline->GetDirectionAtDistanceAlongSpline(TargetD, ESplineCoordinateSpace::World) * float(AutoDriveDirection);
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Tangent).GetSafeNormal();
	const FVector Target = Spline->GetLocationAtDistanceAlongSpline(TargetD, ESplineCoordinateSpace::World) + Right * AutoDriveLane;
	const FVector ToTarget = (Target - GetActorLocation()).GetSafeNormal2D();
	const float DesiredYaw = ToTarget.Rotation().Yaw;
	const float YawError = FRotator::NormalizeAxis(DesiredYaw - HeadingYaw);
	OutSteer = FMath::Clamp(YawError / 22.f, -1.f, 1.f);

	// Speed: cruise, slowed for curves (v = sqrt(a * R)) and by the vehicle ahead.
	float TargetSpeed = AutoDriveCruise;
	const float Radius = GetMinRadiusAhead(3600.f);
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
