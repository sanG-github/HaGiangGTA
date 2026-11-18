#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "HGGtaHUD.generated.h"

class UHGGtaHudWidget;

/** Puts the C++ UMG GTA-style HUD widget on screen. */
UCLASS()
class HAGIANGGTA_API AHGGtaHUD : public AHUD
{
	GENERATED_BODY()

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(Transient) TObjectPtr<UHGGtaHudWidget> Widget;
};
