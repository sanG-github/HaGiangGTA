#include "HGTruckPawn.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/SpringArmComponent.h"
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
	AutoDriveLookahead = 1700.f;

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
	for (int32 i = 0; i < Parts.Num() && i < PartColors.Num(); ++i)
	{
		if (UMaterialInstanceDynamic* MID = Parts[i]->CreateAndSetMaterialInstanceDynamic(0))
		{
			MID->SetVectorParameterValue(TEXT("Color"), PartColors[i]);
		}
	}
	LoadedHorn = HornSound.LoadSynchronous();
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
		UGameplayStatics::PlaySoundAtLocation(this, LoadedHorn, GetActorLocation(), 1.f, FMath::FRandRange(0.97f, 1.03f));
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
