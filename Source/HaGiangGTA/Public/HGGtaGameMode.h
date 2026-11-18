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
};
