// tests/hydro.test.js — DM.Hydro (SPEC-SIM §3.4, BUILD.md A4).
//   node tools/test.js hydro
//   T_VERBOSE=1     lists the passing tests
//   HYDRO_REPORT=1  prints the measured numbers (river counts, lengths, timings)
//   HYDRO_MAPS=1    also writes shots/hydro-rivers-s<seed>.png (helper worlds, L 6) and, when terrain.js and climate.js
//                   load, shots/hydro-real-s<seed>.png (the game's own planets): elevation with lakes and rivers
// Worlds: the helper's synthetic planets (10 seeds at L 4, 3 at L 5, one at L 6) and hand-built ones (a cone
// island with pits dug into it, a valley continent with two ridges, planets with no sea and with no land). Every
// check re-derives its expectation here, by brute force, from elev / seaLevel / moist and the documented rules:
// filled levels by relaxation, depressions by flood fill, discharge by a topological sum, river cells from the
// three rules (riverT, creek, head), fresh by a plain BFS. No other sim module is needed.
'use strict';
const T = require('../tools/t.js');
const DM = require('../tools/load.js')({ only: ['hydro'] });        // strict: a broken hydro.js fails here
const H = require('./helpers/world.js');
T.watchEvents();

const Hy = DM.Hydro, DEG = 180 / Math.PI, REF = 40962;
const REPORT = !!process.env.HYDRO_REPORT;
const say = (...a) => { if (REPORT) console.log('    ·', ...a); };
const tune = () => DM.TUNE.hydro;

// ------------------------------------------------------------------ world builders
function blank(L, seaFloor) {                      // an all-sea planet at sea level 0
  const w = DM.State.create({ seed: 7, L });
  w.elev.fill(seaFloor === undefined ? -3 : seaFloor);
  w.seaLevel = 0; w.heat = 0; w.phase = 'history';
  return w;
}
function setMoist(w, m) { for (let c = 0; c < w.n; c++) w.moist[c] = w.elev[c] < w.seaLevel ? 1 : m; return w; }
// A cone island: highest at `centre`, falling evenly to `rim` at angular radius `ang`. No pits of its own.
function coneIsland(L, ang, top, rim) {
  const w = blank(L), g = w.geo, c0 = g.cellAt(0.3, 0.5, 0.8), list = [];
  const k = g.within(c0, ang, list);
  for (let i = 0; i < k; i++) { const c = list[i]; w.elev[c] = rim + (top - rim) * (1 - g.angle(c0, c) / ang); }
  w._centre = c0;
  return setMoist(w, 0.5);
}
// A continent between ±40° latitude and ±50° longitude: a valley along longitude 0 that falls toward the south
// coast, flanks rising to a ridge at ±32° on either side, and beyond each ridge an outer slope down to the coast.
const RIDGE = 32;
function valleyWorld(L, moist) {
  const w = blank(L), g = w.geo;
  for (let c = 0; c < w.n; c++) {
    const la = g.lat[c] * DEG, lo = Math.abs(g.lon[c] * DEG);
    if (Math.abs(la) < 40 && lo < 50) w.elev[c] = 0.1 + 0.03 * (la + 40) + 0.05 * (lo <= RIDGE ? lo : RIDGE - 0.9 * (lo - RIDGE));
  }
  return setMoist(w, moist);
}
const nbrs = (w, c) => { const out = []; for (let k = 0; k < w.geo.nbrN[c]; k++) out.push(w.geo.nbr[6 * c + k]); return out; };
const isSea = (w, c) => w.elev[c] < w.seaLevel;
const landCount = w => { let k = 0; for (let c = 0; c < w.n; c++) if (!isSea(w, c)) k++; return k; };
// dig `cells` (one basin) down to `depth` below the lowest cell around them
function dig(w, cells, depth) {
  let rimMin = Infinity;
  for (const c of cells) for (const b of nbrs(w, c)) if (cells.indexOf(b) < 0 && w.elev[b] < rimMin) rimMin = w.elev[b];
  for (const c of cells) w.elev[c] = rimMin - depth;
  return rimMin;
}
// an inland cell of the cone island `hops` from the centre, plus neighbours forming a compact patch
function patch(w, hops, size) {
  const g = w.geo, ring = [];
  g.ring(w._centre, hops, ring);
  const c = ring[ring.length - 1], out = [c];
  for (const b of nbrs(w, c)) {
    if (out.length >= size) break;
    if (out.every(x => x === b || nbrs(w, x).indexOf(b) >= 0 || x === c)) out.push(b);
  }
  // keep it one connected, mutually adjacent group (a triangle for size 3)
  if (size === 3 && nbrs(w, out[1]).indexOf(out[2]) < 0) { for (const b of nbrs(w, c)) if (b !== out[1] && nbrs(w, out[1]).indexOf(b) >= 0) { out[2] = b; break; } }
  return out;
}

// ------------------------------------------------------------------ brute-force references
const scaledT = w => tune().riverT * Math.pow(w.n / REF, tune().riverExp);
function riverT(w) { return Math.max(tune().riverMin, scaledT(w)); }
function creekT(w) { return Math.max(riverT(w), tune().creek * scaledT(w)); }
function headT(w) { return Math.max(tune().riverMin, tune().head * riverT(w)); }
function outletOf(w) {                              // the lowest cell of a world with no sea, else -1
  for (let c = 0; c < w.n; c++) if (isSea(w, c)) return -1;
  let o = 0;
  for (let c = 1; c < w.n; c++) if (w.elev[c] < w.elev[o]) o = c;
  return w.n > 0 ? o : -1;
}
// filled level of every land cell (−Infinity on sea cells) by relaxation: max(elev, lowest filled neighbour)
function bruteFilled(w) {
  const n = w.n, nbr = w.geo.nbr, e = w.elev, f = new Float64Array(n).fill(Infinity), out = outletOf(w);
  for (let c = 0; c < n; c++) if (isSea(w, c)) f[c] = -Infinity;
  if (out >= 0) f[out] = e[out];
  for (let changed = true; changed;) {
    changed = false;
    for (let pass = 0; pass < 2; pass++) {
      for (let i = 0; i < n; i++) {
        const c = pass ? n - 1 - i : i;
        if (f[c] === -Infinity || c === out) continue;
        let m = Infinity;
        for (let k = 0; k < 6; k++) { const b = nbr[6 * c + k]; if (b < 0) break; if (f[b] < m) m = f[b]; }
        const v = Math.max(e[c], m);
        if (v < f[c]) { f[c] = v; changed = true; }
      }
    }
  }
  return f;
}
const rain = (w, c) => { const m = w.moist[c]; return m > 0 && m < Infinity ? m : 0; };
// discharge implied by down[] and moist, in float64; throws on a cycle
function bruteFlow(w) {
  const n = w.n, deg = new Int32Array(n), acc = new Float64Array(n), stack = [];
  for (let c = 0; c < n; c++) { if (!isSea(w, c)) acc[c] = rain(w, c); const d = w.down[c]; if (d >= 0) deg[d]++; }
  for (let c = 0; c < n; c++) if (deg[c] === 0) stack.push(c);
  let seen = 0;
  while (stack.length) {
    const c = stack.pop(), d = w.down[c];
    seen++;
    if (d >= 0) { acc[d] += acc[c]; if (--deg[d] === 0) stack.push(d); }
  }
  T.eq(seen, n, 'down[] has a cycle');
  return acc;
}
// the mouth (last land cell on the way to the sea) of every land cell, -1 on sea cells
function mouths(w) {
  const n = w.n, mouth = new Int32Array(n).fill(-2);
  for (let c0 = 0; c0 < n; c0++) {
    if (isSea(w, c0)) { mouth[c0] = -1; continue; }
    const path = [];
    let c = c0, steps = 0;
    while (mouth[c] === -2) {
      path.push(c);
      const d = w.down[c];
      if (d < 0 || isSea(w, d)) { mouth[c] = c; break; }
      c = d;
      T.ok(++steps <= n, 'down[] does not reach the sea from cell ' + c0);
    }
    for (const x of path) mouth[x] = mouth[c];
  }
  return mouth;
}
const lakeCell = (w, c) => !isSea(w, c) && w.water[c] > tune().lakeEps;
// River cells by the documented rules, from flow / water / down (and the filled levels f):
//   1. a land cell that is no lake cell, carries more than riverT, in a system whose mouth carries creek × riverT;
//   2. from every source of those, upstream along the largest inflow above headT: not past a lake cell draining
//      into the course, not onto ground lakeDepth or more below its spill level.
// Returns { is, base }: all river cells, and the ones of rule 1.
function riverCells(w, f) {
  f = f || bruteFilled(w);
  const n = w.n, t = tune(), RT = riverT(w), CT = creekT(w), HT = headT(w), mouth = mouths(w), out = outletOf(w);
  const is = new Uint8Array(n), base = new Uint8Array(n), ins = [];
  for (let c = 0; c < n; c++) ins.push([]);
  for (let c = 0; c < n; c++) if (w.down[c] >= 0) ins[w.down[c]].push(c);
  for (let c = 0; c < n; c++) if (!isSea(w, c) && !lakeCell(w, c) && w.flow[c] > RT && w.flow[mouth[c]] >= CT) { is[c] = 1; base[c] = 1; }
  if (t.head > 0 && HT < RT) {
    const sources = [];
    for (let c = 0; c < n; c++) if (base[c] && !ins[c].some(b => base[b])) sources.push(c);
    for (const s of sources) {
      for (let cur = s; ;) {
        if (ins[cur].some(b => lakeCell(w, b))) break;
        let up = -1;
        for (const b of ins[cur]) if (w.flow[b] > HT && (up < 0 || w.flow[b] > w.flow[up] || (w.flow[b] === w.flow[up] && b < up))) up = b;
        if (up < 0 || f[up] - w.elev[up] >= t.lakeDepth) break;
        is[up] = 1; cur = up;
      }
    }
  }
  if (out >= 0 && is[out] && !ins[out].some(b => is[b])) { is[out] = 0; base[out] = 0; }   // a lone outlet cell is no river
  return { is, base };
}
function bruteFresh(w, is) {
  const n = w.n, d = new Uint8Array(n).fill(255), q = [];
  for (let c = 0; c < n; c++) if (is[c] || lakeCell(w, c)) { d[c] = 0; q.push(c); }
  for (let h = 0; h < q.length; h++) {
    const c = q[h];
    if (d[c] >= 254) continue;
    for (const b of nbrs(w, c)) if (d[b] === 255) { d[b] = d[c] + 1; q.push(b); }
  }
  return d;
}
// the highest a river climbs above the lowest water surface it has already passed
function maxClimb(w) {
  let worst = 0;
  for (const r of w.rivers) {
    let lo = Infinity;
    for (let i = 0; i < r.cells.length; i++) {
      const c = r.cells[i], h = isSea(w, c) ? w.seaLevel : w.elev[c] + w.water[c];
      if (h < lo) lo = h;
      if (h - lo > worst) worst = h - lo;
    }
  }
  return worst;
}
// share of river cells standing proud of their surroundings (at least 4 of the neighbours clearly lower)
function convexShare(w, is) {
  let rc = 0, cv = 0, land = 0, landCv = 0;
  for (let c = 0; c < w.n; c++) {
    if (isSea(w, c)) continue;
    let lower = 0;
    for (const b of nbrs(w, c)) if (w.elev[b] < w.elev[c] - 0.02) lower++;
    land++; if (lower >= 4) landCv++;
    if (is[c]) { rc++; if (lower >= 4) cv++; }
  }
  return { rivers: rc ? cv / rc : 0, land: land ? landCv / land : 0 };
}
// pairs of neighbouring river cells that belong to two courses running side by side (neither flows into the
// other, they do not meet in the next cell, and it is not one course turning a corner)
function sideBySide(w, is) {
  let pairs = 0;
  for (let c = 0; c < w.n; c++) {
    if (!is[c]) continue;
    for (const b of nbrs(w, c)) {
      if (b < c || !is[b]) continue;
      const dc = w.down[c], db = w.down[b];
      if (dc === b || db === c || dc === db) continue;
      if ((dc >= 0 && w.down[dc] === b) || (db >= 0 && w.down[db] === c)) continue;
      pairs++;
    }
  }
  return pairs;
}
// nothing that Hydro writes is NaN or infinite
function outputsFinite(w) {
  for (let c = 0; c < w.n; c++) if (!Number.isFinite(w.water[c]) || !Number.isFinite(w.flow[c])) return false;
  for (const r of w.rivers) for (let i = 0; i < r.flow.length; i++) if (!Number.isFinite(r.flow[i])) return false;
  return true;
}

// ------------------------------------------------------------------ the full set of invariants
// Returns numbers for the report. `w` must have been updated.
function checkAll(w, label) {
  const n = w.n, t = tune(), RT = riverT(w), HT = headT(w), out = outletOf(w), f = bruteFilled(w), tag = label + ': ';
  T.near(Hy.riverT(w), RT, 1e-9, tag + 'riverT');

  // --- down[]
  const mouth = mouths(w);                          // also proves every land cell reaches the sea (or the outlet)
  for (let c = 0; c < n; c++) {
    const d = w.down[c];
    if (isSea(w, c)) { T.eq(d, -1, tag + 'down on sea cell ' + c); continue; }
    if (c === out) { T.eq(d, -1, tag + 'down on the outlet'); continue; }
    T.ok(d >= 0 && nbrs(w, c).indexOf(d) >= 0, tag + 'down[' + c + '] = ' + d + ' is not a neighbour');
    let seaLow = -1;
    for (const b of nbrs(w, c)) if (isSea(w, b) && (seaLow < 0 || w.elev[b] < w.elev[seaLow])) seaLow = b;
    if (seaLow >= 0) T.ok(isSea(w, d) && w.elev[d] === w.elev[seaLow], tag + 'coastal cell ' + c + ' must drain into its lowest sea neighbour');
    else T.ok(f[d] <= f[c], tag + 'down[' + c + '] climbs to a higher filled level');
    T.ok(Number.isFinite(f[c]), tag + 'land cell ' + c + ' has no way out');
  }
  if (out >= 0) for (let c = 0; c < n; c++) T.eq(mouth[c], out, tag + 'every cell of a sea-less world drains to the outlet');

  // --- flow[]
  const acc = bruteFlow(w);
  for (let c = 0; c < n; c++) {
    T.ok(Math.abs(w.flow[c] - acc[c]) <= 1e-5 * acc[c] + 1e-6, tag + 'flow[' + c + '] = ' + w.flow[c] + ', upstream moist sums to ' + acc[c]);
    const d = w.down[c];
    if (d >= 0) T.ok(w.flow[d] >= w.flow[c], tag + 'flow decreases downstream at ' + c);
  }

  // --- water[]: exactly the documented lakes
  const rc = riverCells(w, f), is = rc.is, CT = creekT(w);
  const comp = new Int32Array(n).fill(-1);
  let lakes = 0, lakeCells = 0, oneCellLakes = 0;
  for (let c0 = 0; c0 < n; c0++) {
    T.ok(w.water[c0] >= 0, tag + 'negative water');
    if (isSea(w, c0)) { T.eq(w.water[c0], 0, tag + 'water on sea cell ' + c0); continue; }
    if (!(f[c0] > w.elev[c0])) { T.eq(w.water[c0], 0, tag + 'water outside a depression at ' + c0); continue; }
    if (comp[c0] >= 0) continue;
    const q = [c0];
    comp[c0] = c0;
    for (let h = 0; h < q.length; h++) for (const b of nbrs(w, q[h])) if (comp[b] < 0 && !isSea(w, b) && f[b] > w.elev[b]) { comp[b] = c0; q.push(b); }
    let wet = 0, depth = 0, sum = 0, fed = false;
    for (const c of q) {
      const d = f[c] - w.elev[c];
      T.eq(f[c], f[c0], tag + 'one basin, one level');
      if (Math.fround(d) > t.lakeEps) wet++;
      if (d > depth) depth = d;
      sum += rain(w, c);
      if (w.flow[c] > RT && w.flow[mouth[c]] >= CT) fed = true;
    }
    const lake = (wet >= t.lakeCells || depth >= t.lakeDepth) && (fed || sum >= t.lakeMoist * q.length);
    for (const c of q) T.near(w.water[c], lake ? Math.min(t.lakeMax, f[c] - w.elev[c]) : 0, 1e-6, tag + 'water[' + c + '] in a basin of ' + q.length + ' cells, ' + depth.toFixed(3) + ' deep');
    if (lake) { lakes++; lakeCells += wet; if (q.length === 1) oneCellLakes++; }
    // the acceptance wording: water only in depressions of at least 3 cells or at least 0.25 deep
    if (lake) T.ok(q.length >= 3 || depth >= 0.25, tag + 'a lake in a pit that is too small');
  }

  // --- rivers
  const used = new Int32Array(n);
  let confluences = 0, longest = 0, prevQ = Infinity, headCells = 0, mainAtOutlet = 0;
  T.ok(Array.isArray(w.rivers), tag + 'world.rivers is an array');
  for (let ri = 0; ri < w.rivers.length; ri++) {
    const r = w.rivers[ri], cs = r.cells, len = cs.length, rt = tag + 'river ' + ri + ' ';
    T.ok(cs instanceof Int32Array && r.flow instanceof Float32Array, rt + 'is { cells: Int32Array, flow: Float32Array }');
    T.eq(Object.keys(r).sort().join(','), 'cells,flow', rt + 'keys');
    T.ok(len >= 2 && r.flow.length === len, rt + 'lengths');
    if (len > longest) longest = len;
    const last = cs[len - 1], prev = cs[len - 2];
    let headDone = false;
    for (let i = 0; i < len; i++) {
      T.eq(r.flow[i], w.flow[cs[i]], rt + 'flow[' + i + ']');
      if (i < len - 1) {
        T.ok(is[cs[i]] === 1, rt + 'runs through cell ' + cs[i] + ', which is not a river cell');
        T.ok(!lakeCell(w, cs[i]) && !isSea(w, cs[i]), rt + 'runs on through water at ' + cs[i]);
        T.eq(w.down[cs[i]], cs[i + 1], rt + 'does not follow down[]');
        T.ok(nbrs(w, cs[i]).indexOf(cs[i + 1]) >= 0, rt + 'is not a chain of neighbours');
        T.ok(r.flow[i] > HT && r.flow[i] >= t.riverMin, rt + 'carries only ' + r.flow[i] + ' at ' + cs[i]);
        // headwater cells (at or below riverT) come first, then the river proper
        if (r.flow[i] > RT) headDone = true; else { T.ok(!headDone, rt + 'drops below riverT again at ' + cs[i]); headCells++; }
        used[cs[i]]++;
      }
      // the largest river cell flowing into a river cell of the course is the course's own previous cell
      // (the final cell is looked at below: there a tributary meets a bigger branch)
      if (i > 0 && i < len - 1) {
        for (const b of nbrs(w, cs[i])) if (is[b] && w.down[b] === cs[i] && b !== cs[i - 1]) T.ok(w.flow[b] < w.flow[cs[i - 1]] || (w.flow[b] === w.flow[cs[i - 1]] && b > cs[i - 1]), rt + 'does not follow the main stem at ' + cs[i]);
      }
    }
    for (const b of nbrs(w, cs[0])) T.ok(!(is[b] && w.down[b] === cs[0]), rt + 'does not start at a source');
    if (isSea(w, last) || lakeCell(w, last)) { /* ends under water */ }
    else {
      T.ok(is[last] === 1, rt + 'ends on dry ground at ' + last);
      let bigger = false;
      for (const b of nbrs(w, last)) if (b !== prev && is[b] && w.down[b] === last && (w.flow[b] > w.flow[prev] || (w.flow[b] === w.flow[prev] && b < prev))) bigger = true;
      if (last === out && !bigger) mainAtOutlet++;       // the main river of a sea-less world ends on the outlet itself
      else { T.ok(bigger, rt + 'stops at a confluence where it is the main stem'); confluences++; }
    }
    const q = w.flow[prev];
    T.ok(q <= prevQ, rt + 'is out of order (largest river first)');
    prevQ = q;
  }
  T.eq(mainAtOutlet, out >= 0 && is[out] ? 1 : 0, tag + 'rivers ending on the outlet as its main stem');
  let nRiver = 0;
  for (let c = 0; c < n; c++) {
    if (is[c]) { nRiver++; T.eq(used[c], c === out ? 0 : 1, tag + 'river cell ' + c + ' must lie on exactly one river'); }
    else T.eq(used[c], 0, tag + 'cell ' + c + ' is on a river but is no river cell');
    T.eq(Hy.isRiver(w, c), is[c] === 1, tag + 'isRiver(' + c + ')');
    T.eq(Hy.isLake(w, c), lakeCell(w, c), tag + 'isLake(' + c + ')');
  }

  // --- fresh[]
  const fr = bruteFresh(w, is);
  for (let c = 0; c < n; c++) T.eq(w.fresh[c], fr[c], tag + 'fresh[' + c + ']');

  // --- nothing runs uphill, nothing is NaN
  const climb = maxClimb(w);
  T.ok(climb <= t.lakeDepth + 1e-4, tag + 'a river climbs ' + climb.toFixed(3));
  T.ok(outputsFinite(w), tag + 'non-finite output');
  return { rivers: w.rivers.length, riverCells: nRiver, headCells, confluences, longest, lakes, lakeCells, oneCellLakes, climb, is, base: rc.base };
}
function update(w) { Hy.update(w); return w; }

// ------------------------------------------------------------------ API and thresholds
T.test('API: update and riverT exist; nothing else is needed at load time', () => {
  T.eq(typeof Hy.update, 'function'); T.eq(typeof Hy.riverT, 'function');
  T.ok(DM.TUNE.hydro && typeof DM.TUNE.hydro.riverT === 'number', 'TUNE.hydro');
});

T.test('riverT scales with the cell count, has a floor, and reads TUNE at call time', () => {
  const t = tune();
  T.near(Hy.riverT({ n: REF }), t.riverT, 1e-12);
  T.near(Hy.riverT({ n: 10242 }), Math.max(t.riverMin, t.riverT * Math.sqrt(10242 / REF)), 1e-12);
  T.near(Hy.riverT({ n: 163842 }), t.riverT * Math.sqrt(163842 / REF), 1e-12);
  T.ok(Hy.riverT({ n: 163842 }) > Hy.riverT({ n: REF }) && Hy.riverT({ n: REF }) > Hy.riverT({ n: 10242 }), 'grows with n');
  T.eq(Hy.riverT({ n: 12 }), t.riverMin, 'floor on tiny planets');
  T.stub(DM.TUNE.hydro, 'riverT', 10);
  T.near(Hy.riverT({ n: REF }), 10, 1e-12, 'TUNE is read when called');
});

// ------------------------------------------------------------------ the acceptance checks on helper worlds
T.test('helper worlds, 10 seeds at L 4: every documented invariant', () => {
  let rivers = 0, minRivers = Infinity, lakes = 0, conf = 0, one = 0;
  for (let seed = 1; seed <= 10; seed++) {
    const w = update(H.makeWorld({ L: 4, seed }));
    const r = checkAll(w, 'L4 seed ' + seed);
    rivers += r.rivers; lakes += r.lakes; conf += r.confluences; one += r.oneCellLakes;
    if (r.rivers < minRivers) minRivers = r.rivers;
  }
  say('L4 × 10: rivers ' + rivers + ' (fewest ' + minRivers + '), confluences ' + conf + ', lakes ' + lakes + ' (' + one + ' of one deep cell)');
  T.ok(minRivers >= 5, 'every helper world has rivers (fewest: ' + minRivers + ')');
  T.ok(lakes >= 10, 'lakes form in basins');
});

T.test('helper worlds, 3 seeds at L 5: invariants, branching, rivers keep off the ridges', () => {
  let conf = 0;
  for (let seed = 1; seed <= 3; seed++) {
    const w = update(H.makeWorld({ L: 5, seed }));
    const r = checkAll(w, 'L5 seed ' + seed), cv = convexShare(w, r.is);
    conf += r.confluences;
    say('L5 seed ' + seed + ': rivers ' + r.rivers + ', river cells ' + r.riverCells + ' (' + r.headCells + ' headwater), confluences ' + r.confluences + ', longest ' + r.longest +
      ', lakes ' + r.lakes + ' (' + r.lakeCells + ' cells), convex river cells ' + (100 * cv.rivers).toFixed(1) + ' % (all land ' + (100 * cv.land).toFixed(1) + ' %)');
    T.ok(r.rivers >= 40 && r.confluences >= 4, 'rivers branch (' + r.rivers + ' rivers, ' + r.confluences + ' confluences)');
    T.ok(r.longest >= 8, 'some river is long (' + r.longest + ')');
    T.ok(cv.rivers < 0.08 && cv.rivers < 0.3 * cv.land, 'rivers on ridges: ' + (100 * cv.rivers).toFixed(1) + ' % of river cells stand proud');
  }
  T.ok(conf >= 25, 'confluences on three worlds: ' + conf);
});

T.test('helper worlds at other sea levels (flooded lowlands, exposed shelves)', () => {
  for (const sea of [-1.5, 0.6, 2.5]) {
    const w = update(H.makeWorld({ L: 4, seed: 3, sea }));
    const r = checkAll(w, 'sea ' + sea);
    say('sea ' + sea + ': land ' + landCount(w) + ', rivers ' + r.rivers + ', lakes ' + r.lakes);
  }
});

// ------------------------------------------------------------------ flags, determinism, robustness
T.test('update clears need.hydro, sets need.life, dirty.rivers and dirty.water, and touches no other flag', () => {
  const w = H.makeWorld({ L: 3, seed: 2 });
  w.need = { derive: false, climate: false, hydro: true, life: false };
  w.dirty = { elev: false, color: false, water: false, objects: false, rivers: false, roads: false };
  const ev = H.capture(() => Hy.update(w));
  T.eq(w.need, { derive: false, climate: false, hydro: false, life: true });
  T.eq(w.dirty, { elev: false, color: false, water: true, objects: false, rivers: true, roads: false });
  T.eq(ev.length, 0, 'Hydro emits no events');
  w.need.derive = true; w.need.climate = true; w.dirty.elev = true;
  Hy.update(w);
  T.eq(w.need, { derive: true, climate: true, hydro: false, life: true }, 'flags are only ever set, never cleared for others');
  T.ok(w.dirty.elev === true);
  delete w.need; delete w.dirty;
  Hy.update(w);                                     // tolerated
});

T.test('deterministic, idempotent, draws nothing from world.rng, writes only its own fields', () => {
  const a = H.makeWorld({ L: 4, seed: 6 }), b = H.makeWorld({ L: 4, seed: 6 });
  const owned = { water: 1, flow: 1, down: 1, fresh: 1 };
  const before = {};
  for (const fd of DM.State.CELL_FIELDS) if (!owned[fd.name]) before[fd.name] = Array.from(a[fd.name]);
  const rs = a.rng.s, sea = a.seaLevel;
  Hy.update(a); Hy.update(b);
  T.eq(a.rng.s, rs, 'rng state');
  T.eq(a.seaLevel, sea);
  for (const k in before) T.eq(Array.from(a[k]), before[k], 'field ' + k + ' was modified');
  const snap = w => JSON.stringify([Array.from(w.water), Array.from(w.flow), Array.from(w.down), Array.from(w.fresh), w.rivers.map(r => [Array.from(r.cells), Array.from(r.flow)])]);
  const sa = snap(a);
  T.eq(sa, snap(b), 'same seed, same result');
  const riversBefore = a.rivers;
  Hy.update(a);
  T.eq(snap(a), sa, 'a second update changes nothing');
  T.ok(a.rivers !== riversBefore, 'world.rivers is a new array each time (the old one is never mutated)');
  // a different planet in between must not leak through the scratch buffers
  Hy.update(H.makeWorld({ L: 3, seed: 9 })); Hy.update(H.makeWorld({ L: 4, seed: 2 }));
  Hy.update(a);
  T.eq(snap(a), sa, 'scratch buffers are keyed by cell count and fully rewritten');
});

T.test('the drainage tree depends on the terrain only: rain scales the flow, never moves a river bed', () => {
  const w = update(H.makeWorld({ L: 4, seed: 5 }));
  const down = Array.from(w.down), flow = Array.from(w.flow);
  for (let c = 0; c < w.n; c++) w.moist[c] *= 0.5;
  Hy.update(w);
  T.eq(Array.from(w.down), down, 'down[] changed with the rain');
  for (let c = 0; c < w.n; c++) T.near(w.flow[c], flow[c] * 0.5, 1e-4 * flow[c] + 1e-6, 'flow halves');
  w.moist.fill(0);
  Hy.update(w);
  T.eq(Array.from(w.down), down, 'down[] of a dry world');
  T.eq(w.rivers.length, 0, 'no rain, no rivers');
  for (let c = 0; c < w.n; c++) { T.eq(w.flow[c], 0); T.eq(w.water[c], 0); T.eq(w.fresh[c], 255); }
});

T.test('bad rainfall (NaN, negative, Infinity) counts as none and never reaches the outputs', () => {
  const w = H.makeWorld({ L: 4, seed: 4 });
  const land = [];
  for (let c = 0; c < w.n; c++) if (!isSea(w, c)) land.push(c);
  const bad = [land[3], land[40], land[90], land[200]];
  w.moist[bad[0]] = NaN; w.moist[bad[1]] = -5; w.moist[bad[2]] = Infinity; w.moist[bad[3]] = -Infinity;
  update(w);
  checkAll(w, 'bad moist');
  T.ok(outputsFinite(w));
  for (const c of bad) T.ok(w.flow[c] >= 0 && Number.isFinite(w.flow[c]));
  // the same world with those four cells simply dry gives the same answer
  const flow = Array.from(w.flow), down = Array.from(w.down);
  for (const c of bad) w.moist[c] = 0;
  update(w);
  T.eq(Array.from(w.flow), flow); T.eq(Array.from(w.down), down);
  T.eq(H.checkFinite(w), null);
});

T.test('works in every phase and with the default (empty) world', () => {
  const w = DM.State.create({ seed: 3, L: 3 });     // elev 0 everywhere, seaLevel −9: one flat sea-less plain, dry
  for (const ph of ['molten', 'water', 'life', 'people', 'history', 'ended']) { w.phase = ph; Hy.update(w); }
  T.eq(w.rivers.length, 0);
  checkAll(w, 'default world');
});

// ------------------------------------------------------------------ no sea, no land
T.test('no sea: the lowest cell is the outlet and everything drains to it', () => {
  const w = H.makeWorld({ L: 4, seed: 2, sea: -20, basics: false });
  for (let c = 0; c < w.n; c++) w.moist[c] = 0.4;
  update(w);
  const out = outletOf(w);
  T.ok(out >= 0);
  T.eq(w.down[out], -1);
  let lowest = Infinity;
  for (let c = 0; c < w.n; c++) lowest = Math.min(lowest, w.elev[c]);
  T.eq(w.elev[out], lowest, 'the outlet is the lowest cell');
  const r = checkAll(w, 'no sea');
  T.near(w.flow[out], 0.4 * w.n, 0.01, 'all the rain arrives at the outlet');
  T.ok(r.rivers > 0 && r.lakes > 0, 'rivers and lakes on a sea-less world');
  const main = w.rivers[0];
  T.eq(main.cells[main.cells.length - 1], out, 'the main river ends on the outlet');
  say('no sea: rivers ' + r.rivers + ', lakes ' + r.lakes + ' (' + r.lakeCells + ' cells), longest ' + r.longest);
});

T.test('no sea and no rain: nothing but flow directions', () => {
  const w = update(H.makeWorld({ L: 4, seed: 2, sea: -20, basics: false }));
  T.eq(w.rivers.length, 0);
  for (let c = 0; c < w.n; c++) { T.eq(w.flow[c], 0); T.eq(w.water[c], 0); T.eq(w.fresh[c], 255); }
  checkAll(w, 'no sea, dry');
});

T.test('no land: every field at rest, no rivers', () => {
  const w = H.makeWorld({ L: 4, seed: 2, sea: 30 });
  w.water.fill(1); w.flow.fill(3); w.down.fill(5); w.fresh.fill(7);
  w.rivers = [{ cells: new Int32Array([1, 2]), flow: new Float32Array([1, 2]) }];
  w.need.life = false; w.need.hydro = true;
  update(w);
  for (let c = 0; c < w.n; c++) { T.eq(w.down[c], -1); T.eq(w.flow[c], 0); T.eq(w.water[c], 0); T.eq(w.fresh[c], 255); }
  T.eq(w.rivers, []);
  T.eq(w.need.life, true); T.eq(w.need.hydro, false);
  checkAll(w, 'no land');
});

T.test('a one-cell island and a one-cell sea', () => {
  const w = blank(3);
  w.elev[100] = 1; setMoist(w, 0.9);
  update(w);
  T.ok(isSea(w, w.down[100]), 'the island drains into the sea');
  T.near(w.flow[100], 0.9, 1e-6);
  T.near(w.flow[w.down[100]], 0.9, 1e-6, 'a sea cell carries what flows into it');
  T.eq(w.rivers.length, 0);
  checkAll(w, 'one-cell island');
  const v = DM.State.create({ seed: 1, L: 3 });     // land everywhere but one cell
  v.elev.fill(1); v.elev[50] = -1; v.seaLevel = 0; setMoist(v, 0.3);
  update(v);
  T.near(v.flow[50], 0.3 * (v.n - 1), 1e-3, 'the whole flat planet drains into its only sea cell');
  checkAll(v, 'one-cell sea');
  // across a flat, water takes the shortest way out: path length to the sea cell = hop distance
  const hops = new Int32Array(v.n).fill(-1), q = [50];
  hops[50] = 0;
  for (let h = 0; h < q.length; h++) for (const b of nbrs(v, q[h])) if (hops[b] < 0) { hops[b] = hops[q[h]] + 1; q.push(b); }
  for (let c0 = 0; c0 < v.n; c0++) { let c = c0, steps = 0; while (c !== 50) { c = v.down[c]; steps++; } T.eq(steps, hops[c0], 'flats drain by the shortest way (cell ' + c0 + ')'); }
});

// ------------------------------------------------------------------ lakes, by hand
T.test('a cone island has no lake and drains outward', () => {
  const w = update(coneIsland(4, 0.5, 3, 0.2));
  checkAll(w, 'cone');
  for (let c = 0; c < w.n; c++) T.eq(w.water[c], 0);
  for (let c = 0; c < w.n; c++) if (!isSea(w, c) && c !== w._centre) T.ok(w.elev[w.down[c]] < w.elev[c], 'downhill');
});

T.test('one-cell pits: shallower than 0.25 no lake, at least 0.25 deep a one-cell lake', () => {
  let w = coneIsland(4, 0.5, 3, 0.2);
  const p = patch(w, 3, 1)[0];
  dig(w, [p], 0.1); update(w);
  for (let c = 0; c < w.n; c++) { T.eq(w.water[c], 0, 'a shallow one-cell pit holds no water'); T.ok(!Hy.isLake(w, c)); }
  T.ok(nbrs(w, p).indexOf(w.down[p]) >= 0 && w.elev[w.down[p]] > w.elev[p], 'the pit drains through, over its lowest rim');
  T.eq(checkAll(w, 'shallow pit').lakes, 0);
  w = coneIsland(4, 0.5, 3, 0.2);
  dig(w, [p], 0.24); update(w);
  T.eq(w.water[p], 0, '0.24 deep: still no lake');
  w = coneIsland(4, 0.5, 3, 0.2);
  const rim = dig(w, [p], 0.4); update(w);
  T.near(w.water[p], 0.4, 1e-5, 'a deep one-cell pit is a lake');
  T.near(w.elev[p] + w.water[p], rim, 1e-5, 'filled to the rim');
  T.eq(w.fresh[p], 0);
  for (const b of nbrs(w, p)) { T.eq(w.water[b], 0); T.ok(w.fresh[b] <= 1); }
  T.ok(Hy.isLake(w, p) && !Hy.isRiver(w, p));
  const r = checkAll(w, 'deep pit');
  T.eq(r.lakes, 1); T.eq(r.oneCellLakes, 1);
});

T.test('shallow basins: 3 cells make a lake, 2 do not; a mere film does not', () => {
  let w = coneIsland(4, 0.5, 3, 0.2);
  const tri = patch(w, 3, 3);
  T.eq(tri.length, 3);
  dig(w, tri, 0.1); update(w);
  for (const c of tri) { T.near(w.water[c], 0.1, 1e-5, 'three cells 0.1 deep are a lake'); T.eq(w.fresh[c], 0); }
  T.eq(checkAll(w, 'three-cell basin').lakes, 1);
  w = coneIsland(4, 0.5, 3, 0.2);
  dig(w, tri.slice(0, 2), 0.1); update(w);
  for (let c = 0; c < w.n; c++) T.eq(w.water[c], 0, 'two cells 0.1 deep are not');
  checkAll(w, 'two-cell basin');
  w = coneIsland(4, 0.5, 3, 0.2);
  dig(w, tri, 0.02); update(w);
  for (let c = 0; c < w.n; c++) T.eq(w.water[c], 0, 'three cells 0.02 deep would be a film, not a lake');
  checkAll(w, 'film');
});

T.test('lakes need rain (mean moist ≥ 0.15) and are capped at 2.5 deep', () => {
  let w = coneIsland(4, 0.5, 3, 0.2);
  const tri = patch(w, 3, 3);
  dig(w, tri, 0.5); setMoist(w, 0.1); update(w);
  for (let c = 0; c < w.n; c++) T.eq(w.water[c], 0, 'a dry basin stays dry');
  T.eq(w.rivers.length, 0);
  checkAll(w, 'dry basin');
  setMoist(w, 0.1); for (const c of tri) w.moist[c] = 0.2;            // the basin's own cells decide
  update(w);
  for (const c of tri) T.near(w.water[c], 0.5, 1e-5);
  w = coneIsland(4, 0.5, 9, 0.2);                                      // tall enough for the pit to stay above the sea
  const rim = dig(w, tri, 4); update(w);
  for (const c of tri) { T.ok(!isSea(w, c)); T.near(w.water[c], 2.5, 1e-6, 'capped'); T.ok(w.elev[c] + w.water[c] < rim); }
  checkAll(w, 'deep basin');
});

T.test('a pit dug below sea level inland is a sea cell: no water on it, its neighbours drain into it', () => {
  const w = coneIsland(4, 0.5, 3, 0.2), p = patch(w, 3, 1)[0];
  w.elev[p] = -0.5; setMoist(w, 0.5); update(w);
  T.eq(w.water[p], 0); T.eq(w.down[p], -1);
  for (const b of nbrs(w, p)) T.eq(w.down[b], p, 'a neighbour of the inland sea drains into it');
  T.ok(w.flow[p] > 0);
  checkAll(w, 'inland sea');
});

T.test('a crater lake: one level surface, one dry outlet on the lowest point of the rim, a river leaves from it', () => {
  // a wide, gently sloping island with a bowl in the middle of one flank
  const w = coneIsland(5, 0.6, 2.4, 0.1), g = w.geo, ring = [];
  g.ring(w._centre, 6, ring);
  const mid = ring[ring.length - 1], bowl = [];
  const k = g.within(mid, 2.6 * g.step, bowl);
  const cells = bowl.slice(0, k);
  let rimMin = Infinity;
  for (const c of cells) for (const b of nbrs(w, c)) if (cells.indexOf(b) < 0 && w.elev[b] < rimMin) rimMin = w.elev[b];
  for (const c of cells) w.elev[c] = rimMin - 0.2 - 0.6 * (1 - g.angle(mid, c) / (2.6 * g.step));
  update(w);
  const r = checkAll(w, 'crater');
  T.eq(r.lakes, 1);
  for (const c of cells) { T.ok(w.water[c] > 0.03, 'bowl cell under water'); T.near(w.elev[c] + w.water[c], rimMin, 1e-5, 'one flat surface at the spill level'); }
  // all the water above the lake goes through it: one outlet, on the lowest point of the rim
  let outlet = -1;
  for (const c of cells) { const d = w.down[c]; if (cells.indexOf(d) < 0) { T.ok(outlet < 0 || outlet === d, 'one outlet'); outlet = d; } }
  T.ok(outlet >= 0 && w.water[outlet] === 0, 'the outlet cell is dry');
  T.near(w.elev[outlet], rimMin, 1e-6, 'the lake spills over the lowest point of its rim');
  T.ok(Hy.isRiver(w, outlet), 'a river leaves the lake (flow ' + w.flow[outlet].toFixed(1) + ')');
  if (!nbrs(w, outlet).some(b => Hy.isRiver(w, b) && w.down[b] === outlet)) {
    const leaving = w.rivers.filter(rv => rv.cells[0] === outlet);
    T.eq(leaving.length, 1, 'the river leaving the lake starts on the outlet cell (its headwater is the lake)');
  }
  for (const rv of w.rivers) for (let i = 0; i < rv.cells.length - 1; i++) T.ok(!Hy.isLake(w, rv.cells[i]), 'no river runs on through a lake');
});

// ------------------------------------------------------------------ rivers, by hand
T.test('valley continent: one long river down the valley floor to the coast, tributaries, dry ridges', () => {
  const w = update(valleyWorld(5, 0.3)), g = w.geo;
  const r = checkAll(w, 'valley');
  const main = w.rivers[0], len = main.cells.length, mouth = main.cells[len - 1];
  T.ok(len >= 25, 'the main river is long: ' + len + ' cells');
  T.ok(isSea(w, mouth) && g.lat[mouth] * DEG < -37, 'it reaches the south coast');
  for (let i = Math.max(0, len - 20); i < len - 1; i++) T.ok(Math.abs(g.lon[main.cells[i]] * DEG) < 5, 'it runs along the valley floor');
  for (let i = 1; i < len; i++) T.ok(main.flow[i] >= main.flow[i - 1], 'and grows on the way');
  let total = 0, land = 0, inner = 0;
  for (let c = 0; c < w.n; c++) if (!isSea(w, c)) { total += w.moist[c]; land++; if (Math.abs(g.lon[c] * DEG) < RIDGE) inner += w.moist[c]; }
  const share = main.flow[len - 2] / inner;
  T.ok(share > 0.5, 'it carries most of the rain that falls between the ridges (' + (100 * share).toFixed(0) + ' %)');
  T.ok(r.confluences >= 10, 'tributaries join it: ' + r.confluences + ' confluences');
  // the two ridges carry no river (away from the north and south coasts, where the ridge ends in a cliff)
  let ridge = 0;
  for (let c = 0; c < w.n; c++) {
    if (isSea(w, c) || Math.abs(g.lat[c] * DEG) > 34 || Math.abs(Math.abs(g.lon[c] * DEG) - RIDGE) > 2.5) continue;
    ridge++;
    T.ok(!Hy.isRiver(w, c), 'a river on the ridge at cell ' + c + ' (flow ' + w.flow[c].toFixed(2) + ')');
  }
  T.ok(ridge > 40, 'ridge cells checked: ' + ridge);
  // rivers run downhill here (the valley floor is not a lattice line, so it has dimples a few hundredths deep)
  let up = 0;
  for (const rv of w.rivers) for (let i = 1; i < rv.cells.length; i++) if (!isSea(w, rv.cells[i])) up = Math.max(up, w.elev[rv.cells[i]] - w.elev[rv.cells[i - 1]]);
  T.ok(up < 0.05 && r.climb < 0.05, 'a river climbs ' + up.toFixed(3));
  // rivers run to both coasts from the outer slopes too
  T.ok(w.rivers.some(rv => g.lon[rv.cells[0]] * DEG > RIDGE + 3) && w.rivers.some(rv => g.lon[rv.cells[0]] * DEG < -RIDGE - 3), 'rivers on the outer slopes');
  say('valley: ' + land + ' land cells, main river ' + len + ' cells carrying ' + (100 * share).toFixed(0) + ' % of the rain between the ridges (' +
    (100 * main.flow[len - 2] / total).toFixed(0) + ' % of all), ' + r.rivers + ' rivers, ' + r.confluences + ' confluences');
});

T.test('a dry basin on a river\'s course fills up (the river does not climb out of it); off the river it stays dry', () => {
  const dry = 0.05;
  const w = valleyWorld(5, dry);
  const g = w.geo, axis = g.cellAt(Math.cos(-10 / DEG), Math.sin(-10 / DEG), 0);       // on the valley floor, lat −10°
  const flank = g.cellAt(Math.cos(10 / DEG) * Math.cos(20 / DEG), Math.sin(10 / DEG), Math.cos(10 / DEG) * Math.sin(20 / DEG));
  update(w);
  T.ok(Hy.isRiver(w, axis), 'the valley river exists even in a dry land (flow ' + w.flow[axis].toFixed(1) + ')');
  T.ok(!Hy.isRiver(w, flank) && w.flow[flank] < headT(w), 'no river high on the flank (flow ' + w.flow[flank].toFixed(2) + ')');
  dig(w, [axis], 0.6); dig(w, [flank], 0.6);
  update(w);
  T.near(w.water[axis], 0.6, 1e-5, 'the pit on the river is a lake although the land is dry');
  T.eq(w.water[flank], 0, 'the pit on the dry flank is not');
  const r = checkAll(w, 'river-fed lake');
  T.ok(r.climb <= 0.25 + 1e-4, 'no river climbs out of a basin (' + r.climb.toFixed(3) + ')');
  T.ok(w.rivers.some(rv => rv.cells[rv.cells.length - 1] === axis), 'a river ends in the new lake');
});

T.test('where rivers meet, the bigger one continues and the smaller ends on the confluence cell', () => {
  const w = update(H.makeWorld({ L: 5, seed: 2 }));
  let seen = 0;
  for (const rv of w.rivers) {
    const last = rv.cells[rv.cells.length - 1];
    if (isSea(w, last) || Hy.isLake(w, last)) continue;
    seen++;
    const host = w.rivers.filter(o => o !== rv && Array.prototype.indexOf.call(o.cells, last) >= 0 && o.cells[o.cells.length - 1] !== last);
    T.eq(host.length, 1, 'the confluence cell lies on exactly one continuing river');
    const i = Array.prototype.indexOf.call(host[0].cells, last);
    T.ok(i > 0 && host[0].flow[i - 1] >= rv.flow[rv.flow.length - 2], 'the continuing branch carries at least as much');
    T.ok(host[0].flow[i] >= host[0].flow[i - 1] + rv.flow[rv.flow.length - 2] - 1e-3, 'and the flows add up below');
  }
  T.ok(seen >= 8, 'confluences found: ' + seen);
});

// ------------------------------------------------------------------ the three rules beyond the letter of the spec
T.test('TUNE.hydro.pull = 0 gives the plain flood: every slope cell drains to its lowest neighbour', () => {
  const w = H.makeWorld({ L: 4, seed: 7 });
  T.stub(DM.TUNE.hydro, 'pull', 0);
  update(w);
  checkAll(w, 'pull 0');
  const f = bruteFilled(w);
  let slope = 0;
  for (let c = 0; c < w.n; c++) {
    if (isSea(w, c) || nbrs(w, c).some(b => isSea(w, b))) continue;
    let lo = Infinity;
    for (const b of nbrs(w, c)) if (f[b] < lo) lo = f[b];
    if (lo < f[c]) { slope++; T.eq(f[w.down[c]], lo, 'cell ' + c + ' does not take its lowest neighbour'); }
  }
  T.ok(slope > 300);
});

T.test('with the pull, slope cells still only ever drain to a strictly lower neighbour; streams side by side merge', () => {
  let moved = 0, with_ = 0, without = 0;
  const pull0 = tune().pull;
  for (let seed = 1; seed <= 3; seed++) {
    const w = update(H.makeWorld({ L: 5, seed })), f = bruteFilled(w);
    for (let c = 0; c < w.n; c++) {
      if (isSea(w, c) || nbrs(w, c).some(b => isSea(w, b))) continue;
      let lo = Infinity;
      for (const b of nbrs(w, c)) if (f[b] < lo) lo = f[b];
      if (lo < f[c]) { T.ok(f[w.down[c]] < f[c], 'slope cell ' + c + ' drains sideways or uphill'); if (f[w.down[c]] !== lo) moved++; }
    }
    with_ += sideBySide(w, riverCells(w, f).is);
    T.stub(DM.TUNE.hydro, 'pull', 0);
    update(w);
    without += sideBySide(w, riverCells(w, f).is);
    T.stub(DM.TUNE.hydro, 'pull', pull0);
  }
  say('pull: ' + moved + ' slope cells of three L5 helper worlds chose a stream over their lowest neighbour; pairs of river cells running side by side: ' + with_ + ' (' + without + ' without the pull)');
  T.ok(moved > 100, 'the pull moves water: ' + moved);
  T.ok(with_ < 0.7 * without, 'fewer rivers run side by side: ' + with_ + ' against ' + without);
});

T.test('creeks are left out: a river system needs creek × riverT at its mouth; creek = 1 keeps every stream', () => {
  const w = update(H.makeWorld({ L: 5, seed: 3 }));
  const RT = riverT(w), CT = creekT(w), mouth = mouths(w);
  T.ok(CT > RT, 'the default asks more of a river system than of a river cell');
  let over = 0, rc = 0;
  for (let c = 0; c < w.n; c++) {
    if (isSea(w, c) || Hy.isLake(w, c)) continue;
    if (w.flow[c] > RT) over++;
    if (Hy.isRiver(w, c)) { rc++; T.ok(w.flow[mouth[c]] >= CT, 'a river cell in a creek'); }
    else if (w.flow[c] > RT) T.ok(w.flow[mouth[c]] < CT, 'a cell above riverT in a big system must be a river cell');
  }
  T.ok(rc > 0, 'river cells: ' + rc);
  for (const rv of w.rivers) { const last = rv.cells[rv.cells.length - 1]; if (isSea(w, last)) T.ok(rv.flow[rv.flow.length - 2] >= CT, 'a river reaches the sea with less than the creek threshold'); }
  const withCreeks = w.rivers.length;
  T.stub(DM.TUNE.hydro, 'creek', 1);
  T.stub(DM.TUNE.hydro, 'head', 0);
  update(w);
  let rc1 = 0;
  for (let c = 0; c < w.n; c++) if (!isSea(w, c) && !Hy.isLake(w, c)) { T.eq(Hy.isRiver(w, c), w.flow[c] > RT, 'creek = 1, head = 0: river cell ⇔ flow > riverT (the letter of the spec)'); if (Hy.isRiver(w, c)) rc1++; }
  T.eq(rc1, over);
  T.ok(w.rivers.length > withCreeks);
  checkAll(w, 'creek 1, head 0');
  say('L5 seed 3: ' + rc + ' river cells in ' + withCreeks + ' rivers; by the letter of the spec ' + rc1 + ' in ' + w.rivers.length);
});

T.test('headwaters: every river is followed upstream of riverT along its largest inflow; no branch is added', () => {
  const head0 = tune().head;
  for (const w of [H.makeWorld({ L: 5, seed: 1 }), valleyWorld(5, 0.3)]) {
    update(w);
    const RT = riverT(w), HT = headT(w);
    T.ok(HT < RT && HT >= tune().riverMin, 'headT ' + HT + ' below riverT ' + RT);
    const withHead = w.rivers.map(r => Array.from(r.cells)), flow = Array.from(w.flow), down = Array.from(w.down), water = Array.from(w.water);
    let head = 0, total = 0, extended = 0;
    for (let c = 0; c < w.n; c++) if (Hy.isRiver(w, c)) { total++; if (w.flow[c] <= RT) { head++; T.ok(w.flow[c] > HT, 'a headwater cell below headT'); T.ok(Hy.isRiver(w, w.down[c]), 'a headwater that does not lead into a river'); } }
    T.stub(DM.TUNE.hydro, 'head', 0);
    update(w);
    T.eq(Array.from(w.flow), flow, 'head changes no discharge'); T.eq(Array.from(w.down), down); T.eq(Array.from(w.water), water, 'and no lake');
    T.eq(w.rivers.length, withHead.length, 'the same rivers, with or without their headwaters');
    for (let c = 0; c < w.n; c++) if (Hy.isRiver(w, c)) T.ok(w.flow[c] > RT, 'head = 0: no river cell at or below riverT');
    // each river without head is the lower course of exactly one river with head
    const byEnd = {};
    for (const cs of withHead) byEnd[cs[cs.length - 2] + '>' + cs[cs.length - 1]] = cs;
    for (const r of w.rivers) {
      const cs = Array.from(r.cells), full = byEnd[cs[cs.length - 2] + '>' + cs[cs.length - 1]];
      T.ok(full && full.length >= cs.length, 'a river lost its lower course');
      T.eq(full.slice(full.length - cs.length), cs, 'the lower course is unchanged');
      if (full.length > cs.length) extended++;
      // upstream of the old source, each step takes the largest inflow that is not a lake
      for (let i = full.length - cs.length; i > 0; i--) {
        const cur = full[i], up = full[i - 1];
        for (const b of nbrs(w, cur)) if (down[b] === cur && b !== up) T.ok(flow[b] < flow[up] || (flow[b] === flow[up] && b > up), 'the headwater does not follow the largest inflow at ' + cur);
      }
    }
    T.stub(DM.TUNE.hydro, 'head', head0);
    T.ok(head > 0.1 * total && extended > 0.5 * withHead.length, 'headwaters: ' + head + ' of ' + total + ' river cells, ' + extended + ' of ' + withHead.length + ' rivers extended');
    say('headwaters: ' + head + ' of ' + total + ' river cells lie upstream of riverT; ' + extended + ' of ' + withHead.length + ' rivers are longer for it');
  }
});

T.test('fresh: 0 on rivers and lakes, counts hops through sea cells too, 255 when there is no fresh water', () => {
  const w = update(H.makeWorld({ L: 4, seed: 1 }));
  let zero = 0, far = 0;
  for (let c = 0; c < w.n; c++) {
    if (w.fresh[c] === 0) { zero++; T.ok(Hy.isRiver(w, c) || Hy.isLake(w, c)); }
    else {
      let lo = 255;
      for (const b of nbrs(w, c)) lo = Math.min(lo, w.fresh[b]);
      T.eq(w.fresh[c], Math.min(255, lo + 1), 'fresh is one more than the nearest neighbour');
    }
    if (w.fresh[c] > 5) far++;
  }
  T.ok(zero > 0 && far > 0);
  const toSea = w.rivers.find(rv => isSea(w, rv.cells[rv.cells.length - 1]));
  T.ok(toSea, 'a river reaches the sea');
  T.eq(w.fresh[toSea.cells[toSea.cells.length - 1]], 1, 'the sea cell at a river mouth is one hop from fresh water');
});

// ------------------------------------------------------------------ the reference planet
T.test('L 6: update within budget (target 15 ms; asserted at 5×), long branching rivers, lakes, no climbing', () => {
  const w = H.makeWorld({ L: 6, seed: 1 });
  let best = Infinity, sum = 0;
  const N = 8;
  for (let i = 0; i < N; i++) {
    const t0 = process.hrtime.bigint();
    Hy.update(w);
    const ms = Number(process.hrtime.bigint() - t0) / 1e6;
    sum += ms; if (ms < best) best = ms;
  }
  say('L6 update: best ' + best.toFixed(2) + ' ms, mean ' + (sum / N).toFixed(2) + ' ms (' + landCount(w) + ' land cells)');
  T.ok(best <= 75, 'Hydro.update took ' + best.toFixed(1) + ' ms');
  // the invariants at full size, against the same brute-force references
  const r = checkAll(w, 'L6 seed 1'), is = r.is;
  let land = 0, near2 = 0;
  for (let c = 0; c < w.n; c++) if (!isSea(w, c)) { land++; if (w.fresh[c] <= 2) near2++; }
  let long10 = 0, toSea = 0, toLake = 0;
  for (const rv of w.rivers) {
    const len = rv.cells.length, last = rv.cells[len - 1];
    if (len >= 10) long10++;
    if (isSea(w, last)) toSea++; else if (Hy.isLake(w, last)) toLake++;
  }
  const cv = convexShare(w, is), side = sideBySide(w, is);
  say('L6 seed 1: ' + r.rivers + ' rivers (' + toSea + ' reach the sea, ' + toLake + ' a lake, ' + r.confluences + ' join a bigger river), ' + r.riverCells + ' river cells (' +
    (100 * r.riverCells / land).toFixed(1) + ' % of land, ' + r.headCells + ' of them headwater), ' + long10 + ' rivers of 10+ cells, longest ' + r.longest + '; ' + r.lakes + ' lakes, ' + r.lakeCells +
    ' lake cells; land within 2 hops of fresh water ' + (100 * near2 / land).toFixed(0) + ' %; river cells standing proud ' + (100 * cv.rivers).toFixed(1) + ' % (land ' + (100 * cv.land).toFixed(0) +
    ' %); side by side ' + side + ' pairs; highest climb ' + r.climb.toFixed(2));
  T.ok(toSea >= 60, 'river systems reaching the coast: ' + toSea);
  T.ok(r.confluences >= 25, 'branching: ' + r.confluences + ' confluences');
  T.ok(long10 >= 20 && r.longest >= 14, 'long rivers: ' + long10 + ' of 10+ cells, longest ' + r.longest);
  T.ok(r.lakeCells >= 100, 'lakes in basins: ' + r.lakeCells + ' cells');
  T.ok(cv.rivers < 0.05, 'rivers on ridges: ' + (100 * cv.rivers).toFixed(1) + ' %');
  T.ok(r.riverCells / land > 0.04 && r.riverCells / land < 0.14, 'river density ' + (100 * r.riverCells / land).toFixed(1) + ' %');
});

T.test('L 6 worst case: an all-land planet (genesis) stays within budget, wet or dry', () => {
  const w = H.makeWorld({ L: 6, seed: 2, sea: -20, basics: false });
  const time = () => { let best = Infinity; for (let i = 0; i < 4; i++) { const t0 = process.hrtime.bigint(); Hy.update(w); const ms = Number(process.hrtime.bigint() - t0) / 1e6; if (ms < best) best = ms; } return best; };
  const dry = time();
  T.eq(w.rivers.length, 0);
  for (let c = 0; c < w.n; c++) w.moist[c] = 0.3;
  const wet = time();
  say('L6 all land: ' + dry.toFixed(2) + ' ms dry, ' + wet.toFixed(2) + ' ms wet (' + w.rivers.length + ' rivers)');
  T.ok(dry <= 75 && wet <= 75, 'all-land update took ' + dry.toFixed(1) + ' / ' + wet.toFixed(1) + ' ms');
  T.ok(outputsFinite(w));
  mouths(w);                                        // every cell still reaches the outlet
});

// ------------------------------------------------------------------ maps (HYDRO_MAPS=1): look at them
// Equirectangular, north up: hypsometric tint with a light hillshade, lakes in flat blue, rivers as lines from
// cell centre to cell centre, width by the square root of the discharge.
function renderMap(w, file, W, Hh) {
  const M = require('../tools/mapimg.js'), fs = require('fs'), path = require('path');
  const g = w.geo, sea = w.seaLevel, rgb = Buffer.alloc(W * Hh * 3);
  const SEA = [[0, [150, 215, 235]], [0.6, [60, 140, 200]], [2.5, [25, 70, 150]], [6.5, [8, 20, 70]]];
  const LAND = [[0, [96, 150, 84]], [0.6, [146, 178, 100]], [1.8, [205, 195, 128]], [3.5, [158, 128, 100]], [5.5, [205, 205, 210]], [8, [255, 255, 255]]];
  const east = [0, 0, 0], north = [0, 0, 0];
  let hint = -1;
  for (let y = 0; y < Hh; y++) {
    const lat = (0.5 - (y + 0.5) / Hh) * Math.PI, cl = Math.cos(lat), sl = Math.sin(lat);
    for (let x = 0; x < W; x++) {
      const lon = ((x + 0.5) / W - 0.5) * 2 * Math.PI, c = g.cellAt(cl * Math.cos(lon), sl, cl * Math.sin(lon), hint), e = w.elev[c];
      hint = c;
      let col;
      if (e < sea) col = M.ramp(SEA, sea - e);
      else if (w.water[c] > 0.03) col = [40, 110, 230];
      else {
        col = M.ramp(LAND, e - sea);
        g.basis(c, east, north);                    // light from the north-west
        let gx = 0, gy = 0;
        for (const b of nbrs(w, c)) {
          const dx = g.pos[3 * b] - g.pos[3 * c], dy = g.pos[3 * b + 1] - g.pos[3 * c + 1], dz = g.pos[3 * b + 2] - g.pos[3 * c + 2], de = Math.max(w.elev[b], sea) - e;
          gx += de * (dx * east[0] + dy * east[1] + dz * east[2]); gy += de * (dx * north[0] + dy * north[1] + dz * north[2]);
        }
        const s = Math.max(-0.3, Math.min(0.3, (gx - gy) * 6));
        col = [col[0] * (1 + s), col[1] * (1 + s), col[2] * (1 + s)];
      }
      const o = (y * W + x) * 3;
      rgb[o] = Math.max(0, Math.min(255, col[0])); rgb[o + 1] = Math.max(0, Math.min(255, col[1])); rgb[o + 2] = Math.max(0, Math.min(255, col[2]));
    }
  }
  const dot = (x, y, r) => {
    const R = Math.ceil(r + 1), x0 = Math.round(x), y0 = Math.round(y);
    for (let dy = -R; dy <= R; dy++) for (let dx = -R; dx <= R; dx++) {
      const px = x0 + dx, py = y0 + dy, a = Math.max(0, Math.min(1, r + 0.5 - Math.hypot(px - x, py - y)));
      if (a <= 0 || px < 0 || py < 0 || px >= W || py >= Hh) continue;
      const o = (py * W + px) * 3;
      rgb[o] += (15 - rgb[o]) * a; rgb[o + 1] += (60 - rgb[o + 1]) * a; rgb[o + 2] += (235 - rgb[o + 2]) * a;
    }
  };
  let maxFlow = 1e-9;
  for (const r of w.rivers) for (let i = 0; i < r.flow.length - 1; i++) if (r.flow[i] > maxFlow) maxFlow = r.flow[i];
  const px = c => [(g.lon[c] / (2 * Math.PI) + 0.5) * W - 0.5, (0.5 - g.lat[c] / Math.PI) * Hh - 0.5];
  for (const r of w.rivers) {
    for (let i = 0; i < r.cells.length - 1; i++) {
      const a = px(r.cells[i]), b = px(r.cells[i + 1]);
      if (Math.abs(a[0] - b[0]) > W / 2) continue;      // across the date line
      const rad = (0.45 + 2.2 * Math.sqrt(r.flow[i] / maxFlow)) * W / 2048, steps = Math.max(1, Math.ceil(Math.hypot(b[0] - a[0], b[1] - a[1])));
      for (let s = 0; s <= steps; s++) dot(a[0] + (b[0] - a[0]) * s / steps, a[1] + (b[1] - a[1]) * s / steps, rad);
    }
  }
  const p = path.resolve(__dirname, '..', file);
  fs.mkdirSync(path.dirname(p), { recursive: true });
  fs.writeFileSync(p, M.encodePNG(W, Hh, rgb));
  return p;
}
function describe(w) {
  let land = 0, rc = 0, lakeCells = 0, long10 = 0, longest = 0, toSea = 0;
  for (let c = 0; c < w.n; c++) if (!isSea(w, c)) { land++; if (Hy.isRiver(w, c)) rc++; if (Hy.isLake(w, c)) lakeCells++; }
  for (const r of w.rivers) { const len = r.cells.length; if (len >= 10) long10++; if (len > longest) longest = len; if (isSea(w, r.cells[len - 1])) toSea++; }
  return w.rivers.length + ' rivers (' + toSea + ' reach the sea, ' + long10 + ' of 10+ cells, longest ' + longest + '), ' + rc + ' river cells (' + (100 * rc / land).toFixed(1) + ' % of land), ' + lakeCells + ' lake cells';
}
if (process.env.HYDRO_MAPS) {
  T.test('maps: shots/hydro-rivers-s1…3.png (helper worlds) and shots/hydro-real-s1…3.png (the game\'s planets)', () => {
    for (let seed = 1; seed <= 3; seed++) {
      const w = update(H.makeWorld({ L: 6, seed }));
      console.log('    wrote ' + renderMap(w, 'shots/hydro-rivers-s' + seed + '.png', 2048, 1024) + ': ' + describe(w));
    }
    if (!DM.Terrain || !DM.Terrain.generate || !DM.Climate || !DM.Climate.update) { console.log('    (terrain.js / climate.js not loaded: no hydro-real maps)'); return; }
    for (let seed = 1; seed <= 3; seed++) {
      const w = DM.State.create({ seed, L: 6 });
      DM.Terrain.generate(w);
      w.seaLevel = 0; w.heat = 0; w.phase = 'history';
      DM.Terrain.derive(w); DM.Climate.update(w);
      update(w);
      console.log('    wrote ' + renderMap(w, 'shots/hydro-real-s' + seed + '.png', 2048, 1024) + ': ' + describe(w));
    }
  });
}
