#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "HGGtaGameMode.generated.h"

/** Full game (CP05): on-foot player, GTA-style UMG HUD, wanted system. */
UCLASS()
class HAGIANGGTA_API AHGGtaGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AHGGtaGameMode();

protected:
	/** Always use the level's PlayerStart: the engine rejects a start whose capsule test touches anything
	 *  (e.g. the carved road bank) and then spawns the player at the world origin, under the landscape. */
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;
	/** Spawn on the ground found by a trace under the start (never inside or under the terrain). */
	virtual APawn* SpawnDefaultPawnAtTransform_Implementation(AController* NewPlayer, const FTransform& SpawnTransform) override;
	/** The engine resets the control rotation to the PlayerStart's (pitch 0) after possession, which undid the
	 *  pitch set in AHGPlayerCharacter::PawnClientRestart: start looking slightly down the road instead. */
	virtual void FinishRestartPlayer(AController* NewPlayer, const FRotator& StartRotation) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	// ---- Ambient audio (all modes): wind with speed / altitude, birds when slow, tyres by surface ----
	UPROPERTY(Transient) TObjectPtr<class UAudioComponent> WindAudio;
	UPROPERTY(Transient) TObjectPtr<class UAudioComponent> BirdsAudio;
	UPROPERTY(Transient) TObjectPtr<class UAudioComponent> TyreAudio;
	float WindVol = 0.f, BirdsVol = 0.f, TyreVol = 0.f;
	void StartAmbientAudio();
	void UpdateAmbientAudio(float DeltaSeconds);
	FString AudioState() const;

	// -hgperf benchmark: frame-time statistics at each review camera (Generated/Heightmaps/HaGiang_review_cameras.json,
	// written by Scripts/hg_lib/shots.py) or, without it, at each viewpoint; written to the log, then quit.
	TArray<float> PerfFov;
	UPROPERTY(Transient) TObjectPtr<class ACameraActor> PerfCamera;
	double PerfGpuAccum = 0.0;
	bool bPerfRun = false;
	bool bPerfMoved = false;
	int32 PerfStage = -1;
	float PerfTime = 0.f;
	double PerfAccum = 0.0;
	double PerfWorst = 0.0;
	int32 PerfFrames = 0;
	TArray<FTransform> PerfSpots;
	TArray<FString> PerfNames;

	// -hgplaytest: scripted session through the real player camera + HUD (spawn, walk, mount a bike,
	// ride Doc Tham Ma, Ma Pi Leng and the Ta Lang descent on auto-drive), logging camera / pawn /
	// ground / road checks and taking viewport screenshots to Saved/HG_CI/Play, then quit.
	bool bPlayTest = false;
	float PlayTime = 0.f;
	int32 PlayShot = 0;
	float NextShot = 0.f;
	int32 PlayPhase = 0;
	FVector PlayLastLoc = FVector::ZeroVector;
	TWeakObjectPtr<class AHGGroundVehicle> PlayBike;
	TWeakObjectPtr<class AHGRoadSplineActor> PlayRoad;
	// Key-ridden stages: a "human" presses W/S/A/D (simulated key events through the real input stack).
	TArray<FKey> PlayKeysDown;
	float PlayMaxKmh = 0.f;
	int32 PlayOrbitShot = 0;
	float PlayOffRoadSecs = 0.f;
	FVector PlayStageStart = FVector::ZeroVector;
	FVector PlayCheckpointLoc = FVector::ZeroVector;
	float PlayFpsAccum = 0.f, PlayFpsWindow = 1.f, PlayTotalTime = 0.f, PlayWorstMs = 0.f;
	int32 PlayFpsCount = 0, PlayFpsFrames = 0, PlayTotalFrames = 0;
	float PlayFps() const;
	void PlaySetKey(const FKey& Key, bool bDown);
	void PlayReleaseKeys();
	/** One tick of the key-pressing rider: steer for the right lane ahead, brake for the bends. */
	void PlayDriveKeys(float DeltaSeconds);
	// Boat stage: get off the bike at the Ta Lang dock, board a cano with [F], ride the Tu San gorge on the
	// keys following the tourist boats' cruise line.
	TWeakObjectPtr<class ANhoQueBoatPawn> PlayBoat;
	TArray<FVector> PlayBoatPath;
	int32 PlayBoatIndex = 0;
	float PlayBoatCheckTime = 0.f, PlayBoatReverse = 0.f, PlayBoatArrivedAt = -1.f;
	FVector PlayBoatCheckLoc = FVector::ZeroVector;
	void PlayDriveBoatKeys(float DeltaSeconds);
	void TickPlayTest(float DeltaSeconds);

	// Traffic around the player (per stage): near misses, AI dead stops, oncoming vehicles met, vehicles that
	// overtook the player; lane error of the auto-driven bike.
	int32 PlayNearMiss = 0, PlayStops = 0, PlayOncoming = 0, PlayOvertookUs = 0;
	int32 PlayTotNear = 0, PlayTotStops = 0, PlayTotOncoming = 0, PlayTotOvertookUs = 0;
	TMap<TWeakObjectPtr<AActor>, double> PlayNearCooldown;
	TMap<TWeakObjectPtr<AActor>, float> PlayStopTime;
	TMap<TWeakObjectPtr<AActor>, float> PlayLongOffset;
	TSet<TWeakObjectPtr<AActor>> PlayMet;
	double PlayLaneErrSum = 0.0;
	float PlayLaneErrMax = 0.f;
	int32 PlayLaneSamples = 0;
	void PlayTrafficTick(float DeltaSeconds);
	void PlayResetStageStats();
	FString PlayTrafficSummary() const;
	void PlayCheck(const TCHAR* Label, bool bShot);
};
