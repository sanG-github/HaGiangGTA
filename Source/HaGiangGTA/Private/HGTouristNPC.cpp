#include "HGTouristNPC.h"
#include "HGMeshKit.h"
#include "HGReviewDirector.h"

#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Components/SplineComponent.h"
#include "HGRoadSplineActor.h"
#include "HGVehicleBase.h"

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
	case EHGTouristStyle::Local:
		Look = bFlowerSeller || (Hash & 1) ? EHGPersonLook::HmongWoman : ((Hash % 3) == 0 ? EHGPersonLook::HmongKid : EHGPersonLook::LocalMan);
		break;
	default: break;
	}
	const int32 Variant = Hash % HGPeople::NumVariants;
	// Flower sellers squat on their heels behind a tray of goods (the Doc Tham Ma / Ma Pi Leng roadside sellers).
	const EHGPersonPose Pose = bFlowerSeller ? EHGPersonPose::Squat : EHGPersonPose::Standing;
	Rig = HGPeople::Spawn(this, BodyRoot, Look, Pose, Variant, TEXT("Person"), FVector::ZeroVector,
		Look == EHGPersonLook::Backpacker && (Hash % 3) == 0);
	if (Rig.Arm[1] && Look != EHGPersonLook::HmongWoman && Look != EHGPersonLook::HmongKid)
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
	if (bKid)
	{
		SetActorScale3D(FVector(0.62f));
		SetActorLocation(GetActorLocation() - FVector(0.f, 0.f, HalfHeight * 0.38f));
	}
	if (bShoulderWalker)
	{
		// Find the road and remember where along it (and how far off its centre) we stand.
		float Best = BIG_NUMBER;
		for (TActorIterator<AHGRoadSplineActor> It(GetWorld()); It; ++It)
		{
			const float D = It->GetDistanceAlongRoad(Home);
			const FVector P = It->GetSpline()->GetLocationAtDistanceAlongSpline(D, ESplineCoordinateSpace::World);
			const float Off = FVector::Dist2D(P, Home);
			if (Off < Best)
			{
				Best = Off;
				ShoulderRoad = *It;
				ShoulderHome = D;
				const FVector T = It->GetSpline()->GetDirectionAtDistanceAlongSpline(D, ESplineCoordinateSpace::World).GetSafeNormal2D();
				ShoulderLat = FVector::DotProduct(Home - P, FVector::CrossProduct(FVector::UpVector, T));
			}
		}
		bWalker = ShoulderRoad.IsValid();
	}
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
	const float Speed = bKid ? 70.f : 85.f;
	WalkPos += WalkDir * Speed * DeltaSeconds;
	const float Range = ShoulderRoad.IsValid() ? ShoulderWalkM * 100.f : WalkRange;
	if (FMath::Abs(WalkPos) > Range)
	{
		WalkPos = FMath::Clamp(WalkPos, -Range, Range);
		WalkDir = -WalkDir;
		WalkPause = FMath::FRandRange(2.f, 6.f);   // stop, look at the view, then turn back
	}
	Rig.WalkSpeed = Speed;
	FVector Target = Home + WalkAxis * WalkPos;
	FVector WalkDirVec = WalkAxis * WalkDir;
	if (const AHGRoadSplineActor* Road = ShoulderRoad.Get())
	{
		// Along the shoulder at the same offset from the road centre (bends included), facing the way we walk.
		const USplineComponent* Sp = Road->GetSpline();
		const float D = FMath::Clamp(ShoulderHome + WalkPos, 0.f, Sp->GetSplineLength());
		const FVector T = Sp->GetDirectionAtDistanceAlongSpline(D, ESplineCoordinateSpace::World).GetSafeNormal2D();
		Target = Sp->GetLocationAtDistanceAlongSpline(D, ESplineCoordinateSpace::World) + FVector::CrossProduct(FVector::UpVector, T) * ShoulderLat;
		WalkDirVec = T * WalkDir;
	}
	GroundTimer -= DeltaSeconds;
	if (GroundTimer <= 0.f)
	{
		GroundTimer = 0.2f;
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(HGTouristGround), false, this);
		if (GetWorld()->LineTraceSingleByChannel(Hit, Target + FVector(0, 0, 150.f), Target - FVector(0, 0, 400.f), ECC_Visibility, Params))
		{
			GroundZ = Hit.ImpactPoint.Z + HalfHeight * GetActorScale3D().Z;
		}
	}
	Target.Z = FMath::FInterpTo(GetActorLocation().Z, GroundZ, DeltaSeconds, 8.f);
	SetActorLocation(Target);
	SetActorRotation(FMath::RInterpTo(GetActorRotation(), FRotator(0.f, WalkDirVec.Rotation().Yaw, 0.f), DeltaSeconds, 5.f));
}

int32 AHGTouristNPC::KidWaves = 0;

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
	if (bKid)
	{
		// Kids wave at every rider who passes close (Ha Giang's "hello!" from the roadside).
		KidCheck -= DeltaSeconds;
		if (KidCheck <= 0.f)
		{
			KidCheck = 0.3f;
			for (TActorIterator<AHGVehicleBase> It(GetWorld()); It; ++It)
			{
				if (FMath::Abs(It->GetSpeedKmh()) > 12.f && FVector::DistSquared(It->GetActorLocation(), GetActorLocation()) < FMath::Square(2500.f))
				{
					if (WaveLeft <= 0.f)
					{
						++KidWaves;
					}
					WaveLeft = 2.5f;
					const FVector To = It->GetActorLocation() - GetActorLocation();
					SetActorRotation(FRotator(0.f, To.Rotation().Yaw, 0.f));
					break;
				}
			}
		}
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
