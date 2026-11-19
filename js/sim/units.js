// DEMIURGE — units.js : DM.Units (SPEC-SIM §3.9). Settler parties, caravans, armies and prophets on the move.
//
//   Units.spawn(world, { type, culture, from, to = -1, dest, size, naval = false, data = {} }) → unit | null
//        type 'settler' | 'trader' | 'army' | 'prophet'. Starts at the centre of settlement `from` and walks the
//        cheapest route (Path.find, honouring `naval`) to cell `dest`. null — and nothing changed — when the type is
//        unknown, a cell is bad, there is no route, or TUNE.units.max (300) units are alive, so callers take
//        people out of the source only after a non-null return. Emits unit:spawned.
//        Optional extras: `cell` (start here instead of at `from`'s centre), `maxCost` (route budget, default
//        400), `path` (a ready [start, …, dest] chain of neighbours: no search is run), and `dest` may be left
//        out when `to` names a settlement (its centre is used).
//   Units.step(world, dtYears)             move every unit; arrivals, re-paths and losses
//   Units.kill(world, unit, cause) → bool  removes it and emits unit:died with its last cell (false if it was not alive)
//   Units.near(world, cell, ang) → [unit]  units whose interpolated position is within ang (radians) of the cell's
//                                          direction, nearest first, ties by id (a fresh array)
//   Units.dirOf(world, unit, out) → cell   the unit's current cell (−1 if it has none); writes its interpolated,
//                                          unit-length direction into out[0..2]
//   Units.cellOf(world, unit) → cell       the current cell alone
//   Units.CAUSES                           the causes Units itself reports: 'lost' | 'lava' | 'disbanded'
//                                          (Powers and Society pass their own: 'lightning', 'meteor', 'wave', …)
//
// Movement. A unit is between path[i] and path[i+1] at fraction t; its current cell is the nearer of the two
// (path[i] while t < 0.5) and `sea` says whether that cell is sea. `speed` is in cells per year on open, level
// land. A step a → b takes Path.stepCost(a, b) open-land steps (so slopes, forest, ice and lakes slow a party
// and roads speed it up), at most `slowMax`; a step between two sea cells takes 1 / seaMul (× 2.5 at sea) and
// the step that embarks or lands takes `shoreTime`.
// Each step, in this order: an army or trader whose target settlement `to` is dead, or an army whose culture
// is no longer at war with its target's culture (Society.atWar), dies 'disbanded'; a unit on lava > 0.2 dies
// 'lava'; then it moves. A unit that has not yet entered its next cell and finds it impassable re-paths once
// from where it stands (data.repathed = true); a second block, or no route, kills it ('lost').
// Arrival: settler → joins settlement `to` when that is a live settlement of its culture, else
// Civ.found(dest, culture, size, 'settlers', { devotion, love } of its origin); if no settlement results it
// joins the nearest live settlement of its culture (pop += size), and is 'lost' if there is none.
// Trader, army, prophet → Society.onArrive(world, unit), called while the unit is still in world.units; unless
// that kills it, it is then removed and unit:arrived is emitted.
//
// Entities come from DM.State.newUnit; nothing transient is kept on live units (a removed one is marked _gone).
// No rng is used: movement is fully determined by the world. No allocation per step.
(function (DM) {
  'use strict';
  const Units = (DM.Units = DM.Units || {});
  const TUNE = (DM.TUNE = DM.TUNE || {});

  TUNE.units = {
    max: 300,           // live units; spawn returns null at the cap
    speed: { settler: 5, trader: 7, army: 5, prophet: 4 },   // cells per year on open, level land
    seaMul: 2.5,        // × speed on a step between two sea cells
    shoreTime: 1,       // open-land steps that embarking or landing takes
    slowMax: 3,         // no single land step takes longer than this many open-land steps
    lava: 0.2,          // a unit dies on a cell with more lava than this
    maxCost: 400,       // route budget for spawn and for the re-path
  };

  Units.CAUSES = ['lost', 'lava', 'disbanded'];

  // ---------------------------------------------------------------- helpers
  function finite(x) { return typeof x === 'number' && x === x && x !== Infinity && x !== -Infinity; }
  function validCell(world, c) { return typeof c === 'number' && c >= 0 && c < world.n && c === Math.floor(c); }
  function intOr(x, d) { return finite(x) ? Math.floor(x) : d; }
  function navalOf(v) { return v === true ? true : v === 'coastal' ? 'coastal' : false; }
  function settlementOf(world, id) {
    const list = world.settlements;
    return list && typeof id === 'number' && id >= 0 ? (list[id] || null) : null;
  }

  // The unit's current cell: the nearer end of the segment it is on (−1 when it has no usable path).
  function cellOf(world, u) {
    const path = u.path, len = path ? path.length : 0;
    if (!(len > 0)) return validCell(world, u.dest) ? u.dest : -1;
    let i = u.i | 0;
    if (i < 0) i = 0;
    if (i >= len - 1) { const c = path[len - 1]; return validCell(world, c) ? c : -1; }
    const a = path[i], b = path[i + 1];
    const c = u.t >= 0.5 ? b : a;
    if (validCell(world, c)) return c;
    return validCell(world, a) ? a : validCell(world, b) ? b : -1;
  }

  // Time a → b takes, in open-land steps; Infinity when the step is impassable for this unit.
  function segTime(world, a, b, naval, T) {
    const P = DM.Path;
    const w = P && typeof P.stepCost === 'function' ? P.stepCost(world, a, b, naval)
      : (validCell(world, a) && validCell(world, b) ? 1 : Infinity);
    if (!(w < Infinity)) return Infinity;
    const sea = world.seaLevel, elev = world.elev;
    const sa = elev[a] < sea, sb = elev[b] < sea;
    if (sa && sb) return T.seaMul > 0 ? 1 / T.seaMul : 1;
    if (sa || sb) return T.shoreTime;
    return w < T.slowMax ? w : T.slowMax;
  }

  function remove(world, u) {
    const list = world.units, k = list ? list.indexOf(u) : -1;
    if (k < 0) return false;
    list.splice(k, 1);
    u._gone = true;
    return true;
  }

  // nearest live settlement of a culture to a cell (ties: lower id), or null
  function nearestOwn(world, cell, culture) {
    const C = DM.Civ, geo = world.geo;
    const list = C && typeof C.live === 'function' ? C.live(world) : world.settlements;
    if (!list) return null;
    let best = null, bd = Infinity;
    for (let k = 0; k < list.length; k++) {
      const s = list[k];
      if (!s || s.dead || s.culture !== culture || !validCell(world, s.cell)) continue;
      const d = geo.angle(cell, s.cell);
      if (d < bd || (d === bd && best && s.id < best.id)) { bd = d; best = s; }
    }
    return best;
  }

  function join(s, u) {
    const add = finite(u.size) && u.size > 0 ? u.size : 0;
    s.pop = (finite(s.pop) && s.pop > 0 ? s.pop : 0) + add;
  }

  // A settler party reaches its destination. Returns false when its people found no home.
  function settle(world, u, cell) {
    const target = u.to >= 0 ? settlementOf(world, u.to) : null;
    if (target && !target.dead && target.culture === u.culture) { join(target, u); return true; }
    const C = DM.Civ;
    let s = null;
    if (C && typeof C.found === 'function') {
      const origin = settlementOf(world, u.from);
      const opts = origin && finite(origin.devotion) && finite(origin.love)
        ? { devotion: origin.devotion, love: origin.love } : undefined;
      s = C.found(world, cell, u.culture, u.size, 'settlers', opts) || null;
    }
    if (s) return true;
    const home = nearestOwn(world, cell, u.culture);
    if (!home) return false;
    join(home, u);
    return true;
  }

  function arrive(world, u) {
    const path = u.path, cell = path[path.length - 1];
    u.i = path.length - 1;
    u.t = 0;
    u.sea = world.elev[cell] < world.seaLevel;
    if (u.type === 'settler') {
      if (!settle(world, u, cell)) { Units.kill(world, u, 'lost'); return; }
    } else {
      const So = DM.Society;
      if (So && typeof So.onArrive === 'function') So.onArrive(world, u);
    }
    if (u._gone) return;                                       // whoever received it killed it (a lost battle)
    if (!remove(world, u)) return;
    DM.Bus.emit('unit:arrived', { id: u.id, type: u.type, culture: u.culture, cell });
  }

  // The one allowed re-path, from the cell the unit stands on (path[i]: it has not entered the next one yet).
  // A unit caught part-way along the blocked step turns round on the spot, so its position never jumps.
  const findOpts = { naval: false, maxCost: 400 };
  function repath(world, u, T) {
    const d = u.data && typeof u.data === 'object' ? u.data : (u.data = {});
    if (d.repathed) return false;
    const P = DM.Path;
    if (!P || typeof P.find !== 'function' || !validCell(world, u.dest)) return false;
    const here = u.path[u.i], ahead = u.path[u.i + 1];
    findOpts.naval = u.naval;
    findOpts.maxCost = T.maxCost;
    const p = P.find(world, here, u.dest, findOpts);
    if (!p) return false;
    d.repathed = true;
    if (u.t > 0 && validCell(world, ahead)) { p.unshift(ahead); u.t = 1 - u.t; } else u.t = 0;
    u.path = p;
    u.i = 0;
    return true;
  }

  function advance(world, u, dt) {
    const T = TUNE.units;
    let path = u.path;
    if (!Array.isArray(path) || path.length === 0 || !validCell(world, path[path.length - 1])) {
      Units.kill(world, u, 'lost');
      return;
    }
    // orders that no longer stand
    if ((u.type === 'army' || u.type === 'trader') && u.to >= 0) {
      const target = settlementOf(world, u.to);
      if (!target || target.dead) { Units.kill(world, u, 'disbanded'); return; }
      if (u.type === 'army') {
        const So = DM.Society;
        if (So && typeof So.atWar === 'function' && !So.atWar(world, u.culture, target.culture)) {
          Units.kill(world, u, 'disbanded');
          return;
        }
      }
    }
    if (!(u.i >= 0)) u.i = 0;
    if (!(u.t >= 0 && u.t < 1)) u.t = 0;
    const lava = world.lava;
    let cur = cellOf(world, u);
    if (cur >= 0 && lava[cur] > T.lava) { Units.kill(world, u, 'lava'); return; }

    let speed = u.speed;
    if (!(speed > 0) || !finite(speed)) speed = T.speed[u.type] > 0 ? T.speed[u.type] : 5;
    let left = dt * speed;                                     // budget, in open-land steps
    for (;;) {
      const last = path.length - 1;
      if (u.i >= last) { arrive(world, u); return; }
      const a = path[u.i], b = path[u.i + 1];
      let w = segTime(world, a, b, u.naval, T);
      if (!(w < Infinity)) {
        if (u.t < 0.5) {                                       // the cell ahead has become impassable
          if (!repath(world, u, T)) { Units.kill(world, u, 'lost'); return; }
          path = u.path;
          continue;
        }
        w = 1;                                                 // already inside it: finish the step
      }
      const need = (1 - u.t) * w;
      if (left >= need) {
        left -= need;
        u.i++;
        u.t = 0;
        if (lava[b] > T.lava) { u.sea = world.elev[b] < world.seaLevel; Units.kill(world, u, 'lava'); return; }
      } else {
        u.t += left / w;
        if (!(u.t < 1)) u.t = 0.999999;
        break;
      }
    }
    cur = cellOf(world, u);
    u.sea = cur >= 0 && world.elev[cur] < world.seaLevel;
  }

  // is `path` a chain of neighbouring cells from `start` to `dest`?
  function chainOk(world, path, start, dest) {
    const len = path.length, nbr = world.geo.nbr;
    if (!(len > 0) || path[0] !== start || path[len - 1] !== dest) return false;
    for (let k = 1; k < len; k++) {
      const a = path[k - 1], b = path[k];
      if (!validCell(world, a) || !validCell(world, b)) return false;
      const o = 6 * a;
      if (nbr[o] !== b && nbr[o + 1] !== b && nbr[o + 2] !== b && nbr[o + 3] !== b && nbr[o + 4] !== b && nbr[o + 5] !== b) return false;
    }
    return true;
  }

  // ---------------------------------------------------------------- public API
  Units.spawn = function (world, o) {
    if (!world || !o || !Array.isArray(world.units) || !world.geo) return null;
    const T = TUNE.units, type = o.type;
    const speed = typeof type === 'string' && Object.prototype.hasOwnProperty.call(T.speed, type) ? T.speed[type] : 0;
    if (!(speed > 0)) return null;
    if (world.units.length >= T.max) return null;
    const NU = DM.State && DM.State.newUnit;
    if (typeof NU !== 'function') return null;

    const from = intOr(o.from, -1), to = intOr(o.to, -1);
    const origin = settlementOf(world, from), target = settlementOf(world, to);
    const start = validCell(world, o.cell) ? o.cell : origin ? origin.cell : -1;
    const dest = validCell(world, o.dest) ? o.dest : (o.dest === undefined || o.dest === null) && target ? target.cell : -1;
    if (!validCell(world, start) || !validCell(world, dest)) return null;
    const naval = navalOf(o.naval);

    let path = null;
    if (Array.isArray(o.path)) {
      if (!chainOk(world, o.path, start, dest)) return null;
      path = o.path.slice();
    } else {
      const P = DM.Path;
      if (!P || typeof P.find !== 'function') return null;
      path = P.find(world, start, dest, { naval, maxCost: finite(o.maxCost) ? o.maxCost : T.maxCost });
    }
    if (!path || !path.length) return null;

    const unit = NU(world, {
      type, culture: intOr(o.culture, -1), from, to, dest,
      path, i: 0, t: 0, speed,
      size: finite(o.size) && o.size > 0 ? o.size : 0,
      naval, sea: world.elev[start] < world.seaLevel,
      born: world.year,
      data: o.data && typeof o.data === 'object' && !Array.isArray(o.data) ? o.data : {},
    });
    DM.Bus.emit('unit:spawned', { id: unit.id, type: unit.type, culture: unit.culture, cell: start });
    return unit;
  };

  let work = [];                          // the units being stepped (handlers may add or remove units meanwhile)
  let stepping = false;
  Units.step = function (world, dtYears) {
    if (!world || !Array.isArray(world.units) || !world.geo) return;
    const dt = +dtYears, list = world.units, m = list.length;
    if (!(dt > 0) || !finite(dt) || m === 0) return;
    const mine = stepping ? [] : work;                         // a nested call (from a handler) gets its own list
    const outer = stepping;
    stepping = true;
    try {
      for (let k = 0; k < m; k++) mine[k] = list[k];
      for (let k = 0; k < m; k++) {
        const u = mine[k];
        mine[k] = null;
        if (!u || u._gone) continue;
        advance(world, u, dt);
      }
    } finally {
      stepping = outer;
      if (!outer && work.length > 1024) work = [];
    }
  };

  Units.kill = function (world, unit, cause) {
    if (!world || !unit || unit._gone || !world.geo) return false;
    const cell = cellOf(world, unit);
    if (!remove(world, unit)) return false;
    DM.Bus.emit('unit:died', {
      id: unit.id, type: unit.type, culture: unit.culture, cell,
      cause: typeof cause === 'string' && cause ? cause : 'lost',
    });
    return true;
  };

  Units.cellOf = function (world, unit) {
    return world && unit && world.geo ? cellOf(world, unit) : -1;
  };

  Units.dirOf = function (world, unit, out) {
    if (!world || !unit || !world.geo) {
      if (out) { out[0] = 0; out[1] = 1; out[2] = 0; }
      return -1;
    }
    const pos = world.geo.pos, path = unit.path, len = path ? path.length : 0;
    let i = unit.i | 0, a = -1, b = -1, t = 0;
    if (len > 0) {
      if (i < 0) i = 0;
      if (i >= len - 1) a = path[len - 1];
      else { a = path[i]; b = path[i + 1]; t = unit.t; }
    } else a = unit.dest;
    if (!validCell(world, a)) { a = validCell(world, b) ? b : -1; b = -1; }
    if (a < 0) {
      if (out) { out[0] = 0; out[1] = 1; out[2] = 0; }
      return -1;
    }
    if (!validCell(world, b) || !(t > 0)) {
      if (out) { out[0] = pos[3 * a]; out[1] = pos[3 * a + 1]; out[2] = pos[3 * a + 2]; }
      return a;
    }
    if (t > 1) t = 1;
    if (out) {
      const oa = 3 * a, ob = 3 * b;
      const x = pos[oa] + (pos[ob] - pos[oa]) * t;
      const y = pos[oa + 1] + (pos[ob + 1] - pos[oa + 1]) * t;
      const z = pos[oa + 2] + (pos[ob + 2] - pos[oa + 2]) * t;
      const l = Math.sqrt(x * x + y * y + z * z);
      if (l > 1e-12) { out[0] = x / l; out[1] = y / l; out[2] = z / l; }
      else { out[0] = pos[oa]; out[1] = pos[oa + 1]; out[2] = pos[oa + 2]; }
    }
    return t >= 0.5 ? b : a;
  };

  const nearDir = [0, 1, 0];
  let nearD = new Float64Array(64);
  Units.near = function (world, cell, ang) {
    const out = [];
    if (!world || !world.geo || !Array.isArray(world.units) || !validCell(world, cell) || !(ang >= 0)) return out;
    const list = world.units, pos = world.geo.pos;
    const cx = pos[3 * cell], cy = pos[3 * cell + 1], cz = pos[3 * cell + 2];
    if (nearD.length < list.length) nearD = new Float64Array(list.length * 2);
    for (let k = 0; k < list.length; k++) {
      const u = list[k];
      if (!u || u._gone || Units.dirOf(world, u, nearDir) < 0) continue;
      const dx = nearDir[0] - cx, dy = nearDir[1] - cy, dz = nearDir[2] - cz;
      const h = Math.sqrt(dx * dx + dy * dy + dz * dz) * 0.5;
      const d = 2 * Math.asin(h < 1 ? h : 1);
      if (!(d <= ang)) continue;
      let j = out.length;                                      // insertion sort by (angle, id)
      while (j > 0 && (nearD[j - 1] > d || (nearD[j - 1] === d && out[j - 1].id > u.id))) {
        out[j] = out[j - 1];
        nearD[j] = nearD[j - 1];
        j--;
      }
      out[j] = u;
      nearD[j] = d;
    }
    return out;
  };
})(globalThis.DM = globalThis.DM || {});
