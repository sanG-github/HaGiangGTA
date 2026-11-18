#include "HGGtaHUD.h"

#include "Blueprint/UserWidget.h"
#include "GameFramework/PlayerController.h"
#include "HGGtaHudWidget.h"

void AHGGtaHUD::BeginPlay()
{
	Super::BeginPlay();
	if (APlayerController* PC = GetOwningPlayerController())
	{
		Widget = CreateWidget<UHGGtaHudWidget>(PC, UHGGtaHudWidget::StaticClass());
		if (Widget)
		{
			Widget->AddToViewport(0);
		}
	}
}
