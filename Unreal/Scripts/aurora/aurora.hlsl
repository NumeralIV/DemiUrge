// DEMIURGE aurora (wave 3 'aurora' track): curtains of light over both magnetic poles, ray-marched.
//
// Pasted inside "struct FAur { ... };" by Scripts/aurora.py; the Custom node calls F.Shade(...).
// The mesh is an open spherical cap of radius Rkm + TopKm around each magnetic pole (DmSkyAurora.cpp);
// a fragment only starts a march along its view ray through the aurora slab (Rkm + BaseKm - 0.8 ..
// Rkm + TopKm), clipped by the sea sphere. Everything is in the PLANET frame and in km: PC is the planet's
// centre and PX / PY / PZ its axes in world space (the planet turns), positions in uu.
// Debug (-auroradebug, Anim.w < 0): every early-out returns a tint instead of black (see Shade).
//
// The look (reference: curtains photographed from the ground and from low orbit):
//   - three folded arcs around an oval (colatitude about 19 deg, wider toward local midnight), each a thin
//     vertical sheet: a sharp lower border near BaseKm, green (557.7 nm) emission decaying upward, violet
//     tops several km higher, vertical rays (striations along the arc, the tall ones reaching higher);
//   - a faint diffuse glow equatorward of the arcs (what reads as the "oval" from far away);
//   - brightest around local midnight, gone on the day side (the sun's elevation at every sample);
//   - a pink lower fringe when surged (Boost).
// Sheets are widened to the march step (with their brightness scaled so the integral is kept), so grazing
// rays at the limb are smooth instead of aliased streaks; the result is soft-clamped in luminance so an
// edge-on curtain cannot blow out into a white line.

float Hash(float x) { x = frac(x * 0.1031); x *= x + 33.33; x *= x + x; return frac(x); }

// periodic 1D value noise (period P cells): seamless all the way around the oval
float Noise(float x, float P)
{
    float i = floor(x);
    float f = x - i;
    f = f * f * (3.0 - 2.0 * f);
    return lerp(Hash(i - P * floor(i / P)), Hash((i + 1.0) - P * floor((i + 1.0) / P)), f);
}

// ray / sphere (centre 0): returns (near, far); far < near when missed
float2 Sphere(float3 o, float3 d, float r)
{
    float b = dot(o, d);
    float c = dot(o, o) - r * r;
    float disc = b * b - c;
    if (disc <= 0.0) return float2(1.0, -1.0);
    float s = sqrt(disc);
    return float2(-b - s, -b + s);
}

// emission per km at planet-frame point p (km) for one hemisphere (pole P with tangent basis E, N)
float3 Emit(float3 p, float3 P, float3 E, float3 N, float3 sun, float R, float Base, float Top, float T,
            float Boost, float W, float Act)
{
    float r = length(p);
    float h = r - R;
    if (h < Base - 0.8 || h > Top) return 0;
    float3 n = p / r;
    // the sun's elevation where this light is: full night 12 deg below the horizon, gone at the terminator
    float night = smoothstep(0.0, -0.21, dot(n, sun));
    if (night <= 0.0) return 0;

    float colat = acos(clamp(dot(n, P), -1.0, 1.0)) * 57.29578;
    if (colat > 34.0 || colat < 6.0) return 0;
    float ax = dot(n, E), ay = dot(n, N);
    float az = atan2(ay, ax);
    float u = az * 0.15915494 + 0.5;                  // 0..1 around the oval
    // local midnight: the anti-sun direction in the pole's tangent plane
    float2 mid = float2(-dot(sun, E), -dot(sun, N));
    mid /= max(length(mid), 1e-4);
    float cpsi = (ax * mid.x + ay * mid.y) / max(length(float2(ax, ay)), 1e-4);

    // the oval and its folds (slow drift; integer harmonics so it closes)
    float fold = 2.1 * sin(az * 3.0 + 1.0 + T * 0.013) + 1.0 * sin(az * 7.0 + 0.4 - T * 0.021)
               + 0.45 * sin(az * 17.0 + T * 0.05) + 0.2 * sin(az * 31.0 - T * 0.09) + 0.09 * sin(az * 53.0 + T * 0.17);
    float oval = 19.0 + 2.6 * cpsi + fold;            // degrees
    float kmPerDeg = r * 0.017453293;

    const float3 GREEN = float3(0.06, 1.00, 0.16);    // 557.7 nm: pure green, not mint (verify pass)
    const float3 VIOLET = float3(0.50, 0.04, 1.00);
    const float3 PINK = float3(1.00, 0.22, 0.50);
    float3 acc = 0;

    [unroll] for (int k = 0; k < 3; ++k)
    {
        float kf = float(k);
        float offs = (k == 0) ? 0.0 : ((k == 1) ? -1.7 : 1.5);
        float amp = (k == 0) ? 1.0 : ((k == 1) ? 0.6 : 0.45);
        float curl = (kf > 0.0 ? 0.9 * sin(az * 2.0 + kf * 2.4 + T * 0.01) : 0.0) + 0.32 * sin(az * (11.0 + 6.0 * kf) + kf * 2.1 + T * 0.04) + 0.12 * sin(az * (37.0 + 4.0 * kf) - T * 0.13 + kf);
        float d = (colat - (oval + offs + curl)) * kmPerDeg;      // km across the sheet
        float sheet = exp(-d * d / (W * W)) * (0.35 / W);          // integral across kept when widened
        if (sheet < 2e-3) continue;
        // where along the arc this sheet is bright at all (patches, slowly changing)
        float patch = smoothstep(0.35, 0.85, Noise(u * 7.0 + T * 0.012 + kf * 3.7, 7.0)) * (0.55 + 0.45 * Noise(u * 23.0 - T * 0.02 + kf, 23.0));
        patch = max(patch, Act * 0.6);
        if (patch < 1e-3) continue;
        // vertical rays: fine striations drifting along the arc
        float r1 = Noise(u * 420.0 + T * (0.35 + 0.2 * kf) + kf * 37.0, 420.0);
        float r2 = Noise(u * 1100.0 - T * 0.8 + kf * 11.0, 1100.0);
        float rays = saturate(0.1 + 1.8 * r1 * r1 * r1 + 0.6 * r2 * r2 * r2 * r2);
        // lower border: undulates a little along the arc
        float foot = Base + 0.45 * sin(az * 5.0 + kf) + 0.3 * Noise(u * 60.0 + kf * 5.0, 60.0);
        float above = h - foot;
        float lower = smoothstep(-0.25, 0.15, above);
        float hs = 1.1 + 1.8 * rays;                               // tall rays reach higher (green fades before the violet tops)
        float g = lower * exp(-max(above, 0.0) / hs);
        float v = lower * smoothstep(3.5, 6.5, above) * exp(-max(above - 6.5, 0.0) / (1.6 + 2.2 * rays)) * (0.08 + 0.3 * rays);
        float pk = lower * (1.0 - smoothstep(0.0, 0.6, above)) * Boost;
        acc += (amp * sheet * patch * (0.35 + 0.65 * rays)) * (g * GREEN + v * VIOLET + pk * PINK * 0.8);
    }
    // diffuse aurora: a broad faint glow equatorward of the arcs
    float dd = (colat - (oval + 2.6)) / 2.2;
    float hb = h - Base;
    acc += (exp(-dd * dd) * smoothstep(-0.6, 0.4, hb) * exp(-max(hb, 0.0) / 3.0) * 0.02) * GREEN;

    // midnight sector bright, noon sector dim; fade out under the top of the slab
    acc *= (0.15 + 0.85 * smoothstep(-0.6, 0.7, cpsi)) * night * (1.0 - smoothstep(Top - 3.0, Top, h));
    return acc;
}

float3 Shade(FMaterialPixelParameters Parameters, float3 WP, float3 Cam, float3 PC, float3 PX, float3 PY, float3 PZ, float3 SunDir,
             float3 PoleN, float3 EN, float3 NN, float3 PoleS, float3 ES, float3 NS, float4 Shape, float4 Anim,
             float T)
{
    float R = Shape.x, Base = Shape.y, Top = Shape.z, CapCos = Shape.w;
    float Boost = Anim.x, Strength = Anim.y, Steps = max(Anim.z, 4.0), Act = Anim.w;
    if (Strength <= 0.0) return 0;
    float dbg = Act < 0.0 ? 0.08 : 0.0;   // -auroradebug: colour each early-out (red sphere, orange cap, yellow length, blue far, magenta day)
    Act = max(Act, 0.0);

    // planet frame, km
    float3 co = Cam - PC, wo = WP - PC;
    float3 o = float3(dot(co, PX), dot(co, PY), dot(co, PZ)) * 1e-5;
    float3 w = float3(dot(wo, PX), dot(wo, PY), dot(wo, PZ)) * 1e-5;
    float3 d = w - o;
    float pixT = length(d);
    d /= max(pixT, 1e-6);
    float3 sun = normalize(float3(dot(SunDir, PX), dot(SunDir, PY), dot(SunDir, PZ)));

    float2 so = Sphere(o, d, R + Top);
    if (so.y <= so.x || so.y <= 0.0) return dbg * float3(1, 0, 0);
    float t0 = max(so.x, 0.0), t1 = so.y;
    bool outside = dot(o, o) > (R + Top) * (R + Top);
    // the cap can be crossed twice (entry and exit of the outer sphere): only the first crossing marches
    if (outside && pixT > t0 + 1.0)
    {
        float3 pe = normalize(o + d * t0);
        if (max(dot(pe, PoleN), dot(pe, PoleS)) > CapCos) return dbg * float3(1, 0.5, 0);
    }
    // clip by the planet (sea sphere; the curtains' feet stand 3 km up, above the highest terrain). The scene
    // depth is not used: in this pass it reads in front of the cap over the whole planet (measured with
    // -auroradebug), so it cannot tell an occluder from the ground under the slab.
    float2 sp = Sphere(o, d, R);
    if (sp.y > sp.x && sp.x > 0.0) t1 = min(t1, sp.x);
    // skip the empty sphere under the curtains (two segments when the ray dips under them at the limb)
    float2 A = float2(t0, t1), B = float2(1.0, 0.0);
    float rIn = R + Base - 0.8;
    float2 si = Sphere(o, d, rIn);
    if (si.y > si.x)
    {
        if (dot(o, o) < rIn * rIn) A = float2(max(t0, si.y), t1);
        else if (si.x > 0.0) { A = float2(t0, min(t1, si.x)); B = float2(max(t0, si.y), t1); }
    }
    float LA = max(A.y - A.x, 0.0), LB = max(B.y - B.x, 0.0);
    float L = LA + LB;
    if (L < 0.01) return dbg * float3(1, 1, 0);

    // which hemisphere, and cheap rejects (no oval near this ray, or all of it in daylight)
    float3 pm = o + d * (LA > 0.0 ? 0.5 * (A.x + A.y) : 0.5 * (B.x + B.y));
    float3 pa = normalize(o + d * (LA > 0.0 ? A.x : B.x));
    float3 pb = normalize(o + d * (LB > 0.0 ? B.y : A.y));
    float3 pmn = normalize(pm);
    bool north = dot(pmn, PoleN) > dot(pmn, PoleS);
    float3 P = north ? PoleN : PoleS, E = north ? EN : ES, N = north ? NN : NS;
    // the point of the ray closest (in angle) to the pole: maximise dot(normalize(o + d t), P)
    float pa_ = dot(o, P), pb_ = dot(d, P), oe = dot(o, d), oc = dot(o, o);
    float den = pb_ * oe - pa_;
    float tc = abs(den) > 1e-6 ? (pa_ * oe - pb_ * oc) / den : A.x;
    float3 pc1 = normalize(o + d * clamp(tc, A.x, max(A.x, A.y)));
    float3 pc2 = LB > 0.0 ? normalize(o + d * clamp(tc, B.x, B.y)) : pc1;
    float closest = max(max(max(dot(pa, P), dot(pb, P)), dot(pmn, P)), max(dot(pc1, P), dot(pc2, P)));
    if (closest < 0.82) return dbg * float3(0, 0, 1);                          // farther than 35 deg from the pole everywhere
    float day = min(min(dot(pa, sun), dot(pb, sun)), dot(pmn, sun));
    if (day > 0.02) return dbg * float3(1, 0, 1);                           // the whole segment is in daylight

    // march (jittered per pixel and frame; TSR resolves the noise)
    float ds = L / Steps;
    float W = max(0.32, 0.6 * ds);                      // sheet width never thinner than the step
    float j = frac(52.9829189 * frac(dot(Parameters.SvPosition.xy, float2(0.06711056, 0.00583715))) + 0.618034 * float(View.StateFrameIndexMod8));
    float3 acc = 0;
    for (float i = 0.0; i < Steps; i += 1.0)
    {
        float s = (i + j) * ds;
        float t = (s < LA) ? (A.x + s) : (B.x + (s - LA));
        acc += Emit(o + d * t, P, E, N, sun, R, Base, Top, T, Boost, W, Act);
    }
    acc *= ds * Strength * 0.3;
    // soft clamp in luminance (an edge-on curtain must not clip into a white line)
    float lum = dot(acc, float3(0.25, 0.65, 0.10));
    const float LMAX = 0.15;
    acc *= LMAX * (1.0 - exp(-lum / LMAX)) / max(lum, 1e-5);
    return acc + dbg * float3(0.3, 0.3, 0.3);
}
