#include "HGDogActor.h"

#include "Components/SplineComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HGAudio.h"
#include "HGMeshKit.h"
#include "HGRoadSplineActor.h"
#include "HGVehicleBase.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Sound/SoundBase.h"

int32 AHGDogActor::Barks = 0;
int32 AHGDogActor::Chases = 0;

namespace
{
	constexpr float HipZ = 34.f;
	constexpr float LegLen = 32.f;
}

AHGDogActor::AHGDogActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.03f;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	Visual = CreateDefaultSubobject<USceneComponent>(TEXT("Visual"));
	Visual->SetupAttachment(Root);
	SetActorEnableCollision(false);
}

void AHGDogActor::BuildKit()
{
	const FLinearColor White(1.f, 1.f, 1.f), Dark(0.03f, 0.025f, 0.02f), Nose(0.06f, 0.05f, 0.05f);
	UStaticMesh* BodyMesh = FHGMeshKit::Cached(TEXT("SM_HGDogBody"), [&](FHGMeshKit& K)
	{
		K.Paint(White, HGSurf::Matte, true);
		K.Ellipsoid(FVector(-2.f, 0.f, 40.f), FVector(30.f, 10.f, 11.f), FRotator::ZeroRotator, 12, 7);    // barrel, tucked belly
		K.Ellipsoid(FVector(17.f, 0.f, 41.f), FVector(13.f, 11.f, 13.f), FRotator::ZeroRotator, 10, 6);    // deep chest
		K.Ellipsoid(FVector(-24.f, 0.f, 42.f), FVector(10.f, 9.f, 10.f), FRotator::ZeroRotator, 8, 5);     // haunch
		K.Tube(FVector(24.f, 0.f, 45.f), FVector(33.f, 0.f, 57.f), 7.5f, 5.5f, 8, false);                 // neck
	});
	UStaticMesh* HeadMesh = FHGMeshKit::Cached(TEXT("SM_HGDogHead"), [&](FHGMeshKit& K)
	{
		K.Paint(White, HGSurf::Matte, true);
		K.Ellipsoid(FVector(2.f, 0.f, 0.f), FVector(8.f, 6.5f, 6.5f), FRotator::ZeroRotator, 10, 6);       // skull
		K.Tube(FVector(6.f, 0.f, -2.f), FVector(17.f, 0.f, -4.f), 4.f, 2.6f, 8, true);                  // muzzle
		// Erect pointed ears (the Vietnamese "cho ta" look).
		K.Frustum(FTransform(FRotator(0.f, 0.f, -12.f), FVector(-1.f, -4.f, 5.f)), FVector2D(2.6f, 1.2f), FVector2D(0.3f, 0.2f), 7.f, 4, true);
		K.Frustum(FTransform(FRotator(0.f, 0.f, 12.f), FVector(-1.f, 4.f, 5.f)), FVector2D(2.6f, 1.2f), FVector2D(0.3f, 0.2f), 7.f, 4, true);
		K.Paint(Nose, HGSurf::Satin);
		K.Ellipsoid(FVector(17.5f, 0.f, -3.5f), FVector(1.6f, 1.8f, 1.4f), FRotator::ZeroRotator, 6, 4);
		K.Paint(Dark);
		K.Ellipsoid(FVector(7.f, -4.f, 2.f), FVector(1.2f, 0.8f, 1.2f), FRotator::ZeroRotator, 5, 3);
		K.Ellipsoid(FVector(7.f, 4.f, 2.f), FVector(1.2f, 0.8f, 1.2f), FRotator::ZeroRotator, 5, 3);
	});
	UStaticMesh* TailMesh = FHGMeshKit::Cached(TEXT("SM_HGDogTail"), [&](FHGMeshKit& K)
	{
		K.Paint(White, HGSurf::Matte, true);
		K.Tube(FVector::ZeroVector, FVector(-8.f, 0.f, 12.f), 2.6f, 2.2f, 6, false);                     // curled up over the back
		K.Tube(FVector(-8.f, 0.f, 12.f), FVector(-3.f, 0.f, 20.f), 2.2f, 1.2f, 6, true);
	});
	UStaticMesh* LegMesh = FHGMeshKit::Cached(TEXT("SM_HGDogLeg"), [&](FHGMeshKit& K)
	{
		K.Paint(White, HGSurf::Matte, true);
		K.Tube(FVector::ZeroVector, FVector(0.f, 0.f, -LegLen * 0.55f), 3.4f, 2.4f, 6, true);
		K.Tube(FVector(0.f, 0.f, -LegLen * 0.55f), FVector(1.f, 0.f, -LegLen + 2.f), 2.2f, 1.9f, 6, false);
		K.Ellipsoid(FVector(2.f, 0.f, -LegLen + 1.5f), FVector(3.f, 2.2f, 1.6f), FRotator::ZeroRotator, 6, 3);
	});
	auto Add = [this](UStaticMesh* M, USceneComponent* Parent, const TCHAR* Name, const FVector& Loc)
	{
		UStaticMeshComponent* C = FHGMeshKit::AddComponent(this, Parent, M, Name, Loc);
		if (C)
		{
			C->SetCullDistance(9000.f);
			C->SetCastShadow(true);
			C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			if (UMaterialInstanceDynamic* MID = C->CreateDynamicMaterialInstance(0, FHGMeshKit::VertexColorMaterial()))
			{
				MID->SetVectorParameterValue(TEXT("Tint"), CoatColor);
			}
		}
		return C;
	};
	if (!Add(BodyMesh, Visual, TEXT("KitBody"), FVector::ZeroVector))
	{
		return;
	}
	KitHead = Add(HeadMesh, Visual, TEXT("KitHead"), FVector(36.f, 0.f, 59.f));
	KitTail = Add(TailMesh, Visual, TEXT("KitTail"), FVector(-31.f, 0.f, 46.f));
	const FVector LegAt[4] = { FVector(19.f, -6.f, HipZ), FVector(19.f, 6.f, HipZ), FVector(-22.f, -6.f, HipZ), FVector(-22.f, 6.f, HipZ) };
	for (int32 i = 0; i < 4; ++i)
	{
		KitLegs.Add(Add(LegMesh, Visual, *FString::Printf(TEXT("KitLeg%d"), i), LegAt[i]));
	}
}

void AHGDogActor::BeginPlay()
{
	Super::BeginPlay();
	BuildKit();
	Home = GetActorLocation();
	HomeYaw = GetActorRotation().Yaw;
	GroundZ = Home.Z;
	Clock = FMath::FRand() * 10.f;
	Dir = FMath::RandBool() ? 1.f : -1.f;
	// Road frame: where along the road we live and how far off it (kept off the carriageway).
	float Best = BIG_NUMBER;
	for (TActorIterator<AHGRoadSplineActor> It(GetWorld()); It; ++It)
	{
		const float D = It->GetDistanceAlongRoad(Home);
		const USplineComponent* Sp = It->GetSpline();
		const FVector P = Sp->GetLocationAtDistanceAlongSpline(D, ESplineCoordinateSpace::World);
		const float Off = FVector::Dist2D(P, Home);
		if (Off < Best)
		{
			Best = Off;
			Road = *It;
			HomeD = D;
			const FVector T = Sp->GetDirectionAtDistanceAlongSpline(D, ESplineCoordinateSpace::World).GetSafeNormal2D();
			Lat = FVector::DotProduct(Home - P, FVector::CrossProduct(FVector::UpVector, T));
			MinLat = It->GetWidthAtDistance(D) * 0.5f + 60.f;
		}
	}
	if (!Road.IsValid() || Best > 6000.f)
	{
		Mode = EHGDogMode::Lie;   // far from any road: just a yard dog
	}
}

FVector AHGDogActor::ShoulderPoint(float AlongCm) const
{
	const AHGRoadSplineActor* R = Road.Get();
	if (!R)
	{
		return Home;
	}
	const USplineComponent* Sp = R->GetSpline();
	const float D = FMath::Clamp(HomeD + AlongCm, 0.f, Sp->GetSplineLength());
	const FVector T = Sp->GetDirectionAtDistanceAlongSpline(D, ESplineCoordinateSpace::World).GetSafeNormal2D();
	const float L = FMath::Sign(Lat == 0.f ? 1.f : Lat) * FMath::Max(FMath::Abs(Lat), MinLat);
	return Sp->GetLocationAtDistanceAlongSpline(D, ESplineCoordinateSpace::World) + FVector::CrossProduct(FVector::UpVector, T) * L;
}

void AHGDogActor::Bark()
{
	USoundBase* S = LoadObject<USoundBase>(nullptr, TEXT("/Game/HaGiang/Generated/Audio/SW_HG_DogBark.SW_HG_DogBark"));
	if (S)
	{
		HGAudio::PlayAt(this, S, GetActorLocation(), 0.9f, FMath::FRandRange(0.85f, 1.15f), 5000.f);
	}
	++Barks;
}

void AHGDogActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Clock += DeltaSeconds;
	ChaseCooldown = FMath::Max(0.f, ChaseCooldown - DeltaSeconds);
	const FVector Loc = GetActorLocation();

	// Someone riding past close by: bark, and chase alongside for a few seconds (on the shoulder only).
	CheckTimer -= DeltaSeconds;
	if (CheckTimer <= 0.f)
	{
		CheckTimer = 0.25f;
		if (!ChaseTarget.IsValid() && ChaseCooldown <= 0.f)
		{
			for (TActorIterator<AHGVehicleBase> It(GetWorld()); It; ++It)
			{
				if (!It->IsWaterVehicle() && FMath::Abs(It->GetSpeedKmh()) > 12.f && FVector::DistSquared2D(It->GetActorLocation(), Loc) < FMath::Square(1800.f))
				{
					ChaseTarget = *It;
					ChaseTime = FMath::FRandRange(2.f, 3.5f);
					ChaseCooldown = 9.f;
					BarkLeft = 3.f;
					BarkGap = 0.f;
					++Chases;
					break;
				}
			}
		}
	}
	if (BarkLeft > 0.f)
	{
		BarkLeft -= DeltaSeconds;
		BarkGap -= DeltaSeconds;
		if (BarkGap <= 0.f)
		{
			BarkGap = FMath::FRandRange(0.7f, 1.1f);
			Bark();
		}
	}

	FVector Target = Loc;
	float WantSpeed = 0.f;
	FVector Face = FRotator(0.f, HomeYaw, 0.f).Vector();
	if (AActor* Car = ChaseTarget.Get())
	{
		ChaseTime -= DeltaSeconds;
		if (ChaseTime <= 0.f || !Road.IsValid())
		{
			ChaseTarget = nullptr;
		}
		else
		{
			// Run along the shoulder towards where the bike is, at most 8 m from where the chase started.
			const float CarD = Road->GetDistanceAlongRoad(Car->GetActorLocation()) - HomeD;
			Along = FMath::FInterpConstantTo(Along, FMath::Clamp(CarD, Along - 800.f, Along + 800.f), DeltaSeconds, 520.f);
			Along = FMath::Clamp(Along, -RangeM * 100.f, RangeM * 100.f);
			Target = ShoulderPoint(Along);
			WantSpeed = 520.f;
			Face = (Car->GetActorLocation() - Loc).GetSafeNormal2D();
		}
	}
	if (!ChaseTarget.IsValid())
	{
		if (Mode == EHGDogMode::Trot && Road.IsValid())
		{
			if (Pause > 0.f)
			{
				Pause -= DeltaSeconds;
			}
			else
			{
				Along += Dir * 140.f * DeltaSeconds;
				if (FMath::Abs(Along) > RangeM * 100.f)
				{
					Along = FMath::Clamp(Along, -RangeM * 100.f, RangeM * 100.f);
					Dir = -Dir;
					Pause = FMath::FRandRange(3.f, 9.f);   // sniff about, then trot back
				}
				WantSpeed = 140.f;
			}
			Target = ShoulderPoint(Along);
		}
		else
		{
			// Back to the home spot after a chase, then lie down.
			Target = Home;
			WantSpeed = FVector::Dist2D(Home, Loc) > 30.f ? 200.f : 0.f;
		}
	}
	FVector Step = (Target - Loc) * FVector(1.f, 1.f, 0.f);
	const float Dist = Step.Size();
	const float Move = FMath::Min(Dist, FMath::Max(WantSpeed, 150.f) * DeltaSeconds);
	Speed = Dist > 2.f && WantSpeed > 0.f ? Move / FMath::Max(DeltaSeconds, 1e-3f) : 0.f;
	FVector NewLoc = Loc + (Dist > 1.f ? Step / Dist * Move : FVector::ZeroVector);
	if (Speed > 10.f && ChaseTarget.IsValid() == false)
	{
		Face = Step.GetSafeNormal2D();
	}
	GroundTimer -= DeltaSeconds;
	if (GroundTimer <= 0.f)
	{
		GroundTimer = 0.12f;
		FHitResult Hit;
		FCollisionQueryParams Q(SCENE_QUERY_STAT(HGDogGround), false, this);
		if (GetWorld()->LineTraceSingleByChannel(Hit, NewLoc + FVector(0, 0, 200.f), NewLoc - FVector(0, 0, 600.f), ECC_Visibility, Q))
		{
			GroundZ = Hit.ImpactPoint.Z;
		}
	}
	NewLoc.Z = FMath::FInterpTo(Loc.Z, GroundZ, DeltaSeconds, 10.f);
	SetActorLocation(NewLoc);
	if (!Face.IsNearlyZero())
	{
		SetActorRotation(FMath::RInterpTo(GetActorRotation(), FRotator(0.f, Face.Rotation().Yaw, 0.f), DeltaSeconds, 6.f));
	}

	// Pose: lying (legs folded, body low, head up and turning) or walking / running (leg swing, head bob).
	const bool bLying = Mode == EHGDogMode::Lie && !ChaseTarget.IsValid() && Speed < 5.f;
	const float LieZ = bLying ? -24.f : 0.f;
	Visual->SetRelativeLocation(FMath::VInterpTo(Visual->GetRelativeLocation(), FVector(0.f, 0.f, LieZ), DeltaSeconds, 5.f));
	StepPhase += DeltaSeconds * Speed / 9.f;
	for (int32 i = 0; i < KitLegs.Num(); ++i)
	{
		if (!KitLegs[i]) { continue; }
		const float Sgn = (i == 0 || i == 3) ? 1.f : -1.f;
		const float Swing = bLying ? (i < 2 ? -80.f : 80.f) : (Speed > 5.f ? Sgn * FMath::Sin(StepPhase) * FMath::Clamp(Speed / 250.f, 0.4f, 1.f) * 32.f : 0.f);
		KitLegs[i]->SetRelativeRotation(FMath::RInterpTo(KitLegs[i]->GetRelativeRotation(), FRotator(Swing, 0.f, 0.f), DeltaSeconds, 14.f));
	}
	if (KitHead)
	{
		const float Look = bLying ? 25.f * FMath::Sin(Clock * 0.4f) : 0.f;
		const float Bob = Speed > 5.f ? 4.f * FMath::Sin(StepPhase * 2.f) : (BarkLeft > 0.f ? 10.f * FMath::Abs(FMath::Sin(Clock * 9.f)) : 0.f);
		KitHead->SetRelativeRotation(FRotator(Bob, Look, 0.f));
	}
	if (KitTail)
	{
		const float Wag = (ChaseTarget.IsValid() || BarkLeft > 0.f) ? 25.f * FMath::Sin(Clock * 16.f) : 8.f * FMath::Sin(Clock * 3.f);
		KitTail->SetRelativeRotation(FRotator(0.f, Wag, 0.f));
	}
}
