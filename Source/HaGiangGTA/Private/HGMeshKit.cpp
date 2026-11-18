#include "HGMeshKit.h"
#include "HGPaint.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "UObject/Package.h"
#include "Misc/CommandLine.h"

namespace
{
	const FName KitSlot(TEXT("Kit"));

	void Basis(const FVector& D, FVector& U, FVector& V)
	{
		const FVector Ref = FMath::Abs(D.Z) < 0.95f ? FVector::UpVector : FVector::ForwardVector;
		U = FVector::CrossProduct(D, Ref).GetSafeNormal();
		V = FVector::CrossProduct(D, U).GetSafeNormal();
	}
}

int32 FHGMeshKit::AddVert(const FVector& P, const FVector& N)
{
	FVert V;
	V.P = FVector3f(P);
	V.N = FVector3f(N.GetSafeNormal());
	// The static-mesh build stores vertex colours as sRGB bytes (FLinearColor::ToFColor(true)) but the material's
	// VertexColor node returns those bytes as-is, so a linear 0.02 black came out as 0.15 grey and every outfit
	// looked bleached. Pre-decode so the shader sees the linear colour we asked for.
	auto Decode = [](float c) { c = FMath::Clamp(c, 0.f, 1.f); return c <= 0.04045f ? c / 12.92f : FMath::Pow((c + 0.055f) / 1.055f, 2.4f); };
	V.C = FVector4f(Decode(Color.R), Decode(Color.G), Decode(Color.B), 1.f);
	V.UV = FVector2f(Surf, bTint ? 1.f : 0.f);
	return Verts.Add(V);
}

void FHGMeshKit::Tri(int32 A, int32 B, int32 C, const FVector& N)
{
	// UE's face normal is (P2 - P0) x (P1 - P0) (FStaticMeshOperations); flip the winding to face N.
	const FVector3f P0 = Verts[A].P, P1 = Verts[B].P, P2 = Verts[C].P;
	const FVector3f Face = FVector3f::CrossProduct(P2 - P0, P1 - P0);
	if (FVector3f::DotProduct(Face, FVector3f(N)) < 0.f)
	{
		Swap(B, C);
	}
	Indices.Add(A);
	Indices.Add(B);
	Indices.Add(C);
}

void FHGMeshKit::Box(const FVector& Center, const FVector& Size, const FRotator& Rot)
{
	const FQuat Q = Rot.Quaternion();
	const FVector H = Size * 0.5f;
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		for (const float S : { -1.f, 1.f })
		{
			FVector N = FVector::ZeroVector;
			N[Axis] = S;
			const int32 A1 = (Axis + 1) % 3, A2 = (Axis + 2) % 3;
			int32 Idx[4];
			const float Us[4] = { -1.f, 1.f, 1.f, -1.f };
			const float Vs[4] = { -1.f, -1.f, 1.f, 1.f };
			for (int32 k = 0; k < 4; ++k)
			{
				FVector P;
				P[Axis] = S * H[Axis];
				P[A1] = Us[k] * H[A1];
				P[A2] = Vs[k] * H[A2];
				Idx[k] = AddVert(Center + Q.RotateVector(P), Q.RotateVector(N));
			}
			Quad(Idx[0], Idx[1], Idx[2], Idx[3], Q.RotateVector(N));
		}
	}
}

void FHGMeshKit::Tube(const FVector& A, const FVector& B, float RA, float RB, int32 Sides, bool bCaps)
{
	const FVector AB = B - A;
	const float L = AB.Size();
	if (L < KINDA_SMALL_NUMBER)
	{
		return;
	}
	const FVector D = AB / L;
	FVector U, V;
	Basis(D, U, V);
	Sides = FMath::Max(Sides, 3);
	const int32 Base = Verts.Num();
	for (int32 i = 0; i <= Sides; ++i)
	{
		const float Ang = 2.f * PI * i / Sides;
		const FVector Dir = U * FMath::Cos(Ang) + V * FMath::Sin(Ang);
		const FVector N = Dir * L + D * (RA - RB);
		AddVert(A + Dir * RA, N);
		AddVert(B + Dir * RB, N);
	}
	for (int32 i = 0; i < Sides; ++i)
	{
		const int32 a = Base + 2 * i, b = a + 1, c = a + 3, d = a + 2;
		const float Ang = 2.f * PI * (i + 0.5f) / Sides;
		const FVector Out = U * FMath::Cos(Ang) + V * FMath::Sin(Ang);
		Quad(a, b, c, d, Out);
	}
	if (bCaps)
	{
		for (const int32 End : { 0, 1 })
		{
			const FVector N = End ? D : -D;
			const FVector C = End ? B : A;
			const float R = End ? RB : RA;
			if (R < 0.05f)
			{
				continue;
			}
			const int32 Ctr = AddVert(C, N);
			const int32 Ring = Verts.Num();
			for (int32 i = 0; i < Sides; ++i)
			{
				const float Ang = 2.f * PI * i / Sides;
				AddVert(C + (U * FMath::Cos(Ang) + V * FMath::Sin(Ang)) * R, N);
			}
			for (int32 i = 0; i < Sides; ++i)
			{
				Tri(Ctr, Ring + i, Ring + (i + 1) % Sides, N);
			}
		}
	}
}

void FHGMeshKit::Ellipsoid(const FVector& Center, const FVector& Radii, const FRotator& Rot, int32 Seg, int32 Rings, float ZCut)
{
	const FQuat Q = Rot.Quaternion();
	Seg = FMath::Max(Seg, 4);
	Rings = FMath::Max(Rings, 2);
	const float Lat0 = FMath::Asin(FMath::Clamp(ZCut, -1.f, 1.f));
	const float Lat1 = PI * 0.5f;
	const int32 Base = Verts.Num();
	for (int32 r = 0; r <= Rings; ++r)
	{
		const float Lat = FMath::Lerp(Lat0, Lat1, float(r) / Rings);
		for (int32 s = 0; s <= Seg; ++s)
		{
			const float Lon = 2.f * PI * s / Seg;
			const FVector Unit(FMath::Cos(Lat) * FMath::Cos(Lon), FMath::Cos(Lat) * FMath::Sin(Lon), FMath::Sin(Lat));
			const FVector P = Unit * Radii;
			const FVector N(P.X / FMath::Square(Radii.X), P.Y / FMath::Square(Radii.Y), P.Z / FMath::Square(Radii.Z));
			AddVert(Center + Q.RotateVector(P), Q.RotateVector(N));
		}
	}
	for (int32 r = 0; r < Rings; ++r)
	{
		for (int32 s = 0; s < Seg; ++s)
		{
			const int32 a = Base + r * (Seg + 1) + s, b = a + 1, c = a + Seg + 2, d = a + Seg + 1;
			const FVector Out = FVector(Verts[a].P + Verts[c].P) * 0.5f - Center;
			if (r == Rings - 1)
			{
				Tri(a, b, d, Out);   // pole: c == d position
			}
			else
			{
				Quad(a, b, c, d, Out);
			}
		}
	}
}

void FHGMeshKit::Frustum(const FTransform& Xf, const FVector2D& R0, const FVector2D& R1, float H, int32 Sides, bool bCaps, float Pleat)
{
	Sides = FMath::Max(Sides, 3);
	auto Ring = [&](float Ang, const FVector2D& R, int32 i)
	{
		const float k = (Pleat > 0.f && (i & 1)) ? 1.f + Pleat : 1.f;
		return FVector(FMath::Cos(Ang) * R.X * k, FMath::Sin(Ang) * R.Y * k, 0.f);
	};
	if (Pleat > 0.f)
	{
		// Flat-shaded facets: the alternating radius reads as knife pleats.
		for (int32 i = 0; i < Sides; ++i)
		{
			const float A0 = 2.f * PI * i / Sides, A1 = 2.f * PI * (i + 1) / Sides;
			const FVector P0 = Ring(A0, R0, i), P1 = Ring(A1, R0, i + 1);
			const FVector Q0 = Ring(A0, R1, i) + FVector(0, 0, H), Q1 = Ring(A1, R1, i + 1) + FVector(0, 0, H);
			FVector N = FVector::CrossProduct(Q0 - P0, P1 - P0).GetSafeNormal();
			const FVector Mid = (P0 + P1) * 0.5f;
			if (FVector::DotProduct(N, FVector(Mid.X, Mid.Y, 0.f)) < 0.f)
			{
				N = -N;
			}
			const FVector WN = Xf.TransformVectorNoScale(N);
			const int32 a = AddVert(Xf.TransformPosition(P0), WN), b = AddVert(Xf.TransformPosition(P1), WN);
			const int32 c = AddVert(Xf.TransformPosition(Q1), WN), d = AddVert(Xf.TransformPosition(Q0), WN);
			Quad(a, b, c, d, WN);
		}
	}
	else
	{
		const int32 Base = Verts.Num();
		for (int32 i = 0; i <= Sides; ++i)
		{
			const float Ang = 2.f * PI * i / Sides;
			const FVector P = Ring(Ang, R0, 0), Q = Ring(Ang, R1, 0) + FVector(0, 0, H);
			const FVector2D G(FMath::Cos(Ang) / FMath::Max(R0.X + R1.X, 1.0), FMath::Sin(Ang) / FMath::Max(R0.Y + R1.Y, 1.0));
			const FVector2D G2 = G.GetSafeNormal();
			const float Slope = (FVector2D(Q.X, Q.Y).Size() - FVector2D(P.X, P.Y).Size()) / FMath::Max(H, 1.f);
			const FVector N = Xf.TransformVectorNoScale(FVector(G2.X, G2.Y, -Slope));
			AddVert(Xf.TransformPosition(P), N);
			AddVert(Xf.TransformPosition(Q), N);
		}
		for (int32 i = 0; i < Sides; ++i)
		{
			const int32 a = Base + 2 * i, b = a + 1, c = a + 3, d = a + 2;
			const float Ang = 2.f * PI * (i + 0.5f) / Sides;
			Quad(a, b, c, d, Xf.TransformVectorNoScale(FVector(FMath::Cos(Ang), FMath::Sin(Ang), 0.f)));
		}
	}
	if (bCaps)
	{
		for (const int32 End : { 0, 1 })
		{
			const FVector2D R = End ? R1 : R0;
			if (R.X < 0.05f || R.Y < 0.05f)
			{
				continue;
			}
			const FVector N = Xf.TransformVectorNoScale(FVector(0, 0, End ? 1.f : -1.f));
			const float Z = End ? H : 0.f;
			const int32 Ctr = AddVert(Xf.TransformPosition(FVector(0, 0, Z)), N);
			const int32 First = Verts.Num();
			for (int32 i = 0; i < Sides; ++i)
			{
				AddVert(Xf.TransformPosition(Ring(2.f * PI * i / Sides, R, i) + FVector(0, 0, Z)), N);
			}
			for (int32 i = 0; i < Sides; ++i)
			{
				Tri(Ctr, First + i, First + (i + 1) % Sides, N);
			}
		}
	}
}

void FHGMeshKit::Torus(const FTransform& Xf, float MajorR, float MinorR, int32 Seg, int32 Sides)
{
	Seg = FMath::Max(Seg, 6);
	Sides = FMath::Max(Sides, 3);
	const int32 Base = Verts.Num();
	for (int32 j = 0; j <= Seg; ++j)
	{
		const float U = 2.f * PI * j / Seg;
		const FVector Radial(FMath::Cos(U), 0.f, FMath::Sin(U));
		for (int32 k = 0; k <= Sides; ++k)
		{
			const float V = 2.f * PI * k / Sides;
			const FVector Dir = Radial * FMath::Cos(V) + FVector(0.f, FMath::Sin(V), 0.f);
			AddVert(Xf.TransformPosition(Radial * MajorR + Dir * MinorR), Xf.TransformVectorNoScale(Dir));
		}
	}
	for (int32 j = 0; j < Seg; ++j)
	{
		for (int32 k = 0; k < Sides; ++k)
		{
			const int32 a = Base + j * (Sides + 1) + k, b = a + 1, c = a + Sides + 2, d = a + Sides + 1;
			Quad(a, b, c, d, FVector(Verts[a].N + Verts[c].N));
		}
	}
}

UStaticMesh* FHGMeshKit::Build(FName Name, UMaterialInterface* Material) const
{
	FMeshDescription MD;
	FStaticMeshAttributes Attr(MD);
	Attr.Register();
	TVertexAttributesRef<FVector3f> Pos = Attr.GetVertexPositions();
	TVertexInstanceAttributesRef<FVector3f> Nrm = Attr.GetVertexInstanceNormals();
	TVertexInstanceAttributesRef<FVector3f> Tan = Attr.GetVertexInstanceTangents();
	TVertexInstanceAttributesRef<float> Sgn = Attr.GetVertexInstanceBinormalSigns();
	TVertexInstanceAttributesRef<FVector4f> Col = Attr.GetVertexInstanceColors();
	TVertexInstanceAttributesRef<FVector2f> UVs = Attr.GetVertexInstanceUVs();
	UVs.SetNumChannels(1);

	MD.ReserveNewVertices(Verts.Num());
	MD.ReserveNewVertexInstances(Verts.Num());
	MD.ReserveNewTriangles(Indices.Num() / 3);
	MD.ReserveNewEdges(Indices.Num());
	const FPolygonGroupID Group = MD.CreatePolygonGroup();
	Attr.GetPolygonGroupMaterialSlotNames()[Group] = KitSlot;

	TArray<FVertexInstanceID> Inst;
	Inst.Reserve(Verts.Num());
	for (const FVert& V : Verts)
	{
		const FVertexID Id = MD.CreateVertex();
		Pos[Id] = V.P;
		const FVertexInstanceID I = MD.CreateVertexInstance(Id);
		Nrm[I] = V.N;
		const FVector3f Ref = FMath::Abs(V.N.Z) < 0.95f ? FVector3f::UpVector : FVector3f::ForwardVector;
		Tan[I] = FVector3f::CrossProduct(Ref, V.N).GetSafeNormal();
		Sgn[I] = 1.f;
		Col[I] = V.C;
		UVs.Set(I, 0, V.UV);
		Inst.Add(I);
	}
	for (int32 i = 0; i + 2 < Indices.Num(); i += 3)
	{
		MD.CreateTriangle(Group, { Inst[Indices[i]], Inst[Indices[i + 1]], Inst[Indices[i + 2]] });
	}

	UPackage* Pkg = GetTransientPackage();
	UStaticMesh* Mesh = NewObject<UStaticMesh>(Pkg, MakeUniqueObjectName(Pkg, UStaticMesh::StaticClass(), Name), RF_Transient);
	Mesh->GetStaticMaterials().Add(FStaticMaterial(Material, KitSlot, KitSlot));
	UStaticMesh::FBuildMeshDescriptionsParams Params;
	Params.bBuildSimpleCollision = false;
	Params.bFastBuild = true;
	Params.bAllowCpuAccess = false;
	Params.bMarkPackageDirty = false;
	Params.bUseHashAsGuid = true;
	Params.bCommitMeshDescription = false;
	Mesh->BuildFromMeshDescriptions({ &MD }, Params);
	return Mesh;
}

UStaticMesh* FHGMeshKit::Cached(FName Key, TFunctionRef<void(FHGMeshKit&)> Fill)
{
	static TMap<FName, UStaticMesh*> Cache;
	if (UStaticMesh** Found = Cache.Find(Key))
	{
		return *Found;
	}
	FHGMeshKit Kit;
	Fill(Kit);
	UStaticMesh* Mesh = Kit.Build(Key, VertexColorMaterial());
	if (Mesh)
	{
		Mesh->AddToRoot();   // shared by every actor of the archetype for the whole session
		UE_LOG(LogTemp, Log, TEXT("HGMeshKit: built %s (%d triangles)"), *Key.ToString(), Kit.NumTriangles());
	}
	Cache.Add(Key, Mesh);
	return Mesh;
}

UMaterialInterface* FHGMeshKit::VertexColorMaterial()
{
	static TWeakObjectPtr<UMaterialInterface> Cached;
	if (!Cached.IsValid())
	{
		UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/HaGiang/Generated/Materials/M_HG_VertexColor.M_HG_VertexColor"));
		if (!M)
		{
			M = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineDebugMaterials/VertexColorMaterial.VertexColorMaterial"));
		}
		Cached = M;
	}
	return Cached.Get();
}

UMaterialInterface* FHGMeshKit::LampMaterial()
{
	static TWeakObjectPtr<UMaterialInterface> Cached;
	if (!Cached.IsValid())
	{
		Cached = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/HaGiang/Generated/Materials/M_HG_Lamp.M_HG_Lamp"));
	}
	return Cached.Get();
}

UStaticMeshComponent* FHGMeshKit::AddComponent(AActor* Owner, USceneComponent* Parent, UStaticMesh* Mesh, FName Name,
	const FVector& Location, const FRotator& Rotation)
{
	// -hgnokit: no kit people / bikes at all (A/B perf measurements).
	static const bool bNoKit = FParse::Param(FCommandLine::Get(), TEXT("hgnokit"));
	if (!Owner || !Parent || !Mesh || bNoKit)
	{
		return nullptr;
	}
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(Owner, MakeUniqueObjectName(Owner, UStaticMeshComponent::StaticClass(), Name), RF_Transient);
	C->SetupAttachment(Parent);
	C->SetStaticMesh(Mesh);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetGenerateOverlapEvents(false);
	C->SetCanEverAffectNavigation(false);
	C->bAffectDistanceFieldLighting = false;
	C->SetRelativeLocationAndRotation(Location, Rotation);
	C->RegisterComponent();
	Owner->AddInstanceComponent(C);
	return C;
}

UStaticMeshComponent* FHGMeshKit::AddLamp(AActor* Owner, USceneComponent* Parent, FName Name, const FVector& Location,
	const FVector& SizeCm, const FLinearColor& LampColor, float Intensity, bool bSphere)
{
	static TWeakObjectPtr<UStaticMesh> Sphere, Cube;
	TWeakObjectPtr<UStaticMesh>& Shape = bSphere ? Sphere : Cube;
	if (!Shape.IsValid())
	{
		Shape = LoadObject<UStaticMesh>(nullptr, bSphere ? TEXT("/Engine/BasicShapes/Sphere.Sphere") : TEXT("/Engine/BasicShapes/Cube.Cube"));
	}
	UStaticMeshComponent* C = AddComponent(Owner, Parent, Shape.Get(), Name, Location);
	if (!C)
	{
		return nullptr;
	}
	C->SetRelativeScale3D(SizeCm / 100.f);
	C->SetCastShadow(false);
	C->SetAffectDynamicIndirectLighting(false);
	C->bAffectIndirectLightingWhileHidden = false;
	C->SetVisibleInRayTracing(false);
	C->SetCullDistance(6000.f);
	if (UMaterialInterface* Lamp = LampMaterial())
	{
		UMaterialInstanceDynamic* MID = C->CreateDynamicMaterialInstance(0, Lamp);
		MID->SetVectorParameterValue(TEXT("Color"), LampColor);
		MID->SetScalarParameterValue(TEXT("Intensity"), Intensity);
	}
	else
	{
		HGPaint::Paint(C, LampColor);
	}
	return C;
}

void FHGMeshKit::SetLampIntensity(UStaticMeshComponent* Lamp, float Intensity)
{
	if (Lamp)
	{
		if (UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Lamp->GetMaterial(0)))
		{
			MID->SetScalarParameterValue(TEXT("Intensity"), Intensity);
		}
	}
}
