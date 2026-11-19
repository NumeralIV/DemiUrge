// DEMIURGE — hydro.js : DM.Hydro (SPEC-SIM §3.4). Lakes, drainage, discharge, rivers, distance to fresh water.
//
//   Hydro.update(world)     recompute water, down, flow, world.rivers and fresh from elev, seaLevel and moist;
//                           clears need.hydro; sets need.life, dirty.rivers, dirty.water. Emits nothing, draws no rng.
//   Hydro.riverT(world)     discharge above which a land cell counts as a river (scales with the cell count)
//   Hydro.isLake(world, c)  lake cell: a land cell with standing water deeper than 0.03
//   Hydro.isRiver(world, c) river cell: a land cell that a river of world.rivers runs through (valid after update)
//
// How it works. A priority-flood from the sea over the land gives every land cell a filled level (the height
// water has to reach there before it can run off) and a drainage parent: the lowest neighbour on a slope, the
// shortest way to the outlet inside a depression or across a flat. Then, from the highest filled level down,
// every cell hands its rainfall (`moist`) plus everything it received to its downstream neighbour: that is the
// discharge, flow[]. Depressions (filled > elev) that are big or deep enough, and wet enough, become lakes; the
// others count as filled ground and simply drain through.
//
// Three choices go beyond the letter of the spec. Each has one knob in TUNE.hydro that switches it off.
//
// 1. Streams gather (pull). down[] is the flood's parent, with one correction on slopes. Two flow lines that
//    come down a smooth slope one cell apart never meet if each takes its own lowest neighbour, and both grow
//    into rivers that run side by side to the sea. So a lower neighbour that already carries a stream counts as
//    steeper than it is, by up to 1 + pull times: brooks join the bigger water beside them, as they would in the
//    valley it has cut. The stream is measured in cells drained, not in rain, so the drainage tree depends only
//    on elev and seaLevel: when the climate shifts, rivers swell or shrink in their beds, they never jump to
//    another valley. Water still only ever runs to a strictly lower neighbour. (pull = 0: the plain flood.)
//
// 2. Creeks and dry washes are left out (creek, arid). On this small planet most land lies within a few cells of
//    a coast, and "every cell with flow > riverT" rings each continent with hundreds of one- and two-cell stubs.
//    A cell above riverT is a river cell only if its river system (everything that reaches the sea through the
//    same coastal cell) carries at least creek × riverT at that mouth, and the mean rainfall of that system's
//    whole catchment is at least arid. The second half matters on a nearly dry planet (genesis, after the first
//    comet): a catchment of thousands of desert cells adds up to a mighty discharge on paper; its bed is a dry
//    wash, not a river, and it fills no lakes. (creek = 1 and arid = 0: every stream counts.)
//
// 3. Rivers are drawn from their headwaters (head). A river does not begin where its discharge happens to cross
//    riverT, halfway down the valley: from every source the course is followed upstream along the largest
//    inflow while that still carries more than head × riverT (and never less than riverMin). So rivers come down
//    from the hills, every tributary has some length, and none of this adds a single new branch. (head = 0 or
//    ≥ 1: off.) A headwater never crosses a lake, a lake's outlet, or dry ground more than lakeDepth below its
//    spill level.
//
// Lakes. A depression is a lake if it has at least 3 cells deeper than 0.03 or is at least 0.25 deep, and its
// cells' mean moist is at least 0.15 or a river (flow > riverT, in a system that counts by rule 2) runs into
// it: a dry basin on a river's course fills up, otherwise the river would have to climb out of it. So no river
// ever climbs more than the 0.25 that a pit may be deep before it counts.
//
// What other modules can rely on:
//   down[c]   -1 on every sea cell and on the outlet of a world with no sea (its lowest cell); otherwise a
//             neighbour of c whose filled level is not above c's. Following down[] from any land cell ends on a
//             sea cell (or on that outlet) and never cycles. A land cell next to the sea drains into its lowest
//             sea neighbour.
//   flow[c]   land: moist[c] + Σ flow of the cells that drain into c, so it never decreases downstream.
//             sea: Σ flow of the land cells that drain straight into it (the discharge of a river mouth), else 0.
//   water[c]  min(2.5, filled − elev) on every cell of a lake's depression, 0 everywhere else (sea included).
//             A lake has at least 3 lake cells or is at least 0.25 deep; a lake cell is never a river cell.
//   rivers    [{ cells: Int32Array, flow: Float32Array }], largest river first; flow[i] === world.flow[cells[i]].
//             cells run downstream along down[]. Every cell but the last is a river cell; the last one is the first
//             sea cell, the first lake cell, or the confluence cell on a bigger river (where two rivers meet, the
//             one carrying more water continues and the other ends). On a world with no sea the main river ends
//             on the outlet cell. A river that leaves a lake starts on the dry cell at the lake's outlet. Every
//             river cell lies on exactly one river as a non-final cell, and carries more than riverMin.
//   fresh[c]  hops (through any cells, sea included) to the nearest river cell or lake cell; 255 if there is
//             none within 254 hops. fresh[c] === 0 exactly on river cells and lake cells.
(function (DM) {
  'use strict';
  const Hydro = (DM.Hydro = DM.Hydro || {});
  const TUNE = (DM.TUNE = DM.TUNE || {});

  TUNE.hydro = {
    riverT: 5,          // discharge (sum of upstream moist) that makes a river on the reference world (L = 6)
    riverExp: 0.5,      // riverT scales with (cells / 40962) ^ riverExp ...
    riverMin: 1.2,      // ... but never drops below this (very coarse test planets); no river cell carries less
    creek: 2,           // a river system counts only if its mouth carries at least creek × riverT (1 = every stream)
    arid: 0.1,          // ... and its whole catchment has at least this mean moist (0 = off): desert washes are no rivers
    head: 0.25,         // a river's course is followed upstream of riverT while it carries more than head × riverT (0 = off)
    pull: 4,            // a lower neighbour carrying a big stream counts as up to 1 + pull times steeper (0 = off)
    pullAt: 4,          // cells drained (on the reference world) at which a stream has half of that pull
    lakeCells: 3,       // a depression is a lake if at least this many of its cells would be lake cells ...
    lakeDepth: 0.25,    // ... or it is at least this deep at its deepest point,
    lakeMoist: 0.15,    // and its cells' mean moist is at least this (or a river runs into it)
    lakeMax: 2.5,       // cap on water depth
    lakeEps: 0.03,      // a lake cell has water deeper than this
  };

  const REF_CELLS = 40962;

  // ---- scratch buffers, one set per cell count (tests use L = 4, the game L = 6) ---------------------------
  const scratchByN = {};
  function scratch(n) {
    let s = scratchByN[n];
    if (!s) {
      s = scratchByN[n] = {
        filled: new Float32Array(n),    // filled surface level of each land cell
        acc: new Float64Array(n),       // discharge accumulator
        order: new Int32Array(n),       // land cells in flood order (non-decreasing filled level, parents first)
        heap: new Int32Array(n),        // binary min-heap of cell ids keyed by filled[]
        queue: new Int32Array(n),       // FIFO of the flood (depression cells); later lake labelling, polylines, BFS
        cls: new Uint8Array(n),         // flood: visited flag; afterwards: cell class (below)
        area: new Int32Array(n),        // land cells drained through each cell (itself included)
        big: new Uint8Array(n),         // 1 on land cells whose river system counts (more than a creek, not a dry wash)
        main: new Int32Array(n),        // per river cell: the upstream river cell that carries the most water, or -1
      };
    }
    return s;
  }

  // ---- binary min-heap of cell ids ordered by (key, id) ----------------------------------------------------
  let hN = 0, hHeap = null, hKey = null;
  function hPush(c) {
    const heap = hHeap, key = hKey, k = key[c];
    let i = hN++;
    while (i > 0) {
      const p = (i - 1) >> 1, pc = heap[p], pk = key[pc];
      if (pk < k || (pk === k && pc < c)) break;
      heap[i] = pc;
      i = p;
    }
    heap[i] = c;
  }
  function hPop() {
    const heap = hHeap, key = hKey, top = heap[0], c = heap[--hN];
    if (hN > 0) {
      const k = key[c];
      let i = 0;
      for (;;) {
        let l = 2 * i + 1;
        if (l >= hN) break;
        let lc = heap[l], lk = key[lc];
        const r = l + 1;
        if (r < hN) {
          const rc = heap[r], rk = key[rc];
          if (rk < lk || (rk === lk && rc < lc)) { l = r; lc = rc; lk = rk; }
        }
        if (k < lk || (k === lk && c < lc)) break;
        heap[i] = lc;
        i = l;
      }
      heap[i] = c;
    }
    return top;
  }

  // cell classes after the flood: 0 = sea, LAND = plain land, DEP = depression cell not yet labelled,
  // LAKE = lake cell, RIVER = river cell
  const LAND = 1, DEP = 2, LAKE = 3, RIVER = 4;

  // largest river first: by the discharge that enters the river's final cell (which belongs to whatever the river
  // flows into); ties by the id of the source cell
  function byDischarge(a, b) {
    return (b.flow[b.flow.length - 2] - a.flow[a.flow.length - 2]) || (a.cells[0] - b.cells[0]);
  }

  // riverT before the riverMin floor
  function scaledT(t, n) {
    const r = n / REF_CELLS;
    return t.riverT * (t.riverExp === 0.5 ? Math.sqrt(r) : Math.pow(r, t.riverExp));
  }

  Hydro.riverT = function (world) {
    const t = TUNE.hydro, v = scaledT(t, world && world.n > 0 ? world.n : REF_CELLS);
    return v > t.riverMin ? v : t.riverMin;
  };

  Hydro.isLake = function (world, c) {
    return world.water[c] > TUNE.hydro.lakeEps && world.elev[c] >= world.seaLevel;
  };

  Hydro.isRiver = function (world, c) {
    return world.fresh[c] === 0 && !(world.water[c] > TUNE.hydro.lakeEps) && world.elev[c] >= world.seaLevel;
  };

  Hydro.update = function (world) {
    const t = TUNE.hydro, n = world.n, nbr = world.geo.nbr, sea = world.seaLevel;
    const elev = world.elev, water = world.water, flow = world.flow, down = world.down, moist = world.moist, fresh = world.fresh;
    const s = scratch(n), filled = s.filled, acc = s.acc, order = s.order, heap = s.heap, queue = s.queue, cls = s.cls, big = s.big, main = s.main;
    const RT = Hydro.riverT(world);
    const rivers = [];

    // ---- 1. priority-flood from the sea ------------------------------------------------------------------
    // cls doubles as the visited flag here: 1 = sea, or land that already has a parent.
    let nLand = 0;
    for (let c = 0; c < n; c++) {
      down[c] = -1;
      water[c] = 0;
      if (elev[c] < sea) { cls[c] = 1; acc[c] = 0; } else {
        cls[c] = 0; nLand++;
        const m = moist[c];
        acc[c] = m > 0 && m < Infinity ? m : 0;            // NaN, negative and infinite rainfall count as none
      }
    }
    hN = 0; hHeap = heap; hKey = filled;
    if (nLand === n && n > 0) {
      // no sea: the lowest cell is the outlet
      let o = 0;
      for (let c = 1; c < n; c++) if (elev[c] < elev[o]) o = c;
      cls[o] = 1; filled[o] = elev[o];
      hPush(o);
    } else if (nLand > 0) {
      // coastal land cells drain into their lowest sea neighbour
      for (let c = 0; c < n; c++) {
        if (cls[c]) continue;
        const o = 6 * c;
        let best = -1, be = 0;
        for (let k = 0; k < 6; k++) {
          const b = nbr[o + k];
          if (b < 0) break;
          const e = elev[b];
          if (e < sea && (best < 0 || e < be)) { best = b; be = e; }
        }
        if (best >= 0) { cls[c] = 1; down[c] = best; filled[c] = elev[c]; hPush(c); }
      }
    }
    // Cells at or below the level being flooded (depressions and flats) go through a FIFO instead of the
    // heap, so water crosses them by the shortest way to their outlet. Heap cells at the level of the FIFO's
    // head go first: they are further outlets of the same flat.
    let cnt = 0, qh = 0, qt = 0;
    while (qh < qt || hN > 0) {
      const c = qh < qt && !(hN > 0 && filled[heap[0]] <= filled[queue[qh]]) ? queue[qh++] : hPop();
      order[cnt++] = c;
      const f = filled[c], o = 6 * c;
      for (let k = 0; k < 6; k++) {
        const b = nbr[o + k];
        if (b < 0) break;
        if (cls[b]) continue;
        cls[b] = 1;
        down[b] = c;
        const e = elev[b];
        if (e <= f) { filled[b] = f; queue[qt++] = b; } else { filled[b] = e; hPush(b); }
      }
    }
    hHeap = null; hKey = null;

    // ---- 2. flow directions and discharge, from the highest filled level down ----------------------------
    // A cell's downstream neighbour always comes before it in flood order (the flood's parent by construction,
    // any other strictly lower neighbour because the flood takes levels in order), so one reverse sweep is
    // enough and there can be no cycle. When a slope cell's turn comes, area[] of a lower neighbour is the
    // catchment that already drains through it from higher ground.
    const pull = t.pull > 0 ? t.pull : 0, pullRef = (t.pullAt > 0 ? t.pullAt : 1) * n / REF_CELLS, area = s.area;
    for (let c = 0; c < n; c++) area[c] = elev[c] < sea ? 0 : 1;
    if (pull > 0) {
      for (let i = cnt - 1; i >= 0; i--) {
        const c = order[i];
        let p = down[c];
        if (p < 0) continue;
        const f = filled[c], d0 = f - filled[p];
        if (d0 > 0 && !(elev[p] < sea)) {                  // a slope cell: no sea neighbour, parent strictly lower
          const o = 6 * c, a0 = area[p];
          let best = d0 * (1 + pull * a0 / (a0 + pullRef));
          for (let k = 0; k < 6; k++) {
            const b = nbr[o + k];
            if (b < 0) break;
            const d = f - filled[b];
            if (!(d > 0)) continue;
            const a = area[b], sc = d * (1 + pull * a / (a + pullRef));
            if (sc > best) { best = sc; p = b; }
          }
          down[c] = p;
        }
        acc[p] += acc[c];
        area[p] += area[c];
      }
    } else {
      for (let i = cnt - 1; i >= 0; i--) { const c = order[i], p = down[c]; if (p >= 0) { acc[p] += acc[c]; area[p] += area[c]; } }
    }
    for (let c = 0; c < n; c++) { flow[c] = acc[c]; cls[c] = elev[c] < sea ? 0 : LAND; }

    // ---- 3. river systems that count: more than a creek, and not the wash of a desert -------------------
    // Parents first, so a cell can copy the verdict from its parent; a system's mouth is its last land cell.
    // All thresholds are tested on the stored (float32) discharge.
    const cr = (t.creek > 0 ? t.creek : 0) * scaledT(t, n), creek = cr > RT ? cr : RT, arid = t.arid > 0 ? t.arid : 0;
    for (let i = 0; i < cnt; i++) {
      const c = order[i], p = down[c];
      big[c] = p < 0 || cls[p] === 0 ? (flow[c] >= creek && flow[c] >= arid * area[c] ? 1 : 0) : big[p];
    }

    // ---- 4. lakes: connected depressions that are big or deep enough, and wet enough ---------------------
    // Neighbouring depression cells always share one filled level, so a component is one basin. Its size is
    // counted in cells that would hold more than lakeEps of water, so a lake never comes out as a mere film.
    const lakeMax = t.lakeMax, lakeEps = t.lakeEps, lakeDepth = t.lakeDepth;
    for (let i = 0; i < cnt; i++) { const c = order[i]; if (filled[c] > elev[c]) cls[c] = DEP; }
    for (let i = 0; i < cnt; i++) {
      const c0 = order[i];
      if (cls[c0] !== DEP) continue;
      let m = 0, wetCells = 0, depth = 0, sumMoist = 0, fed = false;
      cls[c0] = LAND; queue[m++] = c0;
      for (let h = 0; h < m; h++) {
        const c = queue[h], d = filled[c] - elev[c], o = 6 * c, r = moist[c];
        if (d > depth) depth = d;
        if (Math.fround(d) > lakeEps) wetCells++;          // as water[] will store it
        if (r > 0 && r < Infinity) sumMoist += r;
        if (big[c] && flow[c] > RT) fed = true;
        for (let k = 0; k < 6; k++) {
          const b = nbr[o + k];
          if (b < 0) break;
          if (cls[b] === DEP) { cls[b] = LAND; queue[m++] = b; }
        }
      }
      if ((wetCells >= t.lakeCells || depth >= lakeDepth) && (fed || sumMoist >= t.lakeMoist * m)) {
        for (let h = 0; h < m; h++) {
          const c = queue[h], d = filled[c] - elev[c];
          water[c] = d < lakeMax ? d : lakeMax;
          if (water[c] > lakeEps) cls[c] = LAKE;
        }
      }
    }

    // ---- 5. river cells ----------------------------------------------------------------------------------
    for (let i = 0; i < cnt; i++) {
      const c = order[i];
      main[c] = -1;
      if (big[c] && cls[c] === LAND && flow[c] > RT) cls[c] = RIVER;
    }
    // the upstream river cell that brings the most water into each river cell (ties: lowest id)
    for (let c = 0; c < n; c++) {
      if (cls[c] !== RIVER) continue;
      const d = down[c];
      if (d < 0 || cls[d] !== RIVER) continue;
      const m = main[d];
      if (m < 0 || flow[c] > flow[m]) main[d] = c;
    }
    // headwaters: from every source (a river cell with no river cell upstream), follow the largest inflow
    // upstream while it carries more than headT. Never past a lake or a lake's outlet (the lake is the source),
    // never onto dry ground lakeDepth or more below its spill level (the stream would have to climb out).
    let headT = (t.head > 0 ? t.head : 0) * RT;
    if (headT < t.riverMin) headT = t.riverMin;
    if (t.head > 0 && headT < RT) {
      let m = 0;
      for (let c = 0; c < n; c++) if (cls[c] === RIVER && main[c] < 0) queue[m++] = c;
      for (let i = 0; i < m; i++) {
        let cur = queue[i];
        for (;;) {
          const o = 6 * cur;
          let up = -1, uf = headT;
          for (let k = 0; k < 6; k++) {
            const b = nbr[o + k];
            if (b < 0) break;
            if (down[b] !== cur) continue;
            if (cls[b] === LAKE) { up = -1; break; }
            if (cls[b] === LAND) { const fb = flow[b]; if (fb > uf || (fb === uf && up >= 0 && b < up)) { up = b; uf = fb; } }
          }
          if (up < 0 || filled[up] - elev[up] >= lakeDepth) break;
          cls[up] = RIVER; main[cur] = up;
          cur = up;
        }
      }
    }

    // ---- 6. one polyline from every source ---------------------------------------------------------------
    for (let c = 0; c < n; c++) {
      if (cls[c] !== RIVER || main[c] >= 0) continue;
      let len = 0, cur = c;
      queue[len++] = c;
      for (;;) {
        const d = down[cur];
        if (d < 0) break;                                  // the outlet of a world with no sea
        queue[len++] = d;
        if (cls[d] !== RIVER || main[d] !== cur) break;    // sea, lake, or a confluence with a bigger branch
        cur = d;
      }
      if (len < 2) { cls[c] = LAND; continue; }            // a lone outlet cell is no river
      const cells = new Int32Array(len), fl = new Float32Array(len);
      for (let i = 0; i < len; i++) { const x = queue[i]; cells[i] = x; fl[i] = flow[x]; }
      rivers.push({ cells, flow: fl });
    }
    rivers.sort(byDischarge);

    // ---- 7. hops to the nearest fresh water (river cell or lake cell) ------------------------------------
    qh = 0; qt = 0;
    for (let c = 0; c < n; c++) {
      if (cls[c] === LAKE || cls[c] === RIVER) { fresh[c] = 0; queue[qt++] = c; } else fresh[c] = 255;
    }
    while (qh < qt) {
      const c = queue[qh++], d = fresh[c] + 1, o = 6 * c;
      if (d > 254) break;                                  // BFS order: everything still queued is at least this far
      for (let k = 0; k < 6; k++) {
        const b = nbr[o + k];
        if (b < 0) break;
        if (fresh[b] === 255) { fresh[b] = d; queue[qt++] = b; }
      }
    }

    world.rivers = rivers;
    if (world.need) { world.need.hydro = false; world.need.life = true; }
    if (world.dirty) { world.dirty.rivers = true; world.dirty.water = true; }
  };
})(globalThis.DM = globalThis.DM || {});
