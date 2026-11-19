// ---- shared by every surface material (pasted at the top of each Custom node) ----
// Mirror of Source/PlanetLab/SurfaceNoise.h. Keep the two identical.
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
// octave weight present in a mesh of vertex spacing S (same as SurfNoise::MeshWeight)
float MeshW(float L, float S) { return Smooth01((L / S - 4.0) / 4.0); }
// anti-aliasing weight of an octave of wavelength L for a pixel footprint fp (m)
float AAW(float L, float fp) { return Smooth01((L / fp - 2.0) / 3.0); }
float Hash01(int3 c) { return float(Hash(c) & 0xFFFFu) / 65535.0; }
// value of plain gradient noise, faded out when its wavelength L (m) is below the pixel footprint
float NoiseAA(float3 Pm, float L, float fp, float3 off) { return Noised(Pm / L + off).x * AAW(L, fp); }
