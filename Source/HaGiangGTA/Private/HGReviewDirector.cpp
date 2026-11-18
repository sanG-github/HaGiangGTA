#include "HGReviewDirector.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HGBikePawn.h"
#include "HGTouristNPC.h"
#include "HGDressingProp.h"
#include "HGViewpointActor.h"
#include "HGCowActor.h"
#include "HGTruckPawn.h"
#include "NhoQueBoatPawn.h"
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
	// Animals, a truck, a cruise boat.
	auto AddActorShot = [this](AActor* A, const TCHAR* Label, const FVector& Offset, const FVector& Look)
	{
		if (A)
		{
			FShot S;
			S.Label = Label;
			S.Follow = A;
			S.LocalOffset = Offset;
			S.LocalLook = Look;
			Shots.Add(S);
		}
	};
	{
		int32 Cows = 0;
		for (TActorIterator<AHGCowActor> It(World); It && Cows < 2; ++It, ++Cows)
		{
			AddActorShot(*It, Cows ? TEXT("animal_1") : TEXT("animal_0"), FVector(120.f, 420.f, 60.f), FVector(0.f, 0.f, 20.f));
		}
		TActorIterator<AHGTruckPawn> Truck(World);
		AddActorShot(Truck ? *Truck : nullptr, TEXT("truck_front"), FVector(900.f, 450.f, 120.f), FVector(0.f, 0.f, 0.f));
		AddActorShot(Truck ? *Truck : nullptr, TEXT("truck_rear"), FVector(-900.f, -400.f, 150.f), FVector(0.f, 0.f, 0.f));
		for (TActorIterator<ANhoQueBoatPawn> It(World); It; ++It)
		{
			if (!It->bAutoCruise && It->BoatType == EHGBoatType::Cano)
			{
				// The player's hire cano at the dock, from the boat camera's distance (~19 m back, 3.3 m up).
				AddActorShot(*It, TEXT("player_cano_chase"), FVector(-1900.f, 0.f, 330.f), FVector(0.f, 0.f, 80.f));
				AddActorShot(*It, TEXT("player_cano_side"), FVector(200.f, 1100.f, 250.f), FVector(0.f, 0.f, 80.f));
				break;
			}
		}
		for (TActorIterator<ANhoQueBoatPawn> It(World); It; ++It)
		{
			if (It->bAutoCruise)
			{
				AddActorShot(*It, TEXT("boat_side"), FVector(150.f, 1100.f, 160.f), FVector(0.f, 0.f, 60.f));
				AddActorShot(*It, TEXT("boat_quarter"), FVector(-700.f, 450.f, 260.f), FVector(-100.f, 0.f, 80.f));
				break;
			}
		}
	}
	// Vistas (Round 3B): from each viewpoint towards its subject and out to the horizon (distant ranges, haze).
	{
		int32 V = 0;
		for (TActorIterator<AHGViewpointActor> It(World); It; ++It, ++V)
		{
			const FVector Eye = It->GetActorLocation() + FVector(0.f, 0.f, 1500.f);   // above the bushes / props
			FVector ToSubject = (It->OrbitCenter - Eye).GetSafeNormal2D();
			const FVector Outward = It->GetActorLocation().GetSafeNormal2D();   // from the map centre: towards the ranges
			if (ToSubject.IsNearlyZero())
			{
				ToSubject = Outward;
			}
			FShot A;
			A.Label = FString::Printf(TEXT("vista_%d_subject"), V);
			A.Loc = Eye;
			A.LookAt = (It->OrbitCenter - Eye).Size2D() > 2000.f ? It->OrbitCenter : Eye + ToSubject * 10000.f + FVector(0.f, 0.f, -1500.f);
			Shots.Add(A);
			FShot B;
			B.Label = FString::Printf(TEXT("vista_%d_horizon"), V);
			B.Loc = Eye;
			B.LookAt = Eye + Outward.RotateAngleAxis(V * 40.f - 40.f, FVector::UpVector) * 100000.f + FVector(0.f, 0.f, -3000.f);
			Shots.Add(B);
		}
	}
	// Set dressing (Round 3B): two houses, every sign / flag, one corn terrace from above.
	{
		int32 HouseShots = 0;
		bool bCorn = false;
		for (TActorIterator<AHGDressingProp> It(World); It; ++It)
		{
			switch (It->Kind)
			{
			case EHGDressingKind::HmongHouse:
				if (HouseShots++ < 2)
				{
					AddActorShot(*It, HouseShots == 1 ? TEXT("house_front") : TEXT("house_quarter"),
						HouseShots == 1 ? FVector(300.f, 1900.f, 350.f) : FVector(-1500.f, 1300.f, 600.f), FVector(0.f, 0.f, 200.f));
				}
				break;
			case EHGDressingKind::CornField:
				if (!bCorn)
				{
					bCorn = true;
					AddActorShot(*It, TEXT("corn_terraces"), FVector(-3000.f, -3000.f, 2500.f), FVector(0.f, 0.f, 0.f));
				}
				break;
			case EHGDressingKind::FlagPole:
				AddActorShot(*It, TEXT("flag_pole"), FVector(-900.f, 1400.f, 400.f), FVector(0.f, 0.f, 700.f));
				break;
			default:
				AddActorShot(*It, *(TEXT("sign_") + It->GetName()), FVector(0.f, 650.f, 180.f), FVector(0.f, 0.f, 110.f));
				break;
			}
		}
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
