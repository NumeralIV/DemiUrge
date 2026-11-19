// DEMIURGE — tests/units.test.js : DM.Units (SPEC-SIM §3.9, BUILD.md A6).
//
//   node tests/units.test.js                  run the tests
//   UNITS_REPORT=1 node tests/units.test.js   also print the measured numbers (journeys, timings)
//   UNITS_MAPS=1 node tests/units.test.js     also write shots/units-voyages.png (every party's position at every
//                                             tick of its journey, on real terrain when terrain.js exists)
//
// Only the foundation, schema.js, path.js and units.js are loaded on purpose; Civ and Society are stubbed in
// each test (T.stub), every other sim module is stubbed away. Expected travel times are worked out here from
// the documented rule (5 / 7 / 5 / 4 cells per year on open land, × 2.5 at sea, terrain by Path's step cost).
'use strict';
const T = require('../tools/t.js');
const DM = require('../tools/load.js')({ only: ['path', 'units'] });   // strict: a broken path.js / units.js fails here
const H = require('./helpers/world.js');                                // (its loader pulls in whatever else exists ...)
const REAL = { Terrain: DM.Terrain };                                   // ... kept only for the optional map
for (const k of ['Names', 'Terrain', 'Climate', 'Hydro', 'Life', 'Civ', 'Society', 'Powers', 'Chronicle', 'Sim']) T.stub(DM, k, undefined);
T.watchEvents();

const Units = DM.Units, Path = DM.Path, State = DM.State;
const REPORT = !!process.env.UNITS_REPORT || !!process.env.UNITS_MAPS;
const say = (...a) => { if (REPORT) console.log('    ·', ...a); };
const now = () => Number(process.hrtime.bigint()) / 1e6;
const TICK = 0.25;
const isSea = (w, c) => w.elev[c] < w.seaLevel;

// ------------------------------------------------------------------ worlds and people
// level land everywhere at +0.5, nothing on it: every step costs exactly 1
function flat(L) {
  const w = State.create({ seed: 1, L: L === undefined ? 4 : L });
  w.elev.fill(0.5); w.seaLevel = 0; w.heat = 0; w.phase = 'history';
  return w;
}
// level land with a sea channel: cells with |x| < half are sea of the given depth
function channel(depth, half, L) {
  const w = flat(L), g = w.geo;
  for (let c = 0; c < w.n; c++) if (Math.abs(g.pos[3 * c]) < half) w.elev[c] = -depth;
  return w;
}
function hops(w, from) {
  const g = w.geo, d = new Int32Array(w.n).fill(-1), q = [from];
  d[from] = 0;
  for (let h = 0; h < q.length; h++) { const c = q[h]; for (let k = 0; k < g.nbrN[c]; k++) { const b = g.nbr[6 * c + k]; if (d[b] < 0) { d[b] = d[c] + 1; q.push(b); } } }
  return d;
}
function cellAtHops(w, from, k) { const d = hops(w, from); for (let c = 0; c < w.n; c++) if (d[c] === k) return c; return -1; }
// a cell near the equator on the given side of the x = 0 plane
function side(w, x) {
  const g = w.geo; let best = -1, bd = Infinity;
  for (let c = 0; c < w.n; c++) { const d = Math.abs(g.pos[3 * c] - x) + Math.abs(g.pos[3 * c + 1]) + (g.pos[3 * c + 2] < 0 ? 1 : 0); if (d < bd) { bd = d; best = c; } }
  return best;
}
function culture(w, name) { return State.newCulture(w, { name: name || 'Umar', adj: 'Umaran', plural: 'the Umari' }); }
function town(w, cell, cu, fields) { return State.newSettlement(w, Object.assign({ cell, culture: cu.id, name: 'Town' + w.settlements.length, pop: 500 }, fields || {})); }
function isNbr(w, a, b) { const g = w.geo; for (let k = 0; k < g.nbrN[a]; k++) if (g.nbr[6 * a + k] === b) return true; return false; }
function isChain(w, p) { for (let i = 1; i < p.length; i++) if (!isNbr(w, p[i - 1], p[i])) return false; return p.length > 0; }
const events = (list, type) => list.filter(e => e.type === type).map(e => e.payload);
const prog = u => u.i + u.t;

// the documented pace, written out again: time for one step in "open-land steps"
function stepTime(w, a, b, naval) {
  const sa = isSea(w, a), sb = isSea(w, b);
  if (sa && sb) return 1 / 2.5;
  if (sa || sb) return 1;
  return Math.min(3, Path.stepCost(w, a, b, naval));
}
function journeyYears(w, path, naval, speed) { let s = 0; for (let i = 1; i < path.length; i++) s += stepTime(w, path[i - 1], path[i], naval); return s / speed; }
// step until the unit is gone; returns the number of ticks it took (or -1)
function runOut(w, u, maxTicks, each) {
  for (let k = 1; k <= (maxTicks || 4000); k++) {
    Units.step(w, TICK);
    if (each) each(k);
    if (w.units.indexOf(u) < 0) return k;
  }
  return -1;
}
const civStub = (w, log) => ({
  found: (world, cell, cultureId, pop, by, opts) => {
    if (log) log.push({ world, cell, cultureId, pop, by, opts });
    return State.newSettlement(world, { cell, culture: cultureId, pop, name: 'New' });
  },
  live: (world) => world.settlements.filter(s => !s.dead),
});

// ------------------------------------------------------------------ spawn
T.test('TUNE.units carries the documented numbers', () => {
  const U = DM.TUNE.units;
  T.eq(U.max, 300);
  T.eq(U.speed, { settler: 5, trader: 7, army: 5, prophet: 4 });
  T.eq(U.seaMul, 2.5);
  T.eq(U.lava, 0.2);
});

T.test('spawn: the unit comes from State.newUnit, with every field of the spec and nothing else', () => {
  const w = flat(), cu = culture(w), a = town(w, 100, cu), dest = cellAtHops(w, 100, 9);
  w.year = 312.5;
  const keys = Object.keys(State.newUnit(State.create({ seed: 1, L: 2 }), {})).sort();
  let made = 0; const realNew = State.newUnit;
  T.stub(State, 'newUnit', function (world, fields) { made++; return realNew(world, fields); });
  const before = w.nextId.unit;
  const data = { cargo: 'salt' };
  const ev = H.capture(() => {
    const u = Units.spawn(w, { type: 'settler', culture: cu.id, from: a.id, dest, size: 42, data });
    T.ok(u, 'a unit');
    T.eq(made, 1, 'created through State.newUnit');
    T.eq(Object.keys(u).sort(), keys, 'exactly the factory’s fields');
    T.eq(u.id, before); T.eq(w.nextId.unit, before + 1);
    T.eq(w.units.length, 1); T.ok(w.units[0] === u, 'pushed into world.units');
    T.eq([u.type, u.culture, u.from, u.to, u.dest, u.i, u.t, u.speed, u.size, u.naval, u.sea, u.born],
      ['settler', cu.id, a.id, -1, dest, 0, 0, 5, 42, false, false, 312.5]);
    T.eq(u.data, { cargo: 'salt' }); T.ok(u.data !== data, 'data is the unit’s own object');
    T.ok(Array.isArray(u.path) && u.path[0] === 100 && u.path[u.path.length - 1] === dest && isChain(w, u.path), 'a chain of neighbours from the origin to dest');
    T.eq(u.path.length, 10, 'the cheapest route: 9 steps');
    T.eq(JSON.parse(JSON.stringify(u)), u, 'plain JSON data');
  });
  T.eq(ev.length, 1);
  T.eq(ev[0], { type: 'unit:spawned', payload: { id: before, type: 'settler', culture: cu.id, cell: 100 } });
  // and the world still saves and loads with units on the road
  Units.step(w, TICK);
  const back = State.deserialize(JSON.parse(JSON.stringify(State.serialize(w))));
  T.eq(back.units, JSON.parse(JSON.stringify(w.units)), 'units survive a save');
});

T.test('spawn: speed by type; naval and to are carried; dest defaults to the centre of `to`', () => {
  const w = channel(0.4, 0.2), cu = culture(w), a = town(w, side(w, -0.6), cu), b = town(w, side(w, 0.6), cu);
  for (const [type, speed] of [['settler', 5], ['trader', 7], ['army', 5], ['prophet', 4]]) {
    const u = Units.spawn(w, { type, culture: cu.id, from: a.id, to: b.id, dest: b.cell, size: 10, naval: true });
    T.eq([u.type, u.speed, u.to, u.dest, u.naval], [type, speed, b.id, b.cell, true]);
  }
  T.eq(Units.spawn(w, { type: 'trader', culture: cu.id, from: a.id, to: b.id, size: 1, naval: 'coastal' }).dest, b.cell, 'dest from to');
  T.eq(Units.spawn(w, { type: 'trader', culture: cu.id, from: a.id, to: b.id, size: 1, naval: 'coastal' }).naval, 'coastal');
  T.eq(Units.spawn(w, { type: 'trader', culture: cu.id, from: a.id, to: b.id, size: 1, naval: 'yes' }), null, 'any other naval value means on foot: no way across');
  const s = Units.spawn(w, { type: 'settler', culture: cu.id, from: a.id, dest: a.cell, size: 5 });
  T.eq(s.path, [a.cell], 'a party may be sent to where it stands');
  T.eq(Units.spawn(w, { type: 'settler', culture: cu.id, from: a.id, dest: b.cell, size: -3, naval: true }).size, 0, 'size is never negative');
  T.eq(Units.spawn(w, { type: 'settler', culture: cu.id, from: a.id, dest: b.cell, size: NaN, naval: true }).size, 0, 'nor NaN');
});

T.test('spawn returns null — and changes nothing — without a path, at the cap, or for bad input', () => {
  const w = channel(2, 0.2), cu = culture(w), a = town(w, side(w, -0.6), cu), b = town(w, side(w, 0.6), cu);
  const dead = town(w, side(w, -0.8), cu); dead.dead = true;
  const quiet = (label, o, world) => {
    const n0 = w.units.length, id0 = w.nextId.unit;
    const ev = H.capture(() => { T.eq(Units.spawn(world === undefined ? w : world, o), null, label); });
    T.eq([w.units.length, w.nextId.unit, ev.length], [n0, id0, 0], label + ': nothing changed');
  };
  quiet('no path on foot across the sea', { type: 'settler', culture: cu.id, from: a.id, dest: b.cell, size: 30 });
  quiet('no path for canoes across deep water', { type: 'settler', culture: cu.id, from: a.id, dest: b.cell, size: 30, naval: 'coastal' });
  quiet('route over budget', { type: 'settler', culture: cu.id, from: a.id, dest: b.cell, size: 30, naval: true, maxCost: 5 });
  T.ok(Units.spawn(w, { type: 'settler', culture: cu.id, from: a.id, dest: b.cell, size: 30, naval: true }), 'ships cross');
  quiet('unknown type', { type: 'dragon', culture: cu.id, from: a.id, dest: a.cell + 1, size: 1 });
  quiet('no type', { culture: cu.id, from: a.id, dest: a.cell, size: 1 });
  quiet('type from Object.prototype', { type: 'constructor', culture: cu.id, from: a.id, dest: a.cell, size: 1 });
  quiet('no origin', { type: 'settler', culture: cu.id, from: -1, dest: a.cell, size: 1 });
  quiet('origin out of range', { type: 'settler', culture: cu.id, from: 99, dest: a.cell, size: 1 });
  quiet('bad dest', { type: 'settler', culture: cu.id, from: a.id, dest: -5, size: 1 });
  quiet('dest out of range', { type: 'settler', culture: cu.id, from: a.id, dest: w.n, size: 1 });
  quiet('fractional dest', { type: 'settler', culture: cu.id, from: a.id, dest: 10.5, size: 1 });
  quiet('no dest and no to', { type: 'settler', culture: cu.id, from: a.id, size: 1 });
  quiet('a path that is not a chain', { type: 'settler', culture: cu.id, from: a.id, dest: b.cell, size: 1, path: [a.cell, b.cell] });
  quiet('no options', undefined);
  quiet('no world', { type: 'settler', from: 0, dest: 1 }, null);
  T.ok(Units.spawn(w, { type: 'settler', culture: cu.id, from: dead.id, dest: dead.cell + 1, size: 1 }) || true, 'a dead origin never throws');
  // the cap
  w.units.length = 0;
  const near = w.geo.nbr[6 * a.cell];
  for (let k = 0; k < 300; k++) T.ok(Units.spawn(w, { type: 'trader', culture: cu.id, from: a.id, dest: near, size: 1 }), 'unit ' + k);
  T.eq(w.units.length, 300);
  quiet('at the cap of 300', { type: 'trader', culture: cu.id, from: a.id, dest: near, size: 1 });
  Units.kill(w, w.units[7], 'lost');
  T.ok(Units.spawn(w, { type: 'trader', culture: cu.id, from: a.id, dest: near, size: 1 }), 'room again after a loss');
  T.stub(DM.TUNE.units, 'max', 3);
  quiet('TUNE.units.max is read at call time', { type: 'trader', culture: cu.id, from: a.id, dest: near, size: 1 });
});

T.test('spawn extras: cell (start anywhere), path (no search), maxCost', () => {
  const w = flat(), cu = culture(w), a = town(w, 100, cu), dest = cellAtHops(w, 300, 6);
  const u = Units.spawn(w, { type: 'prophet', culture: cu.id, from: -1, cell: 300, dest, size: 1 });
  T.eq([u.path[0], u.from, u.path.length], [300, -1, 7], 'starts at `cell`');
  const route = Path.find(w, a.cell, dest);
  let searched = 0; const realFind = Path.find;
  T.stub(Path, 'find', function () { searched++; return realFind.apply(Path, arguments); });
  const v = Units.spawn(w, { type: 'army', culture: cu.id, from: a.id, dest, size: 9, path: route });
  T.eq(searched, 0, 'a given path is used as it is');
  T.eq(v.path, route); T.ok(v.path !== route, 'copied');
  T.eq(Units.spawn(w, { type: 'army', culture: cu.id, from: a.id, dest, size: 9, path: route.slice(1) }), null, 'a path that does not start at the origin');
  T.eq(Units.spawn(w, { type: 'army', culture: cu.id, from: a.id, dest, size: 9, maxCost: route.length - 1.5 }), null, 'budget below the cost');
  T.ok(Units.spawn(w, { type: 'army', culture: cu.id, from: a.id, dest, size: 9, maxCost: route.length - 1 }), 'budget equal to the cost');
  T.ok(searched >= 2);
});

// ------------------------------------------------------------------ movement
T.test('pace on open land: settler 5, trader 7, army 5, prophet 4 cells per year, whatever the tick', () => {
  for (const [type, speed] of [['settler', 5], ['trader', 7], ['army', 5], ['prophet', 4]]) {
    const w = flat(), cu = culture(w), a = town(w, 100, cu), far = cellAtHops(w, 100, 20);
    const b = town(w, far, cu);
    T.stub(DM, 'Civ', civStub(w));
    T.stub(DM, 'Society', { onArrive: () => {}, atWar: () => true });
    const u = Units.spawn(w, { type, culture: cu.id, from: a.id, to: type === 'settler' ? -1 : b.id, dest: far, size: 20 });
    T.eq(u.path.length, 21);
    for (let k = 1; k <= 8; k++) {
      Units.step(w, TICK);
      T.near(prog(u), k * TICK * speed, 1e-9, type + ' after ' + k + ' ticks');
      T.ok(u.i >= 0 && u.i < u.path.length - 1 && u.t >= 0 && u.t < 1, 'i, t in range');
      T.eq(u.sea, false);
    }
    const ticks = 8 + runOut(w, u);
    T.eq(ticks, Math.ceil(20 / (speed * TICK)), type + ': 20 cells take ' + (20 / speed) + ' years');
    // the same journey in uneven little steps ends at the same time
    const v = Units.spawn(w, { type, culture: cu.id, from: a.id, to: type === 'settler' ? -1 : b.id, dest: cellAtHops(w, 100, 12), size: 20 });
    let t = 0;
    const dts = [0.01, 0.07, 0.3, 0.002, 0.11];
    for (let k = 0; w.units.indexOf(v) >= 0 && k < 10000; k++) {
      const dt = dts[k % dts.length];
      Units.step(w, dt); t += dt;
      if (w.units.indexOf(v) >= 0) T.near(prog(v), t * speed, 1e-7, 'progress with uneven steps');
    }
    T.ok(t >= 12 / speed - 1e-9 && t < 12 / speed + 0.3 + 1e-9, type + ' arrived after ' + t.toFixed(3) + ' y (12 cells)');
  }
});

T.test('terrain sets the pace: roads help, forest, slopes, ice and lakes slow, never beyond slowMax', () => {
  const pace = (prep, expectCost) => {
    const w = flat(), cu = culture(w), a = town(w, 100, cu), far = cellAtHops(w, 100, 16);
    prep(w);
    T.stub(DM, 'Civ', civStub(w));
    const u = Units.spawn(w, { type: 'settler', culture: cu.id, from: a.id, dest: far, size: 20 });
    const years = journeyYears(w, u.path, false, 5);
    if (expectCost !== undefined) T.near(years, (u.path.length - 1) * expectCost / 5, 1e-6, 'expected step time ' + expectCost);
    const ticks = runOut(w, u);
    T.ok(Math.abs(ticks - years / TICK) <= 1.0001 && ticks >= years / TICK - 1e-6, 'arrives after ' + years.toFixed(2) + ' y: took ' + ticks + ' ticks');
    return years / (u.path.length - 1) * 5;
  };
  T.near(pace(() => {}, 1), 1, 1e-9, 'open land');
  T.near(pace(w => w.road.fill(1), 0.75), 0.75, 1e-9, 'road 1: a third faster');
  T.near(pace(w => w.road.fill(2), 0.5), 0.5, 1e-9, 'road 2: twice as fast');
  T.near(pace(w => w.road.fill(3), 0.3), 0.3, 1e-9, 'road 3');
  T.near(pace(w => w.veg.fill(0.8), 1.4), 1.4, 1e-6, 'dense forest');
  T.near(pace(w => w.ice.fill(1), 3), 3, 1e-9, 'ice: cost 4, capped at slowMax 3');
  T.near(pace(w => { w.elev.fill(0.3); w.water.fill(0.2); }, 3), 3, 1e-6, 'wading through lakes');
  // a real slope: a ramp rising 0.1 per hop away from the start
  const ramp = pace(w => { const d = hops(w, 100); for (let c = 0; c < w.n; c++) w.elev[c] = 0.5 + 0.1 * d[c]; });
  T.near(ramp, 1.6, 1e-4, 'climbing 0.1 per cell: 1 + 6 × 0.1');
  T.stub(DM.TUNE.units, 'slowMax', 1.2);
  T.near(pace(w => w.veg.fill(0.8), 1.2), 1.2, 1e-9, 'slowMax is read at call time');
});

T.test('at sea: 2.5 times faster, `sea` is true exactly while the current cell is sea', () => {
  for (const naval of ['coastal', true]) {
    const w = channel(0.4, 0.3), cu = culture(w), a = town(w, side(w, -0.75), cu), dest = side(w, 0.75);
    T.stub(DM, 'Civ', civStub(w));
    const u = Units.spawn(w, { type: 'settler', culture: cu.id, from: a.id, dest, size: 20, naval });
    const path = u.path.slice(), seaCells = path.filter(c => isSea(w, c)).length;
    T.ok(seaCells >= 6, 'the route crosses the channel: ' + seaCells + ' sea cells');
    const years = journeyYears(w, path, naval, 5), out = [0, 0, 0];
    let flips = 0, was = u.sea, seaTicks = 0, lastP = 0, seaPace = 0, landPace = 0;
    T.eq(was, false);
    const ticks = runOut(w, u, 4000, () => {
      if (w.units.indexOf(u) < 0) return;
      const cur = Units.dirOf(w, u, out);
      T.eq(cur, u.t >= 0.5 ? u.path[u.i + 1] : u.path[u.i], 'current cell is the nearer end');
      T.eq(u.sea, isSea(w, cur), 'sea flag matches the current cell');
      if (u.sea !== was) { flips++; was = u.sea; }
      if (u.sea) seaTicks++;
      const p = prog(u), a2 = u.path[u.i], b2 = u.path[Math.min(u.i + 1, u.path.length - 1)];
      if (isSea(w, a2) && isSea(w, b2) && isSea(w, u.path[Math.max(0, u.i - 3)])) seaPace = Math.max(seaPace, p - lastP);
      if (u.i < 3) landPace = Math.max(landPace, p - lastP);
      lastP = p;
    });
    T.eq(flips, 2, 'afloat once, ashore once');
    T.ok(seaTicks >= 1, 'seen as a ship');
    T.near(landPace, 1.25, 1e-9, '1.25 cells per tick on land');
    T.near(seaPace, 3.125, 1e-6, '3.125 cells per tick at sea (× 2.5)');
    T.ok(Math.abs(ticks - years / TICK) <= 1.0001, naval + ': ' + years.toFixed(3) + ' y by the rule, took ' + ticks + ' ticks');
    const land = path.length - 1 - (seaCells + 1);
    T.near(years, (land + 2 * 1 + (seaCells - 1) / 2.5) / 5, 1e-9, 'land steps + embark + landing + sea steps / 2.5');
    say('channel crossing', String(naval).padEnd(7), (path.length - 1) + ' steps (' + seaCells + ' sea cells) in', years.toFixed(2), 'years =', ticks, 'ticks');
  }
  // a party launched from a drowned cell starts as a ship
  const w = channel(0.4, 0.3), cu = culture(w);
  let wet = -1; for (let c = 0; c < w.n && wet < 0; c++) if (isSea(w, c) && w.geo.pos[3 * c] < -0.2) wet = c;
  const s = town(w, wet, cu);
  const u = Units.spawn(w, { type: 'settler', culture: cu.id, from: s.id, dest: side(w, -0.75), size: 20 });
  T.ok(u, 'people can leave a drowned town on foot');
  T.eq(u.sea, true);
  Units.step(w, TICK); Units.step(w, TICK);
  T.eq(u.sea, false, 'ashore');
});

// ------------------------------------------------------------------ arrival
T.test('a settler arrives: Civ.found gets the site, the people and its origin’s devotion and love', () => {
  const w = flat(), cu = culture(w), a = town(w, 100, cu, { devotion: 0.83, love: -0.41 }), dest = cellAtHops(w, 100, 5);
  const log = [], order = [];
  T.stub(DM, 'Civ', civStub(w, log));
  const off = DM.Bus.on('settlement:founded', () => order.push('founded'));
  const u = Units.spawn(w, { type: 'settler', culture: cu.id, from: a.id, dest, size: 37 });
  const ev = H.capture(() => { T.eq(runOut(w, u), 4, '5 cells at 1.25 per tick'); });
  off();
  T.eq(log.length, 1, 'Civ.found called once');
  T.ok(log[0].world === w);
  T.eq([log[0].cell, log[0].cultureId, log[0].pop, log[0].by], [dest, cu.id, 37, 'settlers']);
  T.eq(log[0].opts, { devotion: 0.83, love: -0.41 });
  T.eq(ev.map(e => e.type), ['unit:arrived'], 'one event');
  T.eq(ev[0].payload, { id: u.id, type: 'settler', culture: cu.id, cell: dest });
  T.eq(w.units.length, 0, 'gone from world.units');
  T.eq(a.pop, 500, 'the origin is not touched by Units');
  T.eq(w.settlements.length, 2);
  // the origin died on the way: its last devotion and love still travel with the party
  const v = Units.spawn(w, { type: 'settler', culture: cu.id, from: a.id, dest: cellAtHops(w, 100, 3), size: 12 });
  a.dead = true;
  runOut(w, v);
  T.eq(log[1].opts, { devotion: 0.83, love: -0.41 });
  // a party with no origin settlement: Civ's own defaults apply
  const x = Units.spawn(w, { type: 'settler', culture: cu.id, from: -1, cell: 100, dest: cellAtHops(w, 100, 2), size: 12 });
  runOut(w, x);
  T.eq(log[2].opts, undefined);
});

T.test('a settler whose site is no longer valid joins the nearest live settlement of its culture', () => {
  const build = () => {
    const w = flat(), cu = culture(w, 'Umar'), other = culture(w, 'Vorn'), d0 = hops(w, 100);
    const at = k => { for (let c = 0; c < w.n; c++) if (d0[c] === k) return c; };
    const origin = town(w, 100, cu);                     // 8 hops from the site
    const dest = at(8), dd = hops(w, dest);
    const pick = k => { for (let c = w.n - 1; c >= 0; c--) if (dd[c] === k) return c; };
    const farOwn = town(w, pick(14), cu, { pop: 100 });
    const nearOwn = town(w, pick(5), cu, { pop: 200 });
    const foreign = town(w, pick(2), other, { pop: 300 });
    const ruin = town(w, pick(1), cu, { pop: 0 }); ruin.dead = true;
    return { w, cu, other, origin, dest, farOwn, nearOwn, foreign, ruin };
  };
  for (const mode of ['Civ.live', 'no Civ.live', 'no Civ at all']) {
    const S = build(), w = S.w, calls = [];
    if (mode === 'Civ.live') T.stub(DM, 'Civ', { found: (...a) => { calls.push(a); return null; }, live: (world) => world.settlements.filter(s => !s.dead) });
    else if (mode === 'no Civ.live') T.stub(DM, 'Civ', { found: (...a) => { calls.push(a); return null; } });
    else T.stub(DM, 'Civ', undefined);
    const u = Units.spawn(w, { type: 'settler', culture: S.cu.id, from: S.origin.id, dest: S.dest, size: 25 });
    const ev = H.capture(() => { runOut(w, u); });
    if (mode !== 'no Civ at all') T.eq(calls.length, 1, mode + ': the site was tried first');
    T.eq([S.nearOwn.pop, S.farOwn.pop, S.foreign.pop, S.ruin.pop, S.origin.pop], [225, 100, 300, 0, 500], mode + ': the nearest live settlement of its own culture takes the people');
    T.eq(ev.map(e => e.type), ['unit:arrived'], mode);
    T.eq(ev[0].payload, { id: u.id, type: 'settler', culture: S.cu.id, cell: S.dest });
    T.eq(w.units.length, 0);
  }
  // equally near: the lower id wins
  {
    const w = flat(), cu = culture(w), origin = town(w, 100, cu), dest = cellAtHops(w, 100, 6), g = w.geo;
    const n1 = g.nbr[6 * dest + 1], n2 = g.nbr[6 * dest + 4];
    T.near(g.angle(dest, n1), g.angle(dest, n2), 1e-3);
    const first = town(w, g.angle(dest, n1) <= g.angle(dest, n2) ? n1 : n2, cu, { pop: 10 }), second = town(w, first.cell === n1 ? n2 : n1, cu, { pop: 10 });
    T.stub(DM, 'Civ', { found: () => null });
    const u = Units.spawn(w, { type: 'settler', culture: cu.id, from: origin.id, dest, size: 5 });
    runOut(w, u);
    T.ok(first.pop === 15 || (g.angle(dest, second.cell) < g.angle(dest, first.cell) && second.pop === 15), 'nearest, ties to the lower id');
  }
  // nobody left to join: the party is lost
  {
    const S = build(), w = S.w;
    T.stub(DM, 'Civ', { found: () => null, live: (world) => world.settlements.filter(s => !s.dead) });
    const u = Units.spawn(w, { type: 'settler', culture: S.cu.id, from: S.origin.id, dest: S.dest, size: 25 });
    S.origin.dead = true; S.nearOwn.dead = true; S.farOwn.dead = true;
    const ev = H.capture(() => { runOut(w, u); });
    T.eq(ev.map(e => e.type), ['unit:died']);
    T.eq(ev[0].payload, { id: u.id, type: 'settler', culture: S.cu.id, cell: S.dest, cause: 'lost' });
    T.eq(S.foreign.pop, 300, 'strangers do not take them in');
  }
});

T.test('a settler sent to a settlement (to ≥ 0) joins it; refugees fall back to founding or the nearest', () => {
  const w = flat(), cu = culture(w), other = culture(w, 'Vorn'), origin = town(w, 100, cu), home = town(w, cellAtHops(w, 100, 6), cu, { pop: 80 });
  const log = [];
  T.stub(DM, 'Civ', civStub(w, log));
  let u = Units.spawn(w, { type: 'settler', culture: cu.id, from: origin.id, to: home.id, dest: home.cell, size: 60 });
  runOut(w, u);
  T.eq(home.pop, 140, 'joined');
  T.eq(log.length, 0, 'no founding attempt');
  // the target fell to another culture on the way: found at the site if Civ allows, else nearest of their own
  u = Units.spawn(w, { type: 'settler', culture: cu.id, from: origin.id, to: home.id, dest: home.cell, size: 60 });
  home.culture = other.id;
  runOut(w, u);
  T.eq(home.pop, 140, 'not into a foreign town');
  T.eq(log.length, 1, 'tried to found instead');
  T.eq(log[0].cell, home.cell);
});

T.test('traders, armies and prophets are handed to Society.onArrive while still in world.units', () => {
  for (const type of ['trader', 'army', 'prophet']) {
    const w = flat(), cu = culture(w), foe = culture(w, 'Vorn'), a = town(w, 100, cu), b = town(w, cellAtHops(w, 100, 7), type === 'army' ? foe : cu);
    const seen = [];
    T.stub(DM, 'Civ', { found: () => { throw new Error('Civ.found must not be called for a ' + type); } });
    T.stub(DM, 'Society', {
      atWar: () => true,
      onArrive: (world, unit) => { seen.push([world === w, unit, world.units.indexOf(unit) >= 0, unit.i, unit.t, unit.path[unit.i]]); },
    });
    const u = Units.spawn(w, { type, culture: cu.id, from: a.id, to: b.id, dest: b.cell, size: 30 });
    const ev = H.capture(() => { runOut(w, u); });
    T.eq(seen.length, 1, type + ': onArrive once');
    T.ok(seen[0][0] && seen[0][1] === u && seen[0][2], 'called with (world, unit), the unit still listed');
    T.eq([seen[0][3], seen[0][4], seen[0][5]], [u.path.length - 1, 0, b.cell], 'standing on its destination');
    T.eq(ev.map(e => e.type), ['unit:arrived']);
    T.eq(ev[0].payload, { id: u.id, type, culture: cu.id, cell: b.cell });
    T.eq(w.units.length, 0);
  }
  // no Society at all: the unit simply arrives
  const w = flat(), cu = culture(w), a = town(w, 100, cu), b = town(w, cellAtHops(w, 100, 3), cu);
  const u = Units.spawn(w, { type: 'trader', culture: cu.id, from: a.id, to: b.id, dest: b.cell, size: 3 });
  T.eq(H.capture(() => { runOut(w, u); }).map(e => e.type), ['unit:arrived']);
});

T.test('an army that loses its battle: onArrive may kill the unit (died, not arrived)', () => {
  const w = flat(), cu = culture(w), foe = culture(w, 'Vorn'), a = town(w, 100, cu), b = town(w, cellAtHops(w, 100, 4), foe);
  T.stub(DM, 'Society', { atWar: () => true, onArrive: (world, unit) => { T.eq(Units.kill(world, unit, 'battle'), true); } });
  const u = Units.spawn(w, { type: 'army', culture: cu.id, from: a.id, to: b.id, dest: b.cell, size: 30 });
  const ev = H.capture(() => { runOut(w, u); });
  T.eq(ev.map(e => e.type), ['unit:died']);
  T.eq(ev[0].payload, { id: u.id, type: 'army', culture: cu.id, cell: b.cell, cause: 'battle' });
  T.eq(w.units.length, 0);
});

// ------------------------------------------------------------------ losses
T.test('armies and traders disband when the target dies; armies when the war is over', () => {
  const setup = (type) => {
    const w = flat(), cu = culture(w), foe = culture(w, 'Vorn'), a = town(w, 100, cu), b = town(w, cellAtHops(w, 100, 15), foe);
    const u = Units.spawn(w, { type, culture: cu.id, from: a.id, to: b.id, dest: b.cell, size: 30 });
    return { w, cu, foe, a, b, u };
  };
  for (const type of ['army', 'trader']) {
    const S = setup(type), arrived = [];
    T.stub(DM, 'Society', { atWar: () => true, onArrive: (w, u) => arrived.push(u) });
    Units.step(S.w, TICK); Units.step(S.w, TICK);
    T.eq(S.w.units.length, 1, type + ' marching');
    const here = Units.cellOf(S.w, S.u);
    S.b.dead = true;
    const ev = H.capture(() => { Units.step(S.w, TICK); });
    T.eq(ev.map(e => e.type), ['unit:died'], type + ': target dead');
    T.eq(ev[0].payload, { id: S.u.id, type, culture: S.cu.id, cell: here, cause: 'disbanded' });
    T.eq(S.w.units.length, 0); T.eq(arrived.length, 0);
  }
  {
    const S = setup('army'); let war = true; const asked = [];
    T.stub(DM, 'Society', { atWar: (w, x, y) => { asked.push([x, y]); return war; }, onArrive: () => {} });
    Units.step(S.w, TICK);
    T.eq(S.w.units.length, 1);
    T.eq(asked[0], [S.cu.id, S.foe.id], 'atWar(world, the army’s culture, the target’s culture)');
    war = false;
    const ev = H.capture(() => { Units.step(S.w, TICK); });
    T.eq(events(ev, 'unit:died').map(p => p.cause), ['disbanded'], 'peace: the army goes home');
  }
  {
    const S = setup('army');                                         // the target changed hands: now its own culture's town
    T.stub(DM, 'Society', { atWar: (w, x, y) => x !== y, onArrive: () => {} });
    Units.step(S.w, TICK);
    S.b.culture = S.cu.id;
    const ev = H.capture(() => { Units.step(S.w, TICK); });
    T.eq(events(ev, 'unit:died').map(p => p.cause), ['disbanded']);
  }
  {
    const S = setup('trader');                                       // traders do not care about wars
    T.stub(DM, 'Society', { atWar: () => false, onArrive: () => {} });
    const ev = H.capture(() => { runOut(S.w, S.u); });
    T.eq(ev.map(e => e.type), ['unit:arrived']);
  }
  {
    const S = setup('army');                                         // no diplomacy module yet: armies march on
    T.stub(DM, 'Society', { onArrive: () => {} });
    T.eq(H.capture(() => { runOut(S.w, S.u); }).map(e => e.type), ['unit:arrived']);
  }
  {
    const S = setup('settler');                                      // a dead target does not disband settlers or prophets
    T.stub(DM, 'Civ', civStub(S.w));
    S.b.dead = true;
    T.eq(H.capture(() => { runOut(S.w, S.u); }).map(e => e.type), ['unit:arrived']);
    const P = setup('prophet'); const got = [];
    T.stub(DM, 'Society', { onArrive: (w, u) => got.push(u) });
    P.b.dead = true;
    runOut(P.w, P.u);
    T.eq(got.length, 1, 'the prophet still arrives; Society decides');
  }
});

T.test('a blocked unit re-paths once, honouring its naval mode, then dies', () => {
  // on foot around a lava flow
  {
    const w = flat(), cu = culture(w), a = town(w, 100, cu), dest = cellAtHops(w, 100, 14);
    T.stub(DM, 'Civ', civStub(w));
    const u = Units.spawn(w, { type: 'settler', culture: cu.id, from: a.id, dest, size: 20 });
    Units.step(w, TICK); Units.step(w, TICK);                        // 2.5 cells in
    const before = [0, 0, 0], after = [0, 0, 0];
    Units.dirOf(w, u, before);
    const ahead = u.path[u.i + 1], old = u.path.slice();
    T.ok(u.t > 0 && u.t < 0.5 || u.t === 0.5, 'caught part-way (t = ' + u.t + ')');
    w.lava[ahead] = 0.1;                                             // impassable (> 0.05) but not deadly (≤ 0.2)
    // freeze time: a step of (almost) no length still notices the block and re-paths
    Units.step(w, 1e-9);
    Units.dirOf(w, u, after);
    T.eq(u.data.repathed, true, 're-pathed');
    T.ok(DM.V3.angle(before, after) < 1e-6, 'the party does not jump when it turns round');
    T.ok(u.path.indexOf(ahead) <= 0, 'the new route avoids the lava (it may start on the abandoned step)');
    T.ok(isChain(w, u.path) && u.path[u.path.length - 1] === dest, 'a chain to the same destination');
    T.ok(JSON.stringify(u.path) !== JSON.stringify(old));
    const ev = H.capture(() => { runOut(w, u); });
    T.eq(ev.map(e => e.type), ['unit:arrived'], 'and it gets there');
    for (let i = 1; i < u.path.length; i++) T.ok(!(w.lava[u.path[i]] > 0.05), 'never onto the lava');
  }
  // blocked twice: lost
  {
    const w = flat(), cu = culture(w), a = town(w, 100, cu), dest = cellAtHops(w, 100, 14);
    const u = Units.spawn(w, { type: 'trader', culture: cu.id, from: a.id, dest, size: 20 });
    Units.step(w, TICK);                                             // 1.75 cells: t = 0.75 → already inside the next cell
    w.lava[u.path[u.i + 2]] = 0.1;
    Units.step(w, 0.01);                                             // reaches path[i+1] (0.25 away) and finds the way shut
    T.eq(u.data.repathed, true);
    T.eq(w.units.length, 1);
    const here = Units.cellOf(w, u);
    w.lava[u.path[u.i + (u.t >= 0.5 ? 2 : 1)]] = 0.1;
    let ev = H.capture(() => { for (let k = 0; k < 8 && w.units.length; k++) Units.step(w, 0.01); });
    T.eq(ev.map(e => e.type), ['unit:died'], 'second block');
    T.eq(ev[0].payload.cause, 'lost');
    T.ok(DM.Geo && w.geo.angle(ev[0].payload.cell, here) < 0.2, 'died where it stood');
    T.eq(w.units.length, 0);
  }
  // no way round at all: lost at once, without using up a search later
  {
    const w = flat(), cu = culture(w), a = town(w, 100, cu), dest = cellAtHops(w, 100, 10), g = w.geo;
    const u = Units.spawn(w, { type: 'settler', culture: cu.id, from: a.id, dest, size: 20 });
    Units.step(w, TICK);
    for (let k = 0; k < g.nbrN[dest]; k++) w.lava[g.nbr[6 * dest + k]] = 0.1;      // the site is walled in
    const ev = H.capture(() => { for (let k = 0; k < 40 && w.units.length; k++) Units.step(w, TICK); });
    T.eq(ev.map(e => e.type), ['unit:died']);
    T.eq(ev[0].payload.cause, 'lost');
  }
  // the land ahead sinks: a party on foot cannot go on, a party with boats can
  for (const naval of [false, 'coastal', true]) {
    const w = flat(), cu = culture(w), a = town(w, side(w, -0.7), cu), dest = side(w, 0.7), g = w.geo;
    T.stub(DM, 'Civ', civStub(w));
    const u = Units.spawn(w, { type: 'settler', culture: cu.id, from: a.id, dest, size: 20, naval });
    T.ok(u.path.every(c => !isSea(w, c)), 'all land when it set out');
    Units.step(w, TICK); Units.step(w, TICK);
    for (let c = 0; c < w.n; c++) if (Math.abs(g.pos[3 * c]) < 0.25) w.elev[c] = naval === 'coastal' ? -0.3 : -2;   // a channel opens
    const ev = H.capture(() => { runOut(w, u); });
    if (naval === false) {
      T.eq(ev.map(e => e.type), ['unit:died'], 'on foot: no way across');
      T.eq(ev[0].payload.cause, 'lost');
      T.ok(!isSea(w, ev[0].payload.cell), 'it stopped on the shore');
    } else {
      T.eq(ev.map(e => e.type), ['unit:arrived'], naval + ': it sails on');
      T.ok(!u.data.repathed, 'water is no obstacle to a party with boats: no re-path needed');
    }
  }
  // sea ice closes the strait ahead of a ship: it re-paths through the open lead, by sea
  {
    const w = channel(1.5, 0.3), cu = culture(w), a = town(w, side(w, -0.75), cu), dest = side(w, 0.75), g = w.geo;
    T.stub(DM, 'Civ', civStub(w));
    const u = Units.spawn(w, { type: 'settler', culture: cu.id, from: a.id, dest, size: 20, naval: true });
    while (!u.sea) Units.step(w, TICK);
    const route = u.path.slice(u.i);
    for (const c of route) if (isSea(w, c) && c !== u.path[u.i] && c !== u.path[u.i + 1]) { for (let k = 0; k < g.nbrN[c]; k++) { const b = g.nbr[6 * c + k]; if (isSea(w, b) && Math.abs(g.pos[3 * b + 1]) < 0.35) w.ice[b] = 0.9; } }
    const ev = H.capture(() => { runOut(w, u); });
    T.eq(u.data.repathed, true, 're-pathed round the ice');
    T.eq(ev.map(e => e.type), ['unit:arrived']);
    // the same for a party that is not allowed on deep water: its re-path honours 'coastal' and fails
    const v = Units.spawn(w, { type: 'settler', culture: cu.id, from: a.id, dest, size: 20, naval: true });
    T.ok(v, 'ships still find the lead');
    v.naval = 'coastal';                                             // (as if it had set out by canoe)
    const ev2 = H.capture(() => { runOut(w, v); });
    T.eq(ev2.map(e => e.type), ['unit:died'], 'deep water ahead: canoes cannot re-path across it');
    T.eq(ev2[0].payload.cause, 'lost');
  }
});

T.test('lava under a unit kills it (lava > 0.2)', () => {
  const w = flat(), cu = culture(w), a = town(w, 100, cu), dest = cellAtHops(w, 100, 12);
  const u = Units.spawn(w, { type: 'prophet', culture: cu.id, from: a.id, dest, size: 1 });
  Units.step(w, TICK);                                               // one cell in, t = 0
  const here = Units.cellOf(w, u);
  w.lava[here] = 0.2;
  Units.step(w, TICK);
  T.eq(w.units.length, 1, 'exactly 0.2 is survivable');
  const now2 = Units.cellOf(w, u);
  w.lava[now2] = 0.21;
  const ev = H.capture(() => { Units.step(w, TICK); });
  T.eq(ev.map(e => e.type), ['unit:died']);
  T.eq(ev[0].payload, { id: u.id, type: 'prophet', culture: cu.id, cell: now2, cause: 'lava' });
  // walking into a lava field that Path still thought passable is not possible; but a flow that arrives on the
  // cell a unit is entering kills it on arrival there
  const v = Units.spawn(w, { type: 'settler', culture: cu.id, from: a.id, dest, size: 5 });
  Units.step(w, TICK);                                               // i = 1, t = 0.25
  const next = v.path[v.i + 1];
  Units.step(w, 0.06);                                               // t = 0.55: inside the next cell
  T.eq(Units.cellOf(w, v), next);
  w.lava[next] = 3;
  const ev2 = H.capture(() => { Units.step(w, TICK); });
  T.eq(events(ev2, 'unit:died'), [{ id: v.id, type: 'settler', culture: cu.id, cell: next, cause: 'lava' }]);
});

T.test('kill: removes the unit, reports its last cell and the cause, once', () => {
  const w = flat(), cu = culture(w), a = town(w, 100, cu);
  const u = Units.spawn(w, { type: 'army', culture: cu.id, from: a.id, dest: cellAtHops(w, 100, 9), size: 30 });
  const v = Units.spawn(w, { type: 'trader', culture: cu.id, from: a.id, dest: cellAtHops(w, 100, 9), size: 3 });
  T.stub(DM, 'Society', { atWar: () => true, onArrive: () => {} });
  Units.step(w, TICK); Units.step(w, TICK);                          // army 2.5 cells, trader 3.5
  const cell = Units.cellOf(w, u);
  T.eq(cell, u.path[3], 'the nearer end of its step (t = 0.5 counts as the next cell)');
  let ev = H.capture(() => { T.eq(Units.kill(w, u, 'meteor'), true); });
  T.eq(ev, [{ type: 'unit:died', payload: { id: u.id, type: 'army', culture: cu.id, cell, cause: 'meteor' } }]);
  T.eq(w.units.length, 1); T.ok(w.units[0] === v);
  ev = H.capture(() => { T.eq(Units.kill(w, u, 'meteor'), false, 'already dead'); });
  T.eq(ev.length, 0);
  T.eq(Units.kill(w, { id: 99, type: 'army', culture: 0, path: [1, 2], i: 0, t: 0 }, 'x'), false, 'not one of this world’s units');
  T.eq(Units.kill(w, null, 'x'), false); T.eq(Units.kill(null, v, 'x'), false);
  ev = H.capture(() => { T.eq(Units.kill(w, v), true); });
  T.eq(ev[0].payload.cause, 'lost', 'a missing cause is reported as lost');
  T.eq(w.units.length, 0);
  Units.step(w, TICK);                                               // nothing left: no error
  T.eq(Units.CAUSES, ['lost', 'lava', 'disbanded']);
});

// ------------------------------------------------------------------ queries
T.test('dirOf: unit length, on the cell at t = 0, continuous along the whole journey', () => {
  const w = channel(0.4, 0.3), cu = culture(w), a = town(w, side(w, -0.75), cu), dest = side(w, 0.75), g = w.geo, V3 = DM.V3;
  T.stub(DM, 'Civ', civStub(w));
  const u = Units.spawn(w, { type: 'settler', culture: cu.id, from: a.id, dest, size: 20, naval: true });
  const out = new Float32Array(3), prev = [0, 0, 0], cd = [0, 0, 0];
  T.eq(Units.dirOf(w, u, out), a.cell);
  g.dir(a.cell, cd);
  T.near(V3.angle(out, cd), 0, 1e-6, 'at its origin');
  V3.copy(prev, out);
  const dt = 0.01;
  let maxJump = 0, steps = 0, travelled = 0, maxEdge = 0;
  for (let c = 0; c < w.n; c++) for (let k = 0; k < g.nbrN[c]; k++) maxEdge = Math.max(maxEdge, g.angle(c, g.nbr[6 * c + k]));
  while (w.units.indexOf(u) >= 0 && steps < 100000) {
    const seaBefore = u.sea;
    Units.step(w, dt); steps++;
    if (w.units.indexOf(u) < 0) break;
    const cur = Units.dirOf(w, u, out);
    T.near(Math.hypot(out[0], out[1], out[2]), 1, 1e-6, 'unit length');
    T.eq(cur, Units.cellOf(w, u));
    T.ok(cur === u.path[u.i] || cur === u.path[u.i + 1]);
    const jump = V3.angle(prev, out);
    // in dt years a party covers at most dt × 5 × 2.5 cells
    T.ok(jump <= dt * 5 * 2.5 * maxEdge * 1.01 + 1e-7, 'no jump: moved ' + jump.toFixed(5) + ' rad in one step (sea ' + seaBefore + ')');
    if (jump > maxJump) maxJump = jump;
    travelled += jump;
    V3.copy(prev, out);
    // the direction lies on the great-circle step between the two cells
    const A = [0, 0, 0], B = [0, 0, 0];
    g.dir(u.path[u.i], A); g.dir(u.path[Math.min(u.i + 1, u.path.length - 1)], B);
    T.ok(V3.angle(A, out) + V3.angle(out, B) <= V3.angle(A, B) + 1e-5, 'between its two cells');
  }
  g.dir(dest, cd);
  T.ok(V3.angle(prev, cd) <= dt * 5 * 2.5 * maxEdge * 1.01 + 1e-7, 'the last position before arrival is next to the destination');
  T.ok(travelled >= g.angle(a.cell, dest) - 1e-3, 'it covered the whole way: ' + travelled.toFixed(3) + ' rad');
  say('dirOf: journey sampled every', dt, 'y:', steps, 'samples, largest move', maxJump.toFixed(5), 'rad (one cell ≈', g.step.toFixed(4), 'rad)');
  // plain arrays work as `out`; a missing `out` still returns the cell
  const plain = [9, 9, 9], v = Units.spawn(w, { type: 'settler', culture: cu.id, from: a.id, dest, size: 20, naval: true });
  Units.step(w, 0.13);
  const c1 = Units.dirOf(w, v, plain);
  T.near(Math.hypot(plain[0], plain[1], plain[2]), 1, 1e-6);
  T.eq(Units.dirOf(w, v), c1);
  T.eq(Units.dirOf(w, v, undefined), c1);
});

T.test('dirOf and cellOf never throw on damaged units', () => {
  const w = flat(), out = [5, 5, 5];
  const bad = [
    null, undefined, {}, { path: [], dest: -1 }, { path: null, i: 0, t: 0, dest: 7 }, { path: [3], i: 5, t: 0.3 },
    { path: [3, 9999999], i: 0, t: 0.7 }, { path: [-1, 4], i: 0, t: 0.2 }, { path: [3, 4], i: -2, t: NaN }, { path: [3, 4], i: 0, t: 7 },
    { path: [1.5, 2], i: 0, t: 0 },
  ];
  for (const u of bad) {
    const c = Units.dirOf(w, u, out);
    T.ok(c === -1 || (Number.isInteger(c) && c >= 0 && c < w.n), 'cell ' + c + ' for ' + JSON.stringify(u));
    T.near(Math.hypot(out[0], out[1], out[2]), 1, 1e-6, 'unit length for ' + JSON.stringify(u));
    const c2 = Units.cellOf(w, u);
    T.ok(c2 === -1 || (Number.isInteger(c2) && c2 >= 0 && c2 < w.n));
  }
  T.eq(Units.dirOf(w, { path: null, dest: 7 }, out), 7, 'a unit with no path stands on its destination');
  T.eq(Units.dirOf(null, { path: [1] }, out), -1);
  T.eq(out, [0, 1, 0]);
});

T.test('near: units within the angle, nearest first, by their interpolated position', () => {
  const w = flat(), cu = culture(w), g = w.geo, a = town(w, 100, cu);
  T.stub(DM, 'Society', { atWar: () => true, onArrive: () => {} });
  const mk = (k, type) => Units.spawn(w, { type: type || 'trader', culture: cu.id, from: -1, cell: cellAtHops(w, 100, k), dest: 100, size: 1 });
  const u3 = mk(3), u1 = mk(1), u6 = mk(6), u0 = Units.spawn(w, { type: 'trader', culture: cu.id, from: a.id, dest: cellAtHops(w, 100, 9), size: 1 });
  const twin = Units.spawn(w, { type: 'army', culture: cu.id, from: -1, cell: u3.path[0], dest: 100, size: 1 });
  const ids = (list) => list.map(u => u.id);
  T.eq(ids(Units.near(w, 100, 0)), [u0.id], 'radius 0: the unit standing on the cell');
  T.eq(ids(Units.near(w, 100, g.angle(100, u1.path[0]) + 1e-6)), [u0.id, u1.id]);
  T.eq(ids(Units.near(w, 100, 4)), [u0.id, u1.id, u3.id, twin.id, u6.id], 'nearest first; equal distance → lower id');
  T.eq(Units.near(w, 2000, 0.01).length, 0);
  const res = Units.near(w, 100, 4);
  res.length = 0;
  T.eq(Units.near(w, 100, 4).length, 5, 'a fresh array each time');
  for (const bad of [-1, w.n, 1.5, NaN, undefined]) T.eq(Units.near(w, bad, 1), []);
  T.eq(Units.near(w, 100, -1), []); T.eq(Units.near(w, 100, NaN), []); T.eq(Units.near(null, 100, 1), []);
  // positions are interpolated: after 0.1 y the trader from cell 100 is 0.7 cells out
  Units.step(w, 0.1);
  const d = [0, 0, 0], c0 = [0, 0, 0];
  Units.dirOf(w, u0, d); g.dir(100, c0);
  const off = DM.V3.angle(d, c0);
  T.ok(off > 0.6 * g.angle(100, u0.path[1]) && off < 0.8 * g.angle(100, u0.path[1]), '0.7 of the way to the next cell');
  T.eq(ids(Units.near(w, 100, off - 1e-4)).indexOf(u0.id), -1, 'just outside');
  T.ok(ids(Units.near(w, 100, off + 1e-4)).indexOf(u0.id) >= 0, 'just inside');
  // what Powers does with it: strike everything within a radius
  const struck = Units.near(w, 100, 0.3);
  T.ok(struck.length >= 3);
  const ev = H.capture(() => { for (const u of struck) Units.kill(w, u, 'meteor'); });
  T.eq(ev.length, struck.length);
  T.eq(Units.near(w, 100, 0.3).length, 0);
});

// ------------------------------------------------------------------ robustness
T.test('step: bad dt, damaged units and re-entrant handlers', () => {
  const w = flat(), cu = culture(w), a = town(w, 100, cu), b = town(w, cellAtHops(w, 100, 3), cu), far = cellAtHops(w, 100, 12);
  const u = Units.spawn(w, { type: 'settler', culture: cu.id, from: a.id, dest: far, size: 5 });
  for (const dt of [0, -1, NaN, Infinity, undefined, null, 'x']) { Units.step(w, dt); T.eq([u.i, u.t], [0, 0], 'dt ' + dt + ' moves nothing'); }
  Units.step(null, TICK); Units.step({}, TICK); Units.step({ units: [] }, TICK);
  // damaged units die quietly as 'lost' instead of throwing
  const mk = () => Units.spawn(w, { type: 'settler', culture: cu.id, from: a.id, dest: far, size: 5 });
  const broken = [mk(), mk(), mk(), mk(), mk()];
  broken[0].path = []; broken[1].path = null; broken[2].path = [100, 2000]; broken[3].i = NaN; broken[4].path = [100, -4, far];
  T.stub(DM, 'Civ', { found: () => null, live: (world) => world.settlements.filter(s => !s.dead) });
  let ev = H.capture(() => { for (let k = 0; k < 60; k++) Units.step(w, TICK); });
  T.eq(w.units.length, 0, 'every unit is resolved');
  T.ok(events(ev, 'unit:died').length >= 3, 'the hopeless ones are lost: ' + JSON.stringify(ev.map(e => e.type + ':' + (e.payload.cause || ''))));
  T.eq(H.checkFinite(w), null);
  T.ok(Number.isFinite(a.pop) && Number.isFinite(b.pop));
  // handlers that spawn and kill while units are being stepped
  const order = [];
  let victim = null, born = null;
  T.stub(DM, 'Society', {
    atWar: () => true,
    onArrive: (world, unit) => {
      order.push('arrive ' + unit.type);
      if (victim) { Units.kill(world, victim, 'ambush'); victim = null; }
      if (!born) born = Units.spawn(world, { type: 'prophet', culture: cu.id, from: b.id, to: a.id, dest: a.cell, size: 1 });
    },
  });
  const t1 = Units.spawn(w, { type: 'trader', culture: cu.id, from: a.id, to: b.id, dest: b.cell, size: 1 });   // arrives on tick 2
  const t2 = Units.spawn(w, { type: 'trader', culture: cu.id, from: a.id, to: b.id, dest: far, size: 1 });
  victim = t2;
  ev = H.capture(() => { Units.step(w, TICK); Units.step(w, TICK); });
  T.eq(ev.map(e => e.type + ' ' + e.payload.id), ['unit:died ' + t2.id, 'unit:spawned ' + born.id, 'unit:arrived ' + t1.id]);
  T.eq([born.i, born.t], [0, 0], 'a unit born during a step does not move in it');
  T.eq(w.units.length, 1);
  Units.step(w, TICK);
  T.near(prog(born), 1, 1e-9, 'it moves from the next step on');
  // a handler that steps the world again must not corrupt the outer loop
  let depth = 0;
  T.stub(DM, 'Society', { atWar: () => true, onArrive: (world) => { if (depth++ < 2) Units.step(world, TICK); } });
  for (let k = 0; k < 5; k++) Units.spawn(w, { type: 'trader', culture: cu.id, from: a.id, to: b.id, dest: b.cell, size: 1 });
  for (let k = 0; k < 20 && w.units.length; k++) Units.step(w, TICK);
  T.eq(w.units.length, 0);
});

T.test('deterministic, and a saved journey continues exactly', () => {
  const run = (reload) => {
    const w = H.makeWorld({ L: 4, seed: 3 }), cu = culture(w), other = culture(w, 'Vorn');
    const sites = []; for (let c = 0; c < w.n && sites.length < 8; c++) if (w.hab[c] > 0.3 && sites.every(s => w.geo.angle(s, c) > 0.25)) sites.push(c);
    const towns = sites.map((c, k) => town(w, c, k % 3 === 2 ? other : cu));
    T.stub(DM, 'Civ', civStub(w));
    T.stub(DM, 'Society', { atWar: (world, x, y) => x !== y && world.year < 12, onArrive: (world, unit) => { const s = world.settlements[unit.to]; if (s) s.pop += unit.size; } });
    let world = w, spawned = 0;
    for (let tick = 0; tick < 240; tick++) {
      world.year += TICK;
      if (tick % 3 === 0) {
        const from = towns[(tick / 3) % towns.length], to = towns[(tick / 3 * 5 + 2) % towns.length];
        if (from !== to) {
          const type = ['settler', 'trader', 'army', 'prophet'][(tick / 3) % 4];
          const live = world.settlements;
          if (Units.spawn(world, { type, culture: live[from.id].culture, from: from.id, to: type === 'settler' ? -1 : to.id, dest: to.cell, size: 7 + tick, naval: [false, 'coastal', true][tick % 3] })) spawned++;
        }
      }
      if (tick === 60) for (let c = 0; c < world.n; c += 17) if (world.elev[c] >= 0) world.lava[c] = 0.4;       // an eruption
      if (tick === 90) world.seaLevel = 0.35;                                                                    // the sea rises
      Units.step(world, TICK);
      if (reload && tick === 75) {
        world = State.deserialize(JSON.parse(JSON.stringify(State.serialize(world))));
        H.fillBasics(world);
      }
    }
    T.ok(spawned > 20, 'journeys started: ' + spawned);
    T.eq(H.checkFinite(world), null);
    return { hash: H.hash(world), units: JSON.stringify(world.units), pops: world.settlements.map(s => s.pop), n: world.settlements.length };
  };
  const a = run(false), b = run(false);
  T.eq(a.hash, b.hash, 'same calls, same world');
  T.eq(a.units, b.units);
  say('determinism run: hash', a.hash, '·', a.n, 'settlements at the end');
});

T.test('performance: 300 units cost microseconds per tick and no garbage worth the name', () => {
  const w = H.makeWorld({ L: 6, seed: 2 }), cu = culture(w), n = w.n;
  const sites = []; for (let c = 0; c < n && sites.length < 40; c += 97) if (w.hab[c] > 0.3 && w.lava[c] === 0) sites.push(c);
  const towns = sites.map(c => town(w, c, cu));
  T.stub(DM, 'Civ', { found: () => null, live: (world) => world.settlements });
  T.stub(DM, 'Society', { atWar: () => true, onArrive: () => {} });
  const refill = () => {
    let k = w.units.length * 7;
    while (w.units.length < 300 && k < 100000) {
      const a = towns[k % towns.length], b = towns[(k * 7 + 3) % towns.length]; k++;
      if (a !== b) Units.spawn(w, { type: ['settler', 'trader', 'army', 'prophet'][k % 4], culture: cu.id, from: a.id, to: b.id, dest: b.cell, size: 5, naval: true, maxCost: 150 });
    }
  };
  let t0 = now(); refill(); const tSpawn = now() - t0;
  T.eq(w.units.length, 300);
  for (let k = 0; k < 40; k++) { Units.step(w, TICK); refill(); }    // warm up
  let tot = 0, mx = 0, ticks = 0, moved = 0;
  for (let k = 0; k < 200; k++) {
    if (w.units.length < 300) refill();
    moved += w.units.length;
    t0 = now(); Units.step(w, TICK); const dt = now() - t0;
    tot += dt; if (dt > mx) mx = dt; ticks++;
  }
  say('300 spawns at L6 (A* each, budget 150):', tSpawn.toFixed(1), 'ms →', (tSpawn / 300).toFixed(3), 'ms each');
  say('Units.step with ~300 units at L6: mean', (tot / ticks * 1000).toFixed(1), 'µs per tick, worst', (mx * 1000).toFixed(0), 'µs (', (tot / moved * 1e6).toFixed(0), 'ns per unit )');
  T.ok(tot / ticks < 2.5, 'mean step ' + (tot / ticks).toFixed(3) + ' ms');
  // garbage: heap growth per tick with a steady 300 units on long routes (no arrivals in the window)
  w.units.length = 0;
  const far = towns.slice().sort((x, y) => w.geo.angle(towns[0].cell, y.cell) - w.geo.angle(towns[0].cell, x.cell))[0];
  for (let k = 0; k < 300; k++) Units.spawn(w, { type: 'prophet', culture: cu.id, from: towns[0].id, to: far.id, dest: far.cell, size: 1, naval: true });
  const len = w.units[0].path.length;
  for (let k = 0; k < 30; k++) Units.step(w, 0.002);
  const mu = () => process.memoryUsage().heapUsed, d = [];
  for (let b = 0; b < 15; b++) { const a = mu(); for (let k = 0; k < 20; k++) Units.step(w, 0.002); d.push((mu() - a) / 20); }
  d.sort((x, y) => x - y);
  T.eq(w.units.length, 300, 'still travelling (route of ' + len + ' cells)');
  say('heap growth per tick with 300 units in motion: median', d[7].toFixed(0), 'bytes (', (d[7] / 300).toFixed(1), 'per unit )');
  T.ok(d[7] < 300 * 64, 'no arrays or objects per unit per tick: ' + d[7].toFixed(0) + ' bytes per tick');
});

// ------------------------------------------------------------------ map (optional)
if (process.env.UNITS_MAPS) {
  const M = require('../tools/mapimg.js');
  const L = 6, seed = 7;
  let w;
  if (REAL.Terrain && typeof REAL.Terrain.generate === 'function') {
    w = State.create({ seed, name: 'Unitworld', L });
    REAL.Terrain.generate(w); w.seaLevel = 0; w.heat = 0; w.phase = 'history';
    H.fillBasics(w);
    console.log('    map: real terrain, seed', seed);
  } else { w = H.makeWorld({ L, seed }); console.log('    map: helper planet, seed', seed); }
  const g = w.geo, n = w.n, cu = culture(w);
  const order = Array.from({ length: n }, (_, c) => c).filter(c => w.hab[c] > 0.45).sort((a, b) => (w.hab[b] - w.hab[a]) || (a - b));
  const sites = [];
  for (const c of order) { if (sites.length >= 14) break; if (sites.every(s => g.angle(s, c) > 0.45)) sites.push(c); }
  const towns = sites.map(c => town(w, c, cu));
  DM.Civ = { found: () => null, live: (world) => world.settlements };
  DM.Society = { atWar: () => true, onArrive: () => {} };
  const TYPES = ['settler', 'trader', 'army', 'prophet'], COL = { settler: [255, 255, 255], trader: [255, 200, 0], army: [230, 40, 40], prophet: [200, 90, 255] };
  const paint = new Map(), out = [0, 0, 0];
  let k = 0, sent = 0;
  const stats = {};
  for (let i = 0; i < towns.length; i++) for (let j = 0; j < towns.length; j++) {
    if (i === j || g.angle(towns[i].cell, towns[j].cell) > 1.2) continue;
    const type = TYPES[k++ % 4];
    const u = Units.spawn(w, { type, culture: cu.id, from: towns[i].id, to: towns[j].id, dest: towns[j].cell, size: 10, naval: k % 2 ? true : 'coastal' });
    if (!u) continue;
    sent++;
    const steps = u.path.length - 1;
    let ticks = 0, seaTicks = 0;
    while (w.units.indexOf(u) >= 0 && ticks < 5000) {
      const c = g.cellAt(out[0], out[1], out[2], Units.dirOf(w, u, out));
      const base = COL[type];
      paint.set(c, u.sea ? [base[0] * 0.25, base[1] * 0.9 + 25, 255] : base);      // blue-tinted while afloat
      if (u.sea) seaTicks++;
      Units.step(w, TICK); ticks++;
    }
    const s = stats[type] = stats[type] || { n: 0, steps: 0, ticks: 0, sea: 0 };
    s.n++; s.steps += steps; s.ticks += ticks; s.sea += seaTicks;
  }
  const elevCol = c => { const e = w.elev[c]; return e < w.seaLevel ? M.ramp([[0, [120, 170, 190]], [0.6, [50, 110, 160]], [2.5, [22, 58, 120]], [6.5, [8, 20, 60]]], w.seaLevel - e) : M.ramp([[0, [70, 120, 64]], [0.6, [112, 140, 76]], [1.8, [160, 152, 96]], [3.5, [120, 96, 76]], [5.5, [160, 160, 164]], [8, [215, 215, 215]]], e - w.seaLevel); };
  const siteSet = new Set(sites);
  console.log('    wrote', M.custom(w, 'shots/units-voyages.png', c => siteSet.has(c) ? [0, 0, 0] : paint.get(c) || elevCol(c), { w: 2048, h: 1024, rivers: false }));
  console.log('    ' + sent + ' journeys between ' + towns.length + ' towns; one dot per tick (0.25 y): settler white, trader yellow, army red, prophet violet; blue-tinted while afloat');
  for (const t of TYPES) if (stats[t]) console.log('      ' + t.padEnd(8) + String(stats[t].n).padStart(3) + ' journeys, mean ' + (stats[t].steps / stats[t].n).toFixed(0) + ' steps in ' + (stats[t].ticks / stats[t].n * TICK).toFixed(1) + ' years (' + (stats[t].steps / (stats[t].ticks * TICK)).toFixed(2) + ' cells/year overall; ' + (100 * stats[t].sea / stats[t].ticks).toFixed(0) + ' % of the time afloat)');
}
