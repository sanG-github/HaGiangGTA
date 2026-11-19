#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HGReviewDirector.generated.h"

class ACameraActor;

/**
 * Visual self-review "photo booth" (-game -hgreview): frames every parked bike model, the riders of moving
 * bikes and each check-in crowd, writes Saved/HG_CI/Review/NN_label.png, then quits. Spawned by the first
 * AHGTouristNPC when the switch is on; does nothing otherwise.
 */
UCLASS()
class HAGIANGGTA_API AHGReviewDirector : public AActor
{
	GENERATED_BODY()

public:
	AHGReviewDirector();
	virtual void Tick(float DeltaSeconds) override;

	/** Spawn one director if -hgreview is on the command line. */
	static void EnsureFor(UWorld* World);

private:
	struct FShot
	{
		FString Label;
		TWeakObjectPtr<AActor> Follow;   // camera follows this actor (offset in its frame)
		FVector Loc = FVector::ZeroVector;
		FVector LookAt = FVector::ZeroVector;
		FVector LocalOffset = FVector::ZeroVector;
		FVector LocalLook = FVector::ZeroVector;
	};
	void BuildShots();
	void PlaceCamera(const FShot& Shot);

	TArray<FShot> Shots;
	UPROPERTY(Transient) TObjectPtr<ACameraActor> Cam;
	int32 Index = -1;
	float Time = 0.f;
	int32 ShotNo = 0;
};
