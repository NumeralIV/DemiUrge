// tests/state.test.js — DM.State (FOUNDATION §5): the field table, world defaults, isSea and
// friends, and save / load (quantization, entities, rng, rejection of bad saves).
'use strict';
const assert = require('assert');
const T = require('../tools/t.js');
const DM = require('../tools/load.js')({ only: [] });        // core + state.js + schema.js
const S = DM.State;

// ------------------------------------------------------------------ the contract, restated

// FOUNDATION §5.1: name, type, default, save, min, max — in the order listed there.
const TABLE = [
  ['elev', 'f32', 0, 'q16', -12, 20], ['water', 'f32', 0, 'q16', 0, 8], ['flow', 'f32', 0, null],
  ['down', 'i32', -1, null], ['temp', 'f32', 0, null], ['moist', 'f32', 0, null],
  ['wet', 'f32', 0, 'q8', 0, 1], ['veg', 'f32', 0, 'q8', 0, 1], ['biome', 'u8', 0, null],
  ['fert', 'f32', 0.5, 'q8', 0, 1.5], ['ice', 'f32', 0, 'q8', 0, 1], ['fire', 'f32', 0, 'q8', 0, 1],
  ['scorch', 'f32', 0, 'q8', 0, 1], ['lava', 'f32', 0, 'q16', 0, 8], ['fauna', 'f32', 0, 'q8', 0, 1],
  ['rad', 'f32', 0, 'q8', 0, 1], ['owner', 'i16', -1, 'raw'], ['farm', 'f32', 0, 'q8', 0, 1],
  ['urban', 'f32', 0, 'q8', 0, 1], ['road', 'u8', 0, 'raw'], ['hab', 'f32', 0, null],
  ['slope', 'f32', 0, null], ['coast', 'u8', 255, null], ['fresh', 'u8', 255, null],
];
const CTOR = { f32: Float32Array, u8: Uint8Array, i16: Int16Array, i32: Int32Array };
const SCALAR_DEFAULTS = { year: 0, tick: 0, phase: 'molten', heat: 1, seaLevel: -9, globalTemp: 0, co2: 0, dust: 0, age: 0 };
const BAG_DEFAULTS = {
  god: { awe: 0, aweMax: 100, mercy: 0, wrath: 0, epithet: '', deeds: {} },
  settlements: [], cultures: [], units: [], disasters: [], rivers: [], chronicle: [], snapshots: [],
  nextId: { unit: 1, disaster: 1, entry: 1 },
  stats: { pop: 0, peakPop: 0, believers: 0, wars: 0, miracles: 0 },
  settings: { calamity: 1 },
  vars: {},
  ending: null,
  dirty: { elev: true, color: true, water: true, objects: true, rivers: true, roads: true },
  need: { derive: true, climate: true, hydro: true, life: true },
};
const PERSISTED_KEYS = ['year', 'tick', 'phase', 'heat', 'seaLevel', 'globalTemp', 'co2', 'dust', 'age', 'god', 'settlements',
  'cultures', 'units', 'disasters', 'chronicle', 'nextId', 'stats', 'settings', 'vars', 'ending'];

// ------------------------------------------------------------------ helpers

const field = name => S.CELL_FIELDS.find(f => f.name === name);
const quantized = f => f.save === 'q8' || f.save === 'q16';
const top = f => (f.save === 'q8' ? 255 : 65535);
const stepOf = f => (f.max - f.min) / top(f);
const bytesOf = f => (f.save === 'q8' ? 1 : f.save === 'q16' ? 2 : CTOR[f.type].BYTES_PER_ELEMENT);
const viaJson = w => S.deserialize(JSON.parse(JSON.stringify(S.serialize(w))));
const rawBytes = (save, name) => Buffer.from(save.cells[name], 'base64');    // independent of DM.U.b64dec

function firstDiff(a, b) {
  if (a.length !== b.length) return 0;
  for (let i = 0; i < a.length; i++) if (!Object.is(a[i], b[i])) return i;
  return -1;
}
function allEqual(a, v) { for (let i = 0; i < a.length; i++) if (a[i] !== v) return false; return true; }

// every persisted field gets random in-range values (including both ends of each range); every
// derived field gets junk, which a load must replace by the default
function fillRandom(w, r) {
  for (const f of S.CELL_FIELDS) {
    const a = w[f.name], n = a.length;
    if (quantized(f)) {
      for (let c = 0; c < n; c++) a[c] = r.range(f.min, f.max);
      a[0] = f.min; a[1] = f.max;
    } else if (f.type === 'f32') {
      for (let c = 0; c < n; c++) a[c] = r.range(-50, 50);
    } else if (f.type === 'u8') {
      for (let c = 0; c < n; c++) a[c] = r.int(0, 255);
    } else if (f.type === 'i16') {
      for (let c = 0; c < n; c++) a[c] = r.int(-32768, 32767);
      a[0] = -32768; a[1] = 32767; a[2] = -1;
    } else {
      for (let c = 0; c < n; c++) a[c] = r.int(-1, n - 1);
    }
  }
}

// plain entity data as the sim would hold it (literals, so this file does not depend on schema.js)
function populate(w) {
  w.year = 1234.5625; w.tick = 4938; w.phase = 'history'; w.heat = 0.37; w.seaLevel = -0.123456789;
  w.globalTemp = 1.75; w.co2 = 0.4; w.dust = 0.0625; w.age = 4;
  w.god.awe = 77.5; w.god.aweMax = 240; w.god.mercy = 12; w.god.wrath = 3; w.god.epithet = 'the Rain-Giver';
  w.god.deeds.rain = 9; w.god.deeds.meteor = 1;
  w.cultures.push({
    id: 0, name: 'Ashári', adj: 'Ashárian', color: [0.787, 0.342, 0], lang: { onsets: ['k', 'sh', 'th'], rules: { v: ['a', 'é'], deep: [[1, 2], [3, { x: null }]] } },
    parent: -1, born: 0, dead: false, traits: { piety: 0.5, aggression: 0.25 }, prophet: null, seen: { rain: 3 }, edictSince: -9999,
  });
  w.cultures.push({ id: 1, name: 'Tō-Vel', parent: 0, born: 310.25, dead: true, traits: {}, prophet: { name: 'Qel', born: 300 }, seen: {} });
  w.settlements.push({ id: 0, cell: 5, name: 'Qel’ân', culture: 0, founded: 12.25, dead: false, deadYear: null, pop: 1234.5, tier: 2, b: { shrine: 1, temple: 0 }, prayer: { kind: 'rain', start: 1200 }, capital: true });
  w.settlements.push({ id: 1, cell: 9, name: 'Old Harbour', culture: 1, founded: 320, dead: true, deadYear: 400.5, cause: 'flood', pop: 0, tier: 0, b: {}, prayer: null, capital: false });
  w.units.push({ id: 7, type: 'army', culture: 0, from: 0, to: 1, dest: 9, path: [5, 6, 7, 9], i: 1, t: 0.5, speed: 5, size: 120, naval: false, sea: false, born: 1230, data: { target: 1, waypoints: [] } });
  w.disasters.push({ id: 3, type: 'storm', cell: 11, t: 2.5, dur: 8, power: 0.75, ang: 0.2, natural: true, data: { front: [11, 12, 13], seeds: [[1, 0.5], [2, 0.25]] } });
  w.chronicle.push({ id: 1, year: 12.25, type: 'founding', text: 'In the first year the Ashári raised Qel’ân beside the water. “It is enough,” they said.', cell: 5, culture: 0, weight: 3 });
  w.chronicle.push({ id: 2, year: 400.5, type: 'flood', text: 'The sea took Old Harbour — 天の水 🌊', cell: 9, culture: 1, weight: 2 });
  w.nextId.unit = 8; w.nextId.disaster = 4; w.nextId.entry = 3;
  w.stats.pop = 1234; w.stats.peakPop = 5000; w.stats.believers = 900; w.stats.wars = 2; w.stats.miracles = 10;
  w.settings.calamity = 0.5;
  w.vars.seaBase = -0.25; w.vars.lifeBoost = 1; w.vars.rel = { '0|1': { war: true, since: 310.25, trade: false } }; w.vars.flag = false; w.vars.note = '';
  w.ending = { type: 'ark', mood: 'beloved', culture: 0, settlement: 0, year: 1999.5 };
  return w;
}

function isBadSave(v, label) {
  let err = null;
  try { S.deserialize(v); } catch (e) { err = e; }
  if (!(err instanceof Error) || err.message !== 'bad save') {
    T.ok(false, label + ': expected Error("bad save"), got ' + (err ? 'Error("' + err.message + '")' : 'a world'));
  }
}

// ------------------------------------------------------------------ table and enums

T.test('CELL_FIELDS is the contract table: order, types, defaults, encodings, ranges', () => {
  T.ok(Array.isArray(S.CELL_FIELDS), 'is an array');
  T.eq(S.CELL_FIELDS.map(f => f.name).join(' '), TABLE.map(r => r[0]).join(' '), 'names and order');
  TABLE.forEach((row, i) => {
    const f = S.CELL_FIELDS[i];
    T.eq(Object.keys(f).sort().join(), 'def,max,min,name,save,type', row[0] + ' keys');
    T.ok(f.type === row[1], row[0] + ' type ' + f.type);
    T.ok(f.def === row[2], row[0] + ' default ' + f.def);
    T.ok(f.save === row[3], row[0] + ' save ' + f.save);
    if (quantized(f)) {
      T.ok(f.min === row[4] && f.max === row[5], row[0] + ' range ' + f.min + '…' + f.max);
      T.ok(f.type === 'f32' && f.def >= f.min && f.def <= f.max, row[0] + ' default lies in its range');
    } else {
      T.ok(f.min === null && f.max === null, row[0] + ' has no range');
    }
  });
  T.eq(new Set(S.CELL_FIELDS.map(f => f.name)).size, TABLE.length, 'names are unique');
  let perCell = 0;
  for (const f of S.CELL_FIELDS) if (f.save) perCell += bytesOf(f);
  T.eq(perCell, 19, 'persisted bytes per cell');
  T.eq(S.ELEV_MIN, -12); T.eq(S.ELEV_MAX, 20);
  T.ok(field('elev').min === S.ELEV_MIN && field('elev').max === S.ELEV_MAX, 'elev range is ELEV_MIN…ELEV_MAX');
});

T.test('BIOME, BIOME_NAMES and AGES match the contract', () => {
  assert.deepStrictEqual(DM.BIOME, { SEA: 0, SHALLOWS: 1, BEACH: 2, DESERT: 3, SAVANNA: 4, GRASSLAND: 5, FOREST: 6, RAINFOREST: 7,
    TAIGA: 8, TUNDRA: 9, ICE: 10, ROCK: 11, SNOWPEAK: 12, WETLAND: 13, BARREN: 14, SEAICE: 15 });
  assert.deepStrictEqual(DM.BIOME_NAMES, ['Sea', 'Shallows', 'Beach', 'Desert', 'Savanna', 'Grassland', 'Forest', 'Rainforest',
    'Taiga', 'Tundra', 'Ice', 'Rock', 'Snow peak', 'Wetland', 'Barren', 'Sea ice']);
  assert.deepStrictEqual(DM.AGES, [{ id: 0, name: 'Stone' }, { id: 1, name: 'Bronze' }, { id: 2, name: 'Iron' }, { id: 3, name: 'Sail' },
    { id: 4, name: 'Industry' }, { id: 5, name: 'Atomic' }, { id: 6, name: 'Space' }]);
  T.eq(Object.keys(DM.BIOME).length, DM.BIOME_NAMES.length, 'one name per biome');
  T.eq(Object.values(DM.BIOME).join(), DM.BIOME_NAMES.map((_, i) => i).join(), 'biome ids are 0…15 in order');
  DM.AGES.forEach((a, i) => T.eq(a.id, i, 'age id = index'));
});

// ------------------------------------------------------------------ create

T.test('create: every cell field is a typed array of n cells filled with its default', () => {
  for (const L of [0, 3]) {
    const w = S.create({ seed: 9, L });
    T.eq(w.n, 10 * Math.pow(4, L) + 2, 'n at L = ' + L);
    for (const f of S.CELL_FIELDS) {
      const a = w[f.name];
      T.ok(a instanceof CTOR[f.type], f.name + ' is a ' + CTOR[f.type].name);
      T.eq(a.length, w.n, f.name + ' length');
      T.ok(allEqual(a, f.def), f.name + ' filled with ' + f.def);
    }
  }
});

T.test('create: scalars, bags and identity are the contract defaults', () => {
  const w = S.create({ seed: 42, name: 'Tellus', L: 2 });
  T.eq(w.version, 1); T.eq(w.seed, 42); T.eq(w.name, 'Tellus'); T.eq(w.L, 2); T.eq(w.n, 162);
  T.ok(w.geo === DM.Geo.get(2), 'geo comes from DM.Geo.get(L)');
  T.ok(w.rng instanceof DM.U.Rng && w.rng.s === 42, 'rng is DM.U.rng(seed)');
  for (const k in SCALAR_DEFAULTS) T.ok(w[k] === SCALAR_DEFAULTS[k], k + ' = ' + w[k]);
  for (const k in BAG_DEFAULTS) assert.deepStrictEqual(w[k], BAG_DEFAULTS[k], k);
  const expected = ['version', 'seed', 'name', 'L', 'n', 'geo', 'rng'].concat(TABLE.map(r => r[0]), Object.keys(SCALAR_DEFAULTS), Object.keys(BAG_DEFAULTS));
  T.eq(Object.keys(w).sort().join(' '), expected.sort().join(' '), 'the world has exactly the documented properties');
});

T.test('create: option defaults and coercions', () => {
  const w = S.create({ seed: 1 });
  T.eq(w.L, 6, 'L defaults to 6'); T.eq(w.n, 40962); T.eq(w.name, 'Unnamed');
  T.ok(w.geo === DM.Geo.get(6));
  T.eq(S.create({ L: 1 }).seed, 0, 'seed defaults to 0');
  T.eq(S.create({ seed: 0, L: 1 }).rng.s, 0, 'seed 0 is valid');
  T.eq(S.create({ seed: -1, L: 1 }).seed, 4294967295, 'seed is coerced with >>> 0');
  T.eq(S.create({ seed: 4294967296 + 5, L: 1 }).seed, 5);
  T.eq(S.create({ seed: 3.9, L: 1 }).seed, 3);
  T.eq(S.create({ seed: 1, L: 1, name: '' }).name, '', 'an empty name is kept');
  T.eq(S.create({ seed: 1, L: 1, name: null }).name, 'Unnamed');
  T.ok(S.create({ seed: 1, L: 1, name: 77 }).name === '77', 'name is a string');
  T.eq(S.create({ seed: 1, L: 0 }).n, 12, 'L = 0 is honoured, not replaced by the default');
  T.throws(() => S.create({ seed: 1, L: 8 }), 'L outside 0…7');
});

T.test('create: the same seed gives identical worlds, and worlds share nothing', () => {
  const a = S.create({ seed: 77, name: 'A', L: 3 }), b = S.create({ seed: 77, name: 'A', L: 3 });
  T.eq(S.serialize(a), S.serialize(b), 'identical saves');
  for (let i = 0; i < 50; i++) T.ok(a.rng.next() === b.rng.next(), 'identical random streams');
  T.ok(S.create({ seed: 78, L: 3 }).rng.next() !== S.create({ seed: 77, L: 3 }).rng.next(), 'another seed, another stream');
  T.ok(a.geo === b.geo, 'geo is shared (built once)');
  T.ok(a.rng !== b.rng, 'rng objects are separate');
  for (const f of S.CELL_FIELDS) {
    T.ok(a[f.name] !== b[f.name] && a[f.name].buffer !== b[f.name].buffer, f.name + ' is not shared');
    T.ok(a[f.name].byteOffset === 0 && a[f.name].buffer.byteLength === a[f.name].byteLength, f.name + ' owns its whole buffer');
  }
  const bufs = new Set(S.CELL_FIELDS.map(f => a[f.name].buffer));
  T.eq(bufs.size, S.CELL_FIELDS.length, 'fields of one world do not share buffers either');
  for (const k in BAG_DEFAULTS) if (BAG_DEFAULTS[k] !== null) T.ok(a[k] !== b[k], k + ' is not shared');
  T.ok(a.god.deeds !== b.god.deeds, 'god.deeds is not shared');
  // writing to one world leaves the other at its defaults
  for (const f of S.CELL_FIELDS) a[f.name].fill(f.type === 'f32' ? 0.75 : 3);
  a.god.deeds.rain = 1; a.settlements.push({ id: 0 }); a.vars.x = 1; a.dirty.elev = false; a.need.life = false; a.stats.pop = 9; a.nextId.unit = 5;
  for (const f of S.CELL_FIELDS) T.ok(allEqual(b[f.name], f.def), f.name + ' of the other world is untouched');
  for (const k in BAG_DEFAULTS) assert.deepStrictEqual(b[k], BAG_DEFAULTS[k], k);
});

T.test('create: a geo passed in is reused', () => {
  const g = DM.Geo.build(3);
  T.ok(g !== DM.Geo.get(3), 'a private build');
  const w = S.create({ seed: 5, geo: g });
  T.ok(w.geo === g, 'same instance'); T.eq(w.L, 3); T.eq(w.n, g.n); T.eq(w.elev.length, g.n);
  const w2 = S.create({ seed: 5, L: 5, geo: g });
  T.ok(w2.geo === g && w2.L === 3 && w2.n === g.n && w2.veg.length === g.n, 'the geo decides L and n');
});

// ------------------------------------------------------------------ isSea, groundR, surfaceR

T.test('isSea: a cell is sea iff elev < seaLevel', () => {
  const w = S.create({ seed: 1, L: 1 });
  T.ok(S.isSea(w, 0) === false, 'a new world (elev 0, seaLevel -9) is all land');
  w.elev[0] = -9; w.elev[1] = Math.fround(-9.001); w.elev[2] = -12; w.elev[3] = 5;
  T.ok(S.isSea(w, 0) === false, 'elev equal to seaLevel is land');
  T.ok(S.isSea(w, 1) === true); T.ok(S.isSea(w, 2) === true); T.ok(S.isSea(w, 3) === false);
  w.seaLevel = 0;
  w.elev[4] = 0; w.elev[5] = -1e-6; w.elev[6] = 1e-6;
  T.ok(S.isSea(w, 4) === false && S.isSea(w, 5) === true && S.isSea(w, 6) === false, 'around sea level 0');
  w.seaLevel = 5.5;
  T.ok(S.isSea(w, 3) === true, 'raising the sea drowns the cell');
  w.water[3] = 2;
  T.ok(S.isSea(w, 3) === true, 'lake water does not matter');
  for (let c = 0; c < w.n; c++) T.ok(S.isSea(w, c) === (w.elev[c] < w.seaLevel), 'cell ' + c);
});

T.test('groundR and surfaceR', () => {
  const w = S.create({ seed: 1, L: 1 });
  w.seaLevel = 0;
  w.elev[0] = 3; w.water[0] = 0;
  w.elev[1] = 2; w.water[1] = 0.5;
  w.elev[2] = -4; w.water[2] = 0.5;         // sea: lake depth is ignored
  w.elev[3] = 0;
  T.eq(DM.R, 100);
  T.near(S.groundR(w, 0), 103, 1e-9); T.near(S.groundR(w, 2), 96, 1e-9);
  T.near(S.surfaceR(w, 0), 103, 1e-9, 'dry land');
  T.near(S.surfaceR(w, 1), 102.5, 1e-9, 'lake surface');
  T.near(S.surfaceR(w, 2), 100, 1e-9, 'sea surface');
  T.near(S.surfaceR(w, 3), 100, 1e-9, 'land exactly at sea level');
  w.seaLevel = -1.25;
  T.near(S.surfaceR(w, 2), 98.75, 1e-9, 'sea surface follows seaLevel');
  for (let c = 0; c < w.n; c++) T.ok(S.surfaceR(w, c) >= S.groundR(w, c), 'the surface is never below the ground');
});

// ------------------------------------------------------------------ save shape

T.test('serialize: a plain object with a format marker, version and only persisted data', () => {
  const w = populate(S.create({ seed: 31, name: 'Shape', L: 2 }));
  w.rivers.push({ cells: new Int32Array([1, 2]), flow: new Float32Array([1, 2]) });
  w.snapshots.push({ year: 1, data: new Uint8Array(8) });
  const save = S.serialize(w);
  T.ok(save.format === 'demiurge-world' && save.format === S.FORMAT, 'format marker');
  T.ok(save.version === 1 && save.version === S.VERSION, 'version');
  T.eq(Object.keys(save).sort().join(' '), ['format', 'version', 'seed', 'name', 'L', 'n', 'rng', 'cells'].concat(PERSISTED_KEYS).sort().join(' '), 'top-level keys');
  T.eq(Object.keys(save.cells).join(' '), S.CELL_FIELDS.filter(f => f.save).map(f => f.name).join(' '), 'cells holds exactly the persisted fields');
  for (const f of S.CELL_FIELDS) {
    if (!f.save) continue;
    T.ok(typeof save.cells[f.name] === 'string', f.name + ' is a string');
    T.eq(save.cells[f.name].length, 4 * Math.ceil(w.n * bytesOf(f) / 3), f.name + ' base64 length');
    T.eq(rawBytes(save, f.name).length, w.n * bytesOf(f), f.name + ' byte length');
    T.ok(rawBytes(save, f.name).toString('base64') === save.cells[f.name], f.name + ' is standard padded base64');
  }
  T.eq(save.seed, 31); T.eq(save.name, 'Shape'); T.eq(save.L, 2); T.eq(save.n, 162); T.eq(save.rng, w.rng.s);
  // nothing but plain JSON values anywhere
  (function walk(v, path) {
    if (v === null || typeof v === 'string' || typeof v === 'boolean') return;
    if (typeof v === 'number') { T.ok(Number.isFinite(v), path + ' is finite'); return; }
    T.ok(typeof v === 'object', path + ' is a ' + typeof v);
    if (Array.isArray(v)) { v.forEach((x, i) => walk(x, path + '[' + i + ']')); return; }
    T.ok(Object.getPrototypeOf(v) === Object.prototype, path + ' is a plain object');
    for (const k of Object.keys(v)) walk(v[k], path + '.' + k);
  })(save, 'save');
  assert.deepStrictEqual(JSON.parse(JSON.stringify(save)), save, 'survives JSON unchanged');
});

T.test('serialize: known values give known bytes (little-endian, standard base64)', () => {
  const w = S.create({ seed: 1, L: 0 });
  const eStep = 32 / 65535;
  w.elev.set([-12, 20, 0, -12 + eStep, -12 + 256 * eStep, 4]);
  w.water.set([0, 8, 4, 8 / 65535]);
  w.fert.set([0, 1.5, 0.5, 0.75]);
  w.wet.set([0, 1, 0.5, 0.2, 1 / 255, 254 / 255]);
  w.owner.set([-1, 0, 1, 0x1234, -2, 32767, -32768]);
  w.road.set([0, 1, 2, 3, 255]);
  const save = S.serialize(w);
  const u16 = name => { const b = rawBytes(save, name), out = []; for (let i = 0; i < b.length; i += 2) out.push(b[i] | (b[i + 1] << 8)); return out; };
  T.eq(u16('elev').slice(0, 6), [0, 65535, 24576, 1, 256, 32768], 'elev codes');
  T.eq(Array.from(rawBytes(save, 'elev').subarray(6, 10)), [1, 0, 0, 1], 'low byte first');
  T.eq(u16('water').slice(0, 5), [0, 65535, 32768, 1, 0], 'water codes');
  T.eq(Array.from(rawBytes(save, 'fert')).slice(0, 5), [0, 255, 85, 128, 85], 'fert codes (the default 0.5 is code 85)');
  T.eq(Array.from(rawBytes(save, 'wet')).slice(0, 7), [0, 255, 128, 51, 1, 254, 0], 'wet codes');
  T.eq(Array.from(rawBytes(save, 'owner')).slice(0, 14), [255, 255, 0, 0, 1, 0, 0x34, 0x12, 254, 255, 255, 127, 0, 128], 'owner bytes');
  T.eq(Array.from(rawBytes(save, 'owner')).slice(14, 18), [255, 255, 255, 255], 'default owner −1');
  T.eq(Array.from(rawBytes(save, 'road')).slice(0, 6), [0, 1, 2, 3, 255, 0], 'road bytes');
  T.eq(save.cells.road, 'AAECA/8AAAAAAAAA', 'road as base64');
});

// ------------------------------------------------------------------ cell round trip

function checkCells(L, seed) {
  const w = S.create({ seed, L });
  fillRandom(w, DM.U.rng(seed * 7 + 1));
  for (const k in w.dirty) w.dirty[k] = false;
  for (const k in w.need) w.need[k] = false;
  w.rivers.push({ cells: new Int32Array(2), flow: new Float32Array(2) });
  w.snapshots.push({ year: 3 });
  const before = {};
  for (const f of S.CELL_FIELDS) before[f.name] = w[f.name].slice();
  const json = JSON.stringify(S.serialize(w));
  for (const f of S.CELL_FIELDS) T.eq(firstDiff(w[f.name], before[f.name]), -1, 'serialize leaves ' + f.name + ' alone');
  const w2 = S.deserialize(JSON.parse(json));

  T.eq(w2.version, 1); T.eq(w2.L, L); T.eq(w2.n, w.n); T.eq(w2.seed, w.seed); T.eq(w2.name, w.name);
  T.ok(w2.geo === DM.Geo.get(L), 'geo rebuilt for L');
  for (const f of S.CELL_FIELDS) {
    const a = before[f.name], b = w2[f.name];
    T.ok(b instanceof CTOR[f.type] && b.length === w.n, f.name + ' type and length');
    T.ok(b !== w[f.name] && b.buffer !== w[f.name].buffer, f.name + ' is a new array');
    if (!f.save) {
      T.ok(allEqual(b, f.def), 'derived ' + f.name + ' is back at its default');
    } else if (f.save === 'raw') {
      T.eq(firstDiff(a, b), -1, 'raw ' + f.name + ' is exact');
    } else {
      const step = stepOf(f);
      let worst = 0, lo = Infinity, hi = -Infinity;
      for (let c = 0; c < w.n; c++) {
        const e = Math.abs(a[c] - b[c]);
        if (!(e <= worst)) worst = e;                       // also catches NaN
        if (b[c] < lo) lo = b[c];
        if (b[c] > hi) hi = b[c];
      }
      // half a step, plus the float32 rounding of the decoded value (at most 1e-6 at |v| ≤ 20)
      T.ok(worst <= step / 2 + 2e-6, f.name + ': max error ' + worst + ' exceeds half a step (' + step / 2 + ')');
      T.ok(worst > step / 4, f.name + ': max error ' + worst + ' is suspiciously small for random data');
      T.ok(b[0] === f.min && b[1] === Math.fround(f.max), f.name + ': both ends of the range are exact');
      T.ok(lo >= f.min && hi <= Math.fround(f.max), f.name + ': decoded values stay in range');
    }
  }
  for (const k in BAG_DEFAULTS.dirty) T.ok(w2.dirty[k] === true, 'dirty.' + k);
  for (const k in BAG_DEFAULTS.need) T.ok(w2.need[k] === true, 'need.' + k);
  T.eq(Object.keys(w2.dirty).join(), Object.keys(BAG_DEFAULTS.dirty).join()); T.eq(Object.keys(w2.need).join(), Object.keys(BAG_DEFAULTS.need).join());
  T.eq(w2.rivers, []); T.eq(w2.snapshots, []);
  T.eq(Object.keys(w2).sort().join(' '), Object.keys(w).sort().join(' '), 'a loaded world has the same properties as a created one');
  // a second generation is byte-identical: quantization does not drift from save to save
  T.ok(JSON.stringify(S.serialize(w2)) === json, 'saving the loaded world reproduces the save');
  return { w, w2, json };
}

T.test('round trip through JSON, random field contents, L = 4', () => { checkCells(4, 11); });

T.test('round trip through JSON, random field contents, L = 6', () => {
  const r = checkCells(6, 12);
  T.eq(r.w2.n, 40962);
  T.eq(S.sizeOf(JSON.parse(r.json)), r.json.length, 'sizeOf of an all-ASCII save');
});

T.test('a new world round-trips: defaults come back, fert and zero-based fields exactly', () => {
  const w = S.create({ seed: 3, name: 'Blank', L: 3 });
  const w2 = viaJson(w);
  for (const f of S.CELL_FIELDS) {
    if (f.name === 'elev') continue;
    T.ok(allEqual(w2[f.name], f.def), f.name + ' is exactly its default ' + f.def);
  }
  // 0 is not on elev's q16 grid (−12 … 20 in 65535 steps): it comes back as the nearest code
  const e = w2.elev[0];
  T.ok(Math.abs(e) <= stepOf(field('elev')) / 2 + 1e-9 && allEqual(w2.elev, e), 'elev 0 comes back within half a step: ' + e);
  for (const k in SCALAR_DEFAULTS) T.ok(w2[k] === SCALAR_DEFAULTS[k], k);
  for (const k in BAG_DEFAULTS) assert.deepStrictEqual(w2[k], BAG_DEFAULTS[k], k);
  T.eq(w2.name, 'Blank'); T.eq(w2.seed, 3); T.eq(w2.rng.s, 3);
});

T.test('quantization clamps out-of-range values and stores NaN as the default', () => {
  const w = S.create({ seed: 1, L: 1 });
  for (const f of S.CELL_FIELDS) {
    if (!quantized(f)) continue;
    const span = f.max - f.min;
    w[f.name].set([f.min - 1e-3, f.min - span, f.max + 1e-3, f.max + 10 * span, -Infinity, Infinity, NaN, -1e30, 1e30, f.min + span / 2]);
  }
  const w2 = viaJson(w);
  for (const f of S.CELL_FIELDS) {
    if (!quantized(f)) continue;
    const b = w2[f.name], max = Math.fround(f.max);
    T.eq([b[0], b[1], b[4], b[7]], [f.min, f.min, f.min, f.min], f.name + ' clamps to min');
    T.eq([b[2], b[3], b[5], b[8]], [max, max, max, max], f.name + ' clamps to max');
    T.near(b[6], f.def, stepOf(f) / 2 + 1e-9, f.name + ': NaN becomes the default');
    T.near(b[9], f.min + (f.max - f.min) / 2, stepOf(f) / 2 + 2e-6, f.name + ' mid-range');
    for (let c = 0; c < w.n; c++) T.ok(Number.isFinite(b[c]), f.name + '[' + c + '] is finite');
  }
});

T.test('every quantization code survives load → save unchanged (all 65,536, L = 6)', () => {
  const base = JSON.stringify(S.serialize(S.create({ seed: 5 })));
  const n = 40962;
  for (let pass = 0; pass < 2; pass++) {               // codes 0…40961, then 65535 down to 24574
    const save = JSON.parse(base);
    for (const f of S.CELL_FIELDS) {
      if (f.save === 'q8') {
        const b = Buffer.alloc(n);
        for (let i = 0; i < n; i++) b[i] = pass ? 255 - (i & 255) : i & 255;
        save.cells[f.name] = b.toString('base64');
      } else if (f.save === 'q16') {
        const b = Buffer.alloc(2 * n);
        for (let i = 0; i < n; i++) b.writeUInt16LE(pass ? 65535 - i : i, 2 * i);
        save.cells[f.name] = b.toString('base64');
      }
    }
    const w = S.deserialize(save), again = S.serialize(w);
    for (const f of S.CELL_FIELDS) {
      if (!quantized(f)) continue;
      T.ok(again.cells[f.name] === save.cells[f.name], f.name + ' pass ' + pass + ': codes changed');
      const a = w[f.name];
      T.ok(a[0] === (pass ? Math.fround(f.max) : f.min), f.name + ': code ' + (pass ? 'top' : 0) + ' decodes to the range end');
      const period = f.save === 'q8' ? 256 : n;
      for (let i = 1; i < period; i++) if (pass ? !(a[i] < a[i - 1]) : !(a[i] > a[i - 1])) T.ok(false, f.name + ': decoding is not strictly monotonic at code index ' + i);
    }
  }
});

// ------------------------------------------------------------------ scalars and entities

T.test('scalars and entities round-trip exactly', () => {
  const w = populate(S.create({ seed: 2024, name: 'Ærth — 世界 🌍', L: 2 }));
  const w2 = viaJson(w);
  T.ok(w2.name === w.name, 'name with non-ASCII characters');
  for (const k of PERSISTED_KEYS) assert.deepStrictEqual(w2[k], w[k], k);
  T.ok(w2.year === 1234.5625 && w2.seaLevel === -0.123456789 && w2.phase === 'history' && w2.age === 4 && w2.tick === 4938, 'scalars are exact');
  T.ok(w2.settlements[1].dead === true && w2.settlements[1].deadYear === 400.5 && w2.settlements[0].prayer.kind === 'rain');
  T.ok(w2.vars.rel['0|1'].war === true && w2.vars.flag === false && w2.vars.note === '');
  T.ok(w2.ending.type === 'ark' && w2.ending.year === 1999.5);
  // without the JSON step in between it works just the same
  const w3 = S.deserialize(S.serialize(w));
  for (const k of PERSISTED_KEYS) assert.deepStrictEqual(w3[k], w[k], k + ' (no JSON step)');
});

T.test('entities made by the schema.js factories round-trip', () => {
  if (!S.newSettlement) return;                       // schema.js not present
  const w = S.create({ seed: 8, L: 2 });
  const c = S.newCulture(w, { name: 'Ashari', traits: { piety: 0.9 } });
  const s = S.newSettlement(w, { cell: 3, name: 'Qel', culture: c.id, pop: 40, b: { shrine: 1 } });
  S.newUnit(w, { type: 'settler', culture: c.id, from: s.id, path: [3, 4, 5] });
  S.newDisaster(w, { type: 'storm', cell: 7, data: { front: [7, 8] } });
  const w2 = viaJson(w);
  for (const k of ['settlements', 'cultures', 'units', 'disasters', 'nextId']) assert.deepStrictEqual(w2[k], w[k], k);
});

T.test('serialize deep-copies: later changes to the world do not reach an earlier save', () => {
  const w = populate(S.create({ seed: 4, L: 2 }));
  fillRandom(w, DM.U.rng(99));
  const save = S.serialize(w), frozen = JSON.stringify(save);
  for (const k of ['god', 'settlements', 'cultures', 'units', 'disasters', 'chronicle', 'nextId', 'stats', 'settings', 'vars', 'ending']) {
    T.ok(save[k] !== w[k], k + ' is a copy');
  }
  T.ok(save.settlements[0] !== w.settlements[0] && save.settlements[0].b !== w.settlements[0].b && save.units[0].path !== w.units[0].path);
  T.ok(save.cultures[0].lang.rules.deep[1][1] !== w.cultures[0].lang.rules.deep[1][1], 'deep objects are copies too');
  T.ok(save.god.deeds !== w.god.deeds && save.vars.rel !== w.vars.rel);
  // mutate everything
  w.settlements[0].pop = 1; w.settlements[0].b.shrine = 99; w.settlements[0].prayer.kind = 'x'; w.settlements.push({ id: 2 });
  w.cultures[0].lang.onsets.push('zz'); w.cultures[0].lang.rules.deep[1][1].x = 5; w.cultures[0].color[0] = 0; w.cultures[1].prophet.name = 'Other';
  w.units[0].path.push(1); w.units[0].data.target = 3; w.units.length = 0;
  w.disasters[0].data.front[0] = 0; w.disasters[0].data.seeds[1][1] = 9;
  w.chronicle[0].text = 'rewritten'; w.chronicle.pop();
  w.god.awe = 0; w.god.deeds.rain = 0; w.god.deeds.plague = 4; w.nextId.unit = 100; w.stats.pop = 0; w.settings.calamity = 3;
  w.vars.rel['0|1'].war = false; w.vars.added = 1; w.ending.type = 'silence';
  w.year = 9; w.phase = 'ended'; w.seaLevel = 3; w.name = 'renamed'; w.rng.next();
  for (const f of S.CELL_FIELDS) w[f.name].fill(0);
  T.ok(JSON.stringify(save) === frozen, 'the save is unchanged');
});

T.test('deserialize deep-copies: the world and the object it was loaded from are independent', () => {
  const src = populate(S.create({ seed: 4, L: 2 }));
  const obj = JSON.parse(JSON.stringify(S.serialize(src))), frozen = JSON.stringify(obj);
  const w = S.deserialize(obj);
  T.ok(JSON.stringify(obj) === frozen, 'deserialize does not modify its input');
  T.ok(w.settlements !== obj.settlements && w.settlements[0] !== obj.settlements[0] && w.settlements[0].b !== obj.settlements[0].b);
  T.ok(w.god !== obj.god && w.god.deeds !== obj.god.deeds && w.vars !== obj.vars && w.vars.rel !== obj.vars.rel && w.ending !== obj.ending);
  T.ok(w.units[0].path !== obj.units[0].path && w.nextId !== obj.nextId && w.stats !== obj.stats && w.settings !== obj.settings);
  // world → object
  w.settlements[0].b.shrine = 50; w.units[0].path.push(4); w.god.deeds.rain = 100; w.vars.rel['0|1'].war = false; w.ending.mood = 'feared';
  w.chronicle.push({ id: 3 }); w.stats.pop = 1; w.elev.fill(1); w.year = 1;
  T.ok(JSON.stringify(obj) === frozen, 'changing the world does not change the save object');
  // object → world
  const w2 = S.deserialize(obj), before = JSON.stringify(S.serialize(w2));
  obj.settlements[0].b.shrine = 77; obj.settlements.push({ id: 9 }); obj.units[0].path.length = 0; obj.god.deeds.rain = -1; obj.vars.rel['0|1'].since = 0;
  obj.ending.type = 'silence'; obj.chronicle[0].text = ''; obj.cells.elev = ''; obj.year = 5; obj.stats.pop = -1; obj.nextId.unit = 0; obj.settings.calamity = 9;
  T.ok(JSON.stringify(S.serialize(w2)) === before, 'changing the save object does not change the world');
  // two worlds from one object share nothing
  const obj2 = JSON.parse(frozen), a = S.deserialize(obj2), b = S.deserialize(obj2);
  T.ok(a.settlements[0] !== b.settlements[0] && a.god !== b.god && a.elev.buffer !== b.elev.buffer && a.rng !== b.rng);
});

T.test('underscore-prefixed properties are transient: never saved, never loaded', () => {
  const w = populate(S.create({ seed: 6, L: 2 }));
  const clean = JSON.stringify(S.serialize(w));
  w._fires = { cells: new Int32Array(4), n: 0 };
  w._scratch = () => 1;
  w.settlements[0]._cells = new Int32Array([1, 2, 3]); w.settlements[0]._n = 3;
  w.settlements[0].b._dirty = true;
  w.cultures[0].lang._cache = new Map([[1, 2]]);
  w.cultures[0].lang.rules._memo = { f: () => 0 };
  w.units[0].data.waypoints.push({ _tmp: new Float32Array(2), keep: 1 });
  w.units[0]._self = w.units[0];                        // even a cycle is fine behind an underscore
  w.disasters[0].data._front32 = new Float32Array(9);
  w.chronicle[0]._el = {};
  w.god._lastPower = 'rain'; w.god.deeds._x = 1; w.vars._tmp = [1, 2]; w.stats._cache = 1; w.nextId._n = 1; w.settings._ui = {};
  w.ending._shown = true;
  const save = S.serialize(w), json = JSON.stringify(save);
  T.ok(json.indexOf('"_') < 0, 'no underscore key anywhere in the save');
  w.units[0].data.waypoints.pop();
  T.eq(save.units[0].data.waypoints, [{ keep: 1 }], 'the rest of the object is kept');
  assert.deepStrictEqual(JSON.parse(json), Object.assign(JSON.parse(clean), { units: save.units }), 'otherwise the save is as without them');
  // a save that carries underscore keys anyway (hand-edited, foreign) loads without them
  const dirty = JSON.parse(clean);
  dirty._extra = 1; dirty.settlements[0]._cells = [1, 2]; dirty.settlements[0].b._x = 1; dirty.god._y = 2; dirty.vars._z = { a: 1 };
  dirty.cultures[0].lang.rules.deep[1][1]._deep = true; dirty.ending._e = 1; dirty.chronicle[1]._c = 1;
  const w2 = S.deserialize(dirty);
  (function walk(v, path) {
    if (v === null || typeof v !== 'object' || ArrayBuffer.isView(v) || v === w2.geo || v === w2.rng) return;
    for (const k of Object.keys(v)) { T.ok(k[0] !== '_', 'loaded world has ' + path + '.' + k); walk(v[k], path + '.' + k); }
  })(w2, 'world');
  assert.deepStrictEqual(JSON.parse(JSON.stringify(S.serialize(w2))), JSON.parse(clean), 'and equals the clean world');
});

T.test('entity data is copied the way JSON would copy it', () => {
  const w = S.create({ seed: 1, L: 1 });
  const odd = { id: 0, a: NaN, b: Infinity, c: -Infinity, d: -0, e: undefined, f: () => 1, g: [undefined, () => 1, NaN, -0, [], {}], h: { i: undefined }, s: Symbol('x'),
    j: '', k: 0, l: false, m: null, n: [[[]]], o: { 10: 'ten', 2: 'two', z: 'z' }, p: 1e21, q: 5e-324, r: 'line\nbreak "quoted"   😀' };
  w.units.push(odd);
  w.vars.list = [1, [2, [3, [4]]]]; w.vars.nan = NaN; w.vars.undef = undefined; w.god.deeds.rain = Infinity;
  const strip = (k, v) => (typeof k === 'string' && k[0] === '_') ? undefined : v;
  const save = S.serialize(w);
  assert.deepStrictEqual(save.units, JSON.parse(JSON.stringify(w.units, strip)), 'units');
  assert.deepStrictEqual(save.vars, JSON.parse(JSON.stringify(w.vars, strip)), 'vars');
  assert.deepStrictEqual(save.god, JSON.parse(JSON.stringify(w.god, strip)), 'god');
  T.ok(Object.is(save.units[0].d, 0) && Object.is(save.units[0].g[3], 0), '-0 becomes 0, as in JSON');
  assert.deepStrictEqual(JSON.parse(JSON.stringify(save)), save, 'the save is already what JSON makes of it');
  // typed arrays do not belong in entities; if one slips in it is saved as a plain array
  w.units.push({ id: 1, cells: new Int32Array([3, 1, 2]), w: new Float32Array([0.5]) });
  T.eq(S.serialize(w).units[1], { id: 1, cells: [3, 1, 2], w: [0.5] });
  T.ok(Array.isArray(viaJson(w).units[1].cells));
});

T.test('serialize refuses a world that could not be loaded back', () => {
  const fresh = () => populate(S.create({ seed: 1, L: 1 }));
  S.serialize(fresh());
  for (const k of ['year', 'tick', 'heat', 'seaLevel', 'globalTemp', 'co2', 'dust', 'age']) {
    for (const v of [NaN, Infinity, undefined, null, '3']) {
      const w = fresh(); w[k] = v;
      T.throws(() => S.serialize(w), k + ' = ' + String(v));
    }
  }
  let w = fresh(); w.phase = 3; T.throws(() => S.serialize(w), 'phase is not a string');
  for (const k of ['settlements', 'cultures', 'units', 'disasters', 'chronicle']) {
    w = fresh(); w[k] = null; T.throws(() => S.serialize(w), k + ' = null');
    w = fresh(); w[k] = {}; T.throws(() => S.serialize(w), k + ' = {}');
  }
  for (const k of ['god', 'nextId', 'stats', 'settings', 'vars']) {
    w = fresh(); w[k] = null; T.throws(() => S.serialize(w), k + ' = null');
    w = fresh(); w[k] = []; T.throws(() => S.serialize(w), k + ' = []');
  }
  w = fresh(); w.elev = new Float32Array(3); T.throws(() => S.serialize(w), 'short cell array');
  w = fresh(); w.road = undefined; T.throws(() => S.serialize(w), 'missing cell array');
  w = fresh(); const u = { id: 1 }; u.self = u; w.units.push(u); T.throws(() => S.serialize(w), 'cyclic entity');
  w = fresh(); w.ending = undefined; T.ok(S.serialize(w).ending === null, 'a missing ending is saved as null');
  // and whatever it does return, loads
  w = fresh(); viaJson(w);
});

// ------------------------------------------------------------------ rng

T.test('the rng continues the same stream after a round trip', () => {
  const w = S.create({ seed: 123456789, L: 2 });
  for (let i = 0; i < 1000; i++) { w.rng.next(); w.rng.f(); w.rng.gauss(); w.rng.int(0, 9); }
  const w2 = viaJson(w);
  T.ok(w2.rng instanceof DM.U.Rng && w2.rng !== w.rng, 'a real Rng of its own');
  T.eq(w2.seed, 123456789, 'the seed is kept apart from the state');
  T.ok(w2.rng.s === w.rng.s && w2.rng.s !== w2.seed, 'state restored');
  T.ok(w2.rng.fork('chron:5').next() === w.rng.fork('chron:5').next() && w2.rng.fork(7).f() === w.rng.fork(7).f(), 'forks agree');
  for (let i = 0; i < 200; i++) T.ok(w2.rng.next() === w.rng.next(), 'draw ' + i);
  for (let i = 0; i < 50; i++) { T.ok(w2.rng.gauss() === w.rng.gauss()); T.ok(w2.rng.range(-3, 3) === w.rng.range(-3, 3)); }
  T.eq(w2.rng.shuffle([1, 2, 3, 4, 5, 6, 7, 8]), w.rng.shuffle([1, 2, 3, 4, 5, 6, 7, 8]));
  // the extremes of the state
  for (const s of [0, 1, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF]) {
    const a = S.create({ seed: 1, L: 1 }); a.rng.s = s;
    const b = viaJson(a);
    T.ok(b.rng.s === s && b.rng.next() === a.rng.next(), 'state ' + s);
  }
});

// ------------------------------------------------------------------ bad saves

const GOOD = JSON.stringify(S.serialize(populate(S.create({ seed: 21, name: 'Good', L: 1 }))));
const good = () => JSON.parse(GOOD);

T.test('deserialize rejects non-objects and foreign data with Error("bad save")', () => {
  S.deserialize(good());                               // the control loads
  const foreign = [undefined, null, 0, 1, NaN, true, false, '', 'bad save', GOOD, [], [good()], () => good(), Symbol('s'),
    {}, { hello: 'world' }, { version: 1 }, { format: 'demiurge-world' }, { format: 'demiurge-world', version: 1 },
    { format: 'demiurge-world', version: 1, L: 1, n: 42, cells: {} }, { version: 1, cells: [], settlements: [] },
    new Uint8Array(8), new Map(), JSON.parse('{"__proto__": {"format": "demiurge-world"}}')];
  foreign.forEach((v, i) => isBadSave(v, 'foreign #' + i));
  let err = null;
  try { S.deserialize(null); } catch (e) { err = e; }
  T.ok(err && err.constructor === Error && err.message === 'bad save' && typeof err.reason === 'string', 'a plain Error with a reason attached');
});

T.test('deserialize rejects wrong versions and markers', () => {
  for (const v of [0, 2, 1.5, -1, '1', null, undefined, true, [1], { v: 1 }]) { const s = good(); s.version = v; isBadSave(s, 'version ' + JSON.stringify(v)); }
  for (const v of ['', 'demiurge', 'DEMIURGE-WORLD', 'demiurge-world ', 1, null, undefined, ['demiurge-world']]) { const s = good(); s.format = v; isBadSave(s, 'format ' + JSON.stringify(v)); }
});

T.test('deserialize rejects a save with any top-level field missing or of the wrong kind', () => {
  const keys = Object.keys(good());
  T.eq(keys.length, 28);
  for (const k of keys) {
    let s = good(); delete s[k]; isBadSave(s, 'without ' + k);
    s = good(); s[k] = undefined; isBadSave(s, k + ' = undefined');
    if (k !== 'ending') { s = good(); s[k] = null; isBadSave(s, k + ' = null'); }
  }
  const wrong = {
    seed: [-1, 1.5, 4294967296, '21', NaN], rng: [-1, 0.5, 4294967296, '5', Infinity], name: [5, ['x'], {}],
    L: ['1', 1.5, -1, 8, 100, NaN], n: [41, 43, 162, '42', 42.5],
    year: ['0', NaN, Infinity, [], {}], tick: ['1', NaN], phase: [0, ['history'], {}], heat: ['1', -Infinity], seaLevel: ['0', NaN, {}],
    globalTemp: [[], NaN], co2: ['', NaN], dust: [true, NaN], age: ['4', NaN],
    cells: ['', 5, [], true],
    settlements: [{}, 'x', 0], cultures: [{ length: 0 }, ''], units: [5, {}], disasters: [{}, true], chronicle: ['[]', {}],
    god: [[], 'god', 1], nextId: [[], 3], stats: [[], 'x'], settings: [[], 0], vars: [[], 'v', 2],
    ending: [0, 'ark', [], true],
  };
  for (const k in wrong) for (const v of wrong[k]) { const s = good(); s[k] = v; isBadSave(s, k + ' = ' + String(JSON.stringify(v))); }
  S.deserialize(good());                               // still fine afterwards
});

T.test('deserialize rejects missing, short, long and garbled cell arrays', () => {
  const persisted = S.CELL_FIELDS.filter(f => f.save);
  T.eq(persisted.length, 15);
  for (const f of persisted) {
    const name = f.name, str = good().cells[name], size = bytesOf(f);
    const mut = (label, v) => { const s = good(); if (v === undefined) delete s.cells[name]; else s.cells[name] = v; isBadSave(s, name + ' ' + label); };
    mut('missing');
    mut('null', null); mut('number', 7); mut('array', [1, 2, 3]); mut('object', { 0: 1 });
    mut('empty', '');
    mut('one group short', str.slice(0, -4));
    mut('one group long', str + 'AAAA');
    mut('one cell short', Buffer.alloc(41 * size).toString('base64'));
    mut('one cell long', Buffer.alloc(43 * size).toString('base64'));
    mut('one byte short', Buffer.alloc(42 * size - 1).toString('base64'));
    mut('one byte long', Buffer.alloc(42 * size + 1).toString('base64'));
    mut('from another level', Buffer.alloc(162 * size).toString('base64'));
    mut('not base64', '!' + str.slice(1));
    mut('not base64 at the end', str.slice(0, -2) + '*' + str.slice(-1));
    mut('data after padding', str + '=A');
    mut('non-ASCII', 'é' + str.slice(1));
  }
  // swapping two fields of different width is caught too
  const s = good(); const t = s.cells.elev; s.cells.elev = s.cells.wet; s.cells.wet = t; isBadSave(s, 'elev and wet swapped');
  // a save for another level with consistent L and n but this level's arrays
  const s2 = good(); s2.L = 2; s2.n = 162; isBadSave(s2, 'L = 2 with L = 1 arrays');
  // extra unknown cell arrays are ignored
  const s3 = good(); s3.cells.future = 'AAAA'; S.deserialize(s3);
});

T.test('deserialize turns any failure into "bad save" and leaves nothing half-loaded behind', () => {
  // hostile nesting: far deeper than any entity
  const s = good(); let deep = s.vars; for (let i = 0; i < 200; i++) deep = deep.d = {};
  isBadSave(s, 'absurdly deep vars');
  const s2 = good(); let arr = s2.units; for (let i = 0; i < 200; i++) { const inner = []; arr.push(inner); arr = inner; }
  isBadSave(s2, 'absurdly deep array');
  // a truncated JSON text never gets as far as an object; whatever JSON.parse yields for other text is rejected
  for (const text of ['null', '0', '"x"', '[]', '{}', '{"format":"demiurge-world","version":1}']) isBadSave(JSON.parse(text), 'JSON ' + text);
  // the good save still loads, and loads the same
  const w = S.deserialize(good());
  T.ok(JSON.stringify(S.serialize(w)) === GOOD);
});

T.test('deserialize fills in keys an older save lacks inside god, nextId, stats and settings', () => {
  const s = good();
  delete s.god.aweMax; s.god.awe = null; s.god.epithet = 5; s.god.deeds = 'x';
  delete s.stats.wars; s.stats.peakPop = 'many'; s.stats.custom = 5;
  delete s.nextId.entry; s.settings = {}; s.vars = {};
  const w = S.deserialize(s);
  T.eq([w.god.awe, w.god.aweMax, w.god.mercy, w.god.wrath, w.god.epithet, JSON.stringify(w.god.deeds), Object.keys(w.god).length], [0, 100, 12, 3, '', '{}', 6], 'god');
  T.eq(w.stats.wars, 0); T.eq(w.stats.peakPop, 0); T.eq(w.stats.custom, 5, 'unknown keys are kept'); T.eq(w.stats.pop, 1234);
  T.eq(w.nextId, { unit: 8, disaster: 4, entry: 1 });
  T.eq(w.settings, { calamity: 1 }); T.eq(w.vars, {});
  // defaults handed out are not shared between worlds
  const a = S.deserialize(s), b = S.deserialize(s);
  T.ok(a.god.deeds !== b.god.deeds && a.settings !== b.settings);
});

// ------------------------------------------------------------------ the table drives everything

T.test('adding a row to CELL_FIELDS is all it takes (create, serialize, deserialize)', () => {
  const extra = [
    { name: 'xq8', type: 'f32', def: 2, save: 'q8', min: -1, max: 3 },
    { name: 'xq16', type: 'f32', def: -25, save: 'q16', min: -100, max: 100 },
    { name: 'xu8', type: 'u8', def: 7, save: 'raw', min: null, max: null },
    { name: 'xi16', type: 'i16', def: -5, save: 'raw', min: null, max: null },
    { name: 'xi32', type: 'i32', def: -1, save: 'raw', min: null, max: null },
    { name: 'xf32', type: 'f32', def: 0.25, save: 'raw', min: null, max: null },
    { name: 'xder', type: 'i32', def: 9, save: null, min: null, max: null },
  ];
  const oldSave = JSON.stringify(S.serialize(S.create({ seed: 1, L: 2 })));
  T.stub(S, 'CELL_FIELDS', S.CELL_FIELDS.concat(extra));
  const w = S.create({ seed: 1, L: 2 });
  for (const f of extra) T.ok(w[f.name] instanceof CTOR[f.type] && w[f.name].length === 162 && allEqual(w[f.name], f.def), f.name + ' allocated with its default');
  // defaults survive (2 is code 191.25 → within half a step; −25 likewise)
  let w2 = viaJson(w);
  T.near(w2.xq8[0], 2, 4 / 255 / 2 + 1e-6); T.near(w2.xq16[0], -25, 200 / 65535 / 2 + 1e-6);
  for (const f of extra) if (f.save === 'raw' || !f.save) T.ok(allEqual(w2[f.name], f.def), f.name + ' default');
  // contents
  const r = DM.U.rng(5);
  fillRandom(w, r);
  w.xi32.set([-2147483648, 2147483647, -1, 0, 0x12345678]);
  w.xf32.set([NaN, Infinity, -Infinity, -0, 1e-45, 3.4028234663852886e38, Math.fround(0.1)]);
  w.xder.fill(1234);
  const save = S.serialize(w);
  T.eq(Object.keys(save.cells).slice(-6).join(), 'xq8,xq16,xu8,xi16,xi32,xf32', 'new persisted fields are in the save, the derived one is not');
  T.eq(Array.from(rawBytes(save, 'xi32').subarray(16, 20)), [0x78, 0x56, 0x34, 0x12], 'i32 is little-endian');
  T.eq(Array.from(rawBytes(save, 'xf32').subarray(4, 8)), [0, 0, 0x80, 0x7f], 'f32 is little-endian IEEE');
  w2 = S.deserialize(JSON.parse(JSON.stringify(save)));
  for (const f of extra) {
    const a = w[f.name], b = w2[f.name];
    if (!f.save) T.ok(allEqual(b, f.def), f.name + ' (derived) is back at its default');
    else if (f.save === 'raw') T.eq(firstDiff(a, b), -1, f.name + ' is bit-exact');
    else { let worst = 0; for (let c = 0; c < 162; c++) worst = Math.max(worst, Math.abs(a[c] - b[c])); T.ok(worst <= stepOf(f) / 2 + 1e-5, f.name + ' within half a step: ' + worst); }
  }
  // a save written before the row existed lacks the field and is refused
  isBadSave(JSON.parse(oldSave), 'save without the new fields');
  // a truncated new field is refused as well
  const cut = JSON.parse(JSON.stringify(save)); cut.cells.xi32 = cut.cells.xi32.slice(0, -4); isBadSave(cut, 'short xi32');
});

T.test('the stubbed table is gone again', () => {
  T.eq(S.CELL_FIELDS.length, TABLE.length);
  T.ok(S.create({ seed: 1, L: 1 }).xq8 === undefined);
});

// ------------------------------------------------------------------ sizeOf

T.test('sizeOf is the UTF-8 byte length of the JSON text', () => {
  const samples = [null, 0, 'x', [], {}, { a: 1, b: [true, null, 'two'] }, 'é', '€', 'ß天𝄞', '😀🌍', { 'ключ': 'значение', k: ['\u0000', '\u007f', '\u0080', '߿', 'ࠀ', '￿'] },
    '\ud800', 'a\udc00b', '\ud83d', 'quote " backslash \\ newline \n tab \t', '  '];
  for (const v of samples) T.eq(S.sizeOf(v), Buffer.byteLength(JSON.stringify(v), 'utf8'), 'sizeOf(' + JSON.stringify(v) + ')');
  T.eq(S.sizeOf(undefined), 0); T.eq(S.sizeOf(() => 1), 0);
  const w = populate(S.create({ seed: 2, name: 'Ærth — 世界 🌍', L: 2 }));
  const save = S.serialize(w), json = JSON.stringify(save);
  T.eq(S.sizeOf(save), Buffer.byteLength(json, 'utf8'));
  T.ok(S.sizeOf(save) > json.length, 'non-ASCII text takes more bytes than characters');
});

T.test('an empty world at L = 6 serializes to 19 bytes per cell, well under 1.2 MB of JSON', () => {
  const w = S.create({ seed: 1 });
  const save = S.serialize(w), size = S.sizeOf(save);
  let payload = 0;
  for (const f of S.CELL_FIELDS) if (f.save) payload += 4 * Math.ceil(w.n * bytesOf(f) / 3);
  T.eq(payload, 1037704, 'base64 characters of cell data');
  T.eq(size, JSON.stringify(save).length);
  T.ok(size >= payload && size < payload + 2000, 'overhead beyond the cell data is under 2 kB: ' + (size - payload));
  T.ok(size < 1.2e6, 'JSON size ' + size + ' is under the 1.2 MB target');
});
