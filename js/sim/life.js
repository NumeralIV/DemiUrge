// DEMIURGE — life.js : DM.Life, the living layer of the planet (SPEC-SIM §3.5): biomes, vegetation,
// wildlife, soil, wildfire and habitability. Pure and deterministic (all randomness from world.rng).
//
//   Life.classify(world)                 biome for every cell; sets dirty.color and dirty.objects
//   Life.capacity(world, c) → 0..1       vegetation the cell could support right now (0 on sea, ice, lake, lava)
//   Life.step(world, part, parts)        advance veg, fauna, fert, wet, scorch, rad (and farm / urban on unowned
//                                        cells) for the cells with c % parts === part by dt = parts × Sim.TICK
//                                        years, then the drifting seeds for this tick
//   Life.burn(world)                     advance the fires by one tick (call every tick)
//   Life.ignite(world, c, intensity = 1) → bool   start a fire if the cell can burn (emits fire:start, ≤ 2 per tick)
//   Life.seed(world, c, ang = 0.12) → n  veg ≥ 0.35 on plantable land within ang of c; returns the cells planted
//   Life.bloom(world, c, ang, amount) → n   veg toward capacity, fert up and scorch down by amount × falloff
//   Life.fill(world, frac = 1)           veg = frac × capacity and fauna = its capacity everywhere
//   Life.habitability(world)             hab for every cell and world._landHab; clears need.life
//   Life.rebuild(world) → world._fires   rebuild the transient fire list from fire[]
//   Life.cover(world) → { land, veg, forest }   land share of the planet; Σ min(veg, capacity) / Σ capacity over
//                                        plantable land; share of land cells with veg ≥ 0.5 (cached per tick)
//
// Transient caches (all rebuildable): world._fires = { cells: Int32Array, n } (cells[0..n-1] have fire > 0),
// world._landHab (Int32Array: habitable cells per world._landId), world._lifeNz (static noise fields),
// world._lifeAir (seed pressure over dead ground), world._lifeGreen (vegetated cells per slice at its last
// visit), world._lifeCover (cover cache), world._lifeVer (edit counter), world._lifeAcc, world._lifeEv.
//
// How the pieces work (the numbers are in TUNE.life):
//   Biomes     sea by depth and ice; land: lava / dead scorch → BARREN, high and cold → SNOWPEAK, ice → ICE, steep
//              or very high → ROCK, a low strip by the sea → BEACH, flat wet lowland by water → WETLAND, otherwise
//              a Whittaker table on temperature and effective moisture (heat costs moisture). Heights are measured
//              from Climate.datum, slopes are scaled to the reference cell size so every L classifies alike, and
//              the table reads its inputs through a little static noise so borders wander instead of following
//              the climate's isolines.
//   Vegetation exact logistic growth toward capacity (so the result does not depend on the slice count), slow
//              die-back above it. A bare cell never greens by itself: it sprouts when a neighbour is established
//              (veg > 0.05, or at least half of that neighbour's own small capacity, so desert scrub and alpine
//              rock pass life along). Sprouting is a hazard, not a certainty: faster on rich ground, along fresh
//              water and where several neighbours are green, slower on slopes, and modulated by two static noise
//              fields, which is what makes the spreading front ragged, with lobes, bays and river tendrils.
//              Sprouts are planted at the end of a pass, so the front never depends on the order of the cells.
//              Dead ground (ice, snow, lakes, lava) hands seed on from cell to cell, losing 4 % per hop, so a valley
//              behind a glacier or a shore beyond an ice sheet is still reached. Drifting seeds (8 random land
//              cells per year of the boosted clock) start colonies up to 12 hops from mature vegetation, across
//              the sea; while lifeBoost > 1 a pick that could have been reached over land is thinned, so in
//              genesis the front leads on land and the jumps are the ones over water.
//   Fire       world._fires holds the burning cells. A cell burns for two calls of Life.burn: on the first it
//              tries each neighbour once with P = 0.55 × veg × sat(1.2 − 1.6 × (moist + wet)) and loses 80 % of
//              its vegetation; on the second it goes out. Never onto sea, ice, lakes, veg < 0.15, wet > 0.3, a
//              cell that is burning, or while 1,500 cells burn. This is bond percolation on the 6-neighbour
//              lattice (threshold 0.347): measured on a uniform forest, fires average 1.3 cells at P = 0.04,
//              45 at 0.28, and cross the planet from 0.35. fire[] encodes the stage (> 0.55: first tick still to
//              come), so a loaded world carries on.
(function (DM) {
  'use strict';
  const Life = (DM.Life = DM.Life || {});
  const TUNE = (DM.TUNE = DM.TUNE || {});

  TUNE.life = {
    // ---- biomes -----------------------------------------------------------------------------------------
    deepSea: 0.6,          // sea deeper than this is SEA, shallower is SHALLOWS
    iceBiome: 0.5,         // ice above this: SEAICE on sea, ICE on land (nothing grows)
    lakeWater: 0.03,       // a land cell with more standing water than this is a lake cell
    lavaBarren: 0.05,      // lava thicker than this: BARREN, nothing grows
    scorchBarren: 0.7,     // scorch above this with veg below vegBarren: BARREN
    vegBarren: 0.02,
    snowH: 3.0,            // height above the datum from which a cold cell is a SNOWPEAK ...
    snowT: -3,             // ... when its temperature is below this (or it is iced over)
    rockSlope: 1.6,        // slope (height difference to a neighbour at the reference cell size) above which a cell is ROCK
    rockH: 4.6,            // ... or height above the datum above which it is ROCK
    beachH: 0.1,           // BEACH: next to the sea, lower than this above sea level,
    beachSlope: 0.45,      // gentler than this,
    beachT: 1,             // and warmer than this
    wetlandM: 0.5,         // WETLAND: moist at least this,
    wetlandH: 0.6,         // lower than this above the datum,
    wetlandSlope: 0.3,     // flatter than this,
    wetlandT: 3,           // warmer than this, and next to fresh water or the sea
    evap: 0.004,           // effective moisture = moist − evap × max(0, temp − 10): heat dries the ground
    tundraT: -5,           // colder than this: TUNDRA
    borealT: 3,            // colder than this: TAIGA when effective moisture ≥ taigaM, else TUNDRA
    taigaM: 0.25,
    coolT: 7,              // colder than this (and not dry): TAIGA rather than FOREST
    hotT: 20,              // warmer than this: SAVANNA / RAINFOREST replace GRASSLAND / wet FOREST
    desertM: 0.10,         // effective moisture below this: DESERT
    grassM: 0.36,          // below this: GRASSLAND
    savannaM: 0.40,        // hot and below this: SAVANNA
    rainM: 0.62,           // hot and at least this: RAINFOREST
    jitterM: 0.12,         // the Whittaker table reads moisture + jitterM × (patch noise − 0.5) + jitterG × (grain − 0.5)
    jitterG: 0.03,         // and temperature + jitterT × (regional noise − 0.5), so biome borders wander and interfinger
    jitterT: 3,            // instead of following the climate's straight isolines
    refStep: 0.0189,       // geo.step of the reference world (L = 6); slopes are scaled by refStep / geo.step

    // ---- vegetation -------------------------------------------------------------------------------------
    //    SEA SHAL BEACH DESERT SAVAN GRASS FOREST RAINF TAIGA TUNDRA ICE  ROCK SNOW WETL BARREN SEAICE
    cap: [0, 0, 0.05, 0.05, 0.35, 0.3, 0.9, 1, 0.75, 0.15, 0, 0.03, 0, 0.7, 0, 0],   // biomeCapacity
    plantMin: 0.005,       // a cell is plantable when its capacity exceeds this
    grow: 0.045,           // logistic growth rate per year (a cleared forest is back at 90 % in ~120 years)
    die: 0.03,             // die-back per year toward a capacity that fell below the standing vegetation
    kill: 0.6,             // die-back per year where nothing can grow at all (ice, lake, lava)
    established: 0.05,     // a neighbour with more vegetation than this lets a bare cell sprout ...
    establishedRel: 0.5,   // ... or one holding at least this share of its own capacity
    sprout: 0.06,          // vegetation of a new sprout
    spread: 0.08,          // sprouting hazard per year of a bare cell on reference ground with two green neighbours
    spreadBoost: 0.9,      // the hazard scales with lifeBoost ^ this (growth scales with lifeBoost itself): paces the genesis wave
    spreadCap0: 0.3,       // hazard factor on ground with no capacity, rising to 1 at capacity spreadCapRef
    spreadCapRef: 0.6,
    spreadK: [0, 0.8, 1, 1.2, 1.6, 2.2, 3],      // hazard factor by number of established neighbours (holes close fast)
    spreadFresh: [2.2, 1.6],                     // hazard factor on a river / lake shore cell, and one hop from one
    spreadSlope: 1.2,      // hazard ÷ (1 + spreadSlope × slope)
    spreadNoise: 2.4,      // hazard × exp(spreadNoise × (grain − 0.5) + spreadPatch × (patch noise − 0.5)): the static fields make
    spreadPatch: 3.2,      // fast lanes and slow ground a few cells wide (grain) and lobes a dozen cells wide (patches)
    airVeg: 0.02,          // dead ground (ice, snow, lakes, lava) next to vegetation above this carries seed pressure 1 ...
    airDecay: 0.96,        // ... passed on from dead cell to dead cell, × this per hop, so life crosses the barriers of a
                           // continent (0.96: an ice sheet 100 cells wide still lets 2 % through; it must stay below 1)
    driftPicks: 8,         // drifting seeds: random land cells tried per year of the boosted clock (× lifeBoost) ...
    driftHops: 12,         // ... each needs vegetation above driftSrc within this many hops (sea included)
    driftSrc: 0.3,         // ... (or holding driftSrcRel of its own capacity, so grassland and tundra shed seed too)
    driftSrcRel: 0.6,
    driftVeg: 0.08,        // vegetation of a drifted seedling
    driftBoost: 0.3,       // a pick whose source can be reached over land (no sea to cross) only takes with chance
                           // lifeBoost ^ (driftBoost − 1), i.e. overland drift runs at lifeBoost ^ driftBoost: in genesis
                           // the contiguous front must lead and the forerunner colonies only roughen it, while the
                           // jumps over water (islands, the far shore of a strait) keep the pace of everything else
    driftSeaBoost: 1,      // ... at lifeBoost ^ this (1 = the full boosted clock; lower it and one seed no longer hops
                           // from continent to continent within the life phase)
    driftMax: 400,         // most picks per tick
    seedVeg: 0.35,         // Life.seed plants at least this
    forestVeg: 0.5,        // cover().forest counts land cells with at least this much vegetation
    maxBoost: 400,         // clamp for vars.lifeBoost

    // ---- fire -------------------------------------------------------------------------------------------
    fireP: 0.55,           // ignition chance per neighbour = fireP × veg × dry
    dryA: 1.2, dryB: 1.6,  // dry = sat(dryA − dryB × (moist + wet))
    fireVeg: 0.15,         // nothing with less vegetation than this catches fire
    fireWet: 0.3,          // wetter than this cannot burn; rain puts fires out
    fireMax: 1500,         // no new ignitions while this many cells burn
    burnLoss: 0.8,         // share of the vegetation a fire removes
    burnFauna: 0.5,        // share of the wildlife it kills
    burnFert: 0.08,        // fertility gained per unit of vegetation burnt (ash)
    fireEvents: 2,         // fire:start events per tick

    // ---- wildlife ---------------------------------------------------------------------------------------
    //         SEA SHAL BEACH DESERT SAVAN GRASS FOREST RAINF TAIGA TUNDRA ICE ROCK SNOW WETL BARREN SEAICE
    faunaCap: [0, 0, 0.15, 0.08, 0.75, 0.7, 0.8, 0.9, 0.5, 0.25, 0, 0.1, 0, 0.85, 0, 0],
    faunaGrow: 0.3,        // logistic rate per year
    faunaDie: 0.25,        // decline per year toward a lower capacity
    faunaSeed: 0.06,       // density of the first arrivals
    faunaSrc: 0.05,        // a sea / lake cell is colonised from a neighbour with more wildlife than this
    fishShallow: 0.9,      // fish capacity in the shallows ...
    fishDeep: 0.25,        // ... falling to this at fishDeepAt below the surface
    fishDeepAt: 3,
    fishLake: 0.6,

    // ---- soil and decay ---------------------------------------------------------------------------------
    fertRate: 0.004,       // share of the gap to the natural level closed per year
    fertBase: 0.32,        // natural fertility = fertBase + fertVar × regional noise (0..1)
    fertVar: 0.4,
    fertFresh: [0.3, 0.22, 0.12, 0.05],   // + by hops to fresh water (0, 1, 2, 3)
    fertLow: 0.1,          // + on lowlands (fading out by 1.2 above the datum)
    fertHigh: 0.3,         // − on highlands (from 2.2, full by 5.2 above the datum)
    fertAsh: 0.25,         // + scorch × this
    fertMin: 0.15, fertMax: 1.2,          // clamp of the natural level
    fertCap: 1.5,          // hard cap for bloom and ash
    wetDecay: 0.10,        // per year (exponential)
    scorchDecay: 0.06,     // per year
    radDecay: 0.015,       // share lost per year
    farmDecay: 0.05,       // share lost per year on unowned cells
    urbanDecay: 0.01,      // share lost per year on unowned cells
    tiny: 0.004,           // wet, scorch, rad, farm, urban and dying vegetation below this become 0 (one q8 step)

    // ---- habitability -----------------------------------------------------------------------------------
    habCold: [-6, 8],      // temperature suitability rises from 0 to 1 across this range ...
    habHot: [26, 40],      // ... and falls back to 0 across this one
    habRain: [0.06, 0.40], // water from rainfall: 0 at the first value, 1 at the second
    habFresh: [0.8, 0.8, 0.6],            // water from fresh water 0, 1, 2 hops away
    habFert: 0.55,         // fertility factor = habFert + (1 − habFert) × sat(fert)
    habSlope: [0.25, 1.6], // slope factor falls from 1 to 0 across this range
    habCoast: [1, 1.12, 1.06],            // coastal bonus by hops to the sea (index 1, 2)
    habMin: 0.05,          // a cell counts as habitable (world._landHab) above this
  };

  const FRESH = 0.55;                     // fire[] above this: first tick still to come
  const FIRE_NEW = 0.6;                    // fire[] of a new fire is FIRE_NEW … 1

  // ------------------------------------------------------------------ small helpers
  function sat(x) { return x < 0 ? 0 : x > 1 ? 1 : x; }
  function sm(e0, e1, x) {                 // smoothstep, safe for e0 === e1
    if (!(e1 > e0)) return x < e0 ? 0 : 1;
    const t = x <= e0 ? 0 : x >= e1 ? 1 : (x - e0) / (e1 - e0);
    return t * t * (3 - 2 * t);
  }
  function tick() { const S = DM.Sim; return S && S.TICK > 0 ? S.TICK : 0.25; }
  function f32up(x) {                      // the smallest float32 that is not below x (veg[] is a Float32Array)
    let f = Math.fround(x);
    if (f < x) f = Math.fround(x + Math.abs(x) * 1.2e-7 + 1e-38);
    return f;
  }
  function validCell(world, c) { return Number.isInteger(c) && c >= 0 && c < world.n; }
  function bump(world) { world._lifeVer = ((world._lifeVer | 0) + 1) | 0; }
  // vegetation was planted outside Life.step: the per-slice record of where anything grows is no longer known
  function greenUnknown(world) { const G = world._lifeGreen; if (G && G.n) G.n.fill(-1); }

  // the height every "how high is this" rule measures from (the sea datum of the finished world)
  function datumOf(world) {
    const C = DM.Climate;
    if (C && typeof C.datum === 'function') { const d = C.datum(world); if (Number.isFinite(d)) return d; }
    const b = world.vars ? world.vars.seaBase : undefined;
    const v = Number.isFinite(b) ? b : world.seaLevel;
    return v > 0 ? v : 0;
  }
  function slopeScale(world) {
    const st = world.geo.step;
    return st > 0 ? TUNE.life.refStep / st : 1;
  }
  function boostOf(world) {
    const b = world.vars ? world.vars.lifeBoost : 1;
    if (b === undefined || b === null) return 1;
    if (!(b >= 0)) return 1;
    const mx = TUNE.life.maxBoost;
    return b > mx ? mx : b;
  }

  // module scratch, keyed by cell count
  let SCR = null;
  function scratch(n) {
    if (!SCR || SCR.n !== n) {
      SCR = { n, list: new Int32Array(n), ring: new Int32Array(64), walk: new Int32Array(64), mark: new Int32Array(n), stamp: 0,
        sproutC: new Int32Array(n), sproutV: new Float32Array(n) };
    }
    return SCR;
  }
  function ringSize(n, hops) { const need = 1 + 3 * hops * (hops + 1); return need > n ? n : need; }
  function ringBuf(n, hops) {
    const s = scratch(n), need = ringSize(n, hops);
    if (s.ring.length < need) s.ring = new Int32Array(need + 16);
    return s.ring;
  }

  // does a cell shed seed for the drift: vegetation above driftSrc, or a good share of what its own ground can carry
  function sheds(world, m, T) {
    const vm = world.veg[m];
    return vm > T.driftSrc || (vm > T.established && vm >= T.driftSrcRel * capOf(world, m, T));
  }
  // Is there a seed source within `hops` of cell c that can be reached without crossing the sea? (Breadth-first
  // over land cells only; ice and lakes count as land.) Such a pick lies ahead of a front that will arrive by
  // itself; one that fails this test is a true jump over water.
  function overland(world, c, hops, T) {
    const n = world.n, s = scratch(n), mark = s.mark, need = ringSize(n, hops);
    if (s.walk.length < need) s.walk = new Int32Array(need + 16);
    const q = s.walk, nbr = world.geo.nbr, elev = world.elev, sea = world.seaLevel;
    if (s.stamp >= 0x7ffffff0) { mark.fill(0); s.stamp = 0; }
    const stamp = ++s.stamp;
    let head = 0, tail = 0;
    q[tail++] = c; mark[c] = stamp;
    for (let d = 0; d < hops && head < tail; d++) {
      for (const end = tail; head < end; head++) {
        const b = 6 * q[head];
        for (let j = 0; j < 6; j++) {
          const m = nbr[b + j];
          if (m < 0) break;
          if (mark[m] === stamp) continue;
          mark[m] = stamp;
          if (elev[m] < sea) continue;
          if (sheds(world, m, T)) return true;
          if (tail < q.length) q[tail++] = m;
        }
      }
    }
    return false;
  }

  // Three static noise fields per world, built once from the seed: `g` (grain, a few cells across: how readily
  // the ground takes seed), `r` (regional: natural soil richness) and `p` (patches in between: where biome borders
  // bulge). All 0..1.
  function noiseOf(world) {
    let z = world._lifeNz;
    const n = world.n;
    if (z && z.n === n && z.seed === world.seed) return z;
    const U = DM.U, pos = world.geo.pos, seed = world.seed | 0;
    const g = new Float32Array(n), r = new Float32Array(n), pt = new Float32Array(n);
    const ax = U.hashf(seed, 7101) * 97, ay = U.hashf(seed, 7102) * 97, az = U.hashf(seed, 7103) * 97;
    const bx = U.hashf(seed, 7104) * 97, by = U.hashf(seed, 7105) * 97, bz = U.hashf(seed, 7106) * 97;
    const cx = U.hashf(seed, 7107) * 97, cy = U.hashf(seed, 7108) * 97, cz = U.hashf(seed, 7109) * 97;
    for (let c = 0; c < n; c++) {
      const x = pos[3 * c], y = pos[3 * c + 1], zz = pos[3 * c + 2];
      const a = 0.62 * U.noise3(x * 6.5 + ax, y * 6.5 + ay, zz * 6.5 + az) + 0.38 * U.noise3(x * 15.1 + az, y * 15.1 + ax, zz * 15.1 + ay);
      const b = U.noise3(x * 2.6 + bx, y * 2.6 + by, zz * 2.6 + bz);
      const d = 0.7 * U.noise3(x * 4.3 + cx, y * 4.3 + cy, zz * 4.3 + cz) + 0.3 * U.noise3(x * 9.7 + cz, y * 9.7 + cx, zz * 9.7 + cy);
      const ga = 0.5 + 0.95 * a, rb = 0.5 + 0.75 * b, pd = 0.5 + 0.9 * d;
      g[c] = ga < 0 ? 0 : ga > 1 ? 1 : ga === ga ? ga : 0.5;
      r[c] = rb < 0 ? 0 : rb > 1 ? 1 : rb === rb ? rb : 0.5;
      pt[c] = pd < 0 ? 0 : pd > 1 ? 1 : pd === pd ? pd : 0.5;
    }
    z = world._lifeNz = { n, seed: world.seed, g, r, p: pt };
    return z;
  }

  // seed pressure carried by dead ground (transient, relaxes back within a few visits after a load)
  function airOf(world) {
    let a = world._lifeAir;
    if (!a || a.length !== world.n) a = world._lifeAir = new Float32Array(world.n);
    return a;
  }

  // ------------------------------------------------------------------ capacity
  function capOf(world, c, T) {
    if (world.elev[c] < world.seaLevel) return 0;
    if (world.water[c] > T.lakeWater || world.ice[c] > T.iceBiome || world.lava[c] > T.lavaBarren) return 0;
    const f = world.fert[c], fa = world.farm[c], ur = world.urban[c], ra = world.rad[c];
    let k = T.cap[world.biome[c]] * (0.6 + 0.4 * f) * (1 - fa) * (1 - ur) * (1 - 0.8 * ra);
    if (!(k > 0)) return 0;
    return k > 1 ? 1 : k;
  }

  Life.capacity = function (world, c) {
    if (!world || !validCell(world, c)) return 0;
    return capOf(world, c, TUNE.life);
  };

  // wildlife a land cell could hold (the cell is dry land: not sea, not a lake)
  function faunaLand(world, c, T, v) {
    const b = world.biome[c], fc = T.faunaCap[b];
    if (!(fc > 0) || !(v > 0) || world.ice[c] > T.iceBiome) return 0;
    const rel = v / (0.7 * T.cap[b] + 0.03);
    const k = fc * Math.sqrt(rel > 1 ? 1 : rel) * (1 - world.urban[c]) * (1 - 0.5 * world.farm[c]) * (1 - world.rad[c]);
    return k > 0 ? (k > 1 ? 1 : k) : 0;
  }
  // fish: by depth (rich shallows), water temperature, ice cover and fallout; `depth` < 0 means a lake
  function faunaWater(world, c, T, depth) {
    const t = world.temp[c];
    let k;
    if (depth < 0) k = T.fishLake;
    else if (depth <= T.deepSea) k = T.fishShallow;
    else {
      const span = T.fishDeepAt - T.deepSea;
      k = T.fishShallow + (T.fishDeep - T.fishShallow) * (span > 0 ? sat((depth - T.deepSea) / span) : 1);
    }
    k *= (0.55 + 0.45 * sat((t + 2) / 8)) * (1 - 0.3 * sat((t - 24) / 6));
    k *= (1 - 0.8 * sat(world.ice[c])) * (1 - sat(world.rad[c]));
    return k > 0 ? (k > 1 ? 1 : k) : 0;
  }

  // ------------------------------------------------------------------ biomes
  Life.classify = function (world) {
    const T = TUNE.life, B = DM.BIOME, n = world.n;
    const elev = world.elev, ice = world.ice, water = world.water, lava = world.lava, scorch = world.scorch;
    const veg = world.veg, temp = world.temp, moist = world.moist, slope = world.slope, coast = world.coast;
    const fresh = world.fresh, biome = world.biome;
    const sea = world.seaLevel, datum = datumOf(world), sk = slopeScale(world);
    const nz = noiseOf(world), grain = nz.g, region = nz.r, patch = nz.p;   // (this also builds the static fields off the tick path)
    const jM = T.jitterM, jG = T.jitterG, jT = T.jitterT;
    for (let c = 0; c < n; c++) {
      const e = elev[c];
      let b;
      if (e < sea) {
        b = ice[c] > T.iceBiome ? B.SEAICE : (sea - e > T.deepSea ? B.SEA : B.SHALLOWS);
      } else {
        const t = temp[c], h = e - datum, s = slope[c] * sk, iced = ice[c] > T.iceBiome;
        if (lava[c] > T.lavaBarren || (scorch[c] > T.scorchBarren && veg[c] < T.vegBarren)) b = B.BARREN;
        else if (h >= T.snowH && (iced || t < T.snowT)) b = B.SNOWPEAK;
        else if (iced) b = B.ICE;
        else if (s > T.rockSlope || h > T.rockH) b = B.ROCK;
        else if (coast[c] <= 1 && e - sea < T.beachH && s < T.beachSlope && t > T.beachT && !(water[c] > T.lakeWater)) b = B.BEACH;
        else {
          const m = moist[c];
          if (m >= T.wetlandM && t >= T.wetlandT && h < T.wetlandH && s <= T.wetlandSlope && (fresh[c] <= 1 || coast[c] <= 1)) b = B.WETLAND;
          else {
            const tj = t + jT * (region[c] - 0.5);
            if (tj < T.tundraT) b = B.TUNDRA;
            else {
              const me = m - T.evap * (t > 10 ? t - 10 : 0) + jM * (patch[c] - 0.5) + jG * (grain[c] - 0.5);
              if (tj < T.borealT) b = me >= T.taigaM ? B.TAIGA : B.TUNDRA;
              else if (me < T.desertM) b = B.DESERT;
              else if (tj < T.coolT) b = me < T.grassM ? B.GRASSLAND : B.TAIGA;
              else if (tj < T.hotT) b = me < T.grassM ? B.GRASSLAND : B.FOREST;
              else b = me < T.savannaM ? B.SAVANNA : (me < T.rainM ? B.FOREST : B.RAINFOREST);
            }
          }
        }
      }
      biome[c] = b;
    }
    world.dirty.color = true;
    world.dirty.objects = true;
    bump(world);
  };

  // ------------------------------------------------------------------ fires
  function growFires(F, need) {
    if (F.cells.length >= need) return;
    const nc = new Int32Array(need + 64);
    nc.set(F.cells.subarray(0, F.n));
    F.cells = nc;
  }
  Life.rebuild = function (world) {
    const fire = world.fire, n = world.n, T = TUNE.life;
    let k = 0;
    for (let c = 0; c < n; c++) if (fire[c] > 0) k++;
    const want = 2 * T.fireMax + 64;
    const cells = new Int32Array(k > want ? k + 64 : want);
    k = 0;
    for (let c = 0; c < n; c++) if (fire[c] > 0) cells[k++] = c;
    return (world._fires = { cells, n: k });
  };
  function firesOf(world) {
    const F = world._fires;
    if (F && F.cells && F.n >= 0 && F.n <= F.cells.length) return F;
    return Life.rebuild(world);
  }
  function dropFire(world, c) {
    const F = world._fires;
    if (!F || !F.cells) return;
    const cells = F.cells;
    for (let i = 0; i < F.n; i++) if (cells[i] === c) { cells[i] = cells[--F.n]; return; }
  }
  // can the cell catch fire at all (the vegetation and wetness rules; not the chance)
  function flammable(world, c, T) {
    return world.elev[c] >= world.seaLevel && !(world.ice[c] > T.iceBiome) && !(world.water[c] > T.lakeWater) &&
      world.veg[c] >= T.fireVeg && !(world.wet[c] > T.fireWet) && !(world.fire[c] > 0);
  }

  Life.ignite = function (world, c, intensity) {
    if (!world || !validCell(world, c)) return false;
    const T = TUNE.life;
    if (intensity === undefined) intensity = 1;
    if (!(intensity > 0)) return false;
    if (!flammable(world, c, T)) return false;
    const F = firesOf(world);
    if (F.n >= T.fireMax) return false;
    growFires(F, F.n + 1);
    world.fire[c] = FIRE_NEW + (1 - FIRE_NEW) * sat(intensity);
    F.cells[F.n++] = c;
    let ev = world._lifeEv;
    if (!ev) ev = world._lifeEv = { tick: -1, n: 0 };
    if (ev.tick !== world.tick) { ev.tick = world.tick; ev.n = 0; }
    if (ev.n < T.fireEvents) { ev.n++; DM.Bus.emit('fire:start', { cell: c }); }
    return true;
  };

  Life.burn = function (world) {
    const F = firesOf(world), n0 = F.n;
    if (n0 === 0) return;
    const T = TUNE.life, nbr = world.geo.nbr, rng = world.rng, VT = DM.State.VEG_T;
    const elev = world.elev, ice = world.ice, water = world.water, veg = world.veg, wet = world.wet, moist = world.moist;
    const fire = world.fire, scorch = world.scorch, fauna = world.fauna, fert = world.fert;
    const sea = world.seaLevel, iceT = T.iceBiome, lake = T.lakeWater, fmax = T.fireMax;
    const fP = T.fireP, dA = T.dryA, dB = T.dryB, fVeg = T.fireVeg, fWet = T.fireWet, keep = 1 - T.burnLoss;
    growFires(F, n0 + fmax + 8);
    const cells = F.cells;
    // Pass 1, the fires as the tick finds them. Ground that can no longer burn (drowned, frozen, rained on) goes
    // out at once. A fire on its second tick is spent, but stays marked (a negative value, cleared below) until
    // the tick is over, so the cells it lit cannot light it again while it is still burning.
    let live = 0;
    for (let i = 0; i < n0; i++) {
      const c = cells[i], f = fire[c];
      if (!(f > 0)) continue;                                                   // went out some other way
      if (elev[c] < sea || ice[c] > iceT || water[c] > lake || wet[c] > fWet) fire[c] = 0;
      else if (f > FRESH) live++;
      else fire[c] = -1;
    }
    // Pass 2, the first tick of a fire: one attempt per neighbour, then the cell itself burns down. `live` counts
    // the cells that will be burning when this call returns; nothing new catches once that reaches the cap.
    let burnt = false, objects = false;
    for (let i = 0; i < n0; i++) {
      const c = cells[i], f = fire[c];
      if (!(f > FRESH)) continue;
      const b = 6 * c;
      for (let j = 0; j < 6; j++) {
        const m = nbr[b + j];
        if (m < 0) break;
        if (live >= fmax) break;
        const vm = veg[m];
        if (!(vm >= fVeg) || fire[m] !== 0 || elev[m] < sea || ice[m] > iceT || water[m] > lake) continue;
        const wm = wet[m];
        if (wm > fWet) continue;
        let dry = dA - dB * (moist[m] + wm);
        if (!(dry > 0)) continue;
        if (dry > 1) dry = 1;
        if (rng.f() < fP * vm * dry) { fire[m] = 1; cells[F.n++] = m; live++; }
      }
      const v = veg[c], nv = v * keep;
      if (v > 0) {
        veg[c] = nv;
        const sc = 0.3 + 0.9 * v;
        if (sc > scorch[c]) scorch[c] = sc > 1 ? 1 : sc;
        fauna[c] *= 1 - T.burnFauna;
        const nf = fert[c] + T.burnFert * v;
        fert[c] = nf > T.fertCap ? (fert[c] > T.fertCap ? fert[c] : T.fertCap) : nf;
        if ((v >= VT[0]) !== (nv >= VT[0]) || (v >= VT[1]) !== (nv >= VT[1]) || (v >= VT[2]) !== (nv >= VT[2])) objects = true;
        burnt = true;
      }
      fire[c] = f * 0.5;
    }
    // Pass 3: the spent fires go out, and the list is rebuilt without them (and without duplicates, however one
    // might have got in: a kept cell is flagged by flipping its sign for the length of the loop).
    for (let i = 0; i < n0; i++) { const c = cells[i]; if (fire[c] < 0) fire[c] = 0; }
    let k = 0;
    const end = F.n;
    for (let i = 0; i < end; i++) { const c = cells[i], f = fire[c]; if (f > 0) { cells[k++] = c; fire[c] = -f; } }
    for (let i = 0; i < k; i++) { const c = cells[i]; fire[c] = -fire[c]; }
    F.n = k;
    world.dirty.color = true;
    if (objects) world.dirty.objects = true;
    if (burnt) bump(world);
  };

  // ------------------------------------------------------------------ the yearly-ish step
  Life.step = function (world, part, parts) {
    const T = TUNE.life, n = world.n;
    parts = parts > 0 ? Math.floor(parts) : 1;
    part = part > 0 ? Math.floor(part) % parts : 0;
    const TICK = tick(), dt = parts * TICK, boost = boostOf(world), g = dt * boost;
    const nbr = world.geo.nbr, rng = world.rng, VT = DM.State.VEG_T, vt0 = VT[0], vt1 = VT[1], vt2 = VT[2];
    const elev = world.elev, water = world.water, ice = world.ice, lava = world.lava, temp = world.temp;
    const veg = world.veg, fauna = world.fauna, fert = world.fert, wet = world.wet, scorch = world.scorch;
    const rad = world.rad, farm = world.farm, urban = world.urban, road = world.road, fire = world.fire;
    const owner = world.owner, biome = world.biome, fresh = world.fresh, slope = world.slope;
    const sea = world.seaLevel, datum = datumOf(world), sk = slopeScale(world);
    const nz = noiseOf(world), grain = nz.g, region = nz.r, patch = nz.p, air = airOf(world), airVeg = T.airVeg, airDecay = sat(T.airDecay);
    const capB = T.cap, faB = T.faunaCap, iceT = T.iceBiome, lake = T.lakeWater, lavaT = T.lavaBarren, tiny = T.tiny;
    const plantMin = T.plantMin, est = T.established, estRel = T.establishedRel, sprout = T.sprout;
    // per-call factors: every rate below is exact for the time step, so 2 slices and 8 slices agree
    const eGrow = Math.exp(-T.grow * g), kDie = 1 - Math.exp(-T.die * g), kKill = 1 - Math.exp(-T.kill * g);
    const eFauna = Math.exp(-T.faunaGrow * g), kFaunaDie = 1 - Math.exp(-T.faunaDie * g);
    const fWet = Math.exp(-T.wetDecay * dt), fScorch = Math.exp(-T.scorchDecay * dt);
    const fRad = Math.pow(1 - sat(T.radDecay), dt), fFarm = Math.pow(1 - sat(T.farmDecay), dt), fUrban = Math.pow(1 - sat(T.urbanDecay), dt);
    const kFert = 1 - Math.exp(-T.fertRate * dt);
    const spreadG = T.spread * dt * Math.pow(boost, T.spreadBoost), sc0 = T.spreadCap0, scRef = T.spreadCapRef > 0 ? T.spreadCapRef : 1;
    const spK = T.spreadK, spF = T.spreadFresh, spS = T.spreadSlope, spN = T.spreadNoise, spP = T.spreadPatch;
    const fFresh = T.fertFresh, fseed = T.faunaSeed, fsrc = T.faunaSrc, BARREN = DM.BIOME.BARREN;
    const scr = scratch(n), sproutC = scr.sproutC, sproutV = scr.sproutV;
    let dCol = 0, objects = false, relife = false, ns = 0, green = 0;

    for (let c = part; c < n; c += parts) {
      const e = elev[c];
      // ---------------- sea: nothing of the land survives; fish
      if (e < sea) {
        if (veg[c] !== 0) { if (veg[c] >= vt0) objects = true; veg[c] = 0; dCol += 1; }
        if (farm[c] !== 0) { farm[c] = 0; dCol += 1; }
        if (urban[c] !== 0) { urban[c] = 0; dCol += 1; objects = true; }
        if (scorch[c] !== 0) { scorch[c] = 0; dCol += 1; }
        if (road[c] !== 0) { road[c] = 0; world.dirty.roads = true; }
        if (water[c] !== 0) { water[c] = 0; world.dirty.water = true; }
        if (fire[c] !== 0) { fire[c] = 0; dropFire(world, c); }
        if (air[c] !== 0) air[c] = 0;
        let w = wet[c];
        if (w !== 0) { w *= fWet; wet[c] = w > tiny ? w : 0; }
        let r = rad[c];
        if (r !== 0) { r *= fRad; rad[c] = r > tiny ? r : 0; }
        const K = faunaWater(world, c, T, sea - e);
        let F = fauna[c];
        if (F > K) fauna[c] = F + (K - F) * kFaunaDie;
        else if (F > 0) { if (F < K) fauna[c] = K * F / (F + (K - F) * eFauna); }
        else if (K > fsrc) {
          const b = 6 * c;
          for (let j = 0; j < 6; j++) { const m = nbr[b + j]; if (m < 0) break; if (fauna[m] > fsrc) { fauna[c] = fseed < K ? fseed : K; break; } }
        } else if (F !== 0) fauna[c] = 0;
        continue;
      }

      // ---------------- land: decays
      let w = wet[c];
      if (w !== 0) { const o = w; w *= fWet; if (!(w > tiny)) w = 0; wet[c] = w; dCol += o - w; }
      let s = scorch[c];
      if (s !== 0) { const o = s; s *= fScorch; if (!(s > tiny)) s = 0; scorch[c] = s; dCol += o - s; }
      let r = rad[c];
      if (r !== 0) { r *= fRad; if (!(r > tiny)) r = 0; rad[c] = r; }
      let fa = farm[c], ur = urban[c];
      if (owner[c] < 0) {
        if (fa !== 0) { const o = fa; fa *= fFarm; if (!(fa > tiny)) fa = 0; farm[c] = fa; dCol += o - fa; }
        if (ur !== 0) { const o = ur; ur *= fUrban; if (!(ur > tiny)) ur = 0; urban[c] = ur; dCol += o - ur; }
      }

      // ---------------- soil: toward the natural level (rich by fresh water, on lowlands and on old ash)
      const h = e - datum, fr = fresh[c];
      let f = fert[c];
      {
        let nat = T.fertBase + T.fertVar * region[c] + (fr < 4 ? fFresh[fr] : 0) + T.fertAsh * s;
        if (h < 1.2) nat += T.fertLow * (h > 0 ? 1 - h / 1.2 : 1);
        else if (h > 2.2) nat -= T.fertHigh * (h < 5.2 ? (h - 2.2) / 3 : 1);
        if (nat < T.fertMin) nat = T.fertMin; else if (nat > T.fertMax) nat = T.fertMax;
        f += (nat - f) * kFert;
        if (f === f) fert[c] = f; else f = fert[c] = 0.5;
      }

      // ---------------- vegetation
      const isLake = water[c] > lake, hard = isLake || ice[c] > iceT || lava[c] > lavaT;
      const bi = biome[c];
      let cap = 0;
      if (!hard) {
        cap = capB[bi] * (0.6 + 0.4 * f) * (1 - fa) * (1 - ur) * (1 - 0.8 * r);
        if (!(cap > 0)) cap = 0; else if (cap > 1) cap = 1;
      }
      const v0 = veg[c], v = v0 > 0 ? v0 : 0;
      let nv = v, sprouted = false;
      if (v > 0) {
        if (v < cap) nv = cap * v / (v + (cap - v) * eGrow);
        else if (v > cap) {
          nv = v + (cap - v) * (hard ? kKill : kDie);
          if (nv < tiny && cap < tiny) nv = 0;
        }
      } else if (cap > plantMin) {
        const b = 6 * c;
        let k = 0, a = 0;
        for (let j = 0; j < 6; j++) {
          const m = nbr[b + j];
          if (m < 0) break;
          const vm = veg[m];
          if (vm > est) k++;
          else if (vm > 0 && vm >= estRel * capOf(world, m, T)) k++;
          else { const am = air[m]; if (am > a) a = am; }
        }
        if (k > 0 || a > 0) {
          let q = (sc0 + (1 - sc0) * sat(cap / scRef)) * (k > 0 ? spK[k] : spK[1] * a) * Math.exp(spN * (grain[c] - 0.5) + spP * (patch[c] - 0.5));
          if (fr < 2) q *= spF[fr];
          q /= 1 + spS * slope[c] * sk;
          if (rng.f() < 1 - Math.exp(-spreadG * q)) { nv = sprout < cap ? sprout : cap; sprouted = true; }
        }
      }
      if (nv > 0) green++;
      if (sprouted) {
        // planted after the pass, so a sprout never seeds its own neighbours within the call that made it
        // (the front would otherwise run ahead along the order in which the cells are visited)
        sproutC[ns] = c; sproutV[ns++] = nv;
        dCol += nv;
        if (nv >= vt0) objects = true;
      } else if (nv !== v0) {
        veg[c] = nv;
        dCol += nv > v ? nv - v : v - nv;
        if ((v >= vt0) !== (nv >= vt0) || (v >= vt1) !== (nv >= vt1) || (v >= vt2) !== (nv >= vt2)) objects = true;
      }

      // ---------------- seed pressure over dead ground
      if (cap > plantMin) { if (air[c] !== 0) air[c] = 0; }
      else {
        const b = 6 * c;
        let a = 0;
        for (let j = 0; j < 6; j++) {
          const m = nbr[b + j];
          if (m < 0) break;
          const am = veg[m] > airVeg ? 1 : air[m];
          if (am > a) a = am;
        }
        a *= airDecay;
        air[c] = a > 1e-4 ? a : 0;
      }

      // ---------------- wildlife
      let F = fauna[c], K;
      if (isLake) K = faunaWater(world, c, T, -1);
      else {
        const fc = faB[bi];
        if (fc > 0 && nv > 0 && !(ice[c] > iceT)) {
          const rel = nv / (0.7 * capB[bi] + 0.03);
          K = fc * Math.sqrt(rel > 1 ? 1 : rel) * (1 - ur) * (1 - 0.5 * fa) * (1 - r);
          if (!(K > 0)) K = 0; else if (K > 1) K = 1;
        } else K = 0;
      }
      if (F > K) { F += (K - F) * kFaunaDie; fauna[c] = F > tiny || K > 0 ? F : 0; }
      else if (F > 0) { if (F < K) fauna[c] = K * F / (F + (K - F) * eFauna); }
      else if (K > 0.02) {
        if (!isLake) fauna[c] = fseed < K ? fseed : K;
        else {
          const b = 6 * c;
          for (let j = 0; j < 6; j++) { const m = nbr[b + j]; if (m < 0) break; if (fauna[m] > fsrc) { fauna[c] = fseed < K ? fseed : K; break; } }
        }
      } else if (F !== 0) fauna[c] = 0;

      // a BARREN cell whose lava has cooled and whose ash has thinned is due for a new biome
      if (bi === BARREN && !(lava[c] > lavaT) && !(s > T.scorchBarren)) relife = true;
    }
    for (let i = 0; i < ns; i++) veg[sproutC[i]] = sproutV[i];

    // Did any slice hold vegetation when it was last visited? (On a dead planet the drift has nothing to look for.)
    let G = null, alive = true;
    if (parts <= 64) {
      G = world._lifeGreen;
      if (!G || G.parts !== parts || !G.n) G = world._lifeGreen = { parts, n: new Int32Array(parts).fill(-1) };      // −1: not visited yet
      G.n[part] = green;
      alive = false;
      for (let i = 0; i < parts; i++) if (G.n[i] !== 0) { alive = true; break; }
    }

    // ---------------- drifting seeds (a planet-wide process: picks per tick, not per slice)
    if (alive) {
      const want = T.driftPicks * TICK * boost;
      let picks = Math.floor(want);
      if (rng.f() < want - picks) picks++;
      if (picks > T.driftMax) picks = T.driftMax;
      const hops = T.driftHops > 0 ? T.driftHops | 0 : 0;
      const thin = boost > 1 ? sat(Math.pow(boost, T.driftBoost - 1)) : 1;          // chance that an overland pick takes
      const thinSea = boost > 1 ? sat(Math.pow(boost, T.driftSeaBoost - 1)) : 1;    // ... and one across water
      for (let p = 0; p < picks; p++) {
        let c = -1;
        for (let tries = 0; tries < 8; tries++) { const q = (rng.f() * n) | 0; if (elev[q] >= sea) { c = q; break; } }
        if (c < 0 || veg[c] !== 0) continue;
        if (!(capOf(world, c, T) > plantMin)) continue;
        const buf = ringBuf(n, hops), cnt = world.geo.ring(c, hops, buf);
        let found = false;
        for (let i = 1; i < cnt; i++) if (sheds(world, buf[i], T)) { found = true; break; }
        if (!found) continue;
        if (thin < 1 || thinSea < 1) {
          const chance = thin === thinSea || overland(world, c, hops, T) ? thin : thinSea;
          if (chance < 1 && !(rng.f() < chance)) continue;
        }
        veg[c] = T.driftVeg; dCol += 1;
        if (G && G.n[c % parts] >= 0) G.n[c % parts]++;
        if (T.driftVeg >= vt0) objects = true;
      }
    }

    // colour: rebuild when enough has visibly changed (slow drifts add up over several slices)
    let acc = (world._lifeAcc > 0 ? world._lifeAcc : 0) + dCol;
    if (acc >= 0.25) { world.dirty.color = true; acc = 0; }
    world._lifeAcc = acc;
    if (objects) world.dirty.objects = true;
    if (relife) world.need.life = true;
    bump(world);
  };

  // ------------------------------------------------------------------ direct edits
  Life.seed = function (world, c, ang) {
    if (!world || !validCell(world, c)) return 0;
    const T = TUNE.life;
    if (!(ang >= 0)) ang = 0.12;
    const list = scratch(world.n).list, cnt = world.geo.within(c, ang, list);
    const veg = world.veg, sv = f32up(T.seedVeg);
    let k = 0;
    for (let i = 0; i < cnt; i++) {
      const m = list[i];
      if (!(capOf(world, m, T) > T.plantMin)) continue;
      if (veg[m] < sv) { veg[m] = sv; k++; }
    }
    if (k) { world.dirty.color = true; world.dirty.objects = true; greenUnknown(world); bump(world); }
    return k;
  };

  Life.bloom = function (world, c, ang, amount) {
    if (!world || !validCell(world, c) || !(amount > 0)) return 0;
    const T = TUNE.life, geo = world.geo, VT = DM.State.VEG_T;
    if (!(ang >= 0)) ang = 0.08;
    const list = scratch(world.n).list, cnt = geo.within(c, ang, list);
    const veg = world.veg, fert = world.fert, scorch = world.scorch, elev = world.elev, sea = world.seaLevel;
    let k = 0, objects = false;
    for (let i = 0; i < cnt; i++) {
      const m = list[i];
      if (elev[m] < sea) continue;
      const d = ang > 0 ? geo.angle(c, m) / ang : 0;
      const fall = d >= 1 ? 0 : 1 - d * d * (3 - 2 * d);                // 1 at the centre, 0 at the rim
      const a = amount * fall;
      if (!(a > 0)) continue;
      const f = fert[m];
      if (f < T.fertCap) fert[m] = f + a > T.fertCap ? T.fertCap : f + a;
      const s = scorch[m];
      if (s > 0) scorch[m] = s > a ? s - a : 0;
      const cap = capOf(world, m, T), v = veg[m];
      if (cap > T.plantMin && v < cap) {
        const nv = v + a > cap ? cap : v + a;
        veg[m] = nv;
        if ((v >= VT[0]) !== (nv >= VT[0]) || (v >= VT[1]) !== (nv >= VT[1]) || (v >= VT[2]) !== (nv >= VT[2])) objects = true;
      }
      k++;
    }
    if (k) { world.dirty.color = true; if (objects) world.dirty.objects = true; greenUnknown(world); bump(world); }
    return k;
  };

  Life.fill = function (world, frac) {
    const T = TUNE.life, n = world.n, elev = world.elev, sea = world.seaLevel, veg = world.veg, fauna = world.fauna;
    frac = frac === undefined ? 1 : sat(+frac);
    if (!(frac >= 0)) frac = 1;
    for (let c = 0; c < n; c++) {
      if (elev[c] < sea) { veg[c] = 0; fauna[c] = faunaWater(world, c, T, sea - elev[c]); continue; }
      const v = frac * capOf(world, c, T);
      veg[c] = v;
      fauna[c] = world.water[c] > T.lakeWater ? faunaWater(world, c, T, -1) : faunaLand(world, c, T, v);
    }
    world.dirty.color = true;
    world.dirty.objects = true;
    greenUnknown(world);
    bump(world);
  };

  // ------------------------------------------------------------------ habitability
  Life.habitability = function (world) {
    const T = TUNE.life, B = DM.BIOME, n = world.n;
    const elev = world.elev, water = world.water, ice = world.ice, lava = world.lava, temp = world.temp, moist = world.moist;
    const fert = world.fert, slope = world.slope, coast = world.coast, fresh = world.fresh, rad = world.rad;
    const biome = world.biome, hab = world.hab;
    const sea = world.seaLevel, sk = slopeScale(world);
    const c0 = T.habCold[0], c1 = T.habCold[1], h0 = T.habHot[0], h1 = T.habHot[1];
    const r0 = T.habRain[0], r1 = T.habRain[1], rs = r1 > r0 ? 1 / (r1 - r0) : 0;
    const s0 = T.habSlope[0], s1 = T.habSlope[1], ss = s1 > s0 ? 1 / (s1 - s0) : 0;
    const hF = T.habFresh, hC = T.habCoast, hf = T.habFert, hmin = T.habMin;
    const lid = world._landId, useLid = !!lid && lid.length === n;
    let kLand = 0;
    if (useLid) for (let c = 0; c < n; c++) if (lid[c] >= kLand) kLand = lid[c] + 1;
    const landHab = new Int32Array(kLand);
    for (let c = 0; c < n; c++) {
      let v = 0;
      const b = biome[c];
      if (!(elev[c] < sea) && !(water[c] > T.lakeWater) && !(ice[c] > T.iceBiome) && !(lava[c] > T.lavaBarren) &&
          b !== B.ICE && b !== B.ROCK && b !== B.SNOWPEAK && b !== B.BARREN && b !== B.SEA && b !== B.SHALLOWS && b !== B.SEAICE) {
        const t = temp[c];
        const ts = sm(c0, c1, t) * (1 - sm(h0, h1, t));
        let wr = (moist[c] - r0) * rs;
        wr = wr < 0 ? 0 : wr > 1 ? 1 : wr;
        const fr = fresh[c], wf = fr < hF.length ? hF[fr] : 0;
        const wat = wr > wf ? wr : wf;
        const fe = hf + (1 - hf) * sat(fert[c]);
        let sl = 1 - (slope[c] * sk - s0) * ss;
        sl = sl < 0 ? 0 : sl > 1 ? 1 : sl;
        const co = coast[c], cb = co < hC.length ? hC[co] : 1;
        v = ts * wat * fe * sl * cb * (1 - sat(rad[c]));
        if (!(v > 0)) v = 0; else if (v > 1) v = 1;
      }
      hab[c] = v;
      if (useLid && v > hmin) { const l = lid[c]; if (l >= 0) landHab[l]++; }
    }
    world._landHab = landHab;
    world.need.life = false;
  };

  // ------------------------------------------------------------------ cover
  Life.cover = function (world) {
    const cc = world._lifeCover, ver = world._lifeVer | 0;
    if (cc && cc.tick === world.tick && cc.ver === ver && cc.sea === world.seaLevel) return cc.value;
    const T = TUNE.life, n = world.n, elev = world.elev, veg = world.veg, sea = world.seaLevel, fv = T.forestVeg;
    let land = 0, sumV = 0, sumC = 0, forest = 0;
    for (let c = 0; c < n; c++) {
      if (elev[c] < sea) continue;
      land++;
      const v = veg[c];
      if (v >= fv) forest++;
      const k = capOf(world, c, T);
      if (k > T.plantMin) { sumC += k; sumV += v < k ? v : k; }
    }
    const value = { land: n > 0 ? land / n : 0, veg: sumC > 0 ? sumV / sumC : 0, forest: land > 0 ? forest / land : 0 };
    world._lifeCover = { tick: world.tick, ver, sea, value };
    return value;
  };

})(globalThis.DM = globalThis.DM || {});
