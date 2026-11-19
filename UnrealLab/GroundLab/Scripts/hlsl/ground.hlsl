// ---- ground lab: land use at 2-20 km (fields, hedgerows, tracks, roads, village ground) ----
// Pasted between common.hlsl and land.hlsl. All lengths in metres; Pm = world position in metres.

// Gnomonic projection on the ground tile (TO.xyz = tile centre dir, TO.w = half size m; TE/TN = axes).
float2 TileXY(float3 up, float4 TO, float4 TE, float4 TN)
{
	float dc = max(dot(up, TO.xyz), 0.2);
	return 100000.0 * float2(dot(up, TE.xyz), dot(up, TN.xyz)) / dc;
}
float TileIn(float2 xy, float4 TO, float margin)
{
	float2 a = abs(xy);
	return 1.0 - Smooth01((max(a.x, a.y) - (TO.w - margin)) / margin);
}

// 0..1 from any bits of h (re-mixed first: callers pass shifted hashes, and a shift leaves few low bits)
float Rand01(uint h) { h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu; h ^= h >> 16; return float(h & 0xFFFFu) / 65535.0; }

// Crop colour by class and season (linear albedo, before per-field tint). s in 0..1 (0 spring .. 0.5 harvest
// .. 0.75 autumn .. 1 winter), soil = local soil colour, arid 0..1, tc = temperature C.
float3 CropColour(int cls, float s, float3 soil, float arid, float tc, float r)
{
	float3 green = lerp(float3(0.060, 0.105, 0.032), float3(0.085, 0.120, 0.040), r);   // young cereal, pasture
	float3 dark = float3(0.040, 0.080, 0.026);                                            // maize, roots
	float3 gold = lerp(float3(0.235, 0.200, 0.098), float3(0.270, 0.230, 0.125), r);    // ripe grain
	float3 stub = float3(0.235, 0.210, 0.145);                                            // stubble
	float3 plough = soil * lerp(0.70, 0.90, r);                                           // fresh ploughing (moist soil)
	float3 dryG = float3(0.190, 0.170, 0.085);                                            // summer-dry grass
	float3 c;
	if (cls == 0)       // cereal: green in spring, gold by mid summer, stubble then ploughed after harvest
	{
		c = lerp(lerp(soil, green, 0.55), green, Smooth01(s / 0.12));
		c = lerp(c, lerp(green, gold, 0.45), Smooth01((s - 0.22) / 0.10));
		c = lerp(c, gold, Smooth01((s - 0.33) / 0.08));
		c = lerp(c, stub, Smooth01((s - 0.49) / 0.04));
		c = lerp(c, r > 0.5 ? plough : stub * 0.85, Smooth01((s - 0.60) / 0.08));
		c = lerp(c, lerp(plough, green, 0.35), Smooth01((s - 0.82) / 0.1));
	}
	else if (cls == 1)  // pasture: green, browner in dry summers and in winter
	{
		c = green * lerp(0.95, 1.1, r);
		c = lerp(c, dryG, saturate(arid * 1.4 - 0.1) * Smooth01((s - 0.25) / 0.2) * (1.0 - Smooth01((s - 0.7) / 0.1)));
		c = lerp(c, float3(0.085, 0.095, 0.050), Smooth01((s - 0.78) / 0.1));
	}
	else if (cls == 2)  // hay meadow: green, cut in early summer (pale), regrows
	{
		c = lerp(green * 1.1, float3(0.15, 0.16, 0.07), Smooth01((s - 0.28) / 0.04) * (1.0 - Smooth01((s - 0.42) / 0.12)));
		c = lerp(c, float3(0.085, 0.095, 0.050), Smooth01((s - 0.78) / 0.1));
	}
	else if (cls == 3)  // row crops (maize, beet, roots): bare in spring, dark green summer, ploughed autumn
	{
		c = lerp(plough, lerp(plough, dark, 0.5), Smooth01((s - 0.10) / 0.08));
		c = lerp(c, dark, Smooth01((s - 0.20) / 0.10));
		c = lerp(c, lerp(dark, gold * 0.7, 0.5), Smooth01((s - 0.55) / 0.08));
		c = lerp(c, plough, Smooth01((s - 0.66) / 0.06));
	}
	else if (cls == 4)  // fallow / ploughed land: soil with a weedy green cast
	{
		c = lerp(plough, float3(0.11, 0.105, 0.06), 0.35 + 0.3 * r);
	}
	else                // oilseed: green, a few weeks of bright yellow flowering in spring
	{
		c = lerp(green, float3(0.48, 0.40, 0.045), Smooth01((s - 0.10) / 0.03) * (1.0 - Smooth01((s - 0.19) / 0.04)));
		c = lerp(c, lerp(plough, gold, 0.4), Smooth01((s - 0.40) / 0.06));
		c = lerp(c, plough, Smooth01((s - 0.62) / 0.06));
	}
	// warm climates ripen earlier and dry out, cold ones stay green longer
	c = lerp(c, c * float3(1.06, 1.0, 0.9), saturate((tc - 18.0) / 10.0));
	return c;
}

// Field partition. Estates (holdings, ~1 km 3D Voronoi cells) each get an orientation (contours on slopes,
// perpendicular to a nearby river on flood plains, else random), a field size and a layout (strips or
// irregular polygons). Fields inside are clipped by the estate edges.
void Ground(float3 Pm, float3 up, float h, float fp, float3 SunD, float farm, float coreIn, float tc, float arid,
	float forest, float steep, float rivDm, float season, float age, float nA, float nB, float nC, float nD, float nE, float nF,
	Texture2D LandTex, SamplerState LandTexSampler, Texture2D TileH, SamplerState TileHSampler, Texture2D TileF, SamplerState TileFSampler,
	float4 TO, float4 TE, float4 TN, float3 soil, float dbg,
	out float3 gCol, out float gMask, out float gForestK, out float gHedge, out float gShadow, out float3 gNOff, out float gRoad, out float3 gRoadCol, out float4 gRoof)
{
	gCol = 0; gMask = 0; gForestK = 1; gHedge = 0; gShadow = 0; gNOff = 0; gRoad = 0; gRoadCol = 0; gRoof = 0;
	float2 txy = TileXY(up, TO, TE, TN);
	float tin = TileIn(txy, TO, 1500.0);
	float cs = 2.0 * TO.w / TE.w;                         // coarse tile cell, m
	float ft = 2.0 * TO.w / TN.w;                         // fine tile texel, m

	// ---- roads: tile distance field close up, global coverage far away
	{
		float2 uvF = txy / (2.0 * TO.w) + 0.5;
		float4 tf = TileF.SampleLevel(TileFSampler, uvF, 0.0);
		float sd = tf.r, ud = tf.g, rhw = tf.b;
		float w = max(rhw, 0.6 * fp);
		float line_ = (rhw > 0.5 && ud < rhw + 0.75 * ft) ? saturate((w - abs(sd)) / max(0.75 * fp, 0.3) + 0.5) * min(1.0, rhw / w) : 0.0;
		float farK = Smooth01((fp - 0.8 * ft) / (1.5 * ft));
		float rcov = LandTex.SampleLevel(LandTexSampler, OctUV(up), log2(max(fp / 173.0, 1.0))).b;
		gRoad = lerp(line_ * tin, saturate(rcov * 1.2) * 0.35, max(farK, 1.0 - tin));
		gRoad *= Smooth01((h - 0.5) / 1.0);
		float mainR = saturate((rhw - 2.5) / 1.0);
		float3 dirt = lerp(float3(0.17, 0.145, 0.105), float3(0.25, 0.215, 0.160), mainR);
		dirt *= 1.0 + 0.12 * nC + 0.08 * nD;
		// wheel ruts: darker twin lines on dirt tracks close up
		float rut = (1.0 - Smooth01((abs(abs(sd) - 0.9) - 0.15) / 0.2)) * AAW(1.2, fp) * (1.0 - mainR);
		dirt *= 1.0 - 0.25 * rut;
		float3 tar = float3(0.065, 0.065, 0.068) * (1.0 + 0.05 * nD);
		gRoadCol = lerp(dirt, tar, Smooth01((age - 0.65) / 0.1) * mainR);
	}

	if (farm < 0.015 && coreIn < 0.02) return;

	// ---- estates: 3D Voronoi, nearest site then exact distance to its nearest edge (two passes)
	const float Le = 1000.0;
	// warped by a noise gradient (~70 m), so estate edges and the tracks along them wander
	float3 wE = Noised(Pm / 650.0 + float3(2.3, 7.7, 4.1)).yzw;
	float3 pe = (Pm + 70.0 * (wE - up * dot(wE, up))) / Le;
	int3 ip = int3(floor(pe));
	float d1 = 1e9; int3 c1 = 0; float3 s1 = 0; uint h1 = 0;
	for (int z = -1; z <= 1; z++) for (int y = -1; y <= 1; y++) for (int x = -1; x <= 1; x++)
	{
		int3 c = ip + int3(x, y, z);
		uint hh = Hash(c + int3(911, 37, 71));
		float3 s = float3(c) + 0.15 + 0.7 * float3(hh & 0x3FFu, (hh >> 10) & 0x3FFu, (hh >> 20) & 0x3FFu) / 1023.0;
		float dd = dot(s - pe, s - pe);
		if (dd < d1) { d1 = dd; c1 = c; s1 = s; h1 = hh; }
	}
	float deE = 1e9; uint hE2 = 0;
	for (int z2 = -1; z2 <= 1; z2++) for (int y2 = -1; y2 <= 1; y2++) for (int x2 = -1; x2 <= 1; x2++)
	{
		int3 c = c1 + int3(x2, y2, z2);
		if (all(c == c1)) continue;
		uint hh = Hash(c + int3(911, 37, 71));
		float3 s = float3(c) + 0.15 + 0.7 * float3(hh & 0x3FFu, (hh >> 10) & 0x3FFu, (hh >> 20) & 0x3FFu) / 1023.0;
		float3 nn = normalize(s - s1);
		float e = dot(0.5 * (s1 + s) - pe, nn);
		if (e < deE) { deE = e; hE2 = hh; }
	}
	deE *= Le;
	float rE = Rand01(h1), rE2 = Rand01(h1 >> 16);
	uint estId = h1;

	// estate frame
	float3 cu = normalize(s1);
	float3 ctr = cu * length(Pm);
	float3 t1 = normalize(cross(cu, abs(cu.z) < 0.9 ? float3(0, 0, 1) : float3(1, 0, 0)));
	float3 t2 = cross(cu, t1);
	float2 q2 = float2(dot(Pm - ctr, t1), dot(Pm - ctr, t2));

	// orientation: contour direction (tile smooth height gradient), river normal, or random
	float2 cxy = TileXY(cu, TO, TE, TN);
	float cin = TileIn(cxy, TO, 2500.0);
	float2 uvC = cxy / (2.0 * TO.w) + 0.5;
	float du = 1.0 / TE.w;
	float hC = TileH.SampleLevel(TileHSampler, uvC, 0.0).r;
	float gx = (TileH.SampleLevel(TileHSampler, uvC + float2(du, 0), 0.0).r - TileH.SampleLevel(TileHSampler, uvC - float2(du, 0), 0.0).r) / (2.0 * cs);
	float gy = (TileH.SampleLevel(TileHSampler, uvC + float2(0, du), 0.0).r - TileH.SampleLevel(TileHSampler, uvC - float2(0, du), 0.0).r) / (2.0 * cs);
	float3 gW = gx * TE.xyz + gy * TN.xyz;
	float2 gl = float2(dot(gW, t1), dot(gW, t2)) * cin;
	float gm = length(gl);
	float dfn = 1.0 / TN.w;
	float rdC = TileF.SampleLevel(TileFSampler, uvC, 0.0).a;
	float rx = TileF.SampleLevel(TileFSampler, uvC + float2(3.0 * dfn, 0), 0.0).a - TileF.SampleLevel(TileFSampler, uvC - float2(3.0 * dfn, 0), 0.0).a;
	float ry = TileF.SampleLevel(TileFSampler, uvC + float2(0, 3.0 * dfn), 0.0).a - TileF.SampleLevel(TileFSampler, uvC - float2(0, 3.0 * dfn), 0.0).a;
	float3 rW = rx * TE.xyz + ry * TN.xyz;
	float2 rl = float2(dot(rW, t1), dot(rW, t2));
	// a smooth regional orientation (neighbouring holdings share it, as along an old road or a valley)
	// plus a small per-estate turn
	float ang = 2.2 * 3.1416 * Noised(cu * (100000.0 / 7000.0) + float3(6.1, 2.2, 0.9)).x + (rE - 0.5) * 0.6;
	float2 across = float2(cos(ang), sin(ang));
	int layout = rE2 < lerp(0.55, 0.25, age) ? 1 : 0;   // 1 = irregular polygons (bocage), 0 = strips
	float contourW = Smooth01((gm - 0.02) / 0.03);
	bool riverLots = cin > 0.5 && rdC < 900.0 && length(rl) > 1e-3 && gm < 0.04;
	if (contourW > 0.5) { across = gl / gm; layout = rE2 < 0.75 ? 0 : layout; }
	else if (riverLots) { across = float2(-rl.y, rl.x) / length(rl); layout = 0; }  // long side runs away from the river
	float2 along = float2(-across.y, across.x);
	float u = dot(q2, along), v = dot(q2, across);
	// contour coordinate: on slopes, the across-field coordinate is the smooth height itself, so strip
	// boundaries follow the real contour lines (bending around spurs and hollows)
	if (contourW > 0.0 && cin > 0.0)
	{
		float hP = TileH.SampleLevel(TileHSampler, TileXY(up, TO, TE, TN) / (2.0 * TO.w) + 0.5, 0.0).r;
		float vc = (hP - hC) / max(gm, 0.02);
		v = lerp(v, clamp(vc, v - 350.0, v + 350.0), contourW * cin);
	}

	// field size: small near villages and in early ages, larger later; varies per estate and by region
	// per-estate constants only: anything that varies per pixel here would stretch the field coordinates
	// (an early version scaled W by the per-pixel settlement core and drew radial streaks round villages)
	float core = LandTex.SampleLevel(LandTexSampler, OctUV(cu), 0.0).g;
	float region = 0.5 + 0.5 * Noised(cu * (100000.0 / 2600.0) + float3(4.4, 1.1, 8.8)).x;
	float s0 = lerp(115.0, 300.0, Smooth01((age - 0.2) / 0.7)) * lerp(0.8, 1.3, rE2) * lerp(0.8, 1.25, region) * lerp(0.65, 1.0, saturate(1.0 - core * 1.5));
	float W = s0 * (layout == 0 ? lerp(1.5, 2.8, rE) : lerp(1.0, 1.6, rE));
	float H = s0 * (layout == 0 ? lerp(0.5, 0.75, rE2) : lerp(0.75, 1.0, rE2));
	if (riverLots) { W *= 1.4; H *= 0.8; }

	// curvy boundaries: warp both coordinates with noise at ~2.5 field widths
	float wl = 3.0 * max(W, H) + 100.0;
	float wa = (layout == 0 ? 0.10 : 0.30) * min(W, H) + 4.0;
	float3 wg = Noised(Pm / wl + float3(3.3, 1.7, 5.1)).yzw;
	float2 wq = float2(dot(wg, t1), dot(wg, t2)) * wa * 0.6;
	float uu = u + wq.x, vv = v + wq.y;

	uint fid; float de; float2 eN; float2 fc; // field id, distance to field edge (m), edge normal (local), field centre (local u,v)
	uint edgeId;
	if (layout == 0)
	{
		// furlongs: blocks of parallel strips whose ends line up (boundaries along u shared by every row),
		// a few strips split once more
		float row = floor(vv / H);
		float fr = vv - row * H;
		uint hr = Hash(int3(int(row), 77, int(estId & 0xFFFFu)));
		float k0 = floor(uu / W);
		float b0 = W * (k0 + 0.2 + 0.6 * Rand01(Hash(int3(int(k0), 31, int(estId >> 16)))));
		float kl = uu >= b0 ? k0 : k0 - 1.0;
		float lo = W * (kl + 0.2 + 0.6 * Rand01(Hash(int3(int(kl), 31, int(estId >> 16)))));
		float hi = W * (kl + 1.2 + 0.6 * Rand01(Hash(int3(int(kl + 1.0), 31, int(estId >> 16)))));
		uint hs = Hash(int3(int(kl), int(row), int(estId) + 5));
		float split = lerp(lo, hi, 0.3 + 0.4 * Rand01(hs >> 4));
		bool bSplit = Rand01(hs) < 0.3;
		float sub = 0.0;
		if (bSplit) { if (uu < split) hi = split; else { lo = split; sub = 1.0; } }
		float dLo = uu - lo, dHi = hi - uu;
		float dRow = min(fr, H - fr);
		float dU = min(dLo, dHi);
		de = min(dRow, dU);
		eN = dRow < dU ? float2(0, fr < 0.5 * H ? -1.0 : 1.0) : float2(dLo < dHi ? -1.0 : 1.0, 0);
		fid = Hash(int3(int(kl) * 2 + int(sub), int(row), int(estId)));
		edgeId = dRow < dU ? Hash(int3(int(row + (fr < 0.5 * H ? 0.0 : 1.0)), int(kl), int(estId))) : Hash(int3(int(kl + (dLo < dHi ? 0.0 : 1.0)), 3, int(estId) + 9));
		fc = float2(0.5 * (lo + hi), (row + 0.5) * H);
	}
	else
	{
		float2 q = float2(uu / W, vv / H);
		float2 iq = floor(q);
		float b1 = 1e9; float2 sA = 0; int2 cA = 0; uint hA = 0;
		for (int j = -1; j <= 1; j++) for (int i = -1; i <= 1; i++)
		{
			int2 c = int2(iq) + int2(i, j);
			uint hh = Hash(int3(c, int(estId & 0x7FFFu) + 13));
			float2 s = float2(c) + 0.02 + 0.96 * float2(hh & 0x3FFu, (hh >> 10) & 0x3FFu) / 1023.0;
			float dd = dot(s - q, s - q);
			if (dd < b1) { b1 = dd; sA = s; cA = c; hA = hh; }
		}
		float bE = 1e9; float2 nE2 = 0; uint hB = 0;
		for (int j2 = -2; j2 <= 2; j2++) for (int i2 = -2; i2 <= 2; i2++)
		{
			int2 c = cA + int2(i2, j2);
			if (all(c == cA)) continue;
			uint hh = Hash(int3(c, int(estId & 0x7FFFu) + 13));
			float2 s = float2(c) + 0.02 + 0.96 * float2(hh & 0x3FFu, (hh >> 10) & 0x3FFu) / 1023.0;
			float2 nn = normalize(s - sA);
			float2 mg = float2(nn.x / W, nn.y / H);       // metric gradient of the bisector distance
			float e = dot(0.5 * (sA + s) - q, nn) / length(mg);
			if (e < bE) { bE = e; nE2 = normalize(mg); hB = hh; }
		}
		de = bE; eN = nE2;
		fid = hA;
		edgeId = Hash(int3(int(min(hA, hB) & 0xFFFFFu), int(max(hA, hB) & 0xFFFFFu), 41));
		fc = float2(sA.x * W, sA.y * H);
	}
	bool estEdge = deE < de;
	float dEdge = min(de, deE);

	// ---- what grows in this field: decided per field (at its centre), so the farmland frays field by field
	float3 fcW = normalize(ctr + (fc.x * along.x + fc.y * across.x) * t1 + (fc.x * along.y + fc.y * across.y) * t2);
	float4 lc = LandTex.SampleLevel(LandTexSampler, OctUV(fcW), 0.0);
	float farmF = lc.r;
	float r1 = Rand01(fid), r2 = Rand01(fid >> 16), r3 = Rand01(Hash(int3(int(fid & 0xFFFu), 5, 9)));
	float fray = 0.22 * (nF + 0.6 * nA) + 0.12 * nE;
	bool cult = r1 < saturate(farmF * 1.3 - 0.08 + fray);
	bool wild = r1 > saturate(farmF * 2.2 + 0.25 + fray);          // beyond the outfields: untouched land
	float steepF = saturate((gm - 0.12) / 0.1);
	if (steepF > r2) cult = false;                               // steep: grazing or wood, never crops
	// riparian strip: no ploughing within ~25-60 m of a river bank
	if (rivDm < 25.0 + 35.0 * r3) cult = false;

	// crop class by climate, region and chance
	float dry = saturate(arid * 1.3);
	float cold = saturate((8.0 - tc) / 10.0);
	float pick = frac(r2 + 0.35 * nE + 0.15 * nF);
	float pc = lerp(0.42, 0.55, dry) * (1.0 - 0.5 * cold);          // cereals
	float pp = 0.22 + 0.25 * cold - 0.1 * dry;                       // pasture
	float ph = 0.10 + 0.08 * cold;                                    // hay
	float pr = 0.12 * (1.0 - cold);                                   // row crops
	float pf = 0.08;                                                  // fallow
	int cls = pick < pc ? 0 : (pick < pc + pp ? 1 : (pick < pc + pp + ph ? 2 : (pick < pc + pp + ph + pr ? 3 : (pick < pc + pp + ph + pr + pf ? 4 : 5))));
	if (cold > 0.6 && cls == 5) cls = 1;
	float sF = frac(season + 0.06 * (r3 - 0.5) + 1.0);
	float3 crop = CropColour(cls, sF, soil, arid, tc, r3);
	crop *= lerp(0.88, 1.12, Rand01(fid >> 8));                          // field-to-field tone
	float nG = Noised(Pm / 70.0 + float3(float(fid & 0xFFu), 2.0, 5.0)).x * AAW(70.0, fp);
	crop *= 1.0 + 0.16 * nG + 0.10 * nB + 0.10 * nC + 0.06 * nD;          // moisture, soil and growth inside a field
	float nG2 = Noised(Pm / 23.0 + float3(7.0, float(fid & 0xFFu), 1.0)).x * AAW(23.0, fp);
	crop *= 1.0 + 0.08 * nG2;
	crop = lerp(crop, crop.ggg * float3(0.85, 1.0, 0.6), 0.25 * saturate(nG + 0.3 * nC));  // greener in damp patches
	// harvest in progress: the combine's lanes run round the field parallel to its edges (cut stubble paler)
	if (cls == 0 && age > 0.55)
	{
		float hv = Smooth01((sF - 0.43) / 0.02) * (1.0 - Smooth01((sF - 0.49) / 0.02));
		float cutTo = lerp(15.0, 0.5 * min(W, H), r2);
		float lane = (1.0 - Smooth01((dEdge - cutTo) / max(fp, 0.5))) * hv;
		crop = lerp(crop, float3(0.235, 0.210, 0.145) * (1.0 + 0.1 * sin(dEdge * 6.2832 / 6.0) * AAW(6.0, fp)), lane);
	}
	// headland: compacted turning strip along the field edge
	crop *= 1.0 + 0.10 * (1.0 - Smooth01((dEdge - 4.0) / 3.0)) * AAW(8.0, fp) * (cls == 1 ? 0.0 : 1.0);
	// tramlines in modern cereals, ridge and furrow in old ploughland
	float rowsAxis = layout == 0 ? v : u;
	if ((cls == 0 || cls == 3 || cls == 4) && age > 0.55)
	{
		float tl = abs(frac(rowsAxis / 21.0) - 0.5) * 21.0;
		crop *= 1.0 + 0.18 * (1.0 - Smooth01((tl - 0.25) / max(0.4 * fp, 0.2))) * AAW(2.0, fp) * Smooth01((sF - 0.15) / 0.05) * (1.0 - Smooth01((sF - 0.5) / 0.03));
	}
	float rf = 0;
	if ((cls == 0 || cls == 4 || cls == 2) && age < 0.5)
	{
		float ph2 = (layout == 0 ? u : v) / 11.0 * 6.2832;
		rf = sin(ph2) * AAW(11.0, fp);
		crop *= 1.0 + 0.06 * rf;
	}
	float rowsS = sin(rowsAxis * 6.2832 / 0.8) * AAW(0.8, fp);
	crop *= 1.0 + 0.10 * rowsS * (cls == 1 ? 0.0 : 1.0);

	// meadow / rough grazing for uncultivated land in the outfield
	float3 meadow = lerp(float3(0.085, 0.115, 0.042), float3(0.17, 0.16, 0.085), saturate(arid * 1.2 + 0.25 * nA)) * (1.0 + 0.12 * nB + 0.08 * nC);

	// what the fields average to when they are smaller than a pixel (kept mottled per estate)
	float3 avg = lerp(CropColour(0, season, soil, arid, tc, 0.5), CropColour(1, season, soil, arid, tc, 0.5), 0.45 + 0.3 * (rE - 0.5));
	avg *= lerp(0.9, 1.1, rE2) * (1.0 + 0.08 * nF);
	float kF = AAW(min(W, H), fp);
	float3 fieldC = cult ? crop : meadow;
	fieldC = lerp(lerp(avg, meadow, saturate(1.0 - farmF * 1.5)), fieldC, kF);

	// woodlots: some uncultivated outfields are copses even outside the forest
	bool woodlot = !cult && r3 < 0.18 + 0.5 * saturate(forest * 1.5);
	gForestK = wild ? 1.0 : (cult ? 0.0 : (woodlot ? 2.0 : 0.35));
	gForestK = lerp(1.0 - saturate(farm * 1.3), gForestK, kF);

	// ---- hedgerows and tracks along edges; trees along hedges
	float bocage = saturate(0.35 + 0.5 * nE + 0.35 * (1.0 - dry) - 0.6 * age);
	bool hasHedge = Rand01(edgeId) < bocage && !wild;
	bool track = estEdge && Rand01(hE2 ^ h1) < 0.45 && !wild;
	float hw = lerp(1.8, 3.2, Rand01(edgeId >> 5));
	float wH = max(hw, 0.5 * fp);
	float hedge = hasHedge ? saturate((wH - dEdge) / max(0.6 * fp, 0.4) + 0.5) * min(1.0, hw / wH) : 0.0;
	// hedge trees: crowns of ~7 m threaded along the hedge
	float3 offT; float3 crT = Crowns(Pm + float3(5.0, 11.0, 2.0), 7.0, offT);
	float treeLine = hasHedge ? (1.0 - Smooth01((dEdge - lerp(2.0, 4.5, crT.y)) / 1.5)) * step(0.45, crT.y) * AAW(7.0, fp) : 0.0;
	hedge = max(hedge, treeLine * crT.x);
	float tw = 1.6;
	float wT = max(tw, 0.5 * fp);
	float trk = track ? saturate((wT - deE) / max(0.6 * fp, 0.3) + 0.5) * min(1.0, tw / wT) : 0.0;
	gHedge = hedge * kF + (hasHedge ? 0.06 * (1.0 - kF) : 0.0);
	// hedge shadow on the far side from the sun
	float3 sunT = SunD - up * dot(SunD, up);
	float sinE = max(dot(SunD, up), 0.05);
	float2 eNw2 = eN.x * along + eN.y * across;
	float3 eNw = normalize(eNw2.x * t1 + eNw2.y * t2);
	float facing = dot(eNw, sunT);                                    // > 0: the hedge stands between this field and the sun
	float shL = 5.0 * sqrt(max(1.0 - sinE * sinE, 0.0)) / sinE;
	float shd = hasHedge && facing > 0.0 ? 1.0 - Smooth01((dEdge - hw - shL * facing) / max(0.6 * fp, 0.6)) : 0.0;
	gShadow = shd * AAW(shL * facing + 2.0, fp) * (1.0 - hedge);
	gNOff = hasHedge ? (crT.x > 0.0 ? 0.5 * (offT - up * dot(offT, up)) * treeLine * AAW(7.0, fp) : 0.0) : 0.0;
	// ridge and furrow shading
	gNOff += rf * 0.10 * (layout == 0 ? along.x * t1 + along.y * t2 : across.x * t1 + across.y * t2) * (cult ? 1.0 : 0.0);

	float3 trkC = float3(0.20, 0.175, 0.13) * (1.0 + 0.1 * nD);
	// grass margin between fields (uncultivated balk, 1.5-3 m)
	float mw = lerp(1.2, 2.6, r3);
	float margin = cult ? (1.0 - Smooth01((dEdge - mw) / max(0.6 * fp, 0.3))) * min(1.0, mw / max(mw, 0.5 * fp)) : 0.0;
	fieldC = lerp(fieldC, meadow * 1.1, margin * 0.8);
	float3 c = lerp(fieldC, trkC, trk * 0.85);

	// ---- settlement core: house plots, kitchen gardens, orchards and garden trees on an 18 m grid aligned
	// with the fields. Roofs are returned separately (drawn over the canopy). The towns module places the
	// real buildings; this is the ground they stand on and the far-LOD speckle that reads from 5-20 km.
	float ck = Smooth01((coreIn - 0.10) / 0.10);
	if (ck > 0.0)
	{
		const float pc = 18.0;
		float2 hq = float2(u, v) / pc;
		float2 hi2 = floor(hq), hf = frac(hq) - 0.5;
		uint hh = Hash(int3(int2(hi2), int(estId & 0xFFFu) + 3));
		// the built-up edge is ragged plot by plot (each plot joins the village above its own threshold)
		ck = Smooth01((coreIn - 0.30 + 0.16 * nB + 0.10 * nA + 0.05 * nC) / 0.04);
		float pHouse = 0.12 + 0.55 * coreIn * coreIn;
		float rr0 = Rand01(hh);
		bool house = rr0 < pHouse;
		float2 hs = float2(lerp(0.22, 0.34, Rand01(hh >> 3)), lerp(0.16, 0.26, Rand01(hh >> 9)));
		float2 hoff = (float2(Rand01(hh >> 5), Rand01(hh >> 11)) - 0.5) * (0.5 - hs) * 1.6;
		float2 dq = abs(hf - hoff) - hs;
		float roofIn = house ? 1.0 - Smooth01(max(dq.x, dq.y) * pc / max(0.7 * fp, 0.3) + 0.5) : 0.0;
		float rr = Rand01(hh >> 13);
		float3 roof = rr < 0.45 ? float3(0.30, 0.13, 0.07) : (rr < 0.8 ? float3(0.36, 0.355, 0.34) : float3(0.38, 0.31, 0.21));
		roof *= lerp(0.85, 1.15, Rand01(hh >> 17));
		// plots: kitchen garden (striped beds), grass and orchard, a bare yard round each house
		float gsel = Rand01(hh >> 21);
		float beds = sin((hf.x + 0.5) * pc * 6.2832 / 2.2) * AAW(2.2, fp);
		float3 garden = gsel < 0.3 ? lerp(soil * 0.9, float3(0.05, 0.09, 0.03), 0.5 + 0.4 * beds) : (gsel < 0.85 ? float3(0.075, 0.11, 0.04) * (1.0 + 0.2 * nC) : soil * 1.1);
		float yardM = house ? 1.0 - Smooth01((max(dq.x, dq.y) * pc - 3.0) / 2.0) : 0.0;
		garden = lerp(garden, float3(0.15, 0.13, 0.095), yardM * 0.8);
		// plot boundaries (fences, paths)
		float pb = min(0.5 - abs(hf.x), 0.5 - abs(hf.y)) * pc;
		garden *= 1.0 - 0.25 * (1.0 - Smooth01((pb - 0.6) / max(0.5 * fp, 0.3))) * AAW(4.0, fp);
		float kR = Smooth01((pc / max(fp, 0.01) - 1.0) / 1.5);
		float3 gardenAvg = float3(0.10, 0.11, 0.06);
		float3 roofAvg = float3(0.33, 0.25, 0.18);
		float3 vill = lerp(gardenAvg, garden, kR);
		c = lerp(c, vill, ck);
		float roofCov = lerp(pHouse * 0.25, roofIn, kR);
		gRoof = float4(lerp(roofAvg, roof, kR), roofCov * ck);
		gForestK = lerp(gForestK, gsel > 0.55 && !house ? 1.75 : 1.35, ck);  // orchards and garden trees (>1: planted, density = k - 1)
		gHedge *= 1.0 - ck;
		gShadow *= 1.0 - ck;
	}

	gCol = c;
	gMask = wild ? 0.0 : saturate(farm * 6.0 + coreIn * 4.0);
	if (dbg > 2.5) { gCol = float3(coreIn, ck, gRoad); gMask = 1.0; gRoof = 0; return; }
	if (dbg > 0.5)
	{
		// 1: mode (red contour, blue river lots, green regional) x edges; 2: orientation of the long axis
		float3 mc = contourW > 0.5 ? float3(1, 0.1, 0.1) : (riverLots ? float3(0.1, 0.2, 1) : float3(0.1, 1, 0.1));
		float2 al = along;
		float3 ow = normalize(al.x * t1 + al.y * t2);
		float3 oc = abs(float3(dot(ow, TE.xyz), dot(ow, TN.xyz), 0));
		gCol = (dbg < 1.5 ? mc * (0.3 + 0.7 * Rand01(fid)) : float3(oc.x, oc.y, 0.2)) * (dEdge < 3.0 ? 0.2 : 1.0) * 0.5;
		gMask = 1.0;
	}
}
