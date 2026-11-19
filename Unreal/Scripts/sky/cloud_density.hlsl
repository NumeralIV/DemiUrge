// DEMIURGE sky (C2): weather-driven volumetric cloud density. Built on the CloudLab density function
// (docs/lab/clouds.md 1.3) with these production changes:
//  * the planet turns: world positions are rotated into the planet frame by -Yaw before any lookup;
//  * weather moves: the sky actor rebuilds the weather maps (sim fields + moving weather systems) about once a
//    second on a worker thread; two generations are crossfaded here (Blend);
//  * the cyclones' spirals cost nothing here: the warp map holds the offset of the cell-texture lookup, so the
//    static cell texture (Fine) is sheared into spiral bands by one extra fetch, and advected along the wind
//    belts with a bounded two-phase flow map;
//  * optical thickness varies (thin trade cumulus and veils are translucent, frontal sheets and storm cores
//    opaque) and a thin cirrus layer rides the top of the layer (jet streaks, storm outflow, frontal shields);
//  * no emission (measured +4 ms on the view pass): lightning flashes are additive glows drawn over the clouds.
// Returns float4(density, albedo scale, sky AO, conservative density).
//
// WeatherA/B (RGBA8 equirect, planet frame): R cover, G cloud-top height, B cumuliform, A optical thickness
// (> 0.62 = hurricane core: cirrus canopy). WarpA/B (RGBA16F): RG lookup offset (uv) of the cell texture,
// B cirrus. Fine (RGBA8, static, mipped): R/G two independent sets of cloud-cell noise (0.5 = 0).
// ShapeTex 128^3 / DetailTex 64^3: tileable Perlin-Worley / Worley volumes.
Glow = 0.0;
float3 P = WP;
float cy = cos(Yaw), sy = sin(Yaw);
float3 Pp = float3(P.x * cy + P.y * sy, -P.x * sy + P.y * cy, P.z);    // planet frame
float3 Dw = normalize(P);
float3 D = normalize(Pp);
float lat = asin(clamp(D.z, -1.0, 1.0));
float2 uv = float2(frac(atan2(D.y, D.x) * 0.15915494 + 0.5), 0.5 - lat * 0.31830989);
float4 w = Texture2DSampleLevel(WeatherB, WeatherBSampler, uv, 0.0);
float4 v = Texture2DSampleLevel(WarpB, WarpBSampler, uv, 0.0);
if (Blend < 0.999)
{
    w = lerp(Texture2DSampleLevel(WeatherA, WeatherASampler, uv, 0.0), w, Blend);
    v = lerp(Texture2DSampleLevel(WarpA, WarpASampler, uv, 0.0), v, Blend);
}
float C = saturate(w.r * CoverScale), top0 = w.g, cum0 = w.b, thk0 = w.a;
float ci = saturate(v.z * CirrusGain) * CoverScale;
float top = lerp(0.12, 1.0, top0);
bool bCi = ci > 0.004 && Alt > CirrusLo;
// early outs: above every possible cloud top here; clear air that even the strongest cell noise cannot reach
if (Alt >= top && !bCi) return float4(0.0, 1.0, 1.0, 0.0);
// hurricane cores (thk0 > 0.62) are a smooth cirrus-topped deck: the cell texture there only roughens the top, and at
// low sun every bump casts a shadow (the dotted, dark CDO at dusk). Mute it under the canopy.
float coreS = smoothstep(0.62, 0.9, thk0);
float amp = lerp(0.35, 1.1, cum0) * (1.0 - 0.75 * coreS);
float bound = 0.1 + 0.9 * C + amp * 0.6 * NfGain * (0.5 + C);
if (bound * CoverGain + CoverBias < VeilStart - 0.25 * NoiseAmp && !bCi) return float4(0.0, 1.0, 1.0, 0.0);

// ---- fine cloud cells at the warped lookup, advected along the wind belts (positive = increasing sim
// longitude = westward): trades and polar easterlies westward, mid-latitude westerlies eastward. Two-phase
// flow map: each phase slides one noise set for FlowPeriod seconds and restarts at a fresh random longitude
// while its weight is 0.
float2 uvq = uv + v.xy;
float alat = abs(lat) * 57.29578;
float u = WindTrade * (1.0 - smoothstep(20.0, 32.0, alat)) - WindWest * smoothstep(22.0, 35.0, alat) * (1.0 - smoothstep(55.0, 68.0, alat))
        + WindPolar * smoothstep(60.0, 72.0, alat);
float ph1 = SkyTime / FlowPeriod, ph2 = ph1 + 0.5;
float t1 = frac(ph1), t2 = frac(ph2);
float o1 = frac(floor(ph1) * 0.61803399), o2 = frac(floor(ph2) * 0.75487767 + 0.31);
float2 uv1 = float2(uvq.x + o1 - u * t1 * FlowPeriod * 0.15915494, uvq.y);
float2 uv2 = float2(uvq.x + o2 - u * t2 * FlowPeriod * 0.15915494, uvq.y);
float w1 = 1.0 - abs(2.0 * t1 - 1.0), w2 = 1.0 - w1;
float camKm = length(P - CamPos) * 1e-5;
// cells finer than a traced pixel turn into speckle: read a blurred mip far away (and keep a little contrast)
float fmip = clamp(log2(max(camKm, 1.0) / FineMipKm) + FineMipBias, 0.0, 4.0);
float wn = 1.0 / max(sqrt(w1 * w1 + w2 * w2), 0.5);
float Nf = ((Texture2DSampleLevel(Fine, FineSampler, uv1, fmip).r - 0.5) * w1 + (Texture2DSampleLevel(Fine, FineSampler, uv2, fmip).g - 0.5) * w2) * 2.0 * wn * NfGain
         * (1.0 + FineMipBoost * fmip);
// Presence field: 0.5 is the cloud edge (heaps become separate puffs, sheets stay solid with holes)
float pres = saturate(0.1 + 0.9 * C + amp * Nf * (0.5 + C));
float presb = saturate(0.1 + 0.9 * C + 0.25 * amp * (0.5 + C));   // smooth field for tower heights (full-res cells make spiky towers)
float cov = saturate(pres * CoverGain + CoverBias);
float cum = cum0;
float hn = Alt / top;

float3 pk = Pp * 1e-5;                                 // km, planet frame (the noise turns with the planet)
// Distance LOD: sub-pixel noise reads as grey speckle from orbit; far away the maps carry the look.
float lod = saturate((camKm - LodNearKm) / (LodFarKm - LodNearKm));
bool bShadow = ShadowDist > 0.0;

// ---- cirrus: a thin, translucent, fibrous sheet near the top of the layer
float dci = 0.0;
if (bCi)
{
    float ch = saturate((Alt - CirrusLo) / (1.0 - CirrusLo));
    float bell = smoothstep(0.0, 0.35, ch) * (1.0 - smoothstep(0.65, 1.0, ch));
    // fibres: up close, ridged 3D noise stretched along the (warped) latitude circles -- seamless cylinder
    // coordinates, so the streaks follow the jet and spiral out of storms; far away the cell texture breaks it up
    float fibFar = saturate(0.45 + 1.4 * Nf);
    float fib = fibFar;
    if (lod < 0.99 && !bShadow)
    {
        float lonq = (uvq.x - 0.5) * 6.2831853, latq = (0.5 - uvq.y) * 3.14159265;
        float3 q = float3(cos(lonq) * (100.0 / CirrusLenKm), sin(lonq) * (100.0 / CirrusLenKm), latq * (100.0 / CirrusWidKm)) + float3(SkyTime * 0.003, 0.0, 0.0);
        float fa = Texture3DSampleLevel(ShapeTex, ShapeTexSampler, q, 0.0).r;
        float fb = Texture3DSampleLevel(ShapeTex, ShapeTexSampler, q * 2.7 + 0.41, 0.0).r;
        float ridge = 1.0 - abs(2.0 * (0.65 * fa + 0.35 * fb) - 1.0);
        float fil = saturate(ridge * ridge * ridge * 1.8) * saturate(0.6 + 1.2 * Nf);
        fib = lerp(fil, fibFar, lod);
    }
    dci = ci * bell * fib * CirrusDens * lerp(0.5, 1.0, lod);   // seen from close above, cirrus is a faint veil
}

float d = 0.0;
if (cov >= VeilStart - 0.25 * NoiseAmp && hn < 1.0)
{
    float noiseAmp = NoiseAmp * lerp(1.0, FarNoise, lod);
    float edgeSharp = EdgeSharp * lerp(1.0, FarSharp, lod);
    float3 drift = float3(SkyTime * WindKmS, SkyTime * WindKmS * 0.37, 0.0);
    float4 s = Texture3DSampleLevel(ShapeTex, ShapeTexSampler, (pk + drift) / ShapeKm, 0.0);
    float4 s2 = Texture3DSampleLevel(ShapeTex, ShapeTexSampler, (pk + drift * 0.5) / MacroKm + 0.37, 0.0);
    float n = s.r + (s2.r - 0.5) * MacroAmt - 0.5;
    if (lod < 0.99 && !bShadow)
    {
        float4 s3 = Texture3DSampleLevel(ShapeTex, ShapeTexSampler, (pk + drift) / (ShapeKm * 0.2) + 0.61, 0.0);
        n += (s3.r - 0.5) * MidAmt * (1.0 - lod);
    }
    float F = cov + n * noiseAmp * lerp(0.35, 1.0, cum);
    // Cirrus canopy over deep convection (hurricane CDO): a smooth bright sheet hiding the towers. Deep and
    // moderately dense rather than thin and dense: a thin dense sheet shows the tracer's jitter as speckle.
    float canopy = smoothstep(0.62, 0.9, thk0) * smoothstep(CanopyLo, CanopyLo + 0.2, Alt) * smoothstep(1.0, 0.9, Alt)
                 * saturate((F - 0.35) * 3.0) * CanopyAmt * (0.85 + 0.3 * s2.r);   // smooth: relief here self-shadows into speckle at low sun
    // Local tops: towers where the (blurred) field is strong, low clouds near the edges
    float localTop = top * lerp(1.0, saturate(TowerBase + TowerGain * (presb - 0.5) + 0.6 * (s.r - 0.5)), cum);
    float hl = Alt / max(localTop, 0.02);
    if (hl < 1.0)
    {
        float baseRamp = saturate(hl / lerp(0.22, 0.06, cum));
        float topRamp = saturate((1.0 - hl) / lerp(0.45, 0.7, cum));
        float prof = baseRamp * sqrt(topRamp);
        d = saturate((F * prof - 0.5) * edgeSharp);
        float vF = saturate((F - VeilStart) / (0.5 - VeilStart));
        d = max(d, vF * vF * saturate(hl * 4.0) * saturate((1.0 - hl) * 3.0) * VeilAmt * lerp(0.25, 1.0, lod));
        if (d > 0.0 && !bShadow && lod < 0.99)
        {
            float4 e = Texture3DSampleLevel(DetailTex, DetailTexSampler, (pk + drift * 1.6) / DetailKm, 0.0);
            float ef = e.r * 0.625 + e.g * 0.25 + e.b * 0.125;
            float er = lerp(1.0 - ef, ef, saturate(hl * 2.5));
            float amt = DetailAmt * lerp(0.3, 1.0, cum) * (1.0 - lod);
            d = saturate((d - er * amt) / max(1.0 - er * amt, 0.05));
        }
    }
    // optical thickness: thin regimes (trade cumulus, veils) translucent, frontal sheets and cores opaque
    // (only far away: up close small cumulus are opaque puffs; from orbit they are sub-pixel and read as a faint texture)
    float thick = lerp(lerp(1.0, ThinDens, lod), 1.0, saturate(thk0 * 2.2)) * (1.0 + ThickBoost * thk0);
    d = max(d * thick, canopy);
}
d += dci;
if (d <= 0.0) return float4(0.0, 1.0, 1.0, 0.0);

// Sky-light occlusion: the engine's distant sky light is one value for the whole planet; fade it on the night side.
float day = max(smoothstep(-0.10, 0.30, dot(Dw, SunDir)), NightAmb);   // moonlight stands in for the sky at night
float ao = lerp(0.45, 1.0, saturate(min(Alt / top, 1.0) * 1.3)) * day * SkyAmb;
// molten genesis (M_SkyCloudsMolten only): the ash veil lit from below by the magma
Glow = d * (1.0 - smoothstep(0.0, 0.7, Alt / max(top, 0.05)));
return float4(d, 1.0, ao, max(cov, ci));
