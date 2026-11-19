// DEMIURGE — js/sim/civ.js : DM.Civ, the lifecycle half (SPEC-SIM §3.7). economy.js extends the same
// namespace with Civ.yearly and Civ.capacity; this file defines neither and works without them.
//
//   Civ.TIERS  = ['camp', 'village', 'town', 'city', 'metropolis']
//   Civ.CAUSES   the causes a settlement:destroyed event may carry (any other value is reported as 'abandoned')
//   Civ.found(world, cell, cultureId, pop, by = 'settlers', opts = null) → settlement | null
//         opts = { devotion, love } of the origin. null: bad cell, site not settleable (Civ.canSettle), unknown
//         or dead culture, pop not a positive number, or TUNE.civ.maxSettlements live settlements already.
//         Names the place in the culture's language (DM.Names.place; a plain syllable name without names.js),
//         claims the territory, computes K (Civ.capacity when economy.js is loaded, else an estimate), makes it
//         the capital of a culture that has none, emits settlement:founded.
//   Civ.claim(world, s) → n            recompute the territory of s (owner[], s._cells, s._n); n = cells owned
//   Civ.rebuild(world)                 rebuild every _cells/_n from owner[] without touching owner (after a load)
//   Civ.destroy(world, s, cause) → bool      Civ.damage(world, s, frac, cause) → people killed
//   Civ.capture(world, s, cultureId) → bool  Civ.setCulture(world, s, cultureId) → bool
//   Civ.electCapital(world, cultureId) → settlement id | -1
//   Civ.infect(world, s, severity = 0.6) → bool      Civ.cure(world, s, immuneYears = 30) → bool
//   Civ.at(world, cell) → settlement | null   Civ.near(world, cell, ang) → [settlement]   Civ.live(world) → [settlement]
//   Civ.canSettle(world, c) → { ok, reason }  Civ.siteScore(world, c) → number   Civ.bestSite(world) → cell | -1
//   Civ.ageProgress(world, cultureId) → 0..1
// Wherever a settlement `s` is expected, the entity or its id is accepted; anything else (null, a dead one
// where a live one is needed, an object that is not this world's entity) gives false / 0 / null, never a throw.
//
// Rules chosen where the spec leaves room:
//   - Territory. The candidates are the cells within s.radius neighbour hops of the centre (hops may cross
//     water; sea cells themselves are never owned, lake cells can be). A cell another live settlement already
//     owns is left alone. An unowned cell that also lies inside another live settlement's radius goes to the
//     NEARER CENTRE BY TRUE (great-circle) DISTANCE, ties to the lower id, so borders between neighbours are
//     straight bisectors rather than hop-lattice staircases. claim(s) writes only s's own cells: a cell s gives
//     up stays unowned until its new owner next claims.
//   - Capitals are sticky. found gives a culture its first capital; destroy / capture / setCulture re-elect
//     only for a culture whose capital was lost (or that had none). Only Civ.electCapital itself moves a living
//     capital to a more populous town, because moving it restarts the launch site and the Ark.
//   - infect emits settlement:plague { start: true } at the onset only (plague was 0), not on every
//     re-infection of a settlement that is already ill.
//   - siteScore = hab + room (mean habitability within 4 hops) + fresh water + sea nearby − a small penalty
//     for ground barely above the sea. bestSite is its exact maximum over the settleable (canSettle) cells of
//     the landmass with the most habitable cells; if that landmass has no free site, over the whole planet.
//
// Invariants this file keeps (other modules may rely on them):
//   - s._cells[0 … s._n-1] are the territory cells in ASCENDING cell id, after claim and after rebuild alike,
//     and owner[c] === s.id exactly for those cells. Territory never contains a sea cell at the time of a claim.
//   - A culture's `capital` is the id of a live settlement of that culture or -1, and exactly that settlement
//     has `capital === true`. A settlement that stops being a capital, or changes culture, loses its launch
//     site and Ark progress (b.launch = 0, ark = 0).
//   - A dead settlement keeps the fields it had when it fell (`pop` = the people lost with it); only dead,
//     deadYear, cause, capital, b.launch, ark and its territory change. Its `urban` cells stay as ruins.
//   - Civ.live returns the same array object until the set of live settlements changes, and never mutates an
//     array it has handed out (so a caller may destroy settlements while iterating it; check `.dead`).
//   - Nothing here draws from world.rng: names come from a fork of it, which leaves the stream untouched.
// Transient caches: s._cells, s._n, world._civLive.
(function (DM) {
  'use strict';

  const Civ = (DM.Civ = DM.Civ || {});
  const TUNE = (DM.TUNE = DM.TUNE || {});
  TUNE.civ = {
    maxSettlements: 160,       // Civ.found refuses beyond this many live settlements
    crowdHops: 7,              // a site is 'crowded' when live-owned land lies within this many hops
    barrenHab: 0.05,           // a site is 'barren' at or below this habitability
    lakeWater: 0.03,           // a land cell with more standing water than this is a lake cell
    minPeople: 3,              // Civ.damage destroys a settlement left with fewer people
    awakenDevotion: 0.6, awakenLove: 0.3,     // an awakened tribe
    settlerDevotion: 0.5, settlerLove: 0,     // settlers whose origin was not passed in
    radius: [2, 3, 4, 5, 6],   // territory radius in hops by tier (found uses radius[0])
    ageNeed: [420, 500, 600, 750, 620, 520],  // progress-years needed to leave each age (ageProgress)
    campUrban: 0.1,            // built-up fraction of a new camp's centre cell
    // siteScore = hab + siteRoom·room + siteFresh·fresh + siteCoast·coast − siteLow·low
    siteRoom: 0.6,             // room = mean habitability of the cells within siteRoomHops (sea and lakes count 0)
    siteRoomHops: 4,
    siteFresh: 0.12,           // fresh water at ≤ 1 hop (0.6 of it at 2 hops, 0.3 at 3)
    siteCoast: 0.08,           // the sea within 3 hops (half at 4): fishing now, a port later
    siteLow: 0.2,              // penalty for ground barely above the sea (first to drown when the sea rises) …
    siteLowH: 0.3,             // … fading to nothing this far above sea level
    // fallback K estimate, used only while economy.js (Civ.capacity) is absent
    estYield: [1, 2.5, 5, 10, 25, 60, 120],
  };

  Civ.TIERS = ['camp', 'village', 'town', 'city', 'metropolis'];
  Civ.CAUSES = ['drowned', 'meteor', 'lava', 'war', 'famine', 'plague', 'quake', 'nuke', 'abandoned', 'wave', 'lightning'];

  const EMPTY = new Int32Array(0);
  const MAX_HOPS = 32;                                   // hard cap on any hop radius used here
  const MAX_ID = 32767;                                  // owner[] is an Int16Array

  // ------------------------------------------------------------------ small helpers

  function finite(x) { return typeof x === 'number' && x - x === 0; }
  function sat(x) { return x < 0 ? 0 : x > 1 ? 1 : x; }
  function validCell(world, c) { return typeof c === 'number' && c >= 0 && c < world.n && c === Math.floor(c); }
  function usable(world) { return !!world && !!world.geo && !!world.settlements && !!world.cultures; }
  // the world's own culture for an id (or for the culture object itself), else null
  function cultureOf(world, id) {
    if (id && typeof id === 'object') return world.cultures[id.id] === id ? id : null;
    if (typeof id !== 'number' || id < 0 || id !== Math.floor(id)) return null;
    return world.cultures[id] || null;
  }
  // the world's own settlement for an entity (or for its id), else null
  function entity(world, s) {
    if (!usable(world)) return null;
    if (typeof s === 'number') return s >= 0 && s === Math.floor(s) ? (world.settlements[s] || null) : null;
    if (!s || typeof s !== 'object') return null;
    return world.settlements[s.id] === s ? s : null;
  }
  function liveById(world, id) {
    const s = world.settlements[id];
    return s && !s.dead ? s : null;
  }
  function hopsOf(x, def) {
    if (!(x >= 0)) x = def;
    x = Math.floor(x);
    return x > MAX_HOPS ? MAX_HOPS : x;
  }
  function radiusOf(s) { return hopsOf(s.radius, 2); }
  // the culture's capital if it is still a live settlement of that culture, else null
  function validCapital(world, cu) {
    const s = world.settlements[cu.capital];
    return s && !s.dead && s.culture === cu.id ? s : null;
  }
  function dropArk(s) {
    if (s.b) s.b.launch = 0;
    s.ark = 0;
  }
  // cosine of the angle between two cells (larger = nearer); the same arithmetic for every caller, so equal
  // distances compare exactly equal
  function dotc(pos, a, b) {
    a *= 3; b *= 3;
    return pos[a] * pos[b] + pos[a + 1] * pos[b + 1] + pos[a + 2] * pos[b + 2];
  }

  // Scratch buffers, keyed by cell count (tests run L = 4, the game L = 6).
  let S = null;
  function scratch(n) {
    if (!S || S.n !== n) {
      S = {
        n, stamp: 0,
        mark: new Int32Array(n),          // visited stamps of the current search
        q: new Int32Array(n),             // search queue; holds the result of disc()
        qd: new Uint8Array(n),            // hop distance of q[i]
        oMark: new Int32Array(n),         // claim: stamp of "some rival's radius covers this cell"
        oDot: new Float64Array(n),        // claim: nearness (cosine) of the nearest such rival's centre
        oId: new Int32Array(n),           // claim: id of that rival
        tmp: new Int32Array(n),           // claim: the new territory before it is stored
        d8: new Uint8Array(n),            // bestSite: hops to live-owned land, capped
        part: new Float32Array(n),        // bestSite: site score without the room term
        lid: new Int32Array(n),           // bestSite: landmass labels
      };
    }
    return S;
  }
  function nextStamp(sc) {
    if (sc.stamp >= 0x7ffffff0) { sc.mark.fill(0); sc.oMark.fill(0); sc.stamp = 0; }
    return ++sc.stamp;
  }

  // Cells within `hops` neighbour hops of c, breadth first: scratch.q[0 … count-1], distances in scratch.qd.
  function disc(world, c, hops) {
    const sc = scratch(world.n), nbr = world.geo.nbr, mark = sc.mark, q = sc.q, qd = sc.qd;
    const st = nextStamp(sc);
    mark[c] = st; q[0] = c; qd[0] = 0;
    let count = 1;
    for (let head = 0; head < count; head++) {
      const d = qd[head];
      if (d >= hops) break;                              // breadth first: everything after is as far or farther
      const b = 6 * q[head];
      for (let j = 0; j < 6; j++) {
        const m = nbr[b + j];
        if (m < 0) break;
        if (mark[m] === st) continue;
        mark[m] = st; q[count] = m; qd[count] = d + 1; count++;
      }
    }
    return count;
  }

  // ------------------------------------------------------------------ queries

  Civ.live = function (world) {
    if (!world || !world.settlements) return [];
    const setts = world.settlements, c = world._civLive;
    if (c && c.src === setts && c.len === setts.length) {
      // Nothing comes back from the dead and every new settlement lengthens the list, so the cached
      // array is still right unless one of its members has died.
      const a = c.arr;
      let i = 0;
      while (i < a.length && !a[i].dead) i++;
      if (i === a.length) return a;
    }
    const arr = [];
    for (let i = 0; i < setts.length; i++) { const s = setts[i]; if (s && !s.dead) arr.push(s); }
    world._civLive = { src: setts, len: setts.length, arr };
    return arr;
  };

  Civ.at = function (world, cell) {
    if (!usable(world) || !validCell(world, cell)) return null;
    const o = world.owner[cell];
    const own = o >= 0 ? liveById(world, o) : null;
    if (own && own.cell === cell) return own;
    const live = Civ.live(world);
    for (let i = 0; i < live.length; i++) if (live[i].cell === cell) return live[i];
    return own;
  };

  let nearD = new Float64Array(64);                      // angle of each candidate, by settlement id
  function nearCmp(a, b) { return (nearD[a.id] - nearD[b.id]) || (a.id - b.id); }
  Civ.near = function (world, cell, ang) {
    const out = [];
    if (!usable(world) || !validCell(world, cell) || !(ang >= 0)) return out;
    const live = Civ.live(world), geo = world.geo, m = world.settlements.length;
    if (nearD.length < m) nearD = new Float64Array(m * 2);
    for (let i = 0; i < live.length; i++) {
      const s = live[i];
      if (!validCell(world, s.cell)) continue;
      const d = geo.angle(cell, s.cell);
      if (d <= ang) { nearD[s.id] = d; out.push(s); }
    }
    if (out.length > 1) out.sort(nearCmp);
    return out;
  };

  Civ.ageProgress = function (world, cultureId) {
    const cu = world && world.cultures ? cultureOf(world, cultureId) : null;
    if (!cu) return 0;
    const need = TUNE.civ.ageNeed, age = cu.age | 0;
    if (age >= DM.AGES.length - 1 || !need || age >= need.length) return 1;
    const nd = need[age < 0 ? 0 : age];
    if (!(nd > 0)) return 1;
    const p = cu.science / nd;
    return p > 0 ? (p < 1 ? p : 1) : 0;
  };

  // ------------------------------------------------------------------ sites

  // 0 ok, 1 sea or lake, 2 barren (crowding is checked separately)
  function groundCode(world, c, T) {
    if (world.elev[c] < world.seaLevel || world.water[c] > T.lakeWater) return 1;
    return world.hab[c] > T.barrenHab ? 0 : 2;
  }
  // is any cell within `hops` of c owned by a live settlement?
  function crowded(world, c, hops) {
    if (!Civ.live(world).length) return false;
    const cnt = disc(world, c, hops), q = S.q, owner = world.owner, setts = world.settlements;
    for (let i = 0; i < cnt; i++) {
      const o = owner[q[i]];
      if (o >= 0) { const t = setts[o]; if (t && !t.dead) return true; }
    }
    return false;
  }

  Civ.canSettle = function (world, c) {
    if (!usable(world) || !validCell(world, c)) return { ok: false, reason: 'barren' };
    const T = TUNE.civ, g = groundCode(world, c, T);
    if (g === 1) return { ok: false, reason: 'sea' };
    if (g === 2) return { ok: false, reason: 'barren' };
    if (crowded(world, c, hopsOf(T.crowdHops, 7))) return { ok: false, reason: 'crowded' };
    return { ok: true, reason: 'ok' };
  };

  // siteScore = sitePart + siteRoom × siteRoomOf. bestSite uses the same two pieces, so the two always agree.
  function sitePart(world, c, T) {
    const f = world.fresh[c], k = world.coast[c];
    const fresh = f <= 1 ? 1 : f === 2 ? 0.6 : f === 3 ? 0.3 : 0;
    const coast = k >= 1 && k <= 3 ? 1 : k === 4 ? 0.5 : 0;
    const low = T.siteLowH > 0 ? sat(1 - (world.elev[c] - world.seaLevel) / T.siteLowH) : 0;
    return world.hab[c] + T.siteFresh * fresh + T.siteCoast * coast - T.siteLow * low;
  }
  function siteRoomOf(world, c, T) {
    const cnt = disc(world, c, hopsOf(T.siteRoomHops, 4)), q = S.q;
    const elev = world.elev, water = world.water, hab = world.hab, sea = world.seaLevel, lake = T.lakeWater;
    let sum = 0;
    for (let i = 0; i < cnt; i++) {
      const m = q[i];
      if (elev[m] >= sea && !(water[m] > lake)) { const h = hab[m]; if (h > 0) sum += h; }
    }
    return sum / cnt;
  }

  Civ.siteScore = function (world, c) {
    if (!usable(world) || !validCell(world, c)) return 0;
    const T = TUNE.civ, g = groundCode(world, c, T);
    if (g === 1) return 0;
    if (g === 2) { const h = world.hab[c]; return h > 0 ? h : 0; }   // unsettleable ground earns no bonuses
    const v = sitePart(world, c, T) + T.siteRoom * siteRoomOf(world, c, T);
    return finite(v) ? v : 0;
  };

  // The landmass (connected land) with the most habitable cells: labels every cell in scratch.lid (−1 on sea)
  // and returns the winning label, −1 when nothing is habitable. Ties: the larger landmass, then the one
  // holding the lowest cell id. Always a flood fill of our own, so it is right even while Terrain's labels
  // are stale or absent.
  function bestLandmass(world, T) {
    const n = world.n, sc = scratch(n), q = sc.q, lid = sc.lid, nbr = world.geo.nbr;
    const elev = world.elev, sea = world.seaLevel, hab = world.hab, water = world.water;
    const bar = T.barrenHab, lake = T.lakeWater;
    for (let c = 0; c < n; c++) lid[c] = elev[c] < sea ? -1 : -2;
    let k = 0, best = -1, bestHab = 0, bestSize = 0;
    for (let c0 = 0; c0 < n; c0++) {
      if (lid[c0] !== -2) continue;
      let head = 0, end = 0, cnt = 0;
      q[end++] = c0; lid[c0] = k;
      while (head < end) {
        const c = q[head++];
        if (hab[c] > bar && !(water[c] > lake)) cnt++;
        const b = 6 * c;
        for (let j = 0; j < 6; j++) {
          const m = nbr[b + j];
          if (m < 0) break;
          if (lid[m] === -2) { lid[m] = k; q[end++] = m; }
        }
      }
      if (cnt > bestHab || (cnt === bestHab && cnt > 0 && end > bestSize)) { bestHab = cnt; bestSize = end; best = k; }
      k++;
    }
    return best;
  }

  // Exact argmax of siteScore over the settleable cells of one landmass (target < 0: any landmass); ties go
  // to the lower cell id. The room term needs a walk around the cell, so it is only evaluated for cells whose
  // cheap part could still beat the best site found so far.
  function bestOn(world, T, lid, target, useCrowd) {
    const n = world.n, sc = scratch(n), part = sc.part, d8 = sc.d8, crowd = hopsOf(T.crowdHops, 7);
    const hab = world.hab, room = T.siteRoom;
    let seed = -1, seedV = -Infinity, hmax = 0;
    for (let c = 0; c < n; c++) {
      part[c] = -Infinity;
      if (target >= 0 && lid[c] !== target) continue;
      if (groundCode(world, c, T) !== 0) continue;
      if (useCrowd && d8[c] <= crowd) continue;
      const p = sitePart(world, c, T);
      if (!finite(p)) continue;
      part[c] = p;
      if (p > seedV) { seedV = p; seed = c; }
    }
    if (seed < 0) return -1;
    for (let c = 0; c < n; c++) { const h = hab[c]; if (h > hmax) hmax = h; }
    const slack = (room > 0 ? room * hmax : 0) + 1e-6;   // the most the room term can add (part[] is float32)
    let best = seed, bestV = part[seed] + room * siteRoomOf(world, seed, T);
    for (let c = 0; c < n; c++) {
      const p = part[c];
      if (c === seed || !(p + slack >= bestV)) continue;
      const v = p + room * siteRoomOf(world, c, T);
      if (v > bestV || (v === bestV && c < best)) { bestV = v; best = c; }
    }
    return best;
  }

  Civ.bestSite = function (world) {
    if (!usable(world)) return -1;
    const T = TUNE.civ, n = world.n, sc = scratch(n);
    // hops from live-owned land, capped just beyond the crowding distance
    let useCrowd = false;
    if (Civ.live(world).length) {
      const d8 = sc.d8, q = sc.q, nbr = world.geo.nbr, owner = world.owner, setts = world.settlements;
      const cap = hopsOf(T.crowdHops, 7) + 1;
      let tail = 0;
      for (let c = 0; c < n; c++) {
        const o = owner[c];
        const t = o >= 0 ? setts[o] : null;
        if (t && !t.dead) { d8[c] = 0; q[tail++] = c; } else d8[c] = cap;
      }
      useCrowd = tail > 0;
      for (let head = 0; head < tail; head++) {
        const c = q[head], d = d8[c] + 1;
        if (d >= cap) continue;
        const b = 6 * c;
        for (let j = 0; j < 6; j++) {
          const m = nbr[b + j];
          if (m < 0) break;
          if (d8[m] > d) { d8[m] = d; q[tail++] = m; }
        }
      }
    }
    const target = bestLandmass(world, T);
    if (target < 0) return -1;
    const best = bestOn(world, T, sc.lid, target, useCrowd);
    // the chosen landmass may be full: then the best free site anywhere
    return best >= 0 ? best : bestOn(world, T, sc.lid, -1, useCrowd);
  };

  // ------------------------------------------------------------------ territory

  function release(world, s) {
    const owner = world.owner, id = s.id, cells = s._cells, k = s._n;
    if (cells && k >= 0 && k <= cells.length && k === Math.floor(k)) {
      for (let i = 0; i < k; i++) { const c = cells[i]; if (owner[c] === id) owner[c] = -1; }
    } else {
      for (let c = 0; c < owner.length; c++) if (owner[c] === id) owner[c] = -1;   // cache lost: find them the slow way
    }
  }

  Civ.claim = function (world, s) {
    s = entity(world, s);
    if (!s || s.dead || !validCell(world, s.cell)) return 0;
    const n = world.n, geo = world.geo, pos = geo.pos, owner = world.owner, elev = world.elev, sea = world.seaLevel;
    const sc = scratch(n), q = sc.q, oMark = sc.oMark, oDot = sc.oDot, oId = sc.oId, tmp = sc.tmp;
    const setts = world.settlements, id = s.id, cell = s.cell, r = radiusOf(s);
    release(world, s);

    // Every other live settlement whose radius could reach ours records, for each cell it covers, how near
    // that cell is to its centre. (No neighbour step is longer than 1.1 × geo.step; 1.6 is a safe bound.)
    const ost = nextStamp(sc), live = Civ.live(world), reach = geo.step * 1.6;
    for (let i = 0; i < live.length; i++) {
      const t = live[i];
      if (t === s || !validCell(world, t.cell)) continue;
      const rt = radiusOf(t), tc = t.cell;
      if (geo.angle(cell, tc) > (r + rt + 1) * reach) continue;
      const cnt = disc(world, tc, rt);
      for (let j = 0; j < cnt; j++) {
        const c = q[j], d = dotc(pos, c, tc);
        if (oMark[c] !== ost || d > oDot[c] || (d === oDot[c] && t.id < oId[c])) { oMark[c] = ost; oDot[c] = d; oId[c] = t.id; }
      }
    }

    const cnt = disc(world, cell, r);
    let k = 0;
    for (let i = 0; i < cnt; i++) {
      const c = q[i];
      if (elev[c] < sea) continue;                                     // never the sea
      const o = owner[c];
      if (o >= 0 && o !== id) { const t = setts[o]; if (t && !t.dead) continue; }   // theirs already
      if (oMark[c] === ost) {                                          // contested: the nearer centre, then the lower id
        const d = dotc(pos, c, cell), od = oDot[c];
        if (od > d || (od === d && oId[c] < id)) continue;
      }
      owner[c] = id;
      tmp[k++] = c;
    }
    if (k > 1) tmp.subarray(0, k).sort();

    const old = s._cells, oldN = s._n;
    let same = !!old && oldN === k && k <= old.length;
    for (let i = 0; same && i < k; i++) if (old[i] !== tmp[i]) same = false;
    if (!same) {
      const cells = old && old !== EMPTY && old.length >= k ? old : new Int32Array(k > 19 ? k : 19);
      for (let i = 0; i < k; i++) cells[i] = tmp[i];
      s._cells = cells;
      s._n = k;
      world.dirty.color = true;
    }
    return k;
  };

  Civ.rebuild = function (world) {
    if (!usable(world)) return;
    const setts = world.settlements, owner = world.owner, n = world.n, m = setts.length;
    const count = new Int32Array(m);
    for (let c = 0; c < n; c++) { const o = owner[c]; if (o >= 0 && o < m) count[o]++; }
    for (let i = 0; i < m; i++) {
      const s = setts[i];
      if (!s) continue;
      s._cells = s.dead || !count[i] ? EMPTY : new Int32Array(count[i]);
      s._n = 0;
    }
    for (let c = 0; c < n; c++) {
      const o = owner[c];
      if (o < 0 || o >= m) continue;
      const s = setts[o];
      if (s && !s.dead) s._cells[s._n++] = c;
    }
    world._civLive = null;
  };

  // ------------------------------------------------------------------ capitals and membership

  Civ.electCapital = function (world, cultureId) {
    const cu = usable(world) ? cultureOf(world, cultureId) : null;
    if (!cu) return -1;
    const setts = world.settlements;
    let best = null;
    for (let i = 0; i < setts.length; i++) {
      const s = setts[i];
      if (!s || s.dead || s.culture !== cu.id) continue;
      if (!best || s.pop > best.pop) best = s;             // ascending id: ties stay with the lower id
    }
    const oldId = cu.capital, newId = best ? best.id : -1;
    if (oldId !== newId) {
      const old = setts[oldId];
      if (old) dropArk(old);
      if (best) dropArk(best);                           // a new capital starts its own launch site and Ark from 0
      world.dirty.objects = true;
    }
    cu.capital = newId;
    for (let i = 0; i < setts.length; i++) { const s = setts[i]; if (s && s.culture === cu.id) s.capital = s.id === newId; }
    return newId;
  };

  function changeCulture(world, s, cultureId, how) {
    s = entity(world, s);
    if (!s || s.dead) return false;
    const to = cultureOf(world, cultureId);
    if (!to || to.dead || to.id === s.culture) return false;
    const from = s.culture, fromCu = cultureOf(world, from);
    s.culture = to.id;
    s.capital = false;
    dropArk(s);
    if (fromCu && fromCu.capital === s.id) { fromCu.capital = -1; Civ.electCapital(world, fromCu.id); }
    if (!validCapital(world, to)) Civ.electCapital(world, to.id);
    world.dirty.objects = true;
    world.dirty.color = true;
    DM.Bus.emit('settlement:captured', { id: s.id, from, to: to.id, how });
    return true;
  }
  Civ.capture = function (world, s, cultureId) { return changeCulture(world, s, cultureId, 'war'); };
  Civ.setCulture = function (world, s, cultureId) { return changeCulture(world, s, cultureId, 'schism'); };

  // ------------------------------------------------------------------ founding and destruction

  const NAME_C = ['b', 'd', 'g', 'k', 'l', 'm', 'n', 'r', 's', 't', 'v', 'z', 'th', 'sh'];
  const NAME_V = ['a', 'e', 'i', 'o', 'u', 'ai', 'ou'];
  const NAME_E = ['n', 'r', 's', 'l', 'th'];
  function nameTaken(world, name) {
    const setts = world.settlements;
    for (let i = 0; i < setts.length; i++) if (setts[i] && setts[i].name === name) return true;
    return false;
  }
  // A place name in the culture's language. Drawn from a fork of the sim stream, so naming never changes
  // what happens next (with or without names.js).
  function makeName(world, cu, id) {
    const rng = world.rng.fork('settle:' + id), N = DM.Names;
    let name = '';
    for (let tries = 0; tries < 8; tries++) {
      name = '';
      if (N && typeof N.place === 'function' && cu.lang) {
        const v = N.place(cu.lang, rng);
        if (typeof v === 'string') name = v;
      }
      if (!name) {
        const syl = rng.int(2, 3);
        for (let i = 0; i < syl; i++) name += rng.pick(NAME_C) + rng.pick(NAME_V);
        if (rng.chance(0.4)) name += rng.pick(NAME_E);
        name = name.charAt(0).toUpperCase() + name.slice(1);
      }
      if (!nameTaken(world, name)) break;
    }
    return name;
  }

  // Stand-in for Civ.capacity while economy.js is absent: the spec's capacity rule with habitability in
  // place of the temperature / rainfall suitability.
  function estimateK(world, s) {
    const T = TUNE.civ, cells = s._cells, k = s._n | 0;
    if (!cells || k <= 0) return 0;
    const yields = T.techYield || T.estYield, cu = world.cultures[s.culture];
    const age = cu ? Math.max(0, Math.min(yields.length - 1, cu.age | 0)) : 0;
    const ty = yields[age], sc = scratch(world.n), mark = sc.mark, st = nextStamp(sc);
    const nbr = world.geo.nbr, elev = world.elev, sea = world.seaLevel, water = world.water, lake = T.lakeWater;
    const hab = world.hab, fert = world.fert, veg = world.veg, fauna = world.fauna;
    const fishK = 4 * (1 + (s.b && s.b.port ? 1 : 0)) * Math.sqrt(Math.min(ty, 10));
    let K = 0;
    for (let i = 0; i < k; i++) {
      const c = cells[i];
      if (!(water[c] > lake)) {
        const forage = 3 * (0.6 * veg[c] + 0.4 * fauna[c]);
        const farm = hab[c] > 0.15 ? 22 * Math.min(fert[c], 1.2) * hab[c] * ty : 0;
        K += farm > forage ? farm : forage;
      }
      const b = 6 * c;
      for (let j = 0; j < 6; j++) {
        const m = nbr[b + j];
        if (m < 0) break;
        if (elev[m] < sea && mark[m] !== st) { mark[m] = st; K += fishK * fauna[m]; }
      }
    }
    if (s.b && s.b.granary) K *= 1.15;
    if (s.b && s.b.plant) K *= 1.25;
    return finite(K) && K > 0 ? K : 0;
  }
  function capacityOf(world, s) {
    const f = DM.Civ.capacity;
    if (typeof f === 'function') {
      const K = f(world, s);
      if (finite(K) && K >= 0) return K;
    }
    return estimateK(world, s);
  }

  Civ.found = function (world, cell, cultureId, pop, by, opts) {
    if (!usable(world)) return null;
    const T = TUNE.civ, cu = cultureOf(world, cultureId);
    if (!cu || cu.dead || !finite(pop) || !(pop > 0)) return null;
    if (Civ.live(world).length >= T.maxSettlements || world.settlements.length >= MAX_ID) return null;
    if (!Civ.canSettle(world, cell).ok) return null;
    by = by === 'awaken' ? 'awaken' : 'settlers';
    let devotion = by === 'awaken' ? T.awakenDevotion : T.settlerDevotion;
    let love = by === 'awaken' ? T.awakenLove : T.settlerLove;
    if (opts) {
      if (finite(opts.devotion)) devotion = opts.devotion;
      if (finite(opts.love)) love = opts.love;
    }
    const s = DM.State.newSettlement(world, {
      cell, name: makeName(world, cu, world.settlements.length), culture: cu.id,
      founded: world.year, lastExpand: world.year,
      pop, tier: 0, radius: hopsOf(T.radius && T.radius[0], 2),
      devotion: sat(devotion), love: love < -1 ? -1 : love > 1 ? 1 : love,
    });
    s._cells = EMPTY;
    s._n = 0;
    Civ.claim(world, s);
    s.K = capacityOf(world, s);
    if (!validCapital(world, cu)) { cu.capital = s.id; s.capital = true; }
    if (world.urban[cell] < T.campUrban) world.urban[cell] = T.campUrban;
    world.dirty.objects = true;
    world.dirty.color = true;
    DM.Bus.emit('settlement:founded', { id: s.id, cell, culture: cu.id, by });
    return s;
  };

  Civ.destroy = function (world, s, cause) {
    s = entity(world, s);
    if (!s || s.dead) return false;
    if (Civ.CAUSES.indexOf(cause) < 0) cause = 'abandoned';
    s.dead = true;
    s.deadYear = world.year;
    s.cause = cause;
    release(world, s);
    s._n = 0;
    s.capital = false;
    dropArk(s);
    const cu = cultureOf(world, s.culture);
    if (cu && cu.capital === s.id) { cu.capital = -1; Civ.electCapital(world, cu.id); }
    world.dirty.objects = true;
    world.dirty.color = true;
    DM.Bus.emit('settlement:destroyed', { id: s.id, cell: s.cell, culture: s.culture, cause });
    return true;
  };

  Civ.damage = function (world, s, frac, cause) {
    s = entity(world, s);
    if (!s || s.dead || !(frac > 0)) return 0;
    if (frac > 1) frac = 1;
    const before = s.pop > 0 && finite(s.pop) ? s.pop : 0;
    const killed = before * frac;
    if (before - killed < TUNE.civ.minPeople) {
      s.pop = before;
      Civ.destroy(world, s, cause);
      return before;
    }
    s.pop = before - killed;
    return killed;
  };

  // ------------------------------------------------------------------ plague

  Civ.infect = function (world, s, severity) {
    s = entity(world, s);
    if (!s || s.dead || s.immune > 0) return false;
    if (severity === undefined) severity = 0.6;
    if (!(severity > 0)) return false;
    if (severity > 1) severity = 1;
    const was = s.plague > 0 ? s.plague : 0;
    s.plague = was > severity ? was : severity;
    if (was === 0) DM.Bus.emit('settlement:plague', { id: s.id, start: true });
    return true;
  };

  Civ.cure = function (world, s, immuneYears) {
    s = entity(world, s);
    if (!s || s.dead) return false;
    if (immuneYears === undefined) immuneYears = 30;
    const was = s.plague > 0;
    s.plague = 0;
    s.immune = finite(immuneYears) && immuneYears > 0 ? immuneYears : 0;
    if (was) DM.Bus.emit('settlement:plague', { id: s.id, start: false });
    return true;
  };
})(globalThis.DM = globalThis.DM || {});
