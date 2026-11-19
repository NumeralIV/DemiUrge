// CloudLab moon albedo: maria (dark basalt plains) + crater rims, from the object-space direction.
float3 d = normalize(LP);
float3 col = 0.0;
// value noise
float acc = 0.0, amp = 0.5, fr = 2.0;
for (int i = 0; i < 5; ++i)
{
    float3 p = d * fr + i * 7.31;
    float3 c = floor(p), f = frac(p); f = f * f * (3.0 - 2.0 * f);
    float n000 = frac(sin(dot(c, float3(12.99, 78.23, 37.71))) * 43758.55);
    float n100 = frac(sin(dot(c + float3(1,0,0), float3(12.99, 78.23, 37.71))) * 43758.55);
    float n010 = frac(sin(dot(c + float3(0,1,0), float3(12.99, 78.23, 37.71))) * 43758.55);
    float n110 = frac(sin(dot(c + float3(1,1,0), float3(12.99, 78.23, 37.71))) * 43758.55);
    float n001 = frac(sin(dot(c + float3(0,0,1), float3(12.99, 78.23, 37.71))) * 43758.55);
    float n101 = frac(sin(dot(c + float3(1,0,1), float3(12.99, 78.23, 37.71))) * 43758.55);
    float n011 = frac(sin(dot(c + float3(0,1,1), float3(12.99, 78.23, 37.71))) * 43758.55);
    float n111 = frac(sin(dot(c + float3(1,1,1), float3(12.99, 78.23, 37.71))) * 43758.55);
    float n = lerp(lerp(lerp(n000, n100, f.x), lerp(n010, n110, f.x), f.y), lerp(lerp(n001, n101, f.x), lerp(n011, n111, f.x), f.y), f.z);
    acc += amp * n; amp *= 0.5; fr *= 2.1;
}
float maria = smoothstep(0.52, 0.47, acc);
// craters: bright rims in a cell grid
float cr = 0.0;
for (int k = 0; k < 2; ++k)
{
    float sc = 9.0 + k * 13.0;
    float3 p = d * sc; float3 c = floor(p); float3 f = frac(p) - 0.5;
    float h = frac(sin(dot(c, float3(41.3, 289.1, 73.9))) * 9631.7);
    float r = length(f) / (0.18 + 0.25 * h);
    cr += (h > 0.55 ? 1.0 : 0.0) * (smoothstep(0.75, 0.95, r) - smoothstep(0.95, 1.1, r)) * 0.5;
}
float a = lerp(0.26, 0.12, maria) * (0.85 + 0.3 * acc) + cr * 0.12;
return float3(a, a * 0.97, a * 0.93);
