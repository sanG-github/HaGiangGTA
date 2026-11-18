#pragma once

#include "CoreMinimal.h"
#include "HGGroundVehicle.h"
#include "HGGreyboxCarPawn.generated.h"

class UStaticMeshComponent;

/**
 * Checkpoint 01 greybox car: box body + cabin on the shared arcade
 * ground-vehicle model. Controls: W/S, A/D, Space, Shift, R.
 */
UCLASS()
class HAGIANGGTA_API AHGGreyboxCarPawn : public AHGGroundVehicle
{
	GENERATED_BODY()

public:
	AHGGreyboxCarPawn();

protected:
	virtual void BeginPlay() override;
	virtual void UpdateVisuals(float DeltaSeconds, float Steer) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> BodyMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> CabinMesh;

	UPROPERTY(EditAnywhere, Category = "Tuning")
	float BodyLeanDeg = 4.f;
};
