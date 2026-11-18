#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HGTouristNPC.generated.h"

class UCapsuleComponent;
class UStaticMeshComponent;

UENUM(BlueprintType)
enum class EHGTouristStyle : uint8
{
	/** Western backpacker in the red "co do sao vang" T-shirt. */
	Backpacker,
	/** Easy Rider tour guide in a reflective vest. */
	EasyRider,
	/** Local Hmong in indigo clothes. */
	Local
};

/** Greybox pedestrian for the check-in crowds: idles, looks around, takes phone photos. */
UCLASS()
class HAGIANGGTA_API AHGTouristNPC : public AActor
{
	GENERATED_BODY()

public:
	AHGTouristNPC();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tourist")
	EHGTouristStyle Style = EHGTouristStyle::Backpacker;

	/** Direction the crowd looks at when taking photos (world). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tourist")
	FVector LookAtPoint = FVector::ZeroVector;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, Category = "Components") TObjectPtr<UCapsuleComponent> Capsule;
	UPROPERTY(VisibleAnywhere, Category = "Components") TObjectPtr<USceneComponent> BodyRoot;
	UPROPERTY(VisibleAnywhere, Category = "Components") TObjectPtr<UStaticMeshComponent> Torso;
	UPROPERTY(VisibleAnywhere, Category = "Components") TObjectPtr<UStaticMeshComponent> Legs;
	UPROPERTY(VisibleAnywhere, Category = "Components") TObjectPtr<UStaticMeshComponent> Head;
	UPROPERTY(VisibleAnywhere, Category = "Components") TObjectPtr<UStaticMeshComponent> Hat;
	UPROPERTY(VisibleAnywhere, Category = "Components") TObjectPtr<UStaticMeshComponent> Backpack;
	UPROPERTY(VisibleAnywhere, Category = "Components") TObjectPtr<UStaticMeshComponent> Star;
	UPROPERTY(VisibleAnywhere, Category = "Components") TObjectPtr<UStaticMeshComponent> PhoneArm;
	UPROPERTY(VisibleAnywhere, Category = "Components") TObjectPtr<UStaticMeshComponent> Phone;

private:
	void ApplyStyle();
	static void Paint(UStaticMeshComponent* C, const FLinearColor& Color);

	float Phase = 0.f;
	float PhotoTimer = 0.f;
	float PhotoLeft = 0.f;
	float BaseYaw = 0.f;
};
