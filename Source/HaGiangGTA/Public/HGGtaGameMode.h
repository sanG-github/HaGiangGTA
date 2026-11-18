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
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	// -hgperf benchmark: frame-time statistics at each viewpoint, written to the log, then quit.
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
	void TickPlayTest(float DeltaSeconds);
	void PlayCheck(const TCHAR* Label, bool bShot);
};
