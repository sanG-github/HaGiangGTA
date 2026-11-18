#include "HGPoliceHelicopter.h"

#include "Components/AudioComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "HGAudio.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

AHGPoliceHelicopter::AHGPoliceHelicopter()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	Body = CreateDefaultSubobject<USceneComponent>(TEXT("Body"));
	Body->SetupAttachment(RootComponent);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	const FLinearColor Navy(0.02f, 0.05f, 0.2f), White(0.85f, 0.85f, 0.85f), Dark(0.02f, 0.02f, 0.02f), Glass(0.05f, 0.08f, 0.1f);

	AddPart(TEXT("Cabin"), Body, SphMesh.Object, FVector(0.f, 0.f, 0.f), FRotator::ZeroRotator, FVector(420.f, 230.f, 220.f), Navy);
	AddPart(TEXT("Canopy"), Body, SphMesh.Object, FVector(120.f, 0.f, 20.f), FRotator::ZeroRotator, FVector(200.f, 190.f, 150.f), Glass);
	AddPart(TEXT("Stripe"), Body, CubeMesh.Object, FVector(0.f, 0.f, -20.f), FRotator::ZeroRotator, FVector(380.f, 235.f, 25.f), White);
	AddPart(TEXT("TailBoom"), Body, CylMesh.Object, FVector(-420.f, 0.f, 40.f), FRotator(90.f, 0.f, 0.f), FVector(50.f, 50.f, 520.f), Navy);
	AddPart(TEXT("Fin"), Body, CubeMesh.Object, FVector(-670.f, 0.f, 110.f), FRotator(-20.f, 0.f, 0.f), FVector(70.f, 10.f, 130.f), Navy);
	AddPart(TEXT("SkidL"), Body, CylMesh.Object, FVector(0.f, -110.f, -150.f), FRotator(90.f, 0.f, 0.f), FVector(12.f, 12.f, 380.f), Dark);
	AddPart(TEXT("SkidR"), Body, CylMesh.Object, FVector(0.f, 110.f, -150.f), FRotator(90.f, 0.f, 0.f), FVector(12.f, 12.f, 380.f), Dark);

	MainRotor = CreateDefaultSubobject<USceneComponent>(TEXT("MainRotor"));
	MainRotor->SetupAttachment(Body);
	MainRotor->SetRelativeLocation(FVector(0.f, 0.f, 140.f));
	AddPart(TEXT("BladeA"), MainRotor, CubeMesh.Object, FVector::ZeroVector, FRotator::ZeroRotator, FVector(1100.f, 30.f, 4.f), Dark);
	AddPart(TEXT("BladeB"), MainRotor, CubeMesh.Object, FVector::ZeroVector, FRotator(0.f, 90.f, 0.f), FVector(1100.f, 30.f, 4.f), Dark);

	TailRotor = CreateDefaultSubobject<USceneComponent>(TEXT("TailRotor"));
	TailRotor->SetupAttachment(Body);
	TailRotor->SetRelativeLocation(FVector(-680.f, 30.f, 110.f));
	AddPart(TEXT("TailBlade"), TailRotor, CubeMesh.Object, FVector::ZeroVector, FRotator::ZeroRotator, FVector(160.f, 4.f, 16.f), Dark);

	Searchlight = CreateDefaultSubobject<USpotLightComponent>(TEXT("Searchlight"));
	Searchlight->SetupAttachment(Body);
	Searchlight->SetRelativeLocation(FVector(180.f, 0.f, -120.f));
	Searchlight->SetUsingAbsoluteRotation(true);
	Searchlight->SetIntensityUnits(ELightUnits::Candelas);
	Searchlight->SetIntensity(300000.f);
	Searchlight->SetAttenuationRadius(40000.f);
	Searchlight->SetInnerConeAngle(3.f);
	Searchlight->SetOuterConeAngle(7.f);
	Searchlight->SetLightColor(FLinearColor(0.85f, 0.92f, 1.f));
	Searchlight->SetVolumetricScatteringIntensity(12.f);   // visible beam in the canyon fog
	Searchlight->SetCastShadows(true);

	RotorSound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/HaGiang/Generated/Audio/SW_HG_Heli_Loop.SW_HG_Heli_Loop")));
}

UStaticMeshComponent* AHGPoliceHelicopter::AddPart(FName Name, USceneComponent* Parent, UStaticMesh* Mesh, const FVector& Loc, const FRotator& Rot, const FVector& SizeCm, const FLinearColor& Color)
{
	UStaticMeshComponent* C = CreateDefaultSubobject<UStaticMeshComponent>(Name);
	C->SetupAttachment(Parent);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetRelativeLocation(Loc);
	C->SetRelativeRotation(Rot);
	C->SetRelativeScale3D(SizeCm / 100.f);
	if (Mesh)
	{
		C->SetStaticMesh(Mesh);
	}
	Parts.Add(C);
	PartColors.Add(Color);
	return C;
}

void AHGPoliceHelicopter::BeginPlay()
{
	Super::BeginPlay();
	for (int32 i = 0; i < Parts.Num() && i < PartColors.Num(); ++i)
	{
		if (UMaterialInstanceDynamic* MID = Parts[i]->CreateAndSetMaterialInstanceDynamic(0))
		{
			MID->SetVectorParameterValue(TEXT("Color"), PartColors[i]);
		}
	}
	if (USoundBase* Rotor = RotorSound.LoadSynchronous())
	{
		RotorAudio = HGAudio::CreateLoop(RootComponent, Rotor, 40000.f);
		if (RotorAudio)
		{
			RotorAudio->Play();
		}
	}
}

void AHGPoliceHelicopter::FlyAway()
{
	bLeaving = true;
	LeaveTime = 0.f;
	Searchlight->SetIntensity(0.f);
}

void AHGPoliceHelicopter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Time += DeltaSeconds;
	const FVector Loc = GetActorLocation();
	const APawn* Target = ChaseTarget.Get();

	FVector Desired = Loc + FVector(0.f, 0.f, 3000.f);
	if (bLeaving)
	{
		LeaveTime += DeltaSeconds;
		Desired = Loc + GetActorForwardVector() * 8000.f + FVector(0.f, 0.f, 6000.f);
		if (LeaveTime > 20.f)
		{
			Destroy();
			return;
		}
	}
	else if (Target)
	{
		// Hang behind and above the player, offset to the side so the beam rakes across the road.
		const FVector Back = -Target->GetVelocity().GetSafeNormal2D();
		const FVector Side = FVector::CrossProduct(FVector::UpVector, Back).GetSafeNormal();
		const FVector Offset = (Back.IsNearlyZero() ? -Target->GetActorForwardVector() : Back) * FollowDistance + Side * 2500.f * FMath::Sin(Time * 0.15f);
		Desired = Target->GetActorLocation() + Offset + FVector(0.f, 0.f, FollowHeight);
	}

	// Terrain clearance at the desired point and along the next few seconds of flight: with ~1,200 m of
	// relief a ridge between us and the player matters as much as the ground under the target point.
	// Traces start 2 km above the probe so they never begin inside a mountain (a trace that starts under
	// the landscape surface hits nothing and the clearance was silently skipped).
	FCollisionQueryParams Params(SCENE_QUERY_STAT(HGHeliGround), false, this);
	if (const APawn* Chased = ChaseTarget.Get())
	{
		Params.AddIgnoredActor(Chased);
	}
	auto GroundZ = [&](const FVector& At, float& OutZ) -> bool
	{
		FHitResult Ground;
		if (GetWorld()->LineTraceSingleByChannel(Ground, FVector(At.X, At.Y, FMath::Max(At.Z, Loc.Z) + 200000.f),
			FVector(At.X, At.Y, FMath::Min(At.Z, Loc.Z) - 200000.f), ECC_Visibility, Params))
		{
			OutZ = Ground.ImpactPoint.Z;
			return true;
		}
		return false;
	};
	float Highest = -BIG_NUMBER;
	const FVector Ahead = Velocity.GetSafeNormal2D() * FMath::Min(Velocity.Size2D() * 3.f, 15000.f);
	const FVector Probes[] = { Desired, Loc + Ahead * 0.33f, Loc + Ahead * 0.66f, Loc + Ahead, (Loc + Desired) * 0.5f };
	for (const FVector& Probe : Probes)
	{
		float Z;
		if (GroundZ(Probe, Z))
		{
			Highest = FMath::Max(Highest, Z);
		}
	}
	if (Highest > -BIG_NUMBER)
	{
		Desired.Z = FMath::Max(Desired.Z, Highest + MinTerrainClearance);
	}

	// Critically-damped steering toward the desired point.
	const FVector Accel = (Desired - Loc) * 0.9f - Velocity * 1.6f;
	Velocity += Accel * DeltaSeconds;
	Velocity = Velocity.GetClampedToMaxSize(MaxSpeed);
	SetActorLocation(Loc + Velocity * DeltaSeconds);

	// Face the player (or the flight direction), bank into the turn, nose down with speed.
	const FVector LookDir = Target && !bLeaving ? (Target->GetActorLocation() - Loc) : Velocity;
	const float Yaw = LookDir.Rotation().Yaw;
	const FVector LocalVel = GetActorRotation().UnrotateVector(Velocity);
	const FRotator Attitude(FMath::Clamp(-LocalVel.X / MaxSpeed * 18.f, -18.f, 18.f), 0.f,
		FMath::Clamp(LocalVel.Y / MaxSpeed * 22.f, -22.f, 22.f));
	SetActorRotation(FMath::RInterpTo(GetActorRotation(), FRotator(0.f, Yaw, 0.f), DeltaSeconds, 1.5f));
	Body->SetRelativeRotation(FMath::RInterpTo(Body->GetRelativeRotation(), Attitude, DeltaSeconds, 2.f));

	RotorAngle = FMath::Fmod(RotorAngle + DeltaSeconds * 1700.f, 360.f);
	MainRotor->SetRelativeRotation(FRotator(0.f, RotorAngle, 0.f));
	TailRotor->SetRelativeRotation(FRotator(RotorAngle * 2.f, 0.f, 0.f));

	if (Target && !bLeaving)
	{
		// Searchlight with a hand-held sway.
		const FVector Aim = Target->GetActorLocation() + FVector(250.f * FMath::Sin(Time * 1.3f), 250.f * FMath::Cos(Time * 0.9f), 0.f);
		Searchlight->SetWorldRotation((Aim - Searchlight->GetComponentLocation()).Rotation());
	}
}
