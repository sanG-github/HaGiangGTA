#include "HGCowActor.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
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
	Home = GetActorLocation();
	Target = Home;
	Phase = FMath::FRand() * 10.f;
	IdleTime = FMath::FRandRange(2.f, 8.f);
	for (UStaticMeshComponent* C : CoatParts)
	{
		if (UMaterialInstanceDynamic* MID = C->CreateAndSetMaterialInstanceDynamic(0))
		{
			MID->SetVectorParameterValue(TEXT("Color"), CoatColor);
		}
	}
	for (UStaticMeshComponent* C : DarkParts)
	{
		if (UMaterialInstanceDynamic* MID = C->CreateAndSetMaterialInstanceDynamic(0))
		{
			MID->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.05f, 0.04f, 0.03f));
		}
	}
	LoadedMoo = MooSound.LoadSynchronous();
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
		Head->SetRelativeRotation(FRotator(-25.f + 6.f * FMath::Sin(Phase * 2.f), 8.f * FMath::Sin(Phase * 0.5f), 0.f));
	}
	else
	{
		const FRotator Want = ToTarget.Rotation();
		const FRotator NewRot = FMath::RInterpConstantTo(GetActorRotation(), FRotator(0.f, Want.Yaw, 0.f), DeltaSeconds, 40.f);
		SetActorRotation(NewRot);
		FHitResult Block;
		AddActorWorldOffset(NewRot.Vector() * WalkSpeed * DeltaSeconds, true, &Block);
		if (Block.bBlockingHit)
		{
			PickNewTarget();
		}
		Head->SetRelativeRotation(FRotator(-5.f + 4.f * FMath::Sin(Phase * 3.f), 0.f, 0.f));
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
				UGameplayStatics::PlaySoundAtLocation(this, LoadedMoo, Loc, 1.f, FMath::FRandRange(0.85f, 1.1f));
			}
		}
	}
}
