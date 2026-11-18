#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "HGOpenWorldGameMode.generated.h"

/** Open-world game mode (CP02+): on-foot player, vehicles placed in the level. */
UCLASS()
class HAGIANGGTA_API AHGOpenWorldGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AHGOpenWorldGameMode();
};
