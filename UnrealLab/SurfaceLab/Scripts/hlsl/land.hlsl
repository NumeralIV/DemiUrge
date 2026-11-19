// ---- land material (body functions; the call is in land_call.hlsl) ----
// Gradient (per metre, tangent) of the relief octaves the mesh does not carry.
// Same loop as SurfNoise::Relief: E = eroded fbm, R = ridged multifractal. Returns RAe*gE + RAr*gR.
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
// Tree crowns: 3D cellular noise. Returns (crown height 0..1, cell random, dist to centre)
float3 Crowns(float3 Pm, float cell, out float3 off)
{
	off = 0;
	float3 p = Pm / cell;
	float3 i = floor(p);
	float best = 9.0; float rnd = 0.0;
	for (int z = -1; z <= 1; z++)
		for (int y = -1; y <= 1; y++)
			for (int x = -1; x <= 1; x++)
			{
				int3 c = int3(i) + int3(x, y, z);
				uint h = Hash(c + int3(311, 17, 5));
				float3 o = float3(h & 0x3FFu, (h >> 10) & 0x3FFu, (h >> 20) & 0x3FFu) / 1023.0;
				float3 r = float3(c) + 0.15 + 0.7 * o - p;
				float d2 = dot(r, r);
				if (d2 < best) { best = d2; rnd = float((h >> 7) & 0xFFu) / 255.0; off = -r; }
			}
	float dd = sqrt(best);
	return float3(1.0 - Smooth01(dd / 0.75), rnd, dd);
}

float2 OctUV(float3 n)
{
	n /= (abs(n.x) + abs(n.y) + abs(n.z));
	float2 p = n.xy;
	if (n.z < 0.0) p = float2((1.0 - abs(n.y)) * (n.x >= 0.0 ? 1.0 : -1.0), (1.0 - abs(n.x)) * (n.y >= 0.0 ? 1.0 : -1.0));
	return p * 0.5 + 0.5;
}

float3 Land(float3 P, float3 Nv, float4 VC, float2 U0, float2 U1, float2 U2, float2 U3, float Tm, float3 SunD,
	Texture2D RiverTex, SamplerState RiverTexSampler, out float Rough, out float3 Nw, out float3 Emis, out float AO)
{
	float3 up = normalize(P);
	float h = (length(P) - 1.0e7) * 0.01;                         // m above sea
	float fp = max(length(ddx(P)), length(ddy(P))) * 0.01 + 0.02; // pixel footprint, m
	float3 Pm = P * 0.01;
	float S = exp2(U2.x);
	float3 macro = pow(saturate(VC.rgb), 2.2);
	float forest = VC.a;
	float beachZone = U0.x, arid = U0.y;
	float lights = 0.0; // city lights belong to the towns module (settlement map), not to the ground
	float snowC = max(U2.y, 0.0), wetC = max(-U2.y, 0.0);
	float rivD = U3.x, rivW = U3.y;

	float3 n0 = normalize(Nv);
	float RAe = U1.x, RAr = U1.y;
	float FA = 0.3 + 1.5 * RAr / max(RAe + RAr, 1e-3);   // SurfNoise::FineAmp
	float mtn = RAr / max(RAe + RAr, 1e-3);
	float3 nMid = normalize(n0 - ReliefGrad(up, S, fp, RAe, RAr));
	float3 gF = FineGrad(up, S, fp) * FA;
	float3 N = normalize(nMid - gF);
	float cosMid = dot(nMid, up);
	float steepMid = 1.0 - cosMid;

	// broad colour noise (all averaged out when the pixel is larger than the feature)
	float nA = NoiseAA(Pm, 180.0, fp, float3(1.7, 9.2, 3.3));
	float nB = NoiseAA(Pm, 45.0, fp, float3(5.1, 2.4, 7.9));
	float nC = NoiseAA(Pm, 11.0, fp, float3(8.3, 4.6, 1.2));
	float nD = NoiseAA(Pm, 2.7, fp, float3(2.2, 6.1, 9.4));

	// ---- vegetation / soil base, with variation at every scale down to the pixel (orbit included)
	float nE = NoiseAA(Pm, 2600.0, fp, float3(4.4, 1.1, 8.8));
	float nF = NoiseAA(Pm, 700.0, fp, float3(7.2, 3.9, 0.6));
	float3 col = macro * (1.0 + 0.16 * nE + 0.12 * nF);
	// meadow and steppe variation: lighter, drier patches and darker lush ones
	float3 dry = macro * float3(1.25, 1.12, 0.80) + float3(0.02, 0.015, 0.0);
	col = lerp(col, dry, saturate(0.5 + 0.8 * nF + 0.9 * nA + 0.5 * nB) * 0.45 * (1.0 - forest));
	col *= 1.0 + 0.18 * nB + 0.12 * nC + 0.10 * nD;
	// bare soil patches where vegetation is sparse
	float3 soil = float3(0.20, 0.15, 0.10);
	float soilM = saturate((nB + 0.6 * nC) * 2.0 - 0.7 + arid * 0.4) * (1.0 - forest) * 0.45;
	col = lerp(col, lerp(soil, macro, arid), soilM);
	float rough = 0.88;

	// ---- forest canopy: two crown sizes in clumps, gaps with dark understorey, per-crown tint and
	// dome shading; averages back to the macro colour when the crowns are below the pixel size.
	float beachPre = (1.0 - Smooth01((h - 0.5 - 0.9 * saturate(nA + 0.5)) / 1.0)) * beachZone;
	float pondPre = wetC * Smooth01((nB + 0.6 * nC + 0.4 * nD - 0.32) / 0.12);
	if (forest > 0.02 && fp < 10.0 && beachPre < 0.99)
	{
		float3 offS, offB;
		float3 cs = Crowns(Pm, 5.5, offS);
		float3 cb = Crowns(Pm + float3(17.0, 3.0, 9.0), 10.0, offB);
		float big = Smooth01((nB + 0.35 * nC + 0.1) / 0.35);
		float3 cr = lerp(cs, cb, big);
		float3 off = lerp(offS, offB, big);
		float cell = lerp(5.5, 10.0, big);
		float k = AAW(cell, fp);
		float rad = lerp(0.42, 0.68, frac(cr.y * 7.13));
		float cover = 1.0 - Smooth01((cr.z - rad) / 0.18);          // inside a crown
		float dome = saturate(1.0 - cr.z / max(rad, 0.01));
		float3 tint = lerp(float3(0.70, 0.88, 0.62), float3(1.30, 1.18, 0.80), cr.y);
		tint = lerp(tint, float3(0.85, 0.95, 1.05), saturate(frac(cr.y * 3.7) - 0.7) * 1.5);
		float3 canopy = macro * tint * lerp(0.55, 1.2, dome);
		float3 gap = macro * 0.22;
		float3 detail = lerp(gap, canopy, cover);
		float3 avg = macro * 0.62;                                   // what the crowns average to
		float fm = saturate(forest * 1.5 + 0.5 * nB - 0.25) * (1.0 - beachPre) * (1.0 - pondPre);
		col = lerp(col, lerp(avg, detail, k), fm);
		float3 ot = off - up * dot(off, up);
		N = normalize(N + ot * (0.7 * k * fm * cover / max(rad, 0.3)));
		rough = lerp(rough, 0.9, fm);
	}

	// ---- rock with strata on steep slopes
	float rockM = Smooth01((steepMid - 0.16 + 0.10 * nB + 0.06 * nC) / 0.20);
	rockM = max(rockM, saturate(mtn * 1.6 - 0.6 + 0.5 * nA) * Smooth01((steepMid - 0.05) / 0.12));
	{
		float t = h / 6.5 + 2.2 * NoiseAA(Pm, 140.0, fp * 0.2, float3(3.0, 1.0, 7.0)) + 0.35 * NoiseAA(Pm, 23.0, fp, float3(1, 2, 3));
		float k = AAW(6.5, fp);
		float layer = Hash01(int3(floor(t), 7, 3));
		float3 rockA = float3(0.27, 0.24, 0.21), rockB = float3(0.43, 0.39, 0.33), rockC = float3(0.35, 0.27, 0.20);
		float3 rk = lerp(rockA, rockB, layer);
		rk = lerp(rk, rockC, saturate(Hash01(int3(floor(t * 0.37), 1, 9)) - 0.6) * 1.5);
		float3 rockAvg = float3(0.34, 0.30, 0.26);
		rk = lerp(rockAvg, rk, k) * (1.0 + 0.25 * nC + 0.2 * nD);
		// fresh faces lighter, crevices dark
		rk *= lerp(0.65, 1.1, saturate(0.5 + 0.5 * dot(N, nMid) * 2.0 - 0.5));
		col = lerp(col, rk, rockM);
		rough = lerp(rough, 0.72, rockM);
	}

	// ---- sand: beaches and deserts, with ripples
	float beach = (1.0 - Smooth01((h - 0.4 - 0.9 * saturate(nA + 0.5)) / 0.9)) * (1.0 - rockM) * Smooth01((h + 25.0) / 10.0) * beachZone;
	float3 sand = float3(0.60, 0.52, 0.36) * (1.0 + 0.08 * nC + 0.06 * nD);
	col = lerp(col, sand, beach * (1.0 - 0.7 * forest));
	if (arid > 0.3 && fp < 3.0)
	{
		float rip = sin(dot(Pm, normalize(float3(0.3, 0.8, 0.5))) * 6.283 / 1.6 + 3.0 * nC) * AAW(1.6, fp) * (arid - 0.3);
		N = normalize(N + 0.08 * rip * normalize(cross(up, float3(0.6, -0.2, 0.77))));
	}

	// ---- seabed (seen through shallow water)
	float under = Smooth01((-h - 0.5) / 4.0);
	col = lerp(col, float3(0.20, 0.19, 0.15) * (1.0 + 0.2 * nB), under * (1.0 - rockM * 0.5));

	// ---- wet shoreline band and marsh
	float wetBand = 1.0 - Smooth01((h - 0.1 - 0.3 * nC) / 1.4);
	wetBand *= Smooth01((h + 3.0) / 2.0);
	// marsh: darker ground everywhere, standing water only in patches
	float pond = pondPre;
	col *= lerp(1.0, 0.78, saturate(wetC));
	float wet = saturate(max(wetBand, pond));
	col *= lerp(1.0, 0.55, wet);
	col = lerp(col, float3(0.03, 0.045, 0.04), pond * 0.8);
	rough = lerp(rough, 0.3, wetBand);
	rough = lerp(rough, 0.12, pond);
	N = normalize(lerp(N, up, pond));

	// ---- snow: by climate and height, slides off steep faces
	float snow = saturate(snowC * 1.4 - 0.2 + 0.35 * nB + 0.2 * nC);
	// snow does not hold on faces steeper than ~40 deg; rock shows through on ridges and cliffs
	snow *= 1.0 - Smooth01((steepMid - 0.20 - 0.08 * nC - 0.05 * nB) / 0.12);
	N = normalize(lerp(N, nMid, snow * 0.7)); // snow buries the small bumps
	col = lerp(col, float3(0.72, 0.74, 0.78) * (1.0 + 0.04 * nC + 0.04 * nF), snow);
	rough = lerp(rough, 0.55, snow);

	// ---- rivers. The mesh carries a signed distance across the nearest reach (linear across a straight
	// reach, so interpolation keeps the channel crisp even when vertices are wider apart than the river);
	// from orbit (or where the mesh is too coarse to follow meanders) the river map's coverage mips.
	float riv = 0.0;
	{
		const float texelM = 86.6;
		float2 ouv = OctUV(up);
		float covLod = log2(max(fp / texelM, 1.0));
		float cov = saturate(RiverTex.SampleLevel(RiverTexSampler, ouv, covLod).b * 1.2);
		// mid range: the map's distance field drawn as a line at least half a texel wide, dimmed so it
		// keeps the river's real share of the pixel (no crisp meanders, but a continuous line)
		float4 t0 = RiverTex.SampleLevel(RiverTexSampler, ouv, 0.0);
		float dS = t0.r * 400.0, hwS = t0.g * 64.0;
		float w2 = max(hwS, 45.0);
		float sdfLine = hwS > 0.5 ? saturate((w2 - dS) / max(0.5 * fp, 8.0) + 0.5) * saturate(1.4 * hwS / w2) : 0.0;
		float farMix = Smooth01((fp - 0.5 * texelM) / texelM);
		float rivMap = lerp(sdfLine, cov, farMix);
		float ad = abs(rivD);
		float w = max(rivW, 0.5 * fp);
		float edgeN = 0.1 * nD * rivW;
		float rivMesh = rivW > 0.5 ? saturate((w + edgeN - ad) / max(0.5 * fp, 0.15 * rivW) + 0.5) * min(1.0, rivW / w) : 0.0;
		float bank = rivW > 0.5 ? (1.0 - Smooth01((ad - 1.5 * rivW) / (0.6 * rivW))) * (1.0 - rivMesh) * Smooth01((rivW - fp) / rivW) : 0.0;
		float meshOk = 1.0 - Smooth01((S - 12.0) / 24.0);   // the mesh follows meanders below ~12 m spacing
		bank *= meshOk;
		riv = lerp(rivMap, rivMesh, meshOk) * Smooth01((h + 1.5) / 1.5); // not on the sea floor past the mouth
		col = lerp(col, col * 0.6, bank);
		rough = lerp(rough, 0.5, bank);
		float3 water = lerp(float3(0.03, 0.045, 0.04), float3(0.075, 0.065, 0.042), saturate(rivW / 35.0)); // big rivers carry silt
		col = lerp(col, water, riv);
		rough = lerp(rough, 0.07, riv);
		N = normalize(lerp(N, up, riv * Smooth01((rivW - 2.0 * fp) / rivW)));
	}

	// ---- city lights on the night side (placeholder mask until towns exist)
	float night = saturate(-dot(up, SunD) * 6.0 + 0.1);
	float speck = saturate((Noised(Pm / 110.0 + 3.7).x + Noised(Pm / 37.0).x * 0.5 - 0.05) * 4.0);
	Emis = lights * night * speck * float3(1.0, 0.56, 0.24) * 3.0;

	// The sky light is one cubemap captured above the ground under the camera; from orbit it would
	// light the night side too. Gate the ambient by the local sun elevation (twilight to -7 deg).
	AO = Smooth01((dot(up, SunD) + 0.12) / 0.3);
	Rough = rough;
	Nw = N;
	return saturate(col);
}
