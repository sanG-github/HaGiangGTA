#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HGDogActor.generated.h"

class UStaticMeshComponent;
class AHGRoadSplineActor;

UENUM(BlueprintType)
enum class EHGDogMode : uint8
{
	/** Lies by its house, head up when someone passes. */
	Lie,
	/** Trots along the road shoulder (follows the road spline at its offset, +-RangeM metres). */
	Trot
};

/**
 * Round 6B (agent A): a Vietnamese village dog (cho vang / den / ven) from the mesh kit. Lies by the houses or trots
 * along the road shoulder; barks at passing bikes and chases them a few metres - always on the shoulder, never on
 * the carriageway. No collision: it can never hold up the traffic AI.
 */
UCLASS()
class HAGIANGGTA_API AHGDogActor : public AActor
{
	GENERATED_BODY()

public:
	AHGDogActor();
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dog") EHGDogMode Mode = EHGDogMode::Lie;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dog") FLinearColor CoatColor = FLinearColor(0.62f, 0.4f, 0.17f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dog") float RangeM = 35.f;

	/** Session counters (playtest log). */
	static int32 Barks;
	static int32 Chases;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, Category = "Components") TObjectPtr<USceneComponent> Root;
	UPROPERTY(VisibleAnywhere, Category = "Components") TObjectPtr<USceneComponent> Visual;

private:
	void BuildKit();
	void Bark();
	FVector ShoulderPoint(float AlongCm) const;

	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> KitHead;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> KitTail;
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> KitLegs;

	TWeakObjectPtr<AHGRoadSplineActor> Road;
	float HomeD = 0.f;           // road distance of home (cm)
	float Lat = 0.f;             // signed offset from the road centre (cm), kept outside the carriageway
	float MinLat = 0.f;          // |Lat| never below this (road half width + margin)
	float Along = 0.f;           // trot position relative to HomeD (cm)
	float Dir = 1.f;
	float Pause = 0.f;
	FVector Home = FVector::ZeroVector;
	float HomeYaw = 0.f;
	float Speed = 0.f;           // cm/s, for the legs
	float StepPhase = 0.f;
	float Clock = 0.f;
	float CheckTimer = 0.f;
	float ChaseTime = 0.f;
	float ChaseCooldown = 0.f;
	TWeakObjectPtr<AActor> ChaseTarget;
	float BarkLeft = 0.f;
	float BarkGap = 0.f;
	float GroundTimer = 0.f;
	float GroundZ = 0.f;
};
