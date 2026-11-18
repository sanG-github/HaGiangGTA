#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HGPoliceHelicopter.generated.h"

class UAudioComponent;
class USoundBase;
class USpotLightComponent;
class UStaticMeshComponent;

/**
 * Police helicopter (3+ stars): follows the player through the Tu San canyon,
 * keeps clear of the terrain, banks into turns and pins the player with a
 * volumetric searchlight cutting through the fog.
 */
UCLASS()
class HAGIANGGTA_API AHGPoliceHelicopter : public AActor
{
	GENERATED_BODY()

public:
	AHGPoliceHelicopter();

	virtual void Tick(float DeltaSeconds) override;

	void SetChaseTarget(APawn* Target) { ChaseTarget = Target; }

	/** Climb away and disappear (stars cleared). */
	void FlyAway();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Helicopter") float FollowDistance = 6000.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Helicopter") float FollowHeight = 8000.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Helicopter") float MinTerrainClearance = 3500.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Helicopter") float MaxSpeed = 5000.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Helicopter") TSoftObjectPtr<USoundBase> RotorSound;

protected:
	virtual void BeginPlay() override;

	UStaticMeshComponent* AddPart(FName Name, USceneComponent* Parent, UStaticMesh* Mesh, const FVector& Loc, const FRotator& Rot, const FVector& SizeCm, const FLinearColor& Color);

	UPROPERTY(VisibleAnywhere, Category = "Components") TObjectPtr<USceneComponent> Body;
	UPROPERTY(VisibleAnywhere, Category = "Components") TObjectPtr<USceneComponent> MainRotor;
	UPROPERTY(VisibleAnywhere, Category = "Components") TObjectPtr<USceneComponent> TailRotor;
	UPROPERTY(VisibleAnywhere, Category = "Components") TObjectPtr<USpotLightComponent> Searchlight;

private:
	UPROPERTY(Transient) TWeakObjectPtr<APawn> ChaseTarget;
	UPROPERTY(Transient) TObjectPtr<UAudioComponent> RotorAudio;
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Parts;
	TArray<FLinearColor> PartColors;

	FVector Velocity = FVector::ZeroVector;
	float RotorAngle = 0.f;
	float Time = 0.f;
	bool bLeaving = false;
	float LeaveTime = 0.f;
};
