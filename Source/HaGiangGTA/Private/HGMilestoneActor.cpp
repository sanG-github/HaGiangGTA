#include "HGMilestoneActor.h"

#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "HGMeshKit.h"
#include "Materials/MaterialInstanceDynamic.h"

AHGMilestoneActor::AHGMilestoneActor()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
}

void AHGMilestoneActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
}

void AHGMilestoneActor::BeginPlay()
{
	Super::BeginPlay();
	Build();
}

void AHGMilestoneActor::Build()
{
	if (bBuilt)
	{
		return;
	}
	bBuilt = true;
	// 40 x 22 cm post, 75 cm above ground, the top quarter red with a rounded crown (QL milestones).
	UStaticMesh* Stone = FHGMeshKit::Cached(TEXT("SM_HGMilestone"), [](FHGMeshKit& K)
	{
		K.Paint(FLinearColor(0.86f, 0.86f, 0.84f), HGSurf::Matte);
		K.Box(FVector(0.f, 0.f, 22.f), FVector(22.f, 40.f, 64.f));                 // white body (from 10 cm below ground)
		K.Paint(FLinearColor(0.62f, 0.05f, 0.04f), HGSurf::Satin);
		K.Box(FVector(0.f, 0.f, 60.f), FVector(22.6f, 40.6f, 14.f));               // red band
		K.Tube(FVector(-11.3f, 0.f, 66.f), FVector(11.3f, 0.f, 66.f), 20.3f, 20.3f, 14, true);   // rounded red crown
		K.Paint(FLinearColor(0.55f, 0.55f, 0.53f), HGSurf::Matte);
		K.Box(FVector(0.f, 0.f, -6.f), FVector(30.f, 48.f, 8.f));                  // plinth
	});
	UStaticMeshComponent* C = FHGMeshKit::AddComponent(this, Root, Stone, TEXT("Stone"), FVector::ZeroVector);
	if (C)
	{
		C->SetCullDistance(15000.f);
		C->CreateDynamicMaterialInstance(0, FHGMeshKit::VertexColorMaterial());
	}
	// Both faces (X+ and X-): road number on the red crown, town and distance on the white body.
	for (const float Side : { 1.f, -1.f })
	{
		auto Line = [&](const FString& Str, float Z, float Size, const FColor& Color)
		{
			UTextRenderComponent* T = NewObject<UTextRenderComponent>(this);
			T->SetupAttachment(Root);
			T->SetRelativeLocation(FVector(Side * 11.6f, 0.f, Z));
			T->SetRelativeRotation(FRotator(0.f, Side > 0.f ? 0.f : 180.f, 0.f));
			T->SetHorizontalAlignment(EHTA_Center);
			T->SetVerticalAlignment(EVRTA_TextCenter);
			T->SetWorldSize(Size);
			T->SetTextRenderColor(Color);
			T->SetText(FText::FromString(Str));
			T->SetCullDistance(4000.f);
			T->RegisterComponent();
			Texts.Add(T);
		};
		Line(RoadName, 69.f, 8.f, FColor::White);
		Line(Town, 44.f, 5.2f, FColor(20, 20, 20));
		Line(FString::Printf(TEXT("%d Km"), Km), 32.f, 7.f, FColor(20, 20, 20));
	}
}
