// DEMIURGE — tests/life.test.js : DM.Life (SPEC-SIM §3.5, BUILD.md A5).
//
//   node tests/life.test.js                  run the tests; prints the biome histogram and the measured numbers
//   LIFE_MAPS=1 node tests/life.test.js      also writes shots/life-*.png (biomes, habitability, the spreading
//                                            front frame by frame and as an arrival-time map, a fire scar) from helper
//                                            worlds and, when terrain.js / climate.js / hydro.js exist, from the real
//                                            pipeline, for which it also prints biome shares, the spread of the green
//                                            and the sizes of lightning fires with and without a drought
//
// Only the foundation, schema.js and life.js are needed: every other sim module is stubbed away below, so a
// half-written neighbour cannot change the outcome.
'use strict';
const T = require('../tools/t.js');
const DM = require('../tools/load.js')({ only: ['life'] });
const H = require('./helpers/world.js');          // (its loader pulls in whatever else exists ...)
const REAL = { Terrain: DM.Terrain, Climate: DM.Climate, Hydro: DM.Hydro };     // ... kept only for the optional maps
for (const k of ['Names', 'Terrain', 'Climate', 'Hydro', 'Path', 'Civ', 'Society', 'Units', 'Powers', 'Chronicle', 'Sim']) T.stub(DM, k, undefined);
T.watchEvents();

const Life = DM.Life, B = DM.BIOME, TL = DM.TUNE.life;
const LAKE = 0.03;
const say = (...a) => console.log(...a);
const isLand = (w, c) => w.elev[c] >= w.seaLevel;
const fmt = (x, d) => x.toFixed(d === undefined ? 2 : d);

// ------------------------------------------------------------------ world builders
// an all-land plain carrying one biome everywhere: the lattice the fire and growth rules are measured on
function plain(L, o) {
  o = o || {};
  const w = H.makeWorld({ L, seed: o.seed || 1, land: 1, basics: false });
  w.elev.fill(o.elev === undefined ? 0.8 : o.elev);
  w.seaLevel = 0; w.vars.seaBase = 0;
  H.fillBasics(w);
  w.ice.fill(0); w.water.fill(0); w.wet.fill(0); w.scorch.fill(0); w.fire.fill(0); w.fauna.fill(0);
  w.temp.fill(o.temp === undefined ? 15 : o.temp);
  w.moist.fill(o.moist === undefined ? 0.55 : o.moist);
  w.fert.fill(o.fert === undefined ? 0.6 : o.fert);
  w.fresh.fill(255);
  w.biome.fill(o.biome === undefined ? B.FOREST : o.biome);
  w.veg.fill(o.veg === undefined ? 0 : o.veg);
  Life.rebuild(w);
  return w;
}
// a helper planet with Life's own biomes; `bare` strips the stand-in vegetation
function planet(L, seed, bare) {
  const w = H.makeWorld({ L, seed });
  Life.classify(w);
  if (bare) { w.veg.fill(0); w.fauna.fill(0); }
  return w;
}
function advance(w, ticks, parts) {
  for (let i = 0; i < ticks; i++) { w.tick++; Life.burn(w); Life.step(w, w.tick % parts, parts); }
}
// connected land components by flood fill: { id: Int32Array (−1 sea), size: [] }
function landIds(w) {
  const id = new Int32Array(w.n).fill(-1), g = w.geo, q = new Int32Array(w.n), size = [];
  for (let s = 0; s < w.n; s++) {
    if (id[s] >= 0 || !isLand(w, s)) continue;
    let h = 0, t = 0;
    q[t++] = s; id[s] = size.length;
    while (h < t) {
      const c = q[h++];
      for (let j = 0; j < g.nbrN[c]; j++) { const m = g.nbr[6 * c + j]; if (id[m] < 0 && isLand(w, m)) { id[m] = size.length; q[t++] = m; } }
    }
    size.push(t);
  }
  return { id, size };
}
function biggest(ids) { let b = 0; for (let i = 1; i < ids.size.length; i++) if (ids.size[i] > ids.size[b]) b = i; return b; }
// a well-vegetated cell near the middle of a landmass
function heart(w, ids, k) {
  const p = w.geo.pos;
  let x = 0, y = 0, z = 0;
  for (let c = 0; c < w.n; c++) if (ids.id[c] === k) { x += p[3 * c]; y += p[3 * c + 1]; z += p[3 * c + 2]; }
  let best = -1, bd = -Infinity;
  for (let c = 0; c < w.n; c++) {
    if (ids.id[c] !== k || !(Life.capacity(w, c) > 0.3)) continue;
    const d = p[3 * c] * x + p[3 * c + 1] * y + p[3 * c + 2] * z;
    if (d > bd) { bd = d; best = c; }
  }
  return best;
}
// vegetation of one landmass: { plant, bare, cover = Σ min(veg, cap) / Σ cap }
function landCover(w, ids, k) {
  let sc = 0, sv = 0, plant = 0, bare = 0;
  for (let c = 0; c < w.n; c++) {
    if (ids.id[c] !== k) continue;
    const cap = Life.capacity(w, c);
    if (!(cap > TL.plantMin)) continue;
    plant++; sc += cap; sv += Math.min(cap, w.veg[c]);
    if (w.veg[c] === 0) bare++;
  }
  return { plant, bare, cover: sc > 0 ? sv / sc : 0 };
}
function burning(w) { let k = 0; for (let c = 0; c < w.n; c++) if (w.fire[c] > 0) k++; return k; }
function firesMatch(w) {
  const F = w._fires;
  if (!F || !(F.cells instanceof Int32Array)) return 'no _fires';
  const seen = new Set();
  for (let i = 0; i < F.n; i++) { const c = F.cells[i]; if (!(w.fire[c] > 0)) return 'listed cell ' + c + ' is not burning'; if (seen.has(c)) return 'cell ' + c + ' listed twice'; seen.add(c); }
  if (seen.size !== burning(w)) return '_fires.n = ' + F.n + ' but ' + burning(w) + ' cells burn';
  return null;
}
// burn one fire to the end; returns { size (cells scorched), ticks, peak (most cells burning at once) }
function burnOut(w, c, limit) {
  if (!Life.ignite(w, c)) return null;
  let ticks = 0, peak = 1;
  while (w._fires.n > 0 && ticks < (limit || 20000)) { w.tick++; Life.burn(w); ticks++; if (w._fires.n > peak) peak = w._fires.n; }
  let size = 0;
  for (let i = 0; i < w.n; i++) if (w.scorch[i] > 0) size++;
  return { size, ticks, peak };
}
function fireSizes(L, veg, moist, trials, seed) {
  const w = plain(L, { veg, moist }), pick = DM.U.rng(seed || 4242), sizes = [];
  let peak = 0;
  for (let i = 0; i < trials; i++) {
    w.veg.fill(veg); w.scorch.fill(0); w.fire.fill(0); Life.rebuild(w);
    const r = burnOut(w, pick.int(0, w.n - 1));
    sizes.push(r.size); if (r.peak > peak) peak = r.peak;
  }
  sizes.sort((a, b) => a - b);
  const q = p => sizes[Math.min(sizes.length - 1, Math.floor(p * sizes.length))];
  return { n: w.n, sizes, mean: sizes.reduce((a, b) => a + b, 0) / sizes.length, median: q(0.5), p90: q(0.9), max: sizes[sizes.length - 1], peak,
    over: x => sizes.filter(s => s > x).length / sizes.length };
}

// ================================================================== API
T.test('API: every documented function exists and nothing runs at load time', () => {
  for (const f of ['classify', 'capacity', 'step', 'burn', 'ignite', 'seed', 'bloom', 'fill', 'habitability', 'rebuild', 'cover']) T.eq(typeof Life[f], 'function', 'Life.' + f);
  T.ok(TL && Array.isArray(TL.cap) && TL.cap.length === 16, 'TUNE.life with the capacity table');
  T.eq(TL.cap, [0, 0, 0.05, 0.05, 0.35, 0.3, 0.9, 1, 0.75, 0.15, 0, 0.03, 0, 0.7, 0, 0], 'biomeCapacity table');
});

// ================================================================== biomes
// Five helper worlds. The helper's stand-in climate has no arid land at all (the driest 5 % of its land has
// moist ≈ 0.37), so two of the five are dried out by scaling that stand-in; everything else is makeWorld's.
const FIVE = [
  { name: 'temperate', seed: 1 },
  { name: 'warm seas', seed: 2, land: 0.3, temp: 5 },
  { name: 'arid', seed: 3, land: 0.6, dry: 0.3, temp: 3 },
  { name: 'cold', seed: 4, temp: -6, seaIce: true },
  { name: 'steppe', seed: 5, land: 0.5, dry: 0.55 },
];
function fiveWorld(sp) {
  const w = H.makeWorld({ L: 5, seed: sp.seed, land: sp.land, basics: false });
  w.globalTemp = sp.temp || 0; H.fillBasics(w); w.globalTemp = 0;
  if (sp.dry) for (let c = 0; c < w.n; c++) if (isLand(w, c)) w.moist[c] *= sp.dry;
  if (sp.seaIce) for (let c = 0; c < w.n; c++) if (!isLand(w, c) && Math.abs(w.geo.lat[c]) > 1.15) w.ice[c] = 0.8;
  return w;
}

T.test('classify: sea biomes only on sea cells, land biomes only on land; depth and ice rules', () => {
  let seaIce = 0;
  for (const sp of FIVE) {
    const w = fiveWorld(sp);
    w.dirty.color = false; w.dirty.objects = false;
    Life.classify(w);
    T.ok(w.dirty.color && w.dirty.objects, 'classify sets dirty.color and dirty.objects');
    for (let c = 0; c < w.n; c++) {
      const b = w.biome[c], seaB = b === B.SEA || b === B.SHALLOWS || b === B.SEAICE;
      if (isLand(w, c)) { if (seaB) T.ok(false, sp.name + ': land cell ' + c + ' classified ' + DM.BIOME_NAMES[b]); }
      else {
        if (!seaB) T.ok(false, sp.name + ': sea cell ' + c + ' classified ' + DM.BIOME_NAMES[b]);
        const want = w.ice[c] > 0.5 ? B.SEAICE : (w.seaLevel - w.elev[c] > 0.6 ? B.SEA : B.SHALLOWS);
        if (b !== want) T.ok(false, sp.name + ': sea cell ' + c + ' is ' + DM.BIOME_NAMES[b] + ', expected ' + DM.BIOME_NAMES[want]);
        if (b === B.SEAICE) seaIce++;
      }
    }
  }
  T.ok(seaIce > 20, 'sea ice appears where the sea is iced over (' + seaIce + ' cells)');
});

T.test('classify: every land biome appears across 5 helper worlds, in believable proportions', () => {
  const tot = new Array(16).fill(0);
  let land = 0;
  say('  biome shares, % of land (5 helper worlds, L = 5):');
  say('  ' + 'world'.padEnd(11) + DM.BIOME_NAMES.slice(2, 15).map(s => s.slice(0, 6).padStart(7)).join(''));
  for (const sp of FIVE) {
    const w = fiveWorld(sp);
    // a lava field and a burnt-out patch (the only way BARREN exists), on the first world
    if (sp.seed === 1) {
      const c0 = H.findLand(w, c => w.temp[c] > 10 && w.coast[c] > 2), buf = [];
      const k = w.geo.ring(c0, 2, buf);
      for (let i = 0; i < k; i++) if (isLand(w, buf[i])) { if (i % 2) w.lava[buf[i]] = 0.6; else { w.scorch[buf[i]] = 0.95; w.veg[buf[i]] = 0; } }
    }
    Life.classify(w);
    const h = new Array(16).fill(0);
    let l = 0;
    for (let c = 0; c < w.n; c++) if (isLand(w, c)) { h[w.biome[c]]++; l++; }
    say('  ' + sp.name.padEnd(11) + h.slice(2, 15).map(v => fmt(100 * v / l, 1).padStart(7)).join(''));
    for (let b = 0; b < 16; b++) tot[b] += h[b];
    land += l;
  }
  say('  ' + 'all five'.padEnd(11) + tot.slice(2, 15).map(v => fmt(100 * v / land, 1).padStart(7)).join(''));
  for (let b = 2; b <= 14; b++) T.ok(tot[b] > 0, DM.BIOME_NAMES[b] + ' never appears');
  const share = b => tot[b] / land;
  const woods = share(B.FOREST) + share(B.RAINFOREST) + share(B.TAIGA), open = share(B.GRASSLAND) + share(B.SAVANNA);
  T.ok(woods > 0.15 && woods < 0.7, 'forest + rainforest + taiga share ' + fmt(woods));
  T.ok(open > 0.04 && open < 0.5, 'grassland + savanna share ' + fmt(open));
  T.ok(share(B.DESERT) > 0.01 && share(B.DESERT) < 0.35, 'desert share ' + fmt(share(B.DESERT)));
  T.ok(share(B.ICE) > 0.05 && share(B.ICE) < 0.45, 'ice share ' + fmt(share(B.ICE)));
  T.ok(share(B.TUNDRA) > 0.01 && share(B.TUNDRA) < 0.2, 'tundra share ' + fmt(share(B.TUNDRA)));
  T.ok(share(B.ROCK) + share(B.SNOWPEAK) < 0.25, 'rock + snow share');
  T.ok(share(B.BEACH) < 0.08 && share(B.WETLAND) < 0.1 && share(B.BARREN) < 0.01, 'beach / wetland / barren stay rare');
});

T.test('classify: the land rules one by one', () => {
  const w = plain(4), sk = w.geo.step / TL.refStep;     // slopes are judged at the reference cell size
  w.coast.fill(255);
  const cases = [];
  let next = 20;
  const put = (want, f) => { const c = next; next += 7; f(c); cases.push({ c, want }); };
  // the Whittaker table, well away from every threshold (the jitter is at most ±0.075 moist, ±1.5 °C)
  const tm = (t, m) => c => { w.temp[c] = t; w.moist[c] = m; };
  put(B.RAINFOREST, tm(25, 0.95));
  put(B.FOREST, tm(25, 0.57));
  put(B.SAVANNA, tm(25, 0.30));
  put(B.DESERT, tm(25, 0.03));
  put(B.DESERT, tm(13, 0.01));
  put(B.GRASSLAND, tm(13, 0.22));
  put(B.FOREST, tm(13, 0.60));
  put(B.TAIGA, tm(5, 0.60));
  put(B.TAIGA, tm(0, 0.60));
  put(B.TUNDRA, tm(0, 0.08));
  put(B.TUNDRA, tm(-8, 0.50));
  // special ground
  put(B.BARREN, c => { w.lava[c] = 0.5; });
  put(B.BARREN, c => { w.scorch[c] = 0.9; w.veg[c] = 0; });
  put(B.FOREST, c => { w.scorch[c] = 0.9; w.veg[c] = 0.3; w.moist[c] = 0.6; });      // scorched but still vegetated: not barren
  put(B.ICE, c => { w.ice[c] = 0.9; w.temp[c] = -12; });
  put(B.SNOWPEAK, c => { w.elev[c] = 4; w.temp[c] = -6; });
  put(B.SNOWPEAK, c => { w.elev[c] = 4; w.temp[c] = -12; w.ice[c] = 1; });           // a glaciated summit reads as a peak
  put(B.ROCK, c => { w.elev[c] = 5.2; w.temp[c] = 4; });                              // very high
  put(B.ROCK, c => { w.slope[c] = 2.2 * sk; });                                       // very steep
  put(B.FOREST, c => { w.slope[c] = 0.8 * sk; w.moist[c] = 0.6; });                   // hilly, not rock
  put(B.BEACH, c => { w.elev[c] = 0.04; w.coast[c] = 1; });
  put(B.FOREST, c => { w.elev[c] = 0.04; w.coast[c] = 3; w.moist[c] = 0.4; });        // low but not at the sea: no beach
  put(B.WETLAND, c => { w.elev[c] = 0.3; w.moist[c] = 0.8; w.fresh[c] = 0; });
  put(B.FOREST, c => { w.elev[c] = 0.3; w.moist[c] = 0.8; w.fresh[c] = 9; });         // wet lowland far from water
  put(B.FOREST, c => { w.elev[c] = 0.3; w.moist[c] = 0.8; w.fresh[c] = 0; w.slope[c] = 0.8 * sk; });   // not flat
  Life.classify(w);
  for (const k of cases) T.eq(DM.BIOME_NAMES[w.biome[k.c]], DM.BIOME_NAMES[k.want], 'case at cell ' + k.c);
});

T.test('classify: static and deterministic (same world twice, two worlds of one seed)', () => {
  const a = planet(4, 3), b = planet(4, 3), first = Uint8Array.from(a.biome);
  Life.classify(a);
  for (let c = 0; c < a.n; c++) { if (a.biome[c] !== first[c]) T.ok(false, 'reclassifying changed cell ' + c); if (a.biome[c] !== b.biome[c]) T.ok(false, 'worlds differ at ' + c); }
});

// ================================================================== capacity
T.test('capacity: biomeCapacity × (0.6 + 0.4 fert) × (1 − farm) × (1 − urban) × (1 − 0.8 rad); 0 on sea, lakes, ice', () => {
  const w = plain(4, { fert: 0.5 });
  T.near(Life.capacity(w, 100), 0.9 * 0.8, 1e-6, 'plain forest');
  w.farm[101] = 0.25; w.urban[101] = 0.2; w.rad[101] = 0.5;
  T.near(Life.capacity(w, 101), 0.9 * 0.8 * 0.75 * 0.8 * 0.6, 1e-6, 'farm, town and fallout');
  w.fert[102] = 1.5; w.biome[102] = B.RAINFOREST;
  T.near(Life.capacity(w, 102), 1, 1e-6, 'never above 1');
  for (let b = 0; b < 16; b++) { w.biome[103] = b; T.near(Life.capacity(w, 103), TL.cap[b] * 0.8, 1e-6, DM.BIOME_NAMES[b]); }
  w.water[104] = 0.5; T.eq(Life.capacity(w, 104), 0, 'lake cell');
  w.ice[105] = 0.8; T.eq(Life.capacity(w, 105), 0, 'ice');
  w.lava[106] = 0.4; T.eq(Life.capacity(w, 106), 0, 'lava');
  w.elev[107] = -1; T.eq(Life.capacity(w, 107), 0, 'sea');
  w.farm[108] = 1; T.eq(Life.capacity(w, 108), 0, 'fully farmed');
  for (const bad of [-1, w.n, 1.5, NaN, undefined, null, 'x']) T.eq(Life.capacity(w, bad), 0, 'bad cell ' + bad);
});

// ================================================================== vegetation
T.test('vegetation: a barren planet stays barren for 2,000 ticks', () => {
  const w = planet(4, 2, true);
  w.vars.lifeBoost = 25; w.phase = 'life';
  advance(w, 2000, 2);
  let veg = 0, fauna = 0;
  for (let c = 0; c < w.n; c++) { veg += w.veg[c]; if (isLand(w, c)) fauna += w.fauna[c]; }
  T.eq(veg, 0, 'no vegetation without a seed');
  T.eq(fauna, 0, 'no land animals without vegetation');
  T.eq(H.checkFinite(w), null);
});

T.test('seed: veg ≥ 0.35 on plantable land within ang, nothing else touched', () => {
  const w = planet(4, 1, true), ids = landIds(w), c0 = heart(w, ids, biggest(ids));
  w.dirty.color = false; w.dirty.objects = false;
  const planted = Life.seed(w, c0, 0.2);
  let inside = 0, cnt = 0;
  for (let c = 0; c < w.n; c++) {
    const near = w.geo.angle(c0, c) <= 0.2, plantable = Life.capacity(w, c) > TL.plantMin;
    if (near && plantable) { inside++; if (!(w.veg[c] >= 0.35)) T.ok(false, 'plantable cell ' + c + ' inside the brush has veg ' + w.veg[c]); }
    else if (w.veg[c] !== 0) T.ok(false, 'cell ' + c + ' (' + (near ? 'not plantable' : 'outside the brush') + ') was planted');
    if (w.veg[c] > 0) cnt++;
  }
  T.ok(inside >= 10, 'the brush covers land (' + inside + ' cells)');
  T.eq(planted, cnt, 'returns the number of cells planted');
  T.ok(w.dirty.color && w.dirty.objects, 'dirty flags');
  T.eq(Life.seed(w, c0, 0.2), 0, 'seeding twice plants nothing new');
  w.veg[c0] = 0.9;
  Life.seed(w, c0, 0.05);
  T.near(w.veg[c0], 0.9, 1e-6, 'never lowers vegetation');
  T.eq(Life.seed(w, -1, 0.2), 0, 'bad cell');
  const sea = H.findLand(w, () => false);
  T.eq(sea, -1);
});

T.test('vegetation: from one seed the green crosses its continent (boost 25, 2 slices) and keeps spreading', () => {
  const w = planet(5, 2, true), ids = landIds(w), k = biggest(ids), c0 = heart(w, ids, k);
  w.vars.lifeBoost = 25; w.phase = 'life';
  Life.seed(w, c0, 0.12);
  const at = {};
  let line = '  spread on a continent of ' + ids.size[k] + ' cells (L = 5):', prev = 0;
  for (let t = 1; t <= 480; t++) {
    w.tick++; Life.burn(w); Life.step(w, w.tick % 2, 2);
    if (t === 20 || t % 60 === 0) {
      const s = at[t] = landCover(w, ids, k);
      line += ' t' + t + ' ' + fmt(100 * s.cover, 1) + '%';
      T.ok(s.cover >= prev - 1e-6, 'cover never falls back (tick ' + t + ')');
      prev = s.cover;
    }
  }
  say(line + '; bare plantable cells left ' + at[480].bare + ' of ' + at[480].plant);
  T.ok(at[20].cover < 0.25, 'it is a wave, not a switch: ' + fmt(at[20].cover) + ' after 20 ticks');
  T.ok(at[240].cover >= 0.25, 'at least 25 % of capacity within 240 ticks (got ' + fmt(at[240].cover) + ')');
  T.ok(at[120].cover > at[60].cover && at[240].cover > at[120].cover, 'still spreading');
  T.ok(at[480].cover > 0.97, 'the continent ends up green (' + fmt(at[480].cover) + ')');
  T.ok(at[480].bare <= 0.005 * at[480].plant, 'every plantable region is reached (' + at[480].bare + ' bare cells left)');
  T.eq(H.checkFinite(w), null);
});

T.test('vegetation at game size (L = 6): one seed greens its continent within the life phase, nothing left out', () => {
  const w = planet(6, 2, true), ids = landIds(w), k = biggest(ids), c0 = heart(w, ids, k);
  w.vars.lifeBoost = 25; w.phase = 'life';
  const planted = Life.seed(w, c0, 0.12);
  const at = {};
  let line = '  spread on a continent of ' + ids.size[k] + ' cells (L = 6, ' + planted + ' cells seeded):';
  for (let t = 1; t <= 480; t++) {
    w.tick++; Life.burn(w); Life.step(w, w.tick % 2, 2);
    if (t === 30 || t === 60 || t === 120 || t === 240 || t === 480) { at[t] = landCover(w, ids, k); line += ' t' + t + ' ' + fmt(100 * at[t].cover, 1) + '%'; }
  }
  say(line + '; bare plantable cells left ' + at[480].bare + ' of ' + at[480].plant + '; world cover ' + fmt(100 * Life.cover(w).veg, 1) + '%');
  T.ok(at[30].cover < 0.25, 'a wave the player can watch: ' + fmt(at[30].cover) + ' after 30 ticks');
  T.ok(at[240].cover >= 0.25, 'at least 25 % of capacity within 240 ticks (got ' + fmt(at[240].cover) + ')');
  T.ok(at[60].cover < at[120].cover && at[120].cover < at[240].cover && at[240].cover <= at[480].cover + 1e-6, 'keeps spreading');
  T.ok(at[480].cover > 0.97, 'the continent ends up green (' + fmt(at[480].cover) + ')');
  T.ok(at[480].bare <= 0.01 * at[480].plant, 'every plantable region is reached (' + at[480].bare + ' of ' + at[480].plant + ' cells still bare)');
});

// The front on a featureless plain: a deterministic rule would draw a hexagon. Measure how far the edge of the
// green wanders from a circle around the seed.
function frontShape(w, c0) {
  const g = w.geo, rs = [];
  let green = 0;
  for (let c = 0; c < w.n; c++) {
    if (!(w.veg[c] > 0)) continue;
    green++;
    for (let j = 0; j < g.nbrN[c]; j++) if (w.veg[g.nbr[6 * c + j]] === 0) { rs.push(g.angle(c0, c) / g.step); break; }
  }
  const mean = rs.reduce((a, b) => a + b, 0) / rs.length;
  const sd = Math.sqrt(rs.reduce((a, b) => a + (b - mean) * (b - mean), 0) / rs.length);
  return { green, mean, cv: sd / mean, min: Math.min(...rs), max: Math.max(...rs) };
}
T.test('vegetation: the spreading front is ragged, not a circle', () => {
  const w = plain(5, { moist: 0.55 }), c0 = 100;
  w.vars.lifeBoost = 25;
  Life.seed(w, c0, 0.12);
  const s0 = frontShape(w, c0);
  advance(w, 40, 2);
  const s = frontShape(w, c0);
  say('  front on a uniform plain after 40 ticks: ' + s.green + ' green cells, edge ' + fmt(s.min, 1) + ' … ' + fmt(s.max, 1) + ' hops from the seed (mean ' + fmt(s.mean, 1) + '), radial spread ' + fmt(100 * s.cv, 0) + ' % (the seed disc itself: ' + fmt(100 * s0.cv, 0) + ' %)');
  T.ok(s.green > 3 * s0.green && s.green < 0.6 * w.n, 'the green grew but has not filled the planet (' + s.green + ' cells)');
  T.ok(s.cv > 0.12, 'radial spread of the front ' + fmt(s.cv) + ' (a circle is ≈ 0.03)');
  T.ok(s.max > 1.8 * s.min, 'lobes and bays: the edge runs from ' + fmt(s.min, 1) + ' to ' + fmt(s.max, 1) + ' hops out');
});

T.test('vegetation: the green advances one ring per pass at most, whatever the order of the cells', () => {
  const w = plain(4), g = w.geo, c0 = 11;              // (the south pole; the drift below always picks cell 0, far away)
  w.vars.lifeBoost = 1;
  w.rng = { f: () => 0 };                             // every sprouting roll succeeds
  w.veg[c0] = 0.5;
  const hops = new Int32Array(w.n).fill(-1), q = [c0];
  hops[c0] = 0;
  for (let h = 0; h < q.length; h++) { const c = q[h]; for (let j = 0; j < g.nbrN[c]; j++) { const m = g.nbr[6 * c + j]; if (hops[m] < 0) { hops[m] = hops[c] + 1; q.push(m); } } }
  for (let pass = 1; pass <= 6; pass++) {
    w.tick++; Life.step(w, 0, 1);
    for (let c = 0; c < w.n; c++) if ((w.veg[c] > 0) !== (hops[c] <= pass)) T.ok(false, 'pass ' + pass + ': cell ' + c + ' at ' + hops[c] + ' hops is ' + (w.veg[c] > 0 ? 'green' : 'bare'));
  }
  T.ok(w.veg[q[1]] > 0.06 && w.veg[q[1]] < 0.2, 'a sprout starts small and grows');
});

T.test('vegetation: life crosses ice and lakes on land (seed pressure over dead ground), so no pocket is left out', () => {
  // a pocket of good ground walled in by a closed ring of dead cells three deep; drifting seeds switched off, so the
  // only way in is the seed pressure the dead ground hands on
  const run = (kind, airDecay) => {
    const w = plain(5), g = w.geo, c0 = 2000, hops = new Int32Array(w.n).fill(-1), q = [c0];
    hops[c0] = 0;
    for (let h = 0; h < q.length; h++) { const c = q[h]; for (let j = 0; j < g.nbrN[c]; j++) { const m = g.nbr[6 * c + j]; if (hops[m] < 0) { hops[m] = hops[c] + 1; q.push(m); } } }
    const pocket = [];
    let wall = 0, start = -1;
    for (let c = 0; c < w.n; c++) {
      if (hops[c] <= 5) pocket.push(c);
      else if (hops[c] <= 8) { wall++; if (kind === 'ice') w.ice[c] = 1; else w.water[c] = 0.5; }
      else if (hops[c] === 12 && start < 0) start = c;
    }
    T.stub(TL, 'driftPicks', 0);
    if (airDecay !== undefined) T.stub(TL, 'airDecay', airDecay);
    w.vars.lifeBoost = 25;
    Life.seed(w, start, 0.06);
    let first = -1;
    for (let t = 1; t <= 400; t++) {
      w.tick++; Life.step(w, w.tick % 2, 2);
      if (first < 0) for (const c of pocket) if (w.veg[c] > 0) { first = t; break; }
    }
    let green = 0, onWall = 0;
    for (const c of pocket) if (w.veg[c] > 0) green++;
    for (let c = 0; c < w.n; c++) if (hops[c] > 5 && hops[c] <= 8 && w.veg[c] > 0) onWall++;
    return { first, green, pocket: pocket.length, wall, onWall };
  };
  for (const kind of ['ice', 'lake']) {
    const r = run(kind);
    say('  pocket of ' + r.pocket + ' cells behind a 3-cell ring of ' + kind + ' (' + r.wall + ' cells): first sprout inside at tick ' + r.first + ', ' + r.green + ' green after 400 ticks');
    T.ok(r.first > 0, 'life gets across the ' + kind);
    T.eq(r.green, r.pocket, 'and fills the pocket');
    T.eq(r.onWall, 0, 'nothing grows on the ' + kind + ' itself');
  }
  const blocked = run('ice', 0);
  T.eq(blocked.green, 0, 'with the seed pressure switched off the ring is a real barrier');
});

T.test('vegetation: sprouting needs an established neighbour; lifeBoost scales the pace', () => {
  const w = plain(4), nb = w.geo.nbr[6 * 200];
  w.veg[nb] = 0.04;                                  // below 0.05 and far below half of its capacity: not established
  w.vars.lifeBoost = 0;                              // (freeze growth so it stays that way)
  for (let i = 0; i < 400; i++) { w.tick++; Life.step(w, 0, 1); }
  T.eq(w.veg[200], 0, 'a thin neighbour does not seed the cell');
  // pace: years until a seeded plain is half green, boost 1 against boost 4
  const half = boost => {
    const p = plain(4); p.vars.lifeBoost = boost; Life.seed(p, 300, 0.25);
    for (let t = 1; t <= 4000; t++) { p.tick++; Life.step(p, p.tick % 8, 8); if (t % 4 === 0 && Life.cover(p).veg >= 0.5) return t / 4; }
    return Infinity;
  };
  const y1 = half(1), y4 = half(4);
  say('  half cover of a plain from one seed: ' + y1 + ' years at boost 1, ' + y4 + ' years at boost 4');
  T.ok(y4 < 0.6 * y1, 'boost 4 is clearly faster (' + y4 + ' vs ' + y1 + ' years)');
});

T.test('vegetation: drifting seeds cross water within 12 hops (veg 0.08), and no further', () => {
  const build = (band) => {
    const w = plain(4), g = w.geo, c0 = 700, hops = new Int32Array(w.n).fill(-1), buf = [];
    const cnt = g.ring(c0, band, buf);
    // BFS order gives the hop distance; recompute it explicitly
    const q = [c0]; hops[c0] = 0;
    for (let h = 0; h < q.length; h++) { const c = q[h]; if (hops[c] >= band) continue; for (let j = 0; j < g.nbrN[c]; j++) { const m = g.nbr[6 * c + j]; if (hops[m] < 0) { hops[m] = hops[c] + 1; q.push(m); } } }
    T.eq(cnt, q.length);
    const island = [];
    for (let c = 0; c < w.n; c++) {
      if (hops[c] >= 0 && hops[c] <= 3) island.push(c);
      else if (hops[c] > 3 && hops[c] <= band) w.elev[c] = -1;        // a strait all round
    }
    Life.fill(w, 1);
    for (const c of island) w.veg[c] = 0;
    w.vars.lifeBoost = 1;
    return { w, island };
  };
  // a strait 5 cells wide: every island cell is within 12 hops of the mainland forest
  const near = build(8);
  let first = -1, year = -1;
  for (let t = 1; t <= 4 * 600 && first < 0; t++) {
    near.w.tick++; Life.step(near.w, near.w.tick % 8, 8);
    for (const c of near.island) if (near.w.veg[c] > 0) { first = c; year = t / 4; break; }
  }
  T.ok(first >= 0, 'a seed reaches the island');
  T.near(near.w.veg[first], 0.08, 1e-6, 'a drifted seedling starts at 0.08');
  for (let t = 0; t < 4 * 300; t++) { near.w.tick++; Life.step(near.w, near.w.tick % 8, 8); }
  let green = 0;
  for (const c of near.island) if (near.w.veg[c] > 0.3) green++;
  say('  island behind a 5-cell strait: first seedling in year ' + year + ', ' + green + ' of ' + near.island.length + ' cells wooded 300 years later');
  T.ok(green > 0.8 * near.island.length, 'the island greens over (' + green + ' of ' + near.island.length + ')');
  // a strait 17 cells wide: out of reach
  const far = build(20);
  for (let t = 0; t < 4 * 600; t++) { far.w.tick++; Life.step(far.w, far.w.tick % 8, 8); }
  for (const c of far.island) if (far.w.veg[c] !== 0) T.ok(false, 'an island 17 hops out was seeded');
});

T.test('vegetation: drift runs on the boosted clock; overland forerunners are thinned, jumps over water are not', () => {
  // A disc of bare ground 6 hops wide inside a forest, cut off by a ring 3 cells wide that is either sea (every
  // seedling in the disc crossed water) or bare land (the same seedlings would be forerunners of an overland front).
  // Growth and sprouting are switched off, so the only thing that happens is the drift itself. Mean of 6 runs.
  const RUNS = 6;
  const count = (ring, boost, ticks, seaBoost) => {
    let k = 0, cells = 0, land = 0;
    for (let r = 0; r < RUNS; r++) {
      const w = plain(4), g = w.geo, c0 = 1500, hops = new Int32Array(w.n).fill(-1), q = [c0];
      hops[c0] = 0;
      for (let h = 0; h < q.length; h++) { const c = q[h]; for (let j = 0; j < g.nbrN[c]; j++) { const m = g.nbr[6 * c + j]; if (hops[m] < 0) { hops[m] = hops[c] + 1; q.push(m); } } }
      Life.fill(w, 1);
      const disc = [];
      land = w.n;
      for (let c = 0; c < w.n; c++) {
        if (hops[c] <= 6) { disc.push(c); w.veg[c] = 0; }
        else if (hops[c] <= 9) { w.veg[c] = 0; if (ring === 'sea') { w.elev[c] = -1; land--; } }
      }
      T.stub(TL, 'grow', 0); T.stub(TL, 'spread', 0);
      if (seaBoost !== undefined) T.stub(TL, 'driftSeaBoost', seaBoost);
      w.vars.lifeBoost = boost; w.rng = DM.U.rng(900 + r);
      for (let t = 0; t < ticks; t++) { w.tick++; Life.step(w, w.tick % 2, 2); }
      for (const c of disc) if (w.veg[c] > 0) { k++; if (Math.abs(w.veg[c] - 0.08) > 1e-6) T.ok(false, 'a seedling has veg ' + w.veg[c]); }
      cells = disc.length;
    }
    return { k: k / RUNS, cells, land };
  };
  // 8 picks per year of the boosted clock, each on a random land cell: the share of the disc hit after `ticks`
  const predict = (r, rate, ticks) => r.cells * (1 - Math.exp(-8 * 0.25 * rate * ticks / r.land));
  const sea = count('sea', 25, 20), land = count('land', 25, 20);
  const expSea = predict(sea, 25, 20), expLand = predict(land, Math.pow(25, 0.3), 20);
  say('  drift into a bare disc of ' + sea.cells + ' cells in 20 ticks at boost 25 (L = 4): ' + fmt(sea.k, 1) + ' seedlings across a strait (8 picks per boosted year predict ' + fmt(expSea, 1) + '), ' + fmt(land.k, 1) + ' across bare land (thinned to boost ^ 0.3: ' + fmt(expLand, 1) + ')');
  T.ok(Math.abs(sea.k - expSea) < 0.2 * expSea, 'over water: ' + fmt(sea.k, 1) + ' seedlings, expected about ' + fmt(expSea, 1));
  T.ok(Math.abs(land.k - expLand) < 0.4 * expLand + 1, 'overland: ' + fmt(land.k, 1) + ' seedlings, expected about ' + fmt(expLand, 1));
  T.ok(land.k < 0.3 * sea.k, 'overland forerunners are much rarer (' + fmt(land.k, 1) + ' against ' + fmt(sea.k, 1) + ')');
  // TUNE.life.driftSeaBoost paces the jumps over water the same way (the balance owner's knob)
  const slow = count('sea', 25, 20, 0.3), expSlow = predict(slow, Math.pow(25, 0.3), 20);
  T.ok(Math.abs(slow.k - expSlow) < 0.4 * expSlow + 1, 'driftSeaBoost 0.3: ' + fmt(slow.k, 1) + ' seedlings across the strait, expected about ' + fmt(expSlow, 1));
  // at boost 1 there is one rule: 8 picks a year wherever the source is
  const sea1 = count('sea', 1, 500), land1 = count('land', 1, 500), exp1 = predict(sea1, 1, 500), exp1l = predict(land1, 1, 500);
  say('  the same at boost 1 over 125 years: ' + fmt(sea1.k, 1) + ' across the strait (predicted ' + fmt(exp1, 1) + '), ' + fmt(land1.k, 1) + ' across bare land (' + fmt(exp1l, 1) + ')');
  T.ok(Math.abs(sea1.k - exp1) < 0.2 * exp1 && Math.abs(land1.k - exp1l) < 0.2 * exp1l, 'boost 1: ' + fmt(sea1.k, 1) + ' and ' + fmt(land1.k, 1) + ' seedlings, expected about ' + fmt(exp1, 1) + ' and ' + fmt(exp1l, 1));
});

T.test('vegetation: an unseeded island of a helper world greens over centuries', () => {
  const w = planet(4, 1), ids = landIds(w), big = biggest(ids);
  Life.fill(w, 1);
  const isl = [];
  for (let c = 0; c < w.n; c++) if (ids.id[c] >= 0 && ids.id[c] !== big) { w.veg[c] = 0; if (Life.capacity(w, c) > 0.1) isl.push(c); }
  T.ok(isl.length > 50, 'the helper world has islands');
  w.vars.lifeBoost = 1;
  const count = () => { const s = new Set(); let g = 0; for (const c of isl) if (w.veg[c] > 0) { g++; s.add(ids.id[c]); } return { g, isl: s.size }; };
  advance(w, 4 * 25, 8);
  const a = count();
  advance(w, 4 * 375, 8);
  const b = count();
  say('  islands (all but the largest landmass stripped bare): ' + a.g + ' green cells on ' + a.isl + ' islands after 25 years, ' + b.g + ' of ' + isl.length + ' on ' + b.isl + ' islands after 400');
  T.ok(a.g < isl.length * 0.9, 'not at once');
  T.ok(b.isl >= 5 && b.g > 0.9 * isl.length, 'most island ground is green after four centuries');
});

T.test('vegetation: a cleared forest regrows in roughly 100–160 years at boost 1', () => {
  const w = plain(4), buf = [];
  Life.fill(w, 1);
  w.vars.lifeBoost = 1;
  const cells = buf.slice(0, w.geo.ring(500, 2, buf));
  T.eq(cells.length, 19);
  for (const c of cells) w.veg[c] = 0;
  let y50 = -1, y90 = -1;
  for (let t = 1; t <= 4 * 300 && y90 < 0; t++) {
    w.tick++; Life.step(w, w.tick % 8, 8);
    if (t % 4) continue;
    let s = 0, sc = 0;
    for (const c of cells) { s += w.veg[c]; sc += Life.capacity(w, c); }
    if (y50 < 0 && s >= 0.5 * sc) y50 = t / 4;
    if (s >= 0.9 * sc) y90 = t / 4;
  }
  say('  a 19-cell clearing in forest: half grown after ' + y50 + ' years, 90 % after ' + y90);
  T.ok(y90 >= 100 && y90 <= 160, '90 % regrown after ' + y90 + ' years');
  // one felled cell among trees
  const v = plain(4); Life.fill(v, 1); v.vars.lifeBoost = 1; v.veg[900] = 0;
  let y = -1;
  for (let t = 1; t <= 4 * 300 && y < 0; t++) { v.tick++; Life.step(v, v.tick % 8, 8); if (v.veg[900] >= 0.9 * Life.capacity(v, 900)) y = t / 4; }
  T.ok(y >= 85 && y <= 160, 'a single felled cell is back after ' + y + ' years');
});

T.test('step: honours dt = parts × TICK (2 slices and 8 slices agree)', () => {
  // (a) everything deterministic: growth of standing vegetation and every decay
  const run = parts => {
    const w = planet(4, 2, true);
    w.vars.lifeBoost = 1;
    for (let c = 0; c < w.n; c++) {
      if (!isLand(w, c)) continue;
      if (Life.capacity(w, c) > TL.plantMin) w.veg[c] = 0.06;
      w.wet[c] = 0.25; w.scorch[c] = 0.6; w.rad[c] = 0.3;
      if (c % 5 === 0) { w.farm[c] = 0.5; w.urban[c] = 0.4; }
    }
    advance(w, 4 * 40, parts);
    const s = { veg: 0, fauna: 0, fert: 0, wet: 0, scorch: 0, rad: 0, farm: 0, urban: 0 };
    for (let c = 0; c < w.n; c++) if (isLand(w, c)) for (const k in s) s[k] += w[k][c];
    return s;
  };
  const a = run(2), b = run(8);
  for (const k in a) T.ok(Math.abs(a[k] - b[k]) <= 0.03 * Math.max(a[k], b[k]) + 1e-9, k + ' after 40 years: ' + fmt(a[k], 3) + ' with 2 slices, ' + fmt(b[k], 3) + ' with 8');
  // (b) the stochastic spread from one seed: years to half cover, mean of 4 runs each
  const half = (parts, r) => {
    const w = planet(4, 1, true), ids = landIds(w);
    w.vars.lifeBoost = 1; w.rng = DM.U.rng(500 + r);
    Life.seed(w, heart(w, ids, biggest(ids)), 0.2);
    for (let t = 1; t <= 4 * 800; t++) { w.tick++; Life.step(w, w.tick % parts, parts); if (t % 4 === 0 && Life.cover(w).veg >= 0.5) return t / 4; }
    return Infinity;
  };
  let m2 = 0, m8 = 0;
  for (let r = 0; r < 4; r++) { m2 += half(2, r) / 4; m8 += half(8, r) / 4; }
  say('  years to half cover from one seed (helper L = 4, boost 1): ' + fmt(m2, 1) + ' with 2 slices, ' + fmt(m8, 1) + ' with 8');
  T.ok(Math.abs(m2 - m8) <= 0.1 * Math.max(m2, m8), 'spread per year: ' + fmt(m2, 1) + ' vs ' + fmt(m8, 1) + ' years');
  // (c) the tick length comes from Sim.TICK
  const dec = tick => {
    const w = plain(4);
    if (tick) T.stub(DM, 'Sim', { TICK: tick });
    w.rad[5] = 0.5; Life.step(w, 0, 1);
    return w.rad[5];
  };
  T.near(dec(0), 0.5 * Math.pow(0.985, 0.25), 1e-6, 'default tick is a quarter of a year');
  T.near(dec(1), 0.5 * 0.985, 1e-6, 'Sim.TICK = 1');
});

T.test('step: only the cells of its slice change', () => {
  const w = plain(4);
  w.rad.fill(0.5);
  Life.step(w, 3, 8);
  for (let c = 0; c < w.n; c++) T.ok((w.rad[c] < 0.5) === (c % 8 === 3), 'cell ' + c);
});

T.test('fill: veg = frac × capacity, wildlife at its capacity', () => {
  const w = planet(4, 3, true);
  Life.fill(w);
  let richShallow = 0, shallow = 0, deep = 0, richDeep = 0;
  for (let c = 0; c < w.n; c++) {
    T.near(w.veg[c], Life.capacity(w, c), 1e-6, 'veg at capacity, cell ' + c);
    if (isLand(w, c)) { if (w.veg[c] > 0.2 && !(w.fauna[c] > 0.1)) T.ok(false, 'no wildlife in the woods at ' + c); if (w.veg[c] === 0 && !(w.water[c] > LAKE) && w.fauna[c] !== 0) T.ok(false, 'wildlife on bare ground at ' + c); }
    else if (w.seaLevel - w.elev[c] <= 0.6) { shallow++; richShallow += w.fauna[c]; }
    else if (w.seaLevel - w.elev[c] > 3) { deep++; richDeep += w.fauna[c]; }
  }
  T.ok(shallow > 0 && deep > 0 && richShallow / shallow > 1.5 * richDeep / deep, 'shallows are richer in fish than the deep sea');
  T.near(Life.cover(w).veg, 1, 1e-6);
  const before = Float32Array.from(w.fauna);
  advance(w, 16, 8);                                  // already at equilibrium: wildlife does not move
  let drift = 0;
  for (let c = 0; c < w.n; c++) drift = Math.max(drift, Math.abs(w.fauna[c] - before[c]));
  T.ok(drift < 0.02, 'fauna set by fill is the capacity step() relaxes to (largest change ' + fmt(drift, 4) + ')');
  w.tick++;
  Life.fill(w, 0.5);
  for (let c = 0; c < w.n; c++) T.near(w.veg[c], 0.5 * Life.capacity(w, c), 1e-6);
  T.near(Life.cover(w).veg, 0.5, 1e-6);
});

T.test('step: dirty.objects when vegetation crosses a State.VEG_T level, dirty.color when colours move', () => {
  const w = plain(4);
  w.veg.fill(0.2);                                   // just under the first tree level (0.22)
  w.vars.lifeBoost = 1; w.dirty.objects = false; w.dirty.color = false;
  let t = 0;
  while (!w.dirty.objects && t < 200) { w.tick++; Life.step(w, w.tick % 8, 8); t++; }
  T.ok(w.dirty.objects, 'trees appear');
  T.ok(w.dirty.color, 'and the ground changes colour');
  // a world at rest raises no flags
  const q = plain(4);
  Life.fill(q, 1);
  for (let c = 0; c < q.n; c++) q.fert[c] = 0.6;
  advance(q, 80, 8);
  q.dirty.objects = false;
  advance(q, 40, 8);
  T.ok(!q.dirty.objects, 'no re-instancing while nothing crosses a level');
});

// ================================================================== fire
T.test('ignite: starts a fire only where the ground can burn; fire:start at most twice a tick', () => {
  const w = plain(4, { veg: 0.8 });
  const ev = H.capture(() => {
    T.eq(Life.ignite(w, 50), true, 'forest burns');
    T.ok(w.fire[50] > 0 && w.fire[50] <= 1);
    T.eq(Life.ignite(w, 50), false, 'already burning');
    w.elev[51] = -1; T.eq(Life.ignite(w, 51), false, 'sea');
    w.ice[52] = 0.8; T.eq(Life.ignite(w, 52), false, 'ice');
    w.water[53] = 0.4; T.eq(Life.ignite(w, 53), false, 'lake');
    w.veg[54] = 0.14; T.eq(Life.ignite(w, 54), false, 'veg < 0.15');
    w.wet[55] = 0.31; T.eq(Life.ignite(w, 55), false, 'wet > 0.3');
    T.eq(Life.ignite(w, 56, 0), false, 'no intensity');
    for (const bad of [-1, w.n, 2.5, NaN, undefined]) T.eq(Life.ignite(w, bad), false, 'bad cell ' + bad);
    T.eq(Life.ignite(w, 60, 0.3), true);
    T.eq(Life.ignite(w, 70), true);
    T.eq(Life.ignite(w, 80), true);
  });
  const starts = ev.filter(e => e.type === 'fire:start');
  T.eq(starts.length, 2, 'two fire:start events in one tick');
  T.eq(starts[0].payload, { cell: 50 });
  T.eq(firesMatch(w), null);
  T.eq(w._fires.n, 4);
  w.tick++;
  T.eq(H.capture(() => Life.ignite(w, 90)).length, 1, 'the next tick may announce fires again');
});

T.test('fire: a cell burns for exactly 2 ticks and tries each neighbour once, on its first', () => {
  const w = plain(4, { veg: 0.8, moist: 0.2 }), g = w.geo, c0 = 300;
  w.rng = { f: () => 0 };                             // every attempt succeeds: the fire is a clean expanding ring
  const hops = new Int32Array(w.n).fill(-1), q = [c0];
  hops[c0] = 0;
  for (let h = 0; h < q.length; h++) { const c = q[h]; for (let j = 0; j < g.nbrN[c]; j++) { const m = g.nbr[6 * c + j]; if (hops[m] < 0) { hops[m] = hops[c] + 1; q.push(m); } } }
  Life.ignite(w, c0);
  const lit = new Int32Array(w.n);                    // number of Life.burn calls each cell was seen burning after
  for (let t = 1; t <= 8; t++) {
    w.tick++; Life.burn(w);
    for (let c = 0; c < w.n; c++) {
      const want = hops[c] === t || hops[c] === t - 1;          // ring t was lit this tick, ring t − 1 is on its second
      if ((w.fire[c] > 0) !== want) T.ok(false, 'tick ' + t + ': cell ' + c + ' at ' + hops[c] + ' hops is ' + (w.fire[c] > 0 ? 'burning' : 'not burning'));
      if (w.fire[c] > 0) lit[c]++;
    }
    T.eq(firesMatch(w), null);
  }
  for (let c = 0; c < w.n; c++) if (hops[c] >= 1 && hops[c] <= 6) T.eq(lit[c], 2, 'cell at ' + hops[c] + ' hops burnt for two ticks');
  T.near(w.veg[c0], 0.16, 1e-6, 'burning removes 80 % of the vegetation');
  T.ok(w.scorch[c0] > 0.5, 'and leaves scorch');
});

T.test('fire: P = 0.55 × veg × sat(1.2 − 1.6 × (moist + wet)), exactly', () => {
  const tryOnce = (veg, moist, wet, roll) => {
    const w = plain(4, { veg, moist });
    w.wet.fill(wet);
    w.rng = { f: () => roll };
    Life.ignite(w, 300);
    w.tick++; Life.burn(w);
    return burning(w) - 1;                             // neighbours lit
  };
  const cases = [[0.9, 0.4, 0], [0.8, 0.36, 0], [0.5, 0.1, 0], [1, 0, 0], [0.9, 0.3, 0.1], [0.6, 0.2, 0.25], [0.9, 0.7, 0], [0.3, 0.25, 0]];
  for (const [veg, moist, wet] of cases) {
    const P = 0.55 * veg * DM.U.sat(1.2 - 1.6 * (moist + wet));
    T.eq(tryOnce(veg, moist, wet, P - 0.004), 6, 'veg ' + veg + ' moist ' + moist + ' wet ' + wet + ': a roll just under P = ' + fmt(P, 3) + ' lights every neighbour');
    T.eq(tryOnce(veg, moist, wet, P + 0.004), 0, 'veg ' + veg + ' moist ' + moist + ' wet ' + wet + ': a roll just over P = ' + fmt(P, 3) + ' lights none');
  }
  T.eq(tryOnce(0.9, 0.75, 0, 0), 0, 'moist ≥ 0.75: dry = 0, nothing catches');
  T.eq(tryOnce(0.9, 0.2, 0.35, 0), -1, 'wet > 0.3: the fire itself goes out');
  // the documented landmarks
  T.ok(0.55 * 0.9 * DM.U.sat(1.2 - 1.6 * 0.7) < 0.05, 'wet forest P < 0.05');
  T.near(0.55 * 0.9 * DM.U.sat(1.2 - 1.6 * 0.4), 0.28, 0.005, 'dry forest P ≈ 0.28');
  // the frequency really is P: 3,000 first ticks at P = 0.25
  const w = plain(4, { veg: 0.8, moist: (1.2 - 0.25 / 0.55 / 0.8) / 1.6 });
  let lit = 0, tries = 0;
  for (let i = 0; i < 3000; i++) {
    const c = 12 + (i * 7) % (w.n - 12);             // (cells 0–11 are the pentagons)
    w.veg.fill(0.8); w.fire.fill(0); Life.rebuild(w);
    Life.ignite(w, c); w.tick++; Life.burn(w);
    lit += burning(w) - 1; tries += 6;
  }
  say('  ignition frequency at P = 0.25: ' + fmt(lit / tries, 4) + ' over ' + tries + ' attempts');
  T.near(lit / tries, 0.25, 0.012, 'measured ignition frequency');
});

T.test('fire: dies in wet forest, stays small in dry forest, runs under drought (fire sizes)', () => {
  const wet = fireSizes(5, 0.85, 0.70, 80), dry = fireSizes(5, 0.9, 0.40, 200), drought = fireSizes(5, 0.85, 0.15, 12);
  const line = (name, P, r) => '  ' + name.padEnd(34) + 'P ' + fmt(P, 3) + '  mean ' + fmt(r.mean, 1).padStart(7) + '  median ' + String(r.median).padStart(5) + '  p90 ' + String(r.p90).padStart(5) + '  max ' + String(r.max).padStart(5) + '  > 200 cells: ' + fmt(100 * r.over(200), 1) + ' %  peak burning ' + r.peak;
  say('  fire sizes in cells on a uniform forest of ' + wet.n + ' cells:');
  say(line('wet forest (veg .85, moist .70)', 0.55 * 0.85 * 0.08, wet));
  say(line('dry forest (veg .90, moist .40)', 0.55 * 0.9 * 0.56, dry));
  say(line('drought (veg .85, moist .15)', 0.55 * 0.85 * 0.96, drought));
  T.ok(wet.max <= 10 && wet.mean < 2.5, 'wet forest: a few cells at most (max ' + wet.max + ')');
  T.ok(dry.median < 80 && dry.over(200) < 0.25, 'dry forest: most fires under ~200 cells (' + fmt(100 * dry.over(200), 1) + ' % larger)');
  T.ok(dry.mean > 4 * wet.mean, 'but far larger than in wet forest');
  T.ok(drought.median > 0.5 * drought.n, 'drought: the fire runs (median ' + drought.median + ' of ' + drought.n + ' cells)');
});

// The rule is bond percolation on the 6-neighbour lattice (each burning cell tries each neighbour once), whose
// threshold is 2 sin(π / 18) = 0.347: below it every fire is a finite cluster, above it a fire that survives its
// first few rings crosses the planet. Sweep P on a uniform forest and look for the step.
T.test('fire: a sharp percolation threshold near P = 0.35 (sweep of P on a uniform forest)', () => {
  const VEG = 0.9, rows = [];
  say('  fire size against P (uniform forest, veg ' + VEG + ', L = 5, 10,242 cells):');
  for (const [P, trials] of [[0.10, 120], [0.20, 120], [0.25, 120], [0.28, 120], [0.30, 100], [0.33, 60], [0.36, 40], [0.40, 40], [0.45, 40]]) {
    const dry = P / (0.55 * VEG), r = fireSizes(5, VEG, (1.2 - dry) / 1.6, trials, 9000 + Math.round(P * 100));
    const big = r.over(0.1 * r.n);
    rows.push({ P, mean: r.mean, big });
    say('    P ' + fmt(P) + '  mean ' + fmt(r.mean, 1).padStart(7) + '  median ' + String(r.median).padStart(5) + '  p90 ' + String(r.p90).padStart(5) + '  max ' + String(r.max).padStart(5) + '  > 200 cells ' + fmt(100 * r.over(200), 0).padStart(3) + ' %  > 10 % of the planet ' + fmt(100 * big, 0).padStart(3) + ' %');
  }
  for (const r of rows) {
    if (r.P <= 0.28) T.eq(r.big, 0, 'P = ' + r.P + ': no fire crosses the planet');
    if (r.P >= 0.40) T.ok(r.big >= 0.7, 'P = ' + r.P + ': most fires cross the planet (' + fmt(100 * r.big, 0) + ' %)');
  }
  for (let i = 1; i < rows.length; i++) T.ok(rows[i].mean > rows[i - 1].mean, 'fires grow with P (' + rows[i - 1].P + ' → ' + rows[i].P + ')');
  const at = P => rows.find(r => r.P === P);
  T.ok(at(0.36).mean > 20 * at(0.30).mean, 'the step: mean size ' + fmt(at(0.30).mean, 0) + ' cells at P = 0.30, ' + fmt(at(0.36).mean, 0) + ' at 0.36');
});

T.test('fire: never enters sea, ice, lakes or thin vegetation; wet ground stops it', () => {
  const w = plain(5, { veg: 0.85, moist: 0.1 }), g = w.geo, kind = new Uint8Array(w.n);
  // stripes of every obstacle across the burning plain
  for (let c = 0; c < w.n; c++) {
    const lon = g.lon[c], lat = g.lat[c];
    if (Math.abs(lat) > 1.2) continue;
    const s = Math.floor((lon + Math.PI) / (Math.PI / 6)) % 6;
    if (Math.abs(lat - 0.5) < 0.06 && s === 1) { kind[c] = 1; w.elev[c] = -1; }
    else if (Math.abs(lat + 0.3) < 0.06 && s === 2) { kind[c] = 2; w.ice[c] = 0.9; }
    else if (Math.abs(lat - 0.1) < 0.06 && s === 3) { kind[c] = 3; w.water[c] = 0.5; }
    else if (Math.abs(lat + 0.6) < 0.06 && s === 4) { kind[c] = 4; w.veg[c] = 0.14; }
    else if (Math.abs(lat - 0.8) < 0.06 && s === 5) { kind[c] = 5; w.wet[c] = 0.5; }
  }
  const names = ['', 'sea', 'ice', 'lake', 'thin vegetation', 'wet ground'], had = [0, 0, 0, 0, 0, 0];
  for (let c = 0; c < w.n; c++) had[kind[c]]++;
  for (let k = 1; k <= 5; k++) T.ok(had[k] > 10, names[k] + ' stripe exists');
  Life.ignite(w, H.findLand(w, c => kind[c] === 0 && Math.abs(g.lat[c]) < 0.05));
  let ticks = 0;
  while (w._fires.n > 0 && ticks < 3000) {
    w.tick++; Life.burn(w); ticks++;
    for (let c = 0; c < w.n; c++) if (kind[c] && w.fire[c] > 0) T.ok(false, 'fire on ' + names[kind[c]] + ' at tick ' + ticks);
    if (ticks % 16 === 0) T.eq(firesMatch(w), null);
  }
  let burnt = 0;
  for (let c = 0; c < w.n; c++) { if (w.scorch[c] > 0) { burnt++; if (kind[c]) T.ok(false, names[kind[c]] + ' was scorched'); } }
  T.ok(burnt > 0.5 * w.n, 'the drought fire did run across the plain (' + burnt + ' cells)');
  T.eq(burning(w), 0);
  // rain on a running fire
  const r = plain(4, { veg: 0.85, moist: 0.1 });
  Life.ignite(r, 400);
  for (let i = 0; i < 6; i++) { r.tick++; Life.burn(r); }
  T.ok(burning(r) > 10, 'a fire front is running');
  r.wet.fill(0.31);
  r.tick++; Life.burn(r);
  T.eq(burning(r), 0, 'wet > 0.3 puts every fire out');
  T.eq(r._fires.n, 0);
});

T.test('fire: never more than 1,500 cells burn at once', () => {
  // 151 lightning strikes at once on a planet-wide drought forest: uncapped, several thousand cells would burn together
  const w = plain(6, { veg: 0.9, moist: 0.05 });
  let started = 0;
  for (let c = 5; c < w.n; c += 273) if (Life.ignite(w, c)) started++;
  T.eq(started, 151);
  let peak = 0, ticks = 0, atCap = 0, busy = 0, busyMs = 0, worstMs = 0;
  while (w._fires.n > 0 && ticks < 4000) {
    const before = w._fires.n, t0 = process.hrtime.bigint();
    w.tick++; Life.burn(w); ticks++;
    const d = Number(process.hrtime.bigint() - t0) / 1e6;
    if (before >= 1000) { busy++; busyMs += d; if (d > worstMs) worstMs = d; }
    const b = burning(w);
    if (b > peak) peak = b;
    if (b >= 1450) atCap++;
    if (b !== w._fires.n) T.ok(false, '_fires.n ' + w._fires.n + ' ≠ ' + b + ' burning cells at tick ' + ticks);
  }
  let burnt = 0;
  for (let c = 0; c < w.n; c++) if (w.scorch[c] > 0) burnt++;
  say('  151 simultaneous strikes in drought forest (L = 6): peak ' + peak + ' cells burning, ' + atCap + ' ticks at the cap, ' + burnt + ' cells burnt, out after ' + ticks + ' ticks; Life.burn with ≥ 1,000 cells alight: mean ' + fmt(busyMs / Math.max(1, busy), 3) + ' ms, worst ' + fmt(worstMs, 3) + ' ms (' + busy + ' ticks)');
  T.ok(peak <= 1500, 'peak ' + peak);
  T.ok(atCap > 5, 'the cap was really reached (' + atCap + ' ticks)');
  T.eq(burning(w), 0, 'and the fire ends');
  // Life.ignite refuses at the cap as well
  const x = plain(6, { veg: 0.9, moist: 0.05 });
  let lit = 0;
  for (let c = 0; c < x.n; c += 13) if (Life.ignite(x, c)) lit++;
  T.eq(lit, 1500, 'ignite stops at 1,500');
  T.eq(Life.ignite(x, 7), false);
  x.tick++; Life.burn(x);
  T.eq(burning(x), 1500, 'nothing new catches while 1,500 burn');
});

T.test('fire: _fires mirrors fire[]; rebuild restores it; a missing cache is tolerated', () => {
  const w = plain(4, { veg: 0.85, moist: 0.2 });
  Life.ignite(w, 100); Life.ignite(w, 900);
  for (let i = 0; i < 5; i++) { w.tick++; Life.burn(w); T.eq(firesMatch(w), null); }
  const n = burning(w);
  T.ok(n > 2);
  delete w._fires;
  const F = Life.rebuild(w);
  T.ok(F === w._fires && F.cells instanceof Int32Array, 'rebuild returns the cache');
  T.eq(F.n, n);
  T.eq(firesMatch(w), null);
  // a save/load in the middle of a fire (fire[] quantized to 8 bits, the cache gone): every fire keeps its stage,
  // so the cells on their second tick go out and the fresh ones burn one tick more
  const a = plain(4, { veg: 0.85, moist: 0.2 }), b = plain(4, { veg: 0.85, moist: 0.2 });
  for (const x of [a, b]) { Life.ignite(x, 100, 0.5); Life.ignite(x, 1500, 0.01); for (let i = 0; i < 4; i++) { x.tick++; Life.burn(x); } Life.ignite(x, 2000, 0.01); Life.ignite(x, 2100, 1); }
  for (let c = 0; c < b.n; c++) b.fire[c] = Math.round(b.fire[c] * 255) / 255;
  delete b._fires;
  T.ok(burning(a) > 8);
  for (const x of [a, b]) x.rng = { f: () => 1 };      // (no new ignitions: watch the old ones end)
  for (let i = 0; i < 2; i++) {
    a.tick++; Life.burn(a); b.tick++; Life.burn(b);
    for (let c = 0; c < a.n; c++) if ((a.fire[c] > 0) !== (b.fire[c] > 0)) T.ok(false, 'after a reload cell ' + c + ' burns ' + (b.fire[c] > 0 ? 'longer' : 'shorter'));
    T.eq(burning(a) > 0, i === 0, 'all out two ticks later');
  }
  // no cache at all
  const e = plain(4, { veg: 0.85 });
  delete e._fires;
  Life.burn(e);
  T.eq(e._fires.n, 0);
  delete e._fires;
  T.eq(Life.ignite(e, 5), true);
  T.eq(e._fires.n, 1);
  e._fires = null; e.fire[5] = 0;
  Life.step(e, 0, 1);
});

// ================================================================== decay, lakes, the sea
T.test('lake cells: capacity 0, habitability 0, vegetation dies, fish arrive', () => {
  const w = plain(4, { veg: 0.7, moist: 0.5 });
  w.fauna.fill(0.4);
  w.water[640] = 0.6; w.water[641] = 0.02;            // a lake cell, and a puddle below the 0.03 threshold
  T.eq(Life.capacity(w, 640), 0);
  T.ok(Life.capacity(w, 641) > 0.5, 'a puddle is not a lake');
  Life.habitability(w);
  T.eq(w.hab[640], 0);
  T.ok(w.hab[641] > 0.1);
  advance(w, 4 * 30, 8);
  T.ok(w.veg[640] < 0.01, 'the drowned forest is gone (veg ' + fmt(w.veg[640], 4) + ')');
  T.ok(w.fauna[640] > 0.2, 'fish in the lake');
});

T.test('step: farm and urban fade on unowned cells only; rad, wet and scorch decay', () => {
  const w = plain(4, { veg: 0 });
  w.biome.fill(B.ROCK);
  for (const c of [10, 11]) { w.farm[c] = 0.8; w.urban[c] = 0.8; }
  w.owner[11] = 0;
  w.rad[12] = 0.5; w.wet[13] = 0.9; w.scorch[14] = 0.9;
  advance(w, 4 * 10, 8);
  T.near(w.farm[10], 0.8 * Math.pow(0.95, 10), 1e-4, 'farm −5 %/yr');
  T.near(w.urban[10], 0.8 * Math.pow(0.99, 10), 1e-4, 'urban −1 %/yr');
  T.near(w.farm[11], 0.8, 1e-6, 'owned farmland stays');
  T.near(w.urban[11], 0.8, 1e-6, 'owned town stays');
  T.near(w.rad[12], 0.5 * Math.pow(0.985, 10), 1e-4, 'rad −1.5 %/yr');
  T.ok(w.wet[13] < 0.5 && w.wet[13] > 0.1, 'wet decays (' + fmt(w.wet[13]) + ' after 10 years)');
  T.ok(w.scorch[14] < 0.7 && w.scorch[14] > 0.2, 'scorch decays (' + fmt(w.scorch[14]) + ' after 10 years)');
  advance(w, 4 * 500, 8);
  T.eq(w.farm[10], 0, 'ruined fields vanish');
  T.eq(w.wet[13], 0); T.eq(w.scorch[14], 0);
  T.ok(w.urban[10] < 0.01 && w.rad[12] < 0.001, 'ruins and fallout fade');
});

T.test('step: sea cells are zeroed (veg, farm, urban, road, water, fire, scorch)', () => {
  const w = plain(4, { veg: 0.8 });
  const c = 333;
  Life.ignite(w, c);
  w.farm[c] = 0.5; w.urban[c] = 0.5; w.road[c] = 2; w.water[c] = 0.4; w.scorch[c] = 0.7;
  w.elev[c] = -2;                                     // the sea takes it
  w.dirty.roads = false; w.dirty.water = false;
  for (let p = 0; p < 8; p++) Life.step(w, p, 8);
  for (const f of ['veg', 'farm', 'urban', 'road', 'water', 'fire', 'scorch']) T.eq(w[f][c], 0, f);
  T.eq(firesMatch(w), null, 'and it left the fire list');
  T.ok(w.dirty.roads && w.dirty.water, 'dirty.roads / dirty.water');
  T.eq(H.checkFinite(w), null);
});

T.test('step: fertility relaxes toward a natural level, higher by fresh water and on old ash', () => {
  const make = special => {
    const w = plain(4, { veg: 0, fert: 0.5 });
    w.biome.fill(B.GRASSLAND);
    w.fresh.fill(9);
    if (special) w.fresh[200] = 0;
    for (let i = 0; i < 4 * 1500; i++) { w.tick++; if (special && i % 4 === 0) w.scorch[201] = 0.9; Life.step(w, w.tick % 8, 8); }
    return w;
  };
  const w = make(true), ctl = make(false);
  T.ok(Math.abs(ctl.fert[300] - 0.5) > 0.01 || Math.abs(ctl.fert[301] - 0.5) > 0.01, 'fertility moves away from its start');
  const again = make(false);
  for (let i = 0; i < 4 * 1500; i++) { again.tick++; Life.step(again, again.tick % 8, 8); }
  T.near(again.fert[300], ctl.fert[300], 0.01, 'and settles at a level');
  T.ok(w.fert[200] > ctl.fert[200] + 0.15, 'river bank ' + fmt(w.fert[200]) + ' against ' + fmt(ctl.fert[200]) + ' for the same ground without water');
  T.ok(w.fert[201] > ctl.fert[201] + 0.1, 'old ash ' + fmt(w.fert[201]) + ' against ' + fmt(ctl.fert[201]) + ' without');
  let lo = 9, hi = 0;
  for (let c = 0; c < w.n; c++) { lo = Math.min(lo, w.fert[c]); hi = Math.max(hi, w.fert[c]); }
  T.ok(lo >= 0.1 && hi <= 1.5, 'fertility stays in range (' + fmt(lo) + ' … ' + fmt(hi) + ')');
});

T.test('step: wildlife follows the vegetation; a BARREN cell that recovers asks for new biomes', () => {
  const w = plain(4, { veg: 0 });
  w.vars.lifeBoost = 1;
  advance(w, 80, 8);
  T.eq(w.fauna[77], 0, 'no animals on bare ground');
  Life.fill(w, 1); w.fauna.fill(0);
  advance(w, 4 * 40, 8);
  T.ok(w.fauna[77] > 0.6, 'animals fill a forest (' + fmt(w.fauna[77]) + ')');
  w.veg[77] = 0; w.farm[77] = 1; w.owner[77] = 0;     // cleared and kept under the plough
  advance(w, 4 * 40, 8);
  T.ok(w.fauna[77] < 0.05, 'and leave cleared land (' + fmt(w.fauna[77], 3) + ')');
  // barren recovery
  const b = plain(4, { veg: 0 });
  b.lava[50] = 0.5;
  Life.classify(b);
  T.eq(b.biome[50], B.BARREN);
  b.need.life = false;
  advance(b, 16, 8);
  T.eq(b.need.life, false, 'still molten: nothing to do');
  b.lava[50] = 0;
  advance(b, 16, 8);
  T.eq(b.need.life, true, 'cooled: need.life is set');
});

// ================================================================== bloom, cover
T.test('bloom: raises vegetation toward capacity and fertility by amount × falloff', () => {
  const w = plain(4, { veg: 0, fert: 0.5 });
  const c0 = 420, ang = 0.3;
  w.elev[w.geo.nbr[6 * c0]] = -1;                     // a sea cell inside the brush
  w.scorch[w.geo.nbr[6 * c0 + 1]] = 0.5;
  const n = Life.bloom(w, c0, ang, 0.3);
  T.ok(n > 10, 'cells touched: ' + n);
  T.near(w.veg[c0], 0.3, 1e-6, 'centre gets the full amount');
  T.near(w.fert[c0], 0.8, 1e-6);
  T.eq(w.veg[w.geo.nbr[6 * c0]], 0, 'nothing grows on the sea');
  T.near(w.fert[w.geo.nbr[6 * c0]], 0.5, 1e-6, 'and the sea bed is left alone');
  T.ok(w.scorch[w.geo.nbr[6 * c0 + 1]] < 0.5, 'ash is cleared');
  let prev = 1;
  for (let c = 0; c < w.n; c++) {
    const d = w.geo.angle(c0, c);
    if (d >= ang) { if (w.veg[c] !== 0 || Math.abs(w.fert[c] - 0.5) > 1e-6) T.ok(false, 'cell ' + c + ' outside the brush changed'); }
    else if (isLand(w, c)) T.ok(w.veg[c] <= 0.3 + 1e-6 && w.fert[c] >= 0.5 - 1e-6 && w.fert[c] <= 0.8 + 1e-6, 'bounded by the amount');
  }
  const mid = H.findLand(w, c => { const d = w.geo.angle(c0, c); return d > 0.45 * ang && d < 0.55 * ang; });
  T.ok(w.veg[mid] > 0.08 && w.veg[mid] < 0.22, 'smooth falloff (half way out: ' + fmt(w.veg[mid]) + ')');
  for (let i = 0; i < 20; i++) Life.bloom(w, c0, ang, 0.3);
  T.near(w.veg[c0], Life.capacity(w, c0), 1e-6, 'never beyond capacity');
  T.ok(w.fert[c0] <= 1.5 + 1e-6, 'fertility capped at 1.5');
  T.eq(Life.bloom(w, -3, ang, 0.3), 0);
  T.eq(Life.bloom(w, c0, ang, 0), 0);
  T.eq(prev, 1);
});

T.test('cover: land share, vegetation / capacity over plantable land, forest share; cached per tick', () => {
  const w = plain(4, { veg: 0 });
  for (let c = 0; c < w.n; c++) if (c % 2) w.veg[c] = Life.capacity(w, c);
  for (let c = 0; c < w.n; c += 4) w.elev[c] = -1;    // a quarter of the planet under water
  for (let p = 0; p < 2; p++) Life.step(w, p, 2);
  w.tick++;
  const a = Life.cover(w);
  T.near(a.land, 0.75, 1e-3);
  T.near(a.veg, 2 / 3, 0.01, 'two thirds of the remaining land is at capacity');
  T.near(a.forest, 2 / 3, 0.01, 'and wooded');
  T.ok(Life.cover(w) === a, 'same object within a tick');
  Life.fill(w, 1);
  T.near(Life.cover(w).veg, 1, 1e-6, 'an edit invalidates the cache');
  const e = plain(4, { veg: 0 });
  e.elev.fill(-1);
  T.eq(Life.cover(e), { land: 0, veg: 0, forest: 0 }, 'no land at all');
});

// ================================================================== habitability
T.test('habitability: 0..1, zero on sea, lakes, ice, rock, snow peaks, lava and barren ground; _landHab', () => {
  for (const seed of [1, 4]) {
    const w = planet(5, seed);
    const c1 = H.findLand(w, c => w.biome[c] === B.FOREST), c2 = H.findLand(w, c => w.biome[c] === B.FOREST && c !== c1), c3 = H.findLand(w, c => w.biome[c] === B.TAIGA);
    w.water[c1] = 0.5; w.lava[c2] = 0.5; w.scorch[c3] = 0.95; w.veg[c3] = 0;
    Life.classify(w);
    const ids = landIds(w);
    w._landId = ids.id;                               // (Terrain.derive's labels, stubbed)
    w.need.life = true;
    Life.habitability(w);
    T.eq(w.need.life, false, 'clears need.life');
    const want = new Array(ids.size.length).fill(0);
    let good = 0;
    for (let c = 0; c < w.n; c++) {
      const h = w.hab[c], b = w.biome[c];
      if (!(h >= 0 && h <= 1)) T.ok(false, 'hab out of range at ' + c + ': ' + h);
      const dead = !isLand(w, c) || w.water[c] > LAKE || w.ice[c] > 0.5 || w.lava[c] > 0.05 || b === B.ICE || b === B.ROCK || b === B.SNOWPEAK || b === B.BARREN;
      if (dead && h !== 0) T.ok(false, DM.BIOME_NAMES[b] + ' cell ' + c + ' has hab ' + h);
      if (h > 0.05) want[ids.id[c]]++;
      if (h > 0.5) good++;
    }
    T.eq(w.hab[c1], 0, 'lake'); T.eq(w.hab[c2], 0, 'lava'); T.eq(w.hab[c3], 0, 'barren');
    T.ok(good > 100, 'the world has good land (' + good + ' cells above 0.5)');
    T.eq(Array.from(w._landHab), want, '_landHab = habitable cells per landmass');
  }
  const w = planet(4, 2);
  delete w._landId;
  Life.habitability(w);
  T.eq(w._landHab.length, 0, 'no landmass labels: an empty table, no throw');
});

T.test('habitability: temperature, water, soil, slope, coast and fallout each count; a river makes a desert liveable', () => {
  const w = plain(4, { temp: 16, moist: 0.5, fert: 0.6 }), sk = w.geo.step / TL.refStep;
  w.coast.fill(20);
  const set = (c, f) => { f(c); return c; };
  const base = 30;
  const cold = set(31, c => { w.temp[c] = -4; }), hot = set(32, c => { w.temp[c] = 37; }), freezing = set(33, c => { w.temp[c] = -12; });
  const dry = set(34, c => { w.moist[c] = 0.02; }), oasis = set(35, c => { w.moist[c] = 0.02; w.fresh[c] = 1; }), bank2 = set(36, c => { w.moist[c] = 0.02; w.fresh[c] = 2; }), bank3 = set(37, c => { w.moist[c] = 0.02; w.fresh[c] = 3; });
  const poor = set(38, c => { w.fert[c] = 0.1; }), steep = set(39, c => { w.slope[c] = 1.2 * sk; }), cliff = set(40, c => { w.slope[c] = 2 * sk; });
  const shore = set(41, c => { w.coast[c] = 1; }), hotspot = set(42, c => { w.rad[c] = 0.6; }), mild = set(43, c => { w.temp[c] = 9; }), warm = set(44, c => { w.temp[c] = 25; });
  Life.classify(w);
  for (const c of [dry, oasis, bank2, bank3]) T.eq(w.biome[c], B.DESERT);
  Life.habitability(w);
  const h = w.hab;
  T.ok(h[base] > 0.6, 'good land scores high (' + fmt(h[base]) + ')');
  T.ok(h[cold] < 0.3 * h[base] && h[hot] < 0.3 * h[base] && h[freezing] === 0, 'too cold / too hot');
  T.near(h[mild], h[base], 1e-6, 'anything from 8 to 26 °C is equally good');
  T.near(h[warm], h[base], 1e-6);
  T.eq(h[dry], 0, 'desert without water');
  T.ok(h[oasis] > 0.4 && h[bank2] > 0.25 && h[bank2] <= h[oasis], 'fresh water within 2 hops makes it liveable (' + fmt(h[oasis]) + ', ' + fmt(h[bank2]) + ')');
  T.eq(h[bank3], 0, 'three hops is too far');
  T.ok(h[poor] < h[base] && h[poor] > 0.5 * h[base], 'poor soil costs something');
  T.ok(h[steep] < 0.6 * h[base] && h[steep] > 0 && h[cliff] === 0, 'slopes');
  T.ok(h[shore] > h[base] && h[shore] < 1.2 * h[base], 'a small coastal bonus');
  T.near(h[hotspot], 0.4 * h[base], 1e-4, '× (1 − rad)');
});

T.test('habitability: river valleys in dry land beat the surrounding desert (helper world)', () => {
  const w = H.makeWorld({ L: 5, seed: 3, land: 0.6 }), g = w.geo;
  for (let c = 0; c < w.n; c++) if (isLand(w, c)) { w.moist[c] *= 0.12; w.fresh[c] = 255; }     // an arid planet ...
  // ... with rivers: walk downhill from a few inland cells and mark the way
  const river = new Uint8Array(w.n);
  let made = 0;
  for (let s = 0; s < w.n && made < 60; s += 37) {
    if (!isLand(w, s) || w.coast[s] < 3 || w.temp[s] < 10) continue;
    let c = s, len = 0;
    while (isLand(w, c) && len < 80) {
      river[c] = 1; len++;
      let lo = c;
      for (let j = 0; j < g.nbrN[c]; j++) { const m = g.nbr[6 * c + j]; if (w.elev[m] < w.elev[lo]) lo = m; }
      if (lo === c) break;
      c = lo;
    }
    made++;
  }
  // fresh[] = hops to the nearest river cell
  const q = [];
  for (let c = 0; c < w.n; c++) if (river[c]) { w.fresh[c] = 0; q.push(c); }
  for (let h = 0; h < q.length; h++) { const c = q[h]; for (let j = 0; j < g.nbrN[c]; j++) { const m = g.nbr[6 * c + j]; if (isLand(w, m) && w.fresh[m] > w.fresh[c] + 1) { w.fresh[m] = w.fresh[c] + 1; q.push(m); } } }
  Life.classify(w);
  Life.habitability(w);
  let sv = 0, nv = 0, sd = 0, nd = 0;
  for (let c = 0; c < w.n; c++) {
    if (w.biome[c] !== B.DESERT || w.slope[c] * (TL.refStep / g.step) > 1) continue;      // (gentle ground only: cliffs are uninhabitable anywhere)
    if (w.fresh[c] <= 2) { sv += w.hab[c]; nv++; } else if (w.fresh[c] >= 4) { sd += w.hab[c]; nd++; }
  }
  say('  arid helper world: mean hab ' + fmt(sv / nv) + ' in ' + nv + ' desert valley cells (fresh water ≤ 2 hops), ' + fmt(sd / nd, 3) + ' in ' + nd + ' open desert cells');
  T.ok(nv > 100 && nd > 500, 'both kinds of ground exist (' + nv + ', ' + nd + ')');
  T.ok(sv / nv > 0.2 && sv / nv > 5 * (sd / nd) + 0.1, 'valleys ' + fmt(sv / nv) + ' vs desert ' + fmt(sd / nd, 3));
});

// ================================================================== robustness, determinism, speed
T.test('robust: no sea, no land, the poles, bad arguments, any phase; never NaN', () => {
  const run = (w, label) => {
    for (const phase of ['molten', 'water', 'life', 'people', 'history', 'ended']) {
      w.phase = phase;
      Life.classify(w); Life.habitability(w); Life.rebuild(w);
      Life.seed(w, 0, 0.3); Life.seed(w, 11, 0.3); Life.bloom(w, 0, 0.2, 0.2); Life.ignite(w, 0); Life.ignite(w, 11);
      advance(w, 12, 8); advance(w, 4, 2); advance(w, 2, 1);
      Life.fill(w, 0.7); Life.cover(w);
      Life.step(w, 0, 0); Life.step(w, -3, 8); Life.step(w, 99, 8); Life.step(w); Life.step(w, 1.7, 2.9);
    }
    T.eq(H.checkFinite(w), null, label);
    T.eq(firesMatch(w), null, label);
  };
  run(plain(4, { veg: 0.5 }), 'no sea');
  const sea = plain(4); sea.elev.fill(-3); run(sea, 'no land');
  const frozen = plain(4, { temp: -40 }); frozen.ice.fill(1); run(frozen, 'all ice');
  const helper = planet(4, 5); helper.vars.lifeBoost = 1e9; run(helper, 'absurd boost');
  const odd = planet(4, 6); odd.vars.lifeBoost = NaN; delete odd.vars.seaBase; odd.seaLevel = -9; run(odd, 'sea level −9, NaN boost');
  const nov = planet(4, 6); nov.vars = {}; run(nov, 'empty vars');
  // garbage in the fields the other modules own must not spread
  const g = planet(4, 7);
  g.moist[5] = NaN; g.temp[6] = NaN; g.fert[H.findLand(g)] = NaN; g.veg[H.findLand(g, c => c > 300)] = NaN; g.veg[H.findLand(g, c => c > 900)] = -0.5;
  advance(g, 16, 8); Life.classify(g); Life.habitability(g);
  for (const f of ['veg', 'fauna', 'hab', 'fire', 'scorch', 'wet', 'rad', 'farm', 'urban']) for (let c = 0; c < g.n; c++) if (!Number.isFinite(g[f][c])) T.ok(false, f + '[' + c + '] is ' + g[f][c]);
  T.ok(Number.isFinite(g.fert[H.findLand(g)]), 'a NaN fertility is repaired');
});

T.test('deterministic: same seed and calls → same world; module scratch survives switching L', () => {
  const run = (seed) => {
    const w = planet(4, seed, true), ids = landIds(w), c0 = heart(w, ids, biggest(ids));
    w.vars.lifeBoost = 5;
    Life.seed(w, c0, 0.2);
    for (let t = 1; t <= 300; t++) {
      w.tick++; Life.burn(w); Life.step(w, w.tick % 8, 8);
      if (t === 150) { Life.ignite(w, c0); Life.bloom(w, c0, 0.1, 0.2); }
      if (t === 200) { const big = planet(5, 1); advance(big, 3, 8); Life.seed(big, 40, 0.1); Life.cover(big); }      // another world, another size
    }
    return H.hash(w);
  };
  const a = run(3);
  T.eq(run(3), a, 'two runs of seed 3');
  T.ok(run(4) !== a, 'another seed differs');
});

T.test('speed at L = 6: one slice of 8 ≤ 0.6 ms, classify ≤ 6 ms (asserted at 5×)', () => {
  const w = H.makeWorld({ L: 6, seed: 1 });
  const ms = f => { const t0 = process.hrtime.bigint(); f(); return Number(process.hrtime.bigint() - t0) / 1e6; };
  Life.classify(w); Life.fill(w, 1);
  for (let i = 0; i < 40; i++) { w.tick++; Life.step(w, w.tick % 8, 8); }             // warm up
  let worst = 0;
  const N = 400, total = ms(() => { for (let i = 0; i < N; i++) { w.tick++; const d = ms(() => Life.step(w, w.tick % 8, 8)); if (d > worst) worst = d; } });
  const cls = ms(() => { for (let i = 0; i < 20; i++) Life.classify(w); }) / 20;
  const hab = ms(() => { for (let i = 0; i < 20; i++) Life.habitability(w); }) / 20;
  // the life phase: an empty world greening at boost 25, 2 slices
  const g = H.makeWorld({ L: 6, seed: 2 });
  Life.classify(g); g.veg.fill(0); g.vars.lifeBoost = 25;
  const ids = landIds(g);
  Life.seed(g, heart(g, ids, biggest(ids)), 0.12);
  const gen = ms(() => { for (let i = 0; i < 200; i++) { g.tick++; Life.burn(g); Life.step(g, g.tick % 2, 2); } }) / 200;
  say('  L = 6 timings: step (1 slice of 8) mean ' + fmt(total / N, 3) + ' ms, worst ' + fmt(worst, 3) + ' ms; classify ' + fmt(cls, 3) + ' ms; habitability ' + fmt(hab, 3) + ' ms; life-phase tick (2 slices, boost 25) ' + fmt(gen, 3) + ' ms');
  T.ok(total / N <= 3, 'step slice mean ' + fmt(total / N, 3) + ' ms');
  T.ok(cls <= 30, 'classify ' + fmt(cls, 3) + ' ms');
  T.ok(hab <= 30, 'habitability ' + fmt(hab, 3) + ' ms');
});

// ================================================================== optional maps (LIFE_MAPS=1)
if (process.env.LIFE_MAPS) {
  const fs = require('fs'), path = require('path');
  const M = require('../tools/mapimg.js');
  const shot = name => 'shots/life-' + name + '.png';
  // what the ground looks like: bare soil by biome, green by vegetation, fire and ash on top
  const SOIL = [];
  SOIL[B.DESERT] = [214, 190, 132]; SOIL[B.ROCK] = [122, 114, 110]; SOIL[B.SNOWPEAK] = [246, 246, 250]; SOIL[B.BEACH] = [224, 208, 164];
  SOIL[B.TUNDRA] = [150, 142, 124]; SOIL[B.BARREN] = [62, 52, 50]; SOIL[B.SAVANNA] = [176, 150, 104];
  const look = w => c => {
    if (!isLand(w, c)) return w.ice[c] > 0.5 ? [200, 220, 235] : (w.seaLevel - w.elev[c] > 0.6 ? [18, 34, 78] : [34, 66, 118]);
    if (w.fire[c] > 0) return [255, 140, 20];
    if (w.ice[c] > 0.5) return [232, 238, 244];
    if (w.water[c] > LAKE) return [70, 120, 210];
    let base = SOIL[w.biome[c]] || [150, 122, 92];
    const s = w.scorch[c];
    if (s > 0) base = [base[0] * (1 - 0.75 * s), base[1] * (1 - 0.8 * s), base[2] * (1 - 0.8 * s)];
    const v = Math.min(1, w.veg[c]);
    if (!(v > 0)) return base;
    const t = Math.min(1, 0.2 + 0.8 * Math.sqrt(v)), g = [78 - 56 * v, 152 - 62 * v, 54 - 24 * v];
    return [base[0] + (g[0] - base[0]) * t, base[1] + (g[1] - base[1]) * t, base[2] + (g[2] - base[2]) * t];
  };
  // orthographic close-up centred on a cell
  const zoom = (w, file, cell, half, colorFn, size) => {
    size = size || 720;
    const g = w.geo, d = [0, 0, 0], e = [0, 0, 0], nn = [0, 0, 0], rgb = Buffer.alloc(size * size * 3), cache = new Map(), sh = Math.sin(half);
    g.dir(cell, d); g.basis(cell, e, nn);
    let hint = cell;
    for (let y = 0; y < size; y++) for (let x = 0; x < size; x++) {
      const u = ((x + 0.5) / size * 2 - 1) * sh, v = (1 - (y + 0.5) / size * 2) * sh, k = Math.sqrt(Math.max(0, 1 - u * u - v * v));
      const c = g.cellAt(d[0] * k + e[0] * u + nn[0] * v, d[1] * k + e[1] * u + nn[1] * v, d[2] * k + e[2] * u + nn[2] * v, hint);
      hint = c;
      let col = cache.get(c);
      if (!col) { col = colorFn(c); cache.set(c, col); }
      const o = (y * size + x) * 3;
      for (let i = 0; i < 3; i++) rgb[o + i] = Math.max(0, Math.min(255, col[i])) | 0;
    }
    const p = path.resolve(__dirname, '..', file);
    fs.mkdirSync(path.dirname(p), { recursive: true });
    fs.writeFileSync(p, M.encodePNG(size, size, rgb));
  };
  // the life phase from one seed: frames, the cover of the seeded landmass, and an arrival map (the tick each cell
  // first turned green, in bands of 20 ticks: every band edge is where the front stood at that moment)
  const wave = (w, tag, c0, half, frames) => {
    w.veg.fill(0); w.fauna.fill(0); w.vars.lifeBoost = 25; w.phase = 'life'; w.tick = 0;
    const ids = landIds(w), k = ids.id[c0], first = new Int32Array(w.n).fill(-1), last = frames[frames.length - 1];
    Life.seed(w, c0, 0.12);
    zoom(w, shot(tag + '-t0'), c0, half, look(w));
    let line = '  ' + tag + ' (landmass of ' + ids.size[k] + ' cells):';
    for (let t = 0; t <= last; t++) {
      if (t > 0) { w.tick++; Life.burn(w); Life.step(w, w.tick % 2, 2); }
      for (let c = 0; c < w.n; c++) if (first[c] < 0 && w.veg[c] > 0) first[c] = t;
      if (frames.indexOf(t) >= 0) {
        zoom(w, shot(tag + '-t' + t), c0, half, look(w)); M.custom(w, shot(tag + '-globe-t' + t), look(w), { rivers: false });
        const s = landCover(w, ids, k);
        line += ' t' + t + ' ' + fmt(100 * s.cover, 1) + '% (bare ' + s.bare + '/' + s.plant + ', world ' + fmt(100 * Life.cover(w).veg, 1) + '%)';
      }
    }
    say(line);
    const RAMP = [[0, [255, 250, 150]], [0.2, [150, 215, 90]], [0.45, [50, 160, 110]], [0.7, [40, 100, 160]], [1, [80, 40, 120]]];
    const times = [];
    for (let c = 0; c < w.n; c++) if (ids.id[c] === k && first[c] >= 0) times.push(first[c]);
    times.sort((a, b) => a - b);
    const span = Math.max(20, times[Math.floor(0.97 * (times.length - 1))] || last);      // the ramp covers the seeded landmass
    say('  ' + tag + ': ' + times.length + ' cells of the landmass turned green; half of those by tick ' + times[times.length >> 1] + ', 97 % by tick ' + span);
    const arrival = range => c => {
      if (!isLand(w, c)) return [14, 24, 56];
      if (w.ice[c] > 0.5) return [226, 232, 240];
      if (w.water[c] > LAKE) return [60, 100, 180];
      if (first[c] < 0) return Life.capacity(w, c) > TL.plantMin ? [150, 122, 92] : [112, 106, 102];
      const k2 = M.ramp(RAMP, first[c] / range), band = Math.floor(first[c] / 20) % 2 ? 0.84 : 1;
      return [k2[0] * band, k2[1] * band, k2[2] * band];
    };
    zoom(w, shot(tag + '-arrival'), c0, half, arrival(span));                     // (the close-up: the seeded landmass)
    M.custom(w, shot(tag + '-globe-arrival'), arrival(last), { rivers: false, marks: [c0] });   // (the planet: the whole run)
  };
  // lightning on a living world: sizes of `trials` fires, each started on a random flammable cell passing `pred`
  const strikes = (w, trials, seed, pred) => {
    const veg0 = Float32Array.from(w.veg), fert0 = Float32Array.from(w.fert), fauna0 = Float32Array.from(w.fauna), pick = DM.U.rng(seed), sizes = [];
    for (let i = 0; i < trials; i++) {
      w.veg.set(veg0); w.fert.set(fert0); w.fauna.set(fauna0); w.scorch.fill(0); w.fire.fill(0); Life.rebuild(w);
      let c = -1;
      for (let j = 0; j < 4000 && c < 0; j++) { const q = pick.int(0, w.n - 1); if (isLand(w, q) && w.veg[q] >= 0.15 && (!pred || pred(q))) c = q; }
      const r = c < 0 ? null : burnOut(w, c);
      if (r) sizes.push(r.size);
    }
    w.veg.set(veg0); w.fert.set(fert0); w.fauna.set(fauna0); w.scorch.fill(0); w.fire.fill(0); Life.rebuild(w);
    sizes.sort((a, b) => a - b);
    const q = p => sizes.length ? sizes[Math.min(sizes.length - 1, Math.floor(p * sizes.length))] : 0;
    return 'mean ' + fmt(sizes.reduce((a, b) => a + b, 0) / Math.max(1, sizes.length), 1) + ', median ' + q(0.5) + ', p90 ' + q(0.9) + ', p99 ' + q(0.99) + ', max ' + q(1) + ' cells (' + sizes.length + ' fires)';
  };
  const written = [];
  // helper worlds
  for (const seed of [1, 2]) { const w = planet(6, seed); M.biome(w, shot('biome-helper-s' + seed)); Life.habitability(w); M.field(w, 'hab', shot('hab-helper-s' + seed), 0, 1, { dimSea: true, rivers: false }); written.push('biome-helper-s' + seed, 'hab-helper-s' + seed); }
  { const w = plain(6, { moist: 0.55 }); wave(w, 'front-uniform', 100, 0.5, [15, 30, 60]); written.push('front-uniform-t0/15/30/60 (+ globe, arrival)'); }
  { const w = planet(6, 2), ids = landIds(w); wave(w, 'wave-helper', heart(w, ids, biggest(ids)), 0.7, [30, 60, 120, 240]); written.push('wave-helper-t0/30/60/120/240 (+ globe, arrival)'); }
  // a fire under drought on a helper continent: the front (orange) and the scar behind it
  {
    const w = planet(6, 2), ids = landIds(w), c0 = heart(w, ids, biggest(ids));
    Life.fill(w, 1);
    for (let c = 0; c < w.n; c++) if (isLand(w, c)) w.moist[c] *= 0.2;
    const buf = [], cnt = w.geo.ring(c0, 3, buf);
    for (let i = 0; i < cnt && burning(w) < 30; i++) {         // (a single spark may die: strike until one takes)
      if (burning(w) === 0) Life.ignite(w, buf[i]);
      for (let t = 0; t < 12 && burning(w) > 0 && burning(w) < 30; t++) { w.tick++; Life.burn(w); }
    }
    for (let t = 0; t < 16; t++) { w.tick++; Life.burn(w); }
    zoom(w, shot('fire-drought'), c0, 0.45, look(w));
    written.push('fire-drought');
  }
  // the real pipeline, when the neighbours exist
  if (REAL.Terrain && REAL.Climate && REAL.Hydro) {
    T.stub(DM, 'Terrain', REAL.Terrain); T.stub(DM, 'Climate', REAL.Climate); T.stub(DM, 'Hydro', REAL.Hydro);
    for (const seed of [1, 2, 3]) {
      const w = DM.State.create({ seed, L: 6 });
      DM.Terrain.generate(w);
      w.seaLevel = 0; w.heat = 0; w.phase = 'life';
      DM.Terrain.derive(w); DM.Climate.calibrate(w); DM.Hydro.update(w);
      Life.classify(w); Life.habitability(w);
      M.biome(w, shot('biome-terrain-s' + seed));
      M.field(w, 'hab', shot('hab-terrain-s' + seed), 0, 1, { dimSea: true, rivers: false });
      written.push('biome-terrain-s' + seed, 'hab-terrain-s' + seed);
      const h = new Array(16).fill(0);
      let l = 0;
      for (let c = 0; c < w.n; c++) if (isLand(w, c)) { h[w.biome[c]]++; l++; }
      say('  real pipeline seed ' + seed + ', % of land: ' + h.map((v, b) => b >= 2 && b <= 14 ? DM.BIOME_NAMES[b] + ' ' + fmt(100 * v / l, 1) : '').filter(Boolean).join(', '));
      // fires on the living world: lightning anywhere, in the woods, and in the woods under a full drought
      // (Climate takes 0.25 off `moist` there)
      Life.fill(w, 1);
      const woods = c => w.biome[c] === B.FOREST || w.biome[c] === B.TAIGA || w.biome[c] === B.RAINFOREST;
      let nw = 0, sp = 0, sup = 0, supD = 0;
      for (let c = 0; c < w.n; c++) {
        if (!isLand(w, c) || !woods(c) || w.veg[c] < 0.15) continue;
        const P = 0.55 * w.veg[c] * DM.U.sat(1.2 - 1.6 * w.moist[c]), PD = 0.55 * w.veg[c] * DM.U.sat(1.2 - 1.6 * Math.max(0, w.moist[c] - 0.25));
        nw++; sp += P; if (P > 0.347) sup++; if (PD > 0.347) supD++;
      }
      say('    woods: ' + nw + ' cells, mean P ' + fmt(sp / nw, 3) + ', above the threshold ' + fmt(100 * sup / nw, 1) + ' % of them (' + fmt(100 * supD / nw, 1) + ' % under a full drought)');
      say('    lightning anywhere:          ' + strikes(w, 300, 50 + seed));
      say('    lightning in the woods:      ' + strikes(w, 300, 60 + seed, woods));
      const moist0 = Float32Array.from(w.moist);
      for (let c = 0; c < w.n; c++) if (isLand(w, c)) w.moist[c] = Math.max(0, moist0[c] - 0.25);
      say('    in the woods, full drought:  ' + strikes(w, 150, 70 + seed, woods));
      w.moist.set(moist0);
      if (seed === 2) { const ids = landIds(w); wave(w, 'wave-terrain', heart(w, ids, biggest(ids)), 0.7, [30, 60, 120, 240, 480]); written.push('wave-terrain-t0/30/60/120/240/480 (+ globe, arrival)'); }
    }
  }
  say('  maps written to shots/life-*.png: ' + written.join(', '));
}
