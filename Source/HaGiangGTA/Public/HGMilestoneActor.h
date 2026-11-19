#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HGMilestoneActor.generated.h"

class UTextRenderComponent;

/**
 * Round 6B (agent A): Vietnamese national-road kilometre stone (cot cay so): white concrete with a red rounded top
 * carrying the road number, the next town and its distance on the face - the roadside cue every Vietnamese rider
 * knows from QL4C ("Dong Van 18 Km", "Meo Vac 22 Km").
 */
UCLASS()
class HAGIANGGTA_API AHGMilestoneActor : public AActor
{
	GENERATED_BODY()

public:
	AHGMilestoneActor();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Milestone") FString RoadName = TEXT("QL4C");
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Milestone") FString Town = TEXT("ĐỒNG VĂN");
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Milestone") int32 Km = 18;

protected:
	virtual void BeginPlay() override;
	virtual void OnConstruction(const FTransform& Transform) override;

private:
	void Build();
	UPROPERTY(VisibleAnywhere, Category = "Components") TObjectPtr<USceneComponent> Root;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextRenderComponent>> Texts;
	bool bBuilt = false;
};
