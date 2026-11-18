#include "HGTrafficSpawner.h"

#include "Engine/World.h"
#include "HaGiangGTA.h"
#include "HGBikePawn.h"
#include "HGRoadSplineActor.h"
#include "HGTruckPawn.h"

AHGTrafficSpawner::AHGTrafficSpawner()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void AHGTrafficSpawner::BeginPlay()
{
	Super::BeginPlay();
	if (!Road)
	{
		UE_LOG(LogHaGiang, Warning, TEXT("%s: no Road set, no traffic."), *GetName());
		return;
	}
	const float Length = Road->GetRoadLength();
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	static const FLinearColor BikeColors[] = {
		FLinearColor(0.55f, 0.02f, 0.02f), FLinearColor(0.05f, 0.08f, 0.35f), FLinearColor(0.08f, 0.08f, 0.08f),
		FLinearColor(0.6f, 0.6f, 0.62f), FLinearColor(0.02f, 0.25f, 0.08f) };

	for (const FHGConvoySpec& Spec : Convoys)
	{
		AHGGroundVehicle* Leader = nullptr;
		const int32 Num = FMath::Clamp(Spec.NumBikes, 1, 16);
		for (int32 i = 0; i < Num; ++i)
		{
			const float D = Spec.StartMeters * 100.f - float(Spec.Direction >= 0 ? 1 : -1) * i * BikeSpacing;
			if (D < 0.f || D > Length)
			{
				continue;
			}
			AHGBikePawn* Bike = GetWorld()->SpawnActor<AHGBikePawn>(AHGBikePawn::StaticClass(), FTransform::Identity, Params);
			if (!Bike)
			{
				continue;
			}
			Bike->SetHasPassenger(FMath::FRand() < PassengerChance);
			Bike->SetBodyColor(BikeColors[FMath::RandRange(0, int32(UE_ARRAY_COUNT(BikeColors)) - 1)]);
			// Stagger the column a little inside the lane so it reads as a group of riders.
			const float Lane = LaneOffset + ((i % 2) ? 60.f : -40.f);
			Bike->StartAutoDrive(Road, D, Spec.Direction, Spec.CruiseKmh * FMath::FRandRange(0.95f, 1.05f), Lane);
			Bike->SetAutoDriveLeader(Leader);
			Leader = Bike;
			Spawned.Add(Bike);
		}
	}

	for (const FHGTruckSpec& Spec : Trucks)
	{
		AHGTruckPawn* Truck = GetWorld()->SpawnActor<AHGTruckPawn>(AHGTruckPawn::StaticClass(), FTransform::Identity, Params);
		if (!Truck)
		{
			continue;
		}
		Truck->StartAutoDrive(Road, FMath::Clamp(Spec.StartMeters * 100.f, 0.f, Length), Spec.Direction, Spec.CruiseKmh, LaneOffset + 40.f);
		Spawned.Add(Truck);
	}
	UE_LOG(LogHaGiang, Log, TEXT("Traffic: %d vehicles (%d convoys, %d trucks)"), Spawned.Num(), Convoys.Num(), Trucks.Num());
}
