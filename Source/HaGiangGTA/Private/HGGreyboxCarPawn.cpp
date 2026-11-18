#include "HGGreyboxCarPawn.h"

#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "HaGiangGTA.h"
#include "HGInputFactory.h"
#include "InputAction.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** Fraction of the half length at which front/rear ground probes are placed. */
	constexpr float ProbeLengthFraction = 0.8f;
	/** Required "downward pull" (in g) at a crest before the car leaves the ground. */
	constexpr float CrestLaunchG = 2.0f;
	/** Below this speed the car never launches off a crest. */
	constexpr float MinLaunchSpeed = 900.f;
	/** Steering authority left while airborne. */
	constexpr float AirSteerFactor = 0.25f;
}

AHGGreyboxCarPawn::AHGGreyboxCarPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;

	Collision = CreateDefaultSubobject<UBoxComponent>(TEXT("Collision"));
	Collision->SetBoxExtent(FVector(200.f, 90.f, 55.f));
	Collision->SetCollisionProfileName(TEXT("Pawn"));
	Collision->SetSimulatePhysics(false);
	Collision->SetCanEverAffectNavigation(false);
	RootComponent = Collision;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));

	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
	BodyMesh->SetupAttachment(Collision);
	BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BodyMesh->SetRelativeLocation(FVector(0.f, 0.f, -15.f));
	BodyMesh->SetRelativeScale3D(FVector(4.0f, 1.8f, 0.7f));

	CabinMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CabinMesh"));
	CabinMesh->SetupAttachment(BodyMesh);
	CabinMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// Relative to the scaled body: (-30, 0, 45) cm world-ish offset, 2.0 x 1.6 x 0.5 m.
	CabinMesh->SetRelativeLocation(FVector(-30.f / 4.0f, 0.f, 60.f / 0.7f));
	CabinMesh->SetRelativeScale3D(FVector(2.0f / 4.0f, 1.6f / 1.8f, 0.5f / 0.7f));

	if (CubeMesh.Succeeded())
	{
		BodyMesh->SetStaticMesh(CubeMesh.Object);
		CabinMesh->SetStaticMesh(CubeMesh.Object);
	}

	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(Collision);
	SpringArm->TargetArmLength = 700.f;
	SpringArm->SocketOffset = FVector(0.f, 0.f, 200.f);
	SpringArm->SetRelativeRotation(FRotator(-10.f, 0.f, 0.f));
	SpringArm->bUsePawnControlRotation = false;
	SpringArm->bInheritRoll = false;
	SpringArm->bEnableCameraLag = true;
	SpringArm->CameraLagSpeed = 8.f;
	SpringArm->bEnableCameraRotationLag = true;
	SpringArm->CameraRotationLagSpeed = 5.f;
	SpringArm->bDoCollisionTest = true;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);
	Camera->FieldOfView = 90.f;

	AutoPossessAI = EAutoPossessAI::Disabled;
}

void AHGGreyboxCarPawn::BeginPlay()
{
	Super::BeginPlay();

	SpawnTransform = GetActorTransform();
	HeadingYaw = GetActorRotation().Yaw;

	if (UMaterialInstanceDynamic* BodyMID = BodyMesh->CreateAndSetMaterialInstanceDynamic(0))
	{
		BodyMID->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.75f, 0.05f, 0.04f));
	}
	if (UMaterialInstanceDynamic* CabinMID = CabinMesh->CreateAndSetMaterialInstanceDynamic(0))
	{
		CabinMID->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.05f, 0.05f, 0.06f));
	}
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

void AHGGreyboxCarPawn::EnsureInputObjects()
{
	if (DrivingContext)
	{
		return;
	}

	ThrottleAction = HGInput::MakeAction(this, TEXT("IA_Throttle"), EInputActionValueType::Axis1D);
	SteerAction = HGInput::MakeAction(this, TEXT("IA_Steer"), EInputActionValueType::Axis1D);
	HandbrakeAction = HGInput::MakeAction(this, TEXT("IA_Handbrake"), EInputActionValueType::Boolean);
	BoostAction = HGInput::MakeAction(this, TEXT("IA_Boost"), EInputActionValueType::Boolean);
	ResetAction = HGInput::MakeAction(this, TEXT("IA_Reset"), EInputActionValueType::Boolean);

	DrivingContext = HGInput::MakeContext(this, TEXT("IMC_GreyboxDriving"));

	HGInput::MapKey(DrivingContext, ThrottleAction, EKeys::W);
	HGInput::MapKey(DrivingContext, ThrottleAction, EKeys::Up);
	HGInput::MapKey(DrivingContext, ThrottleAction, EKeys::S, /*bNegate*/ true);
	HGInput::MapKey(DrivingContext, ThrottleAction, EKeys::Down, /*bNegate*/ true);
	HGInput::MapKey(DrivingContext, ThrottleAction, EKeys::Gamepad_RightTriggerAxis);
	HGInput::MapKey(DrivingContext, ThrottleAction, EKeys::Gamepad_LeftTriggerAxis, /*bNegate*/ true);

	HGInput::MapKey(DrivingContext, SteerAction, EKeys::D);
	HGInput::MapKey(DrivingContext, SteerAction, EKeys::Right);
	HGInput::MapKey(DrivingContext, SteerAction, EKeys::A, /*bNegate*/ true);
	HGInput::MapKey(DrivingContext, SteerAction, EKeys::Left, /*bNegate*/ true);
	HGInput::MapKey(DrivingContext, SteerAction, EKeys::Gamepad_LeftX);

	HGInput::MapKey(DrivingContext, HandbrakeAction, EKeys::SpaceBar);
	HGInput::MapKey(DrivingContext, HandbrakeAction, EKeys::Gamepad_FaceButton_Right);

	HGInput::MapKey(DrivingContext, BoostAction, EKeys::LeftShift);
	HGInput::MapKey(DrivingContext, BoostAction, EKeys::Gamepad_FaceButton_Bottom);

	HGInput::MapKey(DrivingContext, ResetAction, EKeys::R);
	HGInput::MapKey(DrivingContext, ResetAction, EKeys::Gamepad_Special_Left);
}

void AHGGreyboxCarPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	EnsureInputObjects();

	UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!EIC)
	{
		UE_LOG(LogHaGiang, Error, TEXT("%s: EnhancedInputComponent required (check DefaultInput.ini)."), *GetName());
		return;
	}

	// Continuous values are polled every tick.
	EIC->BindActionValue(ThrottleAction);
	EIC->BindActionValue(SteerAction);
	EIC->BindActionValue(HandbrakeAction);
	EIC->BindActionValue(BoostAction);

	EIC->BindAction(ResetAction, ETriggerEvent::Started, this, &AHGGreyboxCarPawn::OnReset);
}

void AHGGreyboxCarPawn::PawnClientRestart()
{
	EnsureInputObjects();
	Super::PawnClientRestart();
	HGInput::AddContext(this, DrivingContext, 0);
}

void AHGGreyboxCarPawn::UnPossessed()
{
	HGInput::RemoveContext(this, DrivingContext);
	Super::UnPossessed();
}

float AHGGreyboxCarPawn::ReadAxis(const UInputAction* Action) const
{
	const UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent);
	return (EIC && Action) ? FMath::Clamp(EIC->GetBoundActionValue(Action).Get<float>(), -1.f, 1.f) : 0.f;
}

bool AHGGreyboxCarPawn::ReadButton(const UInputAction* Action) const
{
	const UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent);
	return (EIC && Action) ? EIC->GetBoundActionValue(Action).Get<bool>() : false;
}

void AHGGreyboxCarPawn::OnReset(const FInputActionValue& /*Value*/)
{
	ResetToSpawn();
}

void AHGGreyboxCarPawn::ResetToSpawn()
{
	SetActorTransform(SpawnTransform, false, nullptr, ETeleportType::TeleportPhysics);
	HeadingYaw = SpawnTransform.Rotator().Yaw;
	Speed = 0.f;
	VerticalVelocity = 0.f;
	bGrounded = false;
	GroundNormal = FVector::UpVector;
}

// ---------------------------------------------------------------------------
// Driving model
// ---------------------------------------------------------------------------

bool AHGGreyboxCarPawn::TraceGround(const FVector& From, FHitResult& OutHit) const
{
	const float HalfZ = Collision->GetScaledBoxExtent().Z;
	const FVector Start = From + FVector(0.f, 0.f, MaxStepUp + HalfZ);
	const FVector End = From - FVector(0.f, 0.f, 50000.f);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(HGCarGround), /*bTraceComplex*/ false, this);
	return GetWorld()->LineTraceSingleByChannel(OutHit, Start, End, ECC_Visibility, Params);
}

void AHGGreyboxCarPawn::UpdateSpeed(float DeltaSeconds, float Throttle, bool bHandbrake, bool bBoost, const FVector& MoveDir)
{
	const float Boost = bBoost ? BoostMultiplier : 1.f;
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

	// Ease back down to the normal top speed when boost is released.
	if (Speed > TopSpeed)
	{
		Speed = FMath::FInterpConstantTo(Speed, TopSpeed, DeltaSeconds, BrakeDeceleration * 0.5f);
	}
	Speed = FMath::Max(Speed, -MaxReverseSpeed);
}

void AHGGreyboxCarPawn::MoveWithCollision(const FVector& Delta, const FVector& MoveDir)
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
			// Landed on walkable ground; SnapToGround will settle the height.
			return;
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

	// Wall / steep face: slide along it and lose speed proportional to impact angle.
	const FVector Slide = FVector::VectorPlaneProject(Remaining, Hit.ImpactNormal);
	AddActorWorldOffset(Slide, true);

	const float Impact = FMath::Abs(FVector::DotProduct(MoveDir, Hit.ImpactNormal));
	Speed *= (1.f - 0.85f * Impact);

	if (!bGrounded && Hit.ImpactNormal.Z < -0.5f)
	{
		VerticalVelocity = FMath::Min(VerticalVelocity, 0.f); // hit a ceiling
	}
}

void AHGGreyboxCarPawn::SnapToGround(const FVector& MoveDir)
{
	const float DeltaSeconds = FMath::Max(GetWorld()->GetDeltaSeconds(), KINDA_SMALL_NUMBER);
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

	// Combine the two probes into one ground height + normal.
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
		// CrestLaunchG of downward acceleration -> the car leaves the ground.
		const FVector NewDir = FVector::VectorPlaneProject(FlatFwd, NewNormal).GetSafeNormal();
		const float OldVz = MoveDir.Z * Speed;
		const float NewVz = NewDir.Z * Speed;
		const float RequiredAccel = (NewVz - OldVz) / DeltaSeconds;
		if (Speed > MinLaunchSpeed && RequiredAccel < GetWorld()->GetGravityZ() * CrestLaunchG)
		{
			Takeoff();
			return;
		}
	}
	else
	{
		// Airborne: only land once we are at/below the ground while falling.
		if (!bWalkable || Gap > 2.f || VerticalVelocity > 0.f)
		{
			return;
		}
		// Landing scrubs some speed depending on how hard we hit.
		Speed *= FMath::GetMappedRangeValueClamped(FVector2D(0.f, -2500.f), FVector2D(1.f, 0.75f), VerticalVelocity);
	}

	SetActorLocation(FVector(Loc.X, Loc.Y, DesiredZ), false, nullptr, ETeleportType::None);
	bGrounded = true;
	VerticalVelocity = 0.f;
	GroundNormal = NewNormal;
}

void AHGGreyboxCarPawn::UpdateOrientation(float DeltaSeconds, float Steer)
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
		// In the air, nose follows the flight path a little.
		const float FlightPitch = FMath::RadiansToDegrees(FMath::Atan2(VerticalVelocity, FMath::Max(FMath::Abs(Speed), 1.f)));
		Target = FRotator(FMath::Clamp(FlightPitch * 0.5f, -25.f, 25.f), HeadingYaw, 0.f);
	}

	FRotator NewRot = FMath::RInterpTo(GetActorRotation(), Target, DeltaSeconds, AlignInterpSpeed);
	NewRot.Yaw = Target.Yaw; // heading is authoritative, never lag it
	SetActorRotation(NewRot, ETeleportType::None);

	// Cosmetic body lean into corners.
	const float SpeedAlpha = FMath::Clamp(FMath::Abs(Speed) / MaxForwardSpeed, 0.f, 1.f);
	const FRotator Lean(0.f, 0.f, Steer * SpeedAlpha * BodyLeanDeg);
	BodyMesh->SetRelativeRotation(FMath::RInterpTo(BodyMesh->GetRelativeRotation(), Lean, DeltaSeconds, 6.f));
}

void AHGGreyboxCarPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Clamp to avoid tunnelling on hitches (shader compiles, level loads).
	const float Dt = FMath::Min(DeltaSeconds, 1.f / 20.f);

	const float Throttle = ReadAxis(ThrottleAction);
	const float Steer = ReadAxis(SteerAction);
	const bool bHandbrake = ReadButton(HandbrakeAction);
	const bool bBoost = ReadButton(BoostAction);

	// Heading.
	const float SpeedAlpha = FMath::Clamp(FMath::Abs(Speed) / 600.f, 0.f, 1.f);
	const float HighSpeedScale = FMath::Lerp(1.f, HighSpeedSteerScale, FMath::Clamp(FMath::Abs(Speed) / MaxForwardSpeed, 0.f, 1.f));
	const float HandbrakeScale = bHandbrake ? 1.6f : 1.f;
	const float AirScale = bGrounded ? 1.f : AirSteerFactor;
	HeadingYaw = FRotator::NormalizeAxis(HeadingYaw + Steer * SteerRateDeg * SpeedAlpha * HighSpeedScale * HandbrakeScale * AirScale * FMath::Sign(Speed) * Dt);

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
	SnapToGround(MoveDir);
	UpdateOrientation(Dt, Steer);

	if (GetActorLocation().Z < KillZ)
	{
		ResetToSpawn();
	}
}
