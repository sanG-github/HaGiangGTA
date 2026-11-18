#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HGGtaHudWidget.generated.h"

class AHGViewpointActor;

/**
 * GTA-style HUD drawn entirely in C++ (UMG UserWidget, custom NativePaint):
 * money in VND + 5 wanted stars (top right), GPS minimap of the pass road with
 * convoys / trucks / police / viewpoints (bottom left), speedometer (bottom
 * right), location card when reaching a viewpoint, flycam letterbox, prompts
 * and banners (Happy Water, BUSTED).
 */
UCLASS()
class HAGIANGGTA_API UHGGtaHudWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	struct FBlip
	{
		FVector2D World;
		FLinearColor Color;
		float Size = 6.f;
		bool bSquare = false;
		bool bClampToEdge = false;
	};

	void CacheRoad();

	/** Main route first, then the QL4C stubs - one polyline per road actor (cm, world XY). */
	TArray<TArray<FVector2D>> RoadLines;
	/** World distance (cm) from the player to the minimap edge: zooms out with speed. */
	float MapRange = 35000.f;
	TArray<FBlip> Blips;
	FVector2D PlayerXY = FVector2D::ZeroVector;
	float ViewYaw = 0.f;
	float SpeedKmh = 0.f;
	float AltitudeM = 0.f;
	FString ModeName;
	FString Prompt;
	FString ViewpointPrompt;

	int32 Stars = 0;
	bool bSpotted = false;
	int32 Money = 500000;
	float BustedTime = 0.f;
	FString CrimeReason;
	float CrimeReasonTime = 0.f;
	float HappyRemaining = 0.f;

	TWeakObjectPtr<AHGViewpointActor> LastViewpoint;
	FString CardName;
	int32 CardAltitude = 0;
	float CardTime = 0.f;

	bool bFlycam = false;
	FString FlyName;
	int32 FlyAltitude = 0;
	float FlyT = 0.f;
	float FlyDuration = 1.f;

	float Time = 0.f;
};
