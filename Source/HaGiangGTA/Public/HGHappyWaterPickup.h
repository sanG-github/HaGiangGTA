#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HGHappyWaterPickup.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UPointLightComponent;

/** A bottle of rượu ngô ("Happy Water"): drive or walk through it for 2x nitro and a very wobbly screen. */
UCLASS()
class HAGIANGGTA_API AHGHappyWaterPickup : public AActor
{
	GENERATED_BODY()

public:
	AHGHappyWaterPickup();

	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HappyWater")
	float EffectSeconds = 12.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HappyWater")
	float RespawnSeconds = 45.f;

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UPROPERTY(VisibleAnywhere, Category = "Components") TObjectPtr<USphereComponent> Trigger;
	UPROPERTY(VisibleAnywhere, Category = "Components") TObjectPtr<USceneComponent> Spinner;
	UPROPERTY(VisibleAnywhere, Category = "Components") TObjectPtr<UStaticMeshComponent> Bottle;
	UPROPERTY(VisibleAnywhere, Category = "Components") TObjectPtr<UStaticMeshComponent> Neck;
	UPROPERTY(VisibleAnywhere, Category = "Components") TObjectPtr<UStaticMeshComponent> Cap;
	UPROPERTY(VisibleAnywhere, Category = "Components") TObjectPtr<UStaticMeshComponent> Label;
	UPROPERTY(VisibleAnywhere, Category = "Components") TObjectPtr<UPointLightComponent> Glow;

private:
	void SetCollected(bool bCollected);
	float Respawn = 0.f;
	float Phase = 0.f;
};
