// DEMIURGE — tests/path.test.js : DM.Path (SPEC-SIM §3.6, BUILD.md A6).
//
//   node tests/path.test.js                 run the tests
//   PATH_REPORT=1 node tests/path.test.js   also print the measured numbers (search sizes, timings)
//   PATH_MAPS=1 node tests/path.test.js     also write shots/path-routes.png and shots/path-reach.png
//                                           (real terrain when terrain.js exists, else a helper planet)
//
// Every expectation is derived here, independently of path.js: the documented cost rule is written out again
// (refCost) and cheapest costs come from a plain Dijkstra over it, itself cross-checked against Bellman–Ford
// sweeps. Only the foundation, schema.js and path.js are needed; every other sim module is stubbed away.
'use strict';
const T = require('../tools/t.js');
const DM = require('../tools/load.js')({ only: ['path'] });        // strict: a broken path.js fails here
const H = require('./helpers/world.js');                            // (its loader pulls in whatever else exists ...)
const REAL = { Terrain: DM.Terrain };                               // ... kept only for the optional maps
for (const k of ['Names', 'Terrain', 'Climate', 'Hydro', 'Life', 'Civ', 'Society', 'Units', 'Powers', 'Chronicle', 'Sim']) T.stub(DM, k, undefined);
T.watchEvents();

const Path = DM.Path;
const REPORT = !!process.env.PATH_REPORT || !!process.env.PATH_MAPS;
const say = (...a) => { if (REPORT) console.log('    ·', ...a); };
const now = () => Number(process.hrtime.bigint()) / 1e6;
const EPS = 1e-9;

// ------------------------------------------------------------------ the documented rule, written out again
function refCost(w, a, b, naval) {
  const sea = w.seaLevel, ea = w.elev[a], eb = w.elev[b];
  if (w.lava[b] > 0.05) return Infinity;                            // lava is impassable
  const aSea = ea < sea, bSea = eb < sea;
  if (bSea) {
    if (naval !== true && naval !== 'coastal') return Infinity;     // land only
    if (w.ice[b] > 0.6) return Infinity;                            // sea ice
    if (naval === 'coastal' && !(sea - eb < 0.6)) return Infinity;  // canoes keep to the shelves
    if (!aSea) return 3;                                            // embarking
    return naval === true ? 0.35 : 0.6;
  }
  if (aSea) return 3;                                               // landing
  let c = 1 + 6 * Math.abs((eb + w.water[b]) - (ea + w.water[a]));
  if (w.veg[b] >= 0.6) c += 0.4;                                    // dense forest
  if (w.ice[b] > 0.5) c += 3;
  if (w.water[b] > 0.03) c += 2;                                    // lake cell
  c -= 0.25 * Math.min(w.road[a], w.road[b]);
  return Math.max(0.3, c);
}

// plain Dijkstra: a binary heap of [cost, cell] pairs with lazy deletion
function dijkstra(w, from, naval) {
  const n = w.n, g = w.geo, dist = new Float64Array(n).fill(Infinity), done = new Uint8Array(n);
  const heap = [];
  const less = (x, y) => x[0] < y[0] || (x[0] === y[0] && x[1] < y[1]);
  const push = (e) => {
    let i = heap.length; heap.push(e);
    while (i > 0) { const p = (i - 1) >> 1; if (!less(heap[i], heap[p])) break; const t = heap[i]; heap[i] = heap[p]; heap[p] = t; i = p; }
  };
  const pop = () => {
    const top = heap[0], e = heap.pop();
    if (heap.length) {
      heap[0] = e;
      let i = 0;
      for (;;) {
        let m = i; const l = 2 * i + 1, r = l + 1;
        if (l < heap.length && less(heap[l], heap[m])) m = l;
        if (r < heap.length && less(heap[r], heap[m])) m = r;
        if (m === i) break;
        const t = heap[i]; heap[i] = heap[m]; heap[m] = t; i = m;
      }
    }
    return top;
  };
  dist[from] = 0; push([0, from]);
  while (heap.length) {
    const e = pop(), c = e[1];
    if (done[c]) continue;
    done[c] = 1;
    for (let k = 0; k < g.nbrN[c]; k++) {
      const b = g.nbr[6 * c + k], d = e[0] + refCost(w, c, b, naval);
      if (d < dist[b]) { dist[b] = d; push([d, b]); }
    }
  }
  return dist;
}
// Bellman–Ford sweeps: no queue to get wrong
function bellman(w, from, naval) {
  const n = w.n, g = w.geo, dist = new Float64Array(n).fill(Infinity);
  dist[from] = 0;
  for (let changed = true; changed;) {
    changed = false;
    for (let c = 0; c < n; c++) {
      if (dist[c] === Infinity) continue;
      for (let k = 0; k < g.nbrN[c]; k++) {
        const b = g.nbr[6 * c + k], d = dist[c] + refCost(w, c, b, naval);
        if (d < dist[b]) { dist[b] = d; changed = true; }
      }
    }
  }
  return dist;
}
function hops(w, from) {                                            // neighbour hops from a cell (plain BFS)
  const g = w.geo, d = new Int32Array(w.n).fill(-1), q = [from];
  d[from] = 0;
  for (let h = 0; h < q.length; h++) { const c = q[h]; for (let k = 0; k < g.nbrN[c]; k++) { const b = g.nbr[6 * c + k]; if (d[b] < 0) { d[b] = d[c] + 1; q.push(b); } } }
  return d;
}
function isNbr(w, a, b) { const g = w.geo; for (let k = 0; k < g.nbrN[a]; k++) if (g.nbr[6 * a + k] === b) return true; return false; }
// a route is a chain of neighbours from → to; returns its cost by the reference rule
function routeCost(w, p, from, to, naval) {
  T.ok(Array.isArray(p), 'route is a plain array');
  T.eq(p[0], from, 'route starts at from');
  T.eq(p[p.length - 1], to, 'route ends at to');
  let sum = 0;
  for (let i = 1; i < p.length; i++) {
    T.ok(Number.isInteger(p[i]) && isNbr(w, p[i - 1], p[i]), 'route step ' + i + ' joins neighbours');
    sum += refCost(w, p[i - 1], p[i], naval);
  }
  T.ok(new Set(p).size === p.length, 'route never visits a cell twice');
  return sum;
}
const isSea = (w, c) => w.elev[c] < w.seaLevel;
const NAVALS = [false, 'coastal', true];

// ------------------------------------------------------------------ world builders
// level land everywhere at +0.5, nothing on it: every step costs exactly 1
function flat(L) {
  const w = DM.State.create({ seed: 1, L: L === undefined ? 4 : L });
  w.elev.fill(0.5); w.seaLevel = 0; w.heat = 0; w.phase = 'history';
  return w;
}
// a helper planet carrying every kind of obstacle the rule knows
function decorated(seed, L) {
  const w = H.makeWorld({ L: L === undefined ? 4 : L, seed }), g = w.geo, n = w.n, rng = DM.U.rng(1000 + seed);
  for (let c = 0; c < n; c++) {
    const sea = isSea(w, c);
    if (sea && Math.abs(g.lat[c]) > 1.05) w.ice[c] = 0.8;                         // polar pack ice
    if (sea && Math.abs(g.lat[c]) > 0.9 && Math.abs(g.lat[c]) <= 1.05) w.ice[c] = 0.4;   // thin ice: still navigable
  }
  for (let k = 0; k < 14; k++) {                                                  // lakes: small level ponds
    const c = rng.int(0, n - 1);
    if (isSea(w, c)) continue;
    const cells = []; g.ring(c, 1, cells);
    for (const x of cells) if (!isSea(w, x)) w.water[x] = 0.1 + 0.3 * rng.f();
  }
  for (let k = 0; k < 10; k++) {                                                  // lava fields
    const cells = []; g.ring(rng.int(0, n - 1), 1, cells);
    for (const x of cells) if (rng.chance(0.7)) w.lava[x] = 0.06 + rng.f();
  }
  for (let k = 0; k < 10; k++) {                                                  // roads: wandering tracks of every level
    let c = rng.int(0, n - 1); const lvl = 1 + (k % 3);
    for (let s = 0; s < 40; s++) { if (!isSea(w, c)) w.road[c] = Math.max(w.road[c], lvl); c = g.nbr[6 * c + rng.int(0, g.nbrN[c] - 1)]; }
  }
  return w;
}
// level land with a sea channel between two halves: cells with |x| < half are sea of the given depth
function channel(depth, half, L) {
  const w = flat(L), g = w.geo;
  for (let c = 0; c < w.n; c++) if (Math.abs(g.pos[3 * c]) < half) w.elev[c] = -depth;
  return w;
}

// ------------------------------------------------------------------ the cost rule
T.test('TUNE.path carries the documented numbers', () => {
  const P = DM.TUNE.path;
  T.eq([P.land, P.slope, P.forest, P.ice, P.lake, P.road, P.floor, P.coastal, P.shallow, P.sea, P.embark, P.seaIce, P.maxCost],
    [1, 6, 0.4, 3, 2, 0.25, 0.3, 0.6, 0.6, 0.35, 3, 0.6, 400]);
});

T.test('stepCost: land steps (level, slope, forest, ice, lake, road, floor)', () => {
  const w = flat(), g = w.geo, a = 100, b = g.nbr[6 * a], c = g.nbr[6 * a + 3];
  for (const nv of NAVALS) T.near(Path.stepCost(w, a, b, nv), 1, EPS, 'level land');
  w.elev[b] = 0.75;
  T.near(Path.stepCost(w, a, b, false), 2.5, 1e-6, 'uphill 0.25');
  T.near(Path.stepCost(w, b, a, false), 2.5, 1e-6, 'downhill 0.25');
  w.elev[b] = 0.5;
  w.veg[b] = 0.59; T.near(Path.stepCost(w, a, b, false), 1, EPS, 'open woodland');
  w.veg[b] = 0.7;
  T.near(Path.stepCost(w, a, b, false), 1.4, 1e-6, 'into dense forest');
  T.near(Path.stepCost(w, b, a, false), 1, EPS, 'out of the forest');
  w.veg[b] = 0;
  w.ice[b] = 0.8; T.near(Path.stepCost(w, a, b, false), 4, 1e-6, 'onto ice');
  w.ice[b] = 0.4; T.near(Path.stepCost(w, a, b, false), 1, EPS, 'thin snow');
  w.ice[b] = 0;
  w.elev[b] = 0.3; w.water[b] = 0.2;                               // a lake whose surface is level with the land
  T.near(Path.stepCost(w, a, b, false), 3, 1e-6, 'into a lake');
  T.near(Path.stepCost(w, b, a, false), 1, 1e-6, 'out of a lake');
  w.elev[b] = 0.5;                                                 // lake surface 0.2 above the shore
  T.near(Path.stepCost(w, a, b, false), 1 + 6 * 0.2 + 2, 1e-5, 'lake above the shore: slope uses the surface');
  w.water[b] = 0.02; T.near(Path.stepCost(w, a, b, false), 1 + 6 * 0.02, 1e-5, 'a puddle is not a lake');
  w.water[b] = 0;
  w.road[a] = 1; T.near(Path.stepCost(w, a, b, false), 1, EPS, 'a road at one end only does not help');
  w.road[b] = 1; T.near(Path.stepCost(w, a, b, false), 0.75, EPS, 'road level 1');
  w.road[a] = 2; T.near(Path.stepCost(w, a, b, false), 0.75, EPS, 'the lower level counts');
  w.road[b] = 2; T.near(Path.stepCost(w, a, b, false), 0.5, EPS, 'road level 2');
  w.road[a] = 3; w.road[b] = 3; T.near(Path.stepCost(w, a, b, false), 0.3, EPS, 'road level 3: the floor');
  w.elev[b] = 0.6; T.near(Path.stepCost(w, a, b, false), 1 + 6 * 0.1 - 0.75, 1e-5, 'road on a slope');
  w.elev[b] = 0.5; w.road[a] = 0; w.road[b] = 0;
  T.eq(Path.stepCost(w, a, a, false), 0, 'a === b');
  T.eq(Path.stepCost(w, a, c, false), 1, 'another neighbour');
  let far = -1; for (let x = 0; x < w.n; x++) if (x !== a && !isNbr(w, a, x)) { far = x; break; }
  T.eq(Path.stepCost(w, a, far, true), Infinity, 'not a neighbour');
  for (const bad of [-1, w.n, 1.5, NaN, undefined, null, '3']) {
    T.eq(Path.stepCost(w, a, bad, false), Infinity, 'bad cell ' + bad);
    T.eq(Path.stepCost(w, bad, a, false), Infinity, 'bad cell ' + bad);
  }
  T.eq(Path.stepCost(null, a, b, false), Infinity, 'no world');
});

T.test('stepCost: sea, embarking and landing, sea ice, lava', () => {
  const w = flat(), g = w.geo, a = 200, b = g.nbr[6 * a], c = g.nbr[6 * b + 0] === a ? g.nbr[6 * b + 1] : g.nbr[6 * b + 0];
  w.elev[b] = -0.3; w.elev[c] = -0.5;                              // b, c shallow sea; a land
  T.eq(Path.stepCost(w, a, b, false), Infinity, 'no boats: the sea is closed');
  T.eq(Path.stepCost(w, a, b, 'coastal'), 3, 'embark (coastal)');
  T.eq(Path.stepCost(w, a, b, true), 3, 'embark (naval)');
  T.eq(Path.stepCost(w, b, a, 'coastal'), 3, 'landing (coastal)');
  T.eq(Path.stepCost(w, b, a, true), 3, 'landing (naval)');
  T.eq(Path.stepCost(w, b, a, false), 3, 'a party caught in the water can wade out');
  T.eq(Path.stepCost(w, b, c, false), Infinity, 'but not swim on');
  T.eq(Path.stepCost(w, b, c, 'coastal'), 0.6, 'canoe step');
  T.eq(Path.stepCost(w, b, c, true), 0.35, 'ship step');
  T.eq(Path.stepCost(w, b, c, 1), Infinity, 'any other naval value means land only');
  T.eq(Path.stepCost(w, b, c, 'yes'), Infinity, 'any other naval value means land only');
  w.elev[c] = -0.7;                                                // deep water
  T.eq(Path.stepCost(w, b, c, 'coastal'), Infinity, 'too deep for canoes');
  T.eq(Path.stepCost(w, b, c, true), 0.35, 'ships do not care');
  T.eq(Path.stepCost(w, c, b, 'coastal'), 0.6, 'a canoe may come back onto the shelf');
  w.seaLevel = 0.15; w.elev[a] = 0.9;                              // the rule follows seaLevel, not the datum
  w.elev[c] = -0.4;
  T.eq(Path.stepCost(w, b, c, 'coastal'), 0.6, 'depth 0.55 under a raised sea');
  w.elev[c] = -0.5;
  T.eq(Path.stepCost(w, b, c, 'coastal'), Infinity, 'depth 0.65 under a raised sea');
  w.seaLevel = 0; w.elev[c] = -0.5;
  w.ice[c] = 0.61;
  T.eq(Path.stepCost(w, b, c, 'coastal'), Infinity, 'pack ice (coastal)');
  T.eq(Path.stepCost(w, b, c, true), Infinity, 'pack ice (naval)');
  w.ice[c] = 0.5;
  T.eq(Path.stepCost(w, b, c, true), 0.35, 'thin sea ice is open water');
  w.ice[c] = 0;
  w.veg[b] = 0.9; w.road[a] = 3; w.road[b] = 3; w.elev[a] = 7;     // nothing on land changes the shore step
  T.eq(Path.stepCost(w, a, b, true), 3, 'embark is flat');
  T.eq(Path.stepCost(w, b, a, true), 3, 'landing is flat');
  w.lava[a] = 0.2; w.lava[c] = 0.2;
  for (const nv of NAVALS) {
    T.eq(Path.stepCost(w, b, a, nv), Infinity, 'lava on land');
    T.eq(Path.stepCost(w, b, c, nv), Infinity, 'lava in the sea');
  }
  w.lava[a] = 0.04;
  T.eq(Path.stepCost(w, b, a, true), 3, 'a cooled crust can be crossed');
  w.lava[b] = 1;
  T.eq(Path.stepCost(w, b, a, true), 3, 'leaving a lava cell is allowed');
});

T.test('stepCost agrees with the reference rule on every edge of a decorated planet', () => {
  const w = decorated(3), g = w.geo;
  let edges = 0, shut = 0;
  for (const nv of NAVALS) {
    for (let a = 0; a < w.n; a++) for (let k = 0; k < g.nbrN[a]; k++) {
      const b = g.nbr[6 * a + k], r = refCost(w, a, b, nv), p = Path.stepCost(w, a, b, nv);
      edges++;
      if (r === Infinity) { shut++; T.eq(p, Infinity, 'edge ' + a + '→' + b); }
      else T.near(p, r, 1e-9, 'edge ' + a + '→' + b + ' naval ' + nv);
    }
  }
  say('reference rule checked on', edges, 'directed edges,', shut, 'impassable');
  T.ok(shut > 1000 && shut < edges - 1000, 'the planet has both open and closed steps');
});

// ------------------------------------------------------------------ find
T.test('the reference Dijkstra itself matches Bellman–Ford', () => {
  const w = decorated(2);
  for (const nv of NAVALS) for (const from of [5, 700, 2100]) {
    const d = dijkstra(w, from, nv), b = bellman(w, from, nv);
    for (let c = 0; c < w.n; c++) {
      if (b[c] === Infinity) T.eq(d[c], Infinity);
      else T.near(d[c], b[c], 1e-9);
    }
  }
});

T.test('find is optimal: 200 random pairs per mode against plain Dijkstra (L 4, 4 planets)', () => {
  const worlds = [1, 2, 3, 4].map(s => decorated(s));
  for (const nv of NAVALS) {
    const rng = DM.U.rng(77 + NAVALS.indexOf(nv));
    let reached = 0, none = 0, steps = 0, maxC = 0, roadUse = 0;
    for (let i = 0; i < 200; i++) {
      const w = worlds[i % worlds.length], n = w.n;
      let from = rng.int(0, n - 1), to = rng.int(0, n - 1);
      if (i % 4 !== 3) {                                           // three in four: land to land
        while (isSea(w, from)) from = rng.int(0, n - 1);
        while (isSea(w, to)) to = rng.int(0, n - 1);
      }
      const dist = dijkstra(w, from, nv);
      const p = Path.find(w, from, to, { naval: nv, maxCost: 1e9 });
      if (from === to) { T.eq(p, [from]); continue; }
      if (dist[to] === Infinity) { T.eq(p, null, 'no route exists ' + from + '→' + to); none++; continue; }
      T.ok(p, 'route ' + from + '→' + to + ' naval ' + nv + ' (cost ' + dist[to] + ')');
      const c = routeCost(w, p, from, to, nv);
      T.near(c, dist[to], 1e-7, 'cheapest ' + from + '→' + to + ' naval ' + nv);
      T.near(Path.lastCost, dist[to], 1e-7, 'lastCost');
      T.near(Path.cost(w, p, nv), dist[to], 1e-7, 'Path.cost');
      reached++; steps += p.length - 1; if (c > maxC) maxC = c;
      for (let j = 1; j < p.length; j++) if (w.road[p[j]] && w.road[p[j - 1]]) { roadUse++; break; }
      // the default budget (400) and a budget just above / just below the true cost
      const d = Path.find(w, from, to, { naval: nv });
      if (dist[to] <= 400 - 1e-6) T.near(routeCost(w, d, from, to, nv), dist[to], 1e-7, 'default budget');
      else if (dist[to] > 400 + 1e-6) T.eq(d, null, 'beyond the default budget');
      T.ok(Path.find(w, from, to, { naval: nv, maxCost: dist[to] * (1 + 1e-6) + 1e-6 }), 'budget just above the cost');
      T.eq(Path.find(w, from, to, { naval: nv, maxCost: dist[to] * (1 - 1e-6) - 1e-6 }), null, 'budget just below the cost');
    }
    say('naval', String(nv).padEnd(7), 'reached', reached, 'unreachable', none, 'mean steps', (steps / Math.max(1, reached)).toFixed(1),
      'max cost', maxC.toFixed(1), 'routes using a road', roadUse);
    T.ok(reached >= 40, 'enough reachable pairs to mean something (naval ' + nv + '): ' + reached);
    if (nv === false) T.ok(none >= 20, 'and enough unreachable ones: ' + none);
  }
});

T.test('find: a route along a road is found at road prices (the lower bound respects roads)', () => {
  const w = flat(), g = w.geo, d0 = hops(w, 0);
  // a level-3 road from the north pole to the south pole, built along a hop-shortest chain
  let c = 11; const chain = [c];
  while (c !== 0) { for (let k = 0; k < g.nbrN[c]; k++) { const b = g.nbr[6 * c + k]; if (d0[b] === d0[c] - 1) { c = b; break; } } chain.push(c); }
  for (const x of chain) w.road[x] = 3;
  const p = Path.find(w, 0, 11);
  T.near(routeCost(w, p, 0, 11, false), 0.3 * (chain.length - 1), 1e-9, 'pole to pole on the road');
  T.near(Path.lastCost, dijkstra(w, 0, false)[11], 1e-9);
  // leaving the road for a short cut is still found when it is cheaper
  const side = 1300, dist = dijkstra(w, side, false);
  for (const to of [11, 0, 900, 2000]) T.near(routeCost(w, Path.find(w, side, to), side, to, false), dist[to], 1e-9);
});

T.test('find: [from] when from = to, null for bad cells, never throws', () => {
  const w = channel(2, 0.2), land = H.findLand(w);
  let sea = -1; for (let c = 0; c < w.n && sea < 0; c++) if (isSea(w, c)) sea = c;
  T.eq(Path.find(w, land, land), [land]);
  T.eq(Path.find(w, sea, sea), [sea], 'even on a cell the mode cannot enter');
  T.eq(Path.find(w, land, land, { naval: true, maxCost: 0 }), [land]);
  T.eq(Path.lastCost, 0);
  for (const bad of [-1, w.n, 2.5, NaN, undefined, null, '7', {}]) {
    T.eq(Path.find(w, bad, land), null, 'bad from ' + bad);
    T.eq(Path.find(w, land, bad), null, 'bad to ' + bad);
  }
  T.eq(Path.find(null, 0, 1), null);
  T.eq(Path.find({}, 0, 1), null);
  const other = (() => { for (let c = 0; c < w.n; c++) if (!isSea(w, c) && c !== land && isNbr(w, land, c)) return c; return -1; })();
  T.eq(Path.find(w, land, other, null), [land, other], 'null options');
  T.eq(Path.find(w, land, other, {}), [land, other], 'empty options');
  T.eq(Path.find(w, land, other, { maxCost: NaN }), [land, other], 'NaN budget falls back to the default');
  T.eq(Path.find(w, land, other, { maxCost: -1 }), null, 'a negative budget reaches nothing');
  T.eq(Path.find(w, land, other, { maxCost: 0.99 }), null);
  T.eq(Path.find(w, land, other, { maxCost: 1 }), [land, other]);
  const r1 = Path.find(w, land, other);
  r1.push(5);                                                      // the result is the caller's own array
  T.eq(Path.find(w, land, other), [land, other]);
});

T.test('find: land only / coastal / naval across a channel; embark and landing are paid once each', () => {
  const w = channel(0.4, 0.22), g = w.geo;                         // a shallow channel between two land halves
  let west = -1, east = -1;
  for (let c = 0; c < w.n; c++) {
    if (Math.abs(g.pos[3 * c + 1]) > 0.2) continue;                // near the equator
    if (g.pos[3 * c] < -0.6 && west < 0) west = c;
    if (g.pos[3 * c] > 0.6 && east < 0) east = c;
  }
  T.eq(Path.find(w, west, east), null, 'on foot there is no way across');
  T.eq(Path.find(w, west, east, { naval: false, maxCost: 1e9 }), null);
  for (const nv of ['coastal', true]) {
    const p = Path.find(w, west, east, { naval: nv }), dist = dijkstra(w, west, nv);
    T.near(routeCost(w, p, west, east, nv), dist[east], 1e-9, 'cheapest crossing');
    let embark = 0, landing = 0, seaSteps = 0, landSteps = 0;
    for (let i = 1; i < p.length; i++) {
      const sa = isSea(w, p[i - 1]), sb = isSea(w, p[i]);
      if (!sa && sb) embark++; else if (sa && !sb) landing++; else if (sa && sb) seaSteps++; else landSteps++;
    }
    T.eq([embark, landing], [1, 1], 'one embarkation, one landing');
    T.near(Path.lastCost, landSteps + 6 + seaSteps * (nv === true ? 0.35 : 0.6), 1e-9, 'land 1 + embark 3 + sea steps + landing 3');
    say('channel', String(nv).padEnd(7), 'land steps', landSteps, 'sea steps', seaSteps, 'cost', Path.lastCost.toFixed(2));
  }
  // the same channel, too deep for canoes
  const deep = channel(1.5, 0.22);
  T.eq(Path.find(deep, west, east, { naval: 'coastal' }), null, 'canoes cannot cross deep water');
  T.ok(Path.find(deep, west, east, { naval: true }), 'ships can');
  // a shelf along both shores does not help: the middle is still deep
  for (let c = 0; c < deep.n; c++) { const x = Math.abs(deep.geo.pos[3 * c]); if (x < 0.22 && x > 0.12) deep.elev[c] = -0.3; }
  T.eq(Path.find(deep, west, east, { naval: 'coastal', maxCost: 1e9 }), null, 'shelves that do not meet');
  // a frozen channel stops ships too
  const iced = channel(1.5, 0.22);
  for (let c = 0; c < iced.n; c++) if (isSea(iced, c)) iced.ice[c] = 0.7;
  T.eq(Path.find(iced, west, east, { naval: true, maxCost: 1e9 }), null, 'pack ice');
  for (let c = 0; c < iced.n; c++) if (isSea(iced, c) && Math.abs(iced.geo.pos[3 * c + 2]) < 0.15) iced.ice[c] = 0.3;   // a lead opens
  const lead = Path.find(iced, west, east, { naval: true, maxCost: 1e9 });
  T.ok(lead, 'through the lead');
  for (const c of lead) T.ok(!(iced.ice[c] > 0.6), 'never onto pack ice');
  // sea targets
  let mid = -1; for (let c = 0; c < w.n; c++) if (isSea(w, c) && Math.abs(g.pos[3 * c]) < 0.03) { mid = c; break; }
  T.eq(Path.find(w, west, mid), null, 'a sea cell cannot be the target on foot');
  T.ok(Path.find(w, west, mid, { naval: 'coastal' }), 'but it can by canoe');
  T.eq(Path.find(deep, west, (() => { for (let c = 0; c < deep.n; c++) if (Math.abs(deep.geo.pos[3 * c]) < 0.03) return c; })(), { naval: 'coastal' }), null, 'deep target, coastal');
  // a party standing in the water (its cell drowned) can walk out, at the price of a landing
  let shore = -1; for (let c = 0; c < w.n && shore < 0; c++) if (isSea(w, c) && g.pos[3 * c] < 0) for (let k = 0; k < g.nbrN[c]; k++) if (!isSea(w, g.nbr[6 * c + k])) { shore = c; break; }
  const out = Path.find(w, shore, west);
  T.ok(out && !isSea(w, out[1]), 'wades straight out');
  T.near(Path.lastCost, dijkstra(w, shore, false)[west], 1e-9);
  T.near(refCost(w, out[0], out[1], false), 3, EPS);
});

T.test('find: lava and lakes — walls, gaps and the +2 for wading', () => {
  const w = flat(), g = w.geo, target = 1000, dT = hops(w, target);
  const ring = []; for (let k = 0; k < g.nbrN[target]; k++) ring.push(g.nbr[6 * target + k]);
  // walk straight away from the target through one ring cell
  const gap = ring[2]; let start = gap;
  for (let s = 0; s < 5; s++) { let best = start; for (let k = 0; k < g.nbrN[start]; k++) { const b = g.nbr[6 * start + k]; if (dT[b] > dT[best]) best = b; } start = best; }
  T.eq(dT[start], 6);
  T.near(routeCost(w, Path.find(w, start, target), start, target, false), 6, EPS, 'open country: 6 steps');
  // a ring of lakes (surface level with the land) with one dry gap: the route uses the gap
  for (const c of ring) if (c !== gap) { w.elev[c] = 0.3; w.water[c] = 0.2; }
  let p = Path.find(w, start, target);
  T.ok(p.indexOf(gap) >= 0, 'through the gap');
  for (const c of p) T.ok(!(w.water[c] > 0.03), 'dry feet');
  T.near(Path.lastCost, 6, 1e-6);
  // close the gap: wading costs 2 more, and it is still the cheapest way in
  w.elev[gap] = 0.3; w.water[gap] = 0.2;
  p = Path.find(w, start, target);
  T.near(routeCost(w, p, start, target, false), 8, 1e-6, 'one lake cell: +2');
  T.near(Path.lastCost, dijkstra(w, start, false)[target], 1e-9);
  // lava instead of water: with a gap the route threads it, without one there is no route at all
  for (const c of ring) { w.elev[c] = 0.5; w.water[c] = 0; if (c !== gap) w.lava[c] = 0.5; }
  p = Path.find(w, start, target);
  T.ok(p.indexOf(gap) >= 0, 'through the gap in the lava');
  for (const c of p) T.ok(!(w.lava[c] > 0.05), 'never onto lava');
  w.lava[gap] = 0.5;
  T.eq(Path.find(w, start, target, { maxCost: 1e9 }), null, 'walled in by lava');
  T.eq(Path.find(w, start, target, { naval: true, maxCost: 1e9 }), null, 'boats do not help');
  T.eq(Path.find(w, target, start, { maxCost: 1e9 }), null, 'nor can anyone inside get out');
});

T.test('find: the target itself impassable, start impassable', () => {
  const w = flat(), g = w.geo, a = 50, far = 2000;
  w.lava[far] = 1;
  T.eq(Path.find(w, a, far), null, 'lava on the target');
  w.lava[far] = 0; w.lava[a] = 1;
  T.near(routeCost(w, Path.find(w, a, far), a, far, false), hops(w, a)[far], EPS, 'a party standing on fresh lava can still walk away');
  w.lava[a] = 0;
  w.elev[far] = -3;
  T.eq(Path.find(w, a, far), null, 'target under the sea, on foot');
  T.eq(Path.find(w, a, far, { naval: 'coastal' }), null, 'target too deep for canoes');
  T.ok(Path.find(w, a, far, { naval: true }), 'ships reach it');
  w.ice[far] = 0.9;
  T.eq(Path.find(w, a, far, { naval: true }), null, 'target under pack ice');
  void g;
});

T.test('find: maxCost is a hard budget; the default comes from TUNE.path.maxCost', () => {
  const w = flat(), d = hops(w, 0);
  let c8 = -1, c20 = -1;
  for (let c = 0; c < w.n; c++) { if (d[c] === 8 && c8 < 0) c8 = c; if (d[c] === 20 && c20 < 0) c20 = c; }
  T.eq(Path.find(w, 0, c8, { maxCost: 8 }).length, 9);
  T.eq(Path.find(w, 0, c8, { maxCost: 7.999 }), null);
  T.eq(Path.find(w, 0, c20, { maxCost: 19.5 }), null);
  T.eq(Path.find(w, 0, c20).length, 21, 'default budget 400 covers a level L 4 planet');
  T.eq(Path.find(w, 0, 11).length, d[11] + 1, 'pole to pole: ' + d[11] + ' steps');
  T.stub(DM.TUNE.path, 'maxCost', 10);
  T.eq(Path.find(w, 0, c20), null, 'TUNE is read at call time');
  T.eq(Path.find(w, 0, c8).length, 9);
  T.eq(Path.find(w, 0, c20, { maxCost: 25 }).length, 21, 'an explicit budget wins');
});

T.test('find: poles, pentagons, an all-sea planet, repeatable results', () => {
  const w = decorated(4);
  for (const nv of NAVALS) for (const pair of [[0, 11], [3, 9], [11, 5], [1, 2]]) {
    const dist = dijkstra(w, pair[0], nv), p = Path.find(w, pair[0], pair[1], { naval: nv, maxCost: 1e9 });
    if (dist[pair[1]] === Infinity) T.eq(p, null);
    else T.near(routeCost(w, p, pair[0], pair[1], nv), dist[pair[1]], 1e-7);
    T.eq(Path.find(w, pair[0], pair[1], { naval: nv, maxCost: 1e9 }), p, 'same call, same route');
  }
  const ocean = flat(); ocean.elev.fill(-3);
  T.eq(Path.find(ocean, 0, 11), null, 'no land at all');
  T.eq(Path.find(ocean, 0, 11, { naval: 'coastal' }), null);
  const p = Path.find(ocean, 0, 11, { naval: true });
  const poleHops = hops(ocean, 0)[11];
  T.near(Path.lastCost, poleHops * 0.35, 1e-9, 'pole to pole by sea');
  T.eq(p.length, poleHops + 1);
  const high = flat(); high.seaLevel = -9;                         // a planet before its seas (genesis)
  T.eq(Path.find(high, 0, 11, { naval: true }).length, poleHops + 1);
});

T.test('find: no NaN leaks from a damaged world', () => {
  const w = flat();
  w.elev[300] = NaN; w.water[301] = NaN; w.veg[302] = NaN; w.ice[303] = NaN; w.lava[304] = NaN;
  for (let to = 290; to < 320; to++) {
    const p = Path.find(w, 0, to);
    T.ok(p === null || (Array.isArray(p) && Number.isFinite(Path.lastCost)), 'route to ' + to);
  }
  let seen = 0;
  Path.reach(w, 0, 1e9, true, (c, cost) => { T.ok(Number.isFinite(cost) && cost >= 0, 'finite cost at ' + c); seen++; });
  T.ok(seen > w.n - 10, 'the flood still covers the planet');
});

// ------------------------------------------------------------------ reach
T.test('reach: every cell once, in non-decreasing cost, with the cheapest cost', () => {
  for (const seed of [1, 2]) {
    const w = decorated(seed);
    for (const nv of NAVALS) {
      const from = H.findLand(w, c => w.lava[c] === 0 && w.hab[c] > 0.3), dist = dijkstra(w, from, nv);
      for (const budget of [1e9, 45, 12, 0]) {
        const seen = new Uint8Array(w.n); let last = -1, count = 0, first = -1;
        const ret = Path.reach(w, from, budget, nv, (c, cost) => {
          if (count === 0) first = c;
          T.ok(!seen[c], 'cell ' + c + ' visited twice'); seen[c] = 1;
          T.ok(cost >= last, 'cost went down: ' + last + ' → ' + cost); last = cost;
          T.near(cost, dist[c], 1e-7, 'cost of cell ' + c);
          count++;
        });
        T.eq(first, from, 'the start comes first');
        let expect = 0; for (let c = 0; c < w.n; c++) if (dist[c] <= budget) expect++;
        // cells whose cost sits within rounding of the budget may fall either side
        let edge = 0; for (let c = 0; c < w.n; c++) if (Math.abs(dist[c] - budget) < 1e-7) edge++;
        T.ok(Math.abs(count - expect) <= edge, 'visited ' + count + ', expected ' + expect + ' (budget ' + budget + ', naval ' + nv + ')');
        T.eq(ret, count, 'returns the number of cells visited');
        for (let c = 0; c < w.n; c++) if (seen[c]) T.ok(dist[c] <= budget + 1e-7, 'cell ' + c + ' is beyond the budget');
        if (budget === 45) say('reach seed', seed, 'naval', String(nv).padEnd(7), 'budget 45 →', count, 'cells; whole planet →', (() => { let k = 0; for (let c = 0; c < w.n; c++) if (dist[c] < Infinity) k++; return k; })());
      }
    }
  }
});

T.test('reach: stops when the callback returns false; other return values go on', () => {
  const w = flat();
  for (const stopAt of [1, 2, 7, 100]) {
    let k = 0;
    const ret = Path.reach(w, 10, 1e9, false, () => { k++; return k < stopAt; });
    T.eq(k, stopAt, 'stopped after ' + stopAt);
    T.eq(ret, stopAt);
  }
  const d10 = hops(w, 10); let within3 = 0; for (let c = 0; c < w.n; c++) if (d10[c] <= 3) within3++;
  for (const v of [undefined, null, 0, '', true, NaN]) {
    let k = 0;
    Path.reach(w, 10, 3, false, () => { k++; return v; });
    T.eq(k, within3, 'returning ' + String(v) + ' does not stop the flood (' + within3 + ' cells within 3 steps)');
  }
});

T.test('reach: bad arguments reach nothing and never throw', () => {
  const w = flat();
  let k = 0; const visit = () => { k++; };
  for (const bad of [-1, w.n, 1.5, NaN, undefined, null]) T.eq(Path.reach(w, bad, 10, false, visit), 0);
  T.eq(Path.reach(null, 0, 10, false, visit), 0);
  T.eq(Path.reach(w, 0, 10, false, null), 0);
  T.eq(Path.reach(w, 0, -1, false, visit), 0);
  T.eq(k, 0);
  T.eq(Path.reach(w, 0, 0, false, visit), 1, 'budget 0: the start alone');
  k = 0;
  T.eq(Path.reach(w, 0, undefined, false, visit), w.n, 'no budget given: the default (400) covers a level L 4 planet');
  // a flood from a lava cell / a drowned cell still leaves it
  w.lava[40] = 1;
  T.ok(Path.reach(w, 40, 5, false, () => {}) > 30);
});

T.test('reach: the callback may search (find, reach, stepCost) without disturbing the flood', () => {
  const w = decorated(1), from = H.findLand(w, c => w.lava[c] === 0 && w.hab[c] > 0.3);
  const plain = [], nested = [];
  Path.reach(w, from, 150, 'coastal', (c, cost) => { plain.push(c, cost); });
  const far = H.findLand(w, c => c > from + 500 && w.lava[c] === 0), want = Path.find(w, from, far, { naval: true, maxCost: 1e9 });
  let inner = 0, innerCells = 0;
  Path.reach(w, from, 150, 'coastal', (c, cost) => {
    nested.push(c, cost);
    if (nested.length % 14 === 0) {
      T.eq(Path.find(w, from, far, { naval: true, maxCost: 1e9 }), want, 'find inside reach');
      Path.stepCost(w, c, w.geo.nbr[6 * c], false);
      innerCells += Path.reach(w, c, 6, false, (c2, cost2) => {    // a flood inside a flood, itself searching
        if (cost2 > 3) return false;
        Path.find(w, c2, from, { naval: 'coastal' });
      });
      inner++;
    }
  });
  T.eq(nested, plain, 'identical sequence with and without nested searches');
  T.ok(inner >= 5 && innerCells > inner, 'nested searches ran: ' + inner);
  // and an exception thrown by the callback leaves Path usable
  T.throws(() => Path.reach(w, from, 60, false, (c, cost) => { if (cost > 5) throw new Error('boom'); }));
  const again = [];
  Path.reach(w, from, 150, 'coastal', (c, cost) => { again.push(c, cost); });
  T.eq(again, plain, 'still correct after a throwing callback');
  T.eq(Path.find(w, from, far, { naval: true, maxCost: 1e9 }), want);
});

// ------------------------------------------------------------------ scratch buffers, performance
T.test('scratch buffers follow world.n (L 3 → L 5 → L 3) and worlds do not share state', () => {
  const a = flat(3), b = decorated(2, 5), c = flat(3);
  c.elev[20] = 3;
  const h3 = hops(a, 0)[11];
  T.eq(Path.find(a, 0, 11).length, h3 + 1, 'L 3 pole to pole: ' + h3 + ' steps');
  const from = H.findLand(b, x => b.lava[x] === 0), dist = dijkstra(b, from, true);
  let to = -1; for (let x = b.n - 1; x >= 0; x--) if (dist[x] < Infinity && dist[x] > 60) { to = x; break; }
  T.near(routeCost(b, Path.find(b, from, to, { naval: true, maxCost: 1e9 }), from, to, true), dist[to], 1e-7, 'L 5');
  T.eq(Path.find(a, 0, 11).length, h3 + 1, 'L 3 again');
  T.near(routeCost(c, Path.find(c, 0, 11), 0, 11, false), dijkstra(c, 0, false)[11], 1e-9, 'another world of the same size');
  let k = 0; Path.reach(a, 0, 1e9, false, () => { k++; });
  T.eq(k, a.n);
  k = 0; Path.reach(b, from, 1e9, true, () => { k++; });
  let open = 0; for (let x = 0; x < b.n; x++) if (dist[x] < Infinity) open++;
  T.eq(k, open);
});

T.test('performance at L 6: whole-planet searches stay far inside the 25 ms budget', () => {
  const w = H.makeWorld({ L: 6, seed: 2 }), n = w.n, g = w.geo;
  const from = H.bestHab(w);
  // the antipode of `from`, and a land cell walled in by lava (so the search has to exhaust the planet)
  const anti = g.cellAt(-g.pos[3 * from], -g.pos[3 * from + 1], -g.pos[3 * from + 2]);
  let jail = -1; for (let c = n - 1; c >= 0 && jail < 0; c--) if (!isSea(w, c) && g.angle(c, from) > 1.5) jail = c;
  for (let k = 0; k < g.nbrN[jail]; k++) w.lava[g.nbr[6 * jail + k]] = 1;
  const cases = [
    ['on foot, unreachable (floods the continent)', jail, false, 1e9],
    ['coastal, unreachable', jail, 'coastal', 1e9],
    ['naval, unreachable (floods the planet)', jail, true, 1e9],
    ['naval, antipode, no budget', anti, true, 1e9],
    ['naval, antipode, default budget', anti, true, 400],
  ];
  let worst = 0;
  for (const cs of cases) {
    Path.find(w, from, cs[1], { naval: cs[2], maxCost: cs[3] });   // warm up
    let best = Infinity, mx = 0, res = null;
    for (let r = 0; r < 5; r++) { const t0 = now(); res = Path.find(w, from, cs[1], { naval: cs[2], maxCost: cs[3] }); const dt = now() - t0; if (dt < best) best = dt; if (dt > mx) mx = dt; }
    if (mx > worst) worst = mx;
    say('find L6:', cs[0].padEnd(46), res ? (res.length - 1) + ' steps, cost ' + Path.lastCost.toFixed(1) : 'null', '| best', best.toFixed(2), 'ms, worst of 5', mx.toFixed(2), 'ms');
    T.ok(best < 125, cs[0] + ': ' + best.toFixed(1) + ' ms (target 25 ms, asserted at 5×)');
  }
  for (const nv of NAVALS) {
    let cells = 0, best = Infinity;
    for (let r = 0; r < 5; r++) { const t0 = now(); cells = Path.reach(w, from, 1e9, nv, () => {}); const dt = now() - t0; if (dt < best) best = dt; }
    say('reach L6: whole planet, naval', String(nv).padEnd(7), cells, 'cells | best', best.toFixed(2), 'ms');
    T.ok(best < 125, 'reach ' + nv + ': ' + best.toFixed(1) + ' ms');
  }
  // what yearly civ logic asks for: a settler search (budget 45 on land, 120 by sea) and a march of cost ≤ 60
  for (const cs of [[45, false], [45, 'coastal'], [120, true]]) {
    let cells = 0, best = Infinity;
    for (let r = 0; r < 20; r++) { const t0 = now(); cells = Path.reach(w, from, cs[0], cs[1], () => {}); const dt = now() - t0; if (dt < best) best = dt; }
    say('reach L6: budget', cs[0], 'naval', String(cs[1]).padEnd(7), cells, 'cells | best', best.toFixed(3), 'ms');
  }
  const rng = DM.U.rng(9); let tot = 0, runs = 0, found = 0;
  for (let r = 0; r < 300; r++) {
    let a = rng.int(0, n - 1), b = rng.int(0, n - 1);
    while (isSea(w, a)) a = rng.int(0, n - 1);
    while (isSea(w, b) || w.lava[b] > 0) b = rng.int(0, n - 1);
    const t0 = now(); const p = Path.find(w, a, b, { naval: r % 3 === 0 ? false : r % 3 === 1 ? 'coastal' : true, maxCost: 60 }); tot += now() - t0; runs++;
    if (p) found++;
  }
  say('find L6: 300 random land pairs, budget 60: mean', (tot / runs).toFixed(3), 'ms,', found, 'routes found');
  say('worst single find at L6:', worst.toFixed(2), 'ms (budget 25 ms)');
});

T.test('find and reach allocate nothing but the result', () => {
  // Heap growth per call, measured over small batches once the code is hot; the median ignores the batches
  // in which a collection ran. (Unoptimized code boxes every number, so this also guards the hot paths.)
  const w = decorated(1), from = H.findLand(w, c => w.lava[c] === 0 && w.hab[c] > 0.3), nb = w.geo.nbr[6 * from];
  const acc = new Float64Array(1), visit = (c, cost) => { acc[0] += cost; };
  const opts = { naval: true, maxCost: 1e9 };
  let to = -1;
  Path.reach(w, from, 1e9, true, (c, cost) => { if (cost > 30 && !isSea(w, c)) { to = c; return false; } });
  const route = Path.find(w, from, to, opts).length, cells = Path.reach(w, from, 1e9, true, visit);
  const flood = (k) => { let s = 0; for (let i = 0; i < k; i++) s += Path.reach(w, from, 1e9, true, visit); return s; };
  const find = (k) => { let s = 0; for (let i = 0; i < k; i++) s += Path.find(w, from, to, opts).length; return s; };
  const miss = (k) => { let s = 0; for (let i = 0; i < k; i++) s += Path.find(w, from, to, null) === null ? 1 : 0; return s; };
  const step = (k) => { for (let i = 0; i < k; i++) acc[0] += Path.stepCost(w, from, nb, true); return 0; };
  const mu = () => process.memoryUsage().heapUsed;
  const per = (fn, warm, reps) => {
    fn(warm);
    const d = [];
    for (let b = 0; b < 15; b++) { const a = mu(); fn(reps); d.push((mu() - a) / reps); }
    return d.sort((x, y) => x - y)[7];
  };
  const bFlood = per(flood, 600, 4), bFind = per(find, 1500, 20), bMiss = per(miss, 1500, 20), bStep = per(step, 60000, 500);
  say('heap growth per call (median of 15 batches): reach, whole planet (' + cells + ' cells)', bFlood.toFixed(0), 'B · find (' + route + '-cell route)', bFind.toFixed(0),
    'B · find → null (whole continent searched)', bMiss.toFixed(0), 'B · stepCost', bStep.toFixed(0), 'B');
  T.ok(bFlood <= cells * 24 + 512, 'reach: at most one boxed number per callback, got ' + bFlood.toFixed(0) + ' B for ' + cells + ' cells');
  T.ok(bFind <= route * 16 + 512, 'find: the route only, got ' + bFind.toFixed(0) + ' B for ' + route + ' cells');
  T.ok(bMiss <= 512, 'find without a result allocates nothing, got ' + bMiss.toFixed(0) + ' B');
  T.ok(bStep <= 128, 'stepCost: ' + bStep.toFixed(0) + ' B');
});

// ------------------------------------------------------------------ maps (optional)
if (process.env.PATH_MAPS) {
  const M = require('../tools/mapimg.js');
  const L = 6, seed = 7;
  let w;
  if (REAL.Terrain && typeof REAL.Terrain.generate === 'function') {
    w = DM.State.create({ seed, name: 'Pathworld', L });
    REAL.Terrain.generate(w); w.seaLevel = 0; w.heat = 0; w.phase = 'history';
    H.fillBasics(w);
    console.log('    maps: real terrain, seed', seed);
  } else { w = H.makeWorld({ L, seed }); console.log('    maps: helper planet, seed', seed); }
  const g = w.geo, n = w.n;
  // settlements-to-be: the best habitable cells, well apart
  const sites = [], order = Array.from({ length: n }, (_, c) => c).filter(c => w.hab[c] > 0.45).sort((a, b) => (w.hab[b] - w.hab[a]) || (a - b));
  for (const c of order) { if (sites.length >= 14) break; if (sites.every(s => g.angle(s, c) > 0.45)) sites.push(c); }
  const col = new Map();
  const paint = (p, rgb) => { if (p) for (const c of p) col.set(c, rgb); };
  let nLand = 0, nCoast = 0, nSea = 0;
  for (let i = 0; i < sites.length; i++) for (let j = i + 1; j < sites.length; j++) {
    const a = sites[i], b = sites[j];
    if (g.angle(a, b) > 1.3) continue;
    const pl = Path.find(w, a, b, { naval: false, maxCost: 400 });
    const pc = pl ? null : Path.find(w, a, b, { naval: 'coastal', maxCost: 400 });
    const pn = pl || pc ? null : Path.find(w, a, b, { naval: true, maxCost: 400 });
    if (pl) { paint(pl, [230, 40, 40]); nLand++; } else if (pc) { paint(pc, [255, 170, 0]); nCoast++; } else if (pn) { paint(pn, [255, 255, 255]); nSea++; }
  }
  const elevCol = c => { const e = w.elev[c]; return e < w.seaLevel ? M.ramp([[0, [150, 215, 235]], [0.6, [60, 140, 200]], [2.5, [25, 70, 150]], [6.5, [8, 20, 70]]], w.seaLevel - e) : M.ramp([[0, [88, 150, 80]], [0.6, [140, 175, 95]], [1.8, [200, 190, 120]], [3.5, [150, 120, 95]], [5.5, [200, 200, 205]], [8, [255, 255, 255]]], e - w.seaLevel); };
  const siteSet = new Set(sites);
  console.log('    wrote', M.custom(w, 'shots/path-routes.png', c => siteSet.has(c) ? [0, 0, 0] : col.get(c) || elevCol(c), { w: 2048, h: 1024, rivers: false }));
  console.log('    routes between', sites.length, 'sites: on foot (red)', nLand, '· coastal (orange)', nCoast, '· naval (white)', nSea);
  // reach: what a settler search sees from one site — on foot (45), by canoe (45), under sail (120)
  const from = sites[0], costL = new Float32Array(n).fill(-1), costC = new Float32Array(n).fill(-1), costN = new Float32Array(n).fill(-1);
  const kl = Path.reach(w, from, 45, false, (c, k) => { costL[c] = k; });
  const kc = Path.reach(w, from, 45, 'coastal', (c, k) => { costC[c] = k; });
  const kn = Path.reach(w, from, 120, true, (c, k) => { costN[c] = k; });
  const heat = [[0, [255, 255, 160]], [0.5, [240, 130, 40]], [1, [150, 20, 30]]];
  console.log('    wrote', M.custom(w, 'shots/path-reach.png', c => {
    if (c === from) return [255, 255, 255];
    if (costL[c] >= 0) return M.ramp(heat, costL[c] / 45);
    if (costC[c] >= 0) return M.mix(M.ramp(heat, costC[c] / 45), [40, 200, 220], 0.6);
    if (costN[c] >= 0) return M.mix(elevCol(c), [200, 60, 220], 0.25 + 0.35 * (1 - costN[c] / 120));
    const k = elevCol(c); return [k[0] * 0.55, k[1] * 0.55, k[2] * 0.55];
  }, { w: 2048, h: 1024, rivers: false }));
  console.log('    reach from cell', from, ': on foot ≤45 →', kl, 'cells (yellow→red) · coastal ≤45 →', kc, '(cyan tint) · naval ≤120 →', kn, '(violet tint)');
}
