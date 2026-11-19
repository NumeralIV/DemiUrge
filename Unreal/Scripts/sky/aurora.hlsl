// DEMIURGE sky (C2): aurora around each pole. UV.x runs around the oval (0..1). UV.y 0..1: the vertical
// curtain, foot (0) to top (1); UV.y 2..3: the oval band lying above the clouds, across it (what an orbiting
// eye sees). Visible on the night side only. Returns emissive.
float u = UV.x;
float tau = 6.2831853;
float x = u * 90.0 + 1.6 * sin(u * tau * 3.0 + Time * 0.05) + 0.9 * sin(u * tau * 7.0 - Time * 0.11);
// vertical rays (striations), drifting along the curtain
float ray = 0.5 + 0.5 * sin(x * 7.0 + 2.0 * sin(x * 1.3 + Time * 0.35));
ray = pow(ray, 3.0) * 0.7 + 0.3 * (0.5 + 0.5 * sin(x * 2.1 - Time * 0.2));
// large-scale activity along the ring (substorm surges)
float act = 0.25 + 0.75 * pow(0.5 + 0.5 * sin(u * tau * 2.0 + Time * 0.03 + 1.0), 2.0) * (0.6 + 0.4 * sin(u * tau * 5.0 - Time * 0.07));
float night = smoothstep(0.02, -0.2, dot(normalize(WP), SunDir));
float3 col;
if (UV.y < 1.5)
{
    float v = UV.y;
    // sharp lower edge, glow decaying upward; green low, red-violet high
    float prof = smoothstep(0.0, 0.06, v) * exp(-v * 2.6) * (1.0 - smoothstep(0.85, 1.0, v));
    col = lerp(float3(0.12, 1.0, 0.38), float3(0.75, 0.18, 0.55), smoothstep(0.3, 0.85, v)) * (ray * act * prof * 0.35);
}
else
{
    float v = UV.y - 2.0;
    // folded arcs inside the band: two or three bright filaments that wander across it
    float f1 = exp(-pow((v - (0.45 + 0.25 * sin(x * 0.21 + Time * 0.02))) / 0.07, 2.0));
    float f2 = exp(-pow((v - (0.7 + 0.15 * sin(x * 0.13 - 1.3))) / 0.05, 2.0)) * 0.6;
    float diffuse = smoothstep(0.0, 0.3, v) * (1.0 - smoothstep(0.6, 1.0, v)) * 0.25;
    float b = (f1 + f2) * (0.55 + 0.45 * ray) + diffuse;
    // broken into patches along the oval (an even ring reads as a neon tube)
    float patch = smoothstep(0.25, 0.75, 0.5 + 0.5 * sin(x * 0.37 + 2.0 * sin(x * 0.11) + Time * 0.04));
    col = lerp(float3(0.1, 1.0, 0.35), float3(0.5, 0.25, 0.7), smoothstep(0.75, 1.0, v) * 0.6) * (b * act * patch * 0.35);
}
return col * (night * Strength);
