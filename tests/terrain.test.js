// DEMIURGE — tests/terrain.test.js : DM.Terrain (SPEC-SIM §3.2, BUILD.md A2).
//   node tests/terrain.test.js                    (or: node tools/test.js terrain)
//   TERRAIN_REPORT=1 node tests/terrain.test.js   also prints the per-seed acceptance table and timings
// Loads only the foundation, schema.js and terrain.js. Terrain calls no other module and emits no events.
'use strict';
const T = require('../tools/t.js');
const DM = require('../tools/load.js')({ only: ['terrain'] });
T.watchEvents();

const Terrain = DM.Terrain, State = DM.State, U = DM.U;
const REPORT = !!process.env.TERRAIN_REPORT;

// ------------------------------------------------------------------ helpers

function fresh(L, seed) { return State.create({ seed: seed === undefined ? 1 : seed, L }); }
function clearFlags(w) { for (const k in w.dirty) w.dirty[k] = false; for (const k in w.need) w.need[k] = false; }
function generated(L, seed) { const w = fresh(L, seed); Terrain.generate(w); w.seaLevel = 0; clearFlags(w); return w; }
function flat(L, level) { const w = fresh(L, 1); w.elev.fill(level); w.seaLevel = 0; clearFlags(w); return w; }
function ms(fn) { const t0 = process.hrtime.bigint(); fn(); return Number(process.hrtime.bigint() - t0) / 1e6; }
function sameArray(a, b) {
  if (a.length !== b.length) return false;
  for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false;
  return true;
}
function allFinite(w) {
  const lo = State.ELEV_MIN, hi = State.ELEV_MAX;
  for (let c = 0; c < w.n; c++) if (!(w.elev[c] >= lo && w.elev[c] <= hi) || !(w.fert[c] >= 0 && w.fert[c] <= 1.5)) return false;
  return true;
}

// Connected land components by an explicit-stack flood fill (independent of Terrain.derive).
function components(w, sea) {
  const g = w.geo, n = w.n, lab = new Int32Array(n).fill(-1), sizes = [], stack = [];
  for (let c0 = 0; c0 < n; c0++) {
    if (lab[c0] >= 0 || w.elev[c0] < sea) continue;
    const id = sizes.length;
    let size = 0;
    stack.push(c0); lab[c0] = id;
    while (stack.length) {
      const c = stack.pop();
      size++;
      for (let k = 0; k < g.nbrN[c]; k++) {
        const m = g.nbr[6 * c + k];
        if (lab[m] < 0 && w.elev[m] >= sea) { lab[m] = id; stack.push(m); }
      }
    }
    sizes.push(size);
  }
  return { lab, sizes };
}

// Everything the acceptance checks and the generation targets look at, at sea level 0.
function stats(w) {
  const n = w.n, e = w.elev, g = w.geo;
  let land = 0, lo = Infinity, hi = -Infinity, low = 0, high = 0, shelf = 0, deep = 0, polar = 0, belt = 0;
  for (let c = 0; c < n; c++) {
    const v = e[c];
    if (v < lo) lo = v;
    if (v > hi) hi = v;
    if (v >= 0) {
      land++;
      if (v < 0.6) low++;
      if (Math.abs(g.lat[c]) > Math.PI / 3) polar++;
      if (v > 3.5) {
        high++;
        let k = 0;
        for (let j = 0; j < g.nbrN[c]; j++) if (e[g.nbr[6 * c + j]] > 2.5) k++;
        if (k >= 2) belt++;
      }
    } else {
      if (v > -0.6) shelf++;
      if (v < -3) deep++;
    }
  }
  const sizes = components(w, 0).sizes.sort((a, b) => b - a);
  const sea = n - land;
  return {
    land: land / n, lo, hi, low: low / land, high: high / land, shelf: shelf / sea, deep: deep / sea, polar: polar / land,
    belt: high ? belt / high : 0, sizes, top: sizes[0] / land,
    majors3: sizes.filter(s => s >= 0.03 * n).length, majors2: sizes.filter(s => s >= 0.02 * n).length,
    islands: sizes.length - sizes.filter(s => s >= 0.03 * n).length,
    second: sizes.length > 1 ? sizes[1] / n : 0,
  };
}

// BUILD.md A2, first sentence.
function checkAcceptance(s, tag) {
  T.ok(s.land >= 0.36 && s.land <= 0.42, tag + ': land fraction ' + s.land.toFixed(4));
  T.ok(s.lo >= -7 && s.hi <= 8.6, tag + ': elevation range ' + s.lo.toFixed(2) + ' … ' + s.hi.toFixed(2));
  T.ok(s.low >= 0.25, tag + ': share of land below +0.6 = ' + s.low.toFixed(3));
  T.ok(s.high >= 0.02, tag + ': share of land above +3.5 = ' + s.high.toFixed(3));
  T.ok(s.majors3 >= 2, tag + ': landmasses ≥ 3 % of the planet = ' + s.majors3);
  T.ok(s.islands >= 3, tag + ': further islands = ' + s.islands);
}
function row(L, seed, s, t) {
  return 'L' + L + ' seed ' + String(seed).padStart(2) + '  land ' + (100 * s.land).toFixed(1) + ' %  elev ' + s.lo.toFixed(2) + ' … ' + s.hi.toFixed(2) +
    '  <0.6: ' + (100 * s.low).toFixed(0) + ' %  >3.5: ' + (100 * s.high).toFixed(1) + ' %  masses(% of planet) ' +
    s.sizes.slice(0, 5).map(x => (100 * x / (s.sizes.reduce((a, b) => a + b, 0) / s.land)).toFixed(1)).join('/') +
    '  islands ' + s.islands + '  shelf ' + (100 * s.shelf).toFixed(0) + ' %' + (t === undefined ? '' : '  ' + t.toFixed(0) + ' ms');
}

const L5_SEEDS = [];
for (let s = 1; s <= 20; s++) L5_SEEDS.push(s);
const cache5 = new Map();
function world5(seed) {
  if (!cache5.has(seed)) { const w = generated(5, seed); cache5.set(seed, { w, s: stats(w) }); }
  return cache5.get(seed);
}

// ------------------------------------------------------------------ API surface

T.test('exposes the documented API and a TUNE block', () => {
  for (const k of ['generate', 'sculpt', 'crater', 'cone', 'roughen', 'touched', 'derive']) T.eq(typeof Terrain[k], 'function', k);
  T.ok(DM.TUNE && DM.TUNE.terrain && typeof DM.TUNE.terrain.land === 'number', 'TUNE.terrain');
});

// ------------------------------------------------------------------ generate

T.test('generate: acceptance statistics for 20 seeds at L = 5', () => {
  for (const seed of L5_SEEDS) {
    const { s } = world5(seed);
    if (REPORT) console.log(row(5, seed, s));
    checkAcceptance(s, 'L5 seed ' + seed);
  }
});

T.test('generate: acceptance statistics and time for 3 seeds at L = 6', () => {
  DM.Geo.get(6);                                                  // build the sphere outside the timed part
  for (const seed of [1, 2, 3]) {
    const w = fresh(6, seed);
    const t = ms(() => Terrain.generate(w));
    w.seaLevel = 0;
    const s = stats(w);
    if (REPORT) console.log(row(6, seed, s, t));
    checkAcceptance(s, 'L6 seed ' + seed);
    T.ok(allFinite(w), 'finite elev and fert');
    T.ok(t < 2500, 'generate took ' + t.toFixed(0) + ' ms (target 500 ms, asserted at 5×)');
  }
});

T.test('generate: identical output for identical seeds (20 seeds at L = 5, one at L = 6)', () => {
  for (const seed of L5_SEEDS) {
    const a = world5(seed).w, b = fresh(5, seed);
    Terrain.generate(b);
    T.ok(sameArray(a.elev, b.elev), 'elev differs for seed ' + seed);
    T.ok(sameArray(a.fert, b.fert), 'fert differs for seed ' + seed);
  }
  const a = fresh(6, 3), b = fresh(6, 3);
  Terrain.generate(a); Terrain.generate(b);
  T.ok(sameArray(a.elev, b.elev) && sameArray(a.fert, b.fert), 'L6 seed 3');
});

T.test('generate: depends on the seed only (not on earlier calls, the world name or the rng position)', () => {
  const a = fresh(4, 77);
  Terrain.generate(a);
  const other = fresh(5, 78);
  Terrain.generate(other);                                        // different size and seed in between
  const b = State.create({ seed: 77, L: 4, name: 'Another name' });
  for (let i = 0; i < 13; i++) b.rng.next();                      // the world's own stream is not what generate reads
  Terrain.generate(b);
  T.ok(sameArray(a.elev, b.elev) && sameArray(a.fert, b.fert), 'same seed, different history');
  Terrain.generate(b);                                            // regenerating in place gives the same planet again
  T.ok(sameArray(a.elev, b.elev) && sameArray(a.fert, b.fert), 'regenerated in place');
  const c = fresh(4, 78);
  Terrain.generate(c);
  let diff = 0;
  for (let i = 0; i < a.n; i++) if (a.elev[i] !== c.elev[i]) diff++;
  T.ok(diff > 0.9 * a.n, 'another seed gives another planet (' + diff + ' of ' + a.n + ' cells differ)');
});

T.test('generate: writes only elev and fert, does not advance world.rng, sets every dirty and need flag', () => {
  const w = fresh(4, 5);
  clearFlags(w);
  const rng0 = w.rng.s;
  Terrain.generate(w);
  T.eq(w.rng.s, rng0, 'world.rng untouched');
  for (const k in w.dirty) T.ok(w.dirty[k] === true, 'dirty.' + k);
  for (const k in w.need) T.ok(w.need[k] === true, 'need.' + k);
  for (const f of State.CELL_FIELDS) {
    if (f.name === 'elev' || f.name === 'fert') continue;
    const a = w[f.name];
    let same = true;
    for (let c = 0; c < a.length; c++) if (a[c] !== f.def) { same = false; break; }
    T.ok(same, f.name + ' left at its default');
  }
  T.eq(w.seaLevel, -9, 'seaLevel');
  T.eq(w.phase, 'molten', 'phase');
  T.eq(w.heat, 1, 'heat');
  T.eq(w.year, 0, 'year');
  T.eq(w.settlements.length + w.cultures.length + w.units.length + w.disasters.length, 0, 'no entities');
  T.ok(allFinite(w), 'finite, inside the clamps');
});

T.test('generate: fertility is varied, in range, and richer in the lowlands than in the mountains', () => {
  for (const seed of [1, 2, 3]) {
    const { w } = world5(seed);
    let n = 0, sum = 0, sum2 = 0, rich = 0, poor = 0, lowSum = 0, lowN = 0, highSum = 0, highN = 0;
    for (let c = 0; c < w.n; c++) {
      const f = w.fert[c];
      T.ok(f >= 0 && f <= 1.5, 'fert in range');
      if (w.elev[c] < 0) continue;
      n++; sum += f; sum2 += f * f;
      if (f > 0.7) rich++;
      if (f < 0.4) poor++;
      if (w.elev[c] < 0.6) { lowSum += f; lowN++; } else if (w.elev[c] > 3) { highSum += f; highN++; }
    }
    const mean = sum / n, sd = Math.sqrt(sum2 / n - mean * mean);
    T.ok(mean > 0.4 && mean < 0.7, 'mean land fertility ' + mean.toFixed(3));
    T.ok(sd > 0.08, 'standard deviation ' + sd.toFixed(3));
    T.ok(rich > 0.03 * n && poor > 0.03 * n, 'rich ' + rich + ', poor ' + poor + ' of ' + n);
    T.ok(lowSum / lowN > highSum / highN + 0.1, 'lowlands ' + (lowSum / lowN).toFixed(2) + ' vs mountains ' + (highSum / highN).toFixed(2));
  }
});

T.test('generate: the planet has the shape the spec asks for (20 seeds at L = 5)', () => {
  let threeOrMore = 0;
  for (const seed of L5_SEEDS) {
    const { s } = world5(seed), tag = 'seed ' + seed + ': ';
    T.ok(s.hi >= 6 && s.hi <= 8.5, tag + 'highest peak ' + s.hi.toFixed(2));
    T.ok(s.lo <= -5.8 && s.lo >= -6.8, tag + 'deepest ocean floor ' + s.lo.toFixed(2));
    T.ok(s.shelf >= 0.12, tag + 'continental shelf: ' + (100 * s.shelf).toFixed(0) + ' % of the sea is shallower than 0.6');
    T.ok(s.deep >= 0.3, tag + 'deep ocean: ' + (100 * s.deep).toFixed(0) + ' % of the sea is deeper than 3');
    T.ok(s.polar >= 0.01, tag + 'land beyond 60° latitude: ' + (100 * s.polar).toFixed(1) + ' % of the land');
    T.ok(s.majors2 >= 2 && s.majors2 <= 7, tag + 'landmasses ≥ 2 % of the planet: ' + s.majors2);
    T.ok(s.top <= 0.75, tag + 'largest landmass holds ' + (100 * s.top).toFixed(0) + ' % of the land');
    T.ok(s.belt >= 0.7, tag + 'mountains form belts, not speckle: ' + (100 * s.belt).toFixed(0) + ' % of high cells have high neighbours');
    if (s.majors2 >= 3) threeOrMore++;
  }
  T.ok(threeOrMore >= 17, '3 or more major landmasses on ' + threeOrMore + ' of 20 seeds');
});

T.test('generate: works at every subdivision level and for edge-case seeds', () => {
  for (const L of [0, 1, 2, 3, 4]) {
    for (const seed of [0, 1, 0xffffffff]) {
      const w = fresh(L, seed);
      Terrain.generate(w);
      T.ok(allFinite(w), 'L' + L + ' seed ' + seed + ': finite, inside the clamps');
      let land = 0;
      for (let c = 0; c < w.n; c++) if (w.elev[c] >= 0) land++;
      T.ok(land > 0 && land < w.n, 'L' + L + ': both land and sea');
      if (L >= 3) T.ok(land / w.n >= 0.36 && land / w.n <= 0.42, 'L' + L + ' seed ' + seed + ': land fraction ' + (land / w.n).toFixed(3));
    }
  }
});

// ------------------------------------------------------------------ touched

T.test('touched: sets dirty.elev / water / color and need.derive / climate / hydro, nothing else', () => {
  const w = fresh(3);
  clearFlags(w);
  Terrain.touched(w);
  T.eq(w.dirty, { elev: true, color: true, water: true, objects: false, rivers: false, roads: false });
  T.eq(w.need, { derive: true, climate: true, hydro: true, life: false });
});

// ------------------------------------------------------------------ sculpt

T.test('sculpt: raises the centre by amount, falls smoothly to 0 at ang, touches nothing outside', () => {
  const w = flat(5, 1), g = w.geo, c = 1234, ang = 0.25, amount = 2;
  const n = Terrain.sculpt(w, c, ang, amount);
  T.near(w.elev[c], 1 + amount, 1e-5, 'centre');
  let inside = 0, edgeMax = 0, stepMax = 0;
  for (let m = 0; m < w.n; m++) {
    const t = g.angle(c, m) / ang, d = w.elev[m] - 1;
    if (t >= 1) { T.ok(w.elev[m] === 1, 'cell outside the brush changed (t = ' + t.toFixed(3) + ')'); continue; }
    inside++;
    const want = amount * (1 - t * t) * (1 - t * t);
    T.near(d, want, 2e-3 * amount, 'falloff at t = ' + t.toFixed(3));
    T.ok(d >= 0 && d <= amount + 1e-6, 'between 0 and amount');
    if (t > 0.9 && d > edgeMax) edgeMax = d;
    for (let k = 0; k < g.nbrN[m]; k++) { const dd = Math.abs(w.elev[g.nbr[6 * m + k]] - w.elev[m]); if (dd > stepMax) stepMax = dd; }
  }
  T.ok(inside > 100, 'brush covers ' + inside + ' cells');
  T.eq(n, inside, 'returns the number of cells changed');
  T.ok(edgeMax < 0.04 * amount, 'reaches 0 at the edge without a step (largest change in the outer 10 %: ' + edgeMax.toFixed(4) + ')');
  // (1 − t²)² is never steeper than 1.54 / ang: no cliffs between neighbouring cells
  T.ok(stepMax < 1.54 * amount * (1.25 * g.step) / ang * 1.15, 'smooth: largest step between neighbours ' + stepMax.toFixed(3));
});

T.test('sculpt: negative amounts lower, repeated strokes add up, flags are set', () => {
  const w = flat(4, 0.5), c = 300;
  T.ok(Terrain.sculpt(w, c, 0.2, -1.5) > 0);
  T.near(w.elev[c], -1, 1e-5, 'lowered');
  T.eq(w.dirty, { elev: true, color: true, water: true, objects: false, rivers: false, roads: false });
  T.eq(w.need, { derive: true, climate: true, hydro: true, life: false });
  for (let i = 0; i < 10; i++) Terrain.sculpt(w, c, 0.2, 0.15);
  T.near(w.elev[c], 0.5, 1e-4, 'ten strokes of +0.15 undo −1.5');
  let worst = 0;
  for (let m = 0; m < w.n; m++) worst = Math.max(worst, Math.abs(w.elev[m] - 0.5));
  T.ok(worst < 1e-4, 'the whole brush returned to where it was');
});

T.test('sculpt: clamps to State.ELEV_MIN … ELEV_MAX', () => {
  const w = flat(4, 0), c = 77;
  Terrain.sculpt(w, c, 0.3, 1000);
  T.eq(w.elev[c], State.ELEV_MAX, 'top clamp');
  Terrain.sculpt(w, c, 0.3, -5000);
  T.eq(w.elev[c], State.ELEV_MIN, 'bottom clamp');
  for (let m = 0; m < w.n; m++) T.ok(w.elev[m] >= State.ELEV_MIN && w.elev[m] <= State.ELEV_MAX, 'inside the clamps');
});

T.test('sculpt: tiny, huge and odd brushes; the poles', () => {
  const w = flat(4, 0);
  T.eq(Terrain.sculpt(w, 10, 0, 1), 1, 'ang = 0 moves the centre cell only');
  T.near(w.elev[10], 1, 1e-6);
  T.eq(Terrain.sculpt(w, 20, -3, 1), 1, 'negative ang behaves as 0');
  T.eq(Terrain.sculpt(w, 30, NaN, 1), 1, 'NaN ang behaves as 0');
  T.eq(Terrain.sculpt(w, 40, w.geo.step * 0.4, 1), 1, 'a brush smaller than a cell');
  for (const pole of [0, 11]) {
    const n = Terrain.sculpt(w, pole, 0.3, 1);
    T.ok(n > 20, 'pole brush covers ' + n + ' cells');
    T.near(w.elev[pole], 1, 1e-6, 'pole centre');
  }
  const w2 = flat(3, 0);
  const n = Terrain.sculpt(w2, 5, 10, 1);                          // larger than the planet: clamped to π
  T.ok(n >= w2.n - 1, 'whole planet');
  for (let m = 0; m < w2.n; m++) T.ok(Number.isFinite(w2.elev[m]) && w2.elev[m] >= 0 && w2.elev[m] <= 1 + 1e-6, 'finite');
});

T.test('operations ignore bad targets and amounts: return 0, change nothing, set no flags, never throw', () => {
  const w = generated(4, 3), before = Float32Array.from(w.elev), rng = U.rng(1);
  const badCells = [-1, w.n, w.n + 5, 1.5, NaN, Infinity, undefined, null, '7'];
  for (const c of badCells) {
    T.eq(Terrain.sculpt(w, c, 0.1, 1), 0, 'sculpt ' + c);
    T.eq(Terrain.crater(w, c, 0.1, 1), 0, 'crater ' + c);
    T.eq(Terrain.cone(w, c, 0.1, 1), 0, 'cone ' + c);
    T.eq(Terrain.roughen(w, c, 0.1, 1, rng), 0, 'roughen ' + c);
  }
  for (const a of [0, NaN, Infinity, -Infinity, undefined]) {
    T.eq(Terrain.sculpt(w, 5, 0.1, a), 0, 'sculpt amount ' + a);
    T.eq(Terrain.roughen(w, 5, 0.1, a, rng), 0, 'roughen amount ' + a);
  }
  for (const a of [0, -1, NaN, Infinity, undefined]) {
    T.eq(Terrain.crater(w, 5, 0.1, a), 0, 'crater depth ' + a);
    T.eq(Terrain.cone(w, 5, 0.1, a), 0, 'cone height ' + a);
  }
  T.ok(sameArray(w.elev, before), 'elevation untouched');
  T.eq(w.dirty.elev, false, 'no dirty flag');
  T.eq(w.need.derive, false, 'no need flag');
});

// ------------------------------------------------------------------ crater

T.test('crater: on level ground, a bowl of the given depth with a raised rim at 1.0–1.3 × ang (meteor size at L = 6)', () => {
  const w = flat(6, 1), g = w.geo, c = 20000, ang = 0.05, depth = 1.6;
  const n = Terrain.crater(w, c, ang, depth);
  T.near(w.elev[c], 1 - depth, 1e-4, 'centre is `depth` below the old ground');
  let rimCells = 0, rimMax = 0, rimMaxT = 0, changed = 0, bowl = 0;
  for (let m = 0; m < w.n; m++) {
    const t = g.angle(c, m) / ang, d = w.elev[m] - 1;
    if (d !== 0) changed++;
    if (t > 1.5) { T.ok(d === 0, 'cell beyond 1.5 × ang changed'); continue; }
    if (t <= 0.8) { bowl++; T.near(d, -depth * (1 - t * t), 3e-3 * depth, 'bowl profile at t = ' + t.toFixed(2)); }
    if (t >= 1 && t <= 1.3) { rimCells++; T.ok(d > 0.05 * depth, 'rim cell at t = ' + t.toFixed(2) + ' stands ' + d.toFixed(3) + ' above the old level'); }
    if (d > rimMax) { rimMax = d; rimMaxT = t; }
  }
  T.ok(bowl >= 7 && rimCells >= 6, bowl + ' bowl cells, ' + rimCells + ' rim cells');
  T.ok(rimMax > 0.1 * depth && rimMax < 0.3 * depth, 'rim height ' + rimMax.toFixed(3));
  T.ok(rimMaxT >= 0.95 && rimMaxT <= 1.35, 'rim crest at ' + rimMaxT.toFixed(2) + ' × ang');
  T.eq(n, changed, 'returns the number of cells changed');
  T.eq(w.dirty.elev && w.dirty.water && w.dirty.color && w.need.derive && w.need.climate && w.need.hydro, true, 'flags');
});

T.test('crater: on real terrain the floor drops, the rim rises above the old level, nothing moves beyond 1.5 × ang', () => {
  const w = generated(5, 2), g = w.geo, rng = U.rng(9);
  Terrain.derive(w);
  let done = 0;
  for (let i = 0; i < 4000 && done < 25; i++) {
    const c = rng.int(0, w.n - 1);
    if (w.elev[c] < 0.1 || w.elev[c] > 2 || w.slope[c] > 0.6) continue;     // gentle land
    done++;
    const ang = rng.range(0.06, 0.2), depth = rng.range(0.4, 2);
    const before = Float32Array.from(w.elev);
    Terrain.crater(w, c, ang, depth);
    let inN = 0, inSum = 0, rimN = 0, rimSum = 0;
    for (let m = 0; m < w.n; m++) {
      const t = g.angle(c, m) / ang, d = w.elev[m] - before[m];
      if (t > 1.5) { T.ok(d === 0, 'outside'); continue; }
      if (t < 0.6) { inN++; inSum += d; }
      if (t >= 1 && t <= 1.3) { rimN++; rimSum += w.elev[m]; T.ok(d > 0, 'rim cell above its old level'); }
    }
    T.ok(inSum / inN < -0.5 * depth, 'floor dropped by ' + (-inSum / inN).toFixed(2) + ' for depth ' + depth.toFixed(2));
    T.ok(rimN > 0 && w.elev[c] < rimSum / rimN, 'the centre lies below the rim');
  }
  T.eq(done, 25, 'found 25 sites');
});

T.test('crater: smaller than a cell still digs the centre and raises a rim; clamps', () => {
  const w = flat(4, 2), g = w.geo, c = 500;
  T.ok(Terrain.crater(w, c, 0, 1) >= 2);
  T.near(w.elev[c], 1, 1e-5, 'centre down by depth');
  let up = 0;
  for (let k = 0; k < g.nbrN[c]; k++) if (w.elev[g.nbr[6 * c + k]] > 2) up++;
  T.ok(up >= 3, up + ' neighbours form a rim');
  Terrain.crater(w, c, 0.2, 500);
  T.eq(w.elev[c], State.ELEV_MIN, 'clamped');
  for (let m = 0; m < w.n; m++) T.ok(w.elev[m] >= State.ELEV_MIN && w.elev[m] <= State.ELEV_MAX, 'inside the clamps');
});

// ------------------------------------------------------------------ cone

T.test('cone: on level ground, a peak of the given height with a summit pit (volcano size at L = 6)', () => {
  const w = flat(6, 0.5), g = w.geo, c = 31000, ang = 0.04, height = 1.8;
  const n = Terrain.cone(w, c, ang, height);
  let top = 0, changed = 0;
  for (let m = 0; m < w.n; m++) {
    const d = w.elev[m] - 0.5;
    if (d !== 0) changed++;
    if (g.angle(c, m) >= ang) T.ok(d === 0, 'cell outside the cone changed');
    T.ok(d >= 0 && d <= height * 1.0001, 'never lower than before, never above the stated height');
    if (d > top) top = d;
  }
  T.ok(top > 0.9 * height, 'crest reaches ' + top.toFixed(3) + ' of ' + height);
  T.near(w.elev[c] - 0.5, 0.75 * height, 1e-3, 'pit floor');
  let ringMin = Infinity;
  for (let k = 0; k < g.nbrN[c]; k++) ringMin = Math.min(ringMin, w.elev[g.nbr[6 * c + k]]);
  T.ok(ringMin > w.elev[c] + 0.1 * height, 'every neighbour of the vent stands above it (crest ' + ringMin.toFixed(2) + ', pit ' + w.elev[c].toFixed(2) + ')');
  T.ok(changed >= 13, 'the flanks reach beyond the crest ring (' + changed + ' cells)');
  T.eq(n, changed, 'returns the number of cells changed');
  T.eq(w.dirty.elev && w.dirty.water && w.dirty.color && w.need.derive && w.need.climate && w.need.hydro, true, 'flags');
});

T.test('cone: a wide cone has a crater bowl, a crest ring and flanks that fall steadily to the foot', () => {
  const w = flat(5, 0), g = w.geo, c = 4000, ang = 0.4, height = 3, P = DM.TUNE.terrain;
  Terrain.cone(w, c, ang, height);
  const rc = P.conePit * ang;
  T.near(w.elev[c], height * (1 - P.conePitDepth), 1e-3, 'pit floor');
  let top = 0, topD = 0;
  for (let m = 0; m < w.n; m++) {
    const d = g.angle(c, m), e = w.elev[m];
    if (e > top) { top = e; topD = d; }
    if (d >= ang) { T.ok(e === 0, 'outside'); continue; }
    if (d > rc) {
      const q = 1 - (d - rc) / (ang - rc);
      T.near(e, height * q * Math.sqrt(q), 5e-3 * height, 'flank at d = ' + d.toFixed(3));
    } else T.ok(e >= height * (1 - P.conePitDepth) - 1e-3 && e <= height + 1e-6, 'inside the crater');
  }
  T.ok(top > 0.95 * height && Math.abs(topD - rc) < 1.2 * g.step, 'crest ' + top.toFixed(2) + ' at ' + topD.toFixed(3) + ' rad (ring at ' + rc.toFixed(3) + ')');
});

T.test('cone: on real terrain it builds on the vent, keeps its pit and never digs', () => {
  DM.Geo.get(6);
  const w = generated(6, 2), g = w.geo, rng = U.rng(4), ang = 0.04, height = 1.8;
  let done = 0, sea = 0;
  for (let i = 0; i < 4000 && done < 40; i++) {
    const c = rng.int(0, w.n - 1);
    if (w.elev[c] < -1) continue;                                  // land and shallow sea, any slope
    done++;
    const before = Float32Array.from(w.elev), ref = before[c];
    Terrain.cone(w, c, ang, height);
    T.near(w.elev[c], ref + 0.75 * height, 1e-3, 'pit floor is 0.75 × height above the old vent');
    let peak = -Infinity;
    for (let k = 0; k < g.nbrN[c]; k++) {
      const m = g.nbr[6 * c + k];
      T.ok(w.elev[m] > w.elev[c], 'neighbour above the pit');
      if (w.elev[m] > peak) peak = w.elev[m];
    }
    T.ok(peak >= ref + 0.9 * height, 'crest at least 0.9 × height above the old vent');
    for (let m = 0; m < w.n; m++) {
      T.ok(w.elev[m] >= before[m], 'no cell is lowered');
      if (g.angle(c, m) >= ang) T.ok(w.elev[m] === before[m], 'outside');
    }
    if (ref < 0) { sea++; T.ok(w.elev[c] > 0, 'a cone in shallow water makes an island'); }
    w.elev.set(before);
  }
  T.eq(done, 40, 'found 40 sites');
  T.ok(sea >= 3, 'some of them under shallow water (' + sea + ')');
});

T.test('cone: smaller than the mesh can show is a plain peak; clamps', () => {
  const w = flat(4, 0), c = 900;                                   // ang 0.04 at L = 4 is half a cell
  T.eq(Terrain.cone(w, c, 0.04, 1.8), 1, 'one cell');
  T.near(w.elev[c], 1.8, 1e-5, 'raised by the full height');
  Terrain.cone(w, c, 0.5, 1000);
  for (let m = 0; m < w.n; m++) T.ok(w.elev[m] >= 0 && w.elev[m] <= State.ELEV_MAX, 'inside the clamps');
});

// ------------------------------------------------------------------ roughen

function pits(w) {                                                  // one-cell pits: cells strictly below every neighbour
  const g = w.geo, out = new Set();
  for (let c = 0; c < w.n; c++) {
    let pit = true;
    for (let k = 0; k < g.nbrN[c]; k++) if (!(w.elev[g.nbr[6 * c + k]] > w.elev[c])) { pit = false; break; }
    if (pit) out.add(c);
  }
  return out;
}

T.test('roughen: random ± displacement up to `amount`, smooth, inside the patch, flags set', () => {
  const w = flat(5, 1), g = w.geo, c = 2000, ang = 0.3, amount = 0.8;
  const n = Terrain.roughen(w, c, ang, amount, U.rng(5));
  let up = 0, down = 0, max = 0, sumAbs = 0, sumStep = 0, steps = 0, moved = 0;
  for (let m = 0; m < w.n; m++) {
    const d = w.elev[m] - 1;
    if (d !== 0) moved++;
    if (g.angle(c, m) > ang + 1.3 * g.step) { T.ok(d === 0, 'cell outside the patch changed'); continue; }
    if (d > 0) up++; else if (d < 0) down++;
    if (Math.abs(d) > max) max = Math.abs(d);
    sumAbs += Math.abs(d);
    for (let k = 0; k < g.nbrN[m]; k++) { sumStep += Math.abs(w.elev[g.nbr[6 * m + k]] - w.elev[m]); steps++; }
  }
  T.ok(max <= amount * 1.0001 && max > 0.6 * amount, 'largest displacement ' + max.toFixed(3) + ' for amount ' + amount);
  T.ok(up > 30 && down > 30, 'both signs: ' + up + ' up, ' + down + ' down');
  T.ok(n > 0 && n === moved, 'returns the number of cells changed');
  // neighbours move together: the mean step between neighbours is below the mean displacement (white noise: 1.3 ×)
  T.ok(sumStep / steps < 0.8 * sumAbs / (up + down), 'smooth field (mean step ' + (sumStep / steps).toFixed(4) + ', mean |d| ' + (sumAbs / (up + down)).toFixed(4) + ')');
  T.eq(pits(w).size, 0, 'no one-cell pits on what was a plain');
  T.eq(w.dirty.elev && w.dirty.water && w.dirty.color && w.need.derive && w.need.climate && w.need.hydro, true, 'flags');
});

T.test('roughen: creates no one-cell pits on real terrain (80 quakes)', () => {
  const w = generated(5, 4), pick = U.rng(21);
  let total = 0;
  for (let i = 0; i < 80; i++) {
    const c = pick.int(0, w.n - 1), ang = pick.range(0.03, 0.3), amount = pick.range(0.1, 1.5) * (pick.chance(0.5) ? 1 : -1);
    const before = pits(w);
    total += Terrain.roughen(w, c, ang, amount, U.rng(1000 + i));
    for (const p of pits(w)) T.ok(before.has(p), 'new one-cell pit at cell ' + p + ' (quake ' + i + ')');
  }
  T.ok(total > 2000, 'the quakes moved ' + total + ' cells');
  T.ok(allFinite(w), 'finite, inside the clamps');
});

T.test('roughen: draws from the rng it is given (deterministic), or from world.rng when given none', () => {
  const a = generated(4, 6), b = generated(4, 6);
  const s0 = a.rng.s, ra = U.rng(42), rb = U.rng(42);
  Terrain.roughen(a, 100, 0.2, 0.5, ra);
  Terrain.roughen(b, 100, 0.2, 0.5, rb);
  T.ok(sameArray(a.elev, b.elev), 'same rng, same result');
  T.eq(ra.s, rb.s, 'same number of draws');
  T.ok(ra.s !== (42 >>> 0), 'the given rng advanced');
  T.eq(a.rng.s, s0, 'world.rng untouched when an rng is passed');
  const before = Float32Array.from(a.elev);
  Terrain.roughen(a, 100, 0.2, 0.5, U.rng(43));
  Terrain.roughen(b, 100, 0.2, 0.5, U.rng(44));
  T.ok(!sameArray(a.elev, b.elev) && !sameArray(a.elev, before), 'another stream gives another shake');
  T.ok(Terrain.roughen(a, 100, 0.2, 0.5) > 0, 'works without an rng');
  T.ok(a.rng.s !== s0, 'and then draws from world.rng');
});

// ------------------------------------------------------------------ derive

function bruteSlope(w) {
  const g = w.geo, out = new Float32Array(w.n);
  for (let c = 0; c < w.n; c++) {
    let s = 0;
    for (let k = 0; k < g.nbrN[c]; k++) s = Math.max(s, Math.abs(w.elev[g.nbr[6 * c + k]] - w.elev[c]));
    out[c] = s;
  }
  return out;
}
// hops to the nearest sea cell by repeated relaxation (no queue), capped at 255
function bruteCoast(w) {
  const g = w.geo, n = w.n, d = new Int32Array(n);
  for (let c = 0; c < n; c++) d[c] = w.elev[c] < w.seaLevel ? 0 : 255;
  for (let changed = true; changed;) {
    changed = false;
    for (let c = 0; c < n; c++) {
      for (let k = 0; k < g.nbrN[c]; k++) {
        const v = d[g.nbr[6 * c + k]] + 1;
        if (v < d[c]) { d[c] = v; changed = true; }
      }
    }
  }
  return d;
}
function checkDerive(w, tag) {
  w.need.derive = true; w.need.life = false;
  Terrain.derive(w);
  T.eq(w.need.derive, false, tag + ': clears need.derive');
  T.eq(w.need.life, true, tag + ': sets need.life');
  const n = w.n, slope = bruteSlope(w), coast = bruteCoast(w), comp = components(w, w.seaLevel), lid = w._landId;
  T.ok(lid instanceof Int32Array && lid.length === n, tag + ': _landId is an Int32Array(n)');
  for (let c = 0; c < n; c++) {
    if (w.slope[c] !== slope[c]) T.ok(false, tag + ': slope[' + c + '] = ' + w.slope[c] + ', brute force ' + slope[c]);
    if (w.coast[c] !== coast[c]) T.ok(false, tag + ': coast[' + c + '] = ' + w.coast[c] + ', brute force ' + coast[c]);
  }
  // labels: −1 exactly on sea cells; otherwise a bijection with the brute-force components
  const k = comp.sizes.length, toDerived = new Int32Array(k).fill(-1), seen = new Int32Array(k).fill(-1), count = new Int32Array(k), first = new Int32Array(k).fill(-1);
  for (let c = 0; c < n; c++) {
    const b = comp.lab[c], id = lid[c];
    if (b < 0) { if (id !== -1) T.ok(false, tag + ': sea cell ' + c + ' has label ' + id); continue; }
    if (!(id >= 0 && id < k)) T.ok(false, tag + ': land cell ' + c + ' has label ' + id + ' (' + k + ' landmasses)');
    if (toDerived[b] < 0) {
      if (seen[id] >= 0) T.ok(false, tag + ': label ' + id + ' spans two separate landmasses');
      toDerived[b] = id; seen[id] = b;
    } else if (toDerived[b] !== id) T.ok(false, tag + ': one landmass carries labels ' + toDerived[b] + ' and ' + id);
    count[id]++;
    if (first[id] < 0) first[id] = c;
  }
  for (let i = 0; i < k; i++) {
    T.eq(count[i], comp.sizes[seen[i]], tag + ': size of landmass ' + i);
    if (i > 0) {
      T.ok(count[i] <= count[i - 1], tag + ': ids by descending size (' + count[i - 1] + ' then ' + count[i] + ')');
      if (count[i] === count[i - 1]) T.ok(first[i] > first[i - 1], tag + ': equal sizes ordered by lowest cell id');
    }
  }
  T.ok(w._landSize && w._landSize.length === k, tag + ': _landSize has one entry per landmass');
  for (let i = 0; i < k; i++) T.eq(w._landSize[i], count[i], tag + ': _landSize[' + i + ']');
  return k;
}

T.test('derive: slope, coast and landmass labels match brute force (generated worlds, three sea levels)', () => {
  for (const [L, seed] of [[4, 1], [4, 2], [5, 3], [5, 7]]) {
    const w = generated(L, seed);
    for (const sea of [0, -2.5, 1.2]) {
      w.seaLevel = sea;
      const k = checkDerive(w, 'L' + L + ' seed ' + seed + ' sea ' + sea);
      T.ok(k >= 2, 'several landmasses (' + k + ')');
    }
  }
});

T.test('derive: at L = 6 the largest landmass is id 0 and distances inland are plausible', () => {
  const w = generated(6, 1);
  const k = checkDerive(w, 'L6 seed 1');
  T.ok(k >= 5, k + ' landmasses');
  let far = 0;
  for (let c = 0; c < w.n; c++) if (w.coast[c] > far) far = w.coast[c];
  T.ok(far >= 8 && far < 120, 'deepest interior is ' + far + ' hops from the sea');
});

T.test('derive: worlds with no sea and with no land', () => {
  const w = generated(4, 9);
  w.seaLevel = -20;                                                // dry planet
  Terrain.derive(w);
  for (let c = 0; c < w.n; c++) { if (w.coast[c] !== 255) T.ok(false, 'coast is 255 on a planet with no sea'); if (w._landId[c] !== 0) T.ok(false, 'one landmass'); }
  T.eq(Array.from(w._landSize), [w.n], 'one landmass holding every cell');
  T.ok(sameArray(w.slope, bruteSlope(w)), 'slope still computed');
  w.seaLevel = 25;                                                 // drowned planet
  Terrain.derive(w);
  for (let c = 0; c < w.n; c++) { if (w.coast[c] !== 0) T.ok(false, 'coast is 0 on sea cells'); if (w._landId[c] !== -1) T.ok(false, 'no labels'); }
  T.eq(w._landSize.length, 0, 'no landmasses');
  T.eq(w.need.derive, false);
});

T.test('derive: follows sculpting (a new island appears, a strait is bridged) and world size changes', () => {
  const w = flat(4, -1);                                           // all sea
  Terrain.derive(w);
  T.eq(w._landSize.length, 0);
  Terrain.sculpt(w, 100, 0.12, 2);
  T.eq(w.need.derive, true, 'sculpt asks for a derive');
  checkDerive(w, 'one island');
  T.eq(w._landSize.length, 1, 'one island');
  T.eq(w._landId[100], 0);
  T.eq(w.coast[100] > 0, true, 'island centre is inland');
  // a second, larger island far away, then check the ordering flips
  let other = -1;
  for (let c = 0; c < w.n; c++) if (w.geo.angle(c, 100) > 1.5) { other = c; break; }
  Terrain.sculpt(w, other, 0.3, 2);
  T.eq(checkDerive(w, 'two islands'), 2);
  T.eq(w._landId[other], 0, 'the larger island is landmass 0');
  T.eq(w._landId[100], 1, 'the smaller one is landmass 1');
  const big = fresh(5, 2);                                         // scratch buffers follow world.n
  Terrain.generate(big); big.seaLevel = 0;
  checkDerive(big, 'L5 after L4');
  checkDerive(w, 'L4 after L5');
});

// ------------------------------------------------------------------ everything together

T.test('a storm of random operations leaves a finite, clamped, derivable world', () => {
  const w = generated(4, 11), rng = U.rng(77);
  for (let i = 0; i < 400; i++) {
    const c = rng.int(0, w.n - 1), ang = rng.chance(0.1) ? rng.range(0, 3.5) : rng.range(0, 0.3), k = rng.int(0, 3);
    const big = rng.chance(0.05) ? 40 : 1;
    if (k === 0) Terrain.sculpt(w, c, ang, rng.range(-3, 3) * big);
    else if (k === 1) Terrain.crater(w, c, ang, rng.range(0.1, 3) * big);
    else if (k === 2) Terrain.cone(w, c, ang, rng.range(0.1, 3) * big);
    else Terrain.roughen(w, c, ang, rng.range(-2, 2) * big, rng);
  }
  for (let c = 0; c < w.n; c++) T.ok(w.elev[c] >= State.ELEV_MIN && w.elev[c] <= State.ELEV_MAX, 'elev[' + c + '] = ' + w.elev[c]);
  checkDerive(w, 'after the storm');
  for (let c = 0; c < w.n; c++) T.ok(Number.isFinite(w.slope[c]), 'slope finite');
});
