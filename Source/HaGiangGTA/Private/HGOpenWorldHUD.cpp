#include "HGOpenWorldHUD.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "GameFramework/PlayerController.h"
#include "HGPlayerCharacter.h"
#include "HGVehicleBase.h"

void AHGOpenWorldHUD::DrawHUD()
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
	const float S = FMath::Max(H / 1080.f, 0.5f);

	DrawText(TEXT("HA GIANG GTA  |  CP02"), FLinearColor(1.f, 0.85f, 0.2f), 30.f * S, 24.f * S, LargeFont, 1.2f * S);

	APawn* Pawn = GetOwningPawn();
	if (!Pawn)
	{
		return;
	}

	FString Mode;
	float Kmh = 0.f;
	FString Hint;
	if (const AHGVehicleBase* Vehicle = Cast<AHGVehicleBase>(Pawn))
	{
		Mode = Vehicle->GetVehicleDisplayName().ToString();
		Kmh = FMath::Abs(Vehicle->GetSpeedKmh());
		Hint = TEXT("W/S ga-phanh  A/D lai  SHIFT tang toc  SPACE phanh tay  R dat lai  F xuong xe");
	}
	else if (const AHGPlayerCharacter* Player = Cast<AHGPlayerCharacter>(Pawn))
	{
		Mode = Player->GetOnFootState() == EHGOnFootState::Swimming ? TEXT("DANG BOI") : TEXT("DI BO");
		Kmh = Player->GetSpeedKmh();
		Hint = TEXT("WASD di chuyen  CHUOT nhin  SHIFT chay  SPACE nhay");
		if (const AHGVehicleBase* Focus = Player->GetFocusedVehicle())
		{
			const FString Prompt = FString::Printf(TEXT("[F]  %s"), *Focus->GetVehicleDisplayName().ToString());
			float TW = 0.f, TH = 0.f;
			GetTextSize(Prompt, TW, TH, LargeFont, 1.4f * S);
			DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.55f), (W - TW) * 0.5f - 16.f * S, H * 0.72f - 8.f * S, TW + 32.f * S, TH + 16.f * S);
			DrawText(Prompt, FLinearColor::White, (W - TW) * 0.5f, H * 0.72f, LargeFont, 1.4f * S);
		}
	}

	// Altitude: world Z (cm) == metres above sea level (see gen_heightmap.py).
	const float AltitudeM = Pawn->GetActorLocation().Z / 100.f;

	DrawText(Mode, FLinearColor(0.9f, 0.95f, 1.f), W - 380.f * S, H - 170.f * S, SmallFont, 1.6f * S);
	DrawText(FString::Printf(TEXT("%3.0f km/h"), Kmh), FLinearColor::White, W - 380.f * S, H - 130.f * S, LargeFont, 2.2f * S);
	DrawText(FString::Printf(TEXT("%4.0f m"), AltitudeM), FLinearColor(0.7f, 1.f, 0.7f), W - 380.f * S, H - 60.f * S, SmallFont, 1.5f * S);
	DrawText(Hint, FLinearColor(0.9f, 0.9f, 0.9f, 0.85f), 30.f * S, H - 50.f * S, SmallFont, 1.3f * S);
}
