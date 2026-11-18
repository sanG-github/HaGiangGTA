#include "HGTouristNPC.h"
#include "HGMeshKit.h"
#include "HGReviewDirector.h"

#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"

namespace
{
	constexpr float HalfHeight = 90.f;
	constexpr float WalkRange = 260.f;   // cm each side of home
}

AHGTouristNPC::AHGTouristNPC()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.02f;

	Capsule = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Capsule"));
	Capsule->InitCapsuleSize(35.f, HalfHeight);
	Capsule->SetCollisionProfileName(TEXT("Pawn"));
	RootComponent = Capsule;

	BodyRoot = CreateDefaultSubobject<USceneComponent>(TEXT("BodyRoot"));
	BodyRoot->SetupAttachment(Capsule);
	BodyRoot->SetRelativeLocation(FVector(0.f, 0.f, -HalfHeight));
}

void AHGTouristNPC::BeginPlay()
{
	Super::BeginPlay();
	AHGReviewDirector::EnsureFor(GetWorld());
	const FString Name = GetName();
	const int32 Hash = int32(GetTypeHash(Name) & 0x7fffffff);
	const bool bFlowerSeller = Name.Contains(TEXT("Flower"));
	EHGPersonLook Look = EHGPersonLook::Backpacker;
	switch (Style)
	{
	case EHGTouristStyle::EasyRider: Look = EHGPersonLook::EasyRider; break;
	case EHGTouristStyle::Local: Look = (bFlowerSeller || (Hash & 1)) ? EHGPersonLook::HmongWoman : EHGPersonLook::LocalMan; break;
	default: break;
	}
	const int32 Variant = Hash % HGPeople::NumVariants;
	Rig = HGPeople::Spawn(this, BodyRoot, Look, EHGPersonPose::Standing, Variant, TEXT("Person"), FVector::ZeroVector,
		Look == EHGPersonLook::Backpacker && (Hash % 3) == 0);
	if (Rig.Arm[1] && Look != EHGPersonLook::HmongWoman)
	{
		UStaticMesh* PhoneMesh = FHGMeshKit::Cached(TEXT("SM_HGPhone"), [](FHGMeshKit& K)
		{
			K.Paint(FLinearColor(0.01f, 0.01f, 0.012f), HGSurf::Gloss);
			K.Box(FVector::ZeroVector, FVector(1.f, 7.5f, 15.f));
		});
		Phone = FHGMeshKit::AddComponent(this, Rig.Arm[1], PhoneMesh, TEXT("Phone"), FVector(6.f, 0.f, -56.f));
		if (Phone)
		{
			Phone->SetCullDistance(6000.f);
			Phone->SetVisibility(false);
		}
	}

	// Stand on the ground.
	FHitResult Hit;
	const FVector Loc = GetActorLocation();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(HGTouristGround), false, this);
	if (GetWorld()->LineTraceSingleByChannel(Hit, Loc + FVector(0, 0, 300.f), Loc - FVector(0, 0, 2000.f), ECC_Visibility, Params))
	{
		SetActorLocation(Hit.ImpactPoint + FVector(0.f, 0.f, HalfHeight));
	}
	if (!LookAtPoint.IsZero())
	{
		SetActorRotation(FRotator(0.f, (LookAtPoint - GetActorLocation()).Rotation().Yaw, 0.f));
	}
	BaseYaw = GetActorRotation().Yaw;
	Home = GetActorLocation();
	GroundZ = Home.Z;
	// Stroll sideways to the view (along the crowd line), never towards the drop.
	WalkAxis = FRotator(0.f, BaseYaw + 90.f, 0.f).Vector();
	bWalker = bWander && !bFlowerSeller && (Style == EHGTouristStyle::Local || (Hash % 4) == 1);
	WalkDir = (Hash & 2) ? 1.f : -1.f;
	WalkPause = float(Hash % 5);
	PhotoTimer = 1.f + float(Hash % 70) * 0.1f;
	WaveTimer = 5.f + float(Hash % 13);
}

void AHGTouristNPC::StepWalk(float DeltaSeconds)
{
	if (WalkPause > 0.f)
	{
		WalkPause -= DeltaSeconds;
		Rig.WalkSpeed = 0.f;
		const float Face = BaseYaw + (WalkPause < 1.2f ? (WalkDir > 0.f ? 90.f : -90.f) : 0.f);
		SetActorRotation(FMath::RInterpTo(GetActorRotation(), FRotator(0.f, Face, 0.f), DeltaSeconds, 3.f));
		return;
	}
	const float Speed = 85.f;
	WalkPos += WalkDir * Speed * DeltaSeconds;
	if (FMath::Abs(WalkPos) > WalkRange)
	{
		WalkPos = FMath::Clamp(WalkPos, -WalkRange, WalkRange);
		WalkDir = -WalkDir;
		WalkPause = FMath::FRandRange(2.f, 6.f);   // stop, look at the view, then turn back
	}
	Rig.WalkSpeed = Speed;
	FVector Target = Home + WalkAxis * WalkPos;
	GroundTimer -= DeltaSeconds;
	if (GroundTimer <= 0.f)
	{
		GroundTimer = 0.2f;
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(HGTouristGround), false, this);
		if (GetWorld()->LineTraceSingleByChannel(Hit, Target + FVector(0, 0, 150.f), Target - FVector(0, 0, 400.f), ECC_Visibility, Params))
		{
			GroundZ = Hit.ImpactPoint.Z + HalfHeight;
		}
	}
	Target.Z = FMath::FInterpTo(GetActorLocation().Z, GroundZ, DeltaSeconds, 8.f);
	SetActorLocation(Target);
	SetActorRotation(FMath::RInterpTo(GetActorRotation(), FRotator(0.f, (WalkAxis * WalkDir).Rotation().Yaw, 0.f), DeltaSeconds, 5.f));
}

void AHGTouristNPC::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Rig.IsValid())
	{
		return;
	}
	if (bWalker)
	{
		StepWalk(DeltaSeconds);
	}

	// Phone photos of the view (standing still), now and then a wave at passing riders.
	const bool bStill = Rig.WalkSpeed < 1.f;
	PhotoTimer -= DeltaSeconds;
	if (PhotoTimer <= 0.f)
	{
		PhotoTimer = FMath::FRandRange(6.f, 14.f);
		PhotoLeft = bStill && Phone ? FMath::FRandRange(2.f, 4.f) : 0.f;
	}
	PhotoLeft = FMath::Max(0.f, PhotoLeft - DeltaSeconds);
	WaveTimer -= DeltaSeconds;
	if (WaveTimer <= 0.f)
	{
		WaveTimer = FMath::FRandRange(12.f, 25.f);
		WaveLeft = PhotoLeft > 0.f ? 0.f : FMath::FRandRange(1.5f, 2.5f);
	}
	WaveLeft = FMath::Max(0.f, WaveLeft - DeltaSeconds);
	Rig.bPhoto = PhotoLeft > 0.f;
	Rig.bWave = WaveLeft > 0.f && !Rig.bPhoto;
	if (Phone)
	{
		Phone->SetVisibility(Rig.bPhoto);
	}
	if (!bWalker && !Rig.bPhoto)
	{
		// Idle: turn a little on the spot now and then (the head does most of the looking).
		const float Yaw = BaseYaw + 20.f * FMath::Sin(Rig.Clock * 0.12f + Rig.IdleSeed);
		SetActorRotation(FMath::RInterpTo(GetActorRotation(), FRotator(0.f, Yaw, 0.f), DeltaSeconds, 1.5f));
	}
	else if (Rig.bPhoto)
	{
		SetActorRotation(FMath::RInterpTo(GetActorRotation(), FRotator(0.f, BaseYaw, 0.f), DeltaSeconds, 3.f));
	}
	Rig.Animate(this, DeltaSeconds);
}
