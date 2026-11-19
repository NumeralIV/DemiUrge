// ---- DEMIURGE planet materials: shared by every surface material (pasted into each Custom node) ----
// Owner: C1 planet. Mirror of Source/Demiurge/Game/Planet/DmPlanetNoise.h: keep the two identical (the land
// material continues exactly the relief octaves the mesh of a given LOD level leaves out).
uint Hash(int3 c)
{
	uint3 u = asuint(c);
	uint h = (u.x * 73856093u) ^ (u.y * 19349663u) ^ (u.z * 83492791u);
	h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
	return h;
}
float3 Grad(uint h)
{
	h = h % 12u;
	float a = (h & 1u) ? -1.0 : 1.0;
	float b = (h & 2u) ? -1.0 : 1.0;
	return h < 4u ? float3(a, b, 0) : (h < 8u ? float3(a, 0, b) : float3(0, a, b));
}
float4 Noised(float3 p)
{
	float3 i = floor(p); float3 f = p - i;
	float3 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
	float3 du = 30.0 * f * f * (f * (f - 2.0) + 1.0);
	int3 c = int3(i);
	float3 ga = Grad(Hash(c)), gb = Grad(Hash(c + int3(1, 0, 0))), gc = Grad(Hash(c + int3(0, 1, 0))), gd = Grad(Hash(c + int3(1, 1, 0)));
	float3 ge = Grad(Hash(c + int3(0, 0, 1))), gf = Grad(Hash(c + int3(1, 0, 1))), gg = Grad(Hash(c + int3(0, 1, 1))), gh = Grad(Hash(c + int3(1, 1, 1)));
	float va = dot(ga, f), vb = dot(gb, f - float3(1, 0, 0)), vc = dot(gc, f - float3(0, 1, 0)), vd = dot(gd, f - float3(1, 1, 0));
	float ve = dot(ge, f - float3(0, 0, 1)), vf = dot(gf, f - float3(1, 0, 1)), vg = dot(gg, f - float3(0, 1, 1)), vh = dot(gh, f - float3(1, 1, 1));
	float k1 = vb - va, k2 = vc - va, k3 = ve - va, k4 = va - vb - vc + vd, k5 = va - vc - ve + vg, k6 = va - vb - ve + vf, k7 = -va + vb + vc - vd + ve - vf - vg + vh;
	float v = va + u.x * k1 + u.y * k2 + u.z * k3 + u.x * u.y * k4 + u.y * u.z * k5 + u.z * u.x * k6 + u.x * u.y * u.z * k7;
	float3 g = ga + u.x * (gb - ga) + u.y * (gc - ga) + u.z * (ge - ga) + u.x * u.y * (ga - gb - gc + gd) + u.y * u.z * (ga - gc - ge + gg)
		+ u.z * u.x * (ga - gb - ge + gf) + u.x * u.y * u.z * (-ga + gb + gc - gd + ge - gf - gg + gh);
	float3 d = g + du * float3(k1 + u.y * k4 + u.z * k6 + u.y * u.z * k7, k2 + u.z * k5 + u.x * k4 + u.z * u.x * k7, k3 + u.x * k6 + u.y * k5 + u.x * u.y * k7);
	return float4(v, d);
}
float3 Rot(float3 p) { return float3(-0.80 * p.y - 0.60 * p.z, 0.80 * p.x + 0.36 * p.y - 0.48 * p.z, 0.60 * p.x - 0.48 * p.y + 0.64 * p.z); }
float Smooth01(float t) { t = saturate(t); return t * t * (3.0 - 2.0 * t); }
// octave weight present in a mesh of vertex spacing S (same as DmPlanetNoise::MeshWeight)
float MeshW(float L, float S) { return Smooth01((L / S - 4.0) / 4.0); }
// anti-aliasing weight of an octave of wavelength L for a pixel footprint fp (m)
float AAW(float L, float fp) { return Smooth01((L / fp - 2.0) / 3.0); }
float Hash01(int3 c) { return float(Hash(c) & 0xFFFFu) / 65535.0; }
// value of plain gradient noise, faded out when its wavelength L (m) is below the pixel footprint
float NoiseAA(float3 Pm, float L, float fp, float3 off) { return Noised(Pm / L + off).x * AAW(L, fp); }

// Signed distance (m) from the river map's 8-bit encoding (DmPlanetMaps::EncodeSigned): linear within
// +-125 m, compressed beyond, +-400 m at the ends
float DecodeSigned(float e)
{
	float u = (e * 255.0 - 128.0) / 254.0;
	float a = abs(u);
	float d = a <= 0.375 ? a / 0.375 * 125.0 : 125.0 + (a - 0.375) / 0.125 * 275.0;
	return u < 0.0 ? -d : d;
}

// Octahedral mapping of a unit planet-frame direction (DmPlanetOct::UV)
float2 OctUV(float3 n)
{
	n /= (abs(n.x) + abs(n.y) + abs(n.z));
	float2 p = n.xy;
	if (n.z < 0.0) p = float2((1.0 - abs(n.y)) * (n.x >= 0.0 ? 1.0 : -1.0), (1.0 - abs(n.x)) * (n.y >= 0.0 ? 1.0 : -1.0));
	return p * 0.5 + 0.5;
}

// 3D cellular noise: x = distance to the nearest feature point (cell units), y = to the second nearest,
// z = random of the nearest cell, and the offset to the nearest point (cell units).
float3 Cells(float3 p, int3 salt, out float3 off, out float rnd2)
{
	off = 0; rnd2 = 0;
	float3 i = floor(p);
	float b1 = 9.0, b2 = 9.0; float rnd = 0.0;
	for (int z = -1; z <= 1; z++)
		for (int y = -1; y <= 1; y++)
			for (int x = -1; x <= 1; x++)
			{
				int3 c = int3(i) + int3(x, y, z);
				uint h = Hash(c + salt);
				float3 o = float3(h & 0x3FFu, (h >> 10) & 0x3FFu, (h >> 20) & 0x3FFu) / 1023.0;
				float3 r = float3(c) + 0.15 + 0.7 * o - p;
				float d2 = dot(r, r);
				if (d2 < b1) { b2 = b1; b1 = d2; rnd = float((h >> 7) & 0xFFu) / 255.0; rnd2 = float((h >> 15) & 0xFFu) / 255.0; off = -r; }
				else if (d2 < b2) { b2 = d2; }
			}
	return float3(sqrt(b1), sqrt(b2), rnd);
}

// planet frame <-> world (RX, RY, RZ = the planet's axes in world space)
float3 ToPlanet(float3 v, float3 RX, float3 RY, float3 RZ) { return float3(dot(RX, v), dot(RY, v), dot(RZ, v)); }
float3 ToWorld(float3 v, float3 RX, float3 RY, float3 RZ) { return v.x * RX + v.y * RY + v.z * RZ; }
