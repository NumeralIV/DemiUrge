// ---- ocean material (Single Layer Water; also an opaque fallback colour) ----
// Wave slope: a few long directional swells plus scrolling noise octaves, each faded when its
// wavelength drops below the pixel footprint; the lost slope variance goes into roughness.
float3 WaveGrad(float3 Pm, float3 up, float fp, float t, out float lostVar)
{
	float3 G = 0;
	lostVar = 0.0;
	// swells: 3D wave vectors (their tangential part sets the local direction)
	const float3 K0 = normalize(float3(0.8, 0.5, 0.33)), K1 = normalize(float3(-0.3, 0.9, 0.3)), K2 = normalize(float3(0.5, -0.2, 0.84));
	float3 Ks[3] = { K0, K1, K2 };
	float Ls[3] = { 117.0, 51.0, 23.0 };
	[unroll] for (int i = 0; i < 3; i++)
	{
		float L = Ls[i];
		// swell groups: amplitude modulated by slow noise so the crests do not read as stripes
		float grp = saturate(0.55 + 0.9 * Noised(Pm / (L * 7.0) + float3(3.1 * i, 1.7, 0.4 * t / L)).x);
		float slope = 0.06 * grp;
		float w = AAW(L, fp);
		float ph = frac(dot(Pm, Ks[i]) / L - t * sqrt(9.81 / (6.283 * L)) + 0.37 * i) * 6.283;
		float3 kt = Ks[i] - up * dot(Ks[i], up);
		G += kt * (slope * cos(ph) * w);
		lostVar += slope * slope * 0.5 * (1.0 - w);
	}
	// choppy noise octaves, drifting
	float3 p = Pm / 18.0 + float3(t * 0.07, t * 0.05, -t * 0.06);
	float3 q0 = float3(1, 0, 0), q1 = float3(0, 1, 0), q2 = float3(0, 0, 1);
	float L = 18.0, b = 1.0;
	for (int j = 0; j < 6; j++)
	{
		float w = AAW(L, fp);
		float a = 0.25 * b;
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

float3 Water(float3 P, float2 U0, float2 U1, float Tm, float3 SunD, out float Rough, out float3 Nw, out float Opac, out float3 Opq, out float AO)
{
	float3 up = normalize(P);
	AO = Smooth01((dot(up, SunD) + 0.12) / 0.3);
	float3 Pm = P * 0.01;
	float fp = max(length(ddx(P)), length(ddy(P))) * 0.01 + 0.02;
	float depth = U0.x;      // m of water under this point (negative on land)
	float lostVar;
	float3 g = WaveGrad(Pm, up, fp, Tm, lostVar);
	float calm = Smooth01((depth - 0.3) / 6.0);           // waves shoal and flatten in the last metres
	g *= lerp(0.35, 1.0, calm);
	float3 N = normalize(up - g);
	Rough = saturate(0.035 + sqrt(lostVar) * 1.6);

	// foam: swash line, surf bands running in over the shallows, sparse whitecaps offshore. The
	// masks are large features (never faded by distance); only their texture averages out.
	float nA = NoiseAA(Pm, 9.0, fp, float3(3.1, 7.7, 1.9));
	float nBr = Noised(Pm / 2.3 + float3(1.3, 2.2, 5.5)).x;
	float tex = lerp(0.62, saturate(0.62 + 0.9 * nBr), AAW(2.3, fp));
	float shore = 1.0 - Smooth01((depth - 0.05 - 0.25 * nA) / 0.9);
	float bphase = depth * 2.4 - Tm * 1.3 + nA * 2.5;
	float bw = max(abs(ddx(bphase)) + abs(ddy(bphase)), 1e-4);           // phase change per pixel
	float bandAA = saturate(1.0 - bw / 2.0);
	float band = Smooth01((sin(bphase) - 0.6) / 0.3);
	float surfZone = 1.0 - Smooth01((depth - 0.8) / 5.0);
	float bands = lerp(0.18, band, bandAA) * surfZone;
	float foam = saturate(shore * tex * 1.15 + bands * tex * 0.8);
	float capsTex = Noised(Pm / 25.0 + float3(0, Tm * 0.15, 0)).x * 0.7 + nA * 0.3;
	float caps = lerp(0.03, Smooth01((capsTex - 0.42) / 0.12) * 0.35, AAW(25.0, fp));
	foam = max(foam, caps * calm);
	Opac = saturate(foam);

	// opaque fallback colour: deep blue to turquoise shallows
	float tdep = saturate(depth / 40.0);
	Opq = lerp(float3(0.06, 0.20, 0.20), float3(0.005, 0.02, 0.05), sqrt(tdep));
	Opq = lerp(Opq, float3(0.8, 0.82, 0.8), Opac);

	Nw = N;
	return float3(0.85, 0.87, 0.88);
}
