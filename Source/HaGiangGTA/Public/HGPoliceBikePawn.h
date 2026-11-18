#pragma once

#include "CoreMinimal.h"
#include "HGBikePawn.h"
#include "HGPoliceBikePawn.generated.h"

class UPointLightComponent;
class UAudioComponent;
class USoundBase;

/** Traffic police patrol bike: white bike, flashing red/blue lights, siren, chases the player along the road. */
UCLASS()
class HAGIANGGTA_API AHGPoliceBikePawn : public AHGBikePawn
{
	GENERATED_BODY()

public:
	AHGPoliceBikePawn();

	virtual void Tick(float DeltaSeconds) override;

	void SetChaseTarget(APawn* Target) { ChaseTarget = Target; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Police")
	TSoftObjectPtr<USoundBase> SirenSound;

protected:
	virtual void BeginPlay() override;
	virtual void ComputeAutoDriveInput(float DeltaSeconds, float& OutThrottle, float& OutSteer, bool& OutHandbrake) override;

	UPROPERTY(VisibleAnywhere, Category = "Components") TObjectPtr<UPointLightComponent> LightRed;
	UPROPERTY(VisibleAnywhere, Category = "Components") TObjectPtr<UPointLightComponent> LightBlue;

private:
	UPROPERTY(Transient) TWeakObjectPtr<APawn> ChaseTarget;
	UPROPERTY(Transient) TObjectPtr<UAudioComponent> SirenAudio;
	float TargetRoadDistance = 0.f;
	float RetargetTimer = 0.f;
	float FlashTime = 0.f;
	/** Emissive light-bar lamps on the rear rack (the point lights are only a faint glow). */
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> LampRed;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> LampBlue;
};
