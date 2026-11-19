// DEMIURGE sky (C2): a lightning flash seen through the cloud tops: a soft additive glow on a disc laid
// just above the clouds. UV 0..1 over the disc. Returns emissive.
float2 p = UV * 2.0 - 1.0;
float r2 = dot(p, p);
float ring = 0.75 + 0.25 * sin(atan2(p.y, p.x) * 5.0 + Seed * 6.283) * sin(r2 * 9.0 + Seed * 3.1);
float g = (exp(-r2 * 9.0) + 0.25 * exp(-r2 * 3.0)) * (1.0 - smoothstep(0.6, 1.0, r2)) * ring;
return Color.rgb * (Intensity * g);
