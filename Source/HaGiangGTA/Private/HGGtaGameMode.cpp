#include "HGGtaGameMode.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HaGiangGTA.h"
#include "HGGtaHUD.h"
#include "HGPlayerCharacter.h"
#include "HGViewpointActor.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/CommandLine.h"

AHGGtaGameMode::AHGGtaGameMode()
{
	DefaultPawnClass = AHGPlayerCharacter::StaticClass();
	HUDClass = AHGGtaHUD::StaticClass();
	PrimaryActorTick.bCanEverTick = true;
}

void AHGGtaGameMode::BeginPlay()
{
	Super::BeginPlay();
	bPerfRun = FParse::Param(FCommandLine::Get(), TEXT("hgperf"));
	if (!bPerfRun)
	{
		return;
	}
	// Spot 0: wherever the player spawned (bottom of Doc Tham Ma); then each viewpoint, looking at its subject.
	if (APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		PerfSpots.Add(Pawn->GetActorTransform());
		PerfNames.Add(TEXT("Spawn_ThamMa"));
	}
	for (TActorIterator<AHGViewpointActor> It(GetWorld()); It; ++It)
	{
		const FVector Loc = It->GetActorLocation() + FVector(0.f, 0.f, 150.f);
		const FRotator Look = (It->OrbitCenter - Loc).Rotation();
		PerfSpots.Add(FTransform(FRotator(0.f, Look.Yaw, 0.f), Loc));
		PerfNames.Add(It->GetName());
	}
	PerfStage = 0;
	PerfTime = -5.f; // warm-up (streaming, shader cache)
	UE_LOG(LogHaGiang, Display, TEXT("[HG_PERF] benchmark start, %d spots"), PerfSpots.Num());
}

void AHGGtaGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bPerfRun || PerfStage < 0 || PerfStage >= PerfSpots.Num())
	{
		return;
	}
	PerfTime += DeltaSeconds;
	if (PerfTime < 0.f)
	{
		// Warm-up at the new spot: move there first, measure once streaming has settled.
		if (!bPerfMoved)
		{
			bPerfMoved = true;
			if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
			{
				if (APawn* Pawn = PC->GetPawn())
				{
					Pawn->SetActorLocation(PerfSpots[PerfStage].GetLocation(), false, nullptr, ETeleportType::TeleportPhysics);
					PC->SetControlRotation(FRotator(-15.f, PerfSpots[PerfStage].Rotator().Yaw, 0.f));
				}
			}
		}
		return;
	}
	PerfAccum += DeltaSeconds;
	PerfWorst = FMath::Max(PerfWorst, (double)DeltaSeconds);
	++PerfFrames;
	if (PerfTime > 12.f)
	{
		const double AvgMs = 1000.0 * PerfAccum / FMath::Max(PerfFrames, 1);
		UE_LOG(LogHaGiang, Display, TEXT("[HG_PERF] %s: avg %.1f ms (%.0f FPS), worst %.1f ms, %d frames"),
			*PerfNames[PerfStage], AvgMs, 1000.0 / FMath::Max(AvgMs, 0.001), PerfWorst * 1000.0, PerfFrames);
		++PerfStage;
		bPerfMoved = false;
		PerfTime = -4.f;
		PerfAccum = 0.0;
		PerfWorst = 0.0;
		PerfFrames = 0;
		if (PerfStage >= PerfSpots.Num())
		{
			UKismetSystemLibrary::QuitGame(this, nullptr, EQuitPreference::Quit, false);
		}
	}
}
