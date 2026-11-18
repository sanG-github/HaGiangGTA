#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HGRoadSplineActor.generated.h"

class UMaterialInterface;
class USplineComponent;
class USplineMeshComponent;
class UStaticMesh;

/**
 * The pass road as a spline. Points come from gen_heightmap.py (the same
 * centreline that was carved into the landscape), and the visible asphalt is
 * a chain of spline-mesh segments. The landscape provides collision; the
 * mesh floats a few cm above it. Later checkpoints reuse the spline for
 * posts, convoy AI and the minimap.
 */
UCLASS()
class HAGIANGGTA_API AHGRoadSplineActor : public AActor
{
	GENERATED_BODY()

public:
	AHGRoadSplineActor();

	/** Replace the spline with these world-space points (cm). */
	UFUNCTION(BlueprintCallable, Category = "HaGiang|Road")
	void SetRoadPoints(const TArray<FVector>& WorldPoints);

	/** Regenerate the spline-mesh segments (saved with the level as instance components). */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "HaGiang|Road")
	void RebuildRoadMeshes();

	UFUNCTION(BlueprintPure, Category = "HaGiang|Road")
	USplineComponent* GetSpline() const { return Spline; }

	UFUNCTION(BlueprintPure, Category = "HaGiang|Road")
	float GetRoadLength() const;

	/** Distance along the road (cm) of the point closest to WorldLocation. */
	UFUNCTION(BlueprintPure, Category = "HaGiang|Road")
	float GetDistanceAlongRoad(const FVector& WorldLocation) const;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road")
	TObjectPtr<UStaticMesh> RoadMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road")
	TObjectPtr<UMaterialInterface> RoadMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road")
	float RoadWidth = 700.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road")
	float RoadThickness = 10.f;

	/** Length of each spline-mesh piece (cm). Shorter = smoother hairpins. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road")
	float SegmentLength = 800.f;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USplineComponent> Spline;

	UPROPERTY()
	TArray<TObjectPtr<USplineMeshComponent>> Segments;
};
