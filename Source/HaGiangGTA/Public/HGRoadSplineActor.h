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

	/** From this distance along the spline (cm) the road changes surface (e.g. the narrow concrete
	 *  Ta Lang road after the QL4C asphalt). < 0 = one surface for the whole road. Gameplay (AI, minimap,
	 *  police) still sees a single continuous road. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road")
	float SecondaryFromDistance = -1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road")
	TObjectPtr<UMaterialInterface> SecondaryMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road")
	float SecondaryWidth = 400.f;

	/** Road width (cm) at a distance along the spline: blends to SecondaryWidth over 20 m. */
	UFUNCTION(BlueprintPure, Category = "HaGiang|Road")
	float GetWidthAtDistance(float DistanceCm) const;

	/** Length of each spline-mesh piece (cm). Shorter = smoother hairpins. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road")
	float SegmentLength = 800.f;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USplineComponent> Spline;

	UPROPERTY()
	TArray<TObjectPtr<USplineMeshComponent>> Segments;

protected:
	/** Draw distance (cm) of the road segments at runtime: the gorge views drew 280 separate road segments (each
	 *  with its own material instance since round 3B) for road that is a few pixels up on the rim. 0 = no limit;
	 *  -hgroadcull=<m> overrides it (A/B). */
	UPROPERTY(EditAnywhere, Category = "Road") float RuntimeCullDistance = 90000.f;
	virtual void BeginPlay() override;
};
