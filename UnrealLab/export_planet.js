// Export a JS-simulated planet (terrain + climate + hydro + life) for the Unreal graphics lab.
// node UnrealLab/export_planet.js [seed]  ->  UnrealLab/data/planet_<seed>.bin + .json
'use strict';
const fs = require('fs'), path = require('path');
const DM = require('../tools/load.js')({ only: ['terrain', 'climate', 'hydro', 'life'] });
const SEED = +(process.argv[2] || 7);
const w = DM.State.create({ seed: SEED, L: 6 }), geo = w.geo, n = w.n;
DM.Terrain.generate(w); w.seaLevel = 0; w.heat = 0; w.phase = 'history';
DM.Terrain.derive(w); DM.Climate.update(w); DM.Hydro.update(w); DM.Life.classify(w); DM.Life.fill(w, 1);
for (let i = 0; i < 3; i++) { DM.Climate.update(w); DM.Life.classify(w); }
DM.Life.habitability(w);
const fields = {};
for (const k of Object.keys(w)) { const v = w[k]; if (ArrayBuffer.isView(v) && v.length === n) fields[k] = v.constructor.name; }
const riverT = DM.Hydro.riverT ? DM.Hydro.riverT(w) : 1;
const out = [];
const meta = { seed: SEED, n, nTri: geo.tris.length / 3, R: DM.R, seaLevel: w.seaLevel, riverT, layout: [] };
function put(name, arr) { const buf = Buffer.from(new Float32Array(arr).buffer); meta.layout.push({ name, type: 'f32', count: arr.length }); out.push(buf); }
function putU(name, arr, T) { const ta = T === 'u8' ? new Uint8Array(arr) : new Uint32Array(arr); meta.layout.push({ name, type: T, count: ta.length }); out.push(Buffer.from(ta.buffer)); }
put('pos', geo.pos); putU('tris', geo.tris, 'u32');
for (const k of ['elev', 'temp', 'moist', 'veg', 'flow', 'ice', 'hab', 'fauna', 'slope']) if (fields[k]) put(k, w[k]);
if (fields.biome) putU('biome', w.biome, 'u8');
if (fields.lake) putU('lake', w.lake, 'u8');
fs.writeFileSync(path.join(__dirname, 'data', `planet_${SEED}.bin`), Buffer.concat(out));
fs.writeFileSync(path.join(__dirname, 'data', `planet_${SEED}.json`), JSON.stringify(meta, null, 1));
let lo = Infinity, hi = -Infinity, sea = 0; for (let c = 0; c < n; c++) { lo = Math.min(lo, w.elev[c]); hi = Math.max(hi, w.elev[c]); if (w.elev[c] < w.seaLevel) sea++; }
console.log({ n, fields, elev: [lo.toFixed(2), hi.toFixed(2)], seaFrac: (sea / n).toFixed(3), riverT, layout: meta.layout.map(l => l.name) });
// Fixed-layout file for the Unreal lab: int32 n, int32 nTri, f32 R, f32 seaLevel, f32 riverT,
// then pos f32[3n], tris u32[3nTri], elev, temp, moist, veg, flow, ice, hab, slope, water f32[n] each, biome u8[n].
{
  const parts = [];
  const hdr = Buffer.alloc(20); hdr.writeInt32LE(n, 0); hdr.writeInt32LE(geo.tris.length / 3, 4);
  hdr.writeFloatLE(DM.R, 8); hdr.writeFloatLE(w.seaLevel, 12); hdr.writeFloatLE(riverT, 16); parts.push(hdr);
  parts.push(Buffer.from(new Float32Array(geo.pos).buffer), Buffer.from(new Uint32Array(geo.tris).buffer));
  for (const k of ['elev', 'temp', 'moist', 'veg', 'flow', 'ice', 'hab', 'slope', 'water']) parts.push(Buffer.from(new Float32Array(w[k] || new Float32Array(n)).buffer));
  parts.push(Buffer.from(new Uint8Array(w.biome).buffer));
  fs.writeFileSync(path.join(__dirname, 'data', `planet_${SEED}.lab`), Buffer.concat(parts));
  const t = (k) => { let a = Infinity, b = -Infinity; for (const v of w[k]) { a = Math.min(a, v); b = Math.max(b, v); } return [+a.toFixed(2), +b.toFixed(2)]; };
  console.log('ranges', { temp: t('temp'), moist: t('moist'), veg: t('veg'), flow: t('flow'), ice: t('ice'), hab: t('hab'), water: t('water') });
}
