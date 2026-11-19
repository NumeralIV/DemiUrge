// DEMIURGE — js/sim/climate.js : DM.Climate (SPEC-SIM §3.3). Temperature, rainfall, ice, greenhouse gas, dust and sea level.
//
//   Climate.update(world)           temp and moist for every cell; clears need.climate; sets need.life and dirty.color
//                                   (and need.hydro when the rainfall of some land cell has moved by more than
//                                   TUNE.climate.hydroDelta since the rivers were last asked to follow it)
//   Climate.stepIce(world)          ice 6 % of the way toward its equilibrium (30 % in genesis phases), the equilibrium being the
//                                   one Climate.calibrate sets: with the albedo term settled; keeps temp in step with the ice
//   Climate.yearly(world)           dust decay always. On a calibrated world in history / ended also: CO₂ uptake, globalTemp,
//                                   sea level, the need / dirty flags that follow, climate:milestone events. Without a finite
//                                   vars.seaBase it never touches the sea (or the greenhouse); with a seaBase but no iceRef
//                                   (hand-made worlds) the first call finishes the calibration: ice to equilibrium, iceRef
//   Climate.calibrate(world)        when history begins: vars.seaBase = seaLevel, temperatures, ice put at its equilibrium,
//                                   vars.iceRef = max(50, Σ ice over land cells), rainfall
//   Climate.datum(world)            height the lapse rate is measured from: max(0, vars.seaBase) once calibrated, else max(0, seaLevel)
//   Climate.anomaly(world)          globalTemp − 9 × dust (what the HUD shows and milestones use)
//   Climate.emit(world, amount)     add to world.co2 (clamped 0..300)
//   Climate.addDust(world, amount)  add to world.dust (clamped 0..1); sets need.climate
//   Climate.landIce(world)          Σ ice over land cells (extra; read-only)
//
// Writes: temp, moist, ice, co2, dust, globalTemp, seaLevel (yearly, history / ended only), world._up,
// vars.seaBase, vars.iceRef and its own vars.climate_ice0, vars.climate_seaMark, vars.climate_ms.
// Never draws from world.rng: the same world always gets the same climate.
//
// Temperature. cos(latitude)^1.3 profile from +29 °C at the equator to −24 °C at the poles, plus a seed-dependent
// low-frequency wobble, minus 5.5 °C per unit of LAND height above Climate.datum (never the live sea level: a
// world whose sea is still at −9 is not frozen, and a rising sea does not warm the land), plus the anomaly, minus
// 4 °C × ice. A sea cell's stored temp stops at −2; sea ice uses the unclamped value.
//
// Rainfall. Winds by latitude band (easterly trades below 30°, westerlies 30–60°, polar easterlies beyond; the
// trades also drift toward the equator, the westerlies toward the pole; directions from geo.basis, whose east is
// the direction of DEcreasing geo.lon). Each cell takes its air from up to three neighbours on its upwind side
// (world._up is the strongest link). A link only counts as far as the wind AT the neighbour also blows toward
// the cell, so where the wind turns round (the calm lines at 30° and 60°) part of a cell's air is local instead:
// dry sinking air at 30°, moist rising air at 60°. Without that rule neighbours across a calm line name each
// other as upwind and trap humidity in a loop that no number of sweeps settles.
//   Sea cells: the air picks up moisture toward a source value (warm sea gives more; sea ice seals it).
//   Land cells: every step rains out a share of what the air carries — more in the rainy latitudes (equatorial
//   trough, mid-latitude storm belt; least under the subtropical high), more in air fresh off the sea (wet
//   windward coasts, dry lee coasts), more where the air is forced uphill, less where it sinks — and what the
//   thinner air over high ground cannot hold falls on the windward slope, so the lee lies in a rain shadow. Part
//   of the rain evaporates again (most in the hot forest belt) and moves on; a trickle of moisture mixes down
//   from aloft as long as the planet has a sea, so the deep interiors of the rainy latitudes are not deserts.
// The equations are monotone and piecewise linear in the humidity, so with the cells visited upwind-first (two
// orders computed once per grid) a few sweeps settle it however wide the continent. Every rate is per reference
// cell (L = 6), so coarser test grids get the same climate.
// moist = sat(squeeze(rain / RAIN_REF) + wet − 0.25 × drought): RAIN_REF is a fixed number, so changing one place
// never rescales another, and a world with no sea is bone dry; squeeze leaves values below rainKnee (0.7) alone
// and bends the ones above smoothly toward 1, so the wettest coasts and slopes do not all read exactly 1.
// Sea cells get the rain that falls there.
//
// Ice. Land ice 0 at −4 °C … 1 at −9 °C; sea ice 0 at −2.5 … 1 at −6 (unclamped). With the albedo term the land edge
// is a unique fixed point; the sea edge is bistable over half a degree, so sea ice melts a little later than it
// forms. A world without a sea has no snow to make ice from (iceNeedsSea).
//
// Sea level (yearly). r = 1 + (Σ land ice − ice at calibration) / vars.iceRef, which is the spec's Σ / iceRef whenever the
// world had more than the floor of 50 at calibration and keeps r = 1 at calibration on worlds with less; the rest is
// the spec's rule. An undisturbed world keeps its sea level exactly.
//
// Milestones (payload { kind, value }; each kind/value pair fires once, except that a thaw re-arms the two winter ones):
//   warming  value 1, 2, 3, 4     anomaly reached +1 … +4 °C
//   cooling  value −2             anomaly fell to −2 °C          ice-age  value −4    … to −4 °C
//   thaw     value −1             the anomaly is back above −1 °C after a cooling / ice-age (which are then re-armed,
//                                 so a later winter is announced again)
//   sea-rise value 0.2, 0.4       seaLevel − vars.seaBase reached it        sea-fall  value −0.2, −0.4
(function (DM) {
  'use strict';
  const Climate = (DM.Climate = DM.Climate || {});
  const TUNE = (DM.TUNE = DM.TUNE || {});

  TUNE.climate = {
    // ---- temperature (°C)
    tEquator: 29,          // sea-level annual mean at the equator …
    tPole: -24,            // … and at the poles
    tShape: 1.3,           // T = tPole + (tEquator − tPole) × cos(lat)^tShape  (30°: 20, 45°: 10, 60°: −2.5)
    tNoise: 8,             // amplitude of the wobble that keeps isotherms from being circles (the fbm it scales stays within ±0.7: ±5 °C at most, 2 °C typical)
    tNoiseFreq: 1.3,
    lapse: 5.5,            // per world unit of land height above Climate.datum
    albedo: 4,             // cooling of a fully ice-covered cell
    dustCool: 9,           // cooling per unit of world.dust
    seaTempMin: -2,        // floor of the stored temp of a sea cell
    // ---- ice
    landIce0: -4, landIce1: -9,        // land ice: 0 at landIce0 ramping to 1 at landIce1
    seaIce0: -2.5, seaIce1: -6,        // sea ice, on the unclamped sea temperature
    iceRate: 0.06,         // share of the gap closed per stepIce call in history / ended
    iceRateGenesis: 0.30,  // … in the genesis phases
    iceSnap: 0.004,        // closer than this to the equilibrium: land on it
    iceNeedsSea: 0.05,     // land ice needs snow: full strength once this share of the cells is sea, none on a sea-less world
    iceRefMin: 50,         // floor of vars.iceRef
    // ---- winds
    band1: 30, band2: 60,  // latitudes (degrees) where trades / westerlies / polar easterlies meet
    bandBlend: 5,          // half-width of the blend at those edges
    merid: 0.45,           // strength of the drift toward the equator / pole relative to the east–west wind
    windMix: 0.2,          // share of a cell's air that arrives from the two sides instead of from upwind (sideways mixing)
    windAgree: 0.4,        // an upwind link counts in full when the neighbour's own wind points at the cell at least this well (cosine)
    subsHum: 0.2,          // humidity, relative to sea air, of the local air a cell gets instead where links fail: at the 30° calm line …
    frontHum: 0.8,         // … and at the 60° one
    // ---- humidity sources (sea cells)
    srcT0: -2, srcT1: 29,  // source humidity rises linearly from srcCold at srcT0 to 1 at srcT1 …
    srcCold: 0.5,
    srcMin: 0.25, srcMax: 1.2,          // … within these bounds
    srcIce: 0.75,          // share of the source sealed off by full sea ice
    beltEq: 0.15, beltEqW: 9,           // wet belt at the equator: + beltEq, gaussian half-width in degrees
    dryLat: 28, dryW: 8, beltDry: 0.15, // dry belt: − beltDry around dryLat
    evap: 0.14,            // share of the gap to the source value closed per reference cell of sea crossed
    // ---- rain
    rainout: 0.05,         // share of the air's moisture that falls per reference cell where lift = 1
    liftBase: 0.3,         // rain-making lift by latitude: base …
    liftEq: 0.75, liftEqW: 14,          // … + the equatorial trough …
    liftMid: 0.75, midLat: 48, midW: 14, // … + the mid-latitude storm belt …
    liftDry: 0.08,         // … − the subtropical high (at dryLat, width dryW)
    liftMin: 0.08,
    coastLift: 0.5,        // extra lift in air fresh off the sea (windward coasts) …
    coastLen: 10,          // … fading over this many reference cells of land
    coastPasses: 3,        // sweeps for that sea-air field
    rainNoise: 0.35,       // ± seed-dependent variation of the lift
    reliefSmooth: 2,       // smoothing passes over the relief before the uphill / downhill terms
    upK: 2.5, upMax: 0.8,  // air forced uphill: rain-out × (1 + upK × climb per reference cell, at most upMax)
    desc: 0.8, descK: 1.5, // sinking air: rain-out × (1 − desc × sat(descK × drop per reference cell))
    capK: 1.2,             // air over land carries at most capK × (sea-air humidity of that latitude) × exp(−height / capH);
    capH: 3,               // capDump of the excess falls per reference cell (windward slopes); beyond the crest the air stays that dry
    capDump: 0.6,
    seep: 0.007,           // moisture mixed down from aloft per reference cell of land, × the sea-air humidity of that latitude …
    seepSea: 0.3,          // … at full strength once this share of the planet is sea (none on a sea-less world)
    recycleCold: 0.42, recycleHot: 0.82,   // share of the rain that evaporates again, by temperature …
    recycleT0: 0, recycleT1: 24,           // … between these temperatures
    passes: 10,            // humidity sweeps, alternately downwind in longitude and in latitude
    rainRef: 0.06,         // RAIN_REF: rain per reference cell that reads as moist = 1 (a wet equatorial coast ≈ 0.9)
    rainKnee: 0.7,         // rain / RAIN_REF above this is squeezed smoothly toward 1 instead of clipped (1 = plain clipping)
    droughtDry: 0.25,      // moist lost under a drought of power 1
    droughtRim: 0.3,       // outer share of a drought's radius over which it fades out
    hydroDelta: 0.08,      // a land cell's moist moved by more than this since need.hydro was last raised here → need.hydro
    // ---- yearly
    dustDecay: 0.93,
    uptake: 0.005,         // co2 −= co2 × uptake × (0.5 + Life.cover(world).veg)
    vegDefault: 0.5,       // cover.veg assumed while DM.Life is not there
    co2Max: 300,
    sens: 3,               // globalTemp → sens × log2(1 + co2 / 100) …
    lag: 30,               // … closing 1 / lag of the gap per year
    seaIceGain: 0.35,      // sea rise when all land ice is gone
    seaIceLoss: 0.25,      // sea fall when the land ice has doubled
    seaTherm: 0.04,        // sea rise per °C of globalTemp
    seaLo: 0.3, seaHi: 0.6,            // bounds of the sea level relative to vars.seaBase
    seaRate: 0.005,        // largest change per year
    seaStep: 0.03,         // the sea moved this far → derive / hydro / climate and the renderer's water
    anomStep: 0.25,        // the anomaly moved this far since the last update → need.climate
    warmAt: [1, 2, 3, 4], coolAt: -2, iceAgeAt: -4, thawAt: -1,
    seaAt: [0.2, 0.4],
  };

  const REF_STEP = 0.0189;           // geo.step of the reference world (L = 6); rates are per reference cell
  const DEG = 180 / Math.PI;
  const GENESIS = { molten: 1, water: 1, life: 1, people: 1 };
  // climate_ms bits
  const MS_COOL = 16, MS_ICEAGE = 32, MS_RISE = 6, MS_FALL = 8;   // warming: bits 0–3; rise: bits 6–7; fall: bits 8–9

  function fin(v, d) { return typeof v === 'number' && v === v && v !== Infinity && v !== -Infinity ? v : d; }
  function sat(x) { return x < 0 ? 0 : x > 1 ? 1 : x; }
  function gauss(x, c, w) { const d = (x - c) / w; return Math.exp(-d * d); }
  function sstep(e0, e1, x) { const t = sat((x - e0) / (e1 - e0)); return t * t * (3 - 2 * t); }
  // 0 at t >= e0, 1 at t <= e1 (e1 < e0)
  function coldRamp(e0, e1, t) { return e0 > e1 ? sat((e0 - t) / (e0 - e1)) : (t < e0 ? 1 : 0); }

  // ------------------------------------------------------------------ per-grid cache: winds and latitude profiles
  const grids = [];          // [{ geo, sig, … }], a handful at most (tests use several levels)

  function gridSig(T) {
    return [T.band1, T.band2, T.bandBlend, T.merid, T.windMix, T.windAgree, T.subsHum, T.frontHum, T.beltEq, T.beltEqW, T.dryLat,
      T.dryW, T.beltDry, T.liftBase, T.liftEq, T.liftEqW, T.liftMid, T.midLat, T.midW, T.liftDry, T.liftMin].join();
  }

  function gridFor(geo) {
    const T = TUNE.climate, sig = gridSig(T);
    for (let i = 0; i < grids.length; i++) {
      if (grids[i].geo === geo) { if (grids[i].sig === sig) return grids[i]; grids.splice(i, 1); break; }
    }
    const G = buildGrid(geo, T);
    G.sig = sig;
    if (grids.length >= 4) grids.shift();
    grids.push(G);
    return G;
  }

  function buildGrid(geo, T) {
    const n = geo.n, pos = geo.pos, nbr = geo.nbr, nbrN = geo.nbrN;
    const up = new Int32Array(n), up2 = new Int32Array(n), up3 = new Int32Array(n);
    const w1 = new Float32Array(n), w2 = new Float32Array(n), w3 = new Float32Array(n);
    const wl = new Float32Array(n);                            // share of the cell's air that is local (failed links)
    const locH = new Float32Array(n);                          // humidity of that local air relative to sea air
    const wx = new Float32Array(n), wy = new Float32Array(n), wz = new Float32Array(n);   // unit direction the air moves in
    const side = sat(T.windMix) * 0.5;
    const keyLon = new Float64Array(n), keyLat = new Float64Array(n);
    const belt = new Float32Array(n), lift = new Float32Array(n);
    const E = [0, 0, 0], N = [0, 0, 0];
    const dE = [0, 0, 0, 0, 0, 0], dN = [0, 0, 0, 0, 0, 0], sc = [0, 0, 0, 0, 0, 0];
    const b1 = T.band1, b2 = T.band2, bw = Math.max(0.01, T.bandBlend), bMid = 0.5 * (b1 + b2);
    for (let c = 0; c < n; c++) {
      const px = pos[3 * c], py = pos[3 * c + 1], pz = pos[3 * c + 2];
      const a = Math.abs(geo.lat[c]) * DEG, sg = py < 0 ? -1 : 1;

      // latitude profiles
      belt[c] = Math.max(0.05, 1 + T.beltEq * gauss(a, 0, T.beltEqW) - T.beltDry * gauss(a, T.dryLat, T.dryW));
      lift[c] = Math.max(T.liftMin, T.liftBase + T.liftEq * gauss(a, 0, T.liftEqW) + T.liftMid * gauss(a, T.midLat, T.midW)
        - T.liftDry * gauss(a, T.dryLat, T.dryW));
      locH[c] = a < bMid ? T.subsHum : T.frontHum;

      // wind: u toward the east (−1 trades, +1 westerlies, −1 polar easterlies), vp toward the pole
      const u = -1 + 2 * sstep(b1 - bw, b1 + bw, a) - 2 * sstep(b2 - bw, b2 + bw, a);
      let vp;
      if (a < b1) vp = -Math.sin(Math.PI * a / b1);
      else if (a < b2) vp = Math.sin(Math.PI * (a - b1) / (b2 - b1));
      else vp = -Math.sin(Math.PI * (a - b2) / (90 - b2));
      const vn = vp * T.merid * sg;
      keyLon[c] = u > 0 ? -geo.lon[c] : geo.lon[c];           // basis east is the direction of decreasing longitude
      keyLat[c] = vn > 0 ? geo.lat[c] : vn < 0 ? -geo.lat[c] : 0;
      // where the air comes from, in the cell's (east, north) frame
      let ue = -u, un = -vn;
      const ul = Math.sqrt(ue * ue + un * un);
      if (ul < 1e-6) { ue = 1; un = 0; } else { ue /= ul; un /= ul; }

      geo.basis(c, E, N);
      wx[c] = -(ue * E[0] + un * N[0]); wy[c] = -(ue * E[1] + un * N[1]); wz[c] = -(ue * E[2] + un * N[2]);
      const k = nbrN[c];
      let best = 0;
      for (let j = 0; j < k; j++) {
        const m = nbr[6 * c + j];
        let dx = pos[3 * m] - px, dy = pos[3 * m + 1] - py, dz = pos[3 * m + 2] - pz;
        const rad = dx * px + dy * py + dz * pz;            // drop the radial part: direction in the tangent plane
        dx -= rad * px; dy -= rad * py; dz -= rad * pz;
        let e = dx * E[0] + dy * E[1] + dz * E[2], nn = dx * N[0] + dy * N[1] + dz * N[2];
        const l = Math.sqrt(e * e + nn * nn) || 1;
        e /= l; nn /= l;
        dE[j] = e; dN[j] = nn; sc[j] = e * ue + nn * un;
        if (sc[j] > sc[best]) best = j;
      }
      // the upwind direction lies between the best neighbour and one of the two beside it
      const jl = (best + k - 1) % k, jr = (best + 1) % k;
      const second = sc[jl] > sc[jr] ? jl : jr;
      // upwind = α d(best) + β d(second)
      const det = dE[best] * dN[second] - dN[best] * dE[second];
      let wt = 0;
      if (Math.abs(det) > 1e-9) {
        const al = (ue * dN[second] - un * dE[second]) / det, be = (dE[best] * un - dN[best] * ue) / det;
        if (be > 0 && al > 0) wt = be / (al + be);
        else if (be > 0) wt = 1;
      }
      // sideways mixing: a little air also comes from the neighbours on either side of the best one
      up[c] = nbr[6 * c + best]; w1[c] = (1 - wt) * (1 - 2 * side);
      up2[c] = nbr[6 * c + second]; w2[c] = wt * (1 - 2 * side) + side;
      up3[c] = nbr[6 * c + (second === jl ? jr : jl)]; w3[c] = side;
    }

    // A link c ← m carries air only as far as the wind at m blows toward c. Where the wind turns round within a
    // cell or two (the calm lines) neighbours would otherwise name each other as upwind and trap their humidity
    // in a loop; the share that fails becomes local air (wl). Sideways links are weak and stay as they are.
    const agree = Math.max(0.05, T.windAgree);
    for (let c = 0; c < n; c++) {
      const px = pos[3 * c], py = pos[3 * c + 1], pz = pos[3 * c + 2];
      let lost = 0;
      for (let i = 0; i < 2; i++) {
        const m = i ? up2[c] : up[c], w = i ? w2[c] : w1[c];
        const dx = px - pos[3 * m], dy = py - pos[3 * m + 1], dz = pz - pos[3 * m + 2];
        const l = Math.sqrt(dx * dx + dy * dy + dz * dz) || 1;
        const g = sat((wx[m] * dx + wy[m] * dy + wz[m] * dz) / (l * agree));
        if (g < 1) { const keep = w * g; lost += w - keep; if (i) w2[c] = keep; else w1[c] = keep; }
      }
      wl[c] = lost;
      if (w2[c] > w1[c]) {                                    // world._up is the strongest link
        const m = up[c], w = w1[c];
        up[c] = up2[c]; w1[c] = w2[c]; up2[c] = m; w2[c] = w;
      }
      if (w2[c] < 1e-4) { wl[c] += w2[c]; w2[c] = 0; up2[c] = up[c]; }
      if (w3[c] < 1e-4) { wl[c] += w3[c]; w3[c] = 0; up3[c] = up[c]; }
    }

    // Two visiting orders in which (nearly) every cell comes after the cells its air arrives from: one runs
    // downwind in longitude (right for the east–west winds that circle the planet inside each band), one
    // downwind in latitude (right near the calm lines, where the air mostly moves toward the equator or the
    // pole). Sweeping them alternately carries the humidity across any continent in a few sweeps; a link that
    // points the wrong way in one order (the seam where the longitude wraps, a sideways link) is simply read
    // one sweep late.
    const byKey = function (key) {
      const idx = new Array(n);
      for (let c = 0; c < n; c++) idx[c] = c;
      idx.sort(function (p, q) { return key[p] - key[q] || p - q; });
      return Int32Array.from(idx);
    };
    const orderLon = byKey(keyLon), orderLat = byKey(keyLat);
    return { geo, n, up, up2, up3, w1, w2, w3, wl, locH, orderLon, orderLat, belt, lift, scale: Math.max(0.05, geo.step / REF_STEP) };
  }

  // ------------------------------------------------------------------ per-world cache
  // world._clim = { n, seed, sig, base, wob, t0, mh, anom, wet, aloft }
  //   base  sea-level temperature of each cell without anomaly, lapse and ice     wob  rain-lift variation (≈ 1)
  //   t0    temperature of the last update without the ice term and without the sea clamp
  //   mh    moist as it was when need.hydro was last raised from here
  //   anom  the anomaly that update used (NaN before the first)
  //   wet   0..1: enough sea for snow      aloft  0..1: enough sea to keep the upper air moist
  function cacheFor(world) {
    const T = TUNE.climate, n = world.n, seed = world.seed >>> 0;
    const sig = [T.tEquator, T.tPole, T.tShape, T.tNoise, T.tNoiseFreq, T.rainNoise].join();
    let C = world._clim;
    if (C && C.n === n && C.seed === seed && C.sig === sig) return C;
    const U = DM.U, pos = world.geo.pos;
    const base = new Float32Array(n), wob = new Float32Array(n);
    const ox = U.hashf(seed, 101) * 64, oy = U.hashf(seed, 102) * 64, oz = U.hashf(seed, 103) * 64;
    const f = T.tNoiseFreq, span = T.tEquator - T.tPole;
    for (let c = 0; c < n; c++) {
      const x = pos[3 * c], y = pos[3 * c + 1], z = pos[3 * c + 2];
      const cl = Math.sqrt(Math.max(0, 1 - y * y));
      base[c] = T.tPole + span * Math.pow(cl, T.tShape) + T.tNoise * U.fbm3(x * f + ox, y * f + oy, z * f + oz, 2);
      wob[c] = Math.max(0.4, 1 + T.rainNoise * U.noise3(x * 2.1 - oy, y * 2.1 + oz, z * 2.1 - ox));
    }
    C = world._clim = { n, seed, sig, base, wob, t0: new Float32Array(n), mh: null, anom: NaN, wet: 0, aloft: 0 };
    return C;
  }

  // ------------------------------------------------------------------ scratch (keyed by cell count)
  let S = null;
  function scratch(n) {
    if (S && S.n === n) return S;
    S = {
      n,
      z: new Float32Array(n),       // height of the surface above the sea surface (0 on sea cells), smoothed
      z2: new Float32Array(n),
      mar: new Float32Array(n),     // 1 over open sea, fading downwind over land: how fresh off the sea the air is
      hum: new Float32Array(n),     // humidity leaving each cell
      a: new Float32Array(n),       // hum = a × (humidity arriving over the links) + b, then the height cap
      b: new Float32Array(n),
      li: new Float32Array(n),      // humidity arriving as local air
      cap: new Float32Array(n),
      rc: new Float32Array(n),      // sea: rain = rc × hum; land: rain = rc × (humidity arriving) + what the slope wrings out
      rain: new Float32Array(n),
      dr: new Float32Array(n),      // drought strength
      list: new Int32Array(n),      // geo.within result
    };
    return S;
  }

  // ------------------------------------------------------------------ small public pieces
  Climate.datum = function (world) {
    const b = world.vars ? world.vars.seaBase : undefined;
    if (typeof b === 'number' && Number.isFinite(b)) return b > 0 ? b : 0;
    const s = fin(world.seaLevel, 0);
    return s > 0 ? s : 0;
  };

  Climate.anomaly = function (world) {
    return fin(world.globalTemp, 0) - TUNE.climate.dustCool * fin(world.dust, 0);
  };

  Climate.emit = function (world, amount) {
    if (typeof amount !== 'number' || !(amount === amount)) return;   // NaN adds nothing
    const v = fin(world.co2, 0) + amount, max = TUNE.climate.co2Max;
    world.co2 = v < 0 ? 0 : v > max ? max : v;
  };

  Climate.addDust = function (world, amount) {
    if (typeof amount !== 'number' || !(amount === amount)) return;
    const v = fin(world.dust, 0) + amount;
    world.dust = v < 0 ? 0 : v > 1 ? 1 : v;
    world.need.climate = true;
  };

  Climate.landIce = function (world) {
    const elev = world.elev, ice = world.ice, sea = world.seaLevel, n = world.n;
    let sum = 0;
    for (let c = 0; c < n; c++) if (elev[c] >= sea) sum += ice[c];
    return fin(sum, 0);
  };

  // ------------------------------------------------------------------ temperature
  // Fills C.t0 (no ice term, no sea clamp) and world.temp from the current elevation, sea, anomaly and ice.
  function temps(world, C) {
    const T = TUNE.climate, n = world.n;
    const elev = world.elev, ice = world.ice, temp = world.temp, base = C.base, t0 = C.t0;
    const sea = world.seaLevel, datum = Climate.datum(world), an = Climate.anomaly(world);
    const lapse = T.lapse, alb = T.albedo, seaMin = T.seaTempMin;
    let seaCells = 0;
    for (let c = 0; c < n; c++) {
      const e = elev[c];
      let t = base[c] + an, ic = ice[c];
      if (!(ic >= 0 && ic <= 1)) ice[c] = ic = ic > 1 ? 1 : 0;       // repair (NaN included) before anything reads it
      if (e >= sea) {                                       // land
        if (e > datum) t -= lapse * (e - datum);
        t0[c] = t;
        temp[c] = t - alb * ic;
      } else {
        seaCells++;
        t0[c] = t;
        t -= alb * ic;
        temp[c] = t < seaMin ? seaMin : t;
      }
    }
    C.anom = an;
    C.wet = T.iceNeedsSea > 0 ? sat(seaCells / (n * T.iceNeedsSea)) : 1;
    C.aloft = T.seepSea > 0 ? sat(seaCells / (n * T.seepSea)) : (seaCells > 0 ? 1 : 0);
  }

  // ------------------------------------------------------------------ rainfall
  function droughts(world, sc) {
    const T = TUNE.climate, dr = sc.dr, list = sc.list, geo = world.geo, pos = geo.pos, n = world.n;
    const ds = world.disasters;
    let any = false;
    if (!ds) return false;
    for (let i = 0; i < ds.length; i++) {
      const d = ds[i];
      if (!d || d.type !== 'drought') continue;
      const cell = d.cell, ang = d.ang, power = sat(fin(d.power, 0));
      if (!(cell >= 0 && cell < n) || !(ang > 0) || ang === Infinity || !(power > 0)) continue;
      if (!any) { dr.fill(0); any = true; }
      const cnt = geo.within(cell, ang, list);
      const cx = pos[3 * cell], cy = pos[3 * cell + 1], cz = pos[3 * cell + 2];
      const rim = sat(T.droughtRim);
      for (let j = 0; j < cnt; j++) {
        const c = list[j];
        let s = power;
        if (rim > 0) {
          const dot = cx * pos[3 * c] + cy * pos[3 * c + 1] + cz * pos[3 * c + 2];
          const t = Math.acos(dot > 1 ? 1 : dot < -1 ? -1 : dot) / ang;       // 0 centre … 1 edge
          const k = sat((1 - t) / rim);
          s *= k * k * (3 - 2 * k);
        }
        if (s > dr[c]) dr[c] = s;
      }
    }
    return any;
  }

  // Fills world.moist. Returns the largest change of moist on a land cell since need.hydro was last raised.
  function rainfall(world, C) {
    const T = TUNE.climate, n = world.n, G = gridFor(world.geo), sc = scratch(n);
    const elev = world.elev, ice = world.ice, wet = world.wet, moist = world.moist, t0 = C.t0, wob = C.wob;
    const up = G.up, up2 = G.up2, up3 = G.up3, w1 = G.w1, w2 = G.w2, w3 = G.w3, wl = G.wl, locH = G.locH;
    const orderLon = G.orderLon, orderLat = G.orderLat, belt = G.belt, lift = G.lift, s = G.scale;
    const z = sc.z, z2 = sc.z2, mar = sc.mar, hum = sc.hum, A = sc.a, B = sc.b, li = sc.li, cap = sc.cap, rc = sc.rc, rain = sc.rain;
    const nbr = world.geo.nbr, nbrN = world.geo.nbrN;
    const sea = world.seaLevel, alb = T.albedo;
    world._up = up;
    if (!C.mh || C.mh.length !== n) C.mh = new Float32Array(n);
    const mh = C.mh;

    // relief as the air feels it: height above the sea surface, smoothed so single-cell bumps do not wring out rain
    let anySea = false;
    for (let c = 0; c < n; c++) {
      if (elev[c] >= sea) z[c] = elev[c] - sea; else { z[c] = 0; anySea = true; }      // (a cell is land iff elev >= sea, here as everywhere)
    }

    if (anySea) {
      for (let p = T.reliefSmooth | 0; p > 0; p--) {
        for (let c = 0; c < n; c++) {
          const o = 6 * c, k = nbrN[c];
          let sum = 2 * z[c];
          for (let j = 0; j < k; j++) sum += z[nbr[o + j]];
          z2[c] = sum / (2 + k);
        }
        z.set(z2);
      }

      // how fresh off the sea the air over each cell is: 1 over open water, fading downwind over land
      const marKeep = Math.exp(-s / Math.max(0.25, T.coastLen));
      for (let c = 0; c < n; c++) mar[c] = elev[c] >= sea ? 0 : 1 - sat(T.srcIce * ice[c]);
      for (let p = 0, np = Math.max(1, T.coastPasses | 0); p < np; p++) {
        const order = p & 1 ? orderLat : orderLon;
        for (let i = 0; i < n; i++) {
          const c = order[i];
          if (elev[c] >= sea) mar[c] = marKeep * (w1[c] * mar[up[c]] + w2[c] * mar[up2[c]] + w3[c] * mar[up3[c]]);
        }
      }

      const evap = 1 - Math.pow(1 - sat(T.evap), s);
      const srcK = (1 - T.srcCold) / ((T.srcT1 - T.srcT0) || 1);
      const recK = (T.recycleHot - T.recycleCold) / ((T.recycleT1 - T.recycleT0) || 1);
      const recLo = Math.min(T.recycleCold, T.recycleHot), recHi = Math.min(0.95, Math.max(T.recycleCold, T.recycleHot));
      const rainout = T.rainout, coastLift = T.coastLift, upK = T.upK, upMax = T.upMax, desc = T.desc, descK = T.descK;
      const capK = T.capK, capH = -1 / Math.max(0.05, T.capH);
      const lapse = T.lapse, datum = Climate.datum(world);
      const seep = T.seep * s * C.aloft, invS = 1 / s;
      for (let c = 0; c < n; c++) {
        const e = elev[c];
        if (!(e >= sea)) {                                     // sea: evaporation toward the source value
          const t = t0[c] - alb * ice[c];
          let src = T.srcCold + srcK * (t - T.srcT0);
          src = src < T.srcMin ? T.srcMin : src > T.srcMax ? T.srcMax : src;
          src *= belt[c] * (1 - T.srcIce * ice[c]);
          const l = wl[c] * locH[c] * src;
          li[c] = l;
          A[c] = 1 - evap; B[c] = evap * src + (1 - evap) * l; cap[c] = 1e9; hum[c] = src;
          rc[c] = rainout * lift[c] * wob[c];
        } else {                                               // land: rain-out
          // climb per reference cell along the way the air came (local air neither climbs nor sinks)
          const d = (z[c] * (1 - wl[c]) - (z[up[c]] * w1[c] + z[up2[c]] * w2[c] + z[up3[c]] * w3[c])) * invS;
          let r = rainout * (lift[c] + coastLift * mar[c]) * wob[c];
          if (d > 0) r *= 1 + upK * (d < upMax ? d : upMax);   // forced uphill: more rain
          else if (d < 0) r *= 1 - desc * sat(-d * descK);     // sinking air rains less
          const tl = t0[c] - alb * ice[c];
          let rec = T.recycleCold + recK * (tl - T.recycleT0);
          rec = rec < recLo ? recLo : rec > recHi ? recHi : rec;
          const lose = 1 - rec;                                // share of the rain that leaves the air for good
          let keep = 1 - r * lose;
          keep = Math.pow(keep < 0 ? 0 : keep, s);
          rc[c] = (1 - keep) / (lose * s);
          // sea air of this latitude (sea-level temperature of the cell): the yardstick for the terms below
          let src = T.srcCold + srcK * (t0[c] + (e > datum ? lapse * (e - datum) : 0) - T.srcT0);
          src = (src < T.srcMin ? T.srcMin : src > T.srcMax ? T.srcMax : src) * belt[c];
          const l = wl[c] * locH[c] * src;
          li[c] = l;
          // a trickle of moisture mixes down from aloft, so the deepest interior is dry, not sterile (none on a sea-less world)
          A[c] = keep; B[c] = keep * l + seep * src; hum[c] = 0;
          // What the air can carry over this ground thins with height. The excess falls on the way up (orographic
          // rain), and the air beyond the crest stays that dry (rain shadow).
          cap[c] = src * capK * Math.exp(capH * z[c]);
        }
      }

      const passes = Math.max(1, T.passes | 0), capPass = Math.pow(1 - sat(T.capDump), s), dump = (1 - capPass) * invS;
      for (let p = 0; p < passes; p++) {
        const order = p & 1 ? orderLat : orderLon;
        for (let i = 0; i < n; i++) {
          const c = order[i];
          const h = A[c] * (w1[c] * hum[up[c]] + w2[c] * hum[up2[c]] + w3[c] * hum[up3[c]]) + B[c];
          hum[c] = h > cap[c] ? cap[c] + (h - cap[c]) * capPass : h;
        }
      }
      for (let c = 0; c < n; c++) {
        if (!(elev[c] >= sea)) rain[c] = rc[c] * hum[c];
        else {
          const hin = w1[c] * hum[up[c]] + w2[c] * hum[up2[c]] + w3[c] * hum[up3[c]];
          const h = A[c] * hin + B[c];
          rain[c] = (hin + li[c]) * rc[c] + (h > cap[c] ? (h - cap[c]) * dump : 0);
        }
      }
    } else rain.fill(0);

    // one smoothing pass, then the local terms
    const anyDr = droughts(world, sc), dr = sc.dr;
    const inv = 1 / (T.rainRef > 1e-9 ? T.rainRef : 1e-9), dry = T.droughtDry;
    const knee = T.rainKnee < 1 ? (T.rainKnee > 0 ? T.rainKnee : 0) : 1, over = 1 - knee;
    let maxD = 0;
    for (let c = 0; c < n; c++) {
      const o = 6 * c, k = nbrN[c];
      let sum = 2 * rain[c];
      for (let j = 0; j < k; j++) sum += rain[nbr[o + j]];
      let m = sum / (2 + k) * inv;
      if (m > knee && over > 0) m = knee + over * (1 - Math.exp((knee - m) / over));   // the wettest places approach 1, they do not all sit on it
      m += wet[c];
      if (anyDr) m -= dry * dr[c];
      m = m > 0 ? (m < 1 ? m : 1) : 0;                       // also turns NaN into 0
      if (elev[c] >= sea) { const d = Math.abs(m - mh[c]); if (d > maxD) maxD = d; }
      moist[c] = m;
    }
    return maxD;
  }

  function finish(world, C, maxD) {
    world.need.climate = false;
    world.need.life = true;
    world.dirty.color = true;
    if (maxD > TUNE.climate.hydroDelta) { world.need.hydro = true; C.mh.set(world.moist); }
  }

  Climate.update = function (world) {
    const C = cacheFor(world);
    temps(world, C);
    finish(world, C, rainfall(world, C));
  };

  // ------------------------------------------------------------------ ice
  // One cell's equilibrium ice at unclamped temperature t (ice term included).
  function iceEq(T, land, t, wetWorld) {
    return land ? wetWorld * coldRamp(T.landIce0, T.landIce1, t) : coldRamp(T.seaIce0, T.seaIce1, t);
  }

  // Where a cell's ice settles when it starts from i: the fixed point of ice = iceEq(t0 − albedo × ice). On land the
  // albedo feedback is weaker than the ramp (slope 4/5), so there is one answer; sea ice has two over half a
  // degree and keeps the one it is nearer to.
  function iceFixed(T, land, t, i, alb, wetWorld) {
    if (land) {
      const width = T.landIce0 - T.landIce1, room = width - alb * wetWorld;
      if (width > 0 && room > 1e-6) {                        // the usual case, in closed form: exact, so settled ice never creeps
        const v = wetWorld * (T.landIce0 - t) / room;
        return v > 0 ? (v < wetWorld ? v : wetWorld) : 0;    // (NaN → 0)
      }
    }
    for (let it = 0; it < 80; it++) {
      const eq = iceEq(T, land, t - alb * i, wetWorld);
      if (Math.abs(eq - i) < 1e-5) return eq;
      i = eq;
    }
    return i;
  }

  // Move every cell `rate` of the way to that equilibrium (rate >= 1: put it there).
  // Keeps world.temp in step. Sets dirty.color / dirty.water when something changed.
  function moveIce(world, C, rate) {
    const T = TUNE.climate, n = world.n;
    const elev = world.elev, ice = world.ice, temp = world.temp, t0 = C.t0;
    const sea = world.seaLevel, alb = T.albedo, snap = T.iceSnap, seaMin = T.seaTempMin, wetWorld = C.wet;
    let land = false, water = false;
    for (let c = 0; c < n; c++) {
      const isLand = elev[c] >= sea;
      let i0 = ice[c];
      if (!(i0 >= 0)) i0 = 0; else if (i0 > 1) i0 = 1;
      const eq = iceFixed(T, isLand, t0[c], i0, alb, wetWorld), d = eq - i0;
      const ni = rate >= 1 || Math.abs(d) <= snap ? eq : i0 + d * rate;
      if (ni !== ice[c]) {
        if (Math.abs(ni - ice[c]) > 1e-3) { if (isLand) land = true; else water = true; }
        ice[c] = ni;
        const t = t0[c] - alb * ni;
        temp[c] = isLand || t >= seaMin ? t : seaMin;
      }
    }
    if (land || water) world.dirty.color = true;
    if (water) world.dirty.water = true;
  }

  Climate.stepIce = function (world) {
    const T = TUNE.climate;
    let C = world._clim;
    if (!C || C.n !== world.n || !(C.anom === C.anom)) { C = cacheFor(world); temps(world, C); }   // no update has run yet
    moveIce(world, C, GENESIS[world.phase] ? T.iceRateGenesis : T.iceRate);
  };

  // ------------------------------------------------------------------ calibration (history begins)
  // vars.iceRef for a world whose settled land ice sums to `ice`: max(iceRefMin, ice), always finite and positive
  function iceRefFor(T, ice) {
    const floor = T.iceRefMin > 1e-6 ? T.iceRefMin : 1e-6;
    return ice > floor ? ice : floor;
  }

  Climate.calibrate = function (world) {
    const T = TUNE.climate, v = world.vars || (world.vars = {});
    v.seaBase = fin(world.seaLevel, 0);                      // first, so the lapse datum of this very pass is the final one
    world.seaLevel = v.seaBase;
    const C = cacheFor(world);
    temps(world, C);
    moveIce(world, C, 1);
    const ice0 = Climate.landIce(world);
    v.iceRef = iceRefFor(T, ice0);
    v.climate_ice0 = ice0;
    v.climate_seaMark = v.seaBase;
    finish(world, C, rainfall(world, C));
  };

  // ------------------------------------------------------------------ yearly
  function coverVeg(world) {
    const L = DM.Life;
    if (L && typeof L.cover === 'function') {
      const c = L.cover(world);
      if (c && c.veg === c.veg && c.veg >= 0) return c.veg > 1 ? 1 : c.veg;
    }
    return TUNE.climate.vegDefault;
  }

  function milestone(kind, value) { DM.Bus.emit('climate:milestone', { kind, value }); }

  Climate.yearly = function (world) {
    const T = TUNE.climate, v = world.vars || (world.vars = {});
    const coldest = Climate.anomaly(world);                  // before this year's dust settles

    let dust = fin(world.dust, 0) * T.dustDecay;
    if (dust < 1e-4) dust = 0;
    world.dust = dust > 1 ? 1 : dust > 0 ? dust : 0;        // (a NaN from a broken tuning value lands on 0, like every write below)

    const base = v.seaBase;
    if ((world.phase === 'history' || world.phase === 'ended') && typeof base === 'number' && Number.isFinite(base)) {
      // greenhouse
      let co2 = fin(world.co2, 0);
      co2 -= co2 * T.uptake * (0.5 + coverVeg(world));
      co2 = co2 > 0 ? (co2 > T.co2Max ? T.co2Max : co2) : 0;
      world.co2 = co2;
      let gt = fin(world.globalTemp, 0);
      gt = fin(gt + (T.sens * Math.log2(1 + co2 / 100) - gt) / (T.lag > 1 ? T.lag : 1), gt);
      world.globalTemp = gt;

      // sea level
      let ref = v.iceRef;
      if (!(typeof ref === 'number' && Number.isFinite(ref) && ref > 0)) {
        // Calibrated by hand (a seaBase but no iceRef: test helpers, dev worlds). Finish the job here the way
        // calibrate would: the reference has to be the EQUILIBRIUM ice, or the ice merely settling afterwards
        // would read as melting or growth and move the sea.
        const C = cacheFor(world);
        temps(world, C);
        moveIce(world, C, 1);
        const settled = Climate.landIce(world);
        ref = v.iceRef = iceRefFor(T, settled);
        v.climate_ice0 = settled;
        world.need.climate = true;                           // the rainfall has not seen this ice yet
      }
      const ice = Climate.landIce(world);
      // vars.iceRef is floored at iceRefMin; climate_ice0 is the ice that was really there, so r = 1 at calibration
      // (exactly 1 while the ice has not moved: an undisturbed world keeps its sea level to the last bit)
      const ice0 = Math.min(ref, fin(v.climate_ice0, ref));
      const r = 1 + (ice - ice0) / ref;
      const iceTerm = r <= 1 ? T.seaIceGain * (1 - r) : -T.seaIceLoss * Math.min(1, r - 1);
      let target = base + iceTerm + T.seaTherm * gt;
      target = target < base - T.seaLo ? base - T.seaLo : target > base + T.seaHi ? base + T.seaHi : target;
      const old = fin(world.seaLevel, base);
      const step = target - old, cap = T.seaRate > 0 ? T.seaRate : 0;
      const sea = fin(old + (step > cap ? cap : step < -cap ? -cap : step), old);
      world.seaLevel = sea;
      const mark = fin(v.climate_seaMark, old);              // the level the coasts, rivers and climate were last derived for
      if (Math.abs(sea - mark) > T.seaStep) {
        v.climate_seaMark = sea;
        const need = world.need, dirty = world.dirty;
        need.derive = true; need.hydro = true; need.climate = true;
        dirty.water = true; dirty.color = true; dirty.objects = true;
      } else v.climate_seaMark = mark;

      // milestones
      const an = Climate.anomaly(world), cold = an < coldest ? an : coldest;
      let ms = fin(v.climate_ms, 0) | 0;
      const was = ms;
      for (let i = 0; i < T.warmAt.length && i < 4; i++) {
        if (an >= T.warmAt[i] && !(ms & (1 << i))) { ms |= 1 << i; milestone('warming', T.warmAt[i]); }
      }
      if (cold <= T.coolAt && !(ms & MS_COOL)) { ms |= MS_COOL; milestone('cooling', T.coolAt); }
      if (cold <= T.iceAgeAt && !(ms & MS_ICEAGE)) { ms |= MS_ICEAGE; milestone('ice-age', T.iceAgeAt); }
      if ((ms & (MS_COOL | MS_ICEAGE)) && an >= T.thawAt && cold >= T.thawAt) { ms &= ~(MS_COOL | MS_ICEAGE); milestone('thaw', T.thawAt); }
      const ds = sea - base;
      for (let i = 0; i < T.seaAt.length && i < 2; i++) {
        if (ds >= T.seaAt[i] && !(ms & (1 << (MS_RISE + i)))) { ms |= 1 << (MS_RISE + i); milestone('sea-rise', T.seaAt[i]); }
        if (ds <= -T.seaAt[i] && !(ms & (1 << (MS_FALL + i)))) { ms |= 1 << (MS_FALL + i); milestone('sea-fall', -T.seaAt[i]); }
      }
      if (ms !== was || v.climate_ms === undefined) v.climate_ms = ms;
    }

    // temperatures go stale as dust settles and the greenhouse builds
    const C = world._clim;
    if (!C || !(Math.abs(Climate.anomaly(world) - C.anom) < T.anomStep)) world.need.climate = true;
  };
})(globalThis.DM = globalThis.DM || {});
