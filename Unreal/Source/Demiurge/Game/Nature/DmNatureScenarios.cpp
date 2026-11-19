// DEMIURGE — Game/Nature/DmNatureScenarios.cpp : nature scenarios (C4 nature).
//
//   shot.ps1 nature-<view> [-site=forest|taiga|tropic|savanna|desert|burnt|lat,lon] [-heading=] [-sunel=] [-sunaz=]
//   views: orbit (700 km), high (120 km), mid (25 km), low (5 km), close (1.2 km), ground (250 m), lineup (every mesh)
//   shot.ps1 nature-check -noshot     regression test (DmCheck lines, exit code 0 = pass)
// The scenario waits for the fields and the site, frames the camera on the VISIBLE ground (DmNatureGround),
// waits until every wanted tile is built plus 90 frames (TSR, Lumen), shoots and exits. With -shot (the
// default of shot.ps1) the harness also shoots at -warm; the settled shot overwrites it.
#include "Game/Nature/DmNatureActor.h"
#include "Game/Nature/DmNatureFactory.h"
#include "Game/Nature/DmNatureMesh.h"
#include "Game/Nature/DmNatureScatter.h"
#include "Game/Core/DmCoords.h"
#include "Game/Core/DmScenario.h"
#include "Game/Core/DmSimSubsystem.h"

#include "Components/SkyAtmosphereComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "EngineUtils.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"
#include "UObject/UObjectIterator.h"
#include "HAL/PlatformTime.h"
#include "Sim/Core/Geo.h"

namespace
{
	ADmNature* FindNature(UWorld* W)
	{
		if (!W) return nullptr;
		for (TActorIterator<ADmNature> It(W); It; ++It) return *It;
		return nullptr;
	}

	struct FView { const TCHAR* Name; double DistKm, Pitch, Fov; const TCHAR* Site; };
	const FView kViews[] = {
		{ TEXT("orbit"), 700.0, 90.0, 30.0, TEXT("forest") },
		{ TEXT("high"), 120.0, 55.0, 40.0, TEXT("forest") },
		{ TEXT("mid"), 25.0, 38.0, 50.0, TEXT("forest") },
		{ TEXT("low"), 5.0, 26.0, 55.0, TEXT("forest") },
		{ TEXT("close"), 1.2, 20.0, 55.0, TEXT("forest") },
		{ TEXT("ground"), 0.25, 9.0, 60.0, TEXT("forest") },
		{ TEXT("lineup"), 0.17, 17.0, 55.0, TEXT("meadow") },
		{ TEXT("taiga"), 1.2, 20.0, 55.0, TEXT("taiga") },
		{ TEXT("tropic"), 0.6, 16.0, 55.0, TEXT("tropic") },
		{ TEXT("savanna"), 0.6, 18.0, 55.0, TEXT("savanna") },
		{ TEXT("herd"), 0.09, 14.0, 50.0, TEXT("herd") },
		{ TEXT("burnt"), 0.8, 22.0, 55.0, TEXT("burnt") },
	};

	/** Best cell for a site kind (score over every land cell). */
	bool FindSite(const FDmNatureFields& F, const FString& Kind, FVector& OutDir)
	{
		{
			TArray<FString> Parts;
			Kind.ParseIntoArray(Parts, TEXT(","));
			if (Parts.Num() == 2) { OutDir = DmCoords::LatLonToDir(FCString::Atod(*Parts[0]), FCString::Atod(*Parts[1])); return true; }
		}
		if (!F.Geo) return false;
		const dm::Geo& G = *F.Geo;
		double Best = -1e9; int32 BestC = -1;
		double MaxLat = 50.0;   // the placeholder sky leaves high southern latitudes dark; -sitemaxlat=90 to lift
		FDmShotArgs::Value(TEXT("-sitemaxlat="), MaxLat);
		for (int32 C = 0; C < F.N; ++C)
		{
			if (F.Elev[C] < F.SeaLevel + 0.15) continue;
			if (FMath::Abs(FMath::RadiansToDegrees(double(G.lat[size_t(C)]))) > MaxLat) continue;
			const float* P = &G.pos[size_t(C) * 3];
			const FVector D = DmCoords::SimToUE(P[0], P[1], P[2]).GetSafeNormal();
			const FVector4f Cn = F.Sample(F.Canopy, D), Cl = F.Sample(F.Climate, D);
			const float Forest = FDmNatureFields::CanopyValue(Cn.X, D);
			const float T = Cl.X * 80.f - 40.f;
			// an edge: open land within two rings
			float MinR = 1.f, MaxR = 0.f;
			const int32 Deg = G.nbrN[size_t(C)];
			for (int32 K = 0; K < Deg; ++K)
			{
				const int32 M = G.nbr[size_t(6 * C + K)];
				MinR = FMath::Min(MinR, F.Canopy.Num() ? F.Sample(F.Canopy, DmCoords::SimToUE(G.pos[size_t(M) * 3], G.pos[size_t(M) * 3 + 1], G.pos[size_t(M) * 3 + 2]).GetSafeNormal()).X : 0.f);
				MaxR = FMath::Max(MaxR, F.Sample(F.Canopy, DmCoords::SimToUE(G.pos[size_t(M) * 3], G.pos[size_t(M) * 3 + 1], G.pos[size_t(M) * 3 + 2]).GetSafeNormal()).X);
			}
			const double Relief = FMath::Min(1.0, (F.Elev[C] - F.SeaLevel) / 2.5);
			double S = -1e9;
			if (Kind == TEXT("forest")) S = Forest * 2.0 + (1.0 - MinR) * 1.2 + Relief * 0.5 - FMath::Abs(T - 11.0) * 0.05 - Cn.Z;
			else if (Kind == TEXT("taiga")) S = Forest * 2.0 + Cn.Y * 1.5 + (1.0 - MinR) * 0.6 - FMath::Abs(T + 1.0) * 0.06;
			else if (Kind == TEXT("tropic")) S = Forest * 1.5 + Cn.Z * 2.0 + (1.0 - Relief) * 0.6;
			else if (Kind == TEXT("savanna")) S = Cn.W * 2.0 + (Cn.X > 0.15f && Cn.X < 0.55f ? 1.0 : 0.0) + F.Fauna[C] * 0.8;
			else if (Kind == TEXT("desert")) S = (1.0 - Cl.Y) * 2.0 + (T > 14.f ? 1.0 : 0.0) - Cn.X * 3.0;
			else if (Kind == TEXT("burnt")) S = Cl.W * 3.0 + Forest;
			else if (Kind == TEXT("herd")) S = F.Fauna[C] * 3.0 + (Cn.X > 0.1f && Cn.X < 0.5f ? 0.8 : 0.0) - FMath::Abs(T - 16.0) * 0.03;
			else if (Kind == TEXT("meadow")) S = (Cn.X > 0.3f && Cn.X < 0.48f ? 1.5 : 0.0) + MaxR * 0.8 - FMath::Abs(T - 12.0) * 0.05 + F.Fauna[C] * 0.3;
			S += 0.001 * DmNature::Hash01(DmNature::Hash2(C, 0, 99));
			if (S > Best) { Best = S; BestC = C; }
		}
		if (BestC < 0) return false;
		const float* P = &G.pos[size_t(BestC) * 3];
		OutDir = DmCoords::SimToUE(P[0], P[1], P[2]).GetSafeNormal();
		return true;
	}

	struct FRun
	{
		int32 Phase = 0;                 // 0 wait fields, 1 framed: wait settle, 2 shot taken
		int32 SettledFrame = -1, ShotFrame = -1;
		double FramedAt = 0, SettleSeconds = 0;
		FVector Site = FVector::ZeroVector;
		FView View;
		double DistKm = 0, Heading = 0, Pitch = 0, Fov = 0;
	};
	FRun GRun;

	void Frame(FDmScenarioContext& Ctx, ADmNature* N, FRun& R)
	{
		const FDmNatureGroundPtr G = N->GetGround();
		const FTransform P2W = Ctx.Sim->GetPlanetToWorld();
		const FVector Up = R.Site;
		const double Rg = G ? G->Radius(Up) : DmCoords::RadiusUU;
		const FVector Target = Up * Rg;
		FVector East, North;
		DmCoords::Basis(Up, East, North);
		const double H = FMath::DegreesToRadians(R.Heading), Pt = FMath::DegreesToRadians(FMath::Clamp(R.Pitch, -89.9, 90.0));
		const FVector Horizontal = North * FMath::Cos(H) + East * FMath::Sin(H);
		const FVector Fwd = (Horizontal * FMath::Cos(Pt) - Up * FMath::Sin(Pt)).GetSafeNormal();
		FVector Loc = Target - Fwd * R.DistKm * DmCoords::UUPerKm;
		// never below the visible ground (a hill between the camera and the target)
		if (G)
		{
			const FVector Cd = Loc.GetSafeNormal();
			const double Min = G->Radius(Cd) + 25.0 * 100.0;
			if (Loc.Size() < Min) Loc = Cd * Min;
		}
		const FVector CamUp = (FMath::Abs(R.Pitch) > 89.0) ? Horizontal : Up;
		const FVector F2 = (Target - Loc).GetSafeNormal();
		const FRotator Rot = FRotationMatrix::MakeFromXZ(P2W.TransformVectorNoScale(F2), P2W.TransformVectorNoScale(CamUp)).Rotator();
		Ctx.SetView(P2W.TransformPosition(Loc), Rot, R.Fov);
		// sun: by default the bridge's orbit rule (subsolar at lat/2 + 8, lon + 55: mid-morning light from the
		// west); -sunel= / -sunaz= (degrees, azimuth clockwise from north) place it relative to the site instead.
		// Note: with the placeholder planet some sun directions shadow all low land (it turns ocean-blue);
		// that is the placeholder's ocean shell, not nature (see the C4 report).
		double SunEl = -1.0, SunAz = R.Heading - 125.0;
		const bool bRel = FDmShotArgs::Value(TEXT("-sunel="), SunEl) | FDmShotArgs::Value(TEXT("-sunaz="), SunAz);
		if (!FDmShotArgs::Get().bSun && !bRel)
		{
			double La0, Lo0;
			DmCoords::DirToLatLon(Up, La0, Lo0);
			Ctx.SetSubsolar(FMath::Clamp(La0 * 0.5 + 8.0, -23.0, 23.0), Lo0 + 55.0);
		}
		if (SunEl < 0.0) SunEl = 32.0;
		if (!FDmShotArgs::Get().bSun && bRel)
		{
			const double Az = FMath::DegreesToRadians(SunAz);
			const FVector SunH = North * FMath::Cos(Az) + East * FMath::Sin(Az);
			const FVector Sub = (Up * FMath::Sin(FMath::DegreesToRadians(SunEl)) + SunH * FMath::Cos(FMath::DegreesToRadians(SunEl))).GetSafeNormal();
			double La, Lo;
			DmCoords::DirToLatLon(Sub, La, Lo);
			Ctx.SetSubsolar(La, Lo);
		}
		// judging the vegetation needs the lab's aerial perspective scale below ~30 km (the scaled atmosphere hazes
		// ground views heavily, lab pitfall 11): scenario-only, on whatever sky atmosphere the sky part spawned
		double Ap = R.DistKm < 30.0 ? 0.4 : 1.0;
		FDmShotArgs::Value(TEXT("-apscale="), Ap);
		// low views sit inside the scaled cloud layer (bottom ~0.5 km): hide the clouds unless -keepclouds
		if (R.DistKm < 3.0 && !FDmShotArgs::Param(TEXT("keepclouds")))
			for (TObjectIterator<UVolumetricCloudComponent> It; It; ++It)
				if (It->GetWorld() == Ctx.World) It->SetVisibility(false);
		for (TObjectIterator<USkyAtmosphereComponent> It; It; ++It)
			if (It->GetWorld() == Ctx.World) It->SetAerialPespectiveViewDistanceScale(float(Ap));
		double La, Lo;
		DmCoords::DirToLatLon(Up, La, Lo);
		const double SeaM = N->GetFields() ? (N->GetFields()->SeaRadiusUU - DmCoords::RadiusUU) / 100.0 : 0.0;
		Ctx.Log(FString::Printf(TEXT("DmNatureShot: sea %.0f m, camera %.0f m above datum"), SeaM, (Loc.Size() - DmCoords::RadiusUU) / 100.0));
		Ctx.Log(FString::Printf(TEXT("DmNatureShot: site lat %.3f lon %.3f, ground %.0f m above datum, view %s %.2f km heading %.0f pitch %.0f fov %.0f (sun el %.0f az %.0f)"),
			La, Lo, (Rg - DmCoords::RadiusUU) / 100.0, R.View.Name, R.DistKm, R.Heading, R.Pitch, R.Fov, SunEl, SunAz));
	}

	void Setup(FDmScenarioContext& Ctx, const FView& V)
	{
		GRun = FRun();
		GRun.View = V;
		GRun.DistKm = V.DistKm; GRun.Pitch = V.Pitch; GRun.Fov = V.Fov;
		GRun.Heading = 35.0;
		FDmShotArgs::Value(TEXT("-heading="), GRun.Heading);
		FDmShotArgs::Value(TEXT("-dist="), GRun.DistKm);
		FDmShotArgs::Value(TEXT("-pitch="), GRun.Pitch);
		FDmShotArgs::Value(TEXT("-fov="), GRun.Fov);
		Ctx.BuildScene();
		Ctx.HoldExit();
		// a provisional view until the site is known
		Ctx.LookAt(20.0, 30.0, 700.0, 0.0, 90.0, 30.0);
	}

	void Tick(FDmScenarioContext& Ctx, float)
	{
		FRun& R = GRun;
		ADmNature* N = FindNature(Ctx.World);
		if (!N) { if (Ctx.Frame == 30) { Ctx.Log(TEXT("DmNatureShot: no nature actor")); Ctx.Shot(TEXT("")); Ctx.Finish(); } return; }
		if (R.Phase == 0)
		{
			if (!N->HasFields() || !N->GetGround()) return;
			FString Site = R.View.Site;
			FDmShotArgs::Value(TEXT("-site="), Site);
			if (!FindSite(*N->GetFields(), Site, R.Site)) R.Site = DmCoords::LatLonToDir(20.0, 30.0);
			if (FString(R.View.Name) == TEXT("lineup")) N->BuildLineup(R.Site);
			Frame(Ctx, N, R);
			R.Phase = 1;
			R.FramedAt = FPlatformTime::Seconds();
			return;
		}
		if (R.Phase == 1)
		{
			if (Ctx.Frame % 30 == 0) Frame(Ctx, N, R);   // re-frame (the ground may have been refined)
			const bool bSettled = N->IsSettled();
			if (bSettled && R.SettledFrame < 0) { R.SettledFrame = Ctx.Frame; R.SettleSeconds = FPlatformTime::Seconds() - R.FramedAt; }
			if (!bSettled) R.SettledFrame = -1;
			const bool bTimeout = Ctx.Frame > 2400;
			if ((R.SettledFrame >= 0 && Ctx.Frame - R.SettledFrame >= 90 && Ctx.Frame >= 240) || bTimeout)
			{
				Ctx.Log(FString::Printf(TEXT("DmNatureShot: %s after %.1f s%s: %s"), R.View.Name, R.SettleSeconds, bTimeout ? TEXT(" (TIMEOUT)") : TEXT(""), *N->Describe()));
				Ctx.Shot(TEXT(""));
				// -natureprofile: dump a GPU profile of this frame into the log (judging the budget per pass)
				if (FDmShotArgs::Param(TEXT("natureprofile")) && GEngine) GEngine->Exec(Ctx.World, TEXT("ProfileGPU"));
				R.ShotFrame = Ctx.Frame;
				R.Phase = 2;
			}
			return;
		}
		if (R.Phase == 2 && Ctx.Frame > R.ShotFrame + 20) Ctx.Finish();
	}

	struct FRegs
	{
		TArray<TUniquePtr<FDmScenarioRegistrar>> R;
		FRegs()
		{
			for (const FView& V : kViews)
			{
				const FString Name = FString(TEXT("nature-")) + V.Name;
				const FView Vc = V;
				R.Add(MakeUnique<FDmScenarioRegistrar>(*Name, TEXT("nature"), TEXT("nature view: forests, trees, wildlife (see DmNatureScenarios.cpp)"),
					[Vc](FDmScenarioContext& Ctx) { Setup(Ctx, Vc); },
					[](FDmScenarioContext& Ctx, float Dt) { Tick(Ctx, Dt); }));
			}
		}
	};
	FRegs GRegs;

	// ------------------------------------------------------------ regression test
	struct FCheck
	{
		int32 Phase = 0, Fails = 0;
		double T0 = 0;
		FVector Site = FVector::ZeroVector;
	};
	FCheck GCheck;

	void Expect(FDmScenarioContext& Ctx, bool bOk, const FString& What)
	{
		Ctx.Log(FString::Printf(TEXT("DmCheck %s %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What));
		if (!bOk) ++GCheck.Fails;
	}
}

static FDmScenarioRegistrar GDmNatureCheck(TEXT("nature-check"), TEXT("nature"),
	TEXT("nature regression test: factory, fields, ground, deterministic tiles, streaming at a forest (shot.ps1 nature-check -noshot)"),
	[](FDmScenarioContext& Ctx)
	{
		GCheck = FCheck();
		Ctx.BuildScene();
		Ctx.HoldExit();
		Ctx.LookAt(20.0, 30.0, 700.0, 0.0, 90.0, 30.0);
	},
	[](FDmScenarioContext& Ctx, float)
	{
		FCheck& C = GCheck;
		ADmNature* N = FindNature(Ctx.World);
		if (C.Phase == 0)
		{
			if (!N) { if (Ctx.Frame > 60) { Expect(Ctx, false, TEXT("nature actor spawned (scene part 'nature')")); C.Phase = 9; } return; }
			if (!N->HasFields() || !N->GetGround()) { if (Ctx.Frame > 1200) { Expect(Ctx, false, TEXT("fields and ground within 1200 frames")); C.Phase = 9; } return; }
			UDmNatureFactory* Fa = N->GetFactory();
			Expect(Ctx, Fa && Fa->IsBuilt(), TEXT("factory built"));
			Expect(Ctx, Fa && !Fa->MaterialsMissing(), TEXT("every /Game/Nature material loaded"));
			bool bMeshes = true;
			for (int32 I = 0; I < int32(EDmNatureMesh::Num); ++I)
				for (int32 V = 0; V < UDmNatureFactory::NumVariants(EDmNatureMesh(I)); ++V)
					bMeshes &= Fa && Fa->Get(EDmNatureMesh(I), V).Mesh != nullptr;
			Expect(Ctx, bMeshes, TEXT("every mesh id and variant has a mesh"));
			{
				// far LODs carry the camera-facing clump-card data (UV2..UV4) and the trees have 4 LODs
				bool bFar = true;
				for (EDmNatureMesh Id : { EDmNatureMesh::Conifer, EDmNatureMesh::Broadleaf, EDmNatureMesh::Shrub })
				{
					const UStaticMesh* Sm = Fa ? Fa->Get(Id, 0).Mesh : nullptr;
					const FStaticMeshRenderData* Rd = Sm ? Sm->GetRenderData() : nullptr;
					bFar &= Rd && Rd->LODResources.Num() == 4 && Rd->LODResources[3].GetNumTexCoords() >= 5;
				}
				Expect(Ctx, bFar, TEXT("trees have 4 LODs with camera-facing far-LOD card data (5 UV channels)"));
			}
			Expect(Ctx, Fa && Fa->BuildSeconds < 3.0, FString::Printf(TEXT("factory build %.2f s < 3 s"), Fa ? Fa->BuildSeconds : -1.0));
			const FDmNatureFieldsPtr F = N->GetFields();
			const FDmNatureGroundPtr G = N->GetGround();
			Expect(Ctx, F->BuildMs < 3000.0, FString::Printf(TEXT("fields baked in %.0f ms < 3000"), F->BuildMs));
			Ctx.Log(FString::Printf(TEXT("DmCheck INFO ground source %s"), G->SourceName()));
			// forest share of the land
			int32 Land = 0, Forest = 0;
			for (int32 Ci = 0; Ci < F->N; ++Ci)
			{
				if (F->Elev[Ci] < F->SeaLevel) continue;
				++Land;
				const float* P = &F->Geo->pos[size_t(Ci) * 3];
				if (DmNatureScatter::ForestAt(*F, DmCoords::SimToUE(P[0], P[1], P[2]).GetSafeNormal()) > 0.5f) ++Forest;
			}
			Expect(Ctx, Land > 0 && Forest > Land / 20, FString::Printf(TEXT("forest on %d of %d land cells"), Forest, Land));
			// ground matches the visible surface within the sim's range
			if (!FindSite(*F, TEXT("forest"), C.Site)) C.Site = DmCoords::LatLonToDir(20.0, 30.0);
			const double Rg = G->Radius(C.Site);
			Expect(Ctx, FMath::Abs(Rg - DmCoords::RadiusUU) < 25.0 * DmCoords::UUPerElev, FString::Printf(TEXT("ground radius %.0f m above datum is sane"), (Rg - DmCoords::RadiusUU) / 100.0));
			// determinism: the same tile twice, and a thinned tile is a subset
			const FDmTileKey K = FDmTileKey::At(EDmNatureLayer::Trees, C.Site);
			FDmScatterSettings S1; FDmScatterSettings S2; S2.Keep = 0.25f; S2.bShrubs = false;
			const double Tb = FPlatformTime::Seconds();
			TSharedPtr<FDmTileResult, ESPMode::ThreadSafe> A = DmNatureScatter::BuildTile(*F, *G, K, S1);
			const double TileMs = (FPlatformTime::Seconds() - Tb) * 1000.0;
			TSharedPtr<FDmTileResult, ESPMode::ThreadSafe> B = DmNatureScatter::BuildTile(*F, *G, K, S1);
			TSharedPtr<FDmTileResult, ESPMode::ThreadSafe> Th = DmNatureScatter::BuildTile(*F, *G, K, S2);
			bool bSame = A->Instances == B->Instances && A->Batches.Num() == B->Batches.Num();
			for (int32 I = 0; bSame && I < A->Batches.Num(); ++I)
				bSame &= A->Batches[I].Xf.Num() == B->Batches[I].Xf.Num() && (A->Batches[I].Xf.Num() == 0 || A->Batches[I].Xf[0].Equals(B->Batches[I].Xf[0], 0.01));
			Expect(Ctx, bSame && A->Instances > 0, FString::Printf(TEXT("forest tile is deterministic (%d instances, %d candidates, %.0f ms)"), A->Instances, A->Candidates, TileMs));
			const int32 TreesA = A->Instances - A->PerKind[int32(EDmNatureMesh::Shrub)];
			Expect(Ctx, Th->Instances <= TreesA && Th->Instances > TreesA / 8, FString::Printf(TEXT("thinned tile keeps %d of %d trees (~1/4)"), Th->Instances, TreesA));
			// no tree under the sea or floating: every instance sits within 1 m of the ground
			int32 Bad = 0, Tested = 0;
			for (const FDmTileBatch& Bt : A->Batches)
				for (int32 I = 0; I < Bt.Xf.Num(); I += 97)
				{
					const FVector P = Bt.Xf[I].GetLocation() + A->Center;
					const double Gr = G->Radius(P.GetSafeNormal());
					++Tested;
					if (FMath::Abs(P.Size() - Gr) > 100.0 || Gr < F->SeaRadiusUU) ++Bad;
				}
			Expect(Ctx, Tested > 0 && Bad == 0, FString::Printf(TEXT("instances on the ground and above the sea (%d of %d off)"), Bad, Tested));
			// stream a forest at 1.2 km
			double La, Lo;
			DmCoords::DirToLatLon(C.Site, La, Lo);
			Ctx.LookAt(La, Lo, 1.2, 35.0, 22.0, 55.0);
			C.T0 = FPlatformTime::Seconds();
			C.Phase = 1;
			return;
		}
		if (C.Phase == 1)
		{
			const double Dt = FPlatformTime::Seconds() - C.T0;
			if (N->IsSettled() && Dt > 1.0)
			{
				Expect(Ctx, true, FString::Printf(TEXT("streaming settled in %.1f s"), Dt));
				const int32 Trees = N->CountInstances(EDmNatureMesh::Conifer) + N->CountInstances(EDmNatureMesh::Broadleaf) + N->CountInstances(EDmNatureMesh::Palm) + N->CountInstances(EDmNatureMesh::Acacia);
				Expect(Ctx, Trees > 10000, FString::Printf(TEXT("%d trees around the forest view"), Trees));
				Expect(Ctx, N->NumComponents() < 1500, FString::Printf(TEXT("%d components < 1500"), N->NumComponents()));
				Ctx.Log(FString::Printf(TEXT("DmCheck INFO %s"), *N->Describe()));
				// fly to orbit: every tile is released
				Ctx.LookAt(20.0, 30.0, 700.0, 0.0, 90.0, 30.0);
				C.T0 = FPlatformTime::Seconds();
				C.Phase = 2;
			}
			else if (Dt > 60.0) { Expect(Ctx, false, TEXT("streaming settled within 60 s")); C.Phase = 9; }
			return;
		}
		if (C.Phase == 2)
		{
			if (FPlatformTime::Seconds() - C.T0 > 4.0)
			{
				int32 Inst = 0;
				for (int32 I = 0; I < int32(EDmNatureMesh::Num); ++I) Inst += N->CountInstances(EDmNatureMesh(I));
				Expect(Ctx, Inst == 0 && N->NumComponents() == 0, FString::Printf(TEXT("orbit view releases every tile (%d instances, %d components left)"), Inst, N->NumComponents()));
				C.Phase = 9;
			}
			return;
		}
		if (C.Phase == 9)
		{
			Ctx.Log(FString::Printf(TEXT("DmCheck RESULT %s (%d failed)"), C.Fails ? TEXT("FAIL") : TEXT("PASS"), C.Fails));
			Ctx.SetExitCode(C.Fails ? 1 : 0);
			Ctx.Finish();
			C.Phase = 10;
		}
	});
