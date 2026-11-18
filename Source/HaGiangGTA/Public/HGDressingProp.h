#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HGDressingProp.generated.h"

class UHierarchicalInstancedStaticMeshComponent;
class UStaticMeshComponent;
class UTextRenderComponent;

UENUM(BlueprintType)
enum class EHGDressingKind : uint8
{
	/** H'Mong house: stone or rammed-earth walls on a stone plinth, yin-yang clay-tile roof, wooden door, corn
	 *  drying under the eaves, dry-stone yard wall (variant picks walls / fence / corn). */
	HmongHouse,
	/** Tall flag pole with a big Vietnamese flag flapping in the wind (Ma Pi Leng). */
	FlagPole,
	/** Rough limestone block with a carved red name (SignText) - the check-in stone. */
	StoneSign,
	/** Wooden board on two posts (SignText). */
	WoodSign,
	/** Terraced corn plot: young corn on the contour terraces of the slope within Radius (late-March sowing). */
	CornField
};

/**
 * Ha Giang set dressing built at BeginPlay from the mesh kit (Scripts/hg_lib/dressing.py places them). Game-only
 * like the people and bikes; one or two draws each (the corn field is one instanced draw).
 */
UCLASS()
class HAGIANGGTA_API AHGDressingProp : public AActor
{
	GENERATED_BODY()

public:
	AHGDressingProp();
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dressing")
	EHGDressingKind Kind = EHGDressingKind::HmongHouse;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dressing")
	int32 Variant = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dressing")
	FText SignText;

	/** CornField: radius (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dressing")
	float Radius = 5000.f;

protected:
	virtual void BeginPlay() override;

private:
	void BuildCornField();

	UPROPERTY(VisibleAnywhere, Category = "Components") TObjectPtr<USceneComponent> Root;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Flag;
	UPROPERTY(Transient) TObjectPtr<UHierarchicalInstancedStaticMeshComponent> Corn;
	float Clock = 0.f;
};
