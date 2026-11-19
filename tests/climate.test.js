// tests/climate.test.js — DM.Climate (SPEC-SIM §3.3, BUILD.md A3).
//   node tools/test.js climate            (T_VERBOSE=1 lists the passing tests; CLIMATE_REPORT=1 prints the measured numbers)
//   CLIMATE_MAPS=1 node tests/climate.test.js      also renders shots/climate-*.png (see the end of this file)
// Worlds: the helper's synthetic planets (terrain only: basics false) and hand-built planets (one square continent,
// with and without a north–south mountain wall; an all-land and an all-sea planet). No other sim module is needed:
// DM.Life.cover is stubbed wherever Climate.yearly runs.
'use strict';
const T = require('../tools/t.js');
const DM = require('../tools/load.js')({ only: ['climate'] });      // strict: a broken climate.js fails here
const H = require('./helpers/world.js');
T.watchEvents();

const C = DM.Climate, DEG = 180 / Math.PI;
const REPORT = !!process.env.CLIMATE_REPORT;
const say = (...a) => { if (REPORT) console.log('    ·', ...a); };

// ------------------------------------------------------------------ helpers
function helperWorld(opts) {                     // terrain only; sea 0, phase 'history', vars.seaBase = seaLevel
  return H.makeWorld(Object.assign({ L: 5, seed: 1, basics: false }, opts || {}));
}
// One continent between ±55° latitude and ±60° longitude, 0.3 high, in a −4 deep ocean; `wall` adds a
// north–south ridge of that height along longitude 0 (9° half-width).
function squareWorld(L, wall) {
  const w = DM.State.create({ seed: 5, L });
  const g = w.geo;
  for (let c = 0; c < w.n; c++) {
    const la = g.lat[c] * DEG, lo = g.lon[c] * DEG;
    let e = -4;
    if (Math.abs(la) < 55 && Math.abs(lo) < 60) {
      e = 0.3;
      if (wall && Math.abs(lo) < 9) e += wall * (1 - (lo / 9) * (lo / 9));
    }
    w.elev[c] = e;
  }
  w.seaLevel = 0; w.heat = 0; w.phase = 'history';
  return w;
}
function uniformWorld(L, elev, sea, phase) {
  const w = DM.State.create({ seed: 9, L });
  w.elev.fill(elev); w.seaLevel = sea; w.heat = 0; w.phase = phase || 'history';
  return w;
}
const lat = (w, c) => w.geo.lat[c] * DEG, lon = (w, c) => w.geo.lon[c] * DEG;
const isLand = (w, c) => w.elev[c] >= w.seaLevel;
function mean(w, arr, pred) {
  let s = 0, k = 0;
  for (let c = 0; c < w.n; c++) if (pred(c)) { s += arr[c]; k++; }
  return k ? s / k : NaN;
}
function count(w, pred) { let k = 0; for (let c = 0; c < w.n; c++) if (pred(c)) k++; return k; }
function coastDist(w) {                          // neighbour hops to the nearest sea cell (brute-force BFS)
  const g = w.geo, n = w.n, d = new Int32Array(n).fill(1e9), q = new Int32Array(n);
  let head = 0, tail = 0;
  for (let c = 0; c < n; c++) if (!isLand(w, c)) { d[c] = 0; q[tail++] = c; }
  while (head < tail) {
    const c = q[head++];
    for (let k = 0; k < g.nbrN[c]; k++) { const b = g.nbr[6 * c + k]; if (d[b] > d[c] + 1) { d[b] = d[c] + 1; q[tail++] = b; } }
  }
  return d;
}
function finite(w) {
  const bad = H.checkFinite(w);
  if (bad) return bad;
  for (const k of ['seaBase', 'iceRef', 'climate_ice0', 'climate_seaMark', 'climate_ms']) {
    if (w.vars[k] !== undefined && !Number.isFinite(w.vars[k])) return 'vars.' + k;
  }
  return null;
}
function stubLife(veg) { T.stub(DM, 'Life', { cover: () => ({ land: 0.4, veg: veg === undefined ? 0.6 : veg, forest: 0.3 }) }); }
function milestones(events) { return events.filter(e => e.type === 'climate:milestone').map(e => e.payload.kind + ' ' + e.payload.value); }
function clearFlags(w) { for (const k in w.need) w.need[k] = false; for (const k in w.dirty) w.dirty[k] = false; }
// what the scheduler does around Climate.yearly: an update when asked (at most every 2 years), ice every 5 years
function runYears(w, years, each) {
  for (let y = 1; y <= years; y++) {
    if (each) each(y);
    C.yearly(w);
    if (w.need.climate && y % 2 === 0) C.update(w);
    if (y % 5 === 0) C.stepIce(w);
  }
}

// ------------------------------------------------------------------ API
T.test('API: every documented function exists and nothing runs at load time', () => {
  for (const f of ['update', 'stepIce', 'yearly', 'calibrate', 'datum', 'anomaly', 'emit', 'addDust']) T.ok(typeof C[f] === 'function', 'Climate.' + f);
  T.ok(DM.TUNE.climate && DM.TUNE.climate.rainRef > 0, 'TUNE.climate with a fixed RAIN_REF');
});

// ------------------------------------------------------------------ temperature
T.test('temperature: equator about 53 °C warmer than the poles, smooth in latitude', () => {
  // an all-land planet below the datum: no lapse, no sea, no ice — the bare latitude profile plus its wobble
  for (const seed of [9, 2]) {
    const w = uniformWorld(5, -1, -9, 'water'); w.seed = seed;
    C.update(w);
    const eq = mean(w, w.temp, c => Math.abs(lat(w, c)) < 6), pole = mean(w, w.temp, c => Math.abs(lat(w, c)) > 82);
    say('seed', seed, 'equator', eq.toFixed(1), 'poles', pole.toFixed(1), 'difference', (eq - pole).toFixed(1));
    T.ok(eq - pole >= 40 && eq - pole <= 60, 'equator − pole = ' + (eq - pole));
    T.near(eq, 29, 5, 'about +29 at the equator'); T.near(pole, -24, 7, 'about −24 at the poles');
    let prev = Infinity;
    for (let a = 0; a < 90; a += 15) {                       // zonal means fall monotonically toward the poles
      const m = mean(w, w.temp, c => Math.abs(lat(w, c)) >= a && Math.abs(lat(w, c)) < a + 15);
      T.ok(m < prev, 'band ' + a + '° is colder than the one before'); prev = m;
    }
    // "a little noise": cells on one latitude circle differ, but only by a few degrees
    let lo = Infinity, hi = -Infinity;
    for (let c = 0; c < w.n; c++) if (Math.abs(lat(w, c) - 40) < 1.5) { lo = Math.min(lo, w.temp[c]); hi = Math.max(hi, w.temp[c]); }
    T.ok(hi - lo > 0.5 && hi - lo < 14, 'isotherms are not circles, spread at 40°N = ' + (hi - lo));
  }
  // helper worlds: same check on the cells below the datum of a dry world (the ocean floors reach every latitude)
  for (const seed of [1, 2, 3]) {
    const w = helperWorld({ seed, sea: -9, phase: 'water' }); delete w.vars.seaBase;
    C.update(w);
    const eq = mean(w, w.temp, c => w.elev[c] <= 0 && Math.abs(lat(w, c)) < 8), pole = mean(w, w.temp, c => w.elev[c] <= 0 && Math.abs(lat(w, c)) > 78);
    T.ok(eq - pole >= 40 && eq - pole <= 60, 'helper seed ' + seed + ': equator − pole = ' + (eq - pole));
  }
});

T.test('temperature: lapse 5.5 °C per unit, measured from Climate.datum, never from the live sea level', () => {
  const w = helperWorld({ seed: 2 });
  C.calibrate(w);
  const c = H.findLand(w, k => Math.abs(lat(w, k)) < 30 && w.elev[k] > 0.3 && w.elev[k] < 1.5);
  T.ok(c >= 0);
  const t0 = w.temp[c];
  w.elev[c] += 2; C.update(w);
  T.near(w.temp[c] - t0, -11, 1e-3, 'two units higher = 11 °C colder');
  w.elev[c] -= 2; C.update(w);
  T.near(w.temp[c], t0, 1e-3);
  // land below the datum gets no bonus
  const low = uniformWorld(3, -3, -9, 'water'), lower = uniformWorld(3, -6, -9, 'water');
  C.update(low); C.update(lower);
  for (let k = 0; k < low.n; k += 37) T.near(low.temp[k], lower.temp[k], 1e-4, 'no lapse below the datum');
  // a rising sea does not warm the land: the datum stays at vars.seaBase
  const before = Float32Array.from(w.temp);
  w.seaLevel = 0.4; C.update(w);
  T.eq(C.datum(w), 0, 'datum stays at seaBase');
  let checked = 0;
  for (let k = 0; k < w.n; k++) if (w.elev[k] >= 0.4) { T.near(w.temp[k], before[k], 1e-4, 'land temp unchanged by the sea'); checked++; }
  T.ok(checked > 500);
});

T.test('Climate.datum: max(0, seaBase) once calibrated, else max(0, seaLevel)', () => {
  const w = DM.State.create({ seed: 1, L: 2 });
  T.eq(w.seaLevel, -9); T.eq(C.datum(w), 0, 'genesis world at −9');
  w.seaLevel = 1.2; T.near(C.datum(w), 1.2, 1e-9, 'overfilled genesis world');
  w.vars.seaBase = 0.5; w.seaLevel = 0.9; T.near(C.datum(w), 0.5, 1e-9, 'calibrated: the base, not the live sea');
  w.vars.seaBase = -3; T.eq(C.datum(w), 0, 'never below 0');
  for (const bad of [NaN, Infinity, null, 'x', undefined]) { w.vars.seaBase = bad; w.seaLevel = 0.25; T.near(C.datum(w), 0.25, 1e-9, 'bad seaBase ' + bad + ' falls back to the sea'); }
  w.seaLevel = NaN; T.eq(C.datum(w), 0, 'NaN sea level');
});

T.test('genesis: a world whose sea is still at −9 is not frozen', () => {
  for (const seed of [1, 2, 3]) {
    const full = helperWorld({ seed });                       // the finished world: sea 0, calibrated
    C.calibrate(full);
    const landSet = c => full.elev[c] >= 0;
    const tFull = mean(full, full.temp, landSet);
    for (const sea of [-9, -5]) {
      const w = helperWorld({ seed, sea, phase: 'water' }); delete w.vars.seaBase;
      C.update(w);
      for (let i = 0; i < 40; i++) C.stepIce(w);              // 100 real seconds of genesis ice
      C.update(w);
      const t = mean(w, w.temp, landSet);                     // the same cells in both worlds
      const iced = count(w, c => landSet(c) && w.ice[c] > 0.5) / count(w, landSet);
      say('seed', seed, 'sea', sea, 'mean land temp', t.toFixed(2), 'vs', tFull.toFixed(2), 'at sea 0; iced land', (100 * iced).toFixed(1) + '%');
      T.near(t, tFull, 3, 'seed ' + seed + ' sea ' + sea + ': mean land temperature');
      T.ok(iced < 0.3, 'not an ice ball: ' + iced);
      T.ok(mean(w, w.temp, c => landSet(c) && Math.abs(lat(w, c)) < 30) > 10, 'warm low latitudes');
      T.eq(finite(w), null);
    }
  }
});

T.test('sea cells: stored temp stops at −2, sea ice forms from the unclamped value', () => {
  const w = helperWorld({ seed: 3 });
  C.calibrate(w);
  let coldSea = 0, polarIce = 0, polarSea = 0, tropicIce = 0;
  for (let c = 0; c < w.n; c++) {
    if (isLand(w, c)) continue;
    T.ok(w.temp[c] >= -2 - 1e-6, 'sea temp ' + w.temp[c]);
    if (w.temp[c] <= -2 + 1e-6) coldSea++;
    if (Math.abs(lat(w, c)) > 72) { polarSea++; if (w.ice[c] > 0.5) polarIce++; }
    if (Math.abs(lat(w, c)) < 40 && w.ice[c] > 0) tropicIce++;
  }
  say('sea cells at the −2 floor', coldSea, '; polar sea cells', polarSea, 'of which iced', polarIce);
  T.ok(coldSea > 50, 'some sea is at the floor');
  T.ok(polarSea > 20 && polarIce > 0.8 * polarSea, 'the polar sea is frozen: ' + polarIce + ' of ' + polarSea);
  T.eq(tropicIce, 0, 'no sea ice in the warm seas');
});

T.test('dust cools; emit and addDust clamp', () => {
  const w = helperWorld({ seed: 1, L: 4 });
  C.calibrate(w);
  const before = Float32Array.from(w.temp), ice = Float32Array.from(w.ice);
  T.eq(C.anomaly(w), 0);
  w.need.climate = false;
  C.addDust(w, 0.2);
  T.near(w.dust, 0.2, 1e-9); T.eq(w.need.climate, true, 'addDust sets need.climate');
  T.near(C.anomaly(w), -1.8, 1e-9, 'anomaly = globalTemp − 9 × dust');
  C.update(w);
  for (let c = 0; c < w.n; c++) if (isLand(w, c)) T.near(w.temp[c] - before[c], -1.8, 1e-3, 'every land cell 1.8 °C colder');
  T.ok(mean(w, w.temp, c => true) < mean(w, before, c => true) - 1, 'the world is colder');
  T.eq(Array.from(w.ice), Array.from(ice), 'update does not move ice');
  w.globalTemp = 3; T.near(C.anomaly(w), 1.2, 1e-9);
  C.addDust(w, 5); T.eq(w.dust, 1); C.addDust(w, -9); T.eq(w.dust, 0);
  C.addDust(w, NaN); T.eq(w.dust, 0); C.addDust(w, undefined); T.eq(w.dust, 0);
  C.emit(w, 20); T.eq(w.co2, 20); C.emit(w, 1000); T.eq(w.co2, 300); C.emit(w, -1e9); T.eq(w.co2, 0);
  C.emit(w, NaN); T.eq(w.co2, 0); C.emit(w, undefined); T.eq(w.co2, 0);
  w.co2 = NaN; C.emit(w, 5); T.eq(w.co2, 5, 'a poisoned co2 is repaired');
});

// ------------------------------------------------------------------ rainfall
T.test('winds: easterly trades, westerlies, polar easterlies; world._up holds upwind neighbours', () => {
  const w = squareWorld(5, 0);
  C.update(w);
  const g = w.geo, up = w._up, E = [0, 0, 0], N = [0, 0, 0];
  T.ok(up instanceof Int32Array && up.length === w.n, '_up is an Int32Array(n)');
  const fromEast = [0, 0, 0], total = [0, 0, 0];
  for (let c = 0; c < w.n; c++) {
    let isNbr = false;
    for (let k = 0; k < g.nbrN[c]; k++) if (g.nbr[6 * c + k] === up[c]) isNbr = true;
    T.ok(isNbr, '_up[' + c + '] is a neighbour');
    const a = Math.abs(lat(w, c));
    const band = a > 5 && a < 24 ? 0 : a > 36 && a < 54 ? 1 : a > 66 && a < 84 ? 2 : -1;
    if (band < 0) continue;
    g.basis(c, E, N);
    const u = up[c], d = (g.pos[3 * u] - g.pos[3 * c]) * E[0] + (g.pos[3 * u + 1] - g.pos[3 * c + 1]) * E[1] + (g.pos[3 * u + 2] - g.pos[3 * c + 2]) * E[2];
    total[band]++; if (d > 0) fromEast[band]++;
  }
  say('share of cells whose air comes from the east: trades', (fromEast[0] / total[0]).toFixed(2), 'westerlies', (fromEast[1] / total[1]).toFixed(2), 'polar', (fromEast[2] / total[2]).toFixed(2));
  T.ok(fromEast[0] / total[0] > 0.95, 'trades blow from the east');
  T.ok(fromEast[1] / total[1] < 0.05, 'westerlies blow from the west');
  T.ok(fromEast[2] / total[2] > 0.95, 'polar easterlies');
  // the trades drift toward the equator: the upwind neighbour lies on the poleward side more often than not
  let pole = 0, k = 0;
  for (let c = 0; c < w.n; c++) { const a = Math.abs(lat(w, c)); if (a > 8 && a < 22) { k++; if (Math.abs(lat(w, up[c])) > a) pole++; } }
  T.ok(pole / k > 0.6, 'trades come from higher latitudes: ' + pole / k);
  const w2 = squareWorld(5, 3); C.update(w2);
  T.ok(w2._up === up, 'the wind table is cached per grid');
});

T.test('rain shadow: the windward side of a mountain wall is clearly wetter than the lee', () => {
  for (const L of [4, 5]) {
    const wall = squareWorld(L, 5), flat = squareWorld(L, 0);
    C.calibrate(wall); C.calibrate(flat);
    const band = (w, a0, a1, l0, l1) => mean(w, w.moist, c => isLand(w, c) && Math.abs(lat(w, c)) >= a0 && Math.abs(lat(w, c)) < a1 && lon(w, c) > l0 && lon(w, c) < l1);
    // which side the air comes from, read off the wind table rather than assumed
    const side = la => { const c = wall.geo.cellAt(Math.cos(la / DEG) * Math.cos(-0.5), Math.sin(la / DEG), Math.cos(la / DEG) * Math.sin(-0.5)); return wall.geo.lon[wall._up[c]] < wall.geo.lon[c] ? -1 : 1; };
    const sT = side(12), sW = side(45);
    T.ok(sT === -sW, 'trades and westerlies blow opposite ways');
    const rng = s => s < 0 ? [-50, -15] : [15, 50];
    const tw = band(wall, 3, 22, ...rng(sT)), tl = band(wall, 3, 22, ...rng(-sT)), tlFlat = band(flat, 3, 22, ...rng(-sT));
    const ww = band(wall, 38, 52, ...rng(sW)), wlee = band(wall, 38, 52, ...rng(-sW)), wlFlat = band(flat, 38, 52, ...rng(-sW));
    say('L' + L, 'trades: windward', tw.toFixed(3), 'lee', tl.toFixed(3), '(lee without the wall', tlFlat.toFixed(3) + ')',
      '| westerlies: windward', ww.toFixed(3), 'lee', wlee.toFixed(3), '(without the wall', wlFlat.toFixed(3) + ')');
    T.ok(tw > 1.6 * tl, 'trades: windward ' + tw + ' vs lee ' + tl);
    T.ok(ww > 1.6 * wlee, 'westerlies: windward ' + ww + ' vs lee ' + wlee);
    T.ok(tl < 0.8 * tlFlat, 'the wall dries its lee (trades): ' + tl + ' vs ' + tlFlat);
    T.ok(wlee < 0.9 * wlFlat, 'the wall dries its lee (westerlies): ' + wlee + ' vs ' + wlFlat);
    // orographic rain: the windward slope is wetter than the same strip of the flat continent
    const slope = sT < 0 ? [-9, -2] : [2, 9];
    const up = band(wall, 3, 22, ...slope), upFlat = band(flat, 3, 22, ...slope);
    T.ok(up > upFlat + 0.1, 'windward slope ' + up + ' vs plain ' + upFlat);
    T.eq(finite(wall), null);
  }
});

T.test('belts: wet equator, dry subtropics, a wet equatorial coast ≈ 0.9', () => {
  const flat = squareWorld(5, 0);
  C.calibrate(flat);
  const side = flat.geo.lon[flat._up[flat.geo.cellAt(Math.cos(-0.5), 0.05, Math.sin(-0.5))]] < -0.5 ? -1 : 1;     // windward coast of the trades
  const coast = mean(flat, flat.moist, c => isLand(flat, c) && Math.abs(lat(flat, c)) < 8 && side * lon(flat, c) > 52);
  const eq = mean(flat, flat.moist, c => isLand(flat, c) && Math.abs(lat(flat, c)) < 8);
  const dry = mean(flat, flat.moist, c => isLand(flat, c) && Math.abs(lat(flat, c)) > 24 && Math.abs(lat(flat, c)) < 34);
  const mid = mean(flat, flat.moist, c => isLand(flat, c) && Math.abs(lat(flat, c)) > 40 && Math.abs(lat(flat, c)) < 52);
  say('square continent: equatorial windward coast', coast.toFixed(3), 'equatorial land', eq.toFixed(3), 'land at 24–34°', dry.toFixed(3), 'land at 40–52°', mid.toFixed(3));
  T.ok(coast > 0.8 && coast <= 1, 'a wet equatorial coast is about 0.9: ' + coast);
  T.ok(eq > 0.5, 'wet equatorial belt ' + eq);
  T.ok(dry < 0.2 && dry < 0.35 * eq, 'dry subtropics ' + dry);
  T.ok(mid > 1.5 * dry, 'the storm belt is wetter than the subtropics: ' + mid);
  // the same belts over the open sea of helper worlds, and on their land
  for (const seed of [1, 2, 3]) {
    const w = helperWorld({ seed });
    C.calibrate(w);
    const sea = (a0, a1) => mean(w, w.moist, c => !isLand(w, c) && Math.abs(lat(w, c)) >= a0 && Math.abs(lat(w, c)) < a1);
    const land = (a0, a1) => mean(w, w.moist, c => isLand(w, c) && Math.abs(lat(w, c)) >= a0 && Math.abs(lat(w, c)) < a1);
    say('helper seed', seed, 'land moist by band 0–10 / 22–34 / 38–52:', land(0, 10).toFixed(2), land(22, 34).toFixed(2), land(38, 52).toFixed(2),
      '| sea:', sea(0, 10).toFixed(2), sea(22, 34).toFixed(2), sea(38, 52).toFixed(2));
    T.ok(sea(0, 8) > 2 * sea(24, 32) && sea(40, 52) > 1.5 * sea(24, 32), 'sea: wet equator, dry belt, wet storm belt');
    T.ok(land(0, 10) > 0.45 && land(0, 10) > 1.6 * land(22, 34), 'land: wet equator vs dry subtropics');
    for (let c = 0; c < w.n; c++) T.ok(w.moist[c] >= 0 && w.moist[c] <= 1, 'moist in 0..1');
  }
});

T.test('coasts are wetter than deep interiors', () => {
  // outside the equatorial rain belt (which is wet right across a continent) and off the ice
  let sc = 0, kc = 0, sd = 0, kd = 0;
  for (const seed of [1, 2, 3, 4, 5, 6]) {
    const w = helperWorld({ seed, L: 5 });
    C.calibrate(w);
    const d = coastDist(w), ok = c => isLand(w, c) && w.ice[c] < 0.5 && Math.abs(lat(w, c)) > 12 && Math.abs(lat(w, c)) < 60;
    const coast = mean(w, w.moist, c => ok(c) && d[c] <= 2), deep = mean(w, w.moist, c => ok(c) && d[c] >= 4);
    say('helper seed', seed, 'moist within 2 hops of the sea', coast.toFixed(3), '4+ hops inland', deep.toFixed(3), 'ratio', (coast / deep).toFixed(2));
    T.ok(coast > 1.1 * deep, 'seed ' + seed + ': coast ' + coast + ' vs interior ' + deep);
    for (let c = 0; c < w.n; c++) if (ok(c)) { if (d[c] <= 2) { sc += w.moist[c]; kc++; } else if (d[c] >= 4) { sd += w.moist[c]; kd++; } }
  }
  T.ok(sc / kc > 1.3 * sd / kd, 'six worlds together: coast ' + sc / kc + ' vs interior ' + sd / kd);
  const flat = squareWorld(5, 0);
  C.calibrate(flat);
  const d = coastDist(flat), ok = c => isLand(flat, c) && flat.ice[c] < 0.5;
  T.ok(mean(flat, flat.moist, c => ok(c) && d[c] <= 2) > 1.15 * mean(flat, flat.moist, c => ok(c) && d[c] >= 8), 'square continent');
});

T.test('a planet with no sea is bone dry; a planet with no land does not break', () => {
  for (const w of [helperWorld({ seed: 1, sea: -9 }), uniformWorld(4, 2, 0), uniformWorld(4, 0, -9, 'water')]) {
    C.calibrate(w); C.update(w); C.stepIce(w);
    let max = 0; for (let c = 0; c < w.n; c++) if (w.moist[c] > max) max = w.moist[c];
    T.ok(max < 0.05, 'moist < 0.05 everywhere, max ' + max);
    T.eq(count(w, c => w.ice[c] > 0), 0, 'no snow without a sea');
    T.eq(finite(w), null);
    w.wet[5] = 0.6; C.update(w);
    T.near(w.moist[5], 0.6, 1e-6, 'the rain miracle still wets it');
  }
  const ocean = uniformWorld(4, -3, 0);
  C.calibrate(ocean); C.update(ocean); C.stepIce(ocean);
  stubLife();
  for (let y = 0; y < 30; y++) C.yearly(ocean);
  T.eq(finite(ocean), null);
  T.eq(ocean.vars.iceRef, 50, 'iceRef floor');
  T.near(ocean.seaLevel, 0, 1e-9, 'no land ice to melt: the sea stays');
  T.ok(mean(ocean, ocean.moist, c => Math.abs(lat(ocean, c)) < 8) > 0.5, 'rain over the equatorial sea');
});

T.test('fixed RAIN_REF: a new mountain does not change moist on the far side of the planet', () => {
  for (const seed of [1, 3]) {
    const w = helperWorld({ seed, L: 5 });
    C.calibrate(w);
    const before = Float32Array.from(w.moist);
    const c0 = H.findLand(w, c => Math.abs(lat(w, c)) > 8 && Math.abs(lat(w, c)) < 22 && w.elev[c] > 0.2 && w.elev[c] < 1 && before[c] > 0.3);
    T.ok(c0 >= 0);
    const list = [], k = w.geo.within(c0, 0.12, list);
    for (let i = 0; i < k; i++) if (isLand(w, list[i])) w.elev[list[i]] = Math.min(8, w.elev[list[i]] + 5);
    C.update(w);
    let near = 0, far = 0, farCells = 0;
    for (let c = 0; c < w.n; c++) {
      const a = w.geo.angle(c0, c), d = Math.abs(w.moist[c] - before[c]);
      if (a < 0.5) near = Math.max(near, d);
      if (a > 1.7) { far = Math.max(far, d); farCells++; }
    }
    say('seed', seed, 'largest change within 0.5 rad of the new mountain', near.toFixed(3), '; beyond 1.7 rad', far.toExponential(2), 'over', farCells, 'cells');
    T.ok(near > 0.1, 'the mountain changes its own surroundings: ' + near);
    T.ok(far < 1e-3, 'far side untouched: ' + far);
  }
});

T.test('moist adds wet and subtracts 0.25 × drought', () => {
  const w = helperWorld({ seed: 2, L: 5 });
  C.calibrate(w);
  const before = Float32Array.from(w.moist);
  const c0 = H.findLand(w, c => before[c] > 0.4 && before[c] < 0.6);
  T.ok(c0 >= 0);
  const d = DM.State.newDisaster(w, { type: 'drought', cell: c0, ang: 0.25, power: 1, dur: 8 });
  DM.State.newDisaster(w, { type: 'drought', cell: -1, ang: 0.3, power: 1 });          // junk entries are ignored
  DM.State.newDisaster(w, { type: 'drought', cell: 3, ang: NaN, power: 1 });
  DM.State.newDisaster(w, { type: 'storm', cell: c0, ang: 0.3, power: 1 });
  C.update(w);
  T.near(w.moist[c0], before[c0] - 0.25, 1e-5, 'centre of a full drought');
  let outside = 0, inside = 0;
  for (let c = 0; c < w.n; c++) {
    const a = w.geo.angle(c0, c), dm = before[c] - w.moist[c];
    if (a > 0.25 + 1e-6) outside = Math.max(outside, Math.abs(dm));
    else { T.ok(dm >= -1e-6 && dm <= 0.25 + 1e-5, 'drought only dries, by at most 0.25'); inside++; }
  }
  T.ok(inside > 30); T.ok(outside < 1e-6, 'nothing changes outside the drought');
  d.power = 0.4; C.update(w);
  T.near(w.moist[c0], before[c0] - 0.1, 1e-5, 'power scales it');
  w.disasters.length = 0;
  w.wet[c0] = 0.3; C.update(w);
  T.near(w.moist[c0], before[c0] + 0.3, 1e-5, 'wet adds');
  w.wet[c0] = 1; C.update(w);
  T.eq(w.moist[c0], 1, 'saturates at 1');
});

T.test('update: flags, determinism, no rng, idempotent', () => {
  const a = helperWorld({ seed: 4 }), b = helperWorld({ seed: 4 });
  clearFlags(a); a.need.climate = true;
  const rng = a.rng.s;
  C.update(a);
  T.eq(a.need.climate, false, 'clears need.climate'); T.eq(a.need.life, true, 'sets need.life'); T.eq(a.dirty.color, true, 'sets dirty.color');
  T.eq(a.need.hydro, true, 'the first rainfall asks the rivers to follow');
  T.eq(a.need.derive, false); T.eq(a.dirty.elev, false); T.eq(a.dirty.water, false);
  clearFlags(a);
  const m1 = Float32Array.from(a.moist), t1 = Float32Array.from(a.temp);
  C.update(a);
  T.eq(a.need.hydro, false, 'unchanged rainfall leaves the rivers alone');
  T.eq(Array.from(a.moist), Array.from(m1), 'idempotent moist'); T.eq(Array.from(a.temp), Array.from(t1), 'idempotent temp');
  a.wet.fill(0.5); C.update(a);
  T.eq(a.need.hydro, true, 'a rain miracle moves moist enough to refresh the rivers');
  a.wet.fill(0); C.update(a);
  C.update(b);
  T.eq(Array.from(b.moist), Array.from(m1), 'same seed, same rainfall'); T.eq(Array.from(b.temp), Array.from(t1));
  C.calibrate(a); C.stepIce(a); stubLife(); C.yearly(a);
  T.eq(a.rng.s, rng, 'the climate never draws from world.rng');
  const other = helperWorld({ seed: 5 });
  C.update(other);
  T.ok(Array.from(other.temp).join() !== Array.from(t1).join(), 'another seed, another world');
});

T.test('determinism: worlds simulated side by side (different sizes share the scratch buffers) do not disturb each other', () => {
  stubLife(0.6);
  const mk = (seed, L) => { const w = helperWorld({ seed, L }); C.calibrate(w); return w; };
  const year = (w, y) => {
    C.emit(w, 0.7); if (y === 30) C.addDust(w, 0.3);
    C.yearly(w);
    if (w.need.climate && y % 2 === 0) C.update(w);
    if (y % 5 === 0) C.stepIce(w);
  };
  const solo = mk(4, 4), a = mk(4, 4), b = mk(4, 4), other = mk(9, 3);
  H.capture(() => {
    for (let y = 1; y <= 120; y++) year(solo, y);
    for (let y = 1; y <= 120; y++) { year(a, y); year(other, y); year(b, y); }
  });
  for (const w of [a, b]) {
    T.eq(Array.from(w.temp), Array.from(solo.temp), 'temp'); T.eq(Array.from(w.moist), Array.from(solo.moist), 'moist'); T.eq(Array.from(w.ice), Array.from(solo.ice), 'ice');
    T.eq(w.seaLevel, solo.seaLevel); T.eq(w.co2, solo.co2); T.eq(w.globalTemp, solo.globalTemp); T.eq(w.dust, solo.dust);
    T.eq(JSON.stringify(w.vars), JSON.stringify(solo.vars), 'vars');
    T.eq(H.hash(w), H.hash(solo), 'world hash');
  }
  T.ok(solo.co2 > 50 && solo.globalTemp > 0.5, 'and something did happen: co2 ' + solo.co2);
});

// ------------------------------------------------------------------ ice
T.test('ice equilibrium: land 0 at −4 … 1 at −9, sea 0 at −2.5 … 1 at −6; stepIce 6 % (history) and 30 % (genesis)', () => {
  const w = helperWorld({ seed: 1 });
  C.update(w);                                                 // ice still 0: temp is the ice-free temperature
  const tFree = Float32Array.from(w.temp);
  const land = H.findLand(w, c => tFree[c] < -12), warm = H.findLand(w, c => tFree[c] > 5);
  let sea = -1;
  for (let c = 0; c < w.n && sea < 0; c++) if (!isLand(w, c) && Math.abs(lat(w, c)) > 80) sea = c;
  T.ok(land >= 0 && warm >= 0 && sea >= 0);
  T.eq(w.temp[sea], -2, 'polar sea is stored at the floor');
  clearFlags(w);
  C.stepIce(w);
  T.near(w.ice[land], 0.06, 1e-6, 'history: 6 % of the way'); T.near(w.ice[sea], 0.06, 1e-6, 'sea ice from the unclamped temperature');
  T.eq(w.ice[warm], 0);
  T.near(w.temp[land], tFree[land] - 4 * 0.06, 1e-4, 'albedo follows the ice');
  T.eq(w.dirty.color, true); T.eq(w.dirty.water, true, 'sea ice changed');
  C.stepIce(w);
  T.near(w.ice[land], 0.06 + 0.94 * 0.06, 1e-6);
  const g = helperWorld({ seed: 1, phase: 'life' });
  C.update(g); C.stepIce(g);
  T.near(g.ice[land], 0.30, 1e-6, 'genesis: 30 %');
  for (const phase of ['water', 'people']) { const x = helperWorld({ seed: 1, phase }); C.update(x); C.stepIce(x); T.near(x.ice[land], 0.30, 1e-6, phase); }
  const e = helperWorld({ seed: 1, phase: 'ended' }); C.update(e); C.stepIce(e); T.near(e.ice[land], 0.06, 1e-6, 'ended');
  // many steps reach the same equilibrium that calibrate sets directly
  for (let i = 0; i < 250; i++) C.stepIce(w);
  const cal = helperWorld({ seed: 1 }); C.calibrate(cal);
  let worst = 0; for (let c = 0; c < w.n; c++) worst = Math.max(worst, Math.abs(w.ice[c] - cal.ice[c]));
  T.ok(worst < 0.02, 'stepIce converges on the calibrated ice: ' + worst);
  clearFlags(w); C.stepIce(w);
  T.eq(w.dirty.color, false, 'nothing changed, nothing flagged'); T.eq(w.dirty.water, false);
  // the equilibrium itself, on the calibrated world (temp includes the −4 × ice albedo term)
  let full = 0, none = 0;
  for (let c = 0; c < cal.n; c++) {
    T.ok(cal.ice[c] >= 0 && cal.ice[c] <= 1);
    if (isLand(cal, c)) {
      if (cal.temp[c] < -9) { T.near(cal.ice[c], 1, 1e-4, 'land colder than −9 is ice'); full++; }
      else if (cal.temp[c] > -4) { T.near(cal.ice[c], 0, 1e-4, 'land warmer than −4 is bare'); none++; }
      else T.near(cal.ice[c], (-4 - cal.temp[c]) / 5, 1e-3, 'the ramp between');
    } else if (cal.ice[c] === 0) T.ok(cal.temp[c] >= -2, 'open sea');
  }
  T.ok(full > 100 && none > 1000, 'both kinds of land exist: ' + full + ', ' + none);
});

T.test('stepIce before any update, on a loaded world and at another grid size', () => {
  const w = helperWorld({ seed: 2, L: 4 });
  C.stepIce(w);                                               // no Climate.update has run on this world
  T.ok(count(w, c => w.ice[c] > 0) > 10, 'ice starts to form');
  T.eq(finite(w), null);
  const big = helperWorld({ seed: 2, L: 5 }); C.update(big); C.stepIce(big);
  const small = helperWorld({ seed: 3, L: 3 }); C.update(small); C.stepIce(small); C.calibrate(small);
  T.eq(finite(big), null); T.eq(finite(small), null);
  // after save / load the transient caches are gone and everything still works
  C.calibrate(w);
  const l = DM.State.deserialize(JSON.parse(JSON.stringify(DM.State.serialize(w))));
  T.eq(l._clim, undefined); T.eq(l._up, undefined);
  C.stepIce(l); C.update(l);
  T.ok(l._up instanceof Int32Array);
  let worst = 0; for (let c = 0; c < w.n; c++) worst = Math.max(worst, Math.abs(l.temp[c] - w.temp[c]));
  T.ok(worst < 0.5, 'a loaded world gets the same climate (elev and ice are quantized): ' + worst);
  T.near(l.vars.seaBase, w.vars.seaBase, 1e-12); T.near(l.vars.iceRef, w.vars.iceRef, 1e-9);
});

// ------------------------------------------------------------------ calibrate
T.test('calibrate: finite seaBase, iceRef ≥ 50, ice at its equilibrium, idempotent', () => {
  for (const [seed, L] of [[1, 5], [2, 5], [3, 4], [1, 3]]) {
    const w = helperWorld({ seed, L, sea: 0.3, phase: 'people' }); w.vars = {};
    clearFlags(w); w.need.climate = true;
    C.calibrate(w);
    T.eq(w.vars.seaBase, 0.3, 'seaBase = seaLevel'); T.near(w.seaLevel, 0.3, 1e-12);
    let landIce = 0; for (let c = 0; c < w.n; c++) if (isLand(w, c)) landIce += w.ice[c];
    say('seed', seed, 'L' + L, 'land ice', landIce.toFixed(1), 'iceRef', w.vars.iceRef.toFixed(1));
    T.ok(Number.isFinite(w.vars.iceRef) && w.vars.iceRef >= 50, 'iceRef ' + w.vars.iceRef);
    T.near(w.vars.iceRef, Math.max(50, landIce), 1e-3, 'iceRef = max(50, Σ land ice)');
    T.eq(w.need.climate, false); T.eq(w.need.life, true); T.eq(w.dirty.color, true);
    T.ok(count(w, c => w.moist[c] > 0.3) > 20, 'rainfall is filled in too');
    const ice = Float32Array.from(w.ice), temp = Float32Array.from(w.temp), moist = Float32Array.from(w.moist), ref = w.vars.iceRef;
    C.stepIce(w);
    let worst = 0; for (let c = 0; c < w.n; c++) worst = Math.max(worst, Math.abs(w.ice[c] - ice[c]));
    T.ok(worst < 1e-3, 'already at equilibrium: stepIce moves nothing (' + worst + ')');
    C.calibrate(w);
    T.eq(w.vars.seaBase, 0.3); T.near(w.vars.iceRef, ref, 1e-3, 'second calibrate: same iceRef');
    worst = 0; for (let c = 0; c < w.n; c++) worst = Math.max(worst, Math.abs(w.ice[c] - ice[c]), Math.abs(w.temp[c] - temp[c]), Math.abs(w.moist[c] - moist[c]));
    T.ok(worst < 1e-3, 'second calibrate changes nothing: ' + worst);
    T.eq(finite(w), null);
  }
  const bad = helperWorld({ seed: 1, L: 3 }); bad.vars = {}; bad.seaLevel = NaN;
  C.calibrate(bad);
  T.eq(bad.vars.seaBase, 0, 'a NaN sea level is repaired, not copied'); T.eq(bad.seaLevel, 0); T.ok(bad.vars.iceRef >= 50);
});

// ------------------------------------------------------------------ yearly
T.test('yearly does nothing but decay dust in genesis phases', () => {
  for (const phase of ['molten', 'water', 'life', 'people']) {
    for (const calibrated of [false, true]) {
      const w = helperWorld({ seed: 1, L: 4, sea: -2, phase });
      if (calibrated) C.calibrate(w); else { w.vars = {}; C.update(w); }
      w.co2 = 80; w.globalTemp = 1.5; w.dust = 0.5;
      const sea = w.seaLevel, ice = Float32Array.from(w.ice), vars = JSON.stringify(w.vars);
      stubLife();
      const ev = H.capture(() => { for (let y = 0; y < 60; y++) C.yearly(w); });
      T.eq(ev.length, 0, phase + ': no events');
      T.eq(w.seaLevel, sea, phase + ': sea untouched'); T.eq(w.co2, 80); T.eq(w.globalTemp, 1.5);
      T.near(w.dust, 0.5 * Math.pow(0.93, 60), 1e-9, 'dust × 0.93 per year');
      T.eq(Array.from(w.ice), Array.from(ice)); T.eq(JSON.stringify(w.vars), vars, phase + ': vars untouched');
    }
  }
  const w = helperWorld({ seed: 1, L: 3, phase: 'water' }); w.dust = 1e-4; C.yearly(w);
  T.eq(w.dust, 0, 'dust settles to exactly 0');
});

T.test('yearly never touches the sea while vars.seaBase is missing: 400 years, finite sea level', () => {
  for (const sea of [0, -9, 0.7]) {
    const w = helperWorld({ seed: 2, L: 4, sea, phase: 'history' });
    w.vars = {};                                              // a world "created with phase history and empty vars"
    w.co2 = 120; w.dust = 0.3; w.globalTemp = 2;
    stubLife();
    const ev = H.capture(() => runYears(w, 400));
    T.eq(w.seaLevel, sea, 'sea level untouched'); T.ok(Number.isFinite(w.seaLevel));
    T.eq(w.vars.seaBase, undefined, 'yearly does not calibrate behind Sim\'s back');
    T.eq(w.co2, 120, 'and leaves the greenhouse alone'); T.eq(w.globalTemp, 2);
    T.eq(w.dust, 0, 'dust still settles');
    T.eq(milestones(ev).length, 0);
    T.eq(finite(w), null);
  }
  for (const bad of [NaN, Infinity, null, 'x']) {
    const w = helperWorld({ seed: 2, L: 3 }); w.vars = { seaBase: bad }; w.co2 = 50;
    stubLife();
    for (let y = 0; y < 50; y++) C.yearly(w);
    T.eq(w.seaLevel, 0, 'seaBase ' + bad + ': sea untouched'); T.eq(w.co2, 50);
  }
});

T.test('yearly: a calibrated, undisturbed world keeps its sea level', () => {
  const w = helperWorld({ seed: 3, L: 4, sea: 0.2 });
  C.calibrate(w);
  stubLife();
  clearFlags(w);
  const ev = H.capture(() => runYears(w, 400));
  say('undisturbed: sea after 400 years', w.seaLevel.toFixed(4), '(base 0.2), co2', w.co2, 'globalTemp', w.globalTemp);
  T.eq(w.seaLevel, 0.2, 'no drift without a cause, not even in the last bit (the HUD prints the difference)');
  T.eq(w.co2, 0); T.eq(w.globalTemp, 0); T.eq(milestones(ev).length, 0);
  T.eq(w.need.derive, false, 'and never asks for a re-derive');
  T.eq(finite(w), null);
});

T.test('yearly: sustained emissions reach the documented equilibrium; ice shrinks; the sea rises slowly and stays in bounds', () => {
  const w = helperWorld({ seed: 1, L: 5 });
  C.calibrate(w);
  stubLife(0.6);
  const base = w.vars.seaBase, ice0 = C.landIce(w), perYear = 40 * 0.0025 * Math.sqrt(20000 / 1000);      // 40 cities of 20,000
  let maxStep = 0, prev = w.seaLevel, lo = Infinity, hi = -Infinity;
  const ev = H.capture(() => runYears(w, 1500, () => {
    C.emit(w, perYear);
    const d = Math.abs(w.seaLevel - prev); if (d > maxStep) maxStep = d; prev = w.seaLevel;
    lo = Math.min(lo, w.seaLevel); hi = Math.max(hi, w.seaLevel);
  }));
  const ms = milestones(ev);
  say('40 cities × 20,000 for 1,500 years: co2', w.co2.toFixed(1), 'anomaly', C.anomaly(w).toFixed(2), 'land ice', ice0.toFixed(0), '→', C.landIce(w).toFixed(0),
    'sea', base, '→', w.seaLevel.toFixed(3), 'largest yearly step', maxStep.toFixed(4), 'milestones:', ms.join(', '));
  T.near(w.co2, 81.3, 3, 'co2 ≈ 80'); T.near(C.anomaly(w), 2.6, 0.15, 'anomaly ≈ +2.6');
  T.near(w.co2, perYear / (0.005 * 1.1), 1.5, 'equilibrium = emissions ÷ (0.005 × (0.5 + veg))');
  T.ok(C.landIce(w) < 0.9 * ice0, 'ice shrinks: ' + ice0 + ' → ' + C.landIce(w));
  T.ok(w.seaLevel > base + 0.1, 'the sea rises: ' + w.seaLevel);
  T.ok(lo >= base - 0.3 - 1e-9 && hi <= base + 0.6 + 1e-9, 'within seaBase −0.3 … +0.6');
  T.ok(maxStep <= 0.005 + 1e-9, 'at most 0.005 per year: ' + maxStep);
  T.eq(ms.filter(m => m === 'warming 1').length, 1); T.eq(ms.filter(m => m === 'warming 2').length, 1);
  T.eq(ms.filter(m => m === 'warming 3').length, 0);
  T.eq(new Set(ms).size, ms.length, 'each milestone fires once: ' + ms.join(', '));
  if (w.seaLevel >= base + 0.2) T.eq(ms.filter(m => m === 'sea-rise 0.2').length, 1);
  T.eq(finite(w), null);
});

T.test('yearly: CO₂ uptake, the 30-year lag and the vegetation term', () => {
  const w = helperWorld({ seed: 1, L: 3 });
  C.calibrate(w);
  w.co2 = 100;
  stubLife(0.5);
  C.yearly(w);
  T.near(w.co2, 100 - 100 * 0.005 * 1.0, 1e-9, 'uptake 0.5 % × (0.5 + veg)');
  T.near(w.globalTemp, 3 * Math.log2(1 + w.co2 / 100) / 30, 1e-9, 'one thirtieth of the gap');
  stubLife(1); w.co2 = 100; C.yearly(w);
  T.near(w.co2, 100 - 100 * 0.005 * 1.5, 1e-9, 'a green world takes up more');
  T.stub(DM, 'Life', undefined); w.co2 = 100; C.yearly(w);
  T.near(w.co2, 99.5, 1e-9, 'without DM.Life: veg 0.5 assumed');
  T.stub(DM, 'Life', { cover: () => ({ veg: NaN }) }); w.co2 = 100; C.yearly(w);
  T.near(w.co2, 99.5, 1e-9, 'a NaN cover is not believed');
  stubLife(0.5); w.co2 = 300; w.globalTemp = 0;
  for (let y = 0; y < 30; y++) { w.co2 = 300; C.yearly(w); }
  T.near(w.globalTemp, 6 * (1 - Math.pow(29 / 30, 30)), 0.05, 'about 63 % of the way after 30 years');
  w.co2 = NaN; w.globalTemp = NaN; w.dust = NaN; C.yearly(w);
  T.eq(finite(w), null, 'poisoned scalars are repaired');
});

T.test('yearly: sea level follows land ice and warmth, 0.005 per year, clamped; re-derive flags every 0.03', () => {
  const T0 = DM.TUNE.climate;
  // hot: all land ice gone and co2 held at the cap
  const hot = helperWorld({ seed: 1, L: 5 });
  C.calibrate(hot); stubLife(0.5);
  const base = hot.vars.seaBase, ref = hot.vars.iceRef;
  T.ok(ref > 50, 'this world has real ice caps: ' + ref);
  let flagged = 0, maxStep = 0, prev = hot.seaLevel;
  const ev = H.capture(() => {
    for (let y = 1; y <= 400; y++) {
      hot.co2 = 300; hot.ice.fill(0);
      clearFlags(hot);
      C.yearly(hot);
      maxStep = Math.max(maxStep, Math.abs(hot.seaLevel - prev)); prev = hot.seaLevel;
      if (y === 1) T.near(hot.seaLevel, base + 0.005, 1e-9, 'first year: +0.005');
      if (hot.need.derive) {
        flagged++;
        T.ok(hot.need.hydro && hot.need.climate && hot.dirty.water && hot.dirty.color && hot.dirty.objects, 'all six flags together');
      }
    }
  });
  const top = base + 0.35 + 0.04 * hot.globalTemp;
  say('no ice, co2 300: sea', base, '→', hot.seaLevel.toFixed(3), '(target', top.toFixed(3) + ', cap', (base + 0.6).toFixed(2) + ');', flagged, 're-derive requests');
  T.near(hot.seaLevel, top, 0.01, 'base + 0.35 (ice) + 0.04 × globalTemp');
  T.ok(hot.seaLevel <= base + 0.6 + 1e-9); T.ok(maxStep <= T0.seaRate + 1e-9);
  const moved = hot.seaLevel - base;
  T.ok(flagged >= Math.floor(moved / 0.04) && flagged <= Math.ceil(moved / 0.03), 'one request per 0.03–0.035 of rise: ' + flagged + ' for ' + moved);
  const ms = milestones(ev);
  T.eq(ms.filter(m => m === 'sea-rise 0.2').length, 1); T.eq(ms.filter(m => m === 'sea-rise 0.4').length, 1);
  T.eq(ms.filter(m => m.indexOf('warming') === 0).join(), 'warming 1,warming 2,warming 3,warming 4', 'warming milestones in order, once each');
  T.eq(new Set(ms).size, ms.length);
  // cold: the land ice has more than doubled
  const cold = helperWorld({ seed: 1, L: 5 });
  C.calibrate(cold); stubLife(0.5);
  const ev2 = H.capture(() => { for (let y = 1; y <= 200; y++) { for (let c = 0; c < cold.n; c++) cold.ice[c] = 1; C.yearly(cold); } });
  say('land ice everywhere: sea', base, '→', cold.seaLevel.toFixed(3));
  T.near(cold.seaLevel, base - 0.25, 1e-6, 'ice doubled: −0.25'); T.ok(cold.seaLevel >= base - 0.3);
  T.eq(milestones(ev2).join(), 'sea-fall -0.2', 'one sea-fall milestone');
  // half of the land ice gone: + 0.35 × 0.5
  const half = helperWorld({ seed: 1, L: 5 });
  C.calibrate(half); stubLife(0.5);
  const keep = Float32Array.from(half.ice);
  for (let y = 1; y <= 100; y++) { for (let c = 0; c < half.n; c++) half.ice[c] = isLand(half, c) ? keep[c] * 0.5 : keep[c]; C.yearly(half); }
  // (a little more than 0.175: the rising sea floods some iced lowland, which then no longer counts as land ice)
  T.ok(half.seaLevel >= base + 0.175 - 1e-6 && half.seaLevel < base + 0.195, 'r = 0.5 → +0.175: ' + half.seaLevel);
  // a world with almost no ice at calibration (iceRef at its floor) must not start to drown at once
  const bare = squareWorld(4, 0);
  C.calibrate(bare); stubLife(0.5);
  T.eq(bare.vars.iceRef, 50, 'floor'); T.ok(C.landIce(bare) < 50);
  for (let y = 0; y < 100; y++) C.yearly(bare);
  T.near(bare.seaLevel, 0, 1e-9, 'r = 1 at calibration even below the iceRef floor');
  // hand-calibrated world without iceRef (the helper sets only seaBase): the first yearly finishes the calibration,
  // so the ice settling afterwards is not mistaken for an ice age (it used to drain the sea by 0.25)
  for (const started of [false, true]) {
    const hand = helperWorld({ seed: 2, L: 4 });
    T.eq(hand.vars.iceRef, undefined); T.eq(count(hand, c => hand.ice[c] > 0), 0, 'no ice yet');
    if (started) { C.update(hand); for (let i = 0; i < 3; i++) C.stepIce(hand); }
    const cal = helperWorld({ seed: 2, L: 4 }); C.calibrate(cal);
    hand.need.climate = false;
    C.yearly(hand);
    T.near(hand.vars.iceRef, cal.vars.iceRef, 1e-3, 'same iceRef as Climate.calibrate gives');
    T.ok(hand.vars.iceRef > 60, 'a real reference, not the floor: ' + hand.vars.iceRef);
    let worst = 0; for (let c = 0; c < hand.n; c++) worst = Math.max(worst, Math.abs(hand.ice[c] - cal.ice[c]), Math.abs(hand.temp[c] - cal.temp[c]));
    T.ok(worst < 1e-3, 'ice and temp are the calibrated ones: ' + worst);
    T.eq(hand.need.climate, true, 'and the rainfall is asked to follow');
    T.eq(hand.vars.seaBase, 0, 'the hand-set seaBase is kept');
    runYears(hand, 300);
    say('hand-calibrated world (seaBase only), 300 years: sea', hand.seaLevel.toFixed(5), 'iceRef', hand.vars.iceRef.toFixed(1));
    T.ok(Math.abs(hand.seaLevel) < 1e-3, 'the sea stays where it was: ' + hand.seaLevel);
    T.eq(finite(hand), null);
  }
});

T.test('yearly: need.climate once the anomaly has moved 0.25 °C since the last update', () => {
  const w = helperWorld({ seed: 1, L: 4 });
  C.calibrate(w); stubLife(0.5);
  w.co2 = 100;
  let first = -1;
  for (let y = 1; y <= 12 && first < 0; y++) { w.co2 = 100; C.yearly(w); if (w.need.climate) first = y; else T.ok(w.globalTemp < 0.25); }
  T.ok(first > 0 && w.globalTemp >= 0.25 && w.globalTemp < 0.36, 'flagged in year ' + first + ' at ' + w.globalTemp);
  C.update(w);
  T.eq(w.need.climate, false);
  w.co2 = 0; C.yearly(w);
  T.eq(w.need.climate, false, 'a small change is not flagged');
  w.dust = 0.05; w.need.climate = false; C.yearly(w);
  T.eq(w.need.climate, true, 'dust moves the anomaly too');
});

T.test('milestones: cooling, ice-age and thaw fire once per winter', () => {
  const w = helperWorld({ seed: 1, L: 4 });
  C.calibrate(w); stubLife(0.5);
  let ev = H.capture(() => { C.addDust(w, 0.25); for (let y = 0; y < 40; y++) C.yearly(w); });        // a meteor: −2.25 °C
  T.eq(milestones(ev).join(), 'cooling -2,thaw -1', 'meteor winter: ' + milestones(ev).join());
  ev = H.capture(() => { for (let y = 0; y < 40; y++) C.yearly(w); });
  T.eq(milestones(ev).length, 0, 'nothing more once it is over');
  ev = H.capture(() => { C.addDust(w, 0.6); for (let y = 0; y < 60; y++) C.yearly(w); });             // a nuclear winter: −5.4 °C
  T.eq(milestones(ev).join(), 'cooling -2,ice-age -4,thaw -1', 'a later, deeper winter is announced again');
  ev = H.capture(() => { C.addDust(w, 0.3); for (let y = 0; y < 3; y++) C.yearly(w); C.addDust(w, 0.3); for (let y = 0; y < 60; y++) C.yearly(w); });
  T.eq(milestones(ev).join(), 'cooling -2,ice-age -4,thaw -1', 'two blows in one winter: each line once');
  for (const e of ev) T.ok(Number.isFinite(e.payload.value));
  T.eq(finite(w), null);
});

// ------------------------------------------------------------------ robustness
T.test('robustness: odd and poisoned worlds never get NaN or out-of-range cells', () => {
  const rng = DM.U.rng(20261118), pick = a => a[rng.int(0, a.length - 1)];
  const phases = ['molten', 'water', 'life', 'people', 'history', 'ended'];
  stubLife(0.4);
  for (let i = 0; i < 40; i++) {
    const w = DM.State.create({ seed: rng.next(), L: rng.int(0, 4) });
    const flat = rng.chance(0.3), base = rng.range(-12, 20);
    for (let c = 0; c < w.n; c++) {
      w.elev[c] = flat ? base : rng.range(-12, 20);
      if (rng.chance(0.02)) w.elev[c] = NaN;
      w.ice[c] = rng.chance(0.15) ? pick([NaN, -1, 2, 0.5, 1]) : 0;
      w.wet[c] = rng.chance(0.15) ? pick([NaN, 5, -1, 0.4]) : 0;
    }
    w.seaLevel = pick([-12, -9, -3, 0, 0, 2, 19, 25, NaN]);
    w.phase = pick(phases);
    if (rng.chance(0.5)) w.vars.seaBase = pick([0, -4, 3, NaN, null]);
    if (rng.chance(0.3)) w.vars.iceRef = pick([0, -1, NaN, 1e-9, 500]);
    w.co2 = pick([0, 50, NaN, -5, 1e9]); w.dust = pick([0, 0.4, NaN, 7, -2]); w.globalTemp = pick([0, 3, NaN, -40, 1e6]);
    for (let k = rng.int(0, 3); k > 0; k--) {
      DM.State.newDisaster(w, { type: 'drought', cell: pick([0, w.n - 1, -1, w.n, 3.5, NaN]), ang: pick([0.2, 4, 0, -1, NaN, Infinity]), power: pick([1, 0.5, NaN, 9, -1]) });
    }
    w.disasters.push(null);
    H.capture(() => {                                        // milestone payloads are checked by T.watchEvents
      C.update(w); C.stepIce(w);
      for (let y = 0; y < 3; y++) C.yearly(w);
      C.calibrate(w); C.stepIce(w); C.addDust(w, rng.range(-1, 1)); C.emit(w, rng.range(-50, 50));
      for (let y = 0; y < 5; y++) C.yearly(w);
      C.update(w);
    });
    for (let c = 0; c < w.n; c++) {
      if (!Number.isFinite(w.temp[c])) throw new Error('world ' + i + ': temp[' + c + '] = ' + w.temp[c]);
      if (!(w.moist[c] >= 0 && w.moist[c] <= 1)) throw new Error('world ' + i + ': moist[' + c + '] = ' + w.moist[c]);
      if (!(w.ice[c] >= 0 && w.ice[c] <= 1)) throw new Error('world ' + i + ': ice[' + c + '] = ' + w.ice[c]);
    }
    // (in the genesis phases yearly touches nothing but dust, so a poisoned globalTemp is only read safely, not repaired)
    const hist = w.phase === 'history' || w.phase === 'ended';
    for (const k of hist ? ['seaLevel', 'co2', 'dust', 'globalTemp'] : ['seaLevel', 'co2', 'dust']) T.ok(Number.isFinite(w[k]), 'world ' + i + ': ' + k + ' = ' + w[k]);
    T.ok(Number.isFinite(C.anomaly(w)) && Number.isFinite(C.datum(w)), 'anomaly and datum are always finite');
    T.ok(w.co2 >= 0 && w.co2 <= 300 && w.dust >= 0 && w.dust <= 1, 'co2 / dust in range');
    T.ok(Number.isFinite(w.vars.seaBase) && w.vars.iceRef >= 50, 'calibrated');
    T.ok(Math.abs(w.seaLevel - w.vars.seaBase) <= 0.6 + 1e-9, 'sea within its bounds');
  }
});

T.test('robustness: a broken yearly tuning value cannot turn the sea level (or co2, dust, globalTemp, iceRef) into NaN', () => {
  const tune = DM.TUNE.climate, keep = Object.assign({}, tune);
  stubLife(0.5);
  try {
    for (const k of ['iceRefMin', 'lag', 'dustDecay', 'uptake', 'sens', 'seaRate', 'seaTherm', 'seaIceGain', 'seaIceLoss', 'seaLo', 'seaHi', 'co2Max', 'seaStep', 'anomStep']) {
      for (const bad of [NaN, 0, -1]) {
        Object.assign(tune, keep); tune[k] = bad;
        for (const w of [squareWorld(3, 0), helperWorld({ seed: 3, L: 3 })]) {    // one with almost no land ice (iceRef on its floor), one with ice caps
          w.vars = {}; w.co2 = 90; w.dust = 0.3; w.globalTemp = 1;
          H.capture(() => {
            C.calibrate(w);
            for (let y = 1; y <= 12; y++) { C.emit(w, 1); if (y === 6) w.ice.fill(1); C.yearly(w); }
            C.update(w); C.stepIce(w);
          });
          for (const f of ['seaLevel', 'co2', 'dust', 'globalTemp']) T.ok(Number.isFinite(w[f]), k + ' = ' + bad + ': ' + f + ' = ' + w[f]);
          T.ok(Number.isFinite(w.vars.iceRef) && w.vars.iceRef > 0, k + ' = ' + bad + ': iceRef = ' + w.vars.iceRef);
          T.ok(Math.abs(w.seaLevel - w.vars.seaBase) <= 12 * 0.005 + 1e-9, k + ' = ' + bad + ': the sea still creeps at most 0.005 a year: ' + w.seaLevel);
          T.eq(finite(w), null, k + ' = ' + bad);
        }
      }
    }
  } finally { Object.assign(tune, keep); }
  const w = squareWorld(3, 0); C.calibrate(w);
  T.eq(w.vars.iceRef, 50, 'tuning restored');
});

T.test('the wettest places approach 1 smoothly; every grid size gets the same climate', () => {
  const stats = [];
  for (const L of [4, 5]) {
    const w = helperWorld({ seed: 2, L });
    C.calibrate(w);
    const land = count(w, c => isLand(w, c) && w.ice[c] < 0.5), top = count(w, c => isLand(w, c) && w.ice[c] < 0.5 && w.moist[c] >= 0.995);
    T.ok(top < 0.04 * land, 'L' + L + ': few cells sit on the ceiling: ' + top + ' of ' + land);
    const b = (a0, a1) => mean(w, w.moist, c => isLand(w, c) && Math.abs(lat(w, c)) >= a0 && Math.abs(lat(w, c)) < a1);
    stats.push([b(0, 12), b(12, 24), b(24, 36), b(36, 54), mean(w, w.temp, c => isLand(w, c))]);
  }
  say('land moist by band (0–12, 12–24, 24–36, 36–54) and mean land temp: L4', stats[0].map(v => v.toFixed(2)).join(' '), '| L5', stats[1].map(v => v.toFixed(2)).join(' '));
  for (let i = 0; i < 4; i++) T.near(stats[0][i], stats[1][i], 0.1, 'band ' + i + ' reads the same at L = 4 and L = 5');
  T.near(stats[0][4], stats[1][4], 1.5, 'so does the temperature');
});

T.test('retuning the winds rebuilds the wind table; other tuning is read at call time', () => {
  const w = squareWorld(4, 0);
  C.calibrate(w);
  const up = w._up, m0 = Float32Array.from(w.moist), tune = DM.TUNE.climate, keep = { merid: tune.merid, rainRef: tune.rainRef, lapse: tune.lapse };
  try {
    tune.rainRef = keep.rainRef * 2; C.update(w);
    T.ok(w._up === up, 'same winds');
    const c = H.findLand(w, k => m0[k] > 0.2 && m0[k] < 0.6);
    T.near(w.moist[c], m0[c] / 2, 0.02, 'RAIN_REF is read at call time');
    tune.rainRef = keep.rainRef;
    tune.merid = 0; C.update(w);
    T.ok(w._up !== up, 'new wind table');
    tune.merid = keep.merid; C.update(w);
    T.eq(Array.from(w.moist), Array.from(m0), 'and back');
  } finally { Object.assign(tune, keep); }
});

// ------------------------------------------------------------------ the whole genesis story
T.test('genesis to history: comets, ice, calibration, then centuries with a steady sea', () => {
  const w = helperWorld({ seed: 2, L: 5, sea: -9, phase: 'water' });
  w.vars = {};
  stubLife(0.6);
  C.update(w);
  for (let i = 0; i < 6; i++) C.stepIce(w);
  T.eq(count(w, c => w.ice[c] > 0), 0, 'dry rock: no ice yet');
  let sea = -6;
  while (sea < 0) {                                           // each comet raises the sea; the scheduler updates and steps the ice
    w.seaLevel = sea; w.need.climate = true;
    C.update(w); C.stepIce(w); C.stepIce(w);
    C.yearly(w);                                              // Sim does not call it in genesis; if anyone does, the sea is safe
    T.eq(w.seaLevel, sea, 'yearly leaves the genesis sea alone');
    T.eq(C.datum(w), 0);
    sea = Math.min(0, sea + Math.max(0.12, Math.min(1, 0.2 * (0.6 - sea))));
  }
  w.seaLevel = 0; C.update(w);
  w.phase = 'life'; for (let i = 0; i < 12; i++) C.stepIce(w);
  const landCells = count(w, c => isLand(w, c)), iced = count(w, c => isLand(w, c) && w.ice[c] > 0.5);
  say('end of genesis: iced land', (100 * iced / landCells).toFixed(1) + '%', 'mean land temp', mean(w, w.temp, c => isLand(w, c)).toFixed(1));
  T.ok(iced > 0 && iced < 0.3 * landCells, 'ice caps, not an ice ball: ' + iced + ' of ' + landCells);
  T.eq(w.vars.seaBase, undefined);
  w.phase = 'history';
  C.calibrate(w);                                             // Sim.setPhase('history')
  T.eq(w.vars.seaBase, 0); T.ok(w.vars.iceRef >= 50);
  const iceAtStart = C.landIce(w);
  runYears(w, 300);
  say('300 undisturbed years: sea', w.seaLevel.toFixed(4), 'land ice', iceAtStart.toFixed(0), '→', C.landIce(w).toFixed(0));
  T.eq(w.seaLevel, 0, 'the sea neither drains nor floods');
  T.eq(C.landIce(w), iceAtStart, 'the ice caps hold exactly');
  T.eq(finite(w), null);
});

// ------------------------------------------------------------------ cost
T.test('cost at L = 6: update well inside 12 ms (asserted at 5× for a shared machine)', () => {
  const w = helperWorld({ seed: 1, L: 6 });
  let t = process.hrtime.bigint();
  C.calibrate(w);
  const first = Number(process.hrtime.bigint() - t) / 1e6;
  const ts = [];
  for (let i = 0; i < 11; i++) { t = process.hrtime.bigint(); C.update(w); ts.push(Number(process.hrtime.bigint() - t) / 1e6); }
  ts.sort((a, b) => a - b);
  t = process.hrtime.bigint();
  for (let i = 0; i < 20; i++) C.stepIce(w);
  const ice = Number(process.hrtime.bigint() - t) / 1e6 / 20;
  stubLife();
  t = process.hrtime.bigint();
  for (let i = 0; i < 50; i++) C.yearly(w);
  const yr = Number(process.hrtime.bigint() - t) / 1e6 / 50;
  console.log('    climate timing at L = 6: update median ' + ts[5].toFixed(2) + ' ms (min ' + ts[0].toFixed(2) + ', max ' + ts[10].toFixed(2) + '), first calibrate ' +
    first.toFixed(1) + ' ms (builds the wind table), stepIce ' + ice.toFixed(3) + ' ms, yearly ' + yr.toFixed(3) + ' ms');
  T.ok(ts[5] < 60, 'update median ' + ts[5] + ' ms');
  T.ok(ice < 5 && yr < 5);
  T.eq(finite(w), null);
});

// ------------------------------------------------------------------ maps (not a test)
//   CLIMATE_MAPS=1 node tests/climate.test.js
// writes shots/climate-*.png: elevation, temperature, rainfall (sea dimmed) and ice for seeds 1 and 2 on the real terrain
// (the helper's noise planet while terrain.js is not there), the same world 4.6 °C warmer, the hand-built continent with
// and without its mountain wall, and biomes when hydro.js and life.js are there too. North is up and the maps are seen
// from inside the globe: geo.basis east (where the trade winds come from) is on the LEFT.
if (process.env.CLIMATE_MAPS) {
  const M = require('../tools/mapimg.js');
  const real = !!(DM.Terrain && DM.Terrain.generate);
  const bands = w => {
    const m = new Array(9).fill(0), t = new Array(9).fill(0), k = new Array(9).fill(0);
    for (let c = 0; c < w.n; c++) if (isLand(w, c)) { const b = Math.min(8, Math.floor(Math.abs(lat(w, c)) / 10)); m[b] += w.moist[c]; t[b] += w.temp[c]; k[b]++; }
    return 'land moist by 10° band ' + m.map((s, i) => k[i] ? (s / k[i]).toFixed(2) : '--').join(' ') + ' | temp ' + t.map((s, i) => k[i] ? (s / k[i]).toFixed(0) : '--').join(' ');
  };
  const iceMap = (w, file) => M.custom(w, file, c => {
    const i = w.ice[c];
    return isLand(w, c) ? [90 + 165 * i, 120 + 135 * i, 80 + 175 * i] : [20 + 190 * i, 50 + 180 * i, 110 + 135 * i];
  });
  for (const seed of [1, 2]) {
    let w;
    if (real) { w = DM.State.create({ seed, L: 6 }); DM.Terrain.generate(w); w.seaLevel = 0; w.heat = 0; w.phase = 'history'; }
    else w = helperWorld({ seed, L: 6 });
    C.calibrate(w);
    console.log((real ? 'terrain' : 'helper') + ' seed ' + seed + ': ' + bands(w));
    M.elev(w, 'shots/climate-elev-seed' + seed + '.png');
    M.field(w, 'temp', 'shots/climate-temp-seed' + seed + '.png', -30, 32);
    M.field(w, 'moist', 'shots/climate-moist-seed' + seed + '.png', 0, 1, { dimSea: true });
    iceMap(w, 'shots/climate-ice-seed' + seed + '.png');
    if (real && DM.Terrain.derive && DM.Hydro && DM.Life && DM.Life.classify) {
      try {
        DM.Terrain.derive(w); C.update(w); DM.Hydro.update(w); DM.Life.classify(w);
        M.biome(w, 'shots/climate-biome-seed' + seed + '.png', { rivers: false });
      } catch (e) { console.log('biome map skipped: ' + e.message); }
    }
    if (seed === 1) {                                         // 800 years with co2 held at 192
      const warm = DM.State.deserialize(JSON.parse(JSON.stringify(DM.State.serialize(w)))), life = DM.Life;
      T.stub(DM, 'Life', { cover: () => ({ land: 0.4, veg: 0.6, forest: 0.3 }) });
      C.update(warm);
      for (let y = 1; y <= 800; y++) { warm.co2 = 192; C.yearly(warm); if (warm.need.climate && y % 2 === 0) C.update(warm); if (y % 5 === 0) C.stepIce(warm); }
      C.update(warm);
      T.stub(DM, 'Life', life);                               // the real one back for the next biome map
      console.log('  the same world at anomaly ' + C.anomaly(warm).toFixed(2) + ': sea ' + warm.seaLevel.toFixed(3) + ', land ice × ' + (C.landIce(warm) / warm.vars.iceRef).toFixed(3) + '; ' + bands(warm));
      iceMap(warm, 'shots/climate-ice-warm-seed1.png');
    }
  }
  for (const [name, wall] of [['wall', 5], ['flat', 0]]) {
    const w = squareWorld(6, wall);
    C.calibrate(w);
    console.log(name + ' continent: ' + bands(w));
    M.field(w, 'moist', 'shots/climate-' + name + '-moist.png', 0, 1, { dimSea: true });
    if (wall) M.elev(w, 'shots/climate-wall-elev.png');
  }
}
