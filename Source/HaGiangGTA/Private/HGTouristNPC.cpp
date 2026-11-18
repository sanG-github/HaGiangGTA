#include "HGTouristNPC.h"
#include "HGPaint.h"

#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

AHGTouristNPC::AHGTouristNPC()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.05f;

	Capsule = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Capsule"));
	Capsule->InitCapsuleSize(35.f, 90.f);
	Capsule->SetCollisionProfileName(TEXT("Pawn"));
	RootComponent = Capsule;

	BodyRoot = CreateDefaultSubobject<USceneComponent>(TEXT("BodyRoot"));
	BodyRoot->SetupAttachment(Capsule);
	BodyRoot->SetRelativeLocation(FVector(0.f, 0.f, -90.f));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeMesh(TEXT("/Engine/BasicShapes/Cone.Cone"));

	auto Make = [this](FName Name, USceneComponent* Parent, UStaticMesh* Mesh, const FVector& Loc, const FRotator& Rot, const FVector& Size)
	{
		UStaticMeshComponent* C = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		C->SetupAttachment(Parent);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCanEverAffectNavigation(false);
		C->SetRelativeLocation(Loc);
		C->SetRelativeRotation(Rot);
		C->SetRelativeScale3D(Size / 100.f);
		C->SetStaticMesh(Mesh);
		return C;
	};
	Legs = Make(TEXT("Legs"), BodyRoot, CylMesh.Object, FVector(0.f, 0.f, 44.f), FRotator::ZeroRotator, FVector(32.f, 28.f, 88.f));
	Torso = Make(TEXT("Torso"), BodyRoot, CylMesh.Object, FVector(0.f, 0.f, 118.f), FRotator::ZeroRotator, FVector(42.f, 30.f, 60.f));
	Head = Make(TEXT("Head"), BodyRoot, SphMesh.Object, FVector(2.f, 0.f, 163.f), FRotator::ZeroRotator, FVector(23.f, 21.f, 26.f));
	Hat = Make(TEXT("Hat"), BodyRoot, ConeMesh.Object, FVector(2.f, 0.f, 178.f), FRotator::ZeroRotator, FVector(46.f, 46.f, 14.f));
	Backpack = Make(TEXT("Backpack"), BodyRoot, CubeMesh.Object, FVector(-24.f, 0.f, 120.f), FRotator::ZeroRotator, FVector(20.f, 34.f, 50.f));
	Star = Make(TEXT("Star"), BodyRoot, CylMesh.Object, FVector(16.f, 0.f, 126.f), FRotator(90.f, 0.f, 0.f), FVector(13.f, 13.f, 1.f));
	PhoneArm = Make(TEXT("PhoneArm"), BodyRoot, CylMesh.Object, FVector(12.f, 18.f, 132.f), FRotator(-80.f, 0.f, 0.f), FVector(8.f, 8.f, 55.f));
	// Phone at the hand (arm tip). Sizes are in the arm's scaled space: 8 x 1.5 x 15 cm.
	Phone = Make(TEXT("Phone"), PhoneArm, CubeMesh.Object, FVector(0.f, 0.f, 50.f), FRotator::ZeroRotator, FVector(100.f, 19.f, 27.f));
}

void AHGTouristNPC::Paint(UStaticMeshComponent* C, const FLinearColor& Color)
{
	if (UMaterialInstanceDynamic* MID = HGPaint::MakeMID(C))
	{
		MID->SetVectorParameterValue(TEXT("Color"), Color);
	}
}

void AHGTouristNPC::ApplyStyle()
{
	const bool bBackpacker = Style == EHGTouristStyle::Backpacker;
	Backpack->SetVisibility(bBackpacker);
	Star->SetVisibility(bBackpacker);
	Hat->SetVisibility(Style == EHGTouristStyle::Local);
}

void AHGTouristNPC::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyStyle();
}

void AHGTouristNPC::BeginPlay()
{
	Super::BeginPlay();
	HGPaint::LimitDrawDistance(this);
	ApplyStyle();
	const bool bBackpacker = Style == EHGTouristStyle::Backpacker;
	const bool bRider = Style == EHGTouristStyle::EasyRider;
	const FLinearColor Skin = bBackpacker ? FLinearColor(0.85f, 0.62f, 0.5f) : FLinearColor(0.6f, 0.42f, 0.3f);
	Paint(Torso, bBackpacker ? FLinearColor(0.75f, 0.02f, 0.02f) : (bRider ? FLinearColor(0.75f, 0.95f, 0.05f) : FLinearColor(0.05f, 0.06f, 0.25f)));
	Paint(Legs, bRider ? FLinearColor(0.03f, 0.03f, 0.03f) : FLinearColor(0.12f, 0.14f, 0.2f));
	Paint(Head, Skin);
	Paint(PhoneArm, Skin);
	Paint(Hat, FLinearColor(0.85f, 0.75f, 0.5f));
	Paint(Backpack, FLinearColor(0.85f, 0.88f, 0.9f));
	Paint(Star, FLinearColor(1.f, 0.82f, 0.f));
	Paint(Phone, FLinearColor(0.02f, 0.02f, 0.02f));

	// Stand on the ground.
	FHitResult Hit;
	const FVector Loc = GetActorLocation();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(HGTouristGround), false, this);
	if (GetWorld()->LineTraceSingleByChannel(Hit, Loc + FVector(0, 0, 300.f), Loc - FVector(0, 0, 2000.f), ECC_Visibility, Params))
	{
		SetActorLocation(Hit.ImpactPoint + FVector(0.f, 0.f, Capsule->GetScaledCapsuleHalfHeight()));
	}
	if (!LookAtPoint.IsZero())
	{
		SetActorRotation(FRotator(0.f, (LookAtPoint - GetActorLocation()).Rotation().Yaw, 0.f));
	}
	BaseYaw = GetActorRotation().Yaw;
	Phase = FMath::FRand() * 20.f;
	PhotoTimer = FMath::FRandRange(1.f, 8.f);
}

void AHGTouristNPC::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Phase += DeltaSeconds;

	// Look around, then raise the phone for a photo now and then.
	PhotoTimer -= DeltaSeconds;
	if (PhotoTimer <= 0.f)
	{
		PhotoTimer = FMath::FRandRange(6.f, 14.f);
		PhotoLeft = FMath::FRandRange(2.f, 4.f);
	}
	PhotoLeft = FMath::Max(0.f, PhotoLeft - DeltaSeconds);
	const bool bPhoto = PhotoLeft > 0.f;

	const float Yaw = BaseYaw + (bPhoto ? 0.f : 35.f * FMath::Sin(Phase * 0.25f));
	SetActorRotation(FMath::RInterpTo(GetActorRotation(), FRotator(0.f, Yaw, 0.f), DeltaSeconds, 2.f));
	PhoneArm->SetRelativeRotation(FMath::RInterpTo(PhoneArm->GetRelativeRotation(),
		bPhoto ? FRotator(80.f, 0.f, 0.f) : FRotator(-80.f, 0.f, 0.f), DeltaSeconds, 6.f));
	BodyRoot->SetRelativeLocation(FVector(0.f, 0.f, -90.f + FMath::Sin(Phase * 1.6f) * 0.8f)); // breathing
}
