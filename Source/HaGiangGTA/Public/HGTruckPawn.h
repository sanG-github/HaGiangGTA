#pragma once

#include "CoreMinimal.h"
#include "HGGroundVehicle.h"
#include "HGTruckPawn.generated.h"

class USoundBase;
class UStaticMeshComponent;

/**
 * Blue Thaco-style dump truck (greybox). Drives the pass on auto-drive in the
 * opposite direction and blasts its air horn before blind hairpins or when
 * something is in its lane. Can be hijacked with [F].
 */
UCLASS()
class HAGIANGGTA_API AHGTruckPawn : public AHGGroundVehicle
{
	GENERATED_BODY()

public:
	AHGTruckPawn();

	UFUNCTION(BlueprintCallable, Category = "HaGiang|Truck")
	void Honk();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Truck")
	TSoftObjectPtr<USoundBase> HornSound;

protected:
	virtual void BeginPlay() override;
	virtual void UpdateVisuals(float DeltaSeconds, float Steer) override;
	virtual void OnAutoDriveObstacle(AActor* Obstacle) override;

	UStaticMeshComponent* AddPart(FName Name, UStaticMesh* Mesh, const FVector& Location, const FRotator& Rotation, const FVector& SizeCm, const FLinearColor& Color);

	UPROPERTY(VisibleAnywhere, Category = "Components") TObjectPtr<USceneComponent> BodyRoot;

private:
	UPROPERTY(Transient) TObjectPtr<USoundBase> LoadedHorn;
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Parts;
	TArray<FLinearColor> PartColors;
	float HornCooldown = 0.f;
	float BlindCurveCheck = 0.f;
};
