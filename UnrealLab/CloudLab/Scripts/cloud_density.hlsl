// CloudLab weather-driven cloud density. Returns float4(density, albedo scale, sky AO, conservative density).
// WP: absolute world position (cm, planet centred at the origin). Alt: normalised altitude in the cloud layer.
float3 P = WP;
float3 D = normalize(P);
float lon = atan2(D.y, D.x) - LonOffset;
float lat = asin(clamp(D.z, -1.0, 1.0));
float2 uv = float2(frac(lon * 0.15915494 + 0.5), 0.5 - lat * 0.31830989);
float4 w = Texture2DSampleLevel(Weather, WeatherSampler, uv, 0.0);
float cov = saturate(w.r * CoverGain + CoverBias);  // presence field, 0.5 = edge
float top = lerp(0.12, 1.0, w.g);
float cum = w.b;
float hn = Alt / top;
if (cov < VeilStart - 0.25 * NoiseAmp || hn >= 1.0) return float4(0.0, 1.0, 1.0, 0.0);

float3 pk = P * 1e-5;                                  // km
// Distance LOD: noise finer than a pixel turns into grey speckle from orbit, so fade the 3D noise and the
// detail erosion out with camera distance; far away the weather map's own texture carries the look.
float lod = saturate((length(P - CamPos) * 1e-5 - LodNearKm) / (LodFarKm - LodNearKm));
float noiseAmp = NoiseAmp * lerp(1.0, FarNoise, lod);
float edgeSharp = EdgeSharp * lerp(1.0, FarSharp, lod);
float3 drift = float3(Time * WindKmS, Time * WindKmS * 0.37, 0.0);
float4 s = Texture3DSampleLevel(ShapeTex, ShapeTexSampler, (pk + drift) / ShapeKm, 0.0);
float4 s2 = Texture3DSampleLevel(ShapeTex, ShapeTexSampler, (pk * float3(1.0, 1.0, 1.0) + drift * 0.5) / MacroKm + 0.37, 0.0);
// The weather map R is a presence field: 0.5 is the cloud edge, its own fine noise already carries the cells.
// The 3D noise (s.r is histogram-equalised, mean 0.5) only perturbs it, so the weather structure survives.
float n = s.r + (s2.r - 0.5) * MacroAmt - 0.5;
// Near the camera add a mid-frequency octave (ShapeKm/5) so cloud outlines break up below the map's texel size.
// ShadowDist > 0 for every shadow evaluation (secondary rays AND the cloud shadow map pass): use the cheap
// shape there. The shadow map pass otherwise costs 3-5 ms near the ground at full detail.
bool bShadow = ShadowDist > 0.0;
if (lod < 0.99 && !bShadow)
{
    float4 s3 = Texture3DSampleLevel(ShapeTex, ShapeTexSampler, (pk + drift) / (ShapeKm * 0.2) + 0.61, 0.0);
    n += (s3.r - 0.5) * MidAmt * (1.0 - lod);
}
float F = cov + n * noiseAmp * lerp(0.35, 1.0, cum);
// Cirrus canopy over deep convection (hurricane CDO, ITCZ clusters): a thin, smooth, bright sheet at the top
// of the layer that hides the lumpy towers underneath, as in satellite photos of storms.
float canopy = smoothstep(0.62, 0.9, w.a) * smoothstep(0.78, 0.9, Alt) * smoothstep(1.0, 0.94, Alt)
             * saturate((F - 0.35) * 3.0) * CanopyAmt * (0.6 + 0.8 * s2.r);   // fibrous, not uniform
// Local tops: where the field is strong the column grows (towers); near the edges clouds stay low.
// For heaps this gives individual turrets of different heights instead of a flat-topped slab.
float4 wb = Texture2DSampleLevel(Weather, WeatherSampler, uv, TopMip);   // ~1 km blurred cover
float localTop = top * lerp(1.0, saturate(TowerBase + TowerGain * (wb.r - 0.5) + 0.6 * (s.r - 0.5)), cum);
float hl = Alt / max(localTop, 0.02);
float d = 0.0;
if (hl < 1.0)
{
    float baseRamp = saturate(hl / lerp(0.22, 0.06, cum));
    float topRamp = saturate((1.0 - hl) / lerp(0.45, 0.7, cum));
    float prof = baseRamp * sqrt(topRamp);
    d = saturate((F * prof - 0.5) * edgeSharp);
    // Veil: thin, semi-transparent cloud around and between the masses (what makes satellite clouds look soft).
    float vF = saturate((F - VeilStart) / (0.5 - VeilStart));
    d = max(d, vF * vF * saturate(hl * 4.0) * saturate((1.0 - hl) * 3.0) * VeilAmt * lerp(0.25, 1.0, lod)); // haze reads as fog up close
    // Detail erosion: wispy at the base, billowy at the top. Skipped on long shadow rays and far away.
    if (d > 0.0 && !bShadow && lod < 0.99)
    {
        float4 e = Texture3DSampleLevel(DetailTex, DetailTexSampler, (pk + drift * 1.6) / DetailKm, 0.0);
        float ef = e.r * 0.625 + e.g * 0.25 + e.b * 0.125;
        float er = lerp(1.0 - ef, ef, saturate(hl * 2.5));
        float amt = DetailAmt * lerp(0.3, 1.0, cum) * (1.0 - lod);
        d = saturate((d - er * amt) / max(1.0 - er * amt, 0.05));
    }
}
d = max(d, canopy);
if (d <= 0.0) return float4(0.0, 1.0, 1.0, cov);
float thick = 1.0 + ThickBoost * w.a;
// Sky-light occlusion. The engine's distant sky light is one value for the whole planet, so fade it out
// on the night side ourselves, or night clouds glow with daylight ambient.
float day = smoothstep(-0.10, 0.30, dot(D, SunDir));
float ao = lerp(0.45, 1.0, saturate(min(hl, 1.0) * 1.3)) * day * SkyAmb;
return float4(d * thick, 1.0, ao, cov);
