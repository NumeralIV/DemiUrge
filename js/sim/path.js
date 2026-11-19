// DEMIURGE — path.js : DM.Path (SPEC-SIM §3.6). Travel costs and cheapest routes over the cell graph.
//
//   Path.find(world, from, to, { naval = false, maxCost = 400 } = {}) → [from, …, to] | null
//        A* with an admissible, consistent heuristic: the returned route has the lowest possible total cost.
//        null when there is no route costing at most maxCost, when `to` can never be entered, or for bad cells.
//        [from] when from === to. The result is a fresh plain array (JSON-safe, usable as unit.path).
//   Path.stepCost(world, a, b, naval) → cost of moving from cell a to its neighbour b, or Infinity
//        (Infinity for impassable steps, non-neighbours and bad cells; 0 for a === b)
//   Path.reach(world, from, maxCost, naval, visit) → number of cells visited
//        Dijkstra flood. visit(cell, cost) is called once per reachable cell in non-decreasing cost, `from`
//        first with cost 0; returning false stops the flood. visit may itself call Path.find / Path.reach.
//   Extras: Path.cost(world, path, naval) → total cost of a route (Infinity if any step is impassable)
//           Path.lastCost                  cost of the route returned by the latest successful Path.find
//
// naval: false = on foot (land only) · 'coastal' = also sea cells shallower than TUNE.path.shallow ·
//        true = any sea cell. Anything else counts as false.
//
// Cost of a step a → b (it depends on what is being entered, b, and on the height difference):
//   land → land   land + slope × |Δ surface height| (+ forest, + ice, + lake when b is one), − road per level
//                 when BOTH cells carry a road (the lower level counts), never below `floor`
//   sea  → sea    `coastal` / `sea` by naval mode
//   land ↔ sea    `embark` (flat), whatever the terrain
//   impassable    lava; sea without the right naval mode; sea ice
// The start cell itself is never tested, so a party standing on a cell that has just drowned (or caught
// lava) can still walk out of it: leaving a sea cell on foot costs `embark`.
//
// Nothing is allocated per call except the route find returns: one binary heap with decrease-key and stamp
// arrays, all sized by world.n and kept between calls (one set per nesting depth, for reach inside reach).
// Costs travel through typed arrays rather than closure variables and return values, so the searches make
// no garbage at all (measured: 0 bytes for a whole-planet flood). The one thing outside this file's control
// is the engine's box around a fractional `cost` handed to a reach callback it has not inlined (16 bytes).
(function (DM) {
  'use strict';
  const Path = (DM.Path = DM.Path || {});
  const TUNE = (DM.TUNE = DM.TUNE || {});

  TUNE.path = {
    land: 1,          // a step across open, level land
    slope: 6,         // + per world unit of height difference between the two cells (lake surface on lakes)
    forest: 0.4,      // + entering dense forest ...
    forestVeg: 0.6,   // ... a cell with veg at or above this
    ice: 3,           // + entering an iced-over land cell ...
    iceT: 0.5,        // ... ice above this
    lake: 2,          // + entering a lake cell ...
    lakeT: 0.03,      // ... standing water deeper than this (Hydro's lake-cell rule)
    road: 0.25,       // − per road level, when both cells carry a road (the lower level counts)
    floor: 0.3,       // a land step never costs less than this
    lava: 0.05,       // a cell with more lava than this cannot be entered
    coastal: 0.6,     // sea step for naval = 'coastal' ...
    shallow: 0.6,     // ... allowed only where the sea is shallower than this
    sea: 0.35,        // sea step for naval = true
    embark: 3,        // the step that takes a party from land onto the sea or from the sea onto land
    seaIce: 0.6,      // a sea cell with more ice than this cannot be entered
    maxCost: 400,     // default budget of Path.find
  };
  // The A* lower bound assumes the surcharges (slope, forest, ice, lake) are not negative.

  // ---------------------------------------------------------------- cost model, bound to one world + mode
  // Every number of the bound model lives in the Float64Array K, never in a closure variable: to the engine a
  // fractional number held in a variable is a boxed object, and letting one meet plain arithmetic (as in
  // `c > floor ? c : floor`) makes it box the other side too — measured at 14 bytes of garbage per cell searched.
  let cElev = null, cWater = null, cVeg = null, cIce = null, cLava = null, cRoad = null;
  let cMode = 0;                        // 0 on foot, 1 coastal, 2 naval
  let serial = 0;                       // bumped by every bind: lets reach notice a nested search
  const SEA = 0, LAND = 1, SLOPE = 2, FOREST = 3, FOREST_VEG = 4, ICE = 5, ICE_T = 6, LAKE = 7, LAKE_T = 8, ROAD = 9,
    FLOOR = 10, LAVA = 11, COASTAL = 12, SHALLOW = 13, SEA_STEP = 14, EMBARK = 15, SEA_ICE = 16;
  const K = new Float64Array(17);
  const OUT = new Float64Array(2);      // [0] cost written by step(), [1] bound written by minStep()

  function modeOf(naval) { return naval === true ? 2 : naval === 'coastal' ? 1 : 0; }

  function bind(world, mode) {
    const T = TUNE.path;
    cElev = world.elev; cWater = world.water; cVeg = world.veg; cIce = world.ice; cLava = world.lava; cRoad = world.road;
    cMode = mode;
    K[SEA] = world.seaLevel;
    K[LAND] = T.land; K[SLOPE] = T.slope; K[FOREST] = T.forest; K[FOREST_VEG] = T.forestVeg;
    K[ICE] = T.ice; K[ICE_T] = T.iceT; K[LAKE] = T.lake; K[LAKE_T] = T.lakeT;
    K[ROAD] = T.road; K[FLOOR] = T.floor; K[LAVA] = T.lava;
    K[COASTAL] = T.coastal; K[SHALLOW] = T.shallow; K[SEA_STEP] = T.sea; K[EMBARK] = T.embark; K[SEA_ICE] = T.seaIce;
    serial++;
  }

  // The step a → b for the bound world and mode (b must be a neighbour of a): false when b cannot be
  // entered, else true with the cost in OUT[0].
  function step(a, b) {
    if (cLava[b] > K[LAVA]) return false;
    const sea = K[SEA], ea = cElev[a], eb = cElev[b];
    if (eb < sea) {                                            // entering the sea
      if (cMode === 0 || cIce[b] > K[SEA_ICE]) return false;
      if (ea >= sea) OUT[0] = K[EMBARK];
      else if (cMode === 2) OUT[0] = K[SEA_STEP];
      else OUT[0] = K[COASTAL];
      return cMode === 2 || sea - eb < K[SHALLOW];             // canoes keep to the shelves
    }
    if (ea < sea) { OUT[0] = K[EMBARK]; return true; }         // landing
    let d = eb + cWater[b] - ea - cWater[a];
    if (d < 0) d = -d;
    let c = K[LAND] + K[SLOPE] * d;
    if (cVeg[b] >= K[FOREST_VEG]) c += K[FOREST];
    if (cIce[b] > K[ICE_T]) c += K[ICE];
    if (cWater[b] > K[LAKE_T]) c += K[LAKE];
    const ra = cRoad[a], rb = cRoad[b], r = ra < rb ? ra : rb;
    if (r > 0) c -= K[ROAD] * r;
    const floor = K[FLOOR];
    OUT[0] = c > floor ? c : floor;                            // also turns a NaN into the floor
    return true;
  }

  // true when no step can ever enter c in the bound mode
  function shut(c) {
    if (cLava[c] > K[LAVA]) return true;
    const e = cElev[c], sea = K[SEA];
    if (e < sea) {
      if (cMode === 0 || cIce[c] > K[SEA_ICE]) return true;
      if (cMode === 1 && !(sea - e < K[SHALLOW])) return true;
    }
    return false;
  }

  // OUT[1] = the cheapest step that can exist in the bound world and mode (for the A* lower bound)
  function minStep() {
    const floor = K[FLOOR], perLevel = K[ROAD];
    let land = K[LAND];
    if (perLevel > 0 && land > floor) {
      const road = cRoad, n = road.length, enough = (land - floor) / perLevel;
      let mx = 0;
      for (let c = 0; c < n; c++) {
        if (road[c] > mx) { mx = road[c]; if (mx >= enough) break; }
      }
      land -= perLevel * mx;
    }
    if (!(land > floor)) land = floor;
    let m = land < K[EMBARK] ? land : K[EMBARK];
    if (cMode === 1 && K[COASTAL] < m) m = K[COASTAL];
    if (cMode === 2 && K[SEA_STEP] < m) m = K[SEA_STEP];
    OUT[1] = m > 0 ? m : 0;
  }

  // ---------------------------------------------------------------- geometry of the grid (cached per geo)
  // EDGE[0] = the longest edge of the grid as an arc. A route of k steps spans at most k × that arc, so
  // arc(c, target) / longest edge is a lower bound on the steps still to go.
  let mGeo = null;
  const EDGE = new Float64Array(1);
  function maxEdge(geo) {
    if (mGeo === geo) return;
    const pos = geo.pos, nbr = geo.nbr, n = geo.n;
    let mx = 0;
    for (let c = 0; c < n; c++) {
      const x = pos[3 * c], y = pos[3 * c + 1], z = pos[3 * c + 2];
      for (let j = 0; j < 6; j++) {
        const b = nbr[6 * c + j];
        if (b <= c) continue;                                  // each edge once; skips the −1 slot too
        const dx = pos[3 * b] - x, dy = pos[3 * b + 1] - y, dz = pos[3 * b + 2] - z;
        const d2 = dx * dx + dy * dy + dz * dz;
        if (d2 > mx) mx = d2;
      }
    }
    const h = Math.sqrt(mx) * 0.5;
    let e = 2 * Math.asin(h < 1 ? h : 1) * (1 + 1e-9);
    if (!(e > 0)) e = 1;
    EDGE[0] = e;
    mGeo = geo;
  }

  // ---------------------------------------------------------------- scratch, keyed by world.n and nesting depth
  const pool = [];
  let depth = 0;                        // > 0 while a reach is running its callbacks
  function scratch(n, d) {
    let S = pool[d];
    if (!S || S.n !== n) {
      S = pool[d] = {
        n,
        g: new Float64Array(n),         // cost from the start
        f: new Float64Array(n),         // g + heuristic (find only)
        mark: new Int32Array(n),        // === stamp: reached in the current search
        pos: new Int32Array(n),         // index in the heap, −1 once settled
        prev: new Int32Array(n),        // predecessor on the best route (find only)
        heap: new Int32Array(n),
        stamp: 0,
      };
    }
    if (S.stamp >= 0x3ffffff0) { S.mark.fill(0); S.stamp = 0; }
    S.stamp++;
    return S;
  }

  // ---------------------------------------------------------------- binary min-heap of cells by (key, id)
  let hHeap = null, hPos = null, hKey = null, hN = 0;
  // put cell c into slot i and sift it up (push: i = hN++; decrease-key: i = hPos[c])
  function hUp(i, c) {
    const heap = hHeap, pos = hPos, key = hKey, k = key[c];
    while (i > 0) {
      const p = (i - 1) >> 1, pc = heap[p], pk = key[pc];
      if (pk < k || (pk === k && pc < c)) break;
      heap[i] = pc; pos[pc] = i;
      i = p;
    }
    heap[i] = c; pos[c] = i;
  }
  function hPop() {
    const heap = hHeap, pos = hPos, key = hKey;
    const top = heap[0], n = --hN;
    if (n > 0) {
      const c = heap[n], k = key[c];
      let i = 0;
      for (;;) {
        let m = 2 * i + 1;
        if (m >= n) break;
        let mc = heap[m], mk = key[mc];
        const r = m + 1;
        if (r < n) {
          const rc = heap[r], rk = key[rc];
          if (rk < mk || (rk === mk && rc < mc)) { m = r; mc = rc; mk = rk; }
        }
        if (k < mk || (k === mk && c < mc)) break;
        heap[i] = mc; pos[mc] = i;
        i = m;
      }
      heap[i] = c; pos[c] = i;
    }
    pos[top] = -1;
    return top;
  }

  function ok(world, c) { return !!world && !!world.geo && Number.isInteger(c) && c >= 0 && c < world.n; }

  // ---------------------------------------------------------------- public API
  Path.lastCost = 0;

  Path.stepCost = function (world, a, b, naval) {
    if (!ok(world, a) || !ok(world, b)) return Infinity;
    if (a === b) return 0;
    const nbr = world.geo.nbr, o = 6 * a;
    if (nbr[o] !== b && nbr[o + 1] !== b && nbr[o + 2] !== b && nbr[o + 3] !== b && nbr[o + 4] !== b && nbr[o + 5] !== b) return Infinity;
    bind(world, modeOf(naval));
    return step(a, b) ? OUT[0] : Infinity;
  };

  Path.cost = function (world, path, naval) {
    if (!world || !path || !(path.length > 0) || !ok(world, path[0])) return Infinity;
    let sum = 0;
    for (let i = 1; i < path.length; i++) {
      const w = Path.stepCost(world, path[i - 1], path[i], naval);
      if (!(w < Infinity)) return Infinity;
      sum += w;
    }
    return sum;
  };

  Path.find = function (world, from, to, opts) {
    if (!ok(world, from) || !ok(world, to)) return null;
    let naval = false, maxCost = TUNE.path.maxCost;
    if (opts) {
      naval = opts.naval;
      if (opts.maxCost !== undefined && opts.maxCost !== null) maxCost = +opts.maxCost;
    }
    if (maxCost !== maxCost) maxCost = TUNE.path.maxCost;       // NaN
    if (from === to) { Path.lastCost = 0; return [from]; }
    bind(world, modeOf(naval));
    if (shut(to)) return null;

    const geo = world.geo, nbr = geo.nbr, pos = geo.pos;
    const S = scratch(world.n, depth);
    const g = S.g, f = S.f, mark = S.mark, hp = S.pos, prev = S.prev, stamp = S.stamp;
    hHeap = S.heap; hPos = hp; hKey = f; hN = 0;
    // Heuristic: arc to the target × (cheapest step / longest edge). 2·asin(chord / 2) of the stored positions
    // is a metric even though they are float32-rounded, so the bound is consistent: no cell is expanded twice.
    minStep();
    maxEdge(geo);
    const tx = pos[3 * to], ty = pos[3 * to + 1], tz = pos[3 * to + 2];
    const scale = 2 * OUT[1] / EDGE[0] * (1 - 1e-9);

    let dx = pos[3 * from] - tx, dy = pos[3 * from + 1] - ty, dz = pos[3 * from + 2] - tz;
    let h = Math.sqrt(dx * dx + dy * dy + dz * dz) * 0.5;
    const h0 = scale * Math.asin(h < 1 ? h : 1);
    if (h0 > maxCost) return null;
    g[from] = 0; f[from] = h0; mark[from] = stamp; prev[from] = -1;
    hUp(hN++, from);
    while (hN > 0) {
      const c = hPop();
      if (c === to) {
        let len = 1;
        for (let x = to; x !== from; x = prev[x]) len++;
        const out = new Array(len);
        for (let x = to, i = len - 1; i >= 0; i--) { out[i] = x; x = prev[x]; }
        Path.lastCost = g[to];
        return out;
      }
      const gc = g[c], o = 6 * c;
      for (let j = 0; j < 6; j++) {
        const b = nbr[o + j];
        if (b < 0) break;
        if (mark[b] === stamp) {
          if (hp[b] < 0 || !step(c, b)) continue;              // settled, or closed from this side
          const ng = gc + OUT[0];
          if (ng < g[b]) { f[b] += ng - g[b]; g[b] = ng; prev[b] = c; hUp(hp[b], b); }
        } else {
          if (!step(c, b)) continue;
          const ng = gc + OUT[0], q = 3 * b;
          dx = pos[q] - tx; dy = pos[q + 1] - ty; dz = pos[q + 2] - tz;
          h = Math.sqrt(dx * dx + dy * dy + dz * dz) * 0.5;
          const fb = ng + scale * Math.asin(h < 1 ? h : 1);
          if (fb > maxCost) continue;                          // cannot be on a route within the budget
          mark[b] = stamp; g[b] = ng; f[b] = fb; prev[b] = c;
          hUp(hN++, b);
        }
      }
    }
    return null;
  };

  Path.reach = function (world, from, maxCost, naval, visit) {
    if (!ok(world, from) || typeof visit !== 'function') return 0;
    if (maxCost === undefined || maxCost === null || maxCost !== maxCost) maxCost = TUNE.path.maxCost;
    if (!(maxCost >= 0)) return 0;
    const mode = modeOf(naval), d = depth, nbr = world.geo.nbr;
    const S = scratch(world.n, d);
    const g = S.g, mark = S.mark, hp = S.pos, heap = S.heap, stamp = S.stamp;
    bind(world, mode);
    let mine = serial;
    hHeap = heap; hPos = hp; hKey = g; hN = 0;
    g[from] = 0; mark[from] = stamp;
    hUp(hN++, from);
    let count = 0;
    depth = d + 1;                                             // searches started by visit use their own scratch
    try {
      while (hN > 0) {
        const c = hPop(), gc = g[c], left = hN;
        count++;
        const go = visit(c, gc);
        if (serial !== mine) {                                 // visit ran a search of its own: take the heap back
          bind(world, mode); mine = serial;
          hHeap = heap; hPos = hp; hKey = g; hN = left;
        }
        if (go === false) break;
        const o = 6 * c;
        for (let j = 0; j < 6; j++) {
          const b = nbr[o + j];
          if (b < 0) break;
          if (mark[b] === stamp) {
            if (hp[b] < 0 || !step(c, b)) continue;
            const ng = gc + OUT[0];
            if (ng < g[b]) { g[b] = ng; hUp(hp[b], b); }
          } else {
            if (!step(c, b)) continue;
            const ng = gc + OUT[0];
            if (!(ng <= maxCost)) continue;
            mark[b] = stamp; g[b] = ng;
            hUp(hN++, b);
          }
        }
      }
    } finally {
      depth = d;
    }
    return count;
  };
})(globalThis.DM = globalThis.DM || {});
