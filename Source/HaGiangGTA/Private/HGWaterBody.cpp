#include "HGWaterBody.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

AHGWaterBody::AHGWaterBody()
{
	PrimaryActorTick.bCanEverTick = false;

	Surface = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Surface"));
	RootComponent = Surface;
	Surface->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Surface->SetCastShadow(false);
	Surface->SetCanEverAffectNavigation(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneMesh(TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (PlaneMesh.Succeeded())
	{
		Surface->SetStaticMesh(PlaneMesh.Object);
	}
}

void AHGWaterBody::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	// The engine plane is 100 x 100 uu; scale the component, not the actor, so Z stays exact.
	Surface->SetRelativeScale3D(FVector(SizeCm.X / 100.f, SizeCm.Y / 100.f, 1.f));
	if (WaterMaterial)
	{
		Surface->SetMaterial(0, WaterMaterial);
	}
}

bool AHGWaterBody::GetSurfaceZ(const FVector& Location, float& OutSurfaceZ) const
{
	const FVector Local = GetActorTransform().InverseTransformPositionNoScale(Location);
	if (FMath::Abs(Local.X) > SizeCm.X * 0.5f || FMath::Abs(Local.Y) > SizeCm.Y * 0.5f)
	{
		return false;
	}
	OutSurfaceZ = GetActorLocation().Z;
	return true;
}

float AHGWaterBody::GetWaveOffset(const FVector& Location, float TimeSeconds) const
{
	return WaveAmplitude * (0.6f * FMath::Sin(Location.X * 0.0021f + TimeSeconds * 1.3f)
		+ 0.4f * FMath::Sin(Location.Y * 0.0033f - TimeSeconds * 1.7f));
}

bool AHGWaterBody::FindSurface(const UWorld* World, const FVector& Location, float& OutSurfaceZ)
{
	if (!World)
	{
		return false;
	}
	bool bFound = false;
	float Best = -BIG_NUMBER;
	for (TActorIterator<AHGWaterBody> It(World); It; ++It)
	{
		float Z;
		if (It->GetSurfaceZ(Location, Z) && Z > Best)
		{
			Best = Z;
			bFound = true;
		}
	}
	if (bFound)
	{
		OutSurfaceZ = Best;
	}
	return bFound;
}
