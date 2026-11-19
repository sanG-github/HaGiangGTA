#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "InputCoreTypes.h"
#include "HGMissionSubsystem.generated.h"

class AHGPlayerCharacter;
class AHGRoadSplineActor;
class ANhoQueBoatPawn;
class AHGViewpointActor;

/** Pause menu rows (Esc). */
enum class EHGPauseItem : uint8 { Resume, Restart, Quit, Count };

UENUM(BlueprintType)
enum class EHGMissionState : uint8
{
	Available,
	Active,
	Done,
	Failed
};

/** One job in the loop. Positions are world cm; set up from the road / viewpoints at BeginPlay. */
USTRUCT(BlueprintType)
struct FHGMission
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) FName Id;
	UPROPERTY(BlueprintReadOnly) FString Title;
	/** Ride / walk into this ring (RadiusCm) at walking pace to take the job. */
	UPROPERTY(BlueprintReadOnly) FVector Start = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly) FVector Target = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly) float RadiusCm = 900.f;
	UPROPERTY(BlueprintReadOnly) int32 Reward = 0;
	UPROPERTY(BlueprintReadOnly) EHGMissionState State = EHGMissionState::Available;
	/** Seconds since the job was taken (pay tips for a quick ride). */
	float Elapsed = 0.f;
};

/**
 * The mission loop (GTA-style jobs on the real route), all three with a HUD objective, a minimap marker and a
 * pay-out in VND (through UHGWantedSubsystem's wallet):
 *  - "Xe om Dong Van": pick up a backpacker at the Dong Van turn (on a bike, stop in the ring), carry them to the
 *    Ma Pi Leng viewpoint: 150,000 VND + a tip for a quick ride.
 *  - "Anh Ma Pi Leng": at the Ma Pi Leng viewpoint, aim the camera at the Tu San gorge and press [P]: 50,000 VND,
 *    + up to 50,000 for a well-framed shot.
 *  - "Tron CSGT": at the top of the Ta Lang road the police are tipped off (3 stars); get down the hairpins to the
 *    Ta Lang dock without being busted: 300,000 VND. Busted = failed (can be retried).
 *  - "Thuyen Tu San": in the hired cano at the Ta Lang dock, take two tourists down the gorge to the Ma Pi Leng
 *    bend and back to the dock: 250,000 VND + a tip for a quick tour.
 * Also the Ta Lang dock boat hire: on foot near the dock, [F] pays 100,000 VND and puts you in the cano.
 *
 * Plus the game-flow UI state the HUD draws: the mission list (Tab / M) with the one upgrade for sale (grip
 * tyres, [B]), the pause menu (Esc: resume / restart at a viewpoint / quit) and the intro card on first spawn.
 */
UCLASS()
class HAGIANGGTA_API UHGMissionSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickableWhenPaused() const override { return true; }
	virtual void Deinitialize() override;

	// ---- Player profile (Saved/HGProfile.ini; the scripted runs use HGProfile_test.ini) ----
	void SaveProfile();
	bool LoadProfile();
	FString ProfilePath() const;

	const TArray<FHGMission>& GetMissions() const { return Missions; }
	const FHGMission* GetActive() const { return Missions.IsValidIndex(ActiveIndex) ? &Missions[ActiveIndex] : nullptr; }
	const FHGMission* Find(FName Id) const { return Missions.FindByPredicate([Id](const FHGMission& M) { return M.Id == Id; }); }

	/** Objective line for the HUD (empty when nothing to say). */
	const FString& GetObjective() const { return Objective; }
	/** Context prompt (job offer, boat hire); shown when the HUD has no prompt of its own. */
	const FString& GetPrompt() const { return Prompt; }
	/** Big centre banner ("+150.000 VNĐ", "NHIỆM VỤ THẤT BẠI") and its remaining seconds. */
	const FString& GetBanner() const { return Banner; }
	float GetBannerTime() const { return BannerTime; }
	/** Seconds since the photo shutter (white flash on the HUD). */
	float GetFlashTime() const { return FlashTime; }

	/** Drop the active job (scripted playtest between missions). */
	void AbortActive() { if (Missions.IsValidIndex(ActiveIndex)) { Missions[ActiveIndex].State = EHGMissionState::Available; } ActiveIndex = INDEX_NONE; Objective.Reset(); }

	/** Distance (cm, 2D) from the player to the active job's target, or -1. */
	float GetActiveDistance() const { return ActiveDistance; }

	/** On foot near the dock: pay and board the dock cano. Returns true if it handled [F]. */
	bool TryBoatHire(AHGPlayerCharacter* Player);

	/** Boat hire point (world cm) and price. */
	FVector GetBoatHirePoint() const { return BoatHirePoint; }
	static constexpr int32 BoatHirePrice = 100000;

	// ---- Mission list + upgrade ----
	bool IsListOpen() const { return bListOpen; }
	/** Grip tyres ("lop gai"): the player's bike keeps 90 % of its top speed on the Ta Lang concrete (70 % stock)
	 *  and 65 % off the road (45 %), with half the off-road drag. */
	bool HasGripTyres() const { return bGripTyres; }
	static bool PlayerHasGripTyres(const UWorld* World);
	static constexpr int32 GripTyresPrice = 200000;
	/** Buy the tyres (the list's [B]); false + a banner if already owned / not enough money. */
	bool BuyGripTyres();

	// ---- Pause menu ----
	bool IsPauseOpen() const { return bPauseOpen; }
	EHGPauseItem GetPauseItem() const { return PauseItem; }
	/** Viewpoint the "restart" row points at (Left / Right cycles). */
	FString GetRestartName() const;
	void SetPaused(bool bPause);
	void RestartAtViewpoint(int32 Index);
	int32 GetViewpointCount() const { return Viewpoints.Num(); }

	// ---- Intro card ----
	bool IsIntroOpen() const { return IntroTime > 0.f; }
	float GetIntroTime() const { return IntroTime; }
	void CloseIntro() { IntroTime = 0.f; }

	/** Boat tour: on the way back to the dock. */
	bool IsBoatTourReturning() const { return bBoatLegBack; }

private:
	void SetupMissions();
	void StartMission(int32 Index, APawn* Player);
	void Complete(FHGMission& M, int32 Pay, const FString& Line);
	void Fail(FHGMission& M, const FString& Why);
	void TickActive(FHGMission& M, APawn* Player, float DeltaTime);
	ANhoQueBoatPawn* FindHireBoat() const;
	void HandleMenuInput(APlayerController* PC);
	void SpawnBoatGuests(ANhoQueBoatPawn* Boat);
	void ClearBoatGuests();

	TArray<FHGMission> Missions;
	int32 ActiveIndex = INDEX_NONE;
	bool bSetup = false;
	FString Objective;
	FString Prompt;
	FString Banner;
	float BannerTime = 0.f;
	float FlashTime = 10.f;
	FVector PhotoAim = FVector::ZeroVector;
	FVector BoatHirePoint = FVector::ZeroVector;
	int32 BustCountAtStart = 0;
	bool bPassengerAboard = false;
	float ActiveDistance = -1.f;
	TWeakObjectPtr<AHGRoadSplineActor> Road;

	bool bListOpen = false;
	bool bProfileLoaded = false;
	int32 SavedMoney = -1;
	float ProfileTimer = 0.f;
	TMap<FKey, bool> MenuKeysDown;
	bool bGripTyres = false;
	bool bPauseOpen = false;
	EHGPauseItem PauseItem = EHGPauseItem::Resume;
	int32 RestartIndex = 0;
	TArray<TWeakObjectPtr<AHGViewpointActor>> Viewpoints;
	float IntroTime = 0.f;
	bool bIntroShown = false;
	bool bBoatLegBack = false;
	FVector BoatBend = FVector::ZeroVector;
	TArray<TWeakObjectPtr<UActorComponent>> BoatGuests;
};
