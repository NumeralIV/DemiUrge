// DEMIURGE — js/sim/state.js : the world object (FOUNDATION §5). Allocation, defaults and
// (de)serialization only; no simulation logic. Runs unchanged in Node and browsers.
//
//   DM.State.CELL_FIELDS               the per-cell field table of §5.1 as data. create, serialize and
//                                      deserialize all loop over it: a new field is one new row.
//   DM.State.ELEV_MIN = -12, DM.State.ELEV_MAX = 20
//   DM.State.FORMAT, DM.State.VERSION  marker and version written into every save
//   DM.State.create({ seed, name = 'Unnamed', L = 6, geo = null }) → world
//   DM.State.isSea(world, c) → bool    elev[c] < seaLevel
//   DM.State.groundR(world, c)         DM.R + elev[c]
//   DM.State.surfaceR(world, c)        sea cell: DM.R + seaLevel; land cell: DM.R + elev[c] + water[c]
//   DM.State.serialize(world) → plain JSON-compatible object that shares nothing with the world.
//                                      Throws if the world could not be loaded back (a non-finite
//                                      scalar, a missing list, a cell array of the wrong length).
//   DM.State.deserialize(obj) → world  shares nothing with obj; throws Error('bad save')
//   DM.State.sizeOf(obj) → bytes of JSON.stringify(obj) as UTF-8
//   DM.BIOME, DM.BIOME_NAMES, DM.AGES
// (Entity factories and the shared tables live in schema.js.)
//
// Save layout, version 1:
//   { format: 'demiurge-world', version: 1, seed, name, L, n, rng,          rng = world.rng.s
//     year, tick, phase, heat, seaLevel, globalTemp, co2, dust, age,
//     cells: { <name of each persisted field>: base64 string },
//     god, settlements, cultures, units, disasters, chronicle, nextId, stats, settings, vars, ending }
// Cell encodings (multi-byte values are little-endian on every platform):
//   q8 / q16   code = round((v − min) / (max − min) · top) clamped to 0 … top, top = 255 / 65535;
//              decoded v = min + (max − min) · code / top. In-range values come back within half
//              a step, out-of-range values are clamped, NaN is stored as the field's default.
//              Decoding and encoding again gives the same code, so re-saving a loaded world
//              reproduces its save byte for byte.
//   raw        the array's own elements: u8 1 byte, i16 2 bytes, i32 and f32 4 bytes
// Not saved: geo, derived cell fields, dirty, need, snapshots, rivers, and every property whose
// name starts with an underscore (on the world or inside any entity, at any depth).
(function (DM) {
  'use strict';

  const State = (DM.State = DM.State || {});
  const FORMAT = 'demiurge-world', VERSION = 1;

  State.FORMAT = FORMAT;
  State.VERSION = VERSION;
  State.ELEV_MIN = -12;
  State.ELEV_MAX = 20;

  // FOUNDATION §5.1, in the order listed there. save: how the field is persisted (null = derived,
  // recomputed after loading). min / max: the quantization range (null where there is none).
  State.CELL_FIELDS = [
    { name: 'elev',   type: 'f32', def: 0,   save: 'q16', min: State.ELEV_MIN, max: State.ELEV_MAX },
    { name: 'water',  type: 'f32', def: 0,   save: 'q16', min: 0,    max: 8 },
    { name: 'flow',   type: 'f32', def: 0,   save: null,  min: null, max: null },
    { name: 'down',   type: 'i32', def: -1,  save: null,  min: null, max: null },
    { name: 'temp',   type: 'f32', def: 0,   save: null,  min: null, max: null },
    { name: 'moist',  type: 'f32', def: 0,   save: null,  min: null, max: null },
    { name: 'wet',    type: 'f32', def: 0,   save: 'q8',  min: 0,    max: 1 },
    { name: 'veg',    type: 'f32', def: 0,   save: 'q8',  min: 0,    max: 1 },
    { name: 'biome',  type: 'u8',  def: 0,   save: null,  min: null, max: null },
    { name: 'fert',   type: 'f32', def: 0.5, save: 'q8',  min: 0,    max: 1.5 },
    { name: 'ice',    type: 'f32', def: 0,   save: 'q8',  min: 0,    max: 1 },
    { name: 'fire',   type: 'f32', def: 0,   save: 'q8',  min: 0,    max: 1 },
    { name: 'scorch', type: 'f32', def: 0,   save: 'q8',  min: 0,    max: 1 },
    { name: 'lava',   type: 'f32', def: 0,   save: 'q16', min: 0,    max: 8 },
    { name: 'fauna',  type: 'f32', def: 0,   save: 'q8',  min: 0,    max: 1 },
    { name: 'rad',    type: 'f32', def: 0,   save: 'q8',  min: 0,    max: 1 },
    { name: 'owner',  type: 'i16', def: -1,  save: 'raw', min: null, max: null },
    { name: 'farm',   type: 'f32', def: 0,   save: 'q8',  min: 0,    max: 1 },
    { name: 'urban',  type: 'f32', def: 0,   save: 'q8',  min: 0,    max: 1 },
    { name: 'road',   type: 'u8',  def: 0,   save: 'raw', min: null, max: null },
    { name: 'hab',    type: 'f32', def: 0,   save: null,  min: null, max: null },
    { name: 'slope',  type: 'f32', def: 0,   save: null,  min: null, max: null },
    { name: 'coast',  type: 'u8',  def: 255, save: null,  min: null, max: null },
    { name: 'fresh',  type: 'u8',  def: 255, save: null,  min: null, max: null },
  ];

  const ARRAY_TYPE = { f32: Float32Array, u8: Uint8Array, i16: Int16Array, i32: Int32Array };
  const RAW_BYTES = { f32: 4, u8: 1, i16: 2, i32: 4 };

  DM.BIOME = {
    SEA: 0, SHALLOWS: 1, BEACH: 2, DESERT: 3, SAVANNA: 4, GRASSLAND: 5, FOREST: 6, RAINFOREST: 7,
    TAIGA: 8, TUNDRA: 9, ICE: 10, ROCK: 11, SNOWPEAK: 12, WETLAND: 13, BARREN: 14, SEAICE: 15,
  };
  DM.BIOME_NAMES = ['Sea', 'Shallows', 'Beach', 'Desert', 'Savanna', 'Grassland', 'Forest', 'Rainforest',
    'Taiga', 'Tundra', 'Ice', 'Rock', 'Snow peak', 'Wetland', 'Barren', 'Sea ice'];
  DM.AGES = [
    { id: 0, name: 'Stone' }, { id: 1, name: 'Bronze' }, { id: 2, name: 'Iron' }, { id: 3, name: 'Sail' },
    { id: 4, name: 'Industry' }, { id: 5, name: 'Atomic' }, { id: 6, name: 'Space' },
  ];

  // ------------------------------------------------------------------ the world

  // Everything on a new world except identity, geo, rng and the cell arrays (FOUNDATION §5.2).
  // Always a fresh object: nothing in it is shared between worlds.
  function blank() {
    return {
      year: 0, tick: 0, phase: 'molten', heat: 1, seaLevel: -9, globalTemp: 0, co2: 0, dust: 0, age: 0,
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
  }
  // The persisted part of blank(), by kind. `ending` (null or an object) is handled on its own.
  const SCALARS = ['year', 'tick', 'phase', 'heat', 'seaLevel', 'globalTemp', 'co2', 'dust', 'age'];
  const LISTS = ['settlements', 'cultures', 'units', 'disasters', 'chronicle'];
  const BAGS = ['god', 'nextId', 'stats', 'settings', 'vars'];

  // seed: any int, coerced with >>> 0. A geo passed in is reused as is and decides L and n.
  State.create = function (opts) {
    opts = opts || {};
    const geo = opts.geo || DM.Geo.get(opts.L == null ? 6 : opts.L);
    const seed = opts.seed >>> 0;
    const world = {
      version: VERSION, seed, name: opts.name == null ? 'Unnamed' : String(opts.name),
      L: geo.L, n: geo.n, geo, rng: DM.U.rng(seed),
    };
    for (const f of State.CELL_FIELDS) {
      const a = new ARRAY_TYPE[f.type](geo.n);
      if (f.def !== 0) a.fill(f.def);
      world[f.name] = a;
    }
    return Object.assign(world, blank());
  };

  State.isSea = function (world, c) { return world.elev[c] < world.seaLevel; };
  State.groundR = function (world, c) { return DM.R + world.elev[c]; };
  State.surfaceR = function (world, c) {
    const e = world.elev[c];
    return e < world.seaLevel ? DM.R + world.seaLevel : DM.R + e + world.water[c];
  };

  // ------------------------------------------------------------------ plain data

  function isObj(v) { return v !== null && typeof v === 'object' && !Array.isArray(v); }
  function isU32(v) { return Number.isInteger(v) && v >= 0 && v <= 0xFFFFFFFF; }
  // true if v could stand where the default d stands: a finite number for a number, else the same type
  function sameKind(v, d) { return typeof d === 'number' ? Number.isFinite(v) : typeof v === typeof d; }

  // Deep copy of JSON-compatible data with the result JSON.parse(JSON.stringify(v)) would give
  // (non-finite numbers → null, functions and undefined dropped from objects and null in arrays),
  // except that properties whose name starts with '_' (transient caches) are left out and typed
  // arrays, which entities must not hold, become plain arrays instead of index-keyed objects.
  const MAX_DEPTH = 64;
  function plain(v, depth) {
    const t = typeof v;
    if (t === 'number') return Number.isFinite(v) ? v + 0 : null;          // + 0 turns -0 into 0, as JSON does
    if (t === 'string' || t === 'boolean') return v;
    if (t !== 'object' || v === null) return null;
    if (depth >= MAX_DEPTH) throw new Error('DM.State: entity data is cyclic or nested deeper than ' + MAX_DEPTH + ' levels');
    if (Array.isArray(v) || ArrayBuffer.isView(v)) {
      const len = v.length >>> 0, out = new Array(len);
      for (let i = 0; i < len; i++) out[i] = plain(v[i], depth + 1);
      return out;
    }
    const out = {};
    for (const k in v) {
      if (k.charCodeAt(0) === 95) continue;                              // '_…' (this also keeps '__proto__' out)
      const x = v[k], tx = typeof x;
      if (tx === 'undefined' || tx === 'function' || tx === 'symbol') continue;
      out[k] = plain(x, depth + 1);
    }
    return out;
  }

  // Give obj every key of def that it lacks or holds with the wrong kind of value (older saves).
  function fillDefaults(obj, def) {
    for (const k in def) {
      const d = def[k];
      if (isObj(d)) { if (isObj(obj[k])) fillDefaults(obj[k], d); else obj[k] = d; }
      else if (!sameKind(obj[k], d)) obj[k] = d;
    }
  }

  // ------------------------------------------------------------------ cell codecs

  function fieldBytes(f) { return f.save === 'q8' ? 1 : f.save === 'q16' ? 2 : RAW_BYTES[f.type]; }

  function encodeField(f, a) {
    const n = a.length, out = new Uint8Array(n * fieldBytes(f));
    const dv = new DataView(out.buffer);
    if (f.save === 'q8' || f.save === 'q16') {
      const wide = f.save === 'q16', top = wide ? 65535 : 255, min = f.min, k = top / (f.max - f.min);
      let dq = Math.round((f.def - min) * k);                              // what NaN is stored as
      dq = dq > top ? top : dq > 0 ? dq : 0;
      for (let i = 0; i < n; i++) {
        const v = a[i];
        let q = Math.round((v - min) * k);
        q = q > top ? top : q > 0 ? q : v !== v ? dq : 0;
        if (wide) dv.setUint16(2 * i, q, true); else out[i] = q;
      }
    } else if (f.type === 'u8') out.set(a);
    else if (f.type === 'i16') for (let i = 0; i < n; i++) dv.setInt16(2 * i, a[i], true);
    else if (f.type === 'i32') for (let i = 0; i < n; i++) dv.setInt32(4 * i, a[i], true);
    else for (let i = 0; i < n; i++) dv.setFloat32(4 * i, a[i], true);
    return DM.U.b64enc(out);
  }

  function decodeField(f, str, a) {
    const n = a.length;
    let bytes;
    try { bytes = DM.U.b64dec(str); } catch (e) { bad('cells.' + f.name + ': ' + (e && e.message)); }
    if (bytes.length !== n * fieldBytes(f)) bad('cells.' + f.name + ': ' + bytes.length + ' bytes, expected ' + n * fieldBytes(f));
    const dv = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    if (f.save === 'q8') {
      const min = f.min, span = f.max - f.min;
      for (let i = 0; i < n; i++) a[i] = min + span * (bytes[i] / 255);
    } else if (f.save === 'q16') {
      const min = f.min, span = f.max - f.min;
      for (let i = 0; i < n; i++) a[i] = min + span * (dv.getUint16(2 * i, true) / 65535);
    } else if (f.type === 'u8') a.set(bytes);
    else if (f.type === 'i16') for (let i = 0; i < n; i++) a[i] = dv.getInt16(2 * i, true);
    else if (f.type === 'i32') for (let i = 0; i < n; i++) a[i] = dv.getInt32(4 * i, true);
    else for (let i = 0; i < n; i++) a[i] = dv.getFloat32(4 * i, true);
  }

  // ------------------------------------------------------------------ serialize

  // A world that deserialize would refuse must not become a save (it would replace a good one).
  function corrupt(what) { throw new Error('DM.State.serialize: ' + what); }

  State.serialize = function (world) {
    const def = blank();
    const out = {
      format: FORMAT, version: VERSION, seed: world.seed >>> 0, name: String(world.name),
      L: world.L, n: world.n, rng: world.rng.s >>> 0,
    };
    for (const k of SCALARS) {
      if (!sameKind(world[k], def[k])) corrupt('world.' + k + ' is ' + String(world[k]));
      out[k] = world[k];
    }
    const cells = out.cells = {};
    for (const f of State.CELL_FIELDS) {
      if (!f.save) continue;
      const a = world[f.name];
      if (!a || a.length !== world.n) corrupt('world.' + f.name + ' is not an array of ' + world.n + ' cells');
      cells[f.name] = encodeField(f, a);
    }
    for (const k of LISTS) {
      if (!Array.isArray(world[k])) corrupt('world.' + k + ' is not an array');
      out[k] = plain(world[k], 0);
    }
    for (const k of BAGS) {
      if (!isObj(world[k])) corrupt('world.' + k + ' is not an object');
      out[k] = plain(world[k], 0);
    }
    out.ending = isObj(world.ending) ? plain(world.ending, 0) : null;
    return out;
  };

  // ------------------------------------------------------------------ deserialize

  // The message is always exactly 'bad save'; err.reason says what was wrong (for debugging).
  function bad(reason) {
    const err = new Error('bad save');
    err.reason = reason;
    throw err;
  }

  function load(obj) {
    if (!isObj(obj)) bad('not an object');
    if (obj.format !== FORMAT) bad('format');
    if (obj.version !== VERSION) bad('version');
    const L = obj.L;
    if (!Number.isInteger(L) || L < 0 || L > 7) bad('L');
    if (obj.n !== 10 * Math.pow(4, L) + 2) bad('n');
    if (!isU32(obj.seed)) bad('seed');
    if (!isU32(obj.rng)) bad('rng');
    if (typeof obj.name !== 'string') bad('name');
    const def = blank();
    for (const k of SCALARS) if (!sameKind(obj[k], def[k])) bad(k);
    for (const k of LISTS) if (!Array.isArray(obj[k])) bad(k);
    for (const k of BAGS) if (!isObj(obj[k])) bad(k);
    if (obj.ending !== null && !isObj(obj.ending)) bad('ending');
    if (!isObj(obj.cells)) bad('cells');
    for (const f of State.CELL_FIELDS) if (f.save && typeof obj.cells[f.name] !== 'string') bad('cells.' + f.name);

    // create() supplies geo, the derived fields at their defaults, empty rivers and snapshots,
    // and every dirty / need flag set.
    const world = State.create({ seed: obj.seed, name: obj.name, L });
    for (const f of State.CELL_FIELDS) if (f.save) decodeField(f, obj.cells[f.name], world[f.name]);
    world.rng.s = obj.rng;
    for (const k of SCALARS) world[k] = obj[k];
    for (const k of LISTS) world[k] = plain(obj[k], 0);
    for (const k of BAGS) { world[k] = plain(obj[k], 0); fillDefaults(world[k], def[k]); }
    world.ending = plain(obj.ending, 0);
    return world;
  }

  State.deserialize = function (obj) {
    try { return load(obj); }
    catch (e) {
      if (e && e.message === 'bad save') throw e;
      bad(String(e && e.message));
    }
  };

  // ------------------------------------------------------------------ size

  // Exact byte length of JSON.stringify(obj) in UTF-8 (0 if obj has no JSON form).
  State.sizeOf = function (obj) {
    const s = JSON.stringify(obj);
    if (typeof s !== 'string') return 0;
    const len = s.length;
    let bytes = len;
    for (let i = 0; i < len; i++) {
      const c = s.charCodeAt(i);
      // 2 bytes below U+0800; 3 above, except that each half of a surrogate pair counts 2 (4 per pair)
      if (c > 0x7F) bytes += (c < 0x800 || (c & 0xF800) === 0xD800) ? 1 : 2;
    }
    return bytes;
  };
})(globalThis.DM = globalThis.DM || {});
