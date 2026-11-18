#include "HGGroundVehicle.h"

#include "Components/BoxComponent.h"
#include "Engine/World.h"

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
	const float Boost = (bBoost ? BoostMultiplier : 1.f) * SpeedMultiplier;
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

	const float Throttle = ReadAxis(ThrottleAction);
	const float Steer = ReadAxis(SteerAction);
	const bool bHandbrake = ReadButton(HandbrakeAction) || (!Controller && bGrounded);
	const bool bBoost = ReadButton(BoostAction);
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
