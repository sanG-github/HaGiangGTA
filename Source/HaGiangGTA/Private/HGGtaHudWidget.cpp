#include "HGGtaHudWidget.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/SplineComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "HGCowActor.h"
#include "HGGroundVehicle.h"
#include "HGMissionSubsystem.h"
#include "NhoQueBoatPawn.h"
#include "HGHappyWaterPickup.h"
#include "HGHappyWaterSubsystem.h"
#include "HGPlayerCharacter.h"
#include "HGPoliceBikePawn.h"
#include "HGPoliceHelicopter.h"
#include "HGRoadSplineActor.h"
#include "HGTruckPawn.h"
#include "HGVehicleBase.h"
#include "HGViewpointActor.h"
#include "HGWantedSubsystem.h"
#include "NhoQueBoatPawn.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

namespace
{
	const FSlateBrush* White() { return FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")); }

	FSlateFontInfo Font(int32 Size, bool bOutline = true, const TCHAR* Typeface = TEXT("Bold"))
	{
		FSlateFontInfo F = FCoreStyle::GetDefaultFontStyle(Typeface, Size);
		if (bOutline)
		{
			F.OutlineSettings.OutlineSize = FMath::Max(1, Size / 12);
			F.OutlineSettings.OutlineColor = FLinearColor(0.f, 0.f, 0.f, 0.9f);
		}
		return F;
	}

	FVector2D Measure(const FString& Text, const FSlateFontInfo& F)
	{
		const TSharedRef<FSlateFontMeasure> M = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		const auto S = M->Measure(Text, F);
		return FVector2D(S.X, S.Y);
	}

	void Box(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, float X, float Y, float W, float H, const FLinearColor& C)
	{
		FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(FVector2D(W, H), FSlateLayoutTransform(FVector2D(X, Y))), White(), ESlateDrawEffect::None, C);
	}

	void Text(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FString& S, float X, float Y, const FSlateFontInfo& F, const FLinearColor& C)
	{
		const FVector2D Size = Measure(S, F);
		FSlateDrawElement::MakeText(Out, Layer, G.ToPaintGeometry(Size, FSlateLayoutTransform(FVector2D(X, Y))), S, F, ESlateDrawEffect::None, C);
	}

	/** Right-aligned text ending at X. */
	void TextR(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FString& S, float X, float Y, const FSlateFontInfo& F, const FLinearColor& C)
	{
		Text(Out, Layer, G, S, X - Measure(S, F).X, Y, F, C);
	}

	void Lines(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const TArray<FVector2f>& P, const FLinearColor& C, float Thickness)
	{
		if (P.Num() >= 2)
		{
			FSlateDrawElement::MakeLines(Out, Layer, G.ToPaintGeometry(), P, ESlateDrawEffect::None, C, true, Thickness);
		}
	}

	TArray<FVector2f> StarShape(float CX, float CY, float R)
	{
		TArray<FVector2f> P;
		for (int32 i = 0; i <= 10; ++i)
		{
			const float A = FMath::DegreesToRadians(-90.f + 36.f * i);
			const float Rad = (i % 2 == 0) ? R : R * 0.45f;
			P.Add(FVector2f(CX + Rad * FMath::Cos(A), CY + Rad * FMath::Sin(A)));
		}
		return P;
	}

	FString FormatVnd(int32 Value)
	{
		FString Digits = FString::FromInt(FMath::Abs(Value));
		FString Out;
		for (int32 i = 0; i < Digits.Len(); ++i)
		{
			if (i > 0 && (Digits.Len() - i) % 3 == 0)
			{
				Out.AppendChar(TEXT('.'));
			}
			Out.AppendChar(Digits[i]);
		}
		return Out + TEXT(" VNĐ");
	}
}

void UHGGtaHudWidget::CacheRoad()
{
	if (RoadLines.Num() > 0)
	{
		return;
	}
	// Every road actor as its own polyline (joining them drew stray lines between the route and the QL4C
	// stubs); the main route (longest) first. 6 m samples keep the 8 m Ta Lang hairpins readable.
	TArray<const AHGRoadSplineActor*> Roads;
	for (TActorIterator<AHGRoadSplineActor> It(GetWorld()); It; ++It)
	{
		Roads.Add(*It);
	}
	Roads.Sort([](const AHGRoadSplineActor& A, const AHGRoadSplineActor& B) { return A.GetRoadLength() > B.GetRoadLength(); });
	if (const TArray<FVector>* River = ANhoQueBoatPawn::FindCruiseLine(GetWorld()))
	{
		for (const FVector& P : *River)
		{
			RiverLine.Add(FVector2D(P.X, P.Y));
		}
	}
	for (const AHGRoadSplineActor* Road : Roads)
	{
		const USplineComponent* Spline = Road->GetSpline();
		const float Len = Spline->GetSplineLength();
		TArray<FVector2D>& Line = RoadLines.AddDefaulted_GetRef();
		for (float D = 0.f; D < Len + 600.f; D += 600.f)
		{
			const FVector P = Spline->GetLocationAtDistanceAlongSpline(FMath::Min(D, Len), ESplineCoordinateSpace::World);
			Line.Add(FVector2D(P.X, P.Y));
		}
	}
}

void UHGGtaHudWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	Time += InDeltaTime;
	CardTime = FMath::Max(0.f, CardTime - InDeltaTime);

	UWorld* World = GetWorld();
	APlayerController* PC = GetOwningPlayer();
	APawn* Pawn = GetOwningPlayerPawn();
	if (!World || !PC || !Pawn)
	{
		return;
	}
	CacheRoad();

	const FVector Loc = Pawn->GetActorLocation();
	PlayerXY = FVector2D(Loc.X, Loc.Y);
	AltitudeM = Loc.Z / 100.f; // world Z (cm) == metres above sea level
	ViewYaw = PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraRotation().Yaw : Pawn->GetActorRotation().Yaw;

	// Mode, speed, prompts.
	Prompt.Reset();
	SurfaceName.Reset();
	bDriving = false;
	bOnWater = false;
	if (const AHGVehicleBase* Vehicle = Cast<AHGVehicleBase>(Pawn))
	{
		bDriving = true;
		bOnWater = Vehicle->IsWaterVehicle();
		ModeName = Vehicle->GetVehicleDisplayName().ToString();
		SpeedKmh = FMath::Abs(Vehicle->GetSpeedKmh());
		if (const AHGGroundVehicle* Ground = Cast<AHGGroundVehicle>(Vehicle))
		{
			const EHGSurface Surf = Ground->GetSurface();
			SurfaceName = Surf == EHGSurface::Concrete ? TEXT("đường bê tông") : Surf == EHGSurface::OffRoad ? TEXT("đường đất") : TEXT("");
		}
	}
	else if (const AHGPlayerCharacter* Player = Cast<AHGPlayerCharacter>(Pawn))
	{
		ModeName = Player->GetOnFootState() == EHGOnFootState::Swimming ? TEXT("Đang bơi") : TEXT("Đi bộ");
		SpeedKmh = Player->GetSpeedKmh();
		if (const AHGVehicleBase* Focus = Player->GetFocusedVehicle())
		{
			Prompt = FString::Printf(TEXT("Nhấn  F  để lên %s"), *Focus->GetVehicleDisplayName().ToString());
		}
	}

	// Viewpoints: GTA location card on arrival, [V] prompt.
	AHGViewpointActor* Near = AHGViewpointActor::FindNearby(World, Loc);
	ViewpointPrompt = Near ? FString::Printf(TEXT("Nhấn  V  -  Flycam %s"), *Near->ViewpointName.ToString()) : FString();
	if (Near && LastViewpoint.Get() != Near)
	{
		CardName = Near->ViewpointName.ToString();
		CardAltitude = Near->AltitudeM;
		CardTime = 5.f;
	}
	LastViewpoint = Near;

	const AHGViewpointActor* Active = AHGViewpointActor::GetActive(World);
	bFlycam = Active != nullptr;
	if (Active)
	{
		FlyName = Active->ViewpointName.ToString();
		FlyAltitude = Active->AltitudeM;
		FlyT = Active->GetActiveSeconds();
		FlyDuration = Active->OrbitDuration;
	}

	// Wanted, money, Happy Water.
	if (const UHGWantedSubsystem* Wanted = World->GetSubsystem<UHGWantedSubsystem>())
	{
		Stars = Wanted->GetStars();
		bSpotted = Wanted->IsPlayerSpotted();
		Money = Wanted->GetMoney();
		BustedTime = Wanted->GetBustedMessageTime();
		CrimeReason = Wanted->GetLastReason();
		CrimeReasonTime = Wanted->GetReasonTime();
	}
	if (const UHGHappyWaterSubsystem* HW = World->GetSubsystem<UHGHappyWaterSubsystem>())
	{
		HappyRemaining = HW->GetRemaining();
	}

	// Minimap zoom: 350 m to the edge on foot / in the hairpins, out to 750 m at 90+ km/h on the long QL4C
	// stretches of the 19.5 km route (the old fixed 400 m showed under 25 s of road at speed).
	const float WantRange = FMath::GetMappedRangeValueClamped(FVector2D(25.f, 90.f), FVector2D(35000.f, 75000.f), SpeedKmh);
	MapRange = FMath::FInterpTo(MapRange, WantRange, InDeltaTime, 1.5f);

	if (const UHGMissionSubsystem* M = World->GetSubsystem<UHGMissionSubsystem>())
	{
		Objective = M->GetObjective();
		MissionBanner = M->GetBanner();
		MissionBannerTime = M->GetBannerTime();
		FlashTime = M->GetFlashTime();
		if (Prompt.IsEmpty())
		{
			Prompt = M->GetPrompt();
		}
	}

	// Minimap blips: rebuilt 10x a second (walking every actor in the level each frame cost game-thread time
	// on the busy viewpoints).
	BlipTimer -= InDeltaTime;
	if (BlipTimer > 0.f)
	{
		return;
	}
	BlipTimer = 0.1f;
	Blips.Reset();
	if (const UHGMissionSubsystem* MS = World->GetSubsystem<UHGMissionSubsystem>())
	{
		// Active job: its target (magenta diamond, pinned to the map edge); otherwise the job offers (yellow).
		if (const FHGMission* Job0 = MS->GetActive())
		{
			FBlip B;
			B.World = FVector2D(Job0->Target.X, Job0->Target.Y);
			B.Color = FLinearColor(1.f, 0.2f, 0.85f);
			B.Size = 9.f;
			B.bDiamond = true;
			B.bClampToEdge = true;
			Blips.Add(B);
		}
		else
		{
			for (const FHGMission& Job : MS->GetMissions())
			{
				if (Job.State == EHGMissionState::Available)
				{
					FBlip B;
					B.World = FVector2D(Job.Start.X, Job.Start.Y);
					B.Color = FLinearColor(1.f, 0.9f, 0.1f);
					B.Size = 7.f;
					B.bDiamond = true;
					B.bClampToEdge = true;
					Blips.Add(B);
				}
			}
		}
		if (!MS->GetBoatHirePoint().IsNearlyZero())
		{
			FBlip B;
			B.World = FVector2D(MS->GetBoatHirePoint().X, MS->GetBoatHirePoint().Y);
			B.Color = FLinearColor(0.2f, 0.6f, 1.f);
			B.Size = 6.f;
			B.bSquare = true;
			Blips.Add(B);
		}
	}
	const float RangeSq = FMath::Square(MapRange * 1.5f);
	const bool bBlink = FMath::Fmod(Time, 0.5f) < 0.25f;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* A = *It;
		if (A == Pawn)
		{
			continue;
		}
		FBlip B;
		const FVector P = A->GetActorLocation();
		B.World = FVector2D(P.X, P.Y);
		if (A->IsA<AHGViewpointActor>())
		{
			B.Color = FLinearColor(1.f, 0.82f, 0.1f);
			B.Size = 9.f;
			B.bSquare = true;
			B.bClampToEdge = true;
		}
		else if (FVector::DistSquared(P, Loc) > RangeSq)
		{
			continue;
		}
		else if (A->IsA<AHGPoliceBikePawn>() || A->IsA<AHGPoliceHelicopter>())
		{
			B.Color = bBlink ? FLinearColor(1.f, 0.1f, 0.1f) : FLinearColor(0.2f, 0.4f, 1.f);
			B.Size = A->IsA<AHGPoliceHelicopter>() ? 9.f : 7.f;
		}
		else if (A->IsA<AHGTruckPawn>())
		{
			B.Color = FLinearColor(1.f, 0.55f, 0.1f);
			B.Size = 7.f;
		}
		else if (A->IsA<ANhoQueBoatPawn>())
		{
			B.Color = FLinearColor(0.2f, 0.6f, 1.f);
			B.Size = 6.f;
		}
		else if (A->IsA<AHGVehicleBase>())
		{
			B.Color = FLinearColor(0.3f, 0.95f, 1.f); // tour bikes
			B.Size = 4.f;
		}
		else if (A->IsA<AHGCowActor>())
		{
			B.Color = FLinearColor(0.55f, 0.35f, 0.15f);
			B.Size = 4.f;
		}
		else if (A->IsA<AHGHappyWaterPickup>())
		{
			B.Color = FLinearColor(1.f, 0.75f, 0.3f);
			B.Size = 4.f;
			B.bSquare = true;
		}
		else
		{
			continue;
		}
		Blips.Add(B);
	}
}

int32 UHGGtaHudWidget::NativePaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& Out, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	LayerId = Super::NativePaint(Args, G, MyCullingRect, Out, LayerId, InWidgetStyle, bParentEnabled);
	const FVector2D Size = G.GetLocalSize();
	const float W = Size.X;
	const float H = Size.Y;
	const float S = FMath::Clamp(H / 1080.f, 0.6f, 2.f);
	int32 L = LayerId + 1;

	// ---------------- Flycam: letterbox + location card only ----------------
	if (bFlycam)
	{
		const float Fade = FMath::Clamp(FlyT / 1.2f, 0.f, 1.f) * FMath::Clamp((FlyDuration - FlyT) / 1.2f, 0.f, 1.f);
		const float Bar = H * 0.11f * FMath::Clamp(FlyT / 0.8f, 0.f, 1.f);
		Box(Out, L, G, 0.f, 0.f, W, Bar, FLinearColor::Black);
		Box(Out, L, G, 0.f, H - Bar, W, Bar, FLinearColor::Black);
		TextR(Out, L + 1, G, FlyName, W - 70.f * S, H - Bar - 170.f * S, Font(int32(46 * S)), FLinearColor(1.f, 1.f, 1.f, Fade));
		TextR(Out, L + 1, G, FString::Printf(TEXT("%d m"), FlyAltitude), W - 70.f * S, H - Bar - 100.f * S, Font(int32(30 * S)), FLinearColor(1.f, 0.8f, 0.25f, Fade));
		Text(Out, L + 1, G, TEXT("V  quay lại"), 40.f * S, H - Bar - 50.f * S, Font(int32(16 * S)), FLinearColor(0.85f, 0.85f, 0.85f, 0.7f * Fade));
		return L + 2;
	}

	// ---------------- Money + wanted stars (top right) ----------------
	const FSlateFontInfo MoneyFont = Font(int32(40 * S));
	TextR(Out, L, G, FormatVnd(Money), W - 40.f * S, 30.f * S, MoneyFont, FLinearColor(0.35f, 0.85f, 0.35f));
	const float StarR = 17.f * S;
	const bool bFlash = Stars > 0 && !bSpotted && FMath::Fmod(Time, 0.8f) < 0.4f;
	for (int32 i = 0; i < 5; ++i)
	{
		const float CX = W - 40.f * S - StarR - (4 - i) * (StarR * 2.4f);
		const float CY = 110.f * S;
		const bool bOn = i < Stars;
		if (bOn && !bFlash)
		{
			for (float K = 0.15f; K <= 1.0f; K += 0.12f)
			{
				Lines(Out, L, G, StarShape(CX, CY, StarR * K), FLinearColor(1.f, 0.85f, 0.2f), 3.f * S);
			}
		}
		Lines(Out, L + 1, G, StarShape(CX, CY, StarR), bOn ? FLinearColor(0.f, 0.f, 0.f, 0.9f) : FLinearColor(1.f, 1.f, 1.f, 0.35f), 2.f * S);
	}
	if (Stars > 0)
	{
		TextR(Out, L, G, bSpotted ? TEXT("TRUY NÃ") : TEXT("ĐANG TRỐN..."), W - 40.f * S, 135.f * S, Font(int32(18 * S)),
			bSpotted ? FLinearColor(1.f, 0.25f, 0.2f) : FLinearColor(0.9f, 0.9f, 0.9f));
	}
	if (CrimeReasonTime > 0.f && !CrimeReason.IsEmpty())
	{
		TextR(Out, L, G, CrimeReason, W - 40.f * S, 165.f * S, Font(int32(16 * S)), FLinearColor(1.f, 0.6f, 0.4f, FMath::Clamp(CrimeReasonTime, 0.f, 1.f)));
	}

	// ---------------- Minimap (bottom left) ----------------
	const float MapSize = 280.f * S;
	const float MX = 30.f * S;
	const float MY = H - 30.f * S - MapSize;
	const float CX = MX + MapSize * 0.5f;
	const float CY = MY + MapSize * 0.58f;
	const float Scale = (MapSize * 0.5f) / MapRange;
	Box(Out, L, G, MX - 4.f * S, MY - 4.f * S, MapSize + 8.f * S, MapSize + 8.f * S, FLinearColor(0.f, 0.f, 0.f, 0.85f));
	Box(Out, L + 1, G, MX, MY, MapSize, MapSize, FLinearColor(0.12f, 0.17f, 0.12f, 0.85f));

	const float YawRad = FMath::DegreesToRadians(ViewYaw);
	const FVector2D Fwd(FMath::Cos(YawRad), FMath::Sin(YawRad));
	const FVector2D Right(-FMath::Sin(YawRad), FMath::Cos(YawRad));
	auto ToMap = [&](const FVector2D& World) -> FVector2f
	{
		const FVector2D D = World - PlayerXY;
		return FVector2f(CX + FVector2D::DotProduct(D, Right) * Scale, CY - FVector2D::DotProduct(D, Fwd) * Scale);
	};

	const FGeometry MapGeo = G.MakeChild(FVector2D(MapSize, MapSize), FSlateLayoutTransform(FVector2D(MX, MY)));
	Out.PushClip(FSlateClippingZone(MapGeo));
	{
		// Only the parts of each road near the player (the route alone is ~3,200 samples).
		const float CullSq = FMath::Square(MapRange * 1.6f);
		{
			TArray<FVector2f> Run;
			for (const FVector2D& P : RiverLine)
			{
				if (FVector2D::DistSquared(P, PlayerXY) <= CullSq * 1.5f)
				{
					Run.Add(ToMap(P));
				}
				else if (Run.Num() > 0)
				{
					Run.Add(ToMap(P));
					Lines(Out, L + 2, G, Run, FLinearColor(0.15f, 0.55f, 0.75f), 9.f * S);
					Run.Reset();
				}
			}
			Lines(Out, L + 2, G, Run, FLinearColor(0.15f, 0.55f, 0.75f), 9.f * S);
		}
		for (int32 R = 0; R < RoadLines.Num(); ++R)
		{
			const float Width = R == 0 ? 1.f : 0.75f;   // QL4C stubs a little thinner than the route
			TArray<FVector2f> Run;
			auto Flush = [&]()
			{
				Lines(Out, L + 2, G, Run, FLinearColor(0.02f, 0.02f, 0.02f), 7.f * S * Width);
				Lines(Out, L + 3, G, Run, FLinearColor(0.85f, 0.82f, 0.75f), 3.5f * S * Width);
				Run.Reset();
			};
			for (const FVector2D& P : RoadLines[R])
			{
				if (FVector2D::DistSquared(P, PlayerXY) <= CullSq)
				{
					Run.Add(ToMap(P));
				}
				else if (Run.Num() > 0)
				{
					Run.Add(ToMap(P));   // finish the segment towards the edge
					Flush();
				}
			}
			Flush();
		}

		for (const FBlip& B : Blips)
		{
			FVector2f P = ToMap(B.World);
			if (B.bClampToEdge)
			{
				P.X = FMath::Clamp(P.X, float(MX + 8.f * S), float(MX + MapSize - 8.f * S));
				P.Y = FMath::Clamp(P.Y, float(MY + 8.f * S), float(MY + MapSize - 8.f * S));
			}
			const float R = B.Size * S;
			if (B.bDiamond)
			{
				const TArray<FVector2f> D = { FVector2f(P.X, P.Y - R), FVector2f(P.X + R, P.Y), FVector2f(P.X, P.Y + R), FVector2f(P.X - R, P.Y), FVector2f(P.X, P.Y - R) };
				Lines(Out, L + 4, G, D, FLinearColor::Black, 5.f * S);
				Lines(Out, L + 5, G, D, B.Color, 3.f * S);
			}
			else if (B.bSquare)
			{
				Box(Out, L + 4, G, P.X - R - 1.f, P.Y - R - 1.f, 2.f * R + 2.f, 2.f * R + 2.f, FLinearColor::Black);
				Box(Out, L + 5, G, P.X - R, P.Y - R, 2.f * R, 2.f * R, B.Color);
			}
			else
			{
				Box(Out, L + 4, G, P.X - R, P.Y - R, 2.f * R, 2.f * R, B.Color);
			}
		}
		// Player arrow (always points up: the map rotates with the camera).
		const float A = 11.f * S;
		TArray<FVector2f> Arrow = { FVector2f(CX, CY - A), FVector2f(CX + A * 0.7f, CY + A * 0.8f), FVector2f(CX, CY + A * 0.4f),
			FVector2f(CX - A * 0.7f, CY + A * 0.8f), FVector2f(CX, CY - A) };
		Lines(Out, L + 6, G, Arrow, FLinearColor::Black, 5.f * S);
		Lines(Out, L + 7, G, Arrow, FLinearColor::White, 2.5f * S);
	}
	Out.PopClip();
	Text(Out, L + 8, G, FString::Printf(TEXT("%.0f m"), AltitudeM), MX + 8.f * S, MY + 6.f * S, Font(int32(14 * S)), FLinearColor(0.75f, 1.f, 0.75f));
	{
		// North marker on the rim of the rotating map (the real map has north = -Y).
		const FVector2f N = ToMap(PlayerXY + FVector2D(0.0, -1.0e7));
		const FVector2f C(CX, CY);
		const FVector2f Dir = (N - C).GetSafeNormal();
		const float Edge = MapSize * 0.5f - 12.f * S;
		const float T = FMath::Min(Dir.X != 0.f ? FMath::Abs((Dir.X > 0.f ? MX + MapSize - 12.f * S - CX : CX - MX - 12.f * S) / Dir.X) : BIG_NUMBER,
			Dir.Y != 0.f ? FMath::Abs((Dir.Y > 0.f ? MY + MapSize - 12.f * S - CY : CY - MY - 12.f * S) / Dir.Y) : BIG_NUMBER);
		const FVector2f At = C + Dir * FMath::Min(T, Edge * 2.f);
		const FSlateFontInfo NF = Font(int32(15 * S));
		const FVector2D NS = Measure(TEXT("B"), NF);   // Bắc
		Text(Out, L + 8, G, TEXT("B"), At.X - NS.X * 0.5f, At.Y - NS.Y * 0.5f, NF, FLinearColor(1.f, 0.35f, 0.3f));
	}

	// ---------------- Speedometer (bottom right) ----------------
	const float SR = 110.f * S;
	const float SX = W - 60.f * S - SR;
	const float SY = H - 50.f * S - SR;
	const float MaxKmh = 140.f;
	auto Dial = [&](float Kmh, float Radius) -> FVector2f
	{
		const float Ang = FMath::DegreesToRadians(225.f - FMath::Clamp(Kmh / MaxKmh, 0.f, 1.f) * 270.f);
		return FVector2f(SX + Radius * FMath::Cos(Ang), SY - Radius * FMath::Sin(Ang));
	};
	TArray<FVector2f> Arc;
	for (float K = 0.f; K <= MaxKmh + 0.1f; K += 4.f)
	{
		Arc.Add(Dial(K, SR));
	}
	Lines(Out, L, G, Arc, FLinearColor(0.f, 0.f, 0.f, 0.6f), 12.f * S);
	TArray<FVector2f> Fill;
	for (float K = 0.f; K <= FMath::Min(SpeedKmh, MaxKmh) + 0.1f; K += 2.f)
	{
		Fill.Add(Dial(K, SR));
	}
	Lines(Out, L + 1, G, Fill, SpeedKmh > 90.f ? FLinearColor(1.f, 0.3f, 0.2f) : FLinearColor(1.f, 0.8f, 0.2f), 7.f * S);
	for (int32 K = 0; K <= int32(MaxKmh); K += 20)
	{
		Lines(Out, L + 2, G, { Dial(float(K), SR - 14.f * S), Dial(float(K), SR + 4.f * S) }, FLinearColor::White, 2.f * S);
	}
	Lines(Out, L + 3, G, { FVector2f(SX, SY), Dial(SpeedKmh, SR - 18.f * S) }, FLinearColor(1.f, 0.2f, 0.15f), 4.f * S);
	const FString SpeedStr = FString::Printf(TEXT("%d"), FMath::RoundToInt(SpeedKmh));
	const FSlateFontInfo SpeedFont = Font(int32(48 * S));
	const FVector2D SpeedSize = Measure(SpeedStr, SpeedFont);
	Text(Out, L + 4, G, SpeedStr, SX - SpeedSize.X * 0.5f, SY + 10.f * S, SpeedFont, FLinearColor::White);
	Text(Out, L + 4, G, TEXT("km/h"), SX - 20.f * S, SY + 70.f * S, Font(int32(14 * S)), FLinearColor(0.85f, 0.85f, 0.85f));
	TextR(Out, L + 4, G, ModeName, W - 50.f * S, SY - SR - 40.f * S, Font(int32(20 * S)), FLinearColor(0.85f, 0.95f, 1.f));
	if (!SurfaceName.IsEmpty())
	{
		TextR(Out, L + 4, G, SurfaceName, W - 50.f * S, SY - SR - 66.f * S, Font(int32(17 * S)), FLinearColor(1.f, 0.8f, 0.35f));
	}

	// ---------------- Location card (GTA style, on arrival) ----------------
	if (CardTime > 0.f)
	{
		const float A = FMath::Clamp(CardTime / 0.8f, 0.f, 1.f) * FMath::Clamp((5.f - CardTime) / 0.5f, 0.f, 1.f);
		TextR(Out, L + 5, G, CardName, W - 70.f * S, H * 0.55f, Font(int32(40 * S)), FLinearColor(1.f, 1.f, 1.f, A));
		TextR(Out, L + 5, G, FString::Printf(TEXT("Độ cao %d m"), CardAltitude), W - 70.f * S, H * 0.55f + 55.f * S, Font(int32(24 * S)), FLinearColor(1.f, 0.82f, 0.25f, A));
	}

	// ---------------- Prompts & banners ----------------
	if (!Prompt.IsEmpty())
	{
		const FSlateFontInfo F = Font(int32(22 * S));
		const FVector2D PS = Measure(Prompt, F);
		Box(Out, L + 5, G, (W - PS.X) * 0.5f - 18.f * S, H * 0.74f - 10.f * S, PS.X + 36.f * S, PS.Y + 20.f * S, FLinearColor(0.f, 0.f, 0.f, 0.6f));
		Text(Out, L + 6, G, Prompt, (W - PS.X) * 0.5f, H * 0.74f, F, FLinearColor::White);
	}
	if (!ViewpointPrompt.IsEmpty())
	{
		Text(Out, L + 5, G, ViewpointPrompt, 40.f * S, 40.f * S, Font(int32(20 * S)), FLinearColor(1.f, 0.82f, 0.25f));
	}
	if (HappyRemaining > 0.f)
	{
		const float Pulse = 0.7f + 0.3f * FMath::Sin(Time * 8.f);
		Text(Out, L + 5, G, FString::Printf(TEXT("RƯỢU NGÔ!  NITRO x2  -  LÁI NGƯỢC  %.0fs"), HappyRemaining), 40.f * S, 80.f * S,
			Font(int32(24 * S)), FLinearColor(1.f, 0.55f * Pulse, 0.1f));
	}
	if (BustedTime > 0.f)
	{
		const FSlateFontInfo F = Font(int32(84 * S));
		const FString Busted = TEXT("BỊ BẮT!");
		const FVector2D BS = Measure(Busted, F);
		Text(Out, L + 7, G, Busted, (W - BS.X) * 0.5f, H * 0.38f, F, FLinearColor(1.f, 0.15f, 0.1f, FMath::Clamp(BustedTime, 0.f, 1.f)));
		const FString Fine = TEXT("-200.000 VNĐ");
		const FSlateFontInfo F2 = Font(int32(34 * S));
		Text(Out, L + 7, G, Fine, (W - Measure(Fine, F2).X) * 0.5f, H * 0.38f + BS.Y, F2, FLinearColor(1.f, 1.f, 1.f, FMath::Clamp(BustedTime, 0.f, 1.f)));
	}
	if (!Objective.IsEmpty())
	{
		const FSlateFontInfo OF = Font(int32(22 * S));
		const FVector2D OS = Measure(Objective, OF);
		// Below the viewpoint prompt row (top left), so a long flycam prompt never runs into it.
		Box(Out, L + 5, G, (W - OS.X) * 0.5f - 16.f * S, 82.f * S, OS.X + 32.f * S, OS.Y + 14.f * S, FLinearColor(0.f, 0.f, 0.f, 0.45f));
		Text(Out, L + 6, G, Objective, (W - OS.X) * 0.5f, 89.f * S, OF, FLinearColor(1.f, 0.85f, 0.3f));
	}
	if (MissionBannerTime > 0.f && !MissionBanner.IsEmpty())
	{
		const float A = FMath::Clamp(MissionBannerTime / 0.6f, 0.f, 1.f);
		const FSlateFontInfo BF = Font(int32(38 * S));
		const FVector2D BS = Measure(MissionBanner, BF);
		Text(Out, L + 7, G, MissionBanner, (W - BS.X) * 0.5f, H * 0.3f, BF, FLinearColor(0.45f, 1.f, 0.45f, A));
	}
	if (FlashTime < 0.4f)
	{
		Box(Out, L + 8, G, 0.f, 0.f, W, H, FLinearColor(1.f, 1.f, 1.f, 0.85f * (1.f - FlashTime / 0.4f)));   // camera shutter
	}
	{
		// Controls for what the player is doing now; was one 9 px line at 720p that nobody could read.
		const FString Help = bOnWater
			? TEXT("W ga  ·  S lùi  ·  A/D lái  ·  SHIFT tăng tốc  ·  R về luồng  ·  chuột / C nhìn  ·  F lên bờ  ·  V flycam")
			: bDriving
			? TEXT("W ga  ·  S phanh  ·  A/D lái  ·  SPACE phanh tay  ·  SHIFT tăng tốc  ·  R về đường  ·  chuột / C nhìn  ·  F xuống xe  ·  V flycam")
			: TEXT("WASD đi  ·  chuột nhìn  ·  SHIFT chạy  ·  SPACE nhảy  ·  F lên xe  ·  V flycam");
		const FSlateFontInfo HF = Font(FMath::Max(int32(17 * S), 11), true, TEXT("Regular"));
		const FVector2D HS = Measure(Help, HF);
		const float HX = MX + MapSize + 30.f * S;
		const float HY = H - 30.f * S - HS.Y;
		Box(Out, L + 5, G, HX - 10.f * S, HY - 5.f * S, HS.X + 20.f * S, HS.Y + 10.f * S, FLinearColor(0.f, 0.f, 0.f, 0.35f));
		Text(Out, L + 6, G, Help, HX, HY, HF, FLinearColor(1.f, 1.f, 1.f, 0.9f));
	}
	return L + 9;
}
