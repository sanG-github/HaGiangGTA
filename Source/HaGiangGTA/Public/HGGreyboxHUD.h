#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "HGGreyboxHUD.generated.h"

/** Minimal canvas HUD for the greybox: speed, state and control hints. */
UCLASS()
class HAGIANGGTA_API AHGGreyboxHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;
};
