#include "HGHappyWaterPickup.h"

#include "Components/PointLightComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "HGHappyWaterSubsystem.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

AHGHappyWaterPickup::AHGHappyWaterPickup()
{
	PrimaryActorTick.bCanEverTick = true;

	Trigger = CreateDefaultSubobject<USphereComponent>(TEXT("Trigger"));
	Trigger->InitSphereRadius(220.f);
	Trigger->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	Trigger->SetGenerateOverlapEvents(true);
	RootComponent = Trigger;

	Spinner = CreateDefaultSubobject<USceneComponent>(TEXT("Spinner"));
	Spinner->SetupAttachment(Trigger);
	Spinner->SetRelativeLocation(FVector(0.f, 0.f, 60.f));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	auto Make = [&](FName Name, const FVector& Loc, const FVector& Size)
	{
		UStaticMeshComponent* C = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		C->SetupAttachment(Spinner);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetRelativeLocation(Loc);
		C->SetRelativeScale3D(Size / 100.f);
		C->SetStaticMesh(CylMesh.Object);
		return C;
	};
	// Big 2-litre plastic bottle, the classic way rượu ngô is sold on the pass.
	Bottle = Make(TEXT("Bottle"), FVector(0.f, 0.f, 0.f), FVector(28.f, 28.f, 70.f));
	Label = Make(TEXT("Label"), FVector(0.f, 0.f, 0.f), FVector(29.f, 29.f, 22.f));
	Neck = Make(TEXT("Neck"), FVector(0.f, 0.f, 44.f), FVector(12.f, 12.f, 18.f));
	Cap = Make(TEXT("Cap"), FVector(0.f, 0.f, 56.f), FVector(13.f, 13.f, 6.f));

	Glow = CreateDefaultSubobject<UPointLightComponent>(TEXT("Glow"));
	Glow->SetupAttachment(Spinner);
	Glow->SetIntensityUnits(ELightUnits::Candelas);
	Glow->SetIntensity(30.f);
	Glow->SetAttenuationRadius(400.f);
	Glow->SetLightColor(FLinearColor(1.f, 0.8f, 0.3f));
	Glow->SetCastShadows(false);
}

void AHGHappyWaterPickup::BeginPlay()
{
	Super::BeginPlay();
	Trigger->OnComponentBeginOverlap.AddDynamic(this, &AHGHappyWaterPickup::OnOverlap);
	auto Paint = [](UStaticMeshComponent* C, const FLinearColor& Color)
	{
		if (UMaterialInstanceDynamic* MID = C->CreateAndSetMaterialInstanceDynamic(0))
		{
			MID->SetVectorParameterValue(TEXT("Color"), Color);
		}
	};
	Paint(Bottle, FLinearColor(0.9f, 0.85f, 0.6f));   // cloudy corn liquor
	Paint(Neck, FLinearColor(0.9f, 0.85f, 0.6f));
	Paint(Label, FLinearColor(0.7f, 0.05f, 0.03f));
	Paint(Cap, FLinearColor(0.05f, 0.35f, 0.08f));
	Phase = FMath::FRand() * 6.f;
}

void AHGHappyWaterPickup::SetCollected(bool bCollected)
{
	Spinner->SetVisibility(!bCollected, true);
	Trigger->SetCollisionEnabled(bCollected ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryOnly);
	Respawn = bCollected ? RespawnSeconds : 0.f;
}

void AHGHappyWaterPickup::OnOverlap(UPrimitiveComponent* /*OverlappedComp*/, AActor* OtherActor, UPrimitiveComponent* /*OtherComp*/,
	int32 /*OtherBodyIndex*/, bool /*bFromSweep*/, const FHitResult& /*SweepResult*/)
{
	const APawn* Pawn = Cast<APawn>(OtherActor);
	if (!Pawn || !Pawn->IsPlayerControlled() || Respawn > 0.f)
	{
		return;
	}
	if (UHGHappyWaterSubsystem* HW = GetWorld()->GetSubsystem<UHGHappyWaterSubsystem>())
	{
		HW->Drink(EffectSeconds);
	}
	SetCollected(true);
}

void AHGHappyWaterPickup::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Phase += DeltaSeconds;
	Spinner->SetRelativeLocationAndRotation(FVector(0.f, 0.f, 60.f + 12.f * FMath::Sin(Phase * 2.f)),
		FRotator(0.f, Phase * 90.f, 0.f));
	if (Respawn > 0.f)
	{
		Respawn -= DeltaSeconds;
		if (Respawn <= 0.f)
		{
			SetCollected(false);
		}
	}
}
