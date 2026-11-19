// DEMIURGE — tests/helpers/world.js : synthetic worlds for module tests.
//
// Sim modules are built in parallel, so a module's own tests must not depend on other sim modules
// existing or being correct. This helper builds a plausible planet using only the foundation
// (core + state.js), and can fill in simple stand-ins for every derived field.
//
//   const H = require('./helpers/world.js');
//   const w = H.makeWorld({ L: 4, seed: 3 });          // terrain + sea level 0 + basics (temp, moist, biome, veg, hab, slope, coast, fresh)
//   const w = H.makeWorld({ L: 5, basics: false });    // terrain only, everything else at defaults
//   H.fillBasics(w);                                    // (re)compute the simple stand-ins
//   H.findLand(w, c => w.hab[c] > 0.5);                 // first cell matching, or -1
//   H.checkFinite(w) → null or 'field[c]' of the first NaN/Infinity in any cell array
//   H.stub('Life', { ignite: () => true });             // install a stub module/function for the duration of a test file
'use strict';
const DM = require('../../tools/load.js')();

function makeWorld(opts) {
  opts = opts || {};
  const L = opts.L === undefined ? 4 : opts.L, seed = opts.seed === undefined ? 1 : opts.seed;
  const land = opts.land === undefined ? 0.4 : opts.land;
  const w = DM.State.create({ seed, name: 'Testworld', L });
  const g = w.geo, U = DM.U, off = (seed % 97) * 3.17 + 0.5, n = w.n;
  const raw = new Float32Array(n);
  for (let c = 0; c < n; c++) {
    const x = g.pos[3 * c], y = g.pos[3 * c + 1], z = g.pos[3 * c + 2];
    raw[c] = U.fbm3(x * 1.5 + off, y * 1.5 - off, z * 1.5 + off * 0.5, 5) + 0.3 * U.ridged3(x * 3.1 + off, y * 3.1, z * 3.1 - off, 3) - 0.15;
  }
  const sorted = Float32Array.from(raw).sort();
  const shore = sorted[Math.min(n - 1, Math.floor(n * (1 - land)))];
  for (let c = 0; c < n; c++) {
    const d = raw[c] - shore;
    w.elev[c] = d >= 0 ? Math.min(8, 0.02 + d * 9) : Math.max(-6.5, d * 12);
  }
  w.seaLevel = opts.sea === undefined ? 0 : opts.sea;
  w.heat = 0;
  w.phase = opts.phase || 'history';
  w.vars.seaBase = w.seaLevel;
  if (opts.basics !== false) fillBasics(w);
  return w;
}

// Simple stand-ins for the derived fields (NOT the real climate/hydro/life models).
function fillBasics(w) {
  const g = w.geo, U = DM.U, n = w.n, B = DM.BIOME, sea = w.seaLevel;
  // slope
  for (let c = 0; c < n; c++) {
    let s = 0;
    for (let k = 0; k < g.nbrN[c]; k++) { const d = Math.abs(w.elev[g.nbr[6 * c + k]] - w.elev[c]); if (d > s) s = d; }
    w.slope[c] = s;
  }
  // coast: multi-source BFS from sea cells
  const q = new Int32Array(n); let head = 0, tail = 0;
  for (let c = 0; c < n; c++) { if (w.elev[c] < sea) { w.coast[c] = 0; q[tail++] = c; } else w.coast[c] = 255; }
  while (head < tail) {
    const c = q[head++], d = w.coast[c];
    if (d >= 254) continue;
    for (let k = 0; k < g.nbrN[c]; k++) { const b = g.nbr[6 * c + k]; if (w.coast[b] > d + 1) { w.coast[b] = d + 1; q[tail++] = b; } }
  }
  for (let c = 0; c < n; c++) {
    const x = g.pos[3 * c], y = g.pos[3 * c + 1], z = g.pos[3 * c + 2];
    const isSea = w.elev[c] < sea, h = Math.max(0, w.elev[c] - sea);
    let t = 29 - 53 * Math.pow(Math.abs(y), 1.5) - 5.5 * h + w.globalTemp;
    if (isSea && t < -2) t = -2;
    w.temp[c] = t;
    const m = U.sat(0.55 + 0.45 * U.fbm3(x * 2.3 + 11, y * 2.3 + 5, z * 2.3 - 7, 3) - 0.004 * Math.min(60, w.coast[c]) * 3);
    w.moist[c] = isSea ? 1 : m;
    w.fresh[c] = (!isSea && m > 0.62) ? 1 : (isSea ? 255 : 4);
    w.down[c] = -1; w.flow[c] = 0;
    let b;
    if (isSea) b = (sea - w.elev[c] > 0.6) ? B.SEA : B.SHALLOWS;
    else if (t < -9) b = B.ICE;
    else if (h > 4.2 && t < -2) b = B.SNOWPEAK;
    else if (w.slope[c] > 1.1 || h > 4.6) b = B.ROCK;
    else if (h < 0.12 && w.coast[c] <= 1) b = B.BEACH;
    else if (t < -6) b = B.TUNDRA;
    else if (t < 3) b = m > 0.3 ? B.TAIGA : B.TUNDRA;
    else if (m < 0.14) b = B.DESERT;
    else if (m < 0.3) b = t > 18 ? B.SAVANNA : B.GRASSLAND;
    else if (m < 0.6) b = B.FOREST;
    else b = t > 20 ? B.RAINFOREST : B.FOREST;
    w.biome[c] = b;
    w.ice[c] = (b === B.ICE) ? 1 : 0;
    const cap = [0, 0, 0.05, 0.05, 0.35, 0.3, 0.9, 1, 0.75, 0.15, 0, 0.03, 0, 0.7, 0, 0][b];
    w.veg[c] = cap * 0.9;
    w.fauna[c] = isSea ? (b === B.SHALLOWS ? 0.8 : 0.3) : cap * 0.6;
    w.fert[c] = 0.5;
    const tempSuit = U.sat(1 - Math.abs(t - 17) / 22);
    const habitable = !isSea && b !== B.ICE && b !== B.ROCK && b !== B.SNOWPEAK && b !== B.BARREN;
    w.hab[c] = habitable ? U.sat(tempSuit * (0.25 + 0.75 * U.sat(m * 1.6)) * (1 - U.sat(w.slope[c] / 1.5)) * (w.coast[c] <= 2 ? 1.1 : 1)) : 0;
  }
  w.need.derive = false; w.need.climate = false; w.need.hydro = false;
  return w;
}

function findLand(w, pred) {
  for (let c = 0; c < w.n; c++) if (w.elev[c] >= w.seaLevel && (!pred || pred(c))) return c;
  return -1;
}
// best habitable cell (handy as a first settlement site)
function bestHab(w) {
  let best = -1, bv = -1;
  for (let c = 0; c < w.n; c++) if (w.hab[c] > bv) { bv = w.hab[c]; best = c; }
  return best;
}
function checkFinite(w) {
  for (const f of DM.State.CELL_FIELDS) {
    if (f.type !== 'f32') continue;
    const a = w[f.name];
    for (let c = 0; c < a.length; c++) if (!Number.isFinite(a[c])) return f.name + '[' + c + ']';
  }
  for (const k of ['year', 'seaLevel', 'globalTemp', 'co2', 'dust', 'heat']) if (!Number.isFinite(w[k])) return k;
  if (!Number.isFinite(w.god.awe) || !Number.isFinite(w.god.aweMax)) return 'god.awe';
  return null;
}
// Install a stub for a module (or individual functions) that may not exist yet. Existing real
// functions are kept unless `force` is true. Returns a restore function.
function stub(moduleName, fns, force) {
  const had = Object.prototype.hasOwnProperty.call(DM, moduleName);
  const mod = DM[moduleName] = DM[moduleName] || {};
  const saved = {};
  for (const k in fns) { saved[k] = mod[k]; if (force || typeof mod[k] !== 'function') mod[k] = fns[k]; }
  return function restore() { for (const k in saved) { if (saved[k] === undefined) delete mod[k]; else mod[k] = saved[k]; } if (!had && !Object.keys(mod).length) delete DM[moduleName]; };
}
// collect Bus events during fn()
function capture(fn) {
  const events = [];
  const off = DM.Bus.on('*', (payload, type) => { events.push({ type, payload }); });
  try { fn(); } finally { off(); }
  return events;
}

// Order-sensitive fingerprint of everything that defines a world's state (cell arrays, scalars, entities, rng).
// Two runs of the same seed with the same inputs must produce the same hash. Transient `_` fields are ignored.
function hash(w) {
  let h = 2166136261 >>> 0;
  const mixInt = v => { h ^= v & 0xffff; h = Math.imul(h, 16777619) >>> 0; h ^= (v >>> 16) & 0xffff; h = Math.imul(h, 16777619) >>> 0; };
  const f32 = new Float32Array(1), u32 = new Uint32Array(f32.buffer);
  for (const f of DM.State.CELL_FIELDS) {
    if (!f.save) continue;                       // derived fields are not part of the identity
    const a = w[f.name];
    if (f.type === 'f32') { for (let c = 0; c < a.length; c++) { f32[0] = a[c]; mixInt(u32[0]); } }
    else for (let c = 0; c < a.length; c++) mixInt(a[c] | 0);
  }
  const strip = (k, v) => (typeof k === 'string' && k[0] === '_') ? undefined : v;
  const s = JSON.stringify({ year: w.year, tick: w.tick, phase: w.phase, heat: w.heat, sea: w.seaLevel, gt: w.globalTemp, co2: w.co2, dust: w.dust, age: w.age,
    god: w.god, settlements: w.settlements, cultures: w.cultures, units: w.units, disasters: w.disasters, chronicle: w.chronicle.length, vars: w.vars, ending: w.ending, rng: w.rng.s }, strip);
  for (let i = 0; i < s.length; i++) { h ^= s.charCodeAt(i); h = Math.imul(h, 16777619) >>> 0; }
  return h >>> 0;
}

module.exports = { DM, makeWorld, fillBasics, findLand, bestHab, checkFinite, stub, capture, hash };
