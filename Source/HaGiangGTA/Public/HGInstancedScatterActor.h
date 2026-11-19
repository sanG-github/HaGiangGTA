#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HGInstancedScatterActor.generated.h"

class UHierarchicalInstancedStaticMeshComponent;
class UMaterialInterface;
class UStaticMesh;

/**
 * One mesh, many instances (HISM): cliffs, boulders, bushes, road posts.
 * Filled from Python with packed floats so tens of thousands of instances
 * load in one call. Per-instance cull distances keep the 60 FPS budget.
 */
UCLASS()
class HAGIANGGTA_API AHGInstancedScatterActor : public AActor
{
	GENERATED_BODY()

public:
	AHGInstancedScatterActor();

	/** Mesh, optional material override, cull start/end (cm, 0 = never), collision on/off. */
	UFUNCTION(BlueprintCallable, Category = "HaGiang|Scatter")
	void SetupScatter(UStaticMesh* Mesh, UMaterialInterface* Material, float CullStart, float CullEnd, bool bCollision, bool bCastShadow);

	/**
	 * Add instances from packed floats, 9 per instance:
	 * X, Y, Z (cm, world), Yaw, Pitch, Roll (deg), ScaleX, ScaleY, ScaleZ.
	 * Returns the number of instances added.
	 */
	UFUNCTION(BlueprintCallable, Category = "HaGiang|Scatter")
	int32 AddPackedInstances(const TArray<float>& Packed);

	UFUNCTION(BlueprintCallable, Category = "HaGiang|Scatter")
	void ClearInstances();

	UFUNCTION(BlueprintPure, Category = "HaGiang|Scatter")
	int32 GetInstanceCount() const;

	UHierarchicalInstancedStaticMeshComponent* GetInstances() const { return Instances; }

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> Instances;

protected:
	/** Runtime overrides for A/B (-hgtreecull=<m>, -hgtreeshadow=0): trees are the bulk of the instances drawn at
	 *  the Doc Tham Ma spawn view (and their shadow passes). */
	virtual void BeginPlay() override;
};
