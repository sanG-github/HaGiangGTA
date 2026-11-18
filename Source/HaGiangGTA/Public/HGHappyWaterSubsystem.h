#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "HGHappyWaterSubsystem.generated.h"

class APostProcessVolume;
class UCameraModifier;

/**
 * "Happy Water" (rượu ngô): for a few seconds the player gets 2x nitro, the
 * screen sways and smears (unbound PostProcessVolume + camera modifier) and
 * the steering is inverted.
 */
UCLASS()
class HAGIANGGTA_API UHGHappyWaterSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** Start (or extend) the effect. */
	UFUNCTION(BlueprintCallable, Category = "HaGiang|HappyWater")
	void Drink(float DurationSeconds = 12.f);

	UFUNCTION(BlueprintPure, Category = "HaGiang|HappyWater")
	bool IsActive() const { return Remaining > 0.f; }

	UFUNCTION(BlueprintPure, Category = "HaGiang|HappyWater")
	float GetRemaining() const { return Remaining; }

	/** 0..1 with smooth ramp in/out. */
	UFUNCTION(BlueprintPure, Category = "HaGiang|HappyWater")
	float GetIntensity() const { return Intensity; }

	float GetSpeedMultiplier() const { return Remaining > 0.f ? 2.f : 1.f; }
	bool AreControlsInverted() const { return Remaining > 0.5f; }

private:
	void EnsureEffectActors();
	void ApplyPostProcess();

	UPROPERTY(Transient) TObjectPtr<APostProcessVolume> Volume;
	UPROPERTY(Transient) TObjectPtr<UCameraModifier> Modifier;

	float Remaining = 0.f;
	float Duration = 12.f;
	float Intensity = 0.f;
	float Time = 0.f;
};
