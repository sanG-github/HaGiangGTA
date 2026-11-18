#include "HGGreyboxCarPawn.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

AHGGreyboxCarPawn::AHGGreyboxCarPawn()
{
	DisplayName = NSLOCTEXT("HaGiang", "GreyboxCar", "Greybox Car");
	bShowDriver = false;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));

	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
	BodyMesh->SetupAttachment(Collision);
	BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BodyMesh->SetRelativeLocation(FVector(0.f, 0.f, -15.f));
	BodyMesh->SetRelativeScale3D(FVector(4.0f, 1.8f, 0.7f));

	CabinMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CabinMesh"));
	CabinMesh->SetupAttachment(BodyMesh);
	CabinMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// Relative to the scaled body: (-30, 0, +60) cm offset, 2.0 x 1.6 x 0.5 m.
	CabinMesh->SetRelativeLocation(FVector(-30.f / 4.0f, 0.f, 60.f / 0.7f));
	CabinMesh->SetRelativeScale3D(FVector(2.0f / 4.0f, 1.6f / 1.8f, 0.5f / 0.7f));

	if (CubeMesh.Succeeded())
	{
		BodyMesh->SetStaticMesh(CubeMesh.Object);
		CabinMesh->SetStaticMesh(CubeMesh.Object);
	}

	DriverSeat->SetRelativeLocation(FVector(-20.f, -40.f, 10.f));
}

void AHGGreyboxCarPawn::BeginPlay()
{
	Super::BeginPlay();

	if (UMaterialInstanceDynamic* BodyMID = BodyMesh->CreateAndSetMaterialInstanceDynamic(0))
	{
		BodyMID->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.75f, 0.05f, 0.04f));
	}
	if (UMaterialInstanceDynamic* CabinMID = CabinMesh->CreateAndSetMaterialInstanceDynamic(0))
	{
		CabinMID->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.05f, 0.05f, 0.06f));
	}
}

void AHGGreyboxCarPawn::UpdateVisuals(float DeltaSeconds, float Steer)
{
	const float SpeedAlpha = FMath::Clamp(FMath::Abs(GetSpeed()) / MaxForwardSpeed, 0.f, 1.f);
	const FRotator Lean(0.f, 0.f, Steer * SpeedAlpha * BodyLeanDeg);
	BodyMesh->SetRelativeRotation(FMath::RInterpTo(BodyMesh->GetRelativeRotation(), Lean, DeltaSeconds, 6.f));
}
