#include "HGInstancedScatterActor.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "HaGiangGTA.h"
#include "Misc/CommandLine.h"
#include "Materials/MaterialInterface.h"

AHGInstancedScatterActor::AHGInstancedScatterActor()
{
	PrimaryActorTick.bCanEverTick = false;

	Instances = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("Instances"));
	Instances->SetMobility(EComponentMobility::Static);
	Instances->SetCanEverAffectNavigation(false);
	RootComponent = Instances;
}

void AHGInstancedScatterActor::SetupScatter(UStaticMesh* Mesh, UMaterialInterface* Material, float CullStart, float CullEnd, bool bCollision, bool bCastShadow)
{
	Modify();
	Instances->Modify();
	Instances->SetStaticMesh(Mesh);
	if (Material)
	{
		const int32 NumMaterials = Mesh ? Mesh->GetStaticMaterials().Num() : 1;
		for (int32 i = 0; i < FMath::Max(1, NumMaterials); ++i)
		{
			Instances->SetMaterial(i, Material);
		}
	}
	Instances->SetCullDistances(FMath::RoundToInt(CullStart), FMath::RoundToInt(CullEnd));
	Instances->SetCollisionEnabled(bCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	Instances->SetCollisionProfileName(bCollision ? UCollisionProfile::BlockAll_ProfileName : UCollisionProfile::NoCollision_ProfileName);
	Instances->SetCastShadow(bCastShadow);
}

int32 AHGInstancedScatterActor::AddPackedInstances(const TArray<float>& Packed)
{
	const int32 Count = Packed.Num() / 9;
	if (Count == 0)
	{
		return 0;
	}
	TArray<FTransform> Transforms;
	Transforms.Reserve(Count);
	for (int32 i = 0; i < Count; ++i)
	{
		const float* P = &Packed[i * 9];
		Transforms.Emplace(FRotator(P[4], P[3], P[5]), FVector(P[0], P[1], P[2]), FVector(P[6], P[7], P[8]));
	}
	Modify();
	Instances->Modify();
	Instances->AddInstances(Transforms, /*bShouldReturnIndices*/ false, /*bWorldSpace*/ true);
	UE_LOG(LogHaGiang, Log, TEXT("%s: +%d instances (%d total)"), *GetName(), Count, Instances->GetInstanceCount());
	return Count;
}

void AHGInstancedScatterActor::ClearInstances()
{
	Modify();
	Instances->ClearInstances();
}

int32 AHGInstancedScatterActor::GetInstanceCount() const
{
	return Instances->GetInstanceCount();
}

void AHGInstancedScatterActor::BeginPlay()
{
	Super::BeginPlay();
	if (!GetActorNameOrLabel().Contains(TEXT("Scatter_tree")))
	{
		return;
	}
	float Meters = -1.f;
	if (FParse::Value(FCommandLine::Get(), TEXT("hgtreecull="), Meters) && Meters > 0.f)
	{
		Instances->SetCullDistances(FMath::RoundToInt(Meters * 75.f), FMath::RoundToInt(Meters * 100.f));
	}
	int32 Shadow = 1;
	if (FParse::Value(FCommandLine::Get(), TEXT("hgtreeshadow="), Shadow) && Shadow == 0)
	{
		Instances->SetCastShadow(false);
	}
}
