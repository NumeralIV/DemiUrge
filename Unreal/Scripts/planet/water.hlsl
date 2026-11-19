// ---- DEMIURGE water material (Single Layer Water; Custom node body). Owner: C1 planet.
// Port of UnrealLab/SurfaceLab water.hlsl plus sea ice and frozen lakes (planet map B), river silt plumes
// at river mouths (per-pixel scattering), and lakes (U1.y = 1).
// Vertex data: U0 = (water depth m, signed river distance m), U1 = (log2 vertex spacing, lake flag).

// Wave slope: long directional swells plus scrolling noise octaves, each faded when its wavelength drops
// below the pixel footprint; the lost slope variance goes into roughness (wide sun glint from orbit).
float3 WaveGrad(float3 Pm, float3 up, float fp, float t, float amp, out float lostVar)
{
	float3 G = 0;
	lostVar = 0.0;
	const float3 K0 = normalize(float3(0.8, 0.5, 0.33)), K1 = normalize(float3(-0.3, 0.9, 0.3)), K2 = normalize(float3(0.5, -0.2, 0.84));
	float3 Ks[3] = { K0, K1, K2 };
	float Ls[3] = { 117.0, 51.0, 23.0 };
	[unroll] for (int i = 0; i < 3; i++)
	{
		float L = Ls[i];
		// wave groups (amplitude) and bent crests (phase warp), so the swell never reads as ruled stripes
		float grp = saturate(0.45 + 1.1 * Noised(Pm / (L * 6.0) + float3(3.1 * i, 1.7, 0.4 * t / L)).x);
		float slope = (0.034 - 0.006 * i) * grp * amp;
		float w = AAW(L, fp);
		float warp = 1.6 * Noised(Pm / (L * 3.3) + float3(1.9, 4.4 * i, 0.7)).x + 0.5 * Noised(Pm / (L * 1.3) + float3(7.7 * i, 2.1, 5.3)).x;
		float ph = frac(dot(Pm, Ks[i]) / L - t * sqrt(9.81 / (6.283 * L)) + 0.37 * i + warp) * 6.283;
		float3 kt = Ks[i] - up * dot(Ks[i], up);
		G += kt * (slope * cos(ph) * w);
		lostVar += slope * slope * 0.5 * (1.0 - w);
	}
	float3 p = Pm / 18.0 + float3(t * 0.07, t * 0.05, -t * 0.06);
	float3 q0 = float3(1, 0, 0), q1 = float3(0, 1, 0), q2 = float3(0, 0, 1);
	float L = 18.0, b = 1.0;
	for (int j = 0; j < 6; j++)
	{
		float w = AAW(L, fp);
		float a = 0.25 * b * amp;
		if (w > 0.0)
		{
			float4 n = Noised(p);
			float3 g = float3(dot(q0, n.yzw), dot(q1, n.yzw), dot(q2, n.yzw));
			g -= up * dot(g, up);
			G += g * (a * w * 0.5);
		}
		lostVar += a * a * 0.04 * (1.0 - w);
		p = Rot(p) * 2.0 + float3(0.0, t * 0.11 * (j + 1), 0.0); q0 = Rot(q0); q1 = Rot(q1); q2 = Rot(q2);
		L *= 0.5; b *= 0.75;
	}
	return G;
}

float3 Water(float3 Pw, float2 U0, float2 U1, float Tm, float3 SunW, float3 RX, float3 RY, float3 RZ, float3 Ctr4,
	Texture2D MapB, SamplerState MapBSampler, Texture2D MapD, SamplerState MapDSampler,
	out float Rough, out float3 Nw, out float Opac, out float3 Opq, out float AO, out float3 Scat, out float3 Absorb)
{
	float3 P = ToPlanet(Pw - Ctr4, RX, RY, RZ);
	float3 SunD = ToPlanet(SunW, RX, RY, RZ);
	float3 up = normalize(P);
	AO = Smooth01((dot(up, SunD) + 0.12) / 0.3);
	float3 Pm = P * 0.01;
	float fp = max(length(ddx(P)), length(ddy(P))) * 0.01 + 0.02;
	float depth = U0.x;
	float rivD = U0.y;
	float lake = U1.y;

	float2 ouv = OctUV(up);
	float lodS = log2(max(fp / 346.0, 1.0));
	float4 mB = MapB.SampleLevel(MapBSampler, ouv, lodS);
	float tempC = mB.r * 90.0 - 50.0;
	float iceC = mB.a;
	float lava = MapD.SampleLevel(MapDSampler, ouv, lodS).g;

	float lostVar;
	float calm = Smooth01((depth - 0.3) / 6.0);
	float amp = lerp(1.0, 0.45, lake);                                     // lakes are calmer
	float3 g = WaveGrad(Pm, up, fp, Tm, amp, lostVar);
	g *= lerp(0.35, 1.0, calm);

	// ---- foam: swash line, surf bands over the shallows, sparse whitecaps (masks never faded by distance)
	float nA = NoiseAA(Pm, 9.0, fp, float3(3.1, 7.7, 1.9));
	float nBig = NoiseAA(Pm, 900.0, fp, float3(1.1, 4.2, 2.7));
	float nBr = Noised(Pm / 2.3 + float3(1.3, 2.2, 5.5)).x;
	float tex = lerp(0.62, saturate(0.62 + 0.9 * nBr), AAW(2.3, fp));
	// Swash: a band a few metres to ~15 m wide measured along the ground (depth / depth gradient), not a
	// band of depth, which on a gentle shelf would be hundreds of metres wide and outline every coast in
	// white from orbit. When the band is thinner than a pixel it averages to its share of the pixel.
	float dzS = max(max(abs(ddx(depth)), abs(ddy(depth))) / max(fp, 0.05), 0.002);
	float distM = max(depth, 0.0) / dzS;
	float swashW = 4.0 + 9.0 * saturate(0.5 + nA + 0.5 * nBig);
	float shore = (1.0 - Smooth01((distM - swashW * (0.6 + 0.4 * nA)) / max(0.4 * swashW, fp))) * (1.0 - Smooth01((depth - 1.2) / 0.8));
	shore *= saturate(swashW / max(fp, 1.0));
	float bphase = depth * 2.4 - Tm * 1.3 + nA * 2.5;
	float bw = max(abs(ddx(bphase)) + abs(ddy(bphase)), 1e-4);
	float bandAA = saturate(1.0 - bw / 2.0);
	float band = Smooth01((sin(bphase) - 0.6) / 0.3);
	// the surf zone is a band of a few metres of depth; on gentle shelves that can be wide, so the bands also
	// need a steep enough bottom (depth change per metre from the pixel derivatives)
	float dz = max(abs(ddx(depth)), abs(ddy(depth))) / max(fp, 0.05);       // depth gradient per metre
	float steepB = Smooth01((dz - 0.004) / 0.02);
	float surfZone = (1.0 - Smooth01((depth - 0.5) / 2.5)) * (1.0 - lake) * lerp(0.35, 1.0, steepB);
	float bands = lerp(0.05, band, bandAA) * surfZone * saturate(0.5 + 0.9 * nBig);
	float foam = saturate(shore * tex * lerp(1.0, 0.25, lake) + bands * tex * 0.7);
	float capsTex = Noised(Pm / 25.0 + float3(0, Tm * 0.15, 0)).x * 0.7 + nA * 0.3;
	float caps = lerp(0.03, Smooth01((capsTex - 0.42) / 0.12) * 0.35, AAW(25.0, fp)) * (1.0 - lake);
	foam = max(foam, caps * calm);

	// ---- sea ice and frozen lakes: floes with open leads, smooth and bright
	float iceCov = lerp(Smooth01((iceC - 0.18) / 0.45), Smooth01((-6.0 - tempC) / 5.0), lake);
	float iceM = 0.0;
	float3 iceCol = float3(0.80, 0.84, 0.88);
	if (iceCov > 0.001)
	{
		float3 offI; float r2;
		float3 fc = Cells(Pm / 160.0, int3(3, 77, 41), offI, r2);
		float floe = Smooth01((iceCov * 1.25 - 0.25 + 0.35 * fc.z + 0.25 * nBig - 0.5 * (1.0 - Smooth01((fc.y - fc.x) / 0.12)) * AAW(160.0, fp)) / 0.12);
		iceM = lerp(iceCov, floe, AAW(160.0, fp));
		iceCol *= 0.9 + 0.15 * r2 + 0.06 * nA;
		g *= 1.0 - iceM;
	}
	float3 N = normalize(up - g);
	// Unresolved wave slopes become roughness. Cox-Munk: a 5 m/s sea has a mean square slope near 0.03, a
	// GGX roughness of about 0.45-0.5, so from orbit the sun glint is a broad, soft bright area (a nearly
	// mirror ocean would give a pin-point glint and noisy reflections instead).
	Rough = lerp(min(0.035 + sqrt(lostVar) * 4.5, 0.4), 0.45, iceM);
	Opac = saturate(max(foam * (1.0 - iceM), iceM));

	// ---- river silt plumes at the mouths, greener lakes (per-pixel scattering / absorption, cm^-1)
	float plume = (1.0 - Smooth01((abs(rivD) - 150.0) / 1700.0)) * (1.0 - Smooth01((depth - 4.0) / 40.0)) * (1.0 - lake) * saturate(0.75 + 0.5 * nBig);
	// Shelf seas: fine sediment and carbonate in the shallow water over the continental shelf scatter green-
	// blue light, so islands and coasts sit in a turquoise halo from orbit (strongest in warm water).
	float warm = saturate((tempC - 6.0) / 16.0);
	float shelf = (1.0 - Smooth01((depth - 6.0 - 20.0 * warm) / (40.0 + 40.0 * warm))) * (1.0 - lake) * saturate(0.55 + 0.6 * nBig) * lerp(0.35, 1.0, warm);
	// (integration) a touch more blue in the open ocean: from orbit the deep sea read grey-navy once the aerial
	// perspective was thinned so the forests keep their green
	float3 scatDeep = lerp(float3(0.00002, 0.00009, 0.00019), float3(0.00008, 0.00042, 0.00048), shelf);
	Scat = lerp(scatDeep, float3(0.0016, 0.0014, 0.0008), plume);
	Absorb = lerp(float3(0.0040, 0.00065, 0.00025), float3(0.0045, 0.0016, 0.0012), plume);
	Scat = lerp(Scat, float3(0.00003, 0.00008, 0.00007), lake);
	Absorb = lerp(Absorb, float3(0.0055, 0.0016, 0.0012), lake);   // fresh water: tannins and algae, darker
	// boiling where lava runs into the water
	Opac = max(Opac, saturate(lava * 2.0) * 0.6);

	// opaque fallback colour: deep blue to turquoise shallows
	float tdep = saturate(depth / 40.0);
	Opq = lerp(float3(0.06, 0.20, 0.20), float3(0.005, 0.02, 0.05), sqrt(tdep));
	Opq = lerp(Opq, float3(0.8, 0.82, 0.8), Opac);

	Nw = normalize(ToWorld(N, RX, RY, RZ));
	return lerp(float3(0.85, 0.87, 0.88), iceCol, iceM);
}
