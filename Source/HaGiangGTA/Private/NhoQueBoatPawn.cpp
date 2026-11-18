#include "NhoQueBoatPawn.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/SpringArmComponent.h"
#include "HGWaterBody.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

ANhoQueBoatPawn::ANhoQueBoatPawn()
{
	DisplayName = NSLOCTEXT("HaGiang", "Cano", "Cano Song Nho Que");
	bShowDriver = true;

	HullRoot = CreateDefaultSubobject<USceneComponent>(TEXT("HullRoot"));
	HullRoot->SetupAttachment(Collision);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeMesh(TEXT("/Engine/BasicShapes/Cone.Cone"));

	Hull = AddPart(TEXT("Hull"), CubeMesh.Object);
	Bow = AddPart(TEXT("Bow"), ConeMesh.Object);
	Deck = AddPart(TEXT("Deck"), CubeMesh.Object);
	Canopy = AddPart(TEXT("Canopy"), CubeMesh.Object);
	CanopyPostL = AddPart(TEXT("CanopyPostL"), CylMesh.Object);
	CanopyPostR = AddPart(TEXT("CanopyPostR"), CylMesh.Object);
	Motor = AddPart(TEXT("Motor"), CubeMesh.Object);
	Paddle = AddPart(TEXT("Paddle"), CylMesh.Object);

	DriverSeat->SetupAttachment(HullRoot);

	EngineSound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/HaGiang/Generated/Audio/SW_HG_BoatMotor_Loop.SW_HG_BoatMotor_Loop")));
	EngineTopKmh = 60.f;

	ApplyBoatType();
}

UStaticMeshComponent* ANhoQueBoatPawn::AddPart(FName Name, UStaticMesh* Mesh)
{
	UStaticMeshComponent* C = CreateDefaultSubobject<UStaticMeshComponent>(Name);
	C->SetupAttachment(HullRoot);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetGenerateOverlapEvents(false);
	C->SetCanEverAffectNavigation(false);
	if (Mesh)
	{
		C->SetStaticMesh(Mesh);
	}
	return C;
}

void ANhoQueBoatPawn::SetPart(UStaticMeshComponent* Part, bool bVisible, const FVector& Location, const FRotator& Rotation, const FVector& SizeCm)
{
	Part->SetVisibility(bVisible);
	Part->SetHiddenInGame(!bVisible);
	Part->SetRelativeLocation(Location);
	Part->SetRelativeRotation(Rotation);
	Part->SetRelativeScale3D(SizeCm / 100.f);
}

void ANhoQueBoatPawn::ApplyBoatType()
{
	if (BoatType == EHGBoatType::Cano)
	{
		DisplayName = NSLOCTEXT("HaGiang", "Cano", "Cano Song Nho Que");
		Collision->SetBoxExtent(FVector(320.f, 110.f, 50.f));
		MaxForwardSpeed = 1700.f;  // ~61 km/h
		MaxReverseSpeed = 350.f;
		Acceleration = 520.f;
		WaterDrag = 0.3f;
		TurnRateDeg = 48.f;
		BoostMultiplier = 1.35f;
		Draft = 25.f;
		MinDepth = 70.f;
		BowLiftDeg = 7.f;
		TurnRollDeg = 9.f;

		SetPart(Hull, true, FVector(-20.f, 0.f, -10.f), FRotator::ZeroRotator, FVector(560.f, 210.f, 80.f));
		SetPart(Bow, true, FVector(300.f, 0.f, -10.f), FRotator(-90.f, 0.f, 0.f), FVector(210.f, 80.f, 120.f));
		SetPart(Deck, true, FVector(-40.f, 0.f, 32.f), FRotator::ZeroRotator, FVector(480.f, 190.f, 6.f));
		SetPart(Canopy, true, FVector(-60.f, 0.f, 210.f), FRotator::ZeroRotator, FVector(360.f, 220.f, 8.f));
		SetPart(CanopyPostL, true, FVector(-60.f, -100.f, 120.f), FRotator::ZeroRotator, FVector(8.f, 8.f, 180.f));
		SetPart(CanopyPostR, true, FVector(-60.f, 100.f, 120.f), FRotator::ZeroRotator, FVector(8.f, 8.f, 180.f));
		SetPart(Motor, true, FVector(-315.f, 0.f, 20.f), FRotator::ZeroRotator, FVector(40.f, 40.f, 90.f));
		SetPart(Paddle, false);
		DriverSeat->SetRelativeLocation(FVector(-220.f, 0.f, 130.f));
		EngineVolume = 0.8f;

		SpringArm->TargetArmLength = 1000.f;
		SpringArm->SocketOffset = FVector(0.f, 0.f, 320.f);
	}
	else
	{
		DisplayName = NSLOCTEXT("HaGiang", "Kayak", "Kayak Nho Que");
		Collision->SetBoxExtent(FVector(210.f, 40.f, 25.f));
		MaxForwardSpeed = 520.f;   // ~19 km/h
		MaxReverseSpeed = 150.f;
		Acceleration = 240.f;
		WaterDrag = 0.5f;
		TurnRateDeg = 70.f;
		BoostMultiplier = 1.2f;
		Draft = 8.f;
		MinDepth = 35.f;
		BowLiftDeg = 3.f;
		TurnRollDeg = 5.f;

		SetPart(Hull, true, FVector(0.f, 0.f, 0.f), FRotator::ZeroRotator, FVector(330.f, 70.f, 32.f));
		SetPart(Bow, true, FVector(205.f, 0.f, 0.f), FRotator(-90.f, 0.f, 0.f), FVector(64.f, 30.f, 80.f));
		SetPart(Deck, true, FVector(-20.f, 0.f, 18.f), FRotator::ZeroRotator, FVector(90.f, 50.f, 4.f));
		SetPart(Canopy, false);
		SetPart(CanopyPostL, false);
		SetPart(CanopyPostR, false);
		SetPart(Motor, false);
		SetPart(Paddle, true, FVector(10.f, 0.f, 60.f), FRotator(0.f, 0.f, 90.f), FVector(5.f, 5.f, 230.f));
		DriverSeat->SetRelativeLocation(FVector(-20.f, 0.f, 70.f));
		EngineVolume = 0.f; // paddling: no motor

		SpringArm->TargetArmLength = 600.f;
		SpringArm->SocketOffset = FVector(0.f, 0.f, 200.f);
	}
}

void ANhoQueBoatPawn::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyBoatType();
}

void ANhoQueBoatPawn::BeginPlay()
{
	ApplyBoatType();
	Super::BeginPlay();
	HeadingYaw = GetActorRotation().Yaw;

	const bool bCano = BoatType == EHGBoatType::Cano;
	auto Paint = [](UStaticMeshComponent* C, const FLinearColor& Color)
	{
		if (UMaterialInstanceDynamic* MID = C->CreateAndSetMaterialInstanceDynamic(0))
		{
			MID->SetVectorParameterValue(TEXT("Color"), Color);
		}
	};
	const FLinearColor HullColor = bCano ? FLinearColor(0.9f, 0.9f, 0.88f) : FLinearColor(1.f, 0.45f, 0.02f);
	Paint(Hull, HullColor);
	Paint(Bow, HullColor);
	Paint(Deck, bCano ? FLinearColor(0.35f, 0.2f, 0.1f) : FLinearColor(0.05f, 0.05f, 0.05f));
	Paint(Canopy, FLinearColor(0.05f, 0.35f, 0.75f));
	Paint(CanopyPostL, FLinearColor(0.7f, 0.7f, 0.7f));
	Paint(CanopyPostR, FLinearColor(0.7f, 0.7f, 0.7f));
	Paint(Motor, FLinearColor(0.08f, 0.08f, 0.1f));
	Paint(Paddle, FLinearColor(0.9f, 0.8f, 0.1f));
}

void ANhoQueBoatPawn::ResetToSpawn()
{
	Super::ResetToSpawn();
	HeadingYaw = SpawnTransform.Rotator().Yaw;
	Speed = 0.f;
	VerticalVelocity = 0.f;
}

FVector ANhoQueBoatPawn::GetExitLocation() const
{
	// Prefer stepping onto a nearby bank/dock; otherwise drop into the water beside the boat.
	const FVector Extent = Collision->GetScaledBoxExtent();
	const FVector Side = GetActorRightVector().GetSafeNormal2D();
	for (const float Sign : { -1.f, 1.f })
	{
		const FVector Probe = GetActorLocation() + Side * Sign * (Extent.Y + 120.f);
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(HGBoatExit), false, this);
		if (GetWorld()->LineTraceSingleByChannel(Hit, Probe + FVector(0, 0, 300.f), Probe - FVector(0, 0, 400.f), ECC_Visibility, Params))
		{
			float SurfaceZ;
			if (!AHGWaterBody::FindSurface(GetWorld(), Probe, SurfaceZ) || Hit.ImpactPoint.Z > SurfaceZ)
			{
				return Hit.ImpactPoint + FVector(0.f, 0.f, 100.f);
			}
		}
	}
	return GetActorLocation() - Side * (Extent.Y + 90.f) + FVector(0.f, 0.f, 40.f);
}

bool ANhoQueBoatPawn::HasWaterDepthAt(const FVector& Location, float SurfaceZ) const
{
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(HGBoatDepth), false, this);
	const FVector Start(Location.X, Location.Y, SurfaceZ + 50.f);
	const FVector End(Location.X, Location.Y, SurfaceZ - 2000.f);
	if (!GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
	{
		return true;
	}
	return Hit.ImpactPoint.Z < SurfaceZ - MinDepth;
}

void ANhoQueBoatPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const float Dt = FMath::Min(DeltaSeconds, 1.f / 20.f);
	const float Time = GetWorld()->GetTimeSeconds();

	const float Throttle = ReadAxis(ThrottleAction);
	const float Steer = ReadAxis(SteerAction);
	const bool bBoost = ReadButton(BoostAction);
	const bool bKayak = BoatType == EHGBoatType::Kayak;

	FVector Loc = GetActorLocation();
	float SurfaceZ = 0.f;
	const bool bOverWater = AHGWaterBody::FindSurface(GetWorld(), Loc, SurfaceZ);
	bFloating = bOverWater && Loc.Z < SurfaceZ + 150.f;

	// ---- Thrust & drag ----
	if (bFloating)
	{
		float Thrust = Throttle * Acceleration * (bBoost ? BoostMultiplier : 1.f);
		if (bKayak && FMath::Abs(Throttle) > 0.05f)
		{
			// Paddle strokes: thrust comes in pulses.
			PaddlePhase += Dt * 5.5f;
			Thrust *= 0.55f + 0.9f * FMath::Max(0.f, FMath::Sin(PaddlePhase));
		}
		Speed += Thrust * Dt;
		Speed -= Speed * WaterDrag * Dt;
		const float Top = MaxForwardSpeed * (bBoost ? BoostMultiplier : 1.f);
		Speed = FMath::Clamp(Speed, -MaxReverseSpeed, Top);
	}

	// ---- Turning (rudder needs flow; kayaks can pivot slowly) ----
	const float Flow = FMath::Clamp(FMath::Abs(Speed) / 400.f, bKayak ? 0.35f : 0.1f, 1.f);
	const float YawRate = bFloating ? Steer * TurnRateDeg * Flow * (Speed < -10.f ? -1.f : 1.f) : 0.f;
	HeadingYaw = FRotator::NormalizeAxis(HeadingYaw + YawRate * Dt);

	// ---- Horizontal move with bank checks ----
	const FVector Fwd = FRotator(0.f, HeadingYaw, 0.f).Vector();
	const FVector Delta = Fwd * Speed * Dt;
	if (!Delta.IsNearlyZero())
	{
		const FVector BowProbe = Loc + Delta + Fwd * Collision->GetScaledBoxExtent().X * FMath::Sign(Speed);
		if (bFloating && !HasWaterDepthAt(BowProbe, SurfaceZ))
		{
			Speed *= -0.25f; // ran aground: bounce back
		}
		else
		{
			FHitResult Hit;
			AddActorWorldOffset(Delta, true, &Hit);
			if (Hit.bBlockingHit)
			{
				const FVector Slide = FVector::VectorPlaneProject(Delta * (1.f - Hit.Time), Hit.ImpactNormal);
				AddActorWorldOffset(FVector(Slide.X, Slide.Y, 0.f), true);
				Speed *= 1.f - 0.8f * FMath::Abs(FVector::DotProduct(Fwd, Hit.ImpactNormal));
			}
		}
	}

	// ---- Vertical: buoyancy spring on water, gravity otherwise ----
	Loc = GetActorLocation();
	if (bOverWater)
	{
		float Wave = 0.f;
		for (TActorIterator<AHGWaterBody> It(GetWorld()); It; ++It)
		{
			Wave = It->GetWaveOffset(Loc, Time);
			break;
		}
		const float TargetZ = SurfaceZ - Draft + Wave;
		if (Loc.Z > TargetZ + 30.f)
		{
			VerticalVelocity += GetWorld()->GetGravityZ() * Dt; // falling in (e.g. dropped from a cliff)
		}
		else
		{
			VerticalVelocity += ((TargetZ - Loc.Z) * 40.f - VerticalVelocity * 8.f) * Dt;
		}
	}
	else
	{
		VerticalVelocity += GetWorld()->GetGravityZ() * Dt;
	}
	FHitResult VHit;
	AddActorWorldOffset(FVector(0.f, 0.f, VerticalVelocity * Dt), true, &VHit);
	if (VHit.bBlockingHit)
	{
		VerticalVelocity = 0.f;
		Speed *= 0.95f; // grinding on land
	}

	// ---- Visual attitude: bow lifts under power, roll into turns, bob on waves ----
	const float SpeedAlpha = FMath::Clamp(Speed / MaxForwardSpeed, -1.f, 1.f);
	const float PlaneLift = BowLiftDeg * (0.6f * SpeedAlpha * (1.f - 0.4f * SpeedAlpha) + 0.4f * FMath::Max(Throttle, 0.f));
	const float Bob = FMath::Sin(Time * 1.9f + Loc.X * 0.001f) * (bFloating ? 1.2f : 0.f);
	VisualPitch = FMath::FInterpTo(VisualPitch, PlaneLift + Bob, Dt, 3.f);
	VisualRoll = FMath::FInterpTo(VisualRoll, -Steer * TurnRollDeg * FMath::Abs(SpeedAlpha) + Bob * 0.7f, Dt, 3.f);
	HullRoot->SetRelativeRotation(FRotator(VisualPitch, 0.f, VisualRoll));
	SetActorRotation(FRotator(0.f, HeadingYaw, 0.f));

	if (bKayak)
	{
		const float Stroke = FMath::Abs(Throttle) > 0.05f ? FMath::Sin(PaddlePhase) * 35.f : 0.f;
		Paddle->SetRelativeRotation(FRotator(Stroke * 0.5f, 0.f, 90.f + Stroke));
	}

	if (Loc.Z < SpawnTransform.GetLocation().Z - 100000.f)
	{
		ResetToSpawn();
	}
}
