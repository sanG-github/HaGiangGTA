#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "HGWantedSubsystem.generated.h"

class AHGPoliceBikePawn;
class AHGPoliceHelicopter;
class AHGRoadSplineActor;
class UPrimitiveComponent;

/**
 * GTA-style wanted level for the pass: hitting road posts, tour convoys,
 * trucks, cows or tourists raises heat -> 1..5 stars. Stars bring siren
 * patrol bikes; from 3 stars a helicopter with a volumetric searchlight
 * hunts you through the Tu San canyon. Lose them (no police within range)
 * and the heat cools down. Stop next to a police bike and you are BUSTED.
 * Also holds the player's money (starts at 500,000 VND).
 */
UCLASS()
class HAGIANGGTA_API UHGWantedSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** A player vehicle hit something at ImpactSpeed (cm/s). */
	void ReportImpact(AActor* Victim, UPrimitiveComponent* VictimComponent, float ImpactSpeed);

	UFUNCTION(BlueprintCallable, Category = "HaGiang|Wanted")
	void AddHeat(float Amount, const FString& Reason);

	UFUNCTION(BlueprintCallable, Category = "HaGiang|Wanted")
	void ClearWanted();

	UFUNCTION(BlueprintPure, Category = "HaGiang|Wanted")
	int32 GetStars() const { return Stars; }

	UFUNCTION(BlueprintPure, Category = "HaGiang|Wanted")
	int32 GetMoney() const { return Money; }

	/** True while police can see the player (stars flash otherwise, like GTA). */
	UFUNCTION(BlueprintPure, Category = "HaGiang|Wanted")
	bool IsPlayerSpotted() const { return bSpotted; }

	float GetBustedMessageTime() const { return BustedMessageTime; }
	const FString& GetLastReason() const { return LastReason; }
	float GetReasonTime() const { return ReasonTime; }

	void AddMoney(int32 Delta) { Money = FMath::Max(0, Money + Delta); }

private:
	void UpdateStars();
	void ManagePolice(float DeltaTime);
	void SpawnPoliceBike(APawn* Player);
	void SpawnHelicopter(APawn* Player);
	void Busted();
	AHGRoadSplineActor* FindRoad();

	UPROPERTY(Transient) TArray<TObjectPtr<AHGPoliceBikePawn>> PoliceBikes;
	UPROPERTY(Transient) TObjectPtr<AHGPoliceHelicopter> Helicopter;
	UPROPERTY(Transient) TObjectPtr<AHGRoadSplineActor> CachedRoad;

	float Heat = 0.f;
	int32 Stars = 0;
	int32 Money = 500000;
	bool bSpotted = false;
	float UnseenTime = 0.f;
	float SpawnCooldown = 0.f;
	float BustTimer = 0.f;
	float BustedMessageTime = 0.f;
	float ReasonTime = 0.f;
	float ClearTimer = 0.f;
	FString LastReason;
};
