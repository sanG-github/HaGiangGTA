#include "HGCowActor.h"
#include "HGPaint.h"
#include "HGMeshKit.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "HGAudio.h"
#include "HGWaterBody.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

AHGCowActor::AHGCowActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.033f;

	Body = CreateDefaultSubobject<UBoxComponent>(TEXT("Body"));
	Body->SetBoxExtent(FVector(110.f, 40.f, 70.f));
	Body->SetCollisionProfileName(TEXT("Pawn")); // vehicles collide, AI sweeps for ECC_Pawn see it
	RootComponent = Body;

	Visual = CreateDefaultSubobject<USceneComponent>(TEXT("Visual"));
	Visual->SetupAttachment(Body);
	Visual->SetRelativeLocation(FVector(0.f, 0.f, -70.f)); // hooves on the ground

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeMesh(TEXT("/Engine/BasicShapes/Cone.Cone"));

	AddPart(TEXT("Torso"), CubeMesh.Object, Visual, FVector(0.f, 0.f, 105.f), FVector(170.f, 62.f, 70.f), true);
	Head = AddPart(TEXT("Head"), CubeMesh.Object, Visual, FVector(110.f, 0.f, 125.f), FVector(48.f, 30.f, 34.f), true);
	AddPart(TEXT("Hump"), CubeMesh.Object, Visual, FVector(55.f, 0.f, 145.f), FVector(40.f, 40.f, 22.f), true);
	AddPart(TEXT("Muzzle"), CubeMesh.Object, Head, FVector(60.f, 0.f, -20.f), FVector(30.f, 26.f, 24.f), false);
	AddPart(TEXT("HornL"), ConeMesh.Object, Head, FVector(-10.f, -40.f, 60.f), FVector(8.f, 8.f, 18.f), false);
	AddPart(TEXT("HornR"), ConeMesh.Object, Head, FVector(-10.f, 40.f, 60.f), FVector(8.f, 8.f, 18.f), false);
	for (int32 i = 0; i < 4; ++i)
	{
		const float X = (i < 2) ? 60.f : -60.f;
		const float Y = (i % 2 == 0) ? -20.f : 20.f;
		AddPart(*FString::Printf(TEXT("Leg%d"), i), CylMesh.Object, Visual, FVector(X, Y, 37.f), FVector(13.f, 13.f, 75.f), true);
	}
	AddPart(TEXT("Tail"), CylMesh.Object, Visual, FVector(-92.f, 0.f, 95.f), FVector(5.f, 5.f, 55.f), false);

	MooSound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/HaGiang/Generated/Audio/SW_HG_Moo.SW_HG_Moo")));
}

UStaticMeshComponent* AHGCowActor::AddPart(FName Name, UStaticMesh* Mesh, USceneComponent* Parent, const FVector& Loc, const FVector& SizeCm, bool bCoat)
{
	UStaticMeshComponent* C = CreateDefaultSubobject<UStaticMeshComponent>(Name);
	C->SetupAttachment(Parent);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCanEverAffectNavigation(false);
	// Head-relative parts are given in "head" space, which is scaled; undo the parent scale for them.
	const FVector ParentScale = (Parent == Head && Head) ? Head->GetRelativeScale3D() : FVector::OneVector;
	C->SetRelativeLocation(Loc / ParentScale);
	C->SetRelativeScale3D((SizeCm / 100.f) / ParentScale);
	if (Mesh)
	{
		C->SetStaticMesh(Mesh);
	}
	(bCoat ? CoatParts : DarkParts).Add(C);
	return C;
}

void AHGCowActor::BeginPlay()
{
	Super::BeginPlay();
	HGPaint::LimitDrawDistance(this);
	BuildKit();
	if (bGoat)
	{
		SetActorScale3D(FVector(0.5f));
		WalkSpeed *= 1.4f;
	}
	Home = GetActorLocation();
	Target = Home;
	Phase = FMath::FRand() * 10.f;
	IdleTime = FMath::FRandRange(2.f, 8.f);
	for (UStaticMeshComponent* C : CoatParts)
	{
		if (UMaterialInstanceDynamic* MID = HGPaint::MakeMID(C))
		{
			MID->SetVectorParameterValue(TEXT("Color"), CoatColor);
		}
	}
	for (UStaticMeshComponent* C : DarkParts)
	{
		if (UMaterialInstanceDynamic* MID = HGPaint::MakeMID(C))
		{
			MID->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.05f, 0.04f, 0.03f));
		}
	}
	if (bHorse)
	{
		TArray<UStaticMeshComponent*> Parts;
		GetComponents<UStaticMeshComponent>(Parts);
		for (UStaticMeshComponent* C : Parts)
		{
			const FString N = C->GetName();
			if (N.StartsWith(TEXT("Horn")))
			{
				C->SetVisibility(false);
			}
			else if (N == TEXT("Hump"))
			{
				// Re-used as the neck.
				C->SetRelativeLocation(FVector(88.f, 0.f, 150.f));
				C->SetRelativeRotation(FRotator(-55.f, 0.f, 0.f));
				C->SetRelativeScale3D(FVector(0.75f, 0.28f, 0.3f));
			}
			else if (N == TEXT("Torso"))
			{
				C->SetRelativeScale3D(FVector(1.55f, 0.5f, 0.62f));
			}
			else if (N.StartsWith(TEXT("Leg")))
			{
				C->SetRelativeScale3D(FVector(0.1f, 0.1f, 0.9f));
				C->AddRelativeLocation(FVector(0.f, 0.f, 8.f));
			}
		}
		Head->SetRelativeLocation(FVector(125.f, 0.f, 185.f));
		LoadedMoo = nullptr;
	}
}

void AHGCowActor::ShooOff(const FVector& Away, float WalkMeters)
{
	if (HurryTime > 0.f)
	{
		return;
	}
	const FVector Dir = FVector(Away.X, Away.Y, 0.f).GetSafeNormal();
	Target = GetActorLocation() + Dir * WalkMeters * 100.f;
	Home = Target;
	IdleTime = FMath::FRandRange(8.f, 15.f);
	HurryTime = 8.f;
}

void AHGCowActor::PickNewTarget()
{
	const FVector2D R = FMath::RandPointInCircle(WanderRadius);
	Target = Home + FVector(R.X, R.Y, 0.f);
	IdleTime = FMath::FRandRange(3.f, 10.f);
}

void AHGCowActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Phase += DeltaSeconds;
	MooCooldown = FMath::Max(0.f, MooCooldown - DeltaSeconds);
	HurryTime = FMath::Max(0.f, HurryTime - DeltaSeconds);
	const float Pace = HurryTime > 0.f ? 3.f : 1.f;   // shooed off the road: a quick walk

	FVector Loc = GetActorLocation();
	const FVector ToTarget = FVector(Target.X - Loc.X, Target.Y - Loc.Y, 0.f);
	if (ToTarget.Size() < 40.f)
	{
		IdleTime -= DeltaSeconds;
		if (IdleTime <= 0.f)
		{
			PickNewTarget();
		}
		// Grazing / chewing head bob.
		SetHeadRotation(FRotator(-25.f + 6.f * FMath::Sin(Phase * 2.f), 8.f * FMath::Sin(Phase * 0.5f), 0.f));
	}
	else
	{
		const FRotator Want = ToTarget.Rotation();
		const FRotator NewRot = FMath::RInterpConstantTo(GetActorRotation(), FRotator(0.f, Want.Yaw, 0.f), DeltaSeconds, 40.f * Pace);
		SetActorRotation(NewRot);
		// Look 1 m ahead: cows on the real map graze beside cliff roads and the reservoir - never walk onto
		// ground steeper than MaxWanderSlopeDeg or into the water, pick another spot instead.
		const FVector Ahead = Loc + NewRot.Vector() * 100.f;
		FHitResult AheadGround;
		FCollisionQueryParams AheadParams(SCENE_QUERY_STAT(HGCowAhead), false, this);
		float SurfaceZ = 0.f;
		const bool bGroundAhead = GetWorld()->LineTraceSingleByChannel(AheadGround, Ahead + FVector(0, 0, 300.f), Ahead - FVector(0, 0, 1500.f), ECC_Visibility, AheadParams);
		const bool bTooSteep = !bGroundAhead || AheadGround.ImpactNormal.Z < FMath::Cos(FMath::DegreesToRadians(MaxWanderSlopeDeg));
		const bool bWet = bGroundAhead && AHGWaterBody::FindSurface(GetWorld(), Ahead, SurfaceZ) && AheadGround.ImpactPoint.Z < SurfaceZ + 20.f;
		if ((bTooSteep || bWet) && HurryTime <= 0.f)
		{
			PickNewTarget();
		}
		else if (bTooSteep || bWet)
		{
			HurryTime = 0.f;   // the shoulder drops away: stop at the edge instead of walking off it
			Target = Loc;
		}
		else
		{
			FHitResult Block;
			AddActorWorldOffset(NewRot.Vector() * WalkSpeed * Pace * DeltaSeconds, true, &Block);
			if (Block.bBlockingHit)
			{
				PickNewTarget();
			}
		}
		SetHeadRotation(FRotator(-5.f + 4.f * FMath::Sin(Phase * 3.f), 0.f, 0.f));
		StepPhase += DeltaSeconds * WalkSpeed * Pace / 14.f;
	}

	// Legs: walk cycle (diagonal pairs) while moving, settle when grazing; tail swish.
	if (KitLegs.Num() == 4)
	{
		const bool bWalking = ToTarget.Size() >= 40.f;
		for (int32 i = 0; i < 4; ++i)
		{
			const float Sign = (i == 0 || i == 3) ? 1.f : -1.f;
			const float Swing = bWalking ? 18.f * Sign * FMath::Sin(StepPhase) : 0.f;
			KitLegs[i]->SetRelativeRotation(FMath::RInterpTo(KitLegs[i]->GetRelativeRotation(), FRotator(Swing, 0.f, 0.f), DeltaSeconds, 10.f));
		}
	}
	if (KitTail)
	{
		KitTail->SetRelativeRotation(FRotator(0.f, 0.f, 14.f * FMath::Sin(Phase * 2.3f) * FMath::Sin(Phase * 0.4f)));
	}

	// Stick to the terrain.
	Loc = GetActorLocation();
	FHitResult Ground;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(HGCowGround), false, this);
	if (GetWorld()->LineTraceSingleByChannel(Ground, Loc + FVector(0, 0, 200.f), Loc - FVector(0, 0, 1000.f), ECC_Visibility, Params))
	{
		SetActorLocation(FVector(Loc.X, Loc.Y, Ground.ImpactPoint.Z + Body->GetScaledBoxExtent().Z));
	}

	// Moo at the player.
	if (MooCooldown <= 0.f && LoadedMoo)
	{
		if (const APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0))
		{
			if (FVector::DistSquared(Player->GetActorLocation(), Loc) < FMath::Square(2500.f))
			{
				MooCooldown = FMath::FRandRange(8.f, 16.f);
				HGAudio::PlayAt(this, LoadedMoo, Loc, bGoat ? 0.6f : 1.f, FMath::FRandRange(0.85f, 1.1f) * (bGoat ? 2.1f : 1.f), 6000.f);
			}
		}
	}
}

void AHGCowActor::SetHeadRotation(const FRotator& R)
{
	Head->SetRelativeRotation(R);
	if (KitHead)
	{
		KitHead->SetRelativeRotation(R);
	}
}

void AHGCowActor::BuildKit()
{
	// Ha Giang yellow cattle (bo vang: slim, small hump, dewlap, short horns) or a Hmong pack horse with a
	// wooden pack saddle. Coat parts are tinted by CoatColor (MID "Tint"); muzzle, hooves, horns are not.
	const bool bH = bHorse;
	const FLinearColor White(1.f, 1.f, 1.f), Belly(0.82f, 0.8f, 0.76f);
	const FLinearColor Dark(0.03f, 0.025f, 0.02f), Muzzle(0.14f, 0.11f, 0.1f), Horn(0.55f, 0.5f, 0.4f);
	UStaticMesh* BodyMesh = FHGMeshKit::Cached(bH ? TEXT("SM_HGHorseBody") : TEXT("SM_HGCowBody"), [&](FHGMeshKit& K)
	{
		K.Paint(White, HGSurf::Matte, true);
		if (bH)
		{
			K.Ellipsoid(FVector(0.f, 0.f, 112.f), FVector(62.f, 22.f, 27.f), FRotator::ZeroRotator, 14, 8);
			K.Ellipsoid(FVector(-42.f, 0.f, 116.f), FVector(26.f, 22.f, 26.f), FRotator::ZeroRotator, 10, 6);
			K.Tube(FVector(46.f, 0.f, 118.f), FVector(86.f, 0.f, 152.f), 18.f, 11.f, 10, false);       // neck, ~40 deg like a pony
			K.Paint(Dark);
			K.Box(FVector(62.f, 0.f, 145.f), FVector(48.f, 3.f, 12.f), FRotator(-40.f, 0.f, 0.f));    // mane
			// Wooden pack saddle with two sacks.
			K.Paint(FLinearColor(0.25f, 0.15f, 0.07f), HGSurf::Satin);
			K.Box(FVector(0.f, 0.f, 142.f), FVector(46.f, 34.f, 8.f));
			K.Paint(FLinearColor(0.45f, 0.4f, 0.3f));
			K.Ellipsoid(FVector(0.f, -25.f, 122.f), FVector(17.f, 7.f, 14.f), FRotator::ZeroRotator, 10, 6);
			K.Ellipsoid(FVector(0.f, 25.f, 122.f), FVector(17.f, 7.f, 14.f), FRotator::ZeroRotator, 10, 6);
		}
		else
		{
			K.Ellipsoid(FVector(0.f, 0.f, 95.f), FVector(66.f, 25.f, 29.f), FRotator::ZeroRotator, 14, 8);
			K.Ellipsoid(FVector(-46.f, 0.f, 98.f), FVector(26.f, 24.f, 27.f), FRotator::ZeroRotator, 10, 6);   // rump
			K.Ellipsoid(FVector(40.f, 0.f, 122.f), FVector(17.f, 12.f, 11.f), FRotator::ZeroRotator, 10, 6);   // hump
			K.Tube(FVector(52.f, 0.f, 100.f), FVector(80.f, 0.f, 112.f), 16.f, 11.f, 10, false);              // neck
			K.Ellipsoid(FVector(60.f, 0.f, 74.f), FVector(15.f, 5.f, 18.f), FRotator::ZeroRotator, 8, 6);       // dewlap
			K.Paint(Belly, HGSurf::Matte, true);
			K.Ellipsoid(FVector(0.f, 0.f, 76.f), FVector(48.f, 20.f, 12.f), FRotator::ZeroRotator, 12, 5);
		}
	});
	UStaticMesh* HeadMesh = FHGMeshKit::Cached(bH ? TEXT("SM_HGHorseHead") : TEXT("SM_HGCowHead"), [&](FHGMeshKit& K)
	{
		K.Paint(White, HGSurf::Matte, true);
		if (bH)
		{
			K.Ellipsoid(FVector(14.f, 0.f, 0.f), FVector(16.f, 9.f, 11.f), FRotator(30.f, 0.f, 0.f), 12, 6);
			K.Tube(FVector(14.f, 0.f, -2.f), FVector(36.f, 0.f, -20.f), 9.f, 6.5f, 10, true);
			K.Ellipsoid(FVector(2.f, -6.f, 13.f), FVector(3.f, 2.f, 7.f), FRotator::ZeroRotator, 6, 4);
			K.Ellipsoid(FVector(2.f, 6.f, 13.f), FVector(3.f, 2.f, 7.f), FRotator::ZeroRotator, 6, 4);
			K.Paint(Dark);
			K.Ellipsoid(FVector(37.f, 0.f, -21.f), FVector(4.f, 6.f, 5.f), FRotator::ZeroRotator, 8, 4);
		}
		else
		{
			K.Ellipsoid(FVector(14.f, 0.f, 0.f), FVector(18.f, 11.f, 12.f), FRotator(20.f, 0.f, 0.f), 12, 6);
			K.Ellipsoid(FVector(-2.f, -13.f, 4.f), FVector(4.f, 9.f, 3.5f), FRotator(0.f, 0.f, -15.f), 6, 4);    // ears
			K.Ellipsoid(FVector(-2.f, 13.f, 4.f), FVector(4.f, 9.f, 3.5f), FRotator(0.f, 0.f, 15.f), 6, 4);
			K.Paint(Muzzle);
			K.Ellipsoid(FVector(31.f, 0.f, -9.f), FVector(9.f, 9.f, 8.f), FRotator::ZeroRotator, 10, 5);
			K.Paint(Horn, HGSurf::Satin);
			K.Tube(FVector(2.f, -7.f, 10.f), FVector(-1.f, -12.f, 21.f), 1.8f, 0.6f, 6, true);
			K.Tube(FVector(2.f, 7.f, 10.f), FVector(-1.f, 12.f, 21.f), 1.8f, 0.6f, 6, true);
		}
		K.Paint(Dark);
		K.Ellipsoid(FVector(18.f, -9.5f, 4.f), FVector(2.f, 1.f, 2.f), FRotator::ZeroRotator, 5, 3);   // eyes
		K.Ellipsoid(FVector(18.f, 9.5f, 4.f), FVector(2.f, 1.f, 2.f), FRotator::ZeroRotator, 5, 3);
	});
	const float LegLen = bH ? 82.f : 74.f;
	UStaticMesh* LegMesh = FHGMeshKit::Cached(bH ? TEXT("SM_HGHorseLeg") : TEXT("SM_HGCowLeg"), [&](FHGMeshKit& K)
	{
		K.Paint(White, HGSurf::Matte, true);
		K.Tube(FVector::ZeroVector, FVector(0.f, 0.f, -LegLen * 0.52f), bH ? 9.f : 7.f, bH ? 5.5f : 4.5f, 8, true);
		K.Tube(FVector(0.f, 0.f, -LegLen * 0.52f), FVector(1.5f, 0.f, -LegLen + 6.f), bH ? 5.f : 4.f, bH ? 4.f : 3.2f, 8, false);
		K.Paint(Dark, HGSurf::Satin);
		K.Box(FVector(3.f, 0.f, -LegLen + 3.f), FVector(9.f, 7.f, 6.f));
	});
	UStaticMesh* TailMesh = FHGMeshKit::Cached(TEXT("SM_HGTail"), [&](FHGMeshKit& K)
	{
		K.Paint(White, HGSurf::Matte, true);
		K.Tube(FVector::ZeroVector, FVector(-6.f, 0.f, -48.f), 2.2f, 1.4f, 6, false);
		K.Paint(Dark);
		K.Ellipsoid(FVector(-7.f, 0.f, -56.f), FVector(3.f, 3.f, 9.f), FRotator::ZeroRotator, 6, 4);
	});
	auto Add = [this](UStaticMesh* M, USceneComponent* Parent, const TCHAR* Name, const FVector& Loc)
	{
		UStaticMeshComponent* C = FHGMeshKit::AddComponent(this, Parent, M, Name, Loc);
		if (C)
		{
			C->SetCullDistance(20000.f);
			if (UMaterialInstanceDynamic* MID = C->CreateDynamicMaterialInstance(0, FHGMeshKit::VertexColorMaterial()))
			{
				MID->SetVectorParameterValue(TEXT("Tint"), CoatColor);
			}
		}
		return C;
	};
	if (!Add(BodyMesh, Visual, TEXT("KitBody"), FVector::ZeroVector))
	{
		return;   // -hgnokit: keep the greybox
	}
	KitHead = Add(HeadMesh, Visual, TEXT("KitHead"), bH ? FVector(88.f, 0.f, 156.f) : FVector(80.f, 0.f, 113.f));
	KitTail = Add(TailMesh, Visual, TEXT("KitTail"), bH ? FVector(-66.f, 0.f, 128.f) : FVector(-70.f, 0.f, 112.f));
	const float Hip = bH ? 88.f : 76.f;
	const FVector LegAt[4] = { FVector(45.f, -13.f, Hip), FVector(45.f, 13.f, Hip), FVector(-50.f, -13.f, Hip), FVector(-50.f, 13.f, Hip) };
	for (int32 i = 0; i < 4; ++i)
	{
		KitLegs.Add(Add(LegMesh, Visual, *FString::Printf(TEXT("KitLeg%d"), i), LegAt[i]));
	}
	// Hide the greybox parts.
	for (UStaticMeshComponent* C : CoatParts)
	{
		C->SetVisibility(false);
	}
	for (UStaticMeshComponent* C : DarkParts)
	{
		C->SetVisibility(false);
	}
	Head->SetVisibility(false, true);
}
