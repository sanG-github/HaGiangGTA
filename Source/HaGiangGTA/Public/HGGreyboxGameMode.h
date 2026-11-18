#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "HGGreyboxGameMode.generated.h"

/** Checkpoint 01 game mode: spawns the greybox car at the PlayerStart. */
UCLASS()
class HAGIANGGTA_API AHGGreyboxGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AHGGreyboxGameMode();
};
