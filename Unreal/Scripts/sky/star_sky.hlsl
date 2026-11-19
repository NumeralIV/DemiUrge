// DEMIURGE sky (C2): star field, milky way and the genesis comet rain on the world-fixed sky sphere.
// V = CameraVectorWS (points from the surface toward the camera). Returns emissive colour.
float3 d = normalize(-V);
float3 col = 0.0;
// Stars: three hashed cell grids on the view direction, Gaussian dots about 1 px, colour tints, faint twinkle.
for (int L = 0; L < 3; ++L)
{
    float sc = 150.0 * (1.0 + L * 1.7);
    float3 p = d * sc;
    float3 c = floor(p);
    float3 f = p - c;
    float h = frac(sin(dot(c, float3(12.9898, 78.233, 37.719)) + L * 3.1) * 43758.5453);
    if (h > 0.86)
    {
        float3 o = frac(sin(float3(dot(c, float3(127.1, 311.7, 74.7)), dot(c, float3(269.5, 183.3, 246.1)), dot(c, float3(113.5, 271.9, 124.6)))) * 43758.5453);
        float r = length(f - (0.2 + 0.6 * o));
        float mag = pow((h - 0.86) / 0.14, 6.0) * (L == 0 ? 6.0 : 2.0) + 0.04;
        float t = frac(h * 91.7);
        float3 tint = t < 0.2 ? float3(1.0, 0.75, 0.55) : (t < 0.45 ? float3(0.75, 0.85, 1.0) : float3(1.0, 0.97, 0.92));
        float tw = 1.0 + Twinkle * sin(Time * (2.0 + 5.0 * o.x) + 40.0 * o.y);
        col += tint * mag * tw * exp(-r * r / (2.0 * 0.018 * 0.018));
    }
}
// Milky way: a soft band about a tilted great circle, mottled by smooth trig noise and a dark dust lane.
float3 gn = normalize(float3(0.3, -0.55, 0.78));
float b = dot(d, gn);
float band = exp(-b * b / 0.02);
float core = exp(-b * b / 0.004) * (0.5 + 0.5 * sin(d.x * 3.0 + d.y * 2.0 + 1.3));
float mott = 0.55 + 0.25 * sin(d.x * 23.0 + sin(d.y * 17.0) * 2.0) * sin(d.z * 19.0 + d.y * 7.0)
           + 0.2 * sin(d.y * 41.0 + sin(d.z * 29.0) * 1.5) * sin(d.x * 37.0 - d.z * 11.0);
float dust = 1.0 - 0.6 * exp(-b * b / 0.0015) * (0.5 + 0.5 * sin(d.x * 31.0 + d.z * 13.0));
col += (float3(0.55, 0.6, 0.75) * band * mott + float3(0.75, 0.65, 0.55) * core * mott * 0.6) * dust * 0.012 * Galaxy;
col *= Bright;
// Comet rain (genesis, Water phase): faint streaks crossing the black, each on its own great circle.
if (CometRain > 0.0)
{
    for (int k = 0; k < 14; ++k)
    {
        float per = 2.5 + frac(k * 0.371) * 3.0;
        float cyc = floor((Time + k * 1.618) / per);
        float tt = frac((Time + k * 1.618) / per);
        float3 hh = frac(sin(float3(cyc * 12.989 + k * 78.23, cyc * 39.346 + k * 11.13, cyc * 73.156 + k * 52.235)) * 43758.5453);
        float3 a = normalize(hh * 2.0 - 1.0 + float3(0.0, 0.0, 0.001));
        float3 ax = normalize(cross(a, normalize(float3(hh.y - 0.5, hh.z - 0.5, 0.37))));
        float3 bx = cross(ax, a);
        // head travels from a along the circle (a, bx) by 0.5 rad over the cycle
        float hd = tt * 0.5;
        float side = dot(d, ax);
        float along = atan2(dot(d, bx), dot(d, a));
        float tail = 0.12;
        float inSeg = smoothstep(hd - tail, hd, along) * (1.0 - smoothstep(hd, hd + 0.002, along));
        float fade = sin(tt * 3.14159);
        col += float3(0.85, 0.95, 1.0) * CometRain * fade * inSeg * exp(-side * side / (2.0 * 0.0007 * 0.0007)) * 0.6
             * (0.25 + 0.75 * saturate((along - hd + tail) / tail));
    }
}
return col;
