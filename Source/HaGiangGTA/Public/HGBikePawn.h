#pragma once

#include "CoreMinimal.h"
#include "HGGroundVehicle.h"
#include "HGBikeBuilder.h"
#include "HGPeopleBuilder.h"
#include "HGBikePawn.generated.h"

class UPointLightComponent;
class USoundBase;
class USpotLightComponent;
class UStaticMeshComponent;
class UTextRenderComponent;

/**
 * Honda Win 110 (greybox): arcade ground model + physical-ish lean
 * (atan(v * yawRate / g)), spinning wheels, head/tail lights, plate
 * "23 - HA GIANG", and a backpacker passenger on the rear seat whose body
 * sways with a spring-damper driven by the bike's accelerations.
 */
UCLASS()
class HAGIANGGTA_API AHGBikePawn : public AHGGroundVehicle
{
	GENERATED_BODY()

public:
	AHGBikePawn();

	/** Show / hide the passenger (convoy bikes may ride solo). */
	UFUNCTION(BlueprintCallable, Category = "HaGiang|Bike")
	void SetHasPassenger(bool bInHasPassenger);

	UFUNCTION(BlueprintPure, Category = "HaGiang|Bike")
	float GetLeanDeg() const { return LeanDeg; }

	UFUNCTION(BlueprintCallable, Category = "HaGiang|Bike")
	void SetBodyColor(const FLinearColor& Color);

	/** Show the NPC Easy Rider (reflective vest) on the seat - convoy / parked tour bikes. */
	UFUNCTION(BlueprintCallable, Category = "HaGiang|Bike")
	void SetNpcRiderVisible(bool bVisible);

	/** Backpacker screams (sound + arms up), e.g. near the cliff edge or when airborne. */
	UFUNCTION(BlueprintCallable, Category = "HaGiang|Bike")
	void Scream();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Passenger")
	TSoftObjectPtr<USoundBase> ScreamSound;

	/**
	 * Visual model (Win, Wave, Dream, Sirius, EasyRiderWin, LocalLoaded) and seed (paint, load, rider / passenger
	 * outfits, flag). Call right after spawning (before the first tick) or any time later - the visuals rebuild.
	 * Paint comes from the model's palette; SetBodyColor afterwards overrides it. Without a call the bike picks
	 * a model from its name (tour bikes with a passenger: Win / EasyRiderWin, the others: HGBikes::RandomModel).
	 */
	UFUNCTION(BlueprintCallable, Category = "HaGiang|Bike")
	void SetBikeModel(EHGBikeModel InModel, int32 InSeed);

	/** Easy Rider tour group 0..3: matching vests, scarves and helmets for every bike of one convoy. */
	UFUNCTION(BlueprintCallable, Category = "HaGiang|Bike")
	void SetTourGroup(int32 Group);

	/** Weighted random model for traffic (see HGBikes::RandomModel). */
	UFUNCTION(BlueprintPure, Category = "HaGiang|Bike")
	static EHGBikeModel RandomTrafficModel(int32 Seed) { return HGBikes::RandomModel(Seed); }

	UFUNCTION(BlueprintPure, Category = "HaGiang|Bike")
	EHGBikeModel GetBikeModel() const { return BikeModel; }

protected:
	virtual void BeginPlay() override;
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void UpdateVisuals(float DeltaSeconds, float Steer) override;
	virtual void OnAutoDriveChanged(bool bNowAutoDriving) override;
	virtual void PossessedBy(AController* NewController) override;
	void UpdateScreaming(float DeltaSeconds);

	UStaticMeshComponent* AddPart(FName Name, USceneComponent* Parent, UStaticMesh* Mesh,
		const FVector& Location, const FRotator& Rotation, const FVector& SizeCm);

	// ---- Components ----
	/** Everything that leans with the bike. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> LeanRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> FrontWheel;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> RearWheel;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USpotLightComponent> HeadLight;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UPointLightComponent> TailLight;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UTextRenderComponent> Plate;

	/** Pivot at the passenger's hips; rotated by the sway spring. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> PassengerRoot;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bike")
	bool bHasPassenger = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bike")
	FLinearColor BodyColor = FLinearColor(0.55f, 0.02f, 0.02f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bike")
	EHGBikeModel BikeModel = EHGBikeModel::Win;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bike")
	int32 BikeSeed = 0;

	/** -1: from the seed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bike")
	int32 TourGroup = -1;

	/** Model / paint explicitly chosen (SetBikeModel or set in the editor): skip the automatic pick. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bike")
	bool bModelChosen = false;

	/** SetBodyColor was called: keep that paint whatever model is picked. */
	bool bPaintChosen = false;

	/** Who rides an NPC bike of this class (police: CSGT). */
	EHGPersonLook NpcRiderLook = EHGPersonLook::EasyRider;

	/** Kit lamp parts (emissive, built with the visuals); null before the first tick. */
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> HeadLamp;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> TailLamp;
	bool bVisualsBuilt = false;

	UPROPERTY(EditAnywhere, Category = "Bike") float MaxLeanDeg = 42.f;
	UPROPERTY(EditAnywhere, Category = "Bike") float LeanInterpSpeed = 7.f;
	UPROPERTY(EditAnywhere, Category = "Bike") float WheelRadius = 30.f;
	UPROPERTY(EditAnywhere, Category = "Passenger") float SwayStiffness = 60.f;
	UPROPERTY(EditAnywhere, Category = "Passenger") float SwayDamping = 9.f;
	UPROPERTY(EditAnywhere, Category = "Passenger") float SwayDegPerG = 14.f;

private:
	void ApplyColors();
	/** Kit body, wheels, lamps, flag, NPC rider and passenger (HGBikeBuilder / HGPeopleBuilder). */
	void BuildVisuals();
	void ClearVisuals();
	void UpdatePassengerShown();
	bool bPassengerShown = true;

	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> BodyParts;
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> KitParts;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> KitBody;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> FlagCloth;
	UPROPERTY(Transient) TObjectPtr<USoundBase> LoadedScream;
	FHGPersonRig RiderRig;
	FHGPersonRig PassengerRig;
	bool bRiderVisible = false;
	float WaveTimer = 0.f;
	float WaveLeft = 0.f;
	float VisualClock = 0.f;
	float ScreamCooldown = 0.f;
	float ScreamTime = 0.f;       // > 0 while screaming (arms up)
	float CliffCheckTimer = 0.f;
	float AirTime = 0.f;

	float LeanDeg = 0.f;
	float WheelAngle = 0.f;
	float PrevSpeed = 0.f;
	FVector2D SwayAngle = FVector2D::ZeroVector;     // x = roll, y = pitch (deg)
	FVector2D SwayVelocity = FVector2D::ZeroVector;
};
