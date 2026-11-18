#include "HGViewpointActor.h"

#include "CineCameraComponent.h"
#include "Components/AudioComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HaGiangGTA.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

AHGViewpointActor::AHGViewpointActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	Area = CreateDefaultSubobject<USphereComponent>(TEXT("Area"));
	Area->InitSphereRadius(ActivationRadius);
	Area->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Area->SetHiddenInGame(true);
	RootComponent = Area;

	FlyCam = CreateDefaultSubobject<UCineCameraComponent>(TEXT("FlyCam"));
	FlyCam->SetupAttachment(Area);
	FlyCam->SetUsingAbsoluteLocation(true);
	FlyCam->SetUsingAbsoluteRotation(true);
	FlyCam->bConstrainAspectRatio = false;
}

void AHGViewpointActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (FluteAudio)
	{
		FluteAudio->Stop();
	}
	Super::EndPlay(EndPlayReason);
}

AHGViewpointActor* AHGViewpointActor::FindNearby(const UWorld* World, const FVector& Location)
{
	AHGViewpointActor* Best = nullptr;
	float BestDistSq = TNumericLimits<float>::Max();
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<AHGViewpointActor> It(World); It; ++It)
	{
		const float DistSq = FVector::DistSquared(It->GetActorLocation(), Location);
		if (DistSq <= FMath::Square(It->ActivationRadius) && DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			Best = *It;
		}
	}
	return Best;
}

AHGViewpointActor* AHGViewpointActor::GetActive(const UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<AHGViewpointActor> It(World); It; ++It)
	{
		if (It->bActive)
		{
			return *It;
		}
	}
	return nullptr;
}

bool AHGViewpointActor::ToggleFlycam(APawn* Pawn)
{
	if (!Pawn)
	{
		return false;
	}
	APlayerController* PC = Cast<APlayerController>(Pawn->GetController());
	if (!PC)
	{
		return false;
	}
	if (AHGViewpointActor* Active = GetActive(Pawn->GetWorld()))
	{
		Active->StopFlycam();
		return true;
	}
	if (AHGViewpointActor* Near = FindNearby(Pawn->GetWorld(), Pawn->GetActorLocation()))
	{
		Near->StartFlycam(PC);
		return true;
	}
	return false;
}

void AHGViewpointActor::StartFlycam(APlayerController* PC)
{
	if (!PC || bActive)
	{
		return;
	}
	ViewingPC = PC;
	bActive = true;
	ActiveTime = 0.f;

	// Start the orbit on the player's side of the landmark so the move reads clearly.
	const FVector From = PC->GetPawn() ? PC->GetPawn()->GetActorLocation() : GetActorLocation();
	const FVector Dir = (From - OrbitCenter).GetSafeNormal2D();
	OrbitAngleDeg = Dir.IsNearlyZero() ? 0.f : FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X));

	FlyCam->SetCurrentFocalLength(FocalLength);
	FlyCam->FocusSettings.FocusMethod = ECameraFocusMethod::Manual;
	UpdateCamera(0.f);

	PC->SetViewTargetWithBlend(this, 1.6f, EViewTargetBlendFunction::VTBlend_Cubic);
	PC->SetIgnoreMoveInput(true);
	if (FluteSound)
	{
		FluteAudio = UGameplayStatics::SpawnSound2D(this, FluteSound, 0.9f);
	}
	SetActorTickEnabled(true);
	UE_LOG(LogHaGiang, Log, TEXT("Flycam: %s"), *ViewpointName.ToString());
}

void AHGViewpointActor::StopFlycam()
{
	if (!bActive)
	{
		return;
	}
	bActive = false;
	SetActorTickEnabled(false);
	if (FluteAudio)
	{
		FluteAudio->FadeOut(1.5f, 0.f);
		FluteAudio = nullptr;
	}
	if (ViewingPC)
	{
		ViewingPC->SetIgnoreMoveInput(false);
		if (APawn* Pawn = ViewingPC->GetPawn())
		{
			ViewingPC->SetViewTargetWithBlend(Pawn, 1.2f, EViewTargetBlendFunction::VTBlend_Cubic);
		}
	}
	ViewingPC = nullptr;
}

void AHGViewpointActor::UpdateCamera(float DeltaSeconds)
{
	OrbitAngleDeg += OrbitDegreesPerSecond * DeltaSeconds;
	const float A = FMath::DegreesToRadians(OrbitAngleDeg);
	// Slow crane: drift a little lower and closer during the orbit.
	const float T = FMath::Clamp(ActiveTime / FMath::Max(OrbitDuration, 1.f), 0.f, 1.f);
	const float Radius = OrbitRadius * FMath::Lerp(1.08f, 0.92f, T);
	const float Height = OrbitHeight * FMath::Lerp(1.1f, 0.85f, T) + FMath::Sin(ActiveTime * 0.6f) * OrbitHeight * 0.03f;

	const FVector CamPos = OrbitCenter + FVector(FMath::Cos(A) * Radius, FMath::Sin(A) * Radius, Height);
	const FRotator LookAt = (OrbitCenter - CamPos).Rotation();
	FlyCam->SetWorldLocationAndRotation(CamPos, LookAt);
	FlyCam->FocusSettings.ManualFocusDistance = FVector::Dist(CamPos, OrbitCenter);
}

void AHGViewpointActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bActive)
	{
		return;
	}
	ActiveTime += DeltaSeconds;
	UpdateCamera(DeltaSeconds);
	if (ActiveTime >= OrbitDuration)
	{
		StopFlycam();
	}
}
