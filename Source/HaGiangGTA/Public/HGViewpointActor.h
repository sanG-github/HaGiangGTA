#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HGViewpointActor.generated.h"

class APlayerController;
class UAudioComponent;
class UCineCameraComponent;
class USoundBase;
class USphereComponent;

/**
 * A scenic viewpoint (Doc Tham Ma, Ma Pi Leng / Mom Da Tu Than, Tu San).
 * Press [V] inside its radius: the view blends to a cine camera orbiting
 * the landmark, the HUD shows its name + altitude and a Hmong flute plays.
 * [V] again (or the orbit timer) returns to the player.
 */
UCLASS()
class HAGIANGGTA_API AHGViewpointActor : public AActor
{
	GENERATED_BODY()

public:
	AHGViewpointActor();

	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Toggle the flycam for the pawn's player: stop the active one, or start the nearest in range. */
	static bool ToggleFlycam(APawn* Pawn);

	/** Viewpoint whose activation radius contains Location (nearest first). */
	static AHGViewpointActor* FindNearby(const UWorld* World, const FVector& Location);

	/** The viewpoint currently showing its flycam, if any. */
	static AHGViewpointActor* GetActive(const UWorld* World);

	UFUNCTION(BlueprintCallable, Category = "HaGiang|Viewpoint")
	void StartFlycam(APlayerController* PC);

	UFUNCTION(BlueprintCallable, Category = "HaGiang|Viewpoint")
	void StopFlycam();

	UFUNCTION(BlueprintPure, Category = "HaGiang|Viewpoint")
	bool IsFlycamActive() const { return bActive; }

	/** 0..1 since activation, for HUD fades. */
	float GetActiveSeconds() const { return ActiveTime; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Viewpoint")
	FText ViewpointName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Viewpoint")
	int32 AltitudeM = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Viewpoint")
	float ActivationRadius = 4000.f;

	/** World-space point the camera orbits and looks at. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flycam")
	FVector OrbitCenter = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flycam")
	float OrbitRadius = 20000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flycam")
	float OrbitHeight = 8000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flycam")
	float OrbitDegreesPerSecond = 9.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flycam")
	float OrbitDuration = 28.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flycam")
	float FocalLength = 22.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flycam")
	TObjectPtr<USoundBase> FluteSound;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USphereComponent> Area;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UCineCameraComponent> FlyCam;

private:
	void UpdateCamera(float DeltaSeconds);

	UPROPERTY(Transient) TObjectPtr<APlayerController> ViewingPC;
	UPROPERTY(Transient) TObjectPtr<UAudioComponent> FluteAudio;

	bool bActive = false;
	float OrbitAngleDeg = 0.f;
	float ActiveTime = 0.f;
};
