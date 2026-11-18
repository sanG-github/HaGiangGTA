#include "HGGreyboxHUD.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "HGGroundVehicle.h"

void AHGGreyboxHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas || !GEngine)
	{
		return;
	}

	UFont* LargeFont = GEngine->GetLargeFont();
	UFont* SmallFont = GEngine->GetSmallFont();
	const float W = Canvas->ClipX;
	const float H = Canvas->ClipY;
	const float UIScale = FMath::Max(H / 1080.f, 0.5f);

	DrawText(TEXT("HA GIANG GTA  |  CP01 GREYBOX"), FLinearColor(1.f, 0.85f, 0.2f), 30.f * UIScale, 24.f * UIScale, LargeFont, 1.2f * UIScale);

	if (const AHGGroundVehicle* Car = Cast<AHGGroundVehicle>(GetOwningPawn()))
	{
		const FString SpeedText = FString::Printf(TEXT("%3.0f km/h"), FMath::Abs(Car->GetSpeedKmh()));
		DrawText(SpeedText, FLinearColor::White, W - 260.f * UIScale, H - 110.f * UIScale, LargeFont, 2.2f * UIScale);
		DrawText(Car->IsGrounded() ? TEXT("GROUNDED") : TEXT("AIRBORNE"),
			Car->IsGrounded() ? FLinearColor(0.4f, 1.f, 0.4f) : FLinearColor(1.f, 0.4f, 0.3f),
			W - 260.f * UIScale, H - 50.f * UIScale, SmallFont, 1.4f * UIScale);
	}

	DrawText(TEXT("W/S: ga-phanh-lui   A/D: lai   SPACE: phanh tay   SHIFT: tang toc   R: dat lai"),
		FLinearColor(0.9f, 0.9f, 0.9f, 0.85f), 30.f * UIScale, H - 50.f * UIScale, SmallFont, 1.3f * UIScale);
}
