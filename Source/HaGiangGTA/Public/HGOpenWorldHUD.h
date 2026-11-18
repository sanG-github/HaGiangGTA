#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "HGOpenWorldHUD.generated.h"

/**
 * Canvas HUD for CP02: mode (on foot / swimming / vehicle), speed, altitude,
 * and the [F] prompt. Replaced by the UMG GTA-style HUD in CP05.
 */
UCLASS()
class HAGIANGGTA_API AHGOpenWorldHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;
};
