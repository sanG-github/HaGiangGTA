#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HGPeopleBuilder.h"
#include "HGTouristNPC.generated.h"

class UCapsuleComponent;
class UStaticMeshComponent;

UENUM(BlueprintType)
enum class EHGTouristStyle : uint8
{
	/** Western backpacker (co do sao vang T-shirt, yellow duck-bill helmet, backpack). */
	Backpacker,
	/** Easy Rider tour guide in a tour-colour vest. */
	EasyRider,
	/** Local: H'Mong woman (pleated skirt, basket) or man in a dark jacket, picked by name. */
	Local
};

/**
 * Pedestrian for the check-in crowds (low-poly animated person, HGPeopleBuilder): idles with weight shifts and
 * head turns, takes phone photos, waves; locals and some backpackers stroll a few metres back and forth.
 */
UCLASS()
class HAGIANGGTA_API AHGTouristNPC : public AActor
{
	GENERATED_BODY()

public:
	AHGTouristNPC();

	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tourist")
	EHGTouristStyle Style = EHGTouristStyle::Backpacker;

	/** Direction the crowd looks at when taking photos (world). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tourist")
	FVector LookAtPoint = FVector::ZeroVector;

	/** Allow strolling (locals and every fourth backpacker); flower sellers never wander. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tourist")
	bool bWander = true;

	/** Round 6A: a local walking along the road shoulder between hamlets (follows the road spline at its
	 *  current offset, ShoulderWalkM metres each way). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tourist")
	bool bShoulderWalker = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tourist")
	float ShoulderWalkM = 60.f;

	/** Round 6A: a child (smaller) who waves at every rider passing within 25 m. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tourist")
	bool bKid = false;

	/** Session count of kids waving at riders (playtest log). */
	static int32 KidWaves;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, Category = "Components") TObjectPtr<UCapsuleComponent> Capsule;
	UPROPERTY(VisibleAnywhere, Category = "Components") TObjectPtr<USceneComponent> BodyRoot;

private:
	void StepWalk(float DeltaSeconds);

	FHGPersonRig Rig;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Phone;

	float PhotoTimer = 0.f;
	float PhotoLeft = 0.f;
	float WaveTimer = 0.f;
	float WaveLeft = 0.f;
	float BaseYaw = 0.f;
	bool bWalker = false;
	FVector Home = FVector::ZeroVector;
	FVector WalkAxis = FVector::ForwardVector;
	float WalkPos = 0.f;      // cm along WalkAxis from Home
	float WalkDir = 1.f;
	float WalkPause = 0.f;
	float GroundTimer = 0.f;
	float GroundZ = 0.f;
	TWeakObjectPtr<class AHGRoadSplineActor> ShoulderRoad;
	float ShoulderHome = 0.f;
	float ShoulderLat = 0.f;
	float KidCheck = 0.f;
};
