#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HGWaterBody.generated.h"

class UStaticMeshComponent;
class UMaterialInterface;

/**
 * A flat water surface (the Nho Que river). One big plane at the river's
 * surface height is enough: the landscape hides it everywhere except inside
 * the carved river channel. Boats and swimmers query it through FindSurface().
 */
UCLASS()
class HAGIANGGTA_API AHGWaterBody : public AActor
{
	GENERATED_BODY()

public:
	AHGWaterBody();

	virtual void OnConstruction(const FTransform& Transform) override;

	/** Water surface height (world Z) if Location's XY is inside this body. */
	bool GetSurfaceZ(const FVector& Location, float& OutSurfaceZ) const;

	/** Searches all water bodies in the world. Returns false if Location is not over water. */
	static bool FindSurface(const UWorld* World, const FVector& Location, float& OutSurfaceZ);

	/** Water area size in cm (X, Y). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water")
	FVector2D SizeCm = FVector2D(403200.f, 403200.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water")
	TObjectPtr<UMaterialInterface> WaterMaterial;

	/** Gentle swell used by boats and swimmers (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water")
	float WaveAmplitude = 4.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water")
	float CurrentSpeed = 60.f;  // cm/s along the river (cosmetic for now)

	float GetWaveOffset(const FVector& Location, float TimeSeconds) const;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Surface;
};
