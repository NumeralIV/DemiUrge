// DEMIURGE — tools/mapimg.js : write equirectangular PNG maps of a world from Node, so sim engineers can SEE
// what their module produces without the renderer. Zero dependencies (uses Node's zlib).
//
//   const M = require('../tools/mapimg.js');
//   M.elev(world, 'shots/elev.png');                 // hypsometric tint, sea in blues
//   M.biome(world, 'shots/biome.png');               // DM.BIOME colours
//   M.field(world, 'moist', 'shots/moist.png', 0, 1);            // any per-cell array (by name or the array itself), blue→yellow→red ramp
//   M.owner(world, 'shots/owner.png');               // settlement territories coloured by culture, settlement centres as white dots
//   M.custom(world, 'shots/x.png', c => [r, g, b]);  // your own colours, 0..255
//   M.overlay(...)                                   // see bottom: rivers / cells drawn on top of any base map
// Options (last argument): { w: 1024, h: 512, rivers: true, marks: [cell, ...], grid: false }
// North is up; longitude −180° is at the left edge. Then open the PNG with your image-reading tool.
'use strict';
const fs = require('fs');
const path = require('path');
const zlib = require('zlib');

const CRC = (() => { const t = new Uint32Array(256); for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = (c & 1) ? (0xedb88320 ^ (c >>> 1)) : (c >>> 1); t[n] = c >>> 0; } return t; })();
function crc32(buf) { let c = 0xffffffff; for (let i = 0; i < buf.length; i++) c = CRC[(c ^ buf[i]) & 255] ^ (c >>> 8); return (c ^ 0xffffffff) >>> 0; }
function chunk(type, data) {
  const len = Buffer.alloc(4); len.writeUInt32BE(data.length);
  const td = Buffer.concat([Buffer.from(type, 'ascii'), data]);
  const crc = Buffer.alloc(4); crc.writeUInt32BE(crc32(td));
  return Buffer.concat([len, td, crc]);
}
function encodePNG(w, h, rgb) {
  const raw = Buffer.alloc((w * 3 + 1) * h);
  for (let y = 0; y < h; y++) { raw[y * (w * 3 + 1)] = 0; rgb.copy(raw, y * (w * 3 + 1) + 1, y * w * 3, (y + 1) * w * 3); }
  const ihdr = Buffer.alloc(13); ihdr.writeUInt32BE(w, 0); ihdr.writeUInt32BE(h, 4); ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
  return Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', ihdr), chunk('IDAT', zlib.deflateSync(raw, { level: 6 })), chunk('IEND', Buffer.alloc(0))]);
}

function resolve(file) {
  const p = path.isAbsolute(file) ? file : path.resolve(__dirname, '..', file);
  fs.mkdirSync(path.dirname(p), { recursive: true });
  return p;
}

// Core: colorFn(cell) -> [r,g,b] 0..255
function custom(world, file, colorFn, opts) {
  opts = opts || {};
  const W = opts.w || 1024, H = opts.h || 512, geo = world.geo, n = world.n;
  // colour per cell once
  const cols = new Uint8Array(n * 3);
  for (let c = 0; c < n; c++) { const k = colorFn(c); cols[3 * c] = clamp255(k[0]); cols[3 * c + 1] = clamp255(k[1]); cols[3 * c + 2] = clamp255(k[2]); }
  if (opts.rivers !== false && world.rivers && world.rivers.length) {
    for (const r of world.rivers) { const cells = r.cells || r; for (let i = 0; i < cells.length; i++) { const c = cells[i]; cols[3 * c] = 70; cols[3 * c + 1] = 150; cols[3 * c + 2] = 255; } }
  }
  if (opts.marks) for (const c of opts.marks) { if (c >= 0 && c < n) { cols[3 * c] = 255; cols[3 * c + 1] = 255; cols[3 * c + 2] = 255; } }
  const rgb = Buffer.alloc(W * H * 3);
  let hint = -1;
  for (let y = 0; y < H; y++) {
    const lat = (0.5 - (y + 0.5) / H) * Math.PI, cl = Math.cos(lat), sl = Math.sin(lat);
    for (let x = 0; x < W; x++) {
      const lon = ((x + 0.5) / W - 0.5) * 2 * Math.PI;
      const c = geo.cellAt(cl * Math.cos(lon), sl, cl * Math.sin(lon), hint); hint = c;
      const o = (y * W + x) * 3;
      rgb[o] = cols[3 * c]; rgb[o + 1] = cols[3 * c + 1]; rgb[o + 2] = cols[3 * c + 2];
      if (opts.grid && ((x % (W / 12) | 0) === 0 || (y % (H / 6) | 0) === 0)) { rgb[o] = (rgb[o] + 255) >> 1; rgb[o + 1] = (rgb[o + 1] + 255) >> 1; rgb[o + 2] = (rgb[o + 2] + 255) >> 1; }
    }
  }
  const p = resolve(file);
  fs.writeFileSync(p, encodePNG(W, H, rgb));
  return p;
}
function clamp255(v) { return v < 0 ? 0 : v > 255 ? 255 : v | 0; }
function mix(a, b, t) { t = t < 0 ? 0 : t > 1 ? 1 : t; return [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t]; }
function ramp(stops, t) {   // stops: [[t, [r,g,b]], ...] ascending
  if (t <= stops[0][0]) return stops[0][1];
  for (let i = 1; i < stops.length; i++) if (t <= stops[i][0]) return mix(stops[i - 1][1], stops[i][1], (t - stops[i - 1][0]) / (stops[i][0] - stops[i - 1][0]));
  return stops[stops.length - 1][1];
}

const SEA_RAMP = [[0, [150, 215, 235]], [0.6, [60, 140, 200]], [2.5, [25, 70, 150]], [6.5, [8, 20, 70]]];
const LAND_RAMP = [[0, [88, 150, 80]], [0.6, [140, 175, 95]], [1.8, [200, 190, 120]], [3.5, [150, 120, 95]], [5.5, [200, 200, 205]], [8, [255, 255, 255]]];
function elev(world, file, opts) {
  const sea = world.seaLevel;
  return custom(world, file, c => { const e = world.elev[c]; return e < sea ? ramp(SEA_RAMP, sea - e) : ramp(LAND_RAMP, e - sea); }, opts);
}
const BIOME_COL = [
  [20, 60, 140], [70, 150, 200], [230, 215, 160], [225, 195, 120], [190, 180, 90], [130, 185, 90], [45, 125, 60], [15, 90, 45],
  [60, 100, 85], [160, 165, 140], [240, 245, 250], [120, 110, 105], [255, 255, 255], [70, 120, 110], [70, 55, 50], [200, 225, 240],
];
function biome(world, file, opts) { return custom(world, file, c => BIOME_COL[world.biome[c]] || [255, 0, 255], opts); }
const HEAT = [[0, [20, 30, 120]], [0.25, [40, 140, 210]], [0.5, [240, 235, 150]], [0.75, [235, 130, 50]], [1, [150, 20, 30]]];
function field(world, nameOrArray, file, min, max, opts) {
  const a = typeof nameOrArray === 'string' ? world[nameOrArray] : nameOrArray;
  if (min === undefined || max === undefined) { min = Infinity; max = -Infinity; for (let c = 0; c < a.length; c++) { if (a[c] < min) min = a[c]; if (a[c] > max) max = a[c]; } }
  const span = (max - min) || 1, sea = world.seaLevel;
  return custom(world, file, c => {
    const k = ramp(HEAT, (a[c] - min) / span);
    return (opts && opts.dimSea && world.elev[c] < sea) ? [k[0] * 0.45, k[1] * 0.45, k[2] * 0.55] : k;
  }, opts);
}
function owner(world, file, opts) {
  const sea = world.seaLevel, centres = [];
  for (const s of world.settlements) if (s && !s.dead) centres.push(s.cell);
  return custom(world, file, c => {
    if (world.elev[c] < sea) return [18, 40, 90];
    const o = world.owner[c];
    if (o < 0) return [70, 75, 70];
    const s = world.settlements[o]; if (!s) return [255, 0, 255];
    const cu = world.cultures[s.culture], col = cu && cu.color ? cu.color : [1, 1, 1];
    const f = 0.55 + 0.45 * Math.min(1, world.farm[c] + world.urban[c]);
    return [col[0] * 255 * f, col[1] * 255 * f, col[2] * 255 * f];
  }, Object.assign({ marks: centres }, opts || {}));
}

module.exports = { custom, elev, biome, field, owner, encodePNG, ramp, mix };
