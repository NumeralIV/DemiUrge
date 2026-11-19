// DEMIURGE fx (C6): the one particle shader. Pasted into a Custom node of M_Fx by Scripts/fx.py.
// Every effect is drawn as camera-relative quads by Game/FX/DmFxRender.cpp; per vertex:
//   U0 = quad corner (0..1; the curtain shape uses U0.x as a continuous ribbon coordinate)
//   U1 = (shape id, depth-fade distance in km: used outside this node)
//   U2 = (r, g), U3 = (b, a): HDR colour and opacity (premultiplied below)
//   VC = (light 0..1 x2 (CPU sun gradient for lit shapes), age 0..1, seed /255, shape parameter 0..1)
// Output float4(rgb premultiplied, alpha) for the AlphaComposite blend: alpha 0 = purely additive.
// The CPU passes rgb ALREADY premultiplied by the opacity (lit albedo x light x opacity, or emitted
// light), so no shape multiplies rgb by alpha here; shapes only scale both by their coverage m.

// Integer hash of a lattice point: exact, so neighbouring cells agree on their shared corners. (A float
// sine/frac hash differed between call sites after FMA contraction and showed the lattice as tiles.)
float H21(float2 p)
{
	uint2 v = uint2(int2(floor(p)));
	v = v * 1664525u + 1013904223u;
	v.x += v.y * 1664525u; v.y += v.x * 1664525u;
	v ^= v >> 16u;
	v.x += v.y * 1664525u; v.y += v.x * 1664525u;
	v ^= v >> 16u;
	return float(v.x & 0xFFFFFFu) * (1.0 / 16777216.0);
}
float VN(float2 p)
{
	float2 i = floor(p), f = frac(p);
	float2 u = f * f * (3.0 - 2.0 * f);
	return lerp(lerp(H21(i), H21(i + float2(1, 0)), u.x), lerp(H21(i + float2(0, 1)), H21(i + float2(1, 1)), u.x), u.y);
}
float Fbm(float2 p)
{
	float s = 0.0, a = 0.5;
	for (int k = 0; k < 4; k++) { s += a * VN(p); p = p * 2.03 + float2(1.7, 9.2); a *= 0.5; }
	return s / 0.9375;
}

float4 Shade(float2 uv, float2 u1, float2 u2, float2 u3, float4 vc, float t)
{
	const float2 q = uv * 2.0 - 1.0;
	const float r2 = dot(q, q);
	const float3 rgb = float3(u2.x, u2.y, u3.x);
	const float alpha = u3.y;
	const float seed = vc.b * 255.0;
	const float2 so = float2(seed * 0.7131, seed * 1.3791);
	const float age = vc.g;
	const float par = vc.a;
	const float lit = vc.r * 2.0;
	const int s = (int)(u1.x + 0.5);
	float m = 0.0;
	// direction of increasing light in quad space, from the screen derivatives of the per-vertex light
	// (the CPU makes the corners facing the sun brighter): smoke uses it to shade its own billows
	const float2 ux = ddx(uv), uy = ddy(uv);
	const float lx = ddx(vc.r), ly = ddy(vc.r);
	const float det = ux.x * uy.y - ux.y * uy.x;
	float2 lg = float2(0.0, 0.0);
	if (abs(det) > 1e-14) lg = float2(uy.y * lx - ux.y * ly, ux.x * ly - uy.x * lx) / det;
	const float lgl = length(lg);
	lg = lgl > 1e-4 ? lg / lgl : float2(0.0, 0.0);

	if (s == 0)            // glow: gaussian with a hot core, zero at the edge
	{
		m = (0.75 * exp(-r2 * 5.0) + 0.5 * exp(-r2 * 28.0)) * saturate(1.0 - r2);
		return float4(rgb * m, alpha * m);
	}
	if (s == 1)            // smoke / ash / dust puff: evolving billows, eroded rim, shadowed away from the sun
	{
		const float2 drift = float2(0.0, -age * 0.8);
		const float n = Fbm(q * 1.3 + so + drift);
		const float n2 = Fbm(q * 3.4 + so * 1.7 + float2(age * 0.5, 0.0));
		const float d = saturate(1.0 - r2);
		float dens = saturate((d * 1.05 - 0.28 + (n - 0.5) * 1.5 + (n2 - 0.5) * 0.6 - age * 0.22) * 2.6) * smoothstep(0.0, 0.35, d);
		dens *= lerp(1.0, 0.45 + 0.55 * n2, par);         // par: wispier
		const float nl = Fbm((q + lg * 0.32) * 1.3 + so + drift);
		const float occl = saturate((nl - n) * 3.0 + 0.5);  // denser toward the light: in shadow
		const float sh = lit * lerp(1.22, 0.52, occl) * lerp(0.86, 1.06, n2);
		return float4(rgb * sh * dens, dens * alpha);
	}	if (s == 2)            // streak along U: tail at -1 (dim), head at +1 (bright, rounded)
	{
		const float head = saturate(q.x * 0.5 + 0.5);
		m = exp(-q.y * q.y * 9.0) * saturate(1.0 - q.x * q.x) * lerp(0.06, 1.0, head * head);
		m += 0.6 * exp(-((q.x - 0.6) * (q.x - 0.6) * 30.0 + q.y * q.y * 30.0)) * saturate((1.0 - q.x) * 3.0);
		return float4(rgb * m, alpha * m);
	}
	if (s == 3)            // ring: annulus of width par at the quad edge, broken by noise
	{
		const float rr = sqrt(r2);
		const float w = max(par, 0.015);
		float k = 1.0 - abs(rr - (1.0 - w)) / w;
		k = saturate(k);
		m = k * k * (3.0 - 2.0 * k);
		const float2 dir = q / max(rr, 1e-4);
		const float bn = Fbm(dir * 2.6 + so + rr * 1.5);
		m *= saturate(0.35 + 1.1 * bn);
		m += 0.10 * saturate(1.0 - rr) * saturate(rr - 0.2) * (1.0 - w);   // faint pressure fill
		return float4(rgb * m * lerp(1.0, lit, saturate(alpha * 4.0)), alpha * m);
	}
	if (s == 4)            // flame tongue: V axis is up (q.y -1 bottom .. +1 top), ragged, flickering upward
	{
		const float y = q.y * 0.5 + 0.5;
		const float n = Fbm(float2(q.x * 1.6, q.y * 0.9 - t * 2.2) + so);
		const float n2 = VN(float2(q.x * 4.0, q.y * 3.0 - t * 4.5) + so * 1.3);
		const float w = lerp(0.72, 0.05, pow(y, 0.8));
		const float edge = abs(q.x + (n - 0.5) * 0.7 * y) / max(w, 0.04);
		m = saturate((1.0 - edge) * 1.4 + (n - 0.5) * 1.6 + (n2 - 0.5) * 0.7 - y * 0.45);
		m *= saturate(y * 6.0) * saturate((1.0 - y) * 2.2) * saturate(1.0 - q.x * q.x);
		m *= m;
		const float core = m * m;
		const float3 col = rgb * m + rgb * float3(1.0, 0.9, 0.6) * core * 1.2;
		return float4(col, alpha * m * 0.35);
	}	if (s == 5)            // beam / light column: x across, y along (fades toward the top), flowing
	{
		const float y = q.y * 0.5 + 0.5;
		m = 0.65 * exp(-q.x * q.x * 5.0) + 0.55 * exp(-q.x * q.x * 45.0);
		m *= saturate(y * 10.0) * pow(saturate(1.0 - y), 1.4) * saturate(1.0 - q.x * q.x);
		m *= 0.72 + 0.55 * VN(float2(q.x * 3.0 + seed, q.y * 5.0 - t * 1.4));
		return float4(rgb * m, alpha * m);
	}
	if (s == 6)            // lightning segment / light band: x across; razor core plus glow; par = softness
	{
		const float cap = saturate((1.0 - abs(q.y)) * 6.0);
		const float core = exp(-q.x * q.x * 90.0) * 1.6 * (1.0 - par);
		m = (core + (0.3 + 0.7 * par) * exp(-q.x * q.x * 7.0)) * cap * saturate(1.0 - q.x * q.x);
		const float3 col = rgb * m + float3(1.0, 1.0, 1.0) * exp(-q.x * q.x * 300.0) * length(rgb) * 0.6 * cap * (1.0 - par);
		return float4(col, alpha * m);
	}
	if (s == 7)            // flat disc: glowing pools (crater floor, lava, light on the ground), mottled
	{
		const float d = saturate(1.0 - r2);
		const float n = Fbm(q * 2.4 + so + float2(t * 0.05, 0.0));
		m = d * d * lerp(1.0, 0.35 + 1.1 * n, par);
		return float4(rgb * m, alpha * m);
	}
	if (s == 8)            // mote / sparkle: core, soft halo, a four-point glint that twinkles
	{
		const float tw = 0.6 + 0.4 * sin(t * 9.0 + seed * 2.1);
		m = exp(-r2 * 45.0) * 1.4 + 0.25 * exp(-r2 * 7.0) * saturate(1.0 - r2);
		m += tw * 0.7 * (exp(-q.x * q.x * 500.0) * exp(-q.y * q.y * 5.0) + exp(-q.y * q.y * 500.0) * exp(-q.x * q.x * 5.0));
		return float4(rgb * m, alpha * m);
	}
	if (s == 9)            // aurora curtain: U0.x continuous along the ribbon, y vertical
	{
		const float x = uv.x;
		const float y = uv.y;
		const float rays = 0.25 + 0.75 * pow(VN(float2(x * 7.0 - t * 0.35, seed)), 2.0) + 0.4 * pow(VN(float2(x * 31.0 + t * 0.8, seed + 3.0)), 3.0);
		const float fold = 0.55 + 0.45 * sin(x * 2.3 + t * 0.25 + Fbm(float2(x * 0.7, t * 0.05)) * 4.0);
		m = saturate(y * 9.0) * pow(saturate(1.0 - y), 1.3) * rays * fold;
		const float3 green = float3(0.12, 1.0, 0.38);
		const float3 red = float3(0.9, 0.15, 0.45);
		const float3 col = lerp(green, red, smoothstep(0.35, 1.0, y)) * (1.0 + 0.6 * saturate(1.0 - y * 6.0));
		return float4(col * m * rgb, 0.0);
	}
	if (s == 10)           // rain sheet: x across, y vertical; dashed streaks falling, soft envelope
	{
		const float gx = q.x * 34.0;
		const float col = floor(gx);
		const float off = H21(float2(col, seed));
		const float fx = frac(gx) - 0.5;
		const float ln = exp(-fx * fx * 40.0);
		const float dash = smoothstep(0.35, 0.95, frac(q.y * 2.2 + t * (2.6 + off) + off * 7.0));
		const float ex = saturate(1.0 - q.x * q.x);
		const float env = ex * ex * saturate((1.0 - q.y) * 1.6) * saturate((q.y + 1.0) * 3.0);
		const float veil = 0.55 * env * (0.55 + 0.45 * Fbm(float2(q.x * 2.0, q.y * 1.5 + t * 0.6) + so));
		m = (ln * dash * 0.9 + veil) * env;
		return float4(rgb * m * lit, alpha * m);
	}
	// 11: dot: a small hard-edged disc (debris, doves, cinders)
	m = saturate((1.0 - r2) * 3.0);
	return float4(rgb * m * lit, alpha * m);
}
