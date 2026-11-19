// DEMIURGE — js/sim/terrain.js : DM.Terrain (SPEC-SIM §3.2). The only writer of world.elev.
//
//   Terrain.generate(world)                      fill elev and an initial fert from world.seed (world.rng is not advanced)
//   Terrain.sculpt(world, c, ang, amount) → n    add amount × (1 − t²)² (t = distance / ang) around cell c; n = cells changed
//   Terrain.crater(world, c, ang, depth) → n     bowl of the given depth inside ang (the ground is half levelled first),
//                                                raised rim at 1.0–1.3 × ang; nothing moves beyond 1.5 × ang
//   Terrain.cone(world, c, ang, height) → n      volcanic cone built on the level of cell c: crest ring `height` above it,
//                                                summit pit a quarter of that deep; never lowers a cell. A cone narrower
//                                                than about 1.5 cells is a plain one-cell peak
//   Terrain.roughen(world, c, ang, amount, rng) → n   smooth random ± displacement (largest = amount, fading to 0 at ang);
//                                                makes no one-cell pits; draws from rng (world.rng when omitted)
//   Every operation returns the number of cells it changed, clamps to State.ELEV_MIN … ELEV_MAX, calls touched() when
//   something changed, and returns 0 without touching anything for a bad cell or amount. ang is in radians.
//   Terrain.touched(world)                       dirty.elev / water / color and need.derive / climate / hydro
//   Terrain.derive(world)                        slope, coast, world._landId (+ world._landSize); clears need.derive, sets need.life
//
// Transient caches written by derive:
//   world._landId    Int32Array(n)   landmass id of each land cell (−1 on sea cells); ids 0…k−1 by descending size
//   world._landSize  Int32Array(k)   cell count of each landmass
//
// How generate works (all in elevation units of the finished world; sea level 0):
//   1. Plates. 10–14 seed points on the sphere, each a plate with a size weight, a crust bias (continental or
//      oceanic), a rotation pole and a rank. Cells are assigned by domain-warped distance, so plate edges are curved.
//      Relative plate motion across an edge gives convergence (mountain belts, island arcs, trenches) or
//      divergence (mid-ocean ridges, rift valleys). A second, coarser set of "fossil" plates marks old sutures:
//      long worn ranges that cross the continents.
//   2. Continents. Domain-warped fBm plus the plate bias (blended across edges) plus coastline detail whose
//      amplitude varies by region (smooth coasts here, ragged ones there). Several plate layouts are scored on the
//      level-4 cells (cheap) and the best is built: 3–6 real landmasses, no all-swallowing supercontinent. The
//      finished world is rated the same way; if its relief bridged the continents, the next best layout is built
//      (at most `rebuilds` more) and the best world is kept. The field
//      is rank-mapped: the top `land` share of cells is land, and a cell's rank inside land or sea drives a
//      hypsometric base curve (broad lowlands, interior swells and tablelands; shelf, continental slope, abyss).
//   3. Relief. Ridged belts along convergent edges (massifs and saddles along strike, a plateau behind), the old
//      ranges, beaded island arcs and trenches at subduction edges, mid-ocean ridges, rifts, abyssal hills, hotspot
//      island chains, hills that grow with altitude. Every noise is band-limited to what the cells can carry.
//   4. Tidying. Puddle-sized enclosed seas become land basins (lakes for Hydro), some one-cell specks sink.
//   5. Erosion. A priority flood from the sea gives every land cell a drainage path and a catchment; an implicit
//      stream-power pass cuts valleys headward along those paths (big rivers run graded to the sea, ridges stay),
//      a talus limit removes cliffs, and the lowlands are smoothed into plains.
//   6. Normalisation. The shoreline is put back on the exact quantile, then monotone piecewise-linear remaps
//      guarantee the lowland share, the alpine share, the peak height and the deepest trench for every seed.
//
// generate uses only + − × ÷, Math.sqrt, Math.floor, DM.U.noise3 / hashf and the Rng, all of which are bit-identical
// on every JS engine, so a seed gives the same planet in Node and in any browser (checked: Node 24 against Edge 157).
(function (DM) {
  'use strict';

  const TUNE = (DM.TUNE = DM.TUNE || {});
  TUNE.terrain = {
    // ---- generation: layout
    land: 0.39,            // land fraction at sea level 0
    candidates: 12,        // at most this many plate layouts are scored on the level-4 cells (scoring pauses at the first
                           // with full marks and resumes only if the world built from it disappoints)
    scoreExtra: 0.04,      // extra land fraction used when judging whether landmasses are separate
    rebuilds: 2,           // further layouts built when the finished world lost a continent to a land bridge
    rateSecond: 0.034, rateThird: 0.02, rateTop: 0.62,   // a finished world is kept at once when its second and third
                           // landmasses are at least these shares of the planet and the largest holds at most rateTop of the land
    platesMin: 10, platesMax: 14,
    contShare: 0.42,       // share of plates with continental crust
    warpF: 1.15,           // frequency of the domain warp
    warpPlate: 0.24,       // warp amplitude for plate edges
    warpCont: 0.30,        // warp amplitude for the continent noise
    contF: 1.35,           // base frequency of the continent fBm
    contOct: 5,
    plateAmp: 0.85,        // weight of the plate crust bias against unit-variance continent noise
    biasW: 0.12,           // half-width (rad) over which the crust bias blends across a plate edge
    coastF: 8.5,           // frequency of the coastline detail
    coastLo: 0.05, coastHi: 0.24,   // its amplitude on smooth and on ragged coasts
    // ---- generation: plate edges
    convMin: 0.12, convSpan: 0.75,  // relative normal speed → edge strength 0..1
    beltC: 0.045, beltW: 0.085,     // mountain belt: centre distance from the edge and half-width (rad)
    beltUnder: 0.55,                // belt strength on the subducting side
    wideW: 0.17,                    // plateau behind the belt
    arcC: 0.065, arcW: 0.045,       // island arc (overriding side)
    trenchC: 0.02, trenchW: 0.04,   // trench (subducting side)
    riftW: 0.07, riftN: 0.028,      // spreading ridge half-width and its axial valley
    // ---- generation: fossil sutures (old worn ranges)
    fossilMin: 7, fossilMax: 10,
    warpFossil: 0.34,
    oldW: 0.058,                    // half-width (rad)
    oldCut: 0.5,                    // share of fossil edges that carry no range
    // ---- generation: relief amplitudes (world units)
    lowSlope: 0.5, upland: 1.25,    // base land curve: 0.03 + lowSlope·u + upland·u²·basin
    swellF: 2.6, swellH: 0.9,       // broad interior swells and basins
    tabF: 2.3, tabCut: 0.36, tabEdge: 0.07, tabH: 0.95,   // tablelands with crisp escarpments
    beltH: 6.4, plateauH: 0.9, strikeF: 3.3,
    oldH: 2.7,
    ridgeF: 4.6, ridgeStretch: 3,
    hillF: 7.5, hillLow: 0.04, hillHigh: 0.42, hillBelt: 0.5, hillRegion: 0.22,
    shelfV: 0.16, shelfDepth: 0.5, slopeV: 0.31, slopeDepth: 3.4, abyssDepth: 3.0,
    arcH: 5.4, arcSwell: 1.3, trenchH: 2.0, ridgeH: 1.2, riftH: 0.6,
    chainsMin: 3, chainsMax: 5,     // hotspot island chains
    cellsPerWave: 3,                // noise octaves finer than this many cells per wavelength fade out
    // ---- generation: tidying and erosion
    minSea: 0.0005,        // enclosed seas smaller than this share of the planet become land basins
    shelfLim: [0.3, 0.95, 2.4],        // deepest sea floor 1, 2, 3 hops from land on the widest shelves (up to 2.5 × deeper on narrow ones) …
    shelfActive: 4,        // … times 1 + this at an active margin
    speckKeep: 0.5,        // share of one-cell islands that survive
    erodeK: 0.10,          // headward reach (rad) of a river draining erodeRef of the planet
    erodeRef: 0.01,
    erodeMin: 0.00006,     // catchments smaller than this share of the planet do not cut
    erodeMax: 12,          // cap on the reach, in cells
    erodeSide: 0.45,       // share of a river's reach passed on to the cells that drain into it
    talus: 1.3,            // steepest slope left along a drainage path (rise per unit of distance)
    smoothTop: 1.0,        // land below this height is smoothed (fully at the shore, not at all at the top)
    smoothK: 0.55,
    // ---- generation: guarantees
    lowQ: 0.32, lowE: 0.55,         // at least lowQ of the land ends up below lowE
    midQ: 0.86,                     // land below this quantile is never stretched
    highQ: 0.965, highE: 3.6,       // at least 1 − highQ of the land ends up above highE
    peakMin: 6.4, peakMax: 8.2,     // the highest summit
    deepMin: 6.2, deepMax: 6.6,     // the deepest trench
    deepKnee: 5.4,                  // only depths beyond this are rescaled to get there
    // ---- operations
    minBrush: 0.8,         // craters and cones are at least this many cell steps in radius
    craterRim: 0.22,       // rim height as a share of the depth
    craterFlat: 0.5,       // how far the bowl is levelled toward the mean ground before digging
    conePit: 0.25,         // summit crater radius as a share of the cone radius (at least one cell)
    conePitDepth: 0.25,    // its depth as a share of the cone height
    coneLevel: 1,          // how far a cone buries the ground under its summit toward the vent's level (0 = purely additive)
    roughPasses: 3,        // smoothing passes over the random field of roughen (0 = white noise, cell against cell)
  };

  const Terrain = (DM.Terrain = DM.Terrain || {});

  // ------------------------------------------------------------------ scratch (keyed by world.n)

  let S = null;
  function scratch(n) {
    if (!S || S.n !== n) {
      S = {
        n,
        list: new Int32Array(n),     // geo.within output
        queue: new Int32Array(n),    // BFS queues
        tmpI: new Int32Array(n),
        val: new Float32Array(n),
        val2: new Float32Array(n),
        stamp: new Int32Array(n),
        stampId: 0,
      };
    }
    return S;
  }
  function nextStamp(s) {
    if (s.stampId >= 0x7ffffff0) { s.stamp.fill(0); s.stampId = 0; }
    return ++s.stampId;
  }

  function validCell(world, c) {
    return !!world && typeof c === 'number' && c >= 0 && c < world.n && c === Math.floor(c);
  }
  function clampElev(v, old) {
    if (!(v === v)) return old;                                   // never write NaN
    const lo = DM.State.ELEV_MIN, hi = DM.State.ELEV_MAX;
    return v < lo ? lo : v > hi ? hi : v;
  }

  // Squared angle between cells a and b, from the chord (exact to 1e-4 below 0.5 rad; acos beyond).
  function angle2(pos, a, b) {
    const ia = 3 * a, ib = 3 * b;
    const dx = pos[ia] - pos[ib], dy = pos[ia + 1] - pos[ib + 1], dz = pos[ia + 2] - pos[ib + 2];
    const ch2 = dx * dx + dy * dy + dz * dz;
    if (ch2 < 0.25) return ch2 * (1 + ch2 * (1 / 12 + ch2 * (1 / 90)));
    const d = 1 - 0.5 * ch2;
    const a_ = Math.acos(d < -1 ? -1 : d > 1 ? 1 : d);
    return a_ * a_;
  }

  // ------------------------------------------------------------------ flags

  Terrain.touched = function (world) {
    const d = world.dirty, nd = world.need;
    d.elev = true; d.water = true; d.color = true;
    nd.derive = true; nd.climate = true; nd.hydro = true;
  };

  // ------------------------------------------------------------------ sculpting operations

  Terrain.sculpt = function (world, c, ang, amount) {
    if (!validCell(world, c) || !Number.isFinite(amount) || amount === 0) return 0;
    if (!(ang > 0)) ang = 0;
    if (ang > Math.PI) ang = Math.PI;
    const geo = world.geo, pos = geo.pos, elev = world.elev, s = scratch(world.n), list = s.list;
    const cnt = geo.within(c, ang, list);
    const inv = ang > 0 ? 1 / (ang * ang) : 0;
    let changed = 0;
    for (let i = 0; i < cnt; i++) {
      const m = list[i];
      const t2 = m === c ? 0 : angle2(pos, c, m) * inv;
      if (t2 >= 1) continue;
      const f = (1 - t2) * (1 - t2);
      const old = elev[m];
      elev[m] = clampElev(old + amount * f, old);
      changed++;
    }
    if (changed) Terrain.touched(world);
    return changed;
  };

  Terrain.crater = function (world, c, ang, depth) {
    if (!validCell(world, c) || !Number.isFinite(depth) || !(depth > 0)) return 0;
    const P = TUNE.terrain, geo = world.geo, pos = geo.pos, elev = world.elev, s = scratch(world.n), list = s.list;
    if (!(ang > 0)) ang = 0;
    if (ang > 2) ang = 2;
    const R = Math.max(ang, P.minBrush * geo.step);
    const cnt = geo.within(c, R * 1.5, list);
    const inv = 1 / (R * R);
    // mean ground level inside the bowl: the blast levels the ground before it digs
    let ref = 0, k = 0;
    for (let i = 0; i < cnt; i++) {
      const m = list[i];
      if (m === c || angle2(pos, c, m) * inv < 1) { ref += elev[m]; k++; }
    }
    ref = k ? ref / k : elev[c];
    const U = DM.U, rimH = P.craterRim * depth;
    let changed = 0;
    for (let i = 0; i < cnt; i++) {
      const m = list[i];
      const t2 = m === c ? 0 : angle2(pos, c, m) * inv;
      const t = Math.sqrt(t2);
      const old = elev[m];
      let v = old;
      if (t2 < 1) {
        const bowl = 1 - t2;
        v += (ref - old) * P.craterFlat * bowl - depth * bowl;
      }
      if (t > 0.8 && t < 1.5) v += rimH * U.sstep(0.8, 1.12, t) * (1 - U.sstep(1.12, 1.5, t));
      if (v !== old) { elev[m] = clampElev(v, old); changed++; }
    }
    if (changed) Terrain.touched(world);
    return changed;
  };

  Terrain.cone = function (world, c, ang, height) {
    if (!validCell(world, c) || !Number.isFinite(height) || !(height > 0)) return 0;
    const P = TUNE.terrain, geo = world.geo, pos = geo.pos, nbr = geo.nbr, elev = world.elev, s = scratch(world.n), list = s.list;
    if (!(ang > 0)) ang = 0;
    if (ang > 2) ang = 2;
    const R = Math.max(ang, P.minBrush * geo.step);
    // Crest ring: a share of the radius, but just beyond the vent's farthest neighbour, so that on a small cone the
    // vent cell is the pit and every one of its neighbours stands on the crest.
    let far = 0;
    for (let j = 0, b = 6 * c; j < 6; j++) {
      const q = nbr[b + j];
      if (q < 0) break;
      const d2 = angle2(pos, c, q);
      if (d2 > far) far = d2;
    }
    let rc = Math.max(P.conePit * R, 1.02 * Math.sqrt(far));
    const pit = rc < 0.8 * R;                    // room for a pit and a flank? If not: a plain peak
    if (!pit) rc = 0;
    const cnt = geo.within(c, R, list);
    // The cone is built on the vent's own level: the old ground is buried in proportion to how close it is to the
    // summit (never dug away), so the crest stands `height` above the vent and rings the pit on any terrain.
    const ref = elev[c], level = P.coneLevel;
    let changed = 0;
    for (let i = 0; i < cnt; i++) {
      const m = list[i];
      const d = m === c ? 0 : Math.sqrt(angle2(pos, c, m));
      if (d >= R) continue;
      let h, w;
      if (d < rc) {
        const q = d / rc;
        h = 1 - P.conePitDepth * (1 - q * q);
        w = 1;
      } else {
        const q = 1 - (d - rc) / (R - rc);       // 1 at the crest, 0 at the foot
        h = q * Math.sqrt(q);
        w = q * q;
      }
      if (!(h > 0)) continue;
      const old = elev[m];
      const add = height * h + (ref - old) * w * level;
      if (!(add > 0)) continue;
      elev[m] = clampElev(old + add, old);
      changed++;
    }
    if (changed) Terrain.touched(world);
    return changed;
  };

  // Is cell m a one-cell pit: strictly below every neighbour?
  function isPit(nbr, elev, m) {
    const e = elev[m], b = 6 * m;
    for (let j = 0; j < 6; j++) {
      const q = nbr[b + j];
      if (q < 0) break;
      if (!(elev[q] > e)) return false;
    }
    return true;
  }
  function lowestNbr(nbr, elev, m) {
    const b = 6 * m;
    let low = Infinity;
    for (let j = 0; j < 6; j++) {
      const q = nbr[b + j];
      if (q < 0) break;
      if (elev[q] < low) low = elev[q];
    }
    return low;
  }

  Terrain.roughen = function (world, c, ang, amount, rng) {
    if (!validCell(world, c) || !Number.isFinite(amount) || amount === 0) return 0;
    if (!rng || typeof rng.f !== 'function') rng = world.rng;
    if (!(ang > 0)) ang = 0;
    if (ang > Math.PI) ang = Math.PI;
    const P = TUNE.terrain, geo = world.geo, pos = geo.pos, nbr = geo.nbr, elev = world.elev;
    const s = scratch(world.n), list = s.list, val = s.val, val2 = s.val2, stamp = s.stamp, ring = s.tmpI;
    const cnt = geo.within(c, ang, list);
    const id = nextStamp(s);
    for (let i = 0; i < cnt; i++) { const m = list[i]; stamp[m] = id; val[m] = rng.f() * 2 - 1; }

    // the cells one hop outside the patch (their neighbours move, so they can become pits too)
    const idRing = nextStamp(s);
    let nRing = 0;
    for (let i = 0; i < cnt; i++) {
      const b = 6 * list[i];
      for (let j = 0; j < 6; j++) {
        const q = nbr[b + j];
        if (q < 0) break;
        if (stamp[q] !== id && stamp[q] !== idRing) { stamp[q] = idRing; ring[nRing++] = q; }
      }
    }
    // pits that are there already (a crater floor, a volcano's summit) are not ours to fill
    const wasPit = s.queue;
    let nPit = 0;
    for (let i = 0; i < cnt + nRing; i++) {
      const m = i < cnt ? list[i] : ring[i - cnt];
      if (isPit(nbr, elev, m)) wasPit[nPit++] = m;
    }

    // smooth the random field inside the patch so that neighbours move together
    let a = val, bb = val2;
    for (let pass = 0; pass < P.roughPasses; pass++) {
      for (let i = 0; i < cnt; i++) {
        const m = list[i], b = 6 * m;
        let sum = 2 * a[m], w = 2;
        for (let j = 0; j < 6; j++) {
          const q = nbr[b + j];
          if (q < 0) break;
          if (stamp[q] === id) { sum += a[q]; w++; }
        }
        bb[m] = sum / w;
      }
      const t = a; a = bb; bb = t;
    }
    let peak = 0;
    const inv = ang > 0 ? 1 / (ang * ang) : 0;
    for (let i = 0; i < cnt; i++) {
      const m = list[i];
      const t2 = m === c ? 0 : angle2(pos, c, m) * inv;
      const f = t2 >= 1 ? 0 : (1 - t2) * (1 - t2);
      const v = a[m] * f;
      bb[m] = v;
      if (v > peak) peak = v; else if (-v > peak) peak = -v;
    }
    // apply (the old heights are kept in `a`, which is free now, to count what really moved)
    const k = peak > 0 ? amount / peak : 0;
    for (let i = 0; i < cnt; i++) {
      const m = list[i], old = elev[m], v = bb[m] * k;
      a[m] = old;
      if (v !== 0) elev[m] = clampElev(old + v, old);
    }
    for (let i = 0; i < nRing; i++) a[ring[i]] = elev[ring[i]];
    // Fill the one-cell pits the shaking made, up to their lowest neighbour. Raising a pit to that level cannot
    // make a neighbour a pit (the neighbour is then not strictly below it), so one sweep is enough. A filled cell
    // still ends within `amount` of where it was: it was no pit before, so one neighbour started at or below it.
    const idPit = nextStamp(s);
    for (let i = 0; i < nPit; i++) stamp[wasPit[i]] = idPit;
    let changed = 0;
    for (let i = 0; i < cnt + nRing; i++) {
      const m = i < cnt ? list[i] : ring[i - cnt];
      if (stamp[m] !== idPit && isPit(nbr, elev, m)) {
        const lvl = lowestNbr(nbr, elev, m);
        if (lvl < Infinity) elev[m] = clampElev(lvl, elev[m]);
      }
      if (elev[m] !== a[m]) changed++;
    }
    if (changed) Terrain.touched(world);
    return changed;
  };

  // ------------------------------------------------------------------ derived fields

  Terrain.derive = function (world) {
    const geo = world.geo, n = world.n, nbr = geo.nbr, elev = world.elev, sea = world.seaLevel;
    const slope = world.slope, coast = world.coast;
    const s = scratch(n), q = s.queue, sizes = s.tmpI;
    let lid = world._landId;
    if (!lid || lid.length !== n) lid = world._landId = new Int32Array(n);

    // slope, and seed the coast search with every sea cell
    let tail = 0;
    for (let c = 0; c < n; c++) {
      const e = elev[c], b = 6 * c;
      let sl = 0;
      for (let j = 0; j < 6; j++) {
        const m = nbr[b + j];
        if (m < 0) break;
        let d = elev[m] - e;
        if (d < 0) d = -d;
        if (d > sl) sl = d;
      }
      slope[c] = sl;
      if (e < sea) { coast[c] = 0; lid[c] = -1; q[tail++] = c; } else { coast[c] = 255; lid[c] = -2; }
    }

    // coast: breadth-first hops from the sea, capped at 255
    for (let head = 0; head < tail; head++) {
      const c = q[head], d = coast[c] + 1, b = 6 * c;
      if (d >= 255) continue;
      for (let j = 0; j < 6; j++) {
        const m = nbr[b + j];
        if (m < 0) break;
        if (coast[m] > d) { coast[m] = d; q[tail++] = m; }
      }
    }

    // landmasses: flood fill, then renumber by descending size (ties: the one found first, i.e. lowest cell id)
    let k = 0;
    for (let c0 = 0; c0 < n; c0++) {
      if (lid[c0] !== -2) continue;
      const tag = -3 - k;                                         // temporary labels −3, −4, …
      let head = 0, end = 0;
      q[end++] = c0; lid[c0] = tag;
      while (head < end) {
        const b = 6 * q[head++];
        for (let j = 0; j < 6; j++) {
          const m = nbr[b + j];
          if (m < 0) break;
          if (lid[m] === -2) { lid[m] = tag; q[end++] = m; }
        }
      }
      sizes[k++] = end;
    }
    const order = new Array(k);
    for (let i = 0; i < k; i++) order[i] = i;
    order.sort(function (a, b) { return (sizes[b] - sizes[a]) || (a - b); });
    const finalId = new Int32Array(k), landSize = new Int32Array(k);
    for (let i = 0; i < k; i++) { finalId[order[i]] = i; landSize[i] = sizes[order[i]]; }
    for (let c = 0; c < n; c++) { const t = lid[c]; if (t <= -3) lid[c] = finalId[-3 - t]; }
    world._landSize = landSize;

    world.need.derive = false;
    world.need.life = true;
  };

  // ------------------------------------------------------------------ generation: helpers

  let noise3 = null;                             // DM.U.noise3, looked up when generate runs
  const OX = 31.416, OY = -47.853, OZ = 12.679;  // per-octave offsets, so that octaves never line up

  // fBm in about [−1, 1] whose octaves fade out as their frequency passes what the cells can carry
  // (full below fmax, gone at 2·fmax): a coarse planet is the same planet with less detail, not aliased speckle.
  function fbmB(x, y, z, f, oct, lac, gain, fmax, ox, oy, oz) {
    let sum = 0, tot = 0, amp = 1;
    for (let o = 0; o < oct; o++) {
      tot += amp;
      if (f < 2 * fmax) {
        const a = f <= fmax ? amp : amp * (2 - f / fmax);
        sum += a * noise3(x * f + ox + o * OX, y * f + oy + o * OY, z * f + oz + o * OZ);
      }
      amp *= gain; f *= lac;
    }
    return sum / tot;
  }
  // Ridged multifractal in [0, 1] (sharp crests near 1), band-limited the same way.
  function ridgeB(x, y, z, f, oct, lac, gain, fmax, ox, oy, oz) {
    let sum = 0, tot = 0, amp = 1, w = 1;
    for (let o = 0; o < oct; o++) {
      tot += amp;
      if (f < 2 * fmax) {
        let v = noise3(x * f + ox + o * OX, y * f + oy + o * OY, z * f + oz + o * OZ);
        v = 1 - (v < 0 ? -v : v);
        v *= v;
        sum += (f <= fmax ? amp : amp * (2 - f / fmax)) * w * v;
        w = v * 2;
        if (w > 1) w = 1;
      }
      amp *= gain; f *= lac;
    }
    return sum / tot;
  }

  function bump(x, c, w) {                       // smooth bump of half-width w centred on c
    const t = (x - c) / w, u = 1 - t * t;
    return u > 0 ? u * u : 0;
  }
  function unitVec(rng, out, o) {
    const x = rng.gauss(), y = rng.gauss(), z = rng.gauss();
    const l = Math.sqrt(x * x + y * y + z * z);
    if (l < 1e-9) { out[o] = 0; out[o + 1] = 1; out[o + 2] = 0; return; }
    out[o] = x / l; out[o + 1] = y / l; out[o + 2] = z / l;
  }
  // continuous rank of v in an ascending array (index + fraction)
  function rankOf(sorted, n, v) {
    let lo = 0, hi = n - 1;
    if (v <= sorted[0]) return 0;
    if (v >= sorted[hi]) return hi;
    while (hi - lo > 1) { const mid = (lo + hi) >> 1; if (sorted[mid] <= v) lo = mid; else hi = mid; }
    const a = sorted[lo], b = sorted[hi];
    return b > a ? lo + (v - a) / (b - a) : lo;
  }
  function landCount(n) {
    return Math.max(1, Math.min(n - 1, Math.round(TUNE.terrain.land * n)));
  }
  // the value that leaves exactly nLand cells at or above it
  function shoreOf(E, sorted, n, nLand) {
    sorted.set(E);
    sorted.sort();
    return sorted[n - Math.max(1, Math.min(n - 1, nLand))];
  }
  // Sizes of the connected components of { f ≥ thr } over the first n cells, largest first.
  function components(nbr, n, f, thr, lab, q) {
    const sizes = [];
    lab.fill(0, 0, n);
    for (let c0 = 0; c0 < n; c0++) {
      if (lab[c0] || !(f[c0] >= thr)) continue;
      let head = 0, end = 0;
      q[end++] = c0; lab[c0] = 1;
      while (head < end) {
        const b = 6 * q[head++];
        for (let j = 0; j < 6; j++) {
          const m = nbr[b + j];
          if (m < 0) break;
          if (!lab[m] && f[m] >= thr) { lab[m] = 1; q[end++] = m; }
        }
      }
      sizes.push(end);
    }
    sizes.sort(function (a, b) { return b - a; });
    return sizes;
  }

  // ------------------------------------------------------------------ generation: plates and continents

  // Step 1: a plate layout (everything random about the large-scale shape of the world).
  function makePlates(rng) {
    const P = TUNE.terrain;
    const K = rng.int(P.platesMin, P.platesMax);
    const seed = new Float64Array(3 * K), rot = new Float64Array(3 * K);
    const wt = new Float64Array(K), bias = new Float64Array(K), prank = new Float64Array(K);
    const cand = new Float64Array(3);
    for (let i = 0; i < K; i++) {
      let best = -1;
      for (let t = 0; t < (i === 0 ? 1 : 6); t++) {              // best of six candidates: spread out, not regular
        unitVec(rng, cand, 0);
        let md = 4;
        for (let j = 0; j < i; j++) {
          const d = 2 - 2 * (cand[0] * seed[3 * j] + cand[1] * seed[3 * j + 1] + cand[2] * seed[3 * j + 2]);
          if (d < md) md = d;
        }
        if (md > best) { best = md; seed[3 * i] = cand[0]; seed[3 * i + 1] = cand[1]; seed[3 * i + 2] = cand[2]; }
      }
      wt[i] = rng.range(0, 0.11);
      unitVec(rng, rot, 3 * i);
      const rate = rng.range(0.45, 1);
      rot[3 * i] *= rate; rot[3 * i + 1] *= rate; rot[3 * i + 2] *= rate;
      bias[i] = -rng.range(0.6, 1);
      prank[i] = rng.f();
    }
    // continental plates: each pick is the better-separated of two random candidates
    const nCont = Math.max(3, Math.round(K * P.contShare));
    const isCont = new Uint8Array(K);
    for (let i = 0; i < nCont; i++) {
      let pick = -1, pickD = -1;
      for (let t = 0; t < 2; t++) {
        let k = rng.int(0, K - 1), guard = 0;
        while (isCont[k] && guard++ < K) k = (k + 1) % K;
        let md = 4;
        for (let j = 0; j < K; j++) {
          if (!isCont[j]) continue;
          const d = 2 - 2 * (seed[3 * k] * seed[3 * j] + seed[3 * k + 1] * seed[3 * j + 1] + seed[3 * k + 2] * seed[3 * j + 2]);
          if (d < md) md = d;
        }
        if (md > pickD) { pickD = md; pick = k; }
      }
      isCont[pick] = 1;
      bias[pick] = rng.range(0.55, 1);
      prank[pick] += 1;                                          // continental crust overrides oceanic crust
    }
    // fossil plates: their edges are the old sutures; each edge has its own strength (half carry nothing)
    const K2 = rng.int(P.fossilMin, P.fossilMax);
    const fseed = new Float64Array(3 * K2), fstr = new Float64Array(K2 * K2);
    for (let i = 0; i < K2; i++) {
      let best = -1;
      for (let t = 0; t < (i === 0 ? 1 : 3); t++) {
        unitVec(rng, cand, 0);
        let md = 4;
        for (let j = 0; j < i; j++) {
          const d = 2 - 2 * (cand[0] * fseed[3 * j] + cand[1] * fseed[3 * j + 1] + cand[2] * fseed[3 * j + 2]);
          if (d < md) md = d;
        }
        if (md > best) { best = md; fseed[3 * i] = cand[0]; fseed[3 * i + 1] = cand[1]; fseed[3 * i + 2] = cand[2]; }
      }
    }
    for (let i = 0; i < K2; i++) {
      for (let j = i + 1; j < K2; j++) {
        const v = (rng.f() - P.oldCut) / (1 - P.oldCut);
        fstr[i * K2 + j] = fstr[j * K2 + i] = v > 0 ? 0.45 + 0.55 * v : 0;
      }
    }
    const o = new Float64Array(27);
    for (let i = 0; i < 27; i++) o[i] = rng.range(-400, 400);
    return { K, seed, rot, wt, bias, prank, dist: new Float64Array(K), K2, fseed, fstr, o };
  }

  // Step 1 + 2 for the first n cells: the continent field G.cont and, when `full`, the plate-edge envelopes
  // (belt, wide, arc, trench, riftW, riftN, the across-belt vector an*) and the old ranges.
  function contField(pos, n, PL, G, full, fmax) {
    const P = TUNE.terrain, U = DM.U;
    const K = PL.K, seed = PL.seed, rot = PL.rot, wt = PL.wt, bias = PL.bias, prank = PL.prank, dist = PL.dist, o = PL.o;
    const K2 = PL.K2, fseed = PL.fseed, fstr = PL.fstr;
    const cont = G.cont, belt = G.belt, wide = G.wide, arc = G.arc, trench = G.trench, riftW = G.riftW, riftN = G.riftN;
    const anx = G.anx, any = G.any, anz = G.anz, old = G.old;
    const wf = P.warpF, wf2 = wf * 2.07;
    const edgeMax = full ? Math.max(P.wideW, P.biasW, P.beltC + P.beltW, P.arcC + P.arcW) * 1.02 : P.biasW;
    const contNorm = 1 / 0.2;                                    // fBm of 5 octaves has a standard deviation near 0.2

    for (let c = 0; c < n; c++) {
      const x = pos[3 * c], y = pos[3 * c + 1], z = pos[3 * c + 2];
      // domain warp (two octaves of vector noise)
      const wx = noise3(x * wf + o[0], y * wf + o[1], z * wf + o[2]) + 0.5 * noise3(x * wf2 + o[3], y * wf2 + o[4], z * wf2 + o[5]);
      const wy = noise3(x * wf + o[6], y * wf + o[7], z * wf + o[8]) + 0.5 * noise3(x * wf2 + o[9], y * wf2 + o[10], z * wf2 + o[11]);
      const wz = noise3(x * wf + o[12], y * wf + o[13], z * wf + o[14]) + 0.5 * noise3(x * wf2 + o[15], y * wf2 + o[16], z * wf2 + o[17]);

      // nearest plates in the warped frame
      let qx = x + P.warpPlate * wx, qy = y + P.warpPlate * wy, qz = z + P.warpPlate * wz;
      const ql = 1 / Math.sqrt(qx * qx + qy * qy + qz * qz);
      qx *= ql; qy *= ql; qz *= ql;
      let d1 = 9, d2 = 9, i1 = 0, i2 = 0;
      for (let k = 0; k < K; k++) {
        const dot = qx * seed[3 * k] + qy * seed[3 * k + 1] + qz * seed[3 * k + 2];
        const d = Math.sqrt(dot < 1 ? 2 - 2 * dot : 0) - wt[k];
        dist[k] = d;
        if (d < d1) { d2 = d1; i2 = i1; d1 = d; i1 = k; } else if (d < d2) { d2 = d; i2 = k; }
      }

      // crust bias blended across edges; edge envelopes from every plate near enough
      let bsum = bias[i1], bw = 1;
      let eBelt = 0, eWide = 0, eArc = 0, eTrench = 0, eRiftW = 0, eRiftN = 0;
      let ax = 0, ay = 0, az = 0;
      const s1x = seed[3 * i1], s1y = seed[3 * i1 + 1], s1z = seed[3 * i1 + 2];
      for (let k = 0; k < K; k++) {
        if (k === i1) continue;
        const e = 0.5 * (dist[k] - d1);                           // distance to the edge with plate k
        if (e >= edgeMax) continue;
        if (e < P.biasW) { const t = 1 - e / P.biasW, w = t * t; bsum += w * bias[k]; bw += w; }
        if (!full) continue;
        // unit tangent pointing at plate k, and the relative velocity of plate i1 along it
        const dx = seed[3 * k] - s1x, dy = seed[3 * k + 1] - s1y, dz = seed[3 * k + 2] - s1z;
        const dp = dx * x + dy * y + dz * z;
        let nx = dx - dp * x, ny = dy - dp * y, nz = dz - dp * z;
        const nl = Math.sqrt(nx * nx + ny * ny + nz * nz);
        if (nl < 1e-6) continue;
        nx /= nl; ny /= nl; nz /= nl;
        const rx = rot[3 * i1] - rot[3 * k], ry = rot[3 * i1 + 1] - rot[3 * k + 1], rz = rot[3 * i1 + 2] - rot[3 * k + 2];
        const conv = (ry * z - rz * y) * nx + (rz * x - rx * z) * ny + (rx * y - ry * x) * nz;
        if (conv > 0) {
          let st = (conv - P.convMin) / P.convSpan;
          if (st <= 0) continue;
          if (st > 1) st = 1;
          const over = prank[i1] > prank[k];
          eBelt += st * bump(e, P.beltC, P.beltW) * (over ? 1 : P.beltUnder);
          eWide += st * bump(e, 0, P.wideW) * (over ? 1 : 0.5);
          if (over) eArc += st * bump(e, P.arcC, P.arcW); else eTrench += st * bump(e, P.trenchC, P.trenchW);
        } else {
          let st = (-conv - P.convMin) / P.convSpan;
          if (st <= 0) continue;
          if (st > 1) st = 1;
          eRiftW += st * bump(e, 0, P.riftW);
          eRiftN += st * bump(e, 0, P.riftN);
        }
        if (k === i2) { ax = -e * nx; ay = -e * ny; az = -e * nz; }   // across-belt coordinate (continuous over the edge)
      }

      // continent field
      const base = fbmB(x + P.warpCont * wx, y + P.warpCont * wy, z + P.warpCont * wz, P.contF, P.contOct, 2.03, 0.5, fmax,
        o[18], o[19], o[20]) * contNorm;
      const rag = U.sat(0.5 + 1.6 * noise3(x * 1.7 + o[21], y * 1.7 + o[22], z * 1.7 + o[23]));
      const det = fbmB(x, y, z, P.coastF, 3, 2.1, 0.55, fmax, o[24], o[25], o[26]);
      cont[c] = base + P.plateAmp * (bsum / bw) + det * (P.coastLo + (P.coastHi - P.coastLo) * rag * rag) * contNorm * 0.5;
      if (!full) continue;

      belt[c] = eBelt > 1 ? 1 : eBelt; wide[c] = eWide > 1 ? 1 : eWide;
      arc[c] = eArc > 1 ? 1 : eArc; trench[c] = eTrench > 1 ? 1 : eTrench;
      riftW[c] = eRiftW > 1 ? 1 : eRiftW; riftN[c] = eRiftN > 1 ? 1 : eRiftN;
      anx[c] = ax; any[c] = ay; anz[c] = az;

      // old sutures: the edges of the fossil plates (their own, stronger warp)
      qx = x + P.warpFossil * wy; qy = y + P.warpFossil * wz; qz = z + P.warpFossil * wx;
      const fl = 1 / Math.sqrt(qx * qx + qy * qy + qz * qz);
      qx *= fl; qy *= fl; qz *= fl;
      let f1 = 9, f2 = 9, j1 = 0, j2 = 0;
      for (let k = 0; k < K2; k++) {
        const dot = qx * fseed[3 * k] + qy * fseed[3 * k + 1] + qz * fseed[3 * k + 2];
        const d = dot < 1 ? 2 - 2 * dot : 0;                      // squared chord: enough to order them
        if (d < f1) { f2 = f1; j2 = j1; f1 = d; j1 = k; } else if (d < f2) { f2 = d; j2 = k; }
      }
      const fe = 0.5 * (Math.sqrt(f2) - Math.sqrt(f1));
      old[c] = fe < P.oldW ? bump(fe, 0, P.oldW) * fstr[j1 * K2 + j2] : 0;
    }
  }

  // How good is a plate layout? Judged on the level-4 cells: at least two real continents, 3–6 major landmasses,
  // no supercontinent. 5.5 = full marks.
  function scoreLayout(pos, PL, S4, fmax) {
    const P = TUNE.terrain, n4 = S4.n, cont = S4.cont;
    contField(pos, n4, PL, S4, false, fmax);
    // judged with a little extra land, so that landmasses count as separate only when the strait between them
    // survives the relief that is still to come
    const nLand = Math.min(n4 - 1, Math.round((P.land + P.scoreExtra) * n4));
    const thr = shoreOf(cont, S4.sorted, n4, nLand);
    const sizes = components(S4.nbr, n4, cont, thr, S4.lab, S4.queue);
    let major = 0;
    for (let i = 0; i < sizes.length; i++) if (sizes[i] >= 0.02 * n4) major++;
    const s0 = sizes.length > 0 ? sizes[0] / n4 : 0, s1 = sizes.length > 1 ? sizes[1] / n4 : 0, s2 = sizes.length > 2 ? sizes[2] / n4 : 0;
    const top = s0 * n4 / nLand;
    let score = 0;
    if (s1 >= 0.045) score += 2; else if (s1 >= 0.035) score += 1;
    if (major >= 3 && major <= 6) score += 2; else if (major > 6) score += 1;
    if (top <= 0.5) score += 1; else if (top <= 0.62) score += 0.5;
    if (s2 >= 0.03) score += 0.5;
    return score;
  }

  // Rank map: G.u in (0, 1] on land (1 = the most continental cell), [−1, 0) at sea.
  function rankMap(G, n) {
    const cont = G.cont, sorted = G.sorted, u = G.u;
    sorted.set(cont);
    sorted.sort();
    const nLand = landCount(n), ks = n - nLand;                   // ks = index of the first land value
    for (let c = 0; c < n; c++) {
      const r = rankOf(sorted, n, cont[c]);
      let v = r >= ks ? (r - ks + 0.5) / nLand : (r - ks + 0.5) / ks;
      if (v > 1) v = 1; else if (v < -1) v = -1;
      u[c] = v;
    }
  }

  // ------------------------------------------------------------------ generation: relief

  function relief(world, rng, G, E, fmax) {
    const P = TUNE.terrain, U = DM.U;
    const geo = world.geo, n = world.n, pos = geo.pos;
    const u = G.u, belt = G.belt, wide = G.wide, arc = G.arc, trench = G.trench, riftW = G.riftW, riftN = G.riftN;
    const anx = G.anx, any = G.any, anz = G.anz, old = G.old, volc = G.volc;
    const p = new Float64Array(39);
    for (let i = 0; i < 39; i++) p[i] = rng.range(-400, 400);
    const rs = P.ridgeStretch, tf = P.tabF, tf2 = tf * 2.3, sf = P.strikeF;

    for (let c = 0; c < n; c++) {
      const x = pos[3 * c], y = pos[3 * c + 1], z = pos[3 * c + 2];
      const uu = u[c];
      const landness = U.sstep(-0.11, 0.07, uu);                  // 1 on land, fading over the inner shelf
      const b = belt[c], od = old[c];

      // shared noises
      const ridge = ridgeB(x + anx[c] * rs, y + any[c] * rs, z + anz[c] * rs, P.ridgeF, 3, 2.1, 0.55, 2 * fmax, p[0], p[1], p[2]);
      const hill = fbmB(x, y, z, P.hillF, 3, 2.1, 0.5, fmax, p[3], p[4], p[5]);
      const basin = 0.5 + 0.5 * noise3(x * 1.6 + p[6], y * 1.6 + p[7], z * 1.6 + p[8]);

      let e;
      if (uu >= 0) {
        // land base: broad lowlands near the coast; interiors that swell into uplands here and sag into basins there
        const swell = fbmB(x, y, z, P.swellF, 2, 2.2, 0.5, fmax, p[9], p[10], p[11]);
        e = 0.03 + P.lowSlope * uu + P.upland * uu * uu * (0.3 + 1.4 * basin * basin) + P.swellH * uu * swell;
        const floor = 0.03 + 0.3 * P.lowSlope * uu;
        if (e < floor) e = floor;
        // tablelands: flat-topped uplands with a crisp escarpment
        const tn = noise3(x * tf + p[12], y * tf + p[13], z * tf + p[14]) + 0.4 * noise3(x * tf2 + p[15], y * tf2 + p[16], z * tf2 + p[17]);
        if (tn > P.tabCut) e += P.tabH * U.sstep(P.tabCut, P.tabCut + P.tabEdge, tn) * U.sstep(0.1, 0.3, uu) * (0.6 + 0.6 * basin);
      } else {
        // sea base: shelf, continental slope, abyssal plain. Shelves are narrow at subduction edges.
        const shelfN = 0.5 + 0.5 * noise3(x * 2.2 + p[18], y * 2.2 + p[19], z * 2.2 + p[20]);
        let v = -uu * (0.55 + 0.9 * shelfN + 2.2 * (trench[c] + 0.6 * b));
        if (v > 1) v = 1;
        if (v < P.shelfV) {
          const t = v / P.shelfV;
          e = -0.03 - P.shelfDepth * t * Math.sqrt(t);
        } else if (v < P.slopeV) {
          e = -0.03 - P.shelfDepth - (P.slopeDepth - P.shelfDepth) * U.sstep(P.shelfV, P.slopeV, v);
        } else {
          const t = (v - P.slopeV) / (1 - P.slopeV);
          const ab = fbmB(x, y, z, 3.1, 3, 2.1, 0.5, fmax, p[21], p[22], p[23]);
          e = -0.03 - P.slopeDepth - P.abyssDepth * Math.sqrt(t) * (0.2 + 0.8 * basin) + ab * 1.1 * U.sat(t * 4);
        }
      }

      // mountains (on land, and running out to sea as peninsulas and coastal islands)
      if (landness > 0) {
        // massifs and saddles along the strike of a belt
        const strike = U.sat(0.62 + 0.8 * noise3(x * sf + p[24], y * sf + p[25], z * sf + p[26]));
        const mtn = P.beltH * b * (0.4 + 0.6 * strike) * (0.4 + 0.8 * ridge) + P.plateauH * wide[c] * (0.6 + 0.4 * basin)
          + P.oldH * od * (0.35 + 0.65 * strike) * (0.45 + 0.75 * ridge);
        e += mtn * landness;
        // rift valleys on land
        e -= P.riftH * riftN[c] * landness * (1 - b);
      }
      // hills: quiet on the plains, rougher with altitude and inside the belts
      let rough = P.hillLow + P.hillHigh * U.sat((e - 0.25) / 1.6) + P.hillBelt * Math.min(1, b + od);
      if (uu > 0) rough += P.hillRegion * U.sstep(0.05, 0.5, noise3(x * 1.9 + p[30], y * 1.9 + p[31], z * 1.9 + p[32])) * U.sat(uu * 6);   // hill country
      e += hill * rough * (0.25 + 0.75 * landness) * 1.6;

      // the deep sea: arcs, trenches, spreading ridges
      const deep = 1 - landness;
      if (deep > 0) {
        const off = U.sstep(0.12, 0.35, -uu);                     // 0 on the shelf, 1 in open ocean
        const bead = U.sat(0.25 + 1.5 * fbmB(x, y, z, 13, 1, 2, 0.5, fmax, p[27], p[28], p[29]));
        const a = arc[c];
        e += deep * off * (P.arcSwell * a + P.arcH * a * a * bead * (0.55 + 0.6 * ridge));
        e -= deep * off * P.trenchH * trench[c];
        e += deep * off * (P.ridgeH * riftW[c] * (0.7 + 0.6 * ridge) - 0.45 * riftN[c]);
        volc[c] = a * bead * off;
      } else volc[c] = 0;
      volc[c] += 0.5 * b;
      E[c] = e;
    }
  }

  // Hotspot chains: a line of volcanoes of decreasing age across the deep ocean; the young ones are islands.
  function hotspots(world, rng, G, E) {
    const P = TUNE.terrain, geo = world.geo, pos = geo.pos, step = geo.step, volc = G.volc;
    const chains = rng.int(P.chainsMin, P.chainsMax);
    const v = new Float64Array(6), list = G.queue;
    for (let ch = 0; ch < chains; ch++) {
      // start over deep water
      let c0 = -1;
      for (let t = 0; t < 30 && c0 < 0; t++) {
        unitVec(rng, v, 0);
        const c = geo.cellAt(v[0], v[1], v[2]);
        if (E[c] < -3) c0 = c;
      }
      const len = rng.int(4, 7), gap = rng.range(0.07, 0.105), top = rng.range(0.9, 2.4);
      unitVec(rng, v, 3);
      if (c0 < 0) continue;
      let px = pos[3 * c0], py = pos[3 * c0 + 1], pz = pos[3 * c0 + 2];
      // tangent heading
      let dp = v[3] * px + v[4] * py + v[5] * pz;
      let tx = v[3] - dp * px, ty = v[4] - dp * py, tz = v[5] - dp * pz;
      let tl = Math.sqrt(tx * tx + ty * ty + tz * tz);
      if (tl < 1e-6) continue;
      tx /= tl; ty /= tl; tz /= tl;
      for (let i = 0; i < len; i++) {
        const c = geo.cellAt(px, py, pz);
        const r = Math.max(rng.range(0.034, 0.056), 1.05 * step);
        const summit = top - i * rng.range(0.45, 0.9) + rng.range(-0.25, 0.25);
        if (E[c] < -1.6) {
          const h = summit - E[c];
          const cnt = geo.ring(c, Math.ceil(3.2 * r / step) + 1, list);
          for (let k = 0; k < cnt; k++) {
            const m = list[k];
            const d = Math.sqrt(angle2(pos, c, m));
            if (d < r) { const q = 1 - d / r; E[m] += h * q * Math.sqrt(q); volc[m] += q; }
            if (d < 3 * r) { const q = 1 - d / (3 * r); E[m] += 0.9 * q * q; }
          }
        }
        // step along the great circle (curving a little)
        const g = gap * rng.range(0.8, 1.25);
        px += tx * g; py += ty * g; pz += tz * g;
        const pl = 1 / Math.sqrt(px * px + py * py + pz * pz);
        px *= pl; py *= pl; pz *= pl;
        const bend = rng.range(-0.25, 0.25);
        const sxx = py * tz - pz * ty, sy = pz * tx - px * tz, sz = px * ty - py * tx;   // sideways
        tx += bend * sxx; ty += bend * sy; tz += bend * sz;
        dp = tx * px + ty * py + tz * pz;
        tx -= dp * px; ty -= dp * py; tz -= dp * pz;
        tl = Math.sqrt(tx * tx + ty * ty + tz * tz);
        if (tl < 1e-6) break;
        tx /= tl; ty /= tl; tz /= tl;
      }
    }
  }

  // ------------------------------------------------------------------ generation: tidying and erosion

  // Step 4. Enclosed seas smaller than minSea become land basins just above the shore (hollows that Hydro turns
  // into lakes where it rains), and some one-cell islands sink into shoals. Returns the net number of cells that
  // turn into land; with apply = false nothing is written.
  function tidy(world, G, E, shore, apply) {
    const P = TUNE.terrain, U = DM.U, n = world.n, nbr = world.geo.nbr, lab = G.lab, q = G.order, seed = world.seed | 0;
    const minSea = Math.max(2, Math.round(P.minSea * n));
    let net = 0;
    // one-cell islands (decided before anything is written)
    const specks = G.queue;
    let nSpeck = 0;
    for (let c = 0; c < n; c++) {
      if (E[c] < shore) continue;
      const b = 6 * c;
      let alone = true;
      for (let j = 0; j < 6; j++) {
        const m = nbr[b + j];
        if (m < 0) break;
        if (E[m] >= shore) { alone = false; break; }
      }
      if (alone && U.hashf(c, seed) >= P.speckKeep) specks[nSpeck++] = c;
    }
    // sea components, each a contiguous run of q; the largest is the ocean
    lab.fill(0);
    let end = 0, nComp = 0, big = 0;
    const starts = G.par;                                         // start of each component in q
    for (let c0 = 0; c0 < n; c0++) {
      if (lab[c0] || E[c0] >= shore) continue;
      const start = end;
      let head = end;
      q[end++] = c0; lab[c0] = 1;
      while (head < end) {
        const b = 6 * q[head++];
        for (let j = 0; j < 6; j++) {
          const m = nbr[b + j];
          if (m < 0) break;
          if (!lab[m] && E[m] < shore) { lab[m] = 1; q[end++] = m; }
        }
      }
      starts[nComp++] = start;
      if (end - start > big) big = end - start;
    }
    for (let k = 0; k < nComp; k++) {
      const a = starts[k], z = k + 1 < nComp ? starts[k + 1] : end, size = z - a;
      if (size >= minSea || size === big) continue;
      net += size;
      if (apply) for (let i = a; i < z; i++) { const c = q[i]; E[c] = shore + 0.04 + 0.03 * U.hashf(c, seed + 7); }
    }
    net -= nSpeck;
    if (apply) for (let i = 0; i < nSpeck; i++) { const c = specks[i]; E[c] = shore - 0.12 - 0.1 * U.hashf(c, seed + 11); }
    return net;
  }

  // Shelves: near land the sea floor may only be so deep per hop from the shore (deeper at an active margin, where
  // a trench or a mountain belt runs along the coast), so coasts shelve into shallows before the continental slope.
  function shelve(world, G, E, shore, o) {
    const P = TUNE.terrain, n = world.n, nbr = world.geo.nbr, pos = world.geo.pos, q = G.order, hop = G.lab, trench = G.trench, belt = G.belt;
    const lim = P.shelfLim, maxH = lim.length;
    hop.fill(0);
    let end = 0;
    for (let c = 0; c < n; c++) {
      if (E[c] >= shore) continue;
      const b = 6 * c;
      for (let j = 0; j < 6; j++) {
        const m = nbr[b + j];
        if (m < 0) break;
        if (E[m] >= shore) { hop[c] = 1; q[end++] = c; break; }
      }
    }
    for (let head = 0; head < end; head++) {
      const c = q[head], h = hop[c];
      let act = trench[c] + 0.6 * belt[c];
      if (act > 1) act = 1;
      const wide = 0.7 + 0.3 * noise3(pos[3 * c] * 2.2 + o[21], pos[3 * c + 1] * 2.2 + o[22], pos[3 * c + 2] * 2.2 + o[23]);   // 0.4 … 1
      const floor = shore - lim[h - 1] * (1 + P.shelfActive * act) / wide;
      if (E[c] < floor) E[c] = floor;
      if (h >= maxH) continue;
      const b = 6 * c;
      for (let j = 0; j < 6; j++) {
        const m = nbr[b + j];
        if (m < 0) break;
        if (!hop[m] && E[m] < shore) { hop[m] = h + 1; q[end++] = m; }
      }
    }
  }

  // binary min-heap of (key, cell) in two parallel arrays
  function heapPush(hk, hc, size, key, cell) {
    let i = size;
    while (i > 0) {
      const pi = (i - 1) >> 1;
      if (hk[pi] <= key) break;
      hk[i] = hk[pi]; hc[i] = hc[pi];
      i = pi;
    }
    hk[i] = key; hc[i] = cell;
    return size + 1;
  }
  function heapPop(hk, hc, size) {                // removes the root; the caller has read hk[0], hc[0]
    size--;
    if (size <= 0) return 0;
    const key = hk[size], cell = hc[size];
    let i = 0;
    for (;;) {
      let ch = 2 * i + 1;
      if (ch >= size) break;
      if (ch + 1 < size && hk[ch + 1] < hk[ch]) ch++;
      if (hk[ch] >= key) break;
      hk[i] = hk[ch]; hc[i] = hc[ch];
      i = ch;
    }
    hk[i] = key; hc[i] = cell;
    return size;
  }

  // Step 5. Drainage by priority flood from the sea, then valleys cut along it. Fills G.acc (catchment size in
  // cells, 0 at sea) for the fertility of the flood plains.
  function erode(world, G, E, shore) {
    const P = TUNE.terrain, n = world.n, geo = world.geo, nbr = geo.nbr, step = geo.step;
    const hk = G.hk, hc = G.hc, par = G.par, order = G.order, acc = G.acc, state = G.lab, val = G.val;
    let size = 0, cnt = 0;
    // the flood starts from every land cell that touches the sea
    for (let c = 0; c < n; c++) {
      acc[c] = 0;
      if (E[c] < shore) { state[c] = 2; continue; }
      state[c] = 0;
      const b = 6 * c;
      for (let j = 0; j < 6; j++) {
        const m = nbr[b + j];
        if (m < 0) break;
        if (E[m] < shore) { state[c] = 1; par[c] = -1; size = heapPush(hk, hc, size, E[c], c); break; }
      }
    }
    while (size > 0) {
      const lvl = hk[0], c = hc[0];
      size = heapPop(hk, hc, size);
      order[cnt++] = c;
      const b = 6 * c;
      for (let j = 0; j < 6; j++) {
        const m = nbr[b + j];
        if (m < 0) break;
        if (state[m] !== 0) continue;
        state[m] = 1; par[m] = c;
        size = heapPush(hk, hc, size, E[m] > lvl ? E[m] : lvl, m);
      }
    }
    // catchments: upstream first
    for (let i = 0; i < cnt; i++) acc[order[i]] = 1;
    for (let i = cnt - 1; i >= 0; i--) { const c = order[i], p = par[c]; if (p >= 0) acc[p] += acc[c]; }

    // Implicit stream power, downstream first: a cell relaxes toward its (already final) downstream neighbour by
    // F / (1 + F), where F, the headward reach in cells, grows with the square root of the catchment. Hollows
    // (cells below their way out) are left alone.
    const kF = P.erodeK / step, aMin = P.erodeMin * n, aRef = P.erodeRef * n, side = P.erodeSide;
    const talus = P.talus * step * DM.R, floor = shore + 0.02;
    for (let i = 0; i < cnt; i++) {
      const c = order[i], p = par[c];
      const base = p >= 0 ? E[p] : shore;
      const e0 = E[c];
      // reach: the cell's own river, or a share of the river it drains into (valley sides and flood plains)
      let F = 0;
      const a = acc[c];
      if (a > aMin) {
        F = kF * Math.sqrt((a - aMin) / aRef);
        if (F > P.erodeMax) F = P.erodeMax;
      }
      if (p >= 0 && side * val[p] > F) F = side * val[p];
      val[c] = F;
      if (e0 <= base) continue;
      let e = F > 0 ? (e0 + F * base) / (1 + F) : e0;
      if (e > base + talus) e = base + talus;
      if (e < floor) e = e0 < floor ? e0 : floor;
      E[c] = e;
    }

    // plains: smooth the lowlands (sea neighbours count as the shore, so coasts shelve gently)
    const top = P.smoothTop;
    for (let i = 0; i < cnt; i++) {
      const c = order[i], h = E[c] - shore;
      if (h >= top) { val[c] = E[c]; continue; }
      const b = 6 * c;
      let sum = 0, k = 0;
      for (let j = 0; j < 6; j++) {
        const m = nbr[b + j];
        if (m < 0) break;
        sum += E[m] < shore ? shore : E[m];
        k++;
      }
      val[c] = E[c] + (sum / k - E[c]) * P.smoothK * (1 - h / top);
    }
    for (let i = 0; i < cnt; i++) { const c = order[i]; E[c] = val[c]; }
  }

  // Step 6: exact shoreline, then monotone remaps that guarantee the documented statistics. The generator comes
  // close to them by itself; the remaps only finish the job, and they stretch nothing but the mountains
  // (land above the midQ quantile), so plains and hills keep the slopes erosion gave them.
  function normalise(world, rng, G, E) {
    const P = TUNE.terrain, n = world.n, sorted = G.sorted;
    const nLand = landCount(n);
    const shore = shoreOf(E, sorted, n, nLand);                    // the lowest land value → 0
    const ks = n - nLand;
    const qv = function (q) { return sorted[ks + Math.min(nLand - 1, Math.floor(q * nLand))] - shore; };
    // land quantiles (values relative to the shore)
    const a = qv(P.lowQ), m = qv(P.midQ), b = qv(P.highQ), top = sorted[n - 1] - shore;
    const peak = rng.range(P.peakMin, P.peakMax);
    const deep = rng.range(P.deepMin, P.deepMax);
    const drop = a > P.lowE ? a - P.lowE : 0;                      // lowlands: squeeze [0, a] and lower the rest
    const m2 = m - drop;
    let b2 = b - drop;
    if (b2 < P.highE) b2 = P.highE;
    if (b2 > peak - 1.2) b2 = peak - 1.2;
    if (b2 < m2) b2 = m2;
    const deepest = shore - sorted[0];
    const knee = P.deepKnee;                                       // depths beyond this are rescaled to reach `deep`
    for (let c = 0; c < n; c++) {
      let e = E[c] - shore;
      if (e >= 0) {
        if (e <= a) { if (drop > 0) e = e * P.lowE / a; }
        else if (e <= m) e -= drop;
        else if (e <= b) e = b > m ? m2 + (e - m) * (b2 - m2) / (b - m) : m2;
        else e = top > b ? b2 + (e - b) * (peak - b2) / (top - b) : b2;
      } else if (-e > knee && deepest > knee) {
        const t = (-e - knee) / (deepest - knee);                    // 0 at the knee … 1 at the deepest point
        e = -knee - (deep - knee) * t * (2 - t);                     // concave: the deep basins stay deep
      }
      E[c] = e;
    }
  }

  // ------------------------------------------------------------------ generation: entry point

  // Steps 1–6 for one plate layout. Leaves the finished heights in G.cont (returned), with G.volc and G.acc.
  function build(world, PL, G, root, fmax) {
    const n = world.n;
    contField(world.geo.pos, n, PL, G, true, fmax);
    rankMap(G, n);
    const E = G.cont;                                              // reuse: the raw field is no longer needed
    const rng = root.fork('terrain-relief');
    relief(world, rng, G, E, fmax);
    hotspots(world, rng, G, E);
    const nLand = landCount(n);
    let shore = shoreOf(E, G.sorted, n, nLand);
    const net = tidy(world, G, E, shore, false);
    if (net !== 0) shore = shoreOf(E, G.sorted, n, nLand - net);   // the basins about to be filled leave room for less land elsewhere
    tidy(world, G, E, shore, true);
    shelve(world, G, E, shore, PL.o);
    erode(world, G, E, shore);
    normalise(world, rng, G, E);
    return E;
  }

  // Did the finished world keep its continents apart? 3 = yes: a second landmass of at least rateSecond of the
  // planet, a third of at least rateThird, and no landmass holding more than rateTop of the land.
  function rate(world, G, E) {
    const P = TUNE.terrain, n = world.n, sizes = components(world.geo.nbr, n, E, 0, G.lab, G.queue);
    const s1 = sizes.length > 1 ? sizes[1] / (P.rateSecond * n) : 0, s2 = sizes.length > 2 ? sizes[2] / (P.rateThird * n) : 0;
    const top = sizes.length > 0 ? sizes[0] / landCount(n) : 1;
    const t = top <= P.rateTop ? 1 : (1 - top) / (1 - P.rateTop);
    return (s1 > 1 ? 1 : s1) + (s2 > 1 ? 1 : s2) + (t > 0 ? t : 0);
  }

  Terrain.generate = function (world) {
    const P = TUNE.terrain, U = DM.U, n = world.n, geo = world.geo, pos = geo.pos;
    noise3 = U.noise3;
    const f32 = function () { return new Float32Array(n); };
    const G = {
      cont: f32(), u: f32(), belt: f32(), wide: f32(), arc: f32(), trench: f32(), riftW: f32(), riftN: f32(),
      anx: f32(), any: f32(), anz: f32(), old: f32(), volc: f32(), sorted: f32(), acc: f32(), val: f32(), hk: f32(),
      hc: new Int32Array(n), par: new Int32Array(n), order: new Int32Array(n), queue: new Int32Array(n),
      lab: new Uint8Array(n),
    };
    const fmax = 1 / (P.cellsPerWave * geo.step);
    const root = U.rng(world.seed >>> 0);

    // Layout: score candidates on the level-4 cells (the first cells of every finer level, and band-limited for
    // that level, so a seed has the same layout at every level), best first.
    const geo4 = world.L > 4 ? DM.Geo.get(4) : geo, n4 = geo4.n;
    const fmax4 = 1 / (P.cellsPerWave * geo4.step);
    const S4 = { n: n4, nbr: geo4.nbr, cont: new Float32Array(n4), sorted: new Float32Array(n4), lab: new Uint8Array(n4), queue: new Int32Array(n4) };
    // Candidates are scored in order until one gets full marks; the pool is kept best first.
    const pool = [];
    const tries = Math.max(1, P.candidates | 0);
    let tNext = 0;
    const scoreMore = function () {
      while (tNext < tries) {
        const cand = makePlates(root.fork('terrain-layout-' + tNext));
        const sc = scoreLayout(pos, cand, S4, fmax4);
        pool.push({ PL: cand, sc, t: tNext++ });
        if (sc >= 5.5) break;
      }
      pool.sort(function (a, b) { return (b.sc - a.sc) || (a.t - b.t); });
    };

    // Build the best layout. Relief can still bridge a strait the scoring thought safe (a mountain belt between two
    // continents): then build the next best layout, scoring further candidates first, and keep whichever finished
    // world rates best.
    let PL = null, bestRate = -1, keep = null, inG = false;       // inG: the best build is the one G holds
    const builds = 1 + Math.max(0, P.rebuilds | 0);
    for (let k = 0; k < builds; k++) {
      if (!pool.length || pool[0].sc < 5.5) scoreMore();           // look for a layout with full marks before settling for less
      if (!pool.length) break;                                     // every candidate has been built
      if (inG) {                                                   // the next build overwrites G: copy the best first
        keep = { E: Float32Array.from(G.cont), volc: Float32Array.from(G.volc), acc: Float32Array.from(G.acc) };
        inG = false;
      }
      const cand = pool.shift();
      const r = rate(world, G, build(world, cand.PL, G, root, fmax));
      if (r > bestRate) {
        bestRate = r; PL = cand.PL; inG = true; keep = null;
        if (r >= 3) break;
      }
    }
    let E = G.cont;
    if (!inG) { E = keep.E; G.volc = keep.volc; G.acc = keep.acc; }

    // write elevation and an initial fertility: regional variety, rich lowlands, flood plains and volcanic soils,
    // thin mountain soils
    const elev = world.elev, fert = world.fert, o = PL.o, volc = G.volc, acc = G.acc;
    const lo = DM.State.ELEV_MIN, hi = DM.State.ELEV_MAX, accRef = 0.004 * n;
    for (let c = 0; c < n; c++) {
      let e = E[c];
      if (!(e === e)) e = 0;
      elev[c] = e < lo ? lo : e > hi ? hi : e;
      const x = pos[3 * c], y = pos[3 * c + 1], z = pos[3 * c + 2];
      let f = 0.48 + 0.62 * fbmB(x, y, z, 2.7, 3, 2.1, 0.5, fmax, o[9], o[4], o[14]);
      if (e >= 0) {
        f += 0.12 * (1 - U.sat(e / 1.2)) - 0.3 * U.sat((e - 2.2) / 3);
        if (e < 1.5) f += 0.2 * U.sat(Math.sqrt(acc[c] / accRef)) * (1 - e / 1.5);
      }
      f += 0.18 * (volc[c] > 1 ? 1 : volc[c]);
      fert[c] = f < 0.15 ? 0.15 : f > 1 ? 1 : f;
    }

    const d = world.dirty, nd = world.need;
    for (const k in d) d[k] = true;
    for (const k in nd) nd[k] = true;
  };
})(globalThis.DM = globalThis.DM || {});
