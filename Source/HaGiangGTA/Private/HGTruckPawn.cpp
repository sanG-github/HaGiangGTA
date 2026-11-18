#include "HGTruckPawn.h"
#include "HGPaint.h"
#include "HGMeshKit.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/SpringArmComponent.h"
#include "HGAudio.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

AHGTruckPawn::AHGTruckPawn()
{
	DisplayName = NSLOCTEXT("HaGiang", "Truck", "Xe tai ben Thaco");
	bShowDriver = false;

	MaxForwardSpeed = 1700.f;    // ~60 km/h
	Acceleration = 420.f;
	BrakeDeceleration = 1500.f;
	SteerRateDeg = 60.f;
	HighSpeedSteerScale = 0.6f;
	BoostMultiplier = 1.15f;
	GroundClearance = 25.f;
	AutoDriveLateralAccel = 260.f;

	Collision->SetBoxExtent(FVector(380.f, 125.f, 150.f));
	SpringArm->TargetArmLength = 1300.f;
	SpringArm->SocketOffset = FVector(0.f, 0.f, 420.f);
	DriverSeat->SetRelativeLocation(FVector(250.f, -50.f, 60.f));

	BodyRoot = CreateDefaultSubobject<USceneComponent>(TEXT("BodyRoot"));
	BodyRoot->SetupAttachment(Collision);
	BodyRoot->SetRelativeLocation(FVector(0.f, 0.f, -175.f)); // ground

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	UStaticMesh* Cube = CubeMesh.Object;
	UStaticMesh* Cyl = CylMesh.Object;

	const FLinearColor Blue(0.03f, 0.18f, 0.55f);
	const FLinearColor Grey(0.25f, 0.25f, 0.25f);
	const FLinearColor Dark(0.03f, 0.03f, 0.03f);
	const FLinearColor Dirt(0.3f, 0.2f, 0.12f);
	const FLinearColor Glass(0.08f, 0.12f, 0.15f);

	AddPart(TEXT("Chassis"), Cube, FVector(0.f, 0.f, 75.f), FRotator::ZeroRotator, FVector(740.f, 200.f, 30.f), Dark);
	AddPart(TEXT("Cab"), Cube, FVector(270.f, 0.f, 190.f), FRotator::ZeroRotator, FVector(190.f, 240.f, 200.f), Blue);
	AddPart(TEXT("Windscreen"), Cube, FVector(366.f, 0.f, 230.f), FRotator(-8.f, 0.f, 0.f), FVector(6.f, 220.f, 90.f), Glass);
	AddPart(TEXT("Bumper"), Cube, FVector(372.f, 0.f, 95.f), FRotator::ZeroRotator, FVector(12.f, 245.f, 30.f), Grey);
	AddPart(TEXT("Bed"), Cube, FVector(-110.f, 0.f, 175.f), FRotator::ZeroRotator, FVector(500.f, 245.f, 140.f), Blue);
	AddPart(TEXT("Load"), Cube, FVector(-110.f, 0.f, 255.f), FRotator::ZeroRotator, FVector(470.f, 220.f, 40.f), Dirt);
	const float WheelX[] = { 270.f, -90.f, -250.f };
	for (int32 i = 0; i < 3; ++i)
	{
		for (const float Side : { -1.f, 1.f })
		{
			const FName Name(*FString::Printf(TEXT("Wheel_%d_%s"), i, Side < 0.f ? TEXT("L") : TEXT("R")));
			AddPart(Name, Cyl, FVector(WheelX[i], Side * 105.f, 50.f), FRotator(0.f, 0.f, 90.f), FVector(100.f, 100.f, 40.f), Dark);
		}
	}

	EngineSound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/HaGiang/Generated/Audio/SW_HG_TruckEngine_Loop.SW_HG_TruckEngine_Loop")));
	EngineTopKmh = 60.f;
	EngineIdlePitch = 0.7f;
	EngineMaxPitch = 1.6f;
	EngineFalloff = 9000.f;
	EngineVolume = 0.9f;
	HornSound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/HaGiang/Generated/Audio/SW_HG_AirHorn.SW_HG_AirHorn")));
}

UStaticMeshComponent* AHGTruckPawn::AddPart(FName Name, UStaticMesh* Mesh, const FVector& Location, const FRotator& Rotation, const FVector& SizeCm, const FLinearColor& Color)
{
	UStaticMeshComponent* C = CreateDefaultSubobject<UStaticMeshComponent>(Name);
	C->SetupAttachment(BodyRoot);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetGenerateOverlapEvents(false);
	C->SetCanEverAffectNavigation(false);
	C->SetRelativeLocation(Location);
	C->SetRelativeRotation(Rotation);
	C->SetRelativeScale3D(SizeCm / 100.f);
	if (Mesh)
	{
		C->SetStaticMesh(Mesh);
	}
	Parts.Add(C);
	PartColors.Add(Color);
	return C;
}

void AHGTruckPawn::BeginPlay()
{
	Super::BeginPlay();
	HGPaint::LimitDrawDistance(this, 6000.f, 60000.f);   // trucks read from across the valley
	for (int32 i = 0; i < Parts.Num() && i < PartColors.Num(); ++i)
	{
		if (UMaterialInstanceDynamic* MID = HGPaint::MakeMID(Parts[i]))
		{
			MID->SetVectorParameterValue(TEXT("Color"), PartColors[i]);
		}
	}
	LoadedHorn = HornSound.LoadSynchronous();
	BuildKit();
}

void AHGTruckPawn::Honk()
{
	if (HornCooldown > 0.f)
	{
		return;
	}
	HornCooldown = 5.f;
	if (LoadedHorn)
	{
		HGAudio::PlayAt(this, LoadedHorn, GetActorLocation(), 1.f, FMath::FRandRange(0.97f, 1.03f), 25000.f);
	}
}

void AHGTruckPawn::OnAutoDriveObstacle(AActor* Obstacle)
{
	if (Obstacle)
	{
		Honk();
	}
}

void AHGTruckPawn::UpdateVisuals(float DeltaSeconds, float Steer)
{
	HornCooldown = FMath::Max(0.f, HornCooldown - DeltaSeconds);
	if (KitWheels.Num() > 0)
	{
		WheelAngle = FMath::Fmod(WheelAngle - FMath::RadiansToDegrees(GetSpeed() * DeltaSeconds / 50.f), 360.f);
		for (UStaticMeshComponent* W : KitWheels)
		{
			W->SetRelativeRotation(FRotator(WheelAngle, 0.f, 0.f));
		}
	}

	// Blind hairpin ahead (radius < 30 m within 60 m) and the player nearby -> horn.
	BlindCurveCheck -= DeltaSeconds;
	if (IsAutoDriving() && BlindCurveCheck <= 0.f)
	{
		BlindCurveCheck = 0.5f;
		const APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
		const bool bPlayerNear = Player && FVector::Dist(Player->GetActorLocation(), GetActorLocation()) < 15000.f;
		if (bPlayerNear && GetMinRadiusAhead(6000.f) < 3000.f)
		{
			Honk();
		}
	}
}

void AHGTruckPawn::BuildKit()
{
	using FLC = FLinearColor;
	const FLC Dark(0.015f, 0.015f, 0.016f), Steel(0.3f, 0.31f, 0.32f), Glass(0.02f, 0.03f, 0.035f), Chrome(0.7f, 0.7f, 0.72f);
	UStaticMesh* BodyMesh = FHGMeshKit::Cached(TEXT("SM_HGTruckBody"), [&](FHGMeshKit& K)
	{
		// Chassis, fuel tank, mudguards.
		K.Paint(Dark, HGSurf::Satin);
		K.Box(FVector(-20.f, -45.f, 72.f), FVector(720.f, 12.f, 22.f));
		K.Box(FVector(-20.f, 45.f, 72.f), FVector(720.f, 12.f, 22.f));
		K.Paint(Steel, HGSurf::Satin);
		K.Tube(FVector(120.f, -95.f, 70.f), FVector(40.f, -95.f, 70.f), 22.f, 22.f, 12, true);
		K.Paint(Dark, HGSurf::Satin);
		for (const float X : { 270.f, -90.f, -250.f })
		{
			for (const float S : { -1.f, 1.f })
			{
				K.Box(FVector(X, S * 108.f, 112.f), FVector(X > 0.f ? 115.f : 120.f, 34.f, 4.f));
				if (X < 0.f)
				{
					K.Box(FVector(X - 64.f, S * 108.f, 70.f), FVector(2.f, 34.f, 60.f));      // mud flaps
				}
			}
		}
		// Cab-over cab (tinted blue), roof, white belt line.
		K.Paint(FLC::White, HGSurf::Gloss, true);
		K.Box(FVector(278.f, 0.f, 168.f), FVector(170.f, 236.f, 150.f));
		K.Box(FVector(270.f, 0.f, 262.f), FVector(150.f, 230.f, 40.f), FRotator(-4.f, 0.f, 0.f));
		K.Ellipsoid(FVector(265.f, 0.f, 282.f), FVector(78.f, 114.f, 10.f), FRotator::ZeroRotator, 12, 4, 0.f);
		K.Paint(FLC(0.8f, 0.8f, 0.78f), HGSurf::Gloss);
		K.Box(FVector(278.f, 0.f, 192.f), FVector(172.f, 238.f, 6.f));
		// Windscreen, side windows, door seams, step.
		K.Paint(Glass, HGSurf::Chrome);
		K.Box(FVector(355.f, 0.f, 236.f), FVector(4.f, 214.f, 72.f), FRotator(-10.f, 0.f, 0.f));
		K.Box(FVector(300.f, -118.5f, 232.f), FVector(85.f, 2.f, 58.f));
		K.Box(FVector(300.f, 118.5f, 232.f), FVector(85.f, 2.f, 58.f));
		K.Paint(Dark, HGSurf::Satin);
		K.Box(FVector(250.f, -118.6f, 180.f), FVector(2.f, 1.f, 150.f));
		K.Box(FVector(250.f, 118.6f, 180.f), FVector(2.f, 1.f, 150.f));
		K.Box(FVector(320.f, -112.f, 92.f), FVector(50.f, 16.f, 6.f));
		K.Box(FVector(320.f, 112.f, 92.f), FVector(50.f, 16.f, 6.f));
		// Grille, bumper, headlamps, indicators, mirrors.
		K.Box(FVector(364.f, 0.f, 140.f), FVector(3.f, 150.f, 46.f));
		K.Paint(Steel, HGSurf::Satin);
		for (int32 i = 0; i < 5; ++i)
		{
			K.Box(FVector(366.f, 0.f, 122.f + i * 9.f), FVector(2.f, 146.f, 2.5f));
		}
		K.Paint(FLC(0.55f, 0.55f, 0.55f), HGSurf::Satin);
		K.Box(FVector(370.f, 0.f, 88.f), FVector(16.f, 244.f, 28.f));
		for (const float S : { -1.f, 1.f })
		{
			K.Paint(FLC(0.85f, 0.85f, 0.82f), HGSurf::Gloss);
			K.Box(FVector(366.f, S * 88.f, 115.f), FVector(3.f, 34.f, 15.f));
			K.Paint(FLC(0.85f, 0.4f, 0.02f), HGSurf::Gloss);
			K.Box(FVector(366.f, S * 112.f, 115.f), FVector(3.f, 10.f, 15.f));
			K.Paint(Dark, HGSurf::Satin);
			K.Tube(FVector(345.f, S * 118.f, 250.f), FVector(355.f, S * 142.f, 250.f), 1.2f, 1.2f, 5, false);
			K.Box(FVector(356.f, S * 145.f, 228.f), FVector(4.f, 14.f, 34.f));
		}
		// Dump body with side ribs and a gravel load.
		K.Paint(FLC(0.42f, 0.34f, 0.08f), HGSurf::Satin);
		K.Box(FVector(-110.f, 0.f, 168.f), FVector(500.f, 246.f, 120.f));
		K.Paint(FLC(0.32f, 0.26f, 0.06f), HGSurf::Satin);
		for (int32 i = 0; i < 8; ++i)
		{
			for (const float S : { -1.f, 1.f })
			{
				K.Box(FVector(-340.f + i * 66.f, S * 124.f, 168.f), FVector(6.f, 3.f, 120.f));
			}
		}
		K.Box(FVector(-110.f, 0.f, 230.f), FVector(504.f, 250.f, 6.f));
		K.Paint(FLC(0.33f, 0.27f, 0.2f));
		K.Ellipsoid(FVector(-110.f, 0.f, 228.f), FVector(240.f, 115.f, 34.f), FRotator::ZeroRotator, 14, 6, 0.f);
		K.Paint(FLC(0.25f, 0.22f, 0.18f));
		for (int32 i = 0; i < 9; ++i)
		{
			K.Ellipsoid(FVector(-300.f + i * 45.f, (i % 3 - 1) * 50.f, 248.f + (i % 2) * 6.f), FVector(16.f, 13.f, 9.f), FRotator(0.f, i * 40.f, 0.f), 6, 4);
		}
		// Rear lamps and plate.
		K.Paint(FLC(0.6f, 0.03f, 0.02f), HGSurf::Gloss);
		K.Box(FVector(-361.f, -100.f, 105.f), FVector(3.f, 22.f, 10.f));
		K.Box(FVector(-361.f, 100.f, 105.f), FVector(3.f, 22.f, 10.f));
		K.Paint(FLC(0.8f, 0.7f, 0.1f), HGSurf::Satin);
		K.Box(FVector(-362.f, 0.f, 100.f), FVector(2.f, 52.f, 18.f));
	});
	auto MakeWheel = [&](bool bDual)
	{
		return FHGMeshKit::Cached(bDual ? TEXT("SM_HGTruckWheelDual") : TEXT("SM_HGTruckWheel"), [&](FHGMeshKit& K)
		{
			for (const float Y : bDual ? TArray<float>{ -15.f, 15.f } : TArray<float>{ 0.f })
			{
				K.Paint(FLC(0.02f, 0.02f, 0.02f));
				K.Torus(FTransform(FVector(0.f, Y, 0.f)), 37.f, 13.f, 24, 8);
				K.Paint(FLC(0.45f, 0.45f, 0.46f), HGSurf::Satin);
				K.Tube(FVector(0.f, Y - 10.f, 0.f), FVector(0.f, Y + 10.f, 0.f), 27.f, 27.f, 16, true);
			}
			K.Paint(FLC(0.2f, 0.2f, 0.2f), HGSurf::Satin);
			K.Tube(FVector(0.f, bDual ? -27.f : -13.f, 0.f), FVector(0.f, bDual ? 27.f : 13.f, 0.f), 9.f, 9.f, 8, true);
		});
	};
	UStaticMeshComponent* KitBody = FHGMeshKit::AddComponent(this, BodyRoot, BodyMesh, TEXT("KitBody"), FVector::ZeroVector);
	if (!KitBody)
	{
		return;   // -hgnokit: keep the greybox
	}
	KitBody->SetCullDistance(60000.f);
	if (UMaterialInstanceDynamic* MID = KitBody->CreateDynamicMaterialInstance(0, FHGMeshKit::VertexColorMaterial()))
	{
		const FLC Cabs[3] = { FLC(0.03f, 0.16f, 0.5f), FLC(0.04f, 0.22f, 0.1f), FLC(0.6f, 0.6f, 0.58f) };
		MID->SetVectorParameterValue(TEXT("Tint"), Cabs[GetTypeHash(GetName()) % 3]);
	}
	const float WheelX[] = { 270.f, -90.f, -250.f };
	for (int32 i = 0; i < 3; ++i)
	{
		for (const float Side : { -1.f, 1.f })
		{
			UStaticMeshComponent* W = FHGMeshKit::AddComponent(this, BodyRoot, MakeWheel(i > 0), *FString::Printf(TEXT("KitWheel%d"), KitWheels.Num()),
				FVector(WheelX[i], Side * (i > 0 ? 92.f : 100.f), 50.f));
			if (W)
			{
				W->SetCullDistance(30000.f);
				KitWheels.Add(W);
			}
		}
	}
	for (UStaticMeshComponent* P : Parts)
	{
		P->SetVisibility(false);
	}
}
