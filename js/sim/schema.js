// DEMIURGE — schema.js : entity factories and shared tables (SPEC-SIM §1, §2). Extends DM.State.
//   DM.BUILDINGS, DM.CULTURE_COLORS, DM.EVENTS, DM.State.VEG_T
//   DM.State.newSettlement(world, fields) / newCulture / newUnit / newDisaster  -> entity (pushed into its world list)
// Entities are created ONLY through these factories so every module agrees on their shape.
(function (DM) {
  'use strict';
  const State = (DM.State = DM.State || {});

  DM.BUILDINGS = ['shrine', 'temple', 'granary', 'walls', 'port', 'market', 'mine', 'factory', 'academy', 'plant', 'launch', 'wonder'];

  // colour-blind-safe palette (Okabe–Ito), sRGB hex -> approximate linear RGB
  DM.CULTURE_COLORS = [
    [0.787, 0.342, 0.0],     // #e69f00 orange
    [0.093, 0.458, 0.807],   // #56b4e9 sky blue
    [0.0, 0.342, 0.171],     // #009e73 green
    [0.871, 0.776, 0.058],   // #f0e442 yellow
    [0.665, 0.112, 0.0],     // #d55e00 vermilion
    [0.604, 0.191, 0.392],   // #cc79a7 purple
  ];

  // vegetation levels at which a cell shows 1, 2, 3 trees (Life sets dirty.objects when a cell crosses one)
  State.VEG_T = [0.22, 0.5, 0.78];

  // SPEC-SIM §2 as data: event name -> payload keys
  DM.EVENTS = {
    'phase': ['phase', 'prev'],
    'power:used': ['id', 'cell', 'ang', 'brush'],
    'miracle:witnessed': ['cell', 'kind', 'good', 'ids'],
    'lightning': ['cell', 'natural'],
    'meteor:launched': ['cell', 'eta', 'kind', 'id'],
    'meteor:impact': ['cell', 'ang', 'kind', 'sea'],
    'tsunami:start': ['cell'],
    'tsunami:hit': ['cell', 'height'],
    'volcano:start': ['cell', 'id', 'natural'],
    'volcano:end': ['cell', 'id'],
    'quake': ['cell', 'ang', 'power', 'natural'],
    'fire:start': ['cell'],
    'storm': ['cell', 'ang', 'start'],
    'drought': ['cell', 'ang', 'start'],
    'settlement:founded': ['id', 'cell', 'culture', 'by'],
    'settlement:tier': ['id', 'tier', 'prev'],
    'settlement:building': ['id', 'building'],
    'settlement:famine': ['id'],
    'settlement:plague': ['id', 'start'],
    'settlement:destroyed': ['id', 'cell', 'culture', 'cause'],
    'settlement:captured': ['id', 'from', 'to', 'how'],
    'prayer': ['id', 'kind', 'start'],
    'prayer:answered': ['id', 'kind'],
    'culture:born': ['id', 'parent', 'cell', 'reason'],
    'culture:died': ['id'],
    'culture:age': ['id', 'age', 'prev'],
    'world:age': ['age'],
    'war:declared': ['a', 'b'],
    'war:ended': ['a', 'b', 'victor'],
    'battle': ['cell', 'attacker', 'defender', 'won', 'settlement'],
    'trade:route': ['a', 'b'],
    'prophet:risen': ['culture', 'settlement', 'name'],
    'prophet:died': ['culture', 'name'],
    'edict': ['culture', 'edict'],
    'wonder:built': ['settlement', 'name'],
    'nuke': ['cell', 'from', 'to'],
    'ark:started': ['culture', 'settlement'],
    'ark:launched': ['culture', 'settlement'],
    'climate:milestone': ['kind', 'value'],
    'unit:spawned': ['id', 'type', 'culture', 'cell'],
    'unit:arrived': ['id', 'type', 'culture', 'cell'],
    'unit:died': ['id', 'type', 'culture', 'cell', 'cause'],
    'chronicle:entry': ['entry'],
    'awe:short': ['id'],
    'people:lost': [],
    'ending': ['type', 'mood', 'culture', 'settlement', 'year'],
    // interface events (SPEC-UI §0); listed so tests that watch the Bus accept them
    'ui:state': ['state', 'prev'],
    'ui:speed': ['speed', 'paused'],
    'ui:power': ['id'],
    'ui:select': ['id'],
    'ui:overlay': ['mode'],
    'ui:hud': ['hidden'],
    'ui:settings': [],
    'ui:toast': ['text', 'kind'],
    'camera:shake': ['amount'],
  };

  // shallow merge with one level of nesting for plain-object fields (b, traits, data, seen)
  function fill(def, fields) {
    if (fields) {
      for (const k in fields) {
        const v = fields[k], d = def[k];
        if (v && d && typeof v === 'object' && typeof d === 'object' && !Array.isArray(v) && !Array.isArray(d)) { for (const j in v) d[j] = v[j]; }
        else def[k] = v;
      }
    }
    return def;
  }

  State.newSettlement = function (world, fields) {
    const b = {};
    for (let i = 0; i < DM.BUILDINGS.length; i++) b[DM.BUILDINGS[i]] = 0;
    const s = fill({
      id: world.settlements.length, cell: -1, name: '', culture: -1,
      founded: world.year, dead: false, deadYear: null, cause: null,
      pop: 0, tier: 0, K: 0, growth: 0, radius: 2, b,
      devotion: 0.5, love: 0, jaded: 0,
      plague: 0, immune: 0, inspire: 0, blessed: 0, famine: 0,
      prayer: null, apart: 0, capital: false, ark: 0, lastExpand: world.year,
    }, fields);
    s.id = world.settlements.length;
    world.settlements.push(s);
    return s;
  };

  State.newCulture = function (world, fields) {
    const id = world.cultures.length;
    const col = DM.CULTURE_COLORS[State.freeColorSlot(world)];
    const c = fill({
      id, name: '', adj: '', plural: '', color: [col[0], col[1], col[2]], lang: null,
      parent: -1, born: world.year, dead: false,
      age: 0, science: 0, capital: -1, godName: '', epithet: '',
      traits: { piety: 0.5, aggression: 0.5, curiosity: 0.5, seafaring: 0.5 },
      prophet: null, edict: null, edictSince: -9999,
      pop: 0, towns: 0, exhaustion: 0, seen: {}, lastSplit: -9999,
    }, fields);
    c.id = id;
    world.cultures.push(c);
    return c;
  };
  // index into DM.CULTURE_COLORS not used by a living culture (falls back to id modulo palette size)
  State.freeColorSlot = function (world) {
    const P = DM.CULTURE_COLORS, used = new Array(P.length).fill(false);
    for (const c of world.cultures) {
      if (!c || c.dead || !c.color) continue;
      for (let i = 0; i < P.length; i++) if (Math.abs(P[i][0] - c.color[0]) + Math.abs(P[i][1] - c.color[1]) + Math.abs(P[i][2] - c.color[2]) < 1e-3) used[i] = true;
    }
    for (let i = 0; i < P.length; i++) if (!used[i]) return i;
    return world.cultures.length % P.length;
  };

  State.newUnit = function (world, fields) {
    const u = fill({
      id: 0, type: 'settler', culture: -1, from: -1, to: -1, dest: -1,
      path: [], i: 0, t: 0, speed: 5, size: 0, naval: false, sea: false,
      born: world.year, data: {},
    }, fields);
    u.id = world.nextId.unit++;
    world.units.push(u);
    return u;
  };

  State.newDisaster = function (world, fields) {
    const d = fill({
      id: 0, type: '', cell: -1, t: 0, dur: 0, power: 1, ang: 0, natural: false, data: {},
    }, fields);
    d.id = world.nextId.disaster++;
    world.disasters.push(d);
    return d;
  };
})(globalThis.DM = globalThis.DM || {});
