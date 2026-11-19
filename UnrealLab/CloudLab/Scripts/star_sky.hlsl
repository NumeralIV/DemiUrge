// CloudLab star field + faint milky way on a sky sphere. V = CameraVectorWS (points at the camera).
float3 d = normalize(-V);
float3 col = 0.0;
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
        col += tint * mag * exp(-r * r / (2.0 * 0.018 * 0.018));
    }
}
// Milky way: a soft band about a tilted great circle, mottled by smooth trig noise (no cell hashes:
// those show up as blocks when the band is brightened).
float3 gn = normalize(float3(0.3, -0.55, 0.78));
float b = dot(d, gn);
float band = exp(-b * b / 0.02);
float mott = 0.55 + 0.25 * sin(d.x * 23.0 + sin(d.y * 17.0) * 2.0) * sin(d.z * 19.0 + d.y * 7.0)
           + 0.2 * sin(d.y * 41.0 + sin(d.z * 29.0) * 1.5) * sin(d.x * 37.0 - d.z * 11.0);
float dust = 1.0 - 0.6 * exp(-b * b / 0.0015) * (0.5 + 0.5 * sin(d.x * 31.0 + d.z * 13.0));
col += float3(0.55, 0.6, 0.75) * band * mott * dust * 0.012;
return col * Bright;
