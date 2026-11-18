#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HGTrafficSpawner.generated.h"

class AHGBikePawn;
class AHGGroundVehicle;
class AHGRoadSplineActor;
class AHGTruckPawn;

USTRUCT(BlueprintType)
struct FHGConvoySpec
{
	GENERATED_BODY()

	/** Where the convoy leader starts, metres along the road. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Convoy") float StartMeters = 300.f;
	/** +1 up the pass, -1 down. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Convoy") int32 Direction = 1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Convoy") int32 NumBikes = 10;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Convoy") float CruiseKmh = 42.f;
};

USTRUCT(BlueprintType)
struct FHGTruckSpec
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Truck") float StartMeters = 6000.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Truck") int32 Direction = -1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Truck") float CruiseKmh = 38.f;
};

/**
 * Spawns the Ha Giang Loop traffic at BeginPlay: Easy Rider motorbike convoys
 * (8-12 bikes, guide in a reflective vest + backpacker in a red star T-shirt,
 * nylon-wrapped backpacks) nose-to-tail along the road spline, and dump trucks
 * coming the other way. Every vehicle can be hijacked with [F].
 */
UCLASS()
class HAGIANGGTA_API AHGTrafficSpawner : public AActor
{
	GENERATED_BODY()

public:
	AHGTrafficSpawner();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic")
	TObjectPtr<AHGRoadSplineActor> Road;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic")
	TArray<FHGConvoySpec> Convoys;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic")
	TArray<FHGTruckSpec> Trucks;

	/** Distance between bikes in a convoy (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic")
	float BikeSpacing = 1100.f;

	/** Lateral offset from the road centre (cm, right of travel - Vietnam drives on the right). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic")
	float LaneOffset = 170.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic")
	float PassengerChance = 0.8f;

	const TArray<TObjectPtr<AHGGroundVehicle>>& GetSpawnedVehicles() const { return Spawned; }

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(Transient) TArray<TObjectPtr<AHGGroundVehicle>> Spawned;
};
