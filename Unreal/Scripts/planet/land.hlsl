// ---- DEMIURGE land material (Custom node body). Owner: C1 planet. Port of UnrealLab/SurfaceLab land.hlsl
// plus the per-cell state maps (DmPlanetMaps.h): farms, towns, roads, night lights, scorch, lava, fire,
// fallout, glaciers, the overlays and the molten genesis crust.
//
// Vertex data (DmPlanet.cpp BuildPatch): U0 = (beach zone, relief above base m), U1 = (RAe, RAr),
// U2 = (log2 vertex spacing, m above the nearest lake), U3 = (signed river distance m, river half-width m),
// VC = debug colour. Everything is computed in the planet frame (Unreal axes, +Z north), which turns with
// the planet: P = R^T (world - centre).

// Gradient (per metre, tangent) of the relief octaves the mesh does not carry (DmPlanetNoise::Relief).
float3 ReliefGrad(float3 up, float S, float fp, float RAe, float RAr)
{
	float3 p = up * (100000.0 / 6000.0);
	float3 d = 0, GE = 0, GR = 0;
	float3 q0 = float3(1, 0, 0), q1 = float3(0, 1, 0), q2 = float3(0, 0, 1);
	float b = 1.0, L = 6000.0, wr = 1.0;
	for (int i = 0; i < 10; i++)
	{
		float aa = AAW(L, fp);
		if (aa <= 0.0) break;
		float4 n = Noised(p);
		float3 g = float3(dot(q0, n.yzw), dot(q1, n.yzw), dot(q2, n.yzw));
		g -= up * dot(g, up);
		d += g;
		float att = 1.0 / (1.0 + dot(d, d));
		float k = (1.0 - MeshW(L, S)) * aa * b / L;
		GE += k * att * g;
		if (i < 4)
		{
			float sq = sqrt(n.x * n.x + 0.003);
			float an = sq * 1.5;
			float rr = 1.0 - min(an, 1.0);
			GR += k * wr * 2.0 * rr * (an < 1.0 ? -1.5 * n.x / sq : 0.0) * g;
			wr = saturate(rr * rr * 1.5);
		}
		b *= 0.5; L *= 0.5;
		p = Rot(p) * 2.0; q0 = Rot(q0); q1 = Rot(q1); q2 = Rot(q2);
	}
	return RAe * GE + RAr * GR;
}
// Same for the fine fractal; octaves 5 and 6 (0.5 m, 0.25 m) exist only in the material.
float3 FineGrad(float3 up, float S, float fp)
{
	float3 p = up * (100000.0 / 16.0) + float3(17.31, 5.13, 9.71);
	float3 G = 0;
	float3 q0 = float3(1, 0, 0), q1 = float3(0, 1, 0), q2 = float3(0, 0, 1);
	float b = 1.0, L = 16.0;
	for (int i = 0; i < 7; i++)
	{
		float aa = AAW(L, fp);
		if (aa <= 0.0) break;
		float4 n = Noised(p);
		float3 g = float3(dot(q0, n.yzw), dot(q1, n.yzw), dot(q2, n.yzw));
		g -= up * dot(g, up);
		float mw = i < 5 ? MeshW(L, S) : 0.0;
		G += (1.0 - mw) * aa * b * g / L;
		b *= 0.5; L *= 0.5;
		p = Rot(p) * 2.0; q0 = Rot(q0); q1 = Rot(q1); q2 = Rot(q2);
	}
	return G;
}
// Tree crowns: (crown height 0..1, cell random, dist to centre)
float3 Crowns(float3 Pm, float cell, out float3 off)
{
	float r2;
	float3 c = Cells(Pm / cell, int3(311, 17, 5), off, r2);
	return float3(1.0 - Smooth01(c.x / 0.75), c.z, c.x);
}

// 0..1 from any bits of h (re-mixed first, ground lab: shifted hashes leave few low bits)
float Rand01(uint h) { h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu; h ^= h >> 16; return float(h & 0xFFFFu) / 65535.0; }

float3 Land(float3 Pw, float3 NvW, float3 VC, float VCA, float2 U0, float2 U1, float2 U2, float2 U3, float Tm, float3 SunW,
	float3 RX, float3 RY, float3 RZ, float3 Ctr4, float SeaR, float Heat, float OverOn, float Dbg, float Age, float SeaRise,
	Texture2D MapA, SamplerState MapASampler, Texture2D MapB, SamplerState MapBSampler, Texture2D MapC, SamplerState MapCSampler,
	Texture2D MapD, SamplerState MapDSampler, Texture2D MapE, SamplerState MapESampler, Texture2D RiverTex, SamplerState RiverTexSampler,
	out float Rough, out float3 Nw, out float3 Emis, out float AO)
{
	float3 P = ToPlanet(Pw - Ctr4, RX, RY, RZ);              // planet frame, cm
	float3 Nv = ToPlanet(NvW, RX, RY, RZ);
	float3 SunD = ToPlanet(SunW, RX, RY, RZ);
	float3 up = normalize(P);
	float h = (length(P) - SeaR) * 0.01;                         // m above sea
	float fp = max(length(ddx(P)), length(ddy(P))) * 0.01 + 0.02; // pixel footprint, m
	float3 Pm = P * 0.01;
	float S = exp2(U2.x);
	float beachZone = U0.x, dH = U0.y;
	float hLake = U2.y;
	float rivD = U3.x, rivW = U3.y;

	// ---- per-cell state (planet maps, ~346 m texels, sampled at the footprint's mip)
	float2 ouv = OctUV(up);
	float lodS = log2(max(fp / 346.0, 1.0));
	float4 mA = MapA.SampleLevel(MapASampler, ouv, lodS);
	float4 mB = MapB.SampleLevel(MapBSampler, ouv, lodS);
	float4 mC = MapC.SampleLevel(MapCSampler, ouv, lodS);
	float4 mD = MapD.SampleLevel(MapDSampler, ouv, lodS);
	float3 macro = pow(saturate(mA.rgb), 2.2);
	float forest = mA.a;
	float tempC = mB.r * 90.0 - 50.0;
	float wetC = mB.g, arid = mB.b, iceC = mB.a;
	float farm = mC.r, urban = mC.g, lights = mC.a;
	float scorch = mD.r, lava = mD.g, fire = mD.b, rad = mD.a;
	// snow: temperature with an exaggerated lapse over the relief the mesh adds, plus glaciers
	float snowC = saturate((-7.0 - (tempC - 0.012 * dH)) / 7.0);
	// glaciers and ice sheets: the cell field with a ragged, noise-broken edge (no triangle outlines)
	float iceE = iceC + 0.22 * (0.6 * NoiseAA(Pm, 2600.0, fp * 0.3, float3(4.4, 1.1, 8.8)) + 0.5 * NoiseAA(Pm, 700.0, fp * 0.3, float3(7.2, 3.9, 0.6)) + 0.3 * NoiseAA(Pm, 180.0, fp, float3(1.7, 9.2, 3.3)));
	snowC = max(snowC, Smooth01((iceE - 0.3) / 0.3));
	float hW = min(h, hLake);                                    // m above the nearest water surface

	float3 n0 = normalize(Nv);
	float RAe = U1.x, RAr = U1.y;
	float FA = 0.3 + 1.5 * RAr / (RAe + RAr + 1.0);              // DmPlanetNoise::FineAmp
	float mtn = RAr / (RAe + RAr + 1.0);
	float3 nMid = normalize(n0 - ReliefGrad(up, S, fp, RAe, RAr));
	float3 gF = FineGrad(up, S, fp) * FA;
	float3 N = normalize(nMid - gF);
	float cosMid = dot(nMid, up);
	float steepMid = 1.0 - cosMid;

	// broad colour noise (each averaged out when the pixel is larger than the feature)
	float nA = NoiseAA(Pm, 180.0, fp, float3(1.7, 9.2, 3.3));
	float nB = NoiseAA(Pm, 45.0, fp, float3(5.1, 2.4, 7.9));
	float nC = NoiseAA(Pm, 11.0, fp, float3(8.3, 4.6, 1.2));
	float nD = NoiseAA(Pm, 2.7, fp, float3(2.2, 6.1, 9.4));
	float nE = NoiseAA(Pm, 2600.0, fp, float3(4.4, 1.1, 8.8));
	float nF = NoiseAA(Pm, 700.0, fp, float3(7.2, 3.9, 0.6));

	// (wave 3) burn scar masks. The scorch map is a smooth field over the sim's cells (DmPlanetMaps: smoothed, read
	// at a domain-warped direction); the scar is that field thresholded by noise at every scale from 2.6 km down, and
	// the noise scales the threshold (never adds scorch where there is none), so its edge runs in fingers and leaves
	// unburnt islands instead of tracing the hexagon of the cells. Core (char and ash), burnt ground, singed fringe.
	float sN = 0.24 * nE + 0.30 * nF + 0.24 * nA + 0.16 * nB + 0.08 * nC;
	// (verify) the full multi-scale noise belongs at the edge; inside a strongly scorched area it drew a dense leopard
	// pattern of 200-500 m char and ash spots over the whole scar at 15-60 km (the fixture eruption's cells average
	// 0.47): a third of it inside, and the ash core follows the broad field (near the vent) with wavy edges only
	float sEdge = 1.0 - Smooth01((scorch - 0.28) / 0.30);
	float scK = scorch * (1.6 + 1.5 * sN * lerp(0.35, 1.0, sEdge));
	float burnt = Smooth01((scK - 0.34) / 0.20);
	float scC = scorch * (1.6 + 0.9 * (0.55 * nE + 0.45 * nF)) + 0.12 * nA;
	float charred = Smooth01((scC - 0.92) / 0.30);
	float singe = Smooth01((scK - 0.10) / 0.22) * (1.0 - burnt);

	// people and damage push the forest back
	forest *= (1.0 - saturate(farm * 1.6)) * (1.0 - saturate(urban * 2.0)) * (1.0 - burnt) * (1.0 - 0.6 * singe) * (1.0 - saturate(lava * 3.0));

	// ---- vegetation / soil base, with variation at every scale down to the pixel (orbit included).
	// Every mask here is broad and soft: hard thresholds on noise read as camouflage blotches.
	// Terrain drives it first: hollows (relief below the smooth base) hold water and are greener and darker,
	// crests and convex ground are drier and paler; then broad patches (meadow / scrub / bare) on top.
	float relA = max(RAe + RAr, 25.0);
	float crest = saturate(0.5 + 0.9 * dH / relA);                 // 0 hollow .. 1 crest
	float3 col = macro * (1.0 + 0.14 * nE + 0.10 * nF);
	float3 dry = macro * float3(1.18, 1.08, 0.82) + float3(0.012, 0.009, 0.0);
	float3 lushC = macro * float3(0.80, 0.92, 0.72);
	float patch = saturate(0.5 + 0.55 * nF + 0.45 * nA + 0.25 * nB);  // soft, multi-scale
	col = lerp(col, lushC, saturate(0.65 - crest) * 0.55 * (1.0 - arid * 0.5));
	col = lerp(col, dry, saturate(crest - 0.45 + 0.6 * (patch - 0.5)) * 0.5 * (1.0 - forest));
	col *= 1.0 + 0.12 * nB + 0.09 * nC + 0.07 * nD;
	float3 soil = lerp(float3(0.13, 0.095, 0.065), macro * float3(1.05, 0.9, 0.8), arid);
	float soilM = Smooth01((0.55 * (nB + 0.6 * nC) + arid * 0.35 + steepMid * 1.5 - 0.35) / 0.5) * (1.0 - forest) * 0.35;
	col = lerp(col, soil, soilM);
	float rough = 0.88;

	// ---- dunes in sand seas only (very dry, flat, low ground; never on mountain flanks): long sinuous crests
	// with a gentle windward slope and a steeper lee face, the profile smooth at the crest and the trough so
	// the faces never read as blobs; ripples close up (below)
	if (arid > 0.7 && fp < 60.0)
	{
		float3 dd = normalize(cross(up, float3(0.31, 0.83, 0.46)));
		float ph = dot(Pm, dd) / 140.0 + 0.35 * Noised(Pm / 900.0 + 3.1).x + 0.12 * Noised(Pm / 260.0).x;
		float k = AAW(140.0, fp) * Smooth01((arid - 0.7) / 0.2) * Smooth01((h + 0.5) / 1.0) * (1.0 - Smooth01((steepMid - 0.01) / 0.05)) * (1.0 - saturate(mtn * 3.0)) * (1.0 - forest);
		float s = frac(ph);
		// asymmetric wave (windward 0..0.75, lee 0.75..1) with rounded joins: slope = d profile / d phase
		float sl = s < 0.75 ? 1.0 / 0.75 : -1.0 / 0.25;
		float join = Smooth01(min(abs(s - 0.75), min(s, 1.0 - s)) / 0.06);
		float slope = sl * lerp(0.25, 1.0, join);
		k *= saturate(0.4 + 0.8 * Noised(Pm / 1800.0 + 7.7).x + 0.4);   // dune fields and flat sand sheets
		N = normalize(N - dd * (0.07 * slope * k));
		col *= 1.0 + 0.05 * k * (s < 0.75 ? 1.0 : -1.2);
	}

	// ---- land use frame: "estates" of ~1.6 km (3D cells), each with its own field grid orientation and
	// field size, so farmland reads as fields in holdings of different orientation, and towns get a street grid. The grid is planar in the estate's tangent frame (fixed per estate, so it does not
	// collapse on the sphere). (wave 3) The estate lookup is warped by the broad colour noises, so estate
	// borders wander like lanes and streams instead of running as ruler-straight Voronoi edges across the land.
	float farmM = 0.0, woodlot = 0.0;
	float2 lu = 0; float luW = 1, luH = 1; float eR = 0, eR2 = 0, eEdge = 1; float3 eAx = 0, eAy = 0;
	if (farm > 0.02 || urban > 0.03)
	{
		float3 offE; float r2e;
		float3 Pe = Pm + float3(nF + 0.5 * nA, nE + 0.5 * nF, nA + 0.5 * nE) * 170.0;
		float3 ec = Cells(Pe / 1600.0, int3(51, 7, 83), offE, r2e);
		float3 ctr = Pe - offE * 1600.0;
		float3 cu = normalize(ctr);
		float3 t1 = normalize(cross(cu, abs(cu.z) < 0.9 ? float3(0, 0, 1) : float3(1, 0, 0)));
		float3 t2 = cross(cu, t1);
		float3 d = Pm - ctr;
		float ang = ec.z * 6.283;
		float2 q = float2(dot(d, t1), dot(d, t2));
		lu = float2(cos(ang) * q.x + sin(ang) * q.y, -sin(ang) * q.x + cos(ang) * q.y);
		eAx = cos(ang) * t1 + sin(ang) * t2;                          // lu.x / lu.y axes in the planet frame
		eAy = -sin(ang) * t1 + cos(ang) * t2;
		eR = ec.z; eR2 = r2e;
		eEdge = (ec.y - ec.x) * 1600.0;                              // m to the estate border (approx.)
		luW = lerp(110.0, 300.0, r2e);
		luH = luW * lerp(0.3, 0.75, frac(ec.z * 7.31));
	}
	// (wave 3) farmland, after docs/lab/ground.md at a fraction of its cost (a full port of the lab's holdings and
	// Voronoi fields cost 3-5 ms of base pass at 2-10 km here, measured with ProfileGPU): fields in the estate frame
	// with bending edges; each field decides on its own whether it is cropped, meadow or a woodlot, so the farmland
	// frays field by field into the land around it; crops in the lab's mid-summer palette (mostly green, gold grain,
	// few ploughed fields: the old palette's near-neutral ploughland and stubble turned grey under the aerial haze);
	// grass balks; hedgerows with gaps and their sun-side shadows; a holding average when fields are below a pixel.
	float hedge = 0.0, hedgeSh = 0.0;
	if (farm > 0.02)
	{
		float2 g = lu / float2(luW, luH);
		g += float2(nB, nA) * 0.05;                                 // field edges bend a little (no ruled lines)
		g.x += 0.37 * floor(g.y);                                   // rows of fields do not line up
		float2 cellF = floor(g), f = frac(g);
		uint hh = Hash(int3(int2(cellF), int(eR * 4096.0)));
		float rnd = Rand01(hh), rnd2 = Rand01(hh >> 11), rnd3 = Rand01(hh >> 21);
		float2 dE2 = float2(min(f.x, 1.0 - f.x) * luW, min(f.y, 1.0 - f.y) * luH);
		float edgeM = min(dE2.x, dE2.y);
		float kF = AAW(luH, fp), kE = AAW(1600.0, fp);
		// each field's own decision (the lab's fray): cropped where the farm intensity beats the field's random
		float fray = 0.22 * (nF + 0.6 * nA) + 0.12 * nE;
		float cultK = Smooth01((farm * 1.3 - 0.08 + fray - rnd) / 0.04);           // 1 cropped .. 0 meadow
		float wildK = Smooth01((rnd - farm * 2.2 - 0.25 - fray) / 0.04);           // untouched land beyond the outfields
		farmM = Smooth01((farm * 1.35 + 0.30 * nA + 0.15 * nF + 0.25 * (eR2 - 0.5) * kE - 0.42) / 0.30)
			* (1.0 - Smooth01((steepMid - 0.10) / 0.08)) * Smooth01((hW - 1.0) / 2.0) * (1.0 - burnt) * (1.0 - wildK * kF);
		// neighbouring fields of one holding often grow alike (rotations run farm by farm): the crop choice is part
		// field, part estate, so the patchwork clumps instead of reading as salt and pepper
		float tt = saturate(lerp(rnd2, frac(eR * 13.71), 0.4) + 0.30 * (arid - 0.35));
		float3 green = lerp(float3(0.058, 0.102, 0.030), float3(0.088, 0.125, 0.040), rnd3);
		green = lerp(green, macro * float3(0.8, 1.0, 0.55), 0.2);           // a touch of the local climate
		float3 gold = lerp(float3(0.200, 0.168, 0.080), float3(0.240, 0.205, 0.105), rnd3);
		float3 hay = lerp(float3(0.120, 0.128, 0.055), float3(0.150, 0.155, 0.072), rnd3);
		float3 dark = float3(0.040, 0.080, 0.026) * (0.9 + 0.2 * rnd3);
		float3 plough = lerp(float3(0.110, 0.078, 0.050), float3(0.140, 0.100, 0.062), rnd3);
		float3 crop = tt < 0.44 ? green : (tt < 0.64 ? gold : (tt < 0.74 ? hay : (tt < 0.89 ? dark : plough)));
		crop *= lerp(0.90, 1.10, frac(rnd3 * 7.31));
		crop *= 1.0 + 0.12 * nC + 0.08 * nD + 0.07 * nB + 0.06 * nA;      // moisture, soil and growth inside a field
		crop = lerp(crop, crop.ggg * float3(0.85, 1.0, 0.6), 0.25 * saturate(nA + 0.3 * nC));   // greener damp patches
		float rows = sin(((rnd3 > 0.5) ? lu.x : lu.y) * 6.283 / 1.6) * AAW(1.6, fp) * step(0.44, tt) * step(tt, 0.89);
		crop *= 1.0 + 0.10 * rows;                                  // crop rows close up
		// meadow and rough grazing where the field is not cropped
		float3 meadow = lerp(float3(0.080, 0.112, 0.040), float3(0.160, 0.150, 0.080), saturate(arid * 1.2 + 0.25 * nA)) * (1.0 + 0.12 * nB + 0.08 * nC);
		crop = lerp(meadow, crop, cultK);
		// woodlots: some uncropped fields were never cleared (drawn by the canopy below)
		woodlot = (1.0 - cultK) * step(rnd3, 0.18 + 0.5 * saturate(forest * 1.5)) * kF * farmM;
		// grass balk between cropped fields
		float balk = cultK * (1.0 - Smooth01((edgeM - lerp(1.2, 2.6, rnd2)) / max(0.6 * fp, 0.3))) * AAW(4.0, fp);
		crop = lerp(crop, meadow * 1.1, balk * 0.8);
		// what the fields average to below a pixel: green with a gold share per estate, darkened by the hedge net
		float goldShare = saturate(0.28 + 0.30 * (arid - 0.35) + 0.40 * (frac(eR * 13.71) - 0.5) * kE);
		float3 avgCrop = lerp(float3(0.072, 0.108, 0.036), float3(0.165, 0.142, 0.070), goldShare);
		avgCrop *= 1.0 + 0.14 * (eR - 0.5) * kE + 0.08 * nF;
		avgCrop = lerp(avgCrop, float3(0.030, 0.048, 0.020), 0.12 * (1.0 - 0.6 * arid));
		float3 fcol = lerp(avgCrop, crop, kF);
		// hedgerows: on most field edges in wet country, few in dry open fields, with gaps; a lumpy band of crowns
		// 3-6 m wide drawn as a line (its share of a coarse pixel); on the sun's far side it throws a long thin shadow
		float hedgeOn = step(0.40 + 0.40 * arid, rnd2) * Smooth01((nB + 0.5 * nA + 0.30) / 0.2) * (1.0 - wildK);
		float hwd = lerp(1.8, 3.2, rnd3) + 1.2 * saturate(0.5 + nD);
		float hW2 = max(hwd, 0.5 * fp);
		hedge = saturate((hW2 - edgeM) / max(0.6 * fp, 0.4) + 0.5) * min(1.0, hwd / hW2) * hedgeOn * kF;
		{
			// the edge's outward normal (estate frame -> planet frame); shadow length 5 m x cot(sun elevation)
			float2 eN = dE2.x < dE2.y ? float2(f.x < 0.5 ? -1.0 : 1.0, 0.0) : float2(0.0, f.y < 0.5 ? -1.0 : 1.0);
			float3 sunT = SunD - up * dot(SunD, up);
			float sinE = max(dot(SunD, up), 0.05);
			float facing = dot(eN.x * eAx + eN.y * eAy, sunT);
			float shL = 5.0 * sqrt(max(1.0 - sinE * sinE, 0.0)) / sinE;
			hedgeSh = facing > 0.0 ? (1.0 - Smooth01((edgeM - hwd - shL * facing) / max(0.6 * fp, 0.6))) * AAW(shL * facing + 2.0, fp) * hedgeOn * kF * (1.0 - hedge) : 0.0;
		}
		col = lerp(col, fcol, farmM);
		rough = lerp(rough, 0.9, farmM);
	}

	// ---- forest canopy: two crown sizes in clumps, gaps with dark understorey, per-crown tint and dome
	// shading; averages back to the macro colour when the crowns are below the pixel size.
	float beachPre = (1.0 - Smooth01((h - 0.5 - 0.9 * saturate(nA + 0.5)) / 1.0)) * beachZone;
	// ponds and pools only in real wetland (marsh, lake shores), never in fields or towns
	float pondPre = Smooth01((wetC - 0.45) / 0.3) * Smooth01((nB + 0.6 * nC + 0.4 * nD - 0.32) / 0.12) * (1.0 - saturate(farm * 3.0)) * (1.0 - saturate(urban * 4.0));
	// woodland breaks into copses, belts and clearings at every scale (forest edges are never a cell outline)
	float fclump = 0.45 * nF + 0.35 * nA + 0.25 * nE + 0.30 * nB;
	// Canopy density 0..1: dense forest is closed canopy over a dark understorey; open woodland and savanna
	// are scattered crowns over the ground's own colour, so the density fades out tree by tree instead of
	// ending in a patch outline (hard noise thresholds read as leopard spots on dry hills).
	float fmBase = saturate((forest * 1.35 + 0.45 * fclump - 0.18) / 0.9);
	fmBase *= fmBase * (3.0 - 2.0 * fmBase);
	fmBase = max(fmBase, woodlot * 0.9);                                // (wave 3) farm woodlots
	float dens = fmBase * (1.0 - beachPre) * (1.0 - pondPre);
	float closed = Smooth01((dens - 0.45) / 0.4);                      // 0 open woodland .. 1 closed canopy
	float3 canopyAvg = macro * lerp(0.80, 0.62, closed);
	float3 groundUnder = lerp(col, macro * 0.25, closed);
	if (dens > 0.01 && fp < 10.0)
	{
		float3 offS, offB;
		float3 cs = Crowns(Pm, 5.5, offS);
		float3 cb = Crowns(Pm + float3(17.0, 3.0, 9.0), 10.0, offB);
		float big = Smooth01((nB + 0.35 * nC + 0.1) / 0.35);
		float3 cr = lerp(cs, cb, big);
		float3 off = lerp(offS, offB, big);
		float cell = lerp(5.5, 10.0, big);
		float k = AAW(cell, fp);
		// crown size and presence follow the density: thin stands lose whole trees first
		float present = step(cr.y, 0.15 + 0.85 * sqrt(dens));
		float rad0 = lerp(0.42, 0.68, frac(cr.y * 7.13)) * lerp(0.7, 1.0, closed);
		float cover = (1.0 - Smooth01((cr.z - rad0) / 0.18)) * present;
		float dome = saturate(1.0 - cr.z / max(rad0, 0.01));
		float3 tint = lerp(float3(0.70, 0.88, 0.62), float3(1.30, 1.18, 0.80), frac(cr.y * 5.31));
		tint = lerp(tint, float3(0.85, 0.95, 1.05), saturate(frac(cr.y * 3.7) - 0.7) * 1.5);
		float3 canopy = macro * tint * lerp(0.55, 1.15, dome);
		// shadow of the crown on open ground (sun side unknown here: a soft ring)
		float shade = (1.0 - Smooth01((cr.z - rad0 - 0.05) / 0.25)) * (1.0 - cover) * (1.0 - closed);
		float3 under = groundUnder * (1.0 - 0.35 * shade);
		float3 detail = lerp(under, canopy, cover);
		float3 avgMix = lerp(col, canopyAvg, saturate(dens * 1.1));
		col = lerp(avgMix, detail, k * saturate(dens * 4.0));
		float3 ot = off - up * dot(off, up);
		N = normalize(N + ot * (0.7 * k * cover / max(rad0, 0.3)));
		rough = lerp(rough, 0.9, dens);
	}
	else if (dens > 0.01)
	{
		col = lerp(col, canopyAvg, saturate(dens * 1.1));
	}

	// (wave 3) hedgerows (tree lines) and their shadows across the fields
	{
		float3 hedgeC = float3(0.030, 0.050, 0.020) * (1.0 + 0.12 * nC);
		col = lerp(col, hedgeC, hedge * farmM);
		col *= 1.0 - 0.5 * hedgeSh * farmM;
	}

	// ---- rock with strata on steep slopes
	// Rock: the masks are wide ramps of slope, so bare rock grows steadily with steepness and mountainness;
	// narrow thresholds on a slope that carries every relief octave would draw a lace of rock patches.
	float rockM = Smooth01((steepMid - 0.12 + 0.08 * nB + 0.05 * nC) / 0.34);
	rockM = max(rockM, saturate(mtn * 1.6 - 0.6 + 0.4 * nA) * Smooth01((steepMid - 0.02) / 0.32));
	// dry high mountains: rock on the crests and steep flanks, scree and soil in the gullies
	rockM = max(rockM, saturate(mtn * 1.8 + arid * 0.6 - 0.9 + 0.25 * nA) * saturate(steepMid * 2.2 + 0.5 * (crest - 0.5) + 0.15) * (1.0 - forest) * 0.8);
	// rocky headlands between sandy coves on exposed, hilly coasts
	float coastRock = saturate((mtn * 2.5 + steepMid * 4.0 + 0.8 * nE + 0.5 * nF - 0.45) * 2.0) * (1.0 - Smooth01((hW - 3.0) / 12.0)) * Smooth01((h + 2.0) / 2.0);
	rockM = max(rockM, coastRock * 0.85);
	{
		float t = h / 6.5 + 2.2 * NoiseAA(Pm, 140.0, fp * 0.2, float3(3.0, 1.0, 7.0)) + 0.35 * NoiseAA(Pm, 23.0, fp, float3(1, 2, 3));
		float k = AAW(6.5, fp);
		float layer = Hash01(int3(floor(t), 7, 3));
		float3 rockA = float3(0.27, 0.24, 0.21), rockB = float3(0.43, 0.39, 0.33), rockC = float3(0.35, 0.27, 0.20);
		float3 rk = lerp(rockA, rockB, layer);
		rk = lerp(rk, rockC, saturate(Hash01(int3(floor(t * 0.37), 1, 9)) - 0.6) * 1.5);
		float3 rockAvg = float3(0.34, 0.30, 0.26);
		rk = lerp(rockAvg, rk, k) * (1.0 + 0.25 * nC + 0.2 * nD);
		// desert rock takes the warm colour of the land around it (sandstone, red beds), wet rock is darker
		rk = lerp(rk, macro * float3(0.95, 0.85, 0.75) * (1.0 + 0.2 * nC), 0.45 * arid);
		rk *= lerp(1.0, 0.8, saturate(1.0 - arid) * 0.5);
		rk *= lerp(0.65, 1.1, saturate(0.5 + 0.5 * dot(N, nMid) * 2.0 - 0.5));
		col = lerp(col, rk, rockM);
		rough = lerp(rough, 0.72, rockM);
	}

	// ---- sand: beaches (sea and lake shores) and deserts, with ripples
	// Beaches come and go along the coast (coves and stretches of marsh or rock between them), and their
	// width follows the ground's slope: a band of height, not a fixed distance, so gentle coasts get wide
	// strands and steep ones a thin line.
	float strand = Smooth01((0.35 + 0.9 * nE + 0.6 * nF + 0.4 * nA) / 0.5);
	float beachTop = 0.25 + 1.1 * saturate(nA + 0.5) * strand;
	float beach = (1.0 - Smooth01((hW - beachTop) / 0.6)) * (1.0 - rockM) * Smooth01((h + 25.0) / 10.0) * max(beachZone * lerp(0.25, 1.0, strand), Smooth01((3.0 - hLake) / 3.0) * 0.7);
	float3 sand = lerp(float3(0.30, 0.26, 0.19), float3(0.38, 0.33, 0.24), saturate(0.5 + nB)) * (1.0 + 0.08 * nC + 0.06 * nD);
	sand = lerp(sand, sand * float3(0.75, 0.72, 0.70), saturate(wetC + 0.3 * (1.0 - arid)));
	col = lerp(col, sand, beach * (1.0 - 0.7 * forest));
	if (arid > 0.3 && fp < 3.0)
	{
		float rip = sin(dot(Pm, normalize(float3(0.3, 0.8, 0.5))) * 6.283 / 1.6 + 3.0 * nC) * AAW(1.6, fp) * (arid - 0.3);
		N = normalize(N + 0.08 * rip * normalize(cross(up, float3(0.6, -0.2, 0.77))));
	}

	// ---- towns: built-up ground in blocks with streets (the buildings themselves are the towns module's)
	if (urban > 0.03)
	{
		// a ragged built-up area: dense where urban is high, thinning outward into scattered blocks (the
		// interpolated cell value alone gives straight triangle edges)
		float uN = 0.55 * NoiseAA(Pm, 900.0, fp * 0.3, float3(6.1, 2.2, 9.3)) + 0.35 * nA + 0.25 * nB;
		float um = Smooth01((urban * 1.4 + 0.45 * uN - 0.40) / 0.35) * (1.0 - Smooth01((steepMid - 0.14) / 0.1)) * Smooth01((hW - 0.6) / 1.5);
		float bw = lerp(60.0, 110.0, eR2), bh = bw * lerp(0.6, 1.0, frac(eR * 3.7));
		float2 g = lu / float2(bw, bh);
		float2 f = frac(g);
		uint hh = Hash(int3(int2(floor(g)), 977));
		float rnd = float(hh & 0xFFFFu) / 65535.0;
		float st = min(min(f.x, 1.0 - f.x) * bw, min(f.y, 1.0 - f.y) * bh);   // m to the street centre line
		float street = 1.0 - Smooth01((st - 4.0) / 2.0);
		float k = AAW(bh, fp);
		float3 lot = lerp(float3(0.085, 0.080, 0.072), float3(0.17, 0.15, 0.125), rnd) * (1.0 + 0.15 * nC);
		lot = lerp(lot, float3(0.06, 0.085, 0.04), step(0.86, rnd));                 // parks and yards
		// from far away: a mottled grey-brown mixed with the ground it is built on (gardens, trees)
		float3 avgTown = lerp(float3(0.085, 0.080, 0.072), float3(0.13, 0.12, 0.105), saturate(0.5 + nA + 0.5 * nB)) * lerp(1.0, 0.8, saturate(Age / 6.0));
		avgTown = lerp(avgTown, col, 0.3 * (1.0 - urban));
		float3 ucol = lerp(avgTown, lerp(lot, float3(0.05, 0.05, 0.05), street * AAW(10.0, fp)), k);
		col = lerp(col, ucol, um * 0.9);
		rough = lerp(rough, 0.8, um);
		N = normalize(lerp(N, nMid, um * 0.7));
		forest *= 1.0 - um;
	}

	// ---- seabed (seen through shallow water)
	float under = Smooth01((-h - 0.5) / 4.0);
	// pale sand in the first metres (turquoise through the water, brightest in warm seas), darker silt and
	// weed deeper down
	float3 bed = lerp(float3(0.40, 0.37, 0.28), float3(0.15, 0.15, 0.12), Smooth01((-h - 2.0 - 6.0 * saturate(nF + 0.5)) / 22.0));
	bed = lerp(bed, float3(0.17, 0.16, 0.12), (1.0 - saturate((tempC - 8.0) / 12.0)) * 0.6);
	// (wave 3) drowned land (above the base sea, under the risen one: SeaRise m): not a pale sandy sea floor but the
	// old ground under a young sea, dark silt and drowned meadow in patches at the old land's scales, with pale
	// wave-worked flats in the first metres off the new shore. Seen through the turbid shallows (water.hlsl) this
	// reads as flooded land, and the depth (deepening toward the old coast) as a gradient.
	float drown = SeaRise > 0.5 ? Smooth01((h + SeaRise + 1.5) / 3.0) : 0.0;
	if (drown * under > 0.001)
	{
		float pn = saturate(0.5 + 0.7 * nF + 0.5 * nA + 0.35 * nB + 0.3 * nE);
		float3 silt = float3(0.052, 0.047, 0.033) * (1.0 + 0.2 * nC + 0.12 * nD);
		float3 weed = float3(0.020, 0.030, 0.014) * (1.0 + 0.25 * nC);
		float3 bedD = lerp(silt, weed, Smooth01((pn - 0.35) / 0.4) * 0.85);
		float flats = (1.0 - Smooth01((-h - 0.6 - 2.0 * saturate(nA + 0.5)) / 2.5)) * saturate(0.45 + 0.8 * nF);
		bedD = lerp(bedD, float3(0.115, 0.105, 0.078) * (1.0 + 0.15 * nC), flats * 0.55);
		bed = lerp(bed, bedD, drown);
	}
	col = lerp(col, bed * (1.0 + 0.2 * nB + 0.1 * nC), under * (1.0 - rockM * 0.5));
	float underLake = Smooth01((-hLake - 0.5) / 3.0);
	col = lerp(col, float3(0.16, 0.15, 0.11) * (1.0 + 0.2 * nB), underLake);

	// ---- wet shoreline band and marsh
	float wetBand = 1.0 - Smooth01((hW - 0.1 - 0.3 * nC) / 1.4);
	wetBand *= Smooth01((hW + 3.0) / 2.0);
	// wet sand is a strip of 10-20 m at the water's edge, measured along the ground: on a gentle shore a band
	// of 1.5 m of height is 100+ m wide and read as a dark, glossy second shoreline
	float tanMid = sqrt(max(1.0 - cosMid * cosMid, 0.0)) / max(cosMid, 0.1);
	float distW = abs(hW) / max(tanMid, 0.003);
	wetBand *= (1.0 - Smooth01((distW - 10.0 - 6.0 * nC) / 8.0)) * saturate(18.0 / max(fp, 1.0));
	float pond = pondPre * (1.0 - urban);
	col *= lerp(1.0, 0.78, saturate(wetC));
	float wet = saturate(max(wetBand, pond));
	col *= lerp(1.0, 0.55, wet);
	col = lerp(col, float3(0.03, 0.045, 0.04), pond * 0.8);
	rough = lerp(rough, 0.3, wetBand);
	rough = lerp(rough, 0.12, pond);
	N = normalize(lerp(N, up, pond));

	// ---- snow: by climate and height, slides off steep faces
	// snow line: broken by large features (180 m / 700 m), only lightly by the small ones
	float snow = saturate((snowC * 1.4 - 0.2 + 0.30 * nA + 0.22 * nF + 0.10 * nB + 0.06 * nC - 0.5) * 2.2 + 0.5) * Smooth01(snowC * 6.0) * (1.0 - urban * 0.7);
	snow *= 1.0 - Smooth01((steepMid - 0.20 - 0.08 * nC - 0.05 * nB) / 0.12);
	N = normalize(lerp(N, nMid, snow * 0.7));
	float glacier = Smooth01((iceE - 0.35) / 0.3);
	float3 snowCol = float3(0.70, 0.72, 0.76) * (1.0 + 0.04 * nC + 0.04 * nF);
	float crev = (1.0 - Smooth01((abs(Noised(Pm / 60.0 + float3(4.0, 1.0, 2.0)).x) - 0.02) / 0.05)) * AAW(60.0, fp) * glacier;
	snowCol = lerp(snowCol, float3(0.55, 0.62, 0.70) * (1.0 + 0.1 * nA), glacier * 0.45);
	snowCol = lerp(snowCol, float3(0.18, 0.24, 0.30), crev * 0.7);
	col = lerp(col, snowCol, snow);
	rough = lerp(rough, 0.55, snow);

	// ---- rivers. The mesh carries a signed distance across the nearest reach (crisp channel close up);
	// from orbit and mid range the river map (coverage mips, distance field).
	float riv = 0.0;
	float4 t0 = RiverTex.SampleLevel(RiverTexSampler, ouv, 0.0);
	{
		const float texelM = 86.6;
		float covLod = log2(max(fp / texelM, 1.0));
		float cov = saturate(RiverTex.SampleLevel(RiverTexSampler, ouv, covLod).b * 1.6);
		// riparian vegetation: rivers read from orbit as lush ribbons (strongest in dry land)
		float covWide = saturate(RiverTex.SampleLevel(RiverTexSampler, ouv, covLod + 2.0).b * 6.0);
		// the map holds the SIGNED distance across the nearest reach (DmPlanetMaps EncodeSigned): bilinear filtering
		// keeps its zero line on the river, so a thin river stays one continuous line. Where the sign flips between
		// two reaches (or at the edge of the map's range) the filtered field jumps faster than any true distance
		// (1 m per m): no line there.
		float lx = max(length(ddx(P)) * 0.01, 1e-3), ly = max(length(ddy(P)) * 0.01, 1e-3);
		float dS = DecodeSigned(t0.r), hwS = t0.g * 64.0;
		float adS = abs(dS);
		float okMap = 1.0 - Smooth01((max(abs(ddx(dS)) / lx, abs(ddy(dS)) / ly) - 1.35) / 0.4);
		float ripNear = (t0.g > 0.004) ? 1.0 - Smooth01((adS - hwS - 40.0) / 300.0) : 0.0;
		float rip = lerp(ripNear, covWide, Smooth01((fp - 60.0) / 120.0)) * (1.0 - snowC) * Smooth01((hW - 0.5) / 2.0);
		float3 lush = lerp(macro, float3(0.035, 0.07, 0.025), 0.55);
		col = lerp(col, lush * (1.0 + 0.2 * nB), rip * lerp(0.25, 0.7, saturate(arid + 0.2)) * (1.0 - farmM * 0.5));
		float w2 = max(hwS, 0.5 * fp);
		float sdfLine = hwS > 0.5 ? saturate((w2 - adS) / max(0.5 * fp, 2.0) + 0.5) * min(1.0, 1.4 * hwS / w2) * okMap : 0.0;
		float farMix = Smooth01((fp - 0.5 * texelM) / texelM);
		float rivMap = lerp(sdfLine, cov, farMix);
		float ad = abs(rivD);
		float w = max(rivW, 0.5 * fp);
		float edgeN = 0.1 * nD * rivW;
		// the same test on the mesh's signed distance: across the medial axis between two reaches (a confluence,
		// two delta arms) the per-vertex sign flips and the interpolation crossed zero, drawing a false hairline
		float okMesh = 1.0 - Smooth01((max(abs(ddx(rivD)) / lx, abs(ddy(rivD)) / ly) - 1.6) / 0.6);
		float rivMesh = rivW > 0.5 ? saturate((w + edgeN - ad) / max(0.5 * fp, 0.15 * rivW) + 0.5) * min(1.0, rivW / w) * okMesh : 0.0;
		float bank = rivW > 0.5 ? (1.0 - Smooth01((ad - 1.5 * rivW) / (0.6 * rivW))) * (1.0 - rivMesh) * Smooth01((rivW - fp) / rivW) * okMesh : 0.0;
		float meshOk = 1.0 - Smooth01((S - 12.0) / 24.0);
		bank *= meshOk;
		riv = lerp(rivMap, rivMesh, meshOk) * Smooth01((h + 1.5) / 1.5);
		col = lerp(col, col * 0.6, bank);
		rough = lerp(rough, 0.5, bank);
		// from orbit a river is a dark thread (deep water absorbs, the sky sheen comes from the low roughness)
		float3 water = lerp(float3(0.018, 0.032, 0.045), lerp(float3(0.025, 0.04, 0.05), float3(0.07, 0.062, 0.042), saturate(rivW / 35.0)), meshOk);
		water = lerp(water, float3(0.62, 0.66, 0.70), snowC * Smooth01((-8.0 - tempC) / 6.0));   // frozen in deep winter
		col = lerp(col, water, riv);
		// a river seen as a sub-pixel thread is water mixed with bars and banks: only close up is it a mirror
		rough = lerp(rough, lerp(0.38, 0.07, meshOk), riv);
		N = normalize(lerp(N, up, riv * Smooth01((rivW - 2.0 * fp) / max(rivW, 0.01))));
	}

	// ---- roads (trade routes): a line from mid range, faint coverage from orbit (they glow at night)
	float rcov = 0.0;
	float3 dbgLines = 0;
	{
		float roadLod = max(log2(max(fp / 86.6, 1.0)), 1.0);
		rcov = RiverTex.SampleLevel(RiverTexSampler, ouv, roadLod).a;
		// signed like the river field (an unsigned field of 87 m texels filled the V between texels and broke
		// the road into dots); no line where the sign flips (junctions, the edge of the range)
		float dRs = DecodeSigned(t0.a);
		float okR = 1.0 - Smooth01((max(abs(ddx(dRs)) / max(length(ddx(P)) * 0.01, 1e-3), abs(ddy(dRs)) / max(length(ddy(P)) * 0.01, 1e-3)) - 1.35) / 0.4);
		float dR = abs(dRs);
		float rw = 4.0 + 1.5 * saturate(Age / 4.0);
		float wr = max(rw, 0.5 * fp);
		float lineMap = saturate((wr - dR) / max(0.5 * fp, 2.0) + 0.5) * min(1.0, rw / wr) * okR;
		// close up the mesh carries the signed distance across the nearest road (vertex colour R, +-60 m):
		// exact under interpolation along straight reaches, so the road is a crisp, continuous band
		float sdR = (VC.r - 0.5) * 120.0;
		float rwV = rw * (0.8 + 0.25 * VC.g * 4.0 / 3.0);
		float edgeR = 0.6 * nD;
		float wv = max(rwV, 0.5 * fp);
		float okRM = 1.0 - Smooth01((max(abs(ddx(sdR)) / max(length(ddx(P)) * 0.01, 1e-3), abs(ddy(sdR)) / max(length(ddy(P)) * 0.01, 1e-3)) - 1.6) / 0.6);
		float lineMesh = saturate((wv + edgeR - abs(sdR)) / max(0.5 * fp, 0.3) + 0.5) * min(1.0, rwV / wv) * okRM;
		float meshR = (VCA < 0.5 && abs(sdR) < 58.0) ? 1.0 - Smooth01((S - 12.0) / 20.0) : 0.0;
		float verge = (VCA < 0.5) ? (1.0 - Smooth01((abs(sdR) - rwV - 1.5) / 2.5)) * (1.0 - lineMesh) * meshR * AAW(3.0, fp) : 0.0;
		float line_ = lerp(lineMap, lineMesh, meshR);
		col = lerp(col, col * 0.8 + float3(0.012, 0.01, 0.006), verge);
		float farR = Smooth01((fp - 40.0) / 80.0);
		// a road is a few metres wide: from orbit it is far below a pixel and only a faint trace of the
		// busiest routes remains by day (they glow at night: emissive below)
		float road = lerp(line_, saturate(rcov * 1.5) * 0.08, farR) * (1.0 - riv) * Smooth01((hW - 0.3) / 0.5);
		dbgLines = Dbg > 13.5 ? float3(saturate(1.0 - abs(rivD) / 60.0), saturate(steepMid * 4.0), saturate(1.0 - abs(sdR) / 30.0) * (VCA < 0.5 ? 1.0 : 0.0))
			: float3(lineMap * (1.0 - meshR), lineMesh * meshR, riv);
		float3 rcol = lerp(float3(0.40, 0.34, 0.25), float3(0.16, 0.16, 0.16), Smooth01((Age - 3.5) / 1.0)) * (1.0 + 0.1 * nC);
		col = lerp(col, rcol, road * 0.9);
		rough = lerp(rough, 0.8, road);
		N = normalize(lerp(N, nMid, road));
	}

	// ---- scorch (wave 3: the masks above), fallout
	{
		float keep = 1.0 - riv;
		// singed fringe: leaves browned and dried, not yet black
		col = lerp(col, col * float3(0.82, 0.64, 0.42) + float3(0.010, 0.005, 0.0), singe * keep);
		// burnt ground: black-brown char with darker and browner patches; the core grey-white with ash drifts
		// (verify) gentler 45 m / 11 m flecking: at full strength it read as a uniform pepper grain over the scar at 15 km
		float3 charC = lerp(float3(0.030, 0.022, 0.015), float3(0.068, 0.050, 0.034), saturate(0.5 + 0.55 * nB + 0.4 * nC + 0.3 * nF));
		float3 ash = lerp(charC, float3(0.100, 0.092, 0.082), saturate(0.25 + 0.8 * nC + 0.6 * nD + 0.5 * nA));
		// not every patch burnt to black: scorched grass and half-burnt scrub show through in places
		// (verify) half-burnt ground in broad drifts and fine flecks, not 200 m spots
		float burntK = burnt * lerp(0.72, 1.0, saturate(0.60 + 0.8 * nF + 0.4 * nE + 0.5 * nC));
		col = lerp(col, charC, burntK * keep);
		col = lerp(col, ash, charred * keep * 0.7);
		rough = lerp(rough, 0.95, burnt);
		float rm = saturate(rad * 1.5);
		col = lerp(col, dot(col, float3(0.33, 0.45, 0.22)) * float3(1.15, 1.10, 0.70), rm * 0.7);
	}

	// ---- overlay (cultures, temperature, rainfall, devotion, habitability)
	float3 ovE = 0;
	if (OverOn > 0.5)
	{
		float4 mE = MapE.SampleLevel(MapESampler, ouv, lodS);
		float3 oc = pow(saturate(mE.rgb), 2.2);
		col = lerp(col, oc * 0.9, mE.a);
		ovE = oc * mE.a * 0.04;
	}

	// ---- emissive: night lights, lava, fire, the molten crust
	float night = saturate(-dot(up, SunD) * 6.0 + 0.1);
	// city lights: bright cores and dimmer districts in clumps, broken into specks, along lit roads; close up
	// the towns module lights its windows, so the ground glow fades out below ~40 m footprints
	float clump = saturate(0.55 + 0.9 * NoiseAA(Pm, 1400.0, fp * 0.4, float3(2.1, 7.3, 4.4)) + 0.6 * NoiseAA(Pm, 450.0, fp * 0.4, float3(5.5, 1.2, 8.1)));
	float speck = saturate((NoiseAA(Pm, 110.0, fp * 0.5, float3(3.7, 3.7, 3.7)) + NoiseAA(Pm, 37.0, fp * 0.5, float3(0, 0, 0)) * 0.5 + 0.3 * (1.0 - AAW(37.0, fp)) - 0.05) * 4.0);
	float roadGlow = saturate(rcov * 2.5) * saturate((Age - 2.0) / 3.0) * 0.35;
	float lightsK = (lights * lights * clump * (0.3 + 0.7 * speck) + roadGlow * (0.4 + 0.6 * lights)) * (1.0 - Smooth01((40.0 - fp) / 30.0) * 0.85);
	Emis = lightsK * night * float3(1.0, 0.58, 0.26) * 3.5 + ovE;
	float crack = 0.0;
	if (lava > 0.01 || Heat > 0.001)
	{
		float3 offL; float r2;
		float3 lc = Cells(Pm / 38.0 + float3(0.0, 0.0, Tm * 0.004), int3(9, 9, 9), offL, r2);
		float vein = 1.0 - Smooth01(((lc.y - lc.x) - 0.02) / 0.08);
		float big = 1.0 - Smooth01((abs(Noised(Pm / 420.0 + float3(Tm * 0.01, 0, 0)).x) - 0.02) / 0.10);
		// from orbit the 38 m / 420 m cracks are far below a pixel: what is left is a crust of plates (Worley
		// edges at ~9 km and ~2.5 km) with hotter and cooler provinces, so the molten world reads as cracked
		// slag with glowing seams instead of one flat orange (the fine cracks take over as they resolve)
		float3 offP; float r2p;
		float3 pc = Cells(Pm / 9000.0 + float3(0.0, Tm * 0.0005, 0.0), int3(5, 61, 17), offP, r2p);
		float3 offQ; float r2q;
		float3 qc = Cells(Pm / 2500.0 + float3(Tm * 0.001, 0.0, 0.0), int3(29, 3, 47), offQ, r2q);
		float seamP = (1.0 - Smooth01(((pc.y - pc.x) - 0.015) / 0.09)) * AAW(9000.0, fp * 0.25);
		float seamQ = (1.0 - Smooth01(((qc.y - qc.x) - 0.02) / 0.10)) * AAW(2500.0, fp * 0.5);
		float prov = saturate(0.5 + 0.9 * NoiseAA(Pm, 30000.0, fp * 0.1, float3(2.2, 8.1, 4.6)) + 0.5 * NoiseAA(Pm, 8000.0, fp * 0.2, float3(6.6, 1.3, 9.9)));
		// (integration) the plates themselves barely glow: dark slag, light only in the seams (docs/concept/genesis)
		float farCrack = (0.004 + 0.016 * prov * prov) + 0.5 * max(seamP, 0.55 * seamQ) * (0.25 + 0.75 * prov);
		crack = lerp(farCrack, max(vein * AAW(38.0, fp), big), AAW(420.0, fp));
		float flick = 0.75 + 0.25 * sin(Tm * 2.3 + 40.0 * r2);
		float lv = saturate(lava * (1.4 + 1.2 * sN)) * Smooth01((h + 0.5) / 1.0);   // lava under the sea is quenched; (wave 3) ragged edge
		col = lerp(col, float3(0.035, 0.03, 0.03), lv);
		Emis += lv * (0.25 + crack) * flick * float3(1.0, 0.32, 0.06) * 7.0;
		// the molten crust: dark slag with glowing cracks; under water it is quenched (black, steaming)
		float hm = saturate(Heat);
		float dryCrust = Smooth01((h + 2.0) / 2.0);
		col = lerp(col, float3(0.05, 0.04, 0.035), saturate(hm * 1.5));
		// (integration) deeper red-orange and about a third of the old gain: the old 9x blew the lit disc to peach
		Emis += hm * (0.005 + 0.995 * crack) * float3(1.0, 0.2, 0.03) * 3.2 * (0.35 + 0.65 * hm * hm) * dryCrust;
	}
	if (fire > 0.01)
	{
		float fl = saturate(Noised(Pm / 25.0 + float3(0, 0, Tm * 0.8)).x * 1.5 + 0.5 * Noised(Pm / 7.0 + float3(Tm * 1.7, 0, 0)).x + fire - 0.4);
		col = lerp(col, float3(0.04, 0.035, 0.03), saturate(fire * 2.0));
		Emis += fire * fl * float3(1.0, 0.45, 0.10) * 10.0;
	}

	// mask views (-planetdebug=10/11/12): which rule paints what
	// 13: road from the map (R), road from the mesh (G), river (B); 14: mesh river distance < 60 m (R), slope (G), mesh road distance < 30 m (B)
	if (Dbg > 14.5) { col = float3(saturate(SeaRise / 64.0), drown, under); Emis = 0; }   // 15: sea rise / drowned land / under water
	else if (Dbg > 12.5) { col = dbgLines; Emis = 0; }
	else if (Dbg > 9.5) { col = Dbg < 10.5 ? float3(rockM, dens, beach) : (Dbg < 11.5 ? float3(snow, pond, soilM) : float3(wetBand, under, farmM)); Emis = 0; }
	else if (Dbg > 2.5) { col = VC; Emis = 0; }
	else if (Dbg > 0.5 && VCA > 0.5) { col = VC; }

	// The sky light is one capture above the ground under the camera; from orbit it would light the night
	// side too. Gate the ambient by the local sun elevation (twilight to -7 deg).
	AO = Smooth01((dot(up, SunD) + 0.12) / 0.3);
	Rough = rough;
	Nw = normalize(ToWorld(N, RX, RY, RZ));
	return saturate(col);
}
