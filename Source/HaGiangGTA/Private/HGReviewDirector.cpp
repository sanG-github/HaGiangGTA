#include "HGReviewDirector.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HGBikePawn.h"
#include "HGTouristNPC.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

AHGReviewDirector::AHGReviewDirector()
{
	PrimaryActorTick.bCanEverTick = true;
}

void AHGReviewDirector::EnsureFor(UWorld* World)
{
	if (!World || !World->IsGameWorld() || !FParse::Param(FCommandLine::Get(), TEXT("hgreview")))
	{
		return;
	}
	if (TActorIterator<AHGReviewDirector>(World))
	{
		return;
	}
	World->SpawnActor<AHGReviewDirector>();
}

void AHGReviewDirector::BuildShots()
{
	UWorld* World = GetWorld();
	// One parked bike per model: side and rear-quarter close-ups.
	TSet<uint8> Models;
	for (TActorIterator<AHGBikePawn> It(World); It; ++It)
	{
		AHGBikePawn* Bike = *It;
		if (Bike->IsAutoDriving() || Bike->IsPlayerControlled())
		{
			continue;
		}
		const uint8 M = uint8(Bike->GetBikeModel());
		if (Models.Contains(M))
		{
			continue;
		}
		Models.Add(M);
		const FString Name = UEnum::GetValueAsString(Bike->GetBikeModel()).RightChop(14);
		FShot Side;
		Side.Label = FString::Printf(TEXT("bike_%s_side"), *Name);
		Side.Follow = Bike;
		Side.LocalOffset = FVector(20.f, 330.f, 60.f);
		Side.LocalLook = FVector(0.f, 0.f, 10.f);
		Shots.Add(Side);
		FShot Rear = Side;
		Rear.Label = FString::Printf(TEXT("bike_%s_rear"), *Name);
		Rear.LocalOffset = FVector(-300.f, -170.f, 110.f);
		Shots.Add(Rear);
	}
	// Two moving NPC bikes, chased and from the front.
	int32 Moving = 0;
	for (TActorIterator<AHGBikePawn> It(World); It && Moving < 3; ++It)
	{
		if (!It->IsAutoDriving() || It->IsPlayerControlled())
		{
			continue;
		}
		FShot Chase;
		Chase.Label = FString::Printf(TEXT("riding_%d_chase"), Moving);
		Chase.Follow = *It;
		Chase.LocalOffset = FVector(-420.f, 90.f, 170.f);
		Chase.LocalLook = FVector(0.f, 0.f, 60.f);
		Shots.Add(Chase);
		FShot Front = Chase;
		Front.Label = FString::Printf(TEXT("riding_%d_front"), Moving);
		Front.LocalOffset = FVector(380.f, 120.f, 120.f);
		Shots.Add(Front);
		++Moving;
	}
	// Crowds: grouped by the point they look at; camera in front of them, looking back.
	TMap<FIntVector, TArray<AHGTouristNPC*>> Crowds;
	for (TActorIterator<AHGTouristNPC> It(World); It; ++It)
	{
		const FVector L = It->LookAtPoint.IsZero() ? It->GetActorLocation() : It->LookAtPoint;
		Crowds.FindOrAdd(FIntVector(FMath::RoundToInt(L.X / 5000.f), FMath::RoundToInt(L.Y / 5000.f), 0)).Add(*It);
	}
	int32 C = 0;
	for (const auto& Pair : Crowds)
	{
		FVector Centre = FVector::ZeroVector;
		for (AHGTouristNPC* T : Pair.Value)
		{
			Centre += T->GetActorLocation();
		}
		Centre /= float(Pair.Value.Num());
		const AHGTouristNPC* First = Pair.Value[0];
		const FVector Fwd = First->GetActorForwardVector().GetSafeNormal2D();
		for (int32 k = 0; k < 2; ++k)
		{
			FShot S;
			S.Label = FString::Printf(TEXT("crowd_%d_%c"), C, k ? 'b' : 'a');
			const FVector Dir = Fwd.RotateAngleAxis(k ? 35.f : -20.f, FVector::UpVector);
			S.Loc = Centre + Dir * (k ? 550.f : 900.f) + FVector(0.f, 0.f, k ? 60.f : 140.f);
			S.LookAt = Centre;
			Shots.Add(S);
		}
		++C;
	}
	UE_LOG(LogTemp, Display, TEXT("[HG_REVIEW] %d shots"), Shots.Num());
}

void AHGReviewDirector::PlaceCamera(const FShot& Shot)
{
	FVector Loc = Shot.Loc, Look = Shot.LookAt;
	if (const AActor* F = Shot.Follow.Get())
	{
		Loc = F->GetActorTransform().TransformPosition(Shot.LocalOffset);
		Look = F->GetActorTransform().TransformPosition(Shot.LocalLook);
	}
	Cam->SetActorLocationAndRotation(Loc, (Look - Loc).Rotation());
}

void AHGReviewDirector::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Time += DeltaSeconds;
	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	if (!PC)
	{
		return;
	}
	if (Index < 0)
	{
		if (Time < 4.f)
		{
			return;
		}
		BuildShots();
		Cam = GetWorld()->SpawnActor<ACameraActor>();
		Cam->GetCameraComponent()->SetFieldOfView(55.f);
		Cam->GetCameraComponent()->bConstrainAspectRatio = false;
		PC->SetViewTarget(Cam);
		Index = 0;
		Time = 0.f;
	}
	if (Index >= Shots.Num())
	{
		if (Time > 1.f)
		{
			UE_LOG(LogTemp, Display, TEXT("[HG_REVIEW] done"));
			UKismetSystemLibrary::QuitGame(this, PC, EQuitPreference::Quit, false);
			Index = -100;
		}
		return;
	}
	if (Index >= 0)
	{
		PlaceCamera(Shots[Index]);
		if (Time > 1.6f)
		{
			const FString File = FPaths::ProjectSavedDir() / TEXT("HG_CI/Review") / FString::Printf(TEXT("%02d_%s.png"), ShotNo++, *Shots[Index].Label);
			FScreenshotRequest::RequestScreenshot(File, false, false);
			++Index;
			Time = 0.f;
		}
	}
}
