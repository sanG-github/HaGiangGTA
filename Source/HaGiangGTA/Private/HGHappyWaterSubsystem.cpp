#include "HGHappyWaterSubsystem.h"

#include "Camera/PlayerCameraManager.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HaGiangGTA.h"
#include "HGDrunkCameraModifier.h"

bool UHGHappyWaterSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UHGHappyWaterSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UHGHappyWaterSubsystem, STATGROUP_Tickables);
}

void UHGHappyWaterSubsystem::Drink(float DurationSeconds)
{
	Duration = FMath::Max(DurationSeconds, 1.f);
	Remaining = Duration;
	EnsureEffectActors();
	UE_LOG(LogHaGiang, Log, TEXT("Happy Water! %.0fs of nitro + wobble"), Duration);
}

void UHGHappyWaterSubsystem::EnsureEffectActors()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	if (!Volume)
	{
		FActorSpawnParameters Params;
		Params.ObjectFlags |= RF_Transient;
		Volume = World->SpawnActor<APostProcessVolume>(APostProcessVolume::StaticClass(), FTransform::Identity, Params);
		if (Volume)
		{
			Volume->bUnbound = true;
			Volume->Priority = 100.f;
			Volume->BlendWeight = 0.f;
		}
	}
	if (!Modifier)
	{
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			if (PC->PlayerCameraManager)
			{
				Modifier = PC->PlayerCameraManager->AddNewCameraModifier(UHGDrunkCameraModifier::StaticClass());
			}
		}
	}
}

void UHGHappyWaterSubsystem::Tick(float DeltaTime)
{
	Time += DeltaTime;
	if (Remaining > 0.f)
	{
		Remaining = FMath::Max(0.f, Remaining - DeltaTime);
	}
	const float Elapsed = Duration - Remaining;
	const float Target = Remaining > 0.f
		? FMath::Clamp(Elapsed / 1.5f, 0.f, 1.f) * FMath::Clamp(Remaining / 2.f, 0.f, 1.f)
		: 0.f;
	Intensity = FMath::FInterpTo(Intensity, Target, DeltaTime, 4.f);
	if (Volume)
	{
		ApplyPostProcess();
	}
}

void UHGHappyWaterSubsystem::ApplyPostProcess()
{
	FPostProcessSettings& S = Volume->Settings;
	const float I = Intensity;
	const float Wob = FMath::Sin(Time * 1.7f);
	const float Wob2 = FMath::Sin(Time * 0.9f + 1.3f);

	S.bOverride_SceneFringeIntensity = true;      // colour smear / chromatic aberration
	S.SceneFringeIntensity = 4.f * I * (0.7f + 0.3f * Wob);
	S.bOverride_MotionBlurAmount = true;
	S.MotionBlurAmount = 0.5f + 1.5f * I;
	S.bOverride_MotionBlurMax = true;
	S.MotionBlurMax = 5.f + 25.f * I;
	S.bOverride_BloomIntensity = true;
	S.BloomIntensity = 0.6f + 2.5f * I;
	S.bOverride_VignetteIntensity = true;
	S.VignetteIntensity = 0.3f + 0.7f * I;
	S.bOverride_FilmGrainIntensity = true;
	S.FilmGrainIntensity = 0.5f * I;
	S.bOverride_ColorSaturation = true;
	S.ColorSaturation = FVector4(1.f + 0.5f * I * Wob, 1.f + 0.4f * I, 1.f - 0.3f * I * Wob2, 1.f + 0.35f * I);
	S.bOverride_ColorGain = true;
	S.ColorGain = FVector4(1.f + 0.25f * I * Wob2, 1.f, 1.f + 0.25f * I * Wob, 1.f);
	Volume->BlendWeight = FMath::Clamp(I * 1.2f, 0.f, 1.f);
}
