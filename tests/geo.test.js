// DEMIURGE — tests/geo.test.js : the icosphere grid, DM.Geo (FOUNDATION.md §4).
//   node tools/test.js geo            (T_VERBOSE=1 also prints the measured timings)
'use strict';
const T = require('../tools/t.js');
const DM = require('../tools/load.js')();
const Geo = DM.Geo;

const PI = Math.PI;
const G = L => Geo.get(L);
const ALL = [0, 1, 2, 3, 4, 5, 6, 7];
const nOf = L => 10 * Math.pow(4, L) + 2;
const now = () => Number(process.hrtime.bigint()) / 1e6;
const say = msg => { if (process.env.T_VERBOSE) console.log('       ' + msg); };

// Test-local deterministic random numbers (arithmetic only, so identical on every machine).
function rng(seed) {
  let s = seed >>> 0;
  return function () {
    s = (s + 0x6D2B79F5) >>> 0;
    let t = s;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}
// Uniform random direction by rejection sampling; NOT normalized (length 0.2 … 1).
function randDir(r, o) {
  let x, y, z, d;
  do { x = r() * 2 - 1; y = r() * 2 - 1; z = r() * 2 - 1; d = x * x + y * y + z * z; } while (d > 1 || d < 0.04);
  o[0] = x; o[1] = y; o[2] = z;
  return o;
}
function dotQ(g, x, y, z, c) { return x * g.pos[3 * c] + y * g.pos[3 * c + 1] + z * g.pos[3 * c + 2]; }
function dotC(g, a, b) { return dotQ(g, g.pos[3 * a], g.pos[3 * a + 1], g.pos[3 * a + 2], b); }
// Brute-force nearest cell: largest dot product over all cells (first one wins a tie).
const BR = { id: -1, dot: 0 };
function brute(g, x, y, z) {
  const pos = g.pos, n = g.n;
  let best = -Infinity, bi = -1;
  for (let i = 0, o = 0; i < n; i++, o += 3) {
    const d = x * pos[o] + y * pos[o + 1] + z * pos[o + 2];
    if (d > best) { best = d; bi = i; }
  }
  BR.id = bi; BR.dot = best;
  return bi;
}
// Independent angle between two cells.
function angBetween(g, a, b) {
  const p = g.pos;
  const ax = p[3 * a], ay = p[3 * a + 1], az = p[3 * a + 2];
  const bx = p[3 * b], by = p[3 * b + 1], bz = p[3 * b + 2];
  const cx = ay * bz - az * by, cy = az * bx - ax * bz, cz = ax * by - ay * bx;
  return Math.atan2(Math.sqrt(cx * cx + cy * cy + cz * cz), ax * bx + ay * by + az * bz);
}
function slotOf(g, a, b) {
  for (let j = 0; j < g.nbrN[a]; j++) if (g.nbr[6 * a + j] === b) return j;
  return -1;
}
function sortedCopy(arr, count) {
  const a = Array.prototype.slice.call(arr, 0, count);
  a.sort((x, y) => x - y);
  return a;
}
function sameList(a, b, msg) {
  T.eq(a.length, b.length, msg + ': size');
  for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) T.ok(false, msg + ': differs at index ' + i + ' (' + a[i] + ' vs ' + b[i] + ')');
}
// Hop distance from c to every cell, by a plain BFS written independently of geo.ring.
function hopDist(g, c) {
  const dist = new Int32Array(g.n).fill(-1);
  let frontier = [c];
  dist[c] = 0;
  while (frontier.length) {
    const next = [];
    for (const a of frontier) {
      for (let j = 0; j < g.nbrN[a]; j++) {
        const m = g.nbr[6 * a + j];
        if (dist[m] < 0) { dist[m] = dist[a] + 1; next.push(m); }
      }
    }
    frontier = next;
  }
  return dist;
}

// ---------------------------------------------------------------------------------------------------

T.test('geo: API surface exists', () => {
  T.ok(Geo && typeof Geo.build === 'function' && typeof Geo.get === 'function', 'DM.Geo.build / get');
  const g = G(1);
  for (const f of ['prefix', 'cellAt', 'angle', 'within', 'ring', 'dir', 'basis']) T.eq(typeof g[f], 'function', 'geo.' + f);
});

T.test('geo: n, nTri, prefix and array layouts for L = 0..7', () => {
  T.eq(nOf(6), 40962);
  T.eq(nOf(7), 163842);
  for (const L of ALL) {
    const g = G(L), n = nOf(L);
    T.eq(g.L, L, 'L');
    T.eq(g.n, n, 'n at L=' + L);
    T.eq(g.nTri, 20 * Math.pow(4, L), 'nTri at L=' + L);
    T.ok(g.pos instanceof Float32Array && g.pos.length === 3 * n, 'pos');
    T.ok(g.lat instanceof Float32Array && g.lat.length === n, 'lat');
    T.ok(g.lon instanceof Float32Array && g.lon.length === n, 'lon');
    T.ok(g.nbr instanceof Int32Array && g.nbr.length === 6 * n, 'nbr');
    T.ok(g.nbrN instanceof Uint8Array && g.nbrN.length === n, 'nbrN');
    T.ok(g.tris instanceof Uint32Array && g.tris.length === 3 * g.nTri, 'tris');
    T.ok(typeof g.step === 'number' && g.step > 0 && g.step < 1.2, 'step');
    for (const k of ALL) T.eq(g.prefix(k), nOf(k), 'prefix(' + k + ')');
  }
});

T.test('geo: build rejects levels outside 0..7', () => {
  for (const bad of [-1, 8, 1.5, NaN, undefined, null, '3', Infinity]) {
    T.throws(() => Geo.build(bad), 'build(' + bad + ') should throw');
    T.throws(() => Geo.get(bad), 'get(' + bad + ') should throw');
  }
});

T.test('geo: get caches, build makes a fresh identical instance', () => {
  T.ok(Geo.get(3) === Geo.get(3), 'get returns the cached instance');
  const a = Geo.get(4), b = Geo.build(4);
  T.ok(a !== b && a.pos !== b.pos, 'build returns a new object');
  for (const f of ['pos', 'lat', 'lon', 'nbr', 'nbrN', 'tris']) {
    T.eq(a[f].length, b[f].length, f + ' length');
    for (let i = 0; i < a[f].length; i++) if (a[f][i] !== b[f][i]) T.ok(false, f + ' differs at ' + i);
  }
  T.eq(a.step, b.step, 'step');
});

T.test('geo: cell numbering is pinned (saves store per-cell data by id, so ids must never change)', () => {
  const fnv = arr => { let h = 2166136261; for (let i = 0; i < arr.length; i++) { h ^= arr[i]; h = Math.imul(h, 16777619); } return h >>> 0; };
  const g0 = G(0);
  T.eq(Array.from(g0.tris), [0, 2, 1, 0, 3, 2, 0, 4, 3, 0, 5, 4, 0, 1, 5, 1, 2, 6, 2, 3, 7, 3, 4, 8, 4, 5, 9, 5, 1, 10,
    6, 2, 7, 7, 3, 8, 8, 4, 9, 9, 5, 10, 10, 1, 6, 6, 7, 11, 7, 8, 11, 8, 9, 11, 9, 10, 11, 10, 6, 11], 'level 0 triangles');
  T.eq(Array.from(g0.nbr), [2, 1, 5, 4, 3, -1, 0, 2, 6, 10, 5, -1, 1, 0, 3, 7, 6, -1, 2, 0, 4, 8, 7, -1, 3, 0, 5, 9, 8, -1,
    4, 0, 1, 10, 9, -1, 1, 2, 7, 11, 10, -1, 2, 3, 8, 11, 6, -1, 3, 4, 9, 11, 7, -1, 4, 5, 10, 11, 8, -1,
    5, 1, 6, 11, 9, -1, 6, 7, 8, 9, 10, -1], 'level 0 neighbours');
  T.eq([fnv(G(2).tris), fnv(G(2).nbr)], [2367549661, 110849663], 'level 2 topology hash');
  T.eq([fnv(G(6).tris), fnv(G(6).nbr)], [3449796213, 3473565537], 'level 6 topology hash');
  // cell 12, the first cell created by subdivision, is the midpoint of edge 0-2
  const g1 = G(1);
  T.near(g1.pos[36], 0.16245985, 1e-7); T.near(g1.pos[37], 0.85065081, 1e-7); T.near(g1.pos[38], 0.5, 1e-7);
});

T.test('geo: level 0 orientation (poles and the two rings), kept at every level', () => {
  const deg = 180 / PI, ringLat = Math.atan(0.5);
  for (const L of [0, 3, 6, 7]) {
    const g = G(L);
    T.eq([g.pos[0], g.pos[1], g.pos[2]], [0, 1, 0], 'vertex 0 is the north pole');
    T.eq([g.pos[33], g.pos[34], g.pos[35]], [0, -1, 0], 'vertex 11 is the south pole');
    T.near(g.lat[0], PI / 2, 2e-7, 'lat of north pole');
    T.near(g.lat[11], -PI / 2, 2e-7, 'lat of south pole');
    for (let k = 0; k < 5; k++) {
      T.near(g.lat[1 + k], ringLat, 1e-6, 'upper ring latitude');
      T.near(g.lat[6 + k], -ringLat, 1e-6, 'lower ring latitude');
      const up = ((g.lon[1 + k] * deg - 72 * k) % 360 + 540) % 360 - 180;
      const lo = ((g.lon[6 + k] * deg - (36 + 72 * k)) % 360 + 540) % 360 - 180;
      T.near(up, 0, 1e-4, 'upper ring longitude of vertex ' + (1 + k));
      T.near(lo, 0, 1e-4, 'lower ring longitude of vertex ' + (6 + k));
    }
  }
});

T.test('geo: every position is unit length', () => {
  for (const L of ALL) {
    const g = G(L);
    let worst = 0;
    for (let i = 0; i < g.n; i++) {
      const x = g.pos[3 * i], y = g.pos[3 * i + 1], z = g.pos[3 * i + 2];
      const e = Math.abs(Math.sqrt(x * x + y * y + z * z) - 1);
      if (e > worst) worst = e;
    }
    T.ok(worst < 2e-7, 'L=' + L + ' worst length error ' + worst);
  }
});

T.test('geo: lat / lon are consistent with pos and stay inside [-pi/2, pi/2] and [-pi, pi]', () => {
  for (const L of ALL) {
    const g = G(L);
    for (let i = 0; i < g.n; i++) {
      const x = g.pos[3 * i], y = g.pos[3 * i + 1], z = g.pos[3 * i + 2];
      const la = g.lat[i], lo = g.lon[i];
      if (!(Math.abs(la) <= PI / 2) || !(Math.abs(lo) <= PI)) T.ok(false, 'range at cell ' + i + ': ' + la + ', ' + lo);
      if (!(Math.cos(la) >= 0)) T.ok(false, 'cos(lat) < 0 at cell ' + i);
      if (Math.abs(la - Math.atan2(y, Math.sqrt(x * x + z * z))) > 5e-7) T.ok(false, 'lat at cell ' + i);
      if (Math.abs(la - Math.asin(Math.max(-1, Math.min(1, y)))) > 3e-5) T.ok(false, 'lat vs asin(y) at cell ' + i);
      let dl = lo - Math.atan2(z, x);
      if (dl > PI) dl -= 2 * PI; else if (dl < -PI) dl += 2 * PI;
      if (Math.abs(dl) > 5e-7) T.ok(false, 'lon at cell ' + i + ' (' + lo + ' vs ' + Math.atan2(z, x) + ')');
      // and back again: (cos lat cos lon, sin lat, cos lat sin lon) reproduces pos
      const cl = Math.cos(la);
      if (Math.abs(cl * Math.cos(lo) - x) > 1e-6 || Math.abs(Math.sin(la) - y) > 1e-6 || Math.abs(cl * Math.sin(lo) - z) > 1e-6) {
        T.ok(false, 'pos from lat/lon at cell ' + i);
      }
    }
  }
});

T.test('geo: neighbour lists: 5 or 6 entries, exactly twelve pentagons (ids 0-11), -1 padding', () => {
  for (const L of ALL) {
    const g = G(L);
    let fives = 0, total = 0;
    for (let c = 0; c < g.n; c++) {
      const k = g.nbrN[c];
      total += k;
      if (k === 5) fives++;
      if (c < 12) {
        if (k !== 5) T.ok(false, 'cell ' + c + ' should be a pentagon');
        if (g.nbr[6 * c + 5] !== -1) T.ok(false, 'pentagon ' + c + ' 6th slot should be -1');
      } else if (k !== 6) T.ok(false, 'cell ' + c + ' should have 6 neighbours, has ' + k);
      for (let j = 0; j < k; j++) {
        const m = g.nbr[6 * c + j];
        if (!(m >= 0 && m < g.n) || m === c) T.ok(false, 'bad neighbour ' + m + ' of cell ' + c);
        for (let i = 0; i < j; i++) if (g.nbr[6 * c + i] === m) T.ok(false, 'duplicate neighbour of cell ' + c);
      }
    }
    T.eq(fives, 12, 'pentagon count at L=' + L);
    T.eq(total, 6 * g.n - 12, 'total neighbour slots at L=' + L);
  }
});

T.test('geo: neighbour relation is symmetric', () => {
  for (const L of ALL) {
    const g = G(L);
    for (let c = 0; c < g.n; c++) {
      for (let j = 0; j < g.nbrN[c]; j++) {
        const m = g.nbr[6 * c + j];
        if (slotOf(g, m, c) < 0) T.ok(false, 'L=' + L + ': ' + m + ' is a neighbour of ' + c + ' but not the reverse');
      }
    }
  }
});

T.test('geo: neighbours are ordered counter-clockwise seen from outside (one full turn)', () => {
  const e = [0, 0, 0], nn = [0, 0, 0];
  for (const L of ALL) {
    const g = G(L), p = g.pos;
    for (let c = 0; c < g.n; c++) {
      const k = g.nbrN[c];
      const cx = p[3 * c], cy = p[3 * c + 1], cz = p[3 * c + 2];
      g.basis(c, e, nn);
      let turn = 0, prev = 0;
      for (let j = 0; j <= k; j++) {
        const a = g.nbr[6 * c + (j % k)], b = g.nbr[6 * c + ((j + 1) % k)];
        const ax = p[3 * a], ay = p[3 * a + 1], az = p[3 * a + 2];
        const bx = p[3 * b], by = p[3 * b + 1], bz = p[3 * b + 2];
        // det(c, a, b) > 0  <=>  b is counter-clockwise of a around c for an observer outside
        const det = cx * (ay * bz - az * by) + cy * (az * bx - ax * bz) + cz * (ax * by - ay * bx);
        if (j < k && !(det > 0)) T.ok(false, 'L=' + L + ' cell ' + c + ': neighbours ' + a + ' -> ' + b + ' are not counter-clockwise');
        const th = Math.atan2(ax * nn[0] + ay * nn[1] + az * nn[2], ax * e[0] + ay * e[1] + az * e[2]);
        if (j > 0) { let d = th - prev; if (d <= 0) d += 2 * PI; turn += d; }
        prev = th;
      }
      if (Math.abs(turn - 2 * PI) > 1e-6) T.ok(false, 'L=' + L + ' cell ' + c + ': neighbours wind ' + turn + ' rad, expected 2*pi');
    }
  }
});

T.test('geo: triangles are valid, CCW seen from outside, a closed manifold, and match the neighbour lists', () => {
  for (const L of ALL) {
    const g = G(L), p = g.pos, n = g.n;
    const used = new Uint8Array(6 * n);
    T.eq(3 * g.nTri, 6 * n - 12, 'directed edge count at L=' + L);
    for (let t = 0; t < g.nTri; t++) {
      const a = g.tris[3 * t], b = g.tris[3 * t + 1], c = g.tris[3 * t + 2];
      if (!(a < n && b < n && c < n) || a === b || b === c || a === c) T.ok(false, 'bad triangle ' + t);
      const ax = p[3 * a], ay = p[3 * a + 1], az = p[3 * a + 2];
      const ux = p[3 * b] - ax, uy = p[3 * b + 1] - ay, uz = p[3 * b + 2] - az;
      const vx = p[3 * c] - ax, vy = p[3 * c + 1] - ay, vz = p[3 * c + 2] - az;
      const nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
      const out = nx * (ax + p[3 * b] + p[3 * c]) + ny * (ay + p[3 * b + 1] + p[3 * c + 1]) + nz * (az + p[3 * b + 2] + p[3 * c + 2]);
      if (!(out > 0)) T.ok(false, 'L=' + L + ' triangle ' + t + ' is not counter-clockwise seen from outside');
      // each directed edge x -> y of the triangle (third corner w): y is a neighbour of x, and w is
      // the next neighbour counter-clockwise after y
      for (let e = 0; e < 3; e++) {
        const x = g.tris[3 * t + e], y = g.tris[3 * t + (e + 1) % 3], w = g.tris[3 * t + (e + 2) % 3];
        const j = slotOf(g, x, y);
        if (j < 0) T.ok(false, 'L=' + L + ' triangle edge ' + x + '-' + y + ' is not in the neighbour lists');
        used[6 * x + j]++;
        if (g.nbr[6 * x + (j + 1) % g.nbrN[x]] !== w) T.ok(false, 'L=' + L + ' triangle ' + t + ' disagrees with neighbour order at ' + x);
      }
    }
    // every directed edge belongs to exactly one triangle
    for (let c = 0; c < n; c++) for (let j = 0; j < g.nbrN[c]; j++) if (used[6 * c + j] !== 1) T.ok(false, 'L=' + L + ' edge ' + c + '->' + g.nbr[6 * c + j] + ' used ' + used[6 * c + j] + ' times');
  }
});

T.test('geo: the mesh is the convex hull of the stored positions (why greedy cellAt is exact)', () => {
  // For every triangle and each of its edges, the vertex on the far side of that edge lies strictly
  // below the triangle's plane. Locally convex everywhere => convex hull => Delaunay on the sphere,
  // so "no neighbour has a larger dot product" implies "no cell has a larger dot product".
  for (const L of ALL) {
    const g = G(L), p = g.pos;
    let minDepth = Infinity;
    for (let t = 0; t < g.nTri; t++) {
      for (let e = 0; e < 3; e++) {
        const a = g.tris[3 * t + e], b = g.tris[3 * t + (e + 1) % 3], c = g.tris[3 * t + (e + 2) % 3];
        const k = g.nbrN[a];
        const d = g.nbr[6 * a + (slotOf(g, a, b) + k - 1) % k];   // across edge a-b from c
        const ax = p[3 * a], ay = p[3 * a + 1], az = p[3 * a + 2];
        const ux = p[3 * b] - ax, uy = p[3 * b + 1] - ay, uz = p[3 * b + 2] - az;
        const vx = p[3 * c] - ax, vy = p[3 * c + 1] - ay, vz = p[3 * c + 2] - az;
        const nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
        const depth = -(nx * (p[3 * d] - ax) + ny * (p[3 * d + 1] - ay) + nz * (p[3 * d + 2] - az)) / Math.sqrt(nx * nx + ny * ny + nz * nz);
        if (depth < minDepth) minDepth = depth;
      }
    }
    // float32 rounding moves a position by at most ~1e-7; demand a margin far above that
    T.ok(minDepth > 0.1 * g.step * g.step && minDepth > 1e-5, 'L=' + L + ' smallest convexity margin ' + minDepth);
  }
});

T.test('geo: Euler characteristic V - E + F = 2', () => {
  for (const L of ALL) {
    const g = G(L);
    let E;
    if (L <= 5) {                                 // count distinct undirected edges straight from the triangles
      const set = new Set();
      for (let t = 0; t < g.nTri; t++) {
        for (let e = 0; e < 3; e++) {
          const a = g.tris[3 * t + e], b = g.tris[3 * t + (e + 1) % 3];
          set.add(a < b ? a * g.n + b : b * g.n + a);
        }
      }
      E = set.size;
    } else {                                      // from the neighbour lists (each edge is listed twice)
      let s = 0;
      for (let c = 0; c < g.n; c++) s += g.nbrN[c];
      E = s / 2;
    }
    T.eq(E, 30 * Math.pow(4, L), 'edge count at L=' + L);
    T.eq(g.n - E + g.nTri, 2, 'V - E + F at L=' + L);
  }
});

T.test('geo: prefix property: level k is exactly the first prefix(k) cells of level k+1', () => {
  for (let k = 0; k < 7; k++) {
    const a = G(k), b = G(k + 1);
    T.eq(b.prefix(k), a.n);
    for (let i = 0; i < 3 * a.n; i++) if (a.pos[i] !== b.pos[i]) T.ok(false, 'pos differs between L=' + k + ' and L=' + (k + 1) + ' at ' + i);
    for (let i = 0; i < a.n; i++) {
      if (a.lat[i] !== b.lat[i] || a.lon[i] !== b.lon[i]) T.ok(false, 'lat/lon differs at cell ' + i);
      if (a.nbrN[i] !== b.nbrN[i]) T.ok(false, 'nbrN differs at cell ' + i);
    }
  }
});

T.test('geo: new cells are normalized edge midpoints; neighbour slots line up across levels', () => {
  for (let k = 0; k < 7; k++) {
    const a = G(k), b = G(k + 1);
    for (let c = 0; c < a.n; c++) {
      for (let j = 0; j < a.nbrN[c]; j++) {
        const far = a.nbr[6 * c + j], m = b.nbr[6 * c + j];
        if (m < a.n) T.ok(false, 'finer neighbour of an old cell must be a new cell');
        let x = a.pos[3 * c] + a.pos[3 * far], y = a.pos[3 * c + 1] + a.pos[3 * far + 1], z = a.pos[3 * c + 2] + a.pos[3 * far + 2];
        const inv = 1 / Math.sqrt(x * x + y * y + z * z);
        x *= inv; y *= inv; z *= inv;
        if (Math.abs(b.pos[3 * m] - x) > 1e-6 || Math.abs(b.pos[3 * m + 1] - y) > 1e-6 || Math.abs(b.pos[3 * m + 2] - z) > 1e-6) {
          T.ok(false, 'L=' + k + ' cell ' + c + ' slot ' + j + ': finer neighbour is not the midpoint toward ' + far);
        }
      }
    }
  }
});

T.test('geo: step is the mean neighbour angle', () => {
  T.near(G(0).step, Math.acos(1 / Math.sqrt(5)), 1e-9, 'icosahedron edge angle');
  for (const L of ALL) {
    const g = G(L);
    let sum = 0, cnt = 0, lo = Infinity, hi = 0;
    for (let c = 0; c < g.n; c++) {
      for (let j = 0; j < g.nbrN[c]; j++) {
        const a = angBetween(g, c, g.nbr[6 * c + j]);
        sum += a; cnt++;
        if (a < lo) lo = a;
        if (a > hi) hi = a;
      }
    }
    const mean = sum / cnt;
    T.ok(Math.abs(g.step - mean) < 1e-6 * mean, 'L=' + L + ' step ' + g.step + ' vs measured mean ' + mean);
    T.ok(lo > 0.8 * g.step && hi < 1.2 * g.step, 'L=' + L + ' edge angles ' + lo + ' .. ' + hi + ' stay near step');
  }
  T.near(G(6).step, 0.018886, 2e-6, 'step at L=6');
});

T.test('geo: angle', () => {
  const g = G(5), r = rng(11);
  T.near(G(0).angle(0, 1), Math.acos(1 / Math.sqrt(5)), 1e-7, 'pole to upper ring');
  T.near(g.angle(0, 11), PI, 1e-7, 'pole to pole');
  for (let i = 0; i < g.n; i += 97) T.eq(g.angle(i, i), 0, 'angle to itself is exactly 0');
  for (let i = 0; i < 3000; i++) {
    const a = (r() * g.n) | 0, b = (r() * g.n) | 0;
    const x = g.angle(a, b);
    if (x !== g.angle(b, a)) T.ok(false, 'angle is not symmetric');
    if (!(x >= 0 && x <= PI)) T.ok(false, 'angle out of range');
    if (Math.abs(x - angBetween(g, a, b)) > 1e-12) T.ok(false, 'angle mismatch');
    if (Math.abs(Math.cos(x) - dotC(g, a, b)) > 1e-6) T.ok(false, 'angle disagrees with the dot product');
  }
});

T.test('geo: dir copies the direction and returns out', () => {
  const g = G(4);
  const o = new Float32Array(3), arr = [9, 9, 9, 'keep'];
  for (const c of [0, 11, 12, 777, g.n - 1]) {
    T.ok(g.dir(c, o) === o, 'returns out');
    T.eq([o[0], o[1], o[2]], [g.pos[3 * c], g.pos[3 * c + 1], g.pos[3 * c + 2]]);
    g.dir(c, arr);
    T.eq(arr, [g.pos[3 * c], g.pos[3 * c + 1], g.pos[3 * c + 2], 'keep']);
  }
});

T.test('geo: basis is orthonormal, tangent, right-handed and follows east = normalize(cross(Y, n))', () => {
  const e = new Float32Array(3), nn = new Float32Array(3), E = [0, 0, 0], N = [0, 0, 0];
  for (const L of [0, 1, 2, 3, 4, 5, 6]) {
    const g = G(L), p = g.pos;
    for (let c = 0; c < g.n; c++) {
      g.basis(c, E, N);
      const x = p[3 * c], y = p[3 * c + 1], z = p[3 * c + 2];
      const ee = E[0] * E[0] + E[1] * E[1] + E[2] * E[2], nnn = N[0] * N[0] + N[1] * N[1] + N[2] * N[2];
      const en = E[0] * N[0] + E[1] * N[1] + E[2] * N[2];
      const ep = E[0] * x + E[1] * y + E[2] * z, np = N[0] * x + N[1] * y + N[2] * z;
      if (Math.abs(ee - 1) > 1e-12 || Math.abs(nnn - 1) > 1e-12) T.ok(false, 'basis not unit length at cell ' + c);
      if (Math.abs(en) > 1e-12) T.ok(false, 'east and north not perpendicular at cell ' + c);
      if (Math.abs(ep) > 2e-7 || Math.abs(np) > 2e-7) T.ok(false, 'basis not tangent at cell ' + c);
      // east x north = outward normal
      const ux = E[1] * N[2] - E[2] * N[1], uy = E[2] * N[0] - E[0] * N[2], uz = E[0] * N[1] - E[1] * N[0];
      if (Math.abs(ux - x) > 2e-7 || Math.abs(uy - y) > 2e-7 || Math.abs(uz - z) > 2e-7) T.ok(false, 'east x north != n at cell ' + c);
      if (c === 0 || c === 11) {
        T.eq(E, [1, 0, 0], 'east at a pole is +X');
        T.eq(N, [0, 0, c === 0 ? -1 : 1], 'north at a pole is cross(n, east)');
      } else {
        const h = Math.sqrt(x * x + z * z);       // cross(Y, n) = (z, 0, -x)
        if (Math.abs(E[0] - z / h) > 1e-7 || E[1] !== 0 || Math.abs(E[2] + x / h) > 1e-7) T.ok(false, 'east formula at cell ' + c);
        if (!(N[1] > 0)) T.ok(false, 'north should point toward +Y at cell ' + c);
      }
    }
  }
  // typed-array outputs
  G(3).basis(100, e, nn);
  G(3).basis(100, E, N);
  for (let i = 0; i < 3; i++) { T.near(e[i], E[i], 1e-7); T.near(nn[i], N[i], 1e-7); }
});

T.test('geo: basis agrees with DM.V3.basis (skipped while vec.js is absent)', () => {
  if (!DM.V3 || typeof DM.V3.basis !== 'function') return;
  const g = G(4), d = [0, 0, 0], E = [0, 0, 0], N = [0, 0, 0], E2 = [0, 0, 0], N2 = [0, 0, 0];
  for (let c = 0; c < g.n; c++) {
    g.basis(c, E, N);
    DM.V3.basis(g.dir(c, d), E2, N2);
    for (let i = 0; i < 3; i++) {
      if (Math.abs(E[i] - E2[i]) > 1e-6 || Math.abs(N[i] - N2[i]) > 1e-6) T.ok(false, 'geo.basis and V3.basis differ at cell ' + c);
    }
  }
});

// ---- cellAt ---------------------------------------------------------------------------------------

T.test('geo: cellAt maps every cell direction (at any scale) back to that cell', () => {
  for (const L of [0, 1, 2, 3, 4, 5, 6, 7]) {
    const g = G(L), p = g.pos;
    const stride = L === 7 ? 3 : 1;
    for (let c = 0; c < g.n; c += stride) {
      const x = p[3 * c], y = p[3 * c + 1], z = p[3 * c + 2];
      if (g.cellAt(x, y, z) !== c) T.ok(false, 'L=' + L + ' cell ' + c + ' (no hint)');
      if (g.cellAt(x * 103.7, y * 103.7, z * 103.7, c) !== c) T.ok(false, 'L=' + L + ' cell ' + c + ' (scaled, own hint)');
      if (g.cellAt(x * 1e-3, y * 1e-3, z * 1e-3, g.nbr[6 * c]) !== c) T.ok(false, 'L=' + L + ' cell ' + c + ' (tiny, neighbour hint)');
    }
  }
});

T.test('geo: cellAt is exact on random directions, with and without hints (vs brute force)', () => {
  const d = [0, 0, 0];
  const counts = [400, 600, 1000, 1500, 2000, 2000, 4000, 300];
  for (const L of ALL) {
    const g = G(L), r = rng(1000 + L);
    for (let i = 0; i < counts[L]; i++) {
      randDir(r, d);
      const want = brute(g, d[0], d[1], d[2]);
      const got = g.cellAt(d[0], d[1], d[2]);
      if (got !== want) T.ok(false, 'L=' + L + ' no hint: got ' + got + ', nearest is ' + want + ' for ' + d);
      let hint;
      switch (i % 5) {
        case 0: hint = want; break;                                   // perfect hint
        case 1: hint = g.nbr[6 * want + (i % g.nbrN[want])]; break;   // one cell off
        case 2: hint = (r() * g.n) | 0; break;                        // anywhere on the planet
        case 3: hint = g.nbr[6 * g.nbr[6 * want] + 2]; break;         // one or two cells off
        default: hint = g.cellAt(-d[0], -d[1], -d[2]);                // the antipode
      }
      const got2 = g.cellAt(d[0], d[1], d[2], hint);
      if (got2 !== want) T.ok(false, 'L=' + L + ' hint ' + hint + ': got ' + got2 + ', nearest is ' + want);
    }
  }
});

T.test('geo: cellAt at the poles, on the axes and on the cube-face seams of the lookup', () => {
  const s = [-1, 0, 1], special = [];
  for (const x of s) for (const y of s) for (const z of s) if (x || y || z) special.push([x, y, z]);
  special.push([0, 5, 0], [0, -0.001, 0], [1e-9, 1, 0], [0, -1, 1e-9], [1, 1, 1e-12], [3, 3, 3], [-2, 2, 2],
    [1, 0.999999, 0], [0.5, 0.5, -0.5000001], [1e-300, 0, 1e-300], [1e150, -1e150, 1e150]);
  for (const L of [0, 1, 2, 4, 6, 7]) {
    const g = G(L);
    T.eq(g.cellAt(0, 1, 0), 0, 'north pole');
    T.eq(g.cellAt(0, -1, 0), 11, 'south pole');
    T.eq(g.cellAt(0, 250, 0, 11), 0, 'north pole with the south pole as hint');
    for (const v of special) {
      const got = g.cellAt(v[0], v[1], v[2]);
      brute(g, v[0], v[1], v[2]);
      // ties are possible here: accept any cell that is exactly as near as the brute-force winner
      if (dotQ(g, v[0], v[1], v[2], got) !== BR.dot) T.ok(false, 'L=' + L + ' direction ' + v + ': got ' + got + ', nearest is ' + BR.id);
    }
  }
});

T.test('geo: cellAt on directions exactly between cells (edge midpoints, triangle centres)', () => {
  for (const L of [0, 1, 2, 3, 6, 7]) {
    const g = G(L), p = g.pos;
    const stride = L <= 3 ? 1 : L === 6 ? 211 : 2111;
    for (let t = 0; t < g.nTri; t += stride) {
      const a = g.tris[3 * t], b = g.tris[3 * t + 1], c = g.tris[3 * t + 2];
      // midpoint of edge a-b (sums of float32 values are exact in double precision)
      let x = p[3 * a] + p[3 * b], y = p[3 * a + 1] + p[3 * b + 1], z = p[3 * a + 2] + p[3 * b + 2];
      let got = g.cellAt(x, y, z);
      brute(g, x, y, z);
      if (got !== a && got !== b) T.ok(false, 'L=' + L + ' edge midpoint ' + a + '-' + b + ' gave ' + got);
      if (dotQ(g, x, y, z, got) !== BR.dot) T.ok(false, 'L=' + L + ' edge midpoint ' + a + '-' + b + ': ' + got + ' is not the nearest (' + BR.id + ')');
      got = g.cellAt(x, y, z, c);                 // again, starting from the far corner
      if (dotQ(g, x, y, z, got) !== BR.dot) T.ok(false, 'L=' + L + ' edge midpoint with hint: ' + got + ' vs ' + BR.id);
      // triangle centre
      x += p[3 * c]; y += p[3 * c + 1]; z += p[3 * c + 2];
      got = g.cellAt(x, y, z);
      brute(g, x, y, z);
      if (got !== a && got !== b && got !== c) T.ok(false, 'L=' + L + ' triangle centre ' + t + ' gave ' + got);
      if (dotQ(g, x, y, z, got) !== BR.dot) T.ok(false, 'L=' + L + ' triangle centre ' + t + ': ' + got + ' is not the nearest (' + BR.id + ')');
      // slightly off the midpoint toward a, then toward b: must resolve to that end. (The offset has
      // to outweigh the ~1e-7 length error of the float32 positions, which decides exact midpoints.)
      const k = 0.05;
      got = g.cellAt(p[3 * a] * (1 + k) + p[3 * b], p[3 * a + 1] * (1 + k) + p[3 * b + 1], p[3 * a + 2] * (1 + k) + p[3 * b + 2]);
      if (got !== a) T.ok(false, 'L=' + L + ' just on the ' + a + ' side of edge ' + a + '-' + b + ' gave ' + got);
      got = g.cellAt(p[3 * a] + p[3 * b] * (1 + k), p[3 * a + 1] + p[3 * b + 1] * (1 + k), p[3 * a + 2] + p[3 * b + 2] * (1 + k));
      if (got !== b) T.ok(false, 'L=' + L + ' just on the ' + b + ' side of edge ' + a + '-' + b + ' gave ' + got);
    }
  }
});

T.test('geo: cellAt ignores invalid hints and survives degenerate input', () => {
  const g = G(5), r = rng(5), d = [0, 0, 0];
  for (let i = 0; i < 200; i++) {
    randDir(r, d);
    const want = brute(g, d[0], d[1], d[2]);
    for (const hint of [-1, -7, g.n, g.n + 5, 1e12, -1e12, NaN, Infinity, -Infinity, undefined, null, 2.7, want + 0.5]) {
      if (g.cellAt(d[0], d[1], d[2], hint) !== want) T.ok(false, 'hint ' + hint + ' changed the answer');
    }
  }
  const valid = c => Number.isInteger(c) && c >= 0 && c < g.n;
  for (const v of [[0, 0, 0], [NaN, 0, 1], [NaN, NaN, NaN], [Infinity, 0, 0], [-Infinity, Infinity, 1], [0, -0, 0]]) {
    T.ok(valid(g.cellAt(v[0], v[1], v[2])), 'cellAt(' + v + ') must still return a cell id');
    T.ok(valid(g.cellAt(v[0], v[1], v[2], 17)), 'cellAt(' + v + ', hint) must still return a cell id');
  }
});

// ---- within / ring --------------------------------------------------------------------------------

function bruteWithin(g, c, ang, angleFn) {
  const res = [];
  for (let i = 0; i < g.n; i++) if (angleFn(g, c, i) <= ang) res.push(i);
  return res;
}

T.test('geo: within equals the brute-force set for radii from 0 to the whole sphere', () => {
  const cases = [
    [2, [0, 5, 11, 12, 100, 161]],
    [4, [0, 7, 11, 12, 1300, 2561]],
    [6, [0, 3, 11, 12, 41, 1000, 20481, 40961]],
    [7, [0, 11, 99999, 163841]],
  ];
  for (const [L, centres] of cases) {
    const g = G(L), out = new Int32Array(g.n);
    const radii = [0, 1e-9, 0.5 * g.step, g.step, 1.3 * g.step, 2.5 * g.step, 0.1, 0.3, 1.2, PI / 2, 2.0, 3.0, 3.14, PI, 4, Infinity];
    for (const c of centres) {
      for (const ang of (L === 7 ? [0, g.step, 4.2 * g.step, 0.2, 1.7, PI] : radii)) {
        const count = g.within(c, ang, out);
        const want = bruteWithin(g, c, ang, angBetween);
        if (count !== want.length) T.ok(false, 'L=' + L + ' c=' + c + ' ang=' + ang + ': count ' + count + ', brute force ' + want.length);
        if (out[0] !== c) T.ok(false, 'c must come first');
        sameList(sortedCopy(out, count), want, 'L=' + L + ' c=' + c + ' ang=' + ang);
      }
      T.eq(g.within(c, 0, out), 1, 'radius 0 is the cell alone');
      T.eq(g.within(c, PI, out), g.n, 'radius pi is the whole sphere');
      T.eq(g.within(c, 0.5 * g.step, out), 1, 'half a step reaches no neighbour');
    }
  }
});

T.test('geo: within treats its radius as inclusive, in exact agreement with geo.angle', () => {
  const g = G(6), out = [];
  const byGeo = (gg, a, b) => gg.angle(a, b);
  for (const c of [0, 12, 4242, 30000]) {
    for (const far of [g.nbr[6 * c], g.nbr[6 * c + 3], g.nbr[6 * g.nbr[6 * c + 1] + 2], (c + 5000) % g.n, (c + 20011) % g.n]) {
      if (far === c) continue;
      const ang = g.angle(c, far);
      let count = g.within(c, ang, out);
      let list = sortedCopy(out, count);
      T.ok(list.indexOf(far) >= 0, 'cell at exactly the radius must be included');
      sameList(list, bruteWithin(g, c, ang, byGeo), 'inclusive radius, c=' + c + ' far=' + far);
      const less = ang - ang * 2.3e-16;           // the next smaller double
      T.ok(less < ang);
      count = g.within(c, less, out);
      list = sortedCopy(out, count);
      T.ok(list.indexOf(far) < 0, 'cell just outside the radius must be excluded');
      sameList(list, bruteWithin(g, c, less, byGeo), 'just under the radius, c=' + c + ' far=' + far);
    }
  }
});

T.test('geo: within output containers, defaults, undersized buffers and bad input', () => {
  const g = G(5);
  const arr = [], typed = new Int32Array(g.n);
  const n1 = g.within(500, 0.2, arr), n2 = g.within(500, 0.2, typed);
  T.eq(n1, n2);
  T.eq(g.within(500, 0.2), n1, 'out is optional');
  T.ok(n1 > 50 && arr.length === n1);
  for (let i = 0; i < n1; i++) if (arr[i] !== typed[i]) T.ok(false, 'Array and Int32Array results differ');
  // no duplicates
  T.eq(new Set(arr).size, n1, 'no duplicates');
  // a typed array that is too small: the true count is still returned, and what fits is a correct prefix
  const small = new Int32Array(7).fill(-5);
  T.eq(g.within(500, 0.2, small), n1, 'count is the true size even when out is too small');
  for (let i = 0; i < 7; i++) T.eq(small[i], arr[i], 'prefix of the result');
  // the call after an overflow is unaffected
  T.eq(g.within(500, 0.2, typed), n1);
  // degenerate radii still report the centre
  for (const ang of [-1, -0, NaN, -Infinity]) {
    const o = [];
    T.eq(g.within(77, ang, o), 1, 'ang=' + ang);
    T.eq(o[0], 77);
  }
  // not a cell: nothing
  for (const c of [-1, g.n, NaN, undefined]) T.eq(g.within(c, 0.3, arr), 0, 'c=' + c);
  for (const c of [-1, g.n, NaN, undefined]) T.eq(g.ring(c, 2, arr), 0, 'ring c=' + c);
});

T.test('geo: within and ring are re-entrant (nested calls while iterating a result)', () => {
  const g = G(5);
  const A = new Int32Array(g.n), B = new Int32Array(g.n), C = [];
  const nA = g.within(4000, 0.25, A);
  const keep = A.slice(0, nA);
  T.ok(nA > 100);
  for (let i = 0; i < nA; i++) {
    const c = A[i];
    const nB = g.within(c, 0.09, B);
    if (B[0] !== c) T.ok(false, 'inner result must start with its own centre');
    for (let k = 0; k < nB; k++) if (!(g.angle(c, B[k]) <= 0.09)) T.ok(false, 'inner result has a cell outside the radius');
    if (i % 37 === 0) sameList(sortedCopy(B, nB), bruteWithin(g, c, 0.09, angBetween), 'inner within at ' + c);
    const nC = g.ring(c, 2, C);
    if (C[0] !== c || nC < 16 || nC > 19) T.ok(false, 'inner ring broken');
    // a third level of nesting
    for (let k = 0; k < nC; k += 5) g.within(C[k], 0.05);
  }
  for (let i = 0; i < nA; i++) if (A[i] !== keep[i]) T.ok(false, 'outer result was disturbed by nested calls');
  // and the outer query still gives the same answer afterwards
  T.eq(g.within(4000, 0.25, B), nA);
  for (let i = 0; i < nA; i++) if (B[i] !== keep[i]) T.ok(false, 'within is not repeatable');
});

T.test('geo: ring is a breadth-first search by hops, c first', () => {
  for (const L of [0, 2, 4, 6]) {
    const g = G(L), out = new Int32Array(g.n);
    const centres = [0, 11, 5, 12, (g.n / 2) | 0, g.n - 1].filter(c => c < g.n);
    for (const c of centres) {
      const dist = hopDist(g, c);
      for (const steps of [0, 1, 2, 3, 7, 20]) {
        const count = g.ring(c, steps, out);
        let want = 0;
        for (let i = 0; i < g.n; i++) if (dist[i] <= steps) want++;
        if (count !== want) T.ok(false, 'L=' + L + ' c=' + c + ' steps=' + steps + ': count ' + count + ', expected ' + want);
        if (out[0] !== c) T.ok(false, 'c must come first');
        const seen = new Set();
        for (let i = 0; i < count; i++) {
          if (dist[out[i]] > steps) T.ok(false, 'cell beyond the hop limit');
          if (i > 0 && dist[out[i]] < dist[out[i - 1]]) T.ok(false, 'not in breadth-first order');
          seen.add(out[i]);
        }
        if (seen.size !== count) T.ok(false, 'duplicate cells in ring');
      }
      T.eq(g.ring(c, 1, out), 1 + g.nbrN[c], 'one hop = the cell and its neighbours');
      for (let j = 0; j < g.nbrN[c]; j++) T.eq(out[1 + j], g.nbr[6 * c + j], 'first hop comes in neighbour order');
      T.eq(g.ring(c, 1e9, out), g.n, 'enough hops reach the whole sphere');
      T.eq(g.ring(c, -3, out), 1, 'negative steps');
    }
  }
  // closed forms: around a pentagon 1 + 5k(k+1)/2, on the regular hexagonal lattice 1 + 3k(k+1)
  const g = G(6), arr = [];
  const hex = g.nbr[6 * g.nbr[6 * g.nbr[6 * g.nbr[6 * 12 + 1] + 1] + 1] + 1];
  for (let k = 0; k <= 6; k++) T.eq(g.ring(0, k, arr), 1 + 5 * k * (k + 1) / 2, 'pentagon ring ' + k);
  const dPent = hopDist(g, hex);
  let nearestPent = Infinity;
  for (let i = 0; i < 12; i++) nearestPent = Math.min(nearestPent, dPent[i]);
  const kMax = Math.min(6, nearestPent - 1);
  T.ok(kMax >= 1, 'test cell should be at least two hops from a pentagon');
  for (let k = 0; k <= kMax; k++) T.eq(g.ring(hex, k, arr), 1 + 3 * k * (k + 1), 'hexagon ring ' + k);
  T.eq(g.ring(hex, 2), 19, 'out is optional');
});

// ---- performance ------------------------------------------------------------------------------------

T.test('geo: performance targets at L = 6 (build < 400 ms, cellAt < 10 us, < 2 us with a hint)', () => {
  let buildMs = Infinity;
  for (let i = 0; i < 3; i++) { const t = now(); Geo.build(6); buildMs = Math.min(buildMs, now() - t); }
  const g = G(6), N = 100000, r = rng(99), d = [0, 0, 0];
  const dirs = new Float64Array(3 * N), ans = new Int32Array(N);
  for (let i = 0; i < N; i++) { randDir(r, d); dirs[3 * i] = d[0]; dirs[3 * i + 1] = d[1]; dirs[3 * i + 2] = d[2]; }
  let cold = Infinity, hinted = Infinity, sink = 0;
  for (let rep = 0; rep < 3; rep++) {
    let t = now();
    for (let i = 0; i < N; i++) ans[i] = g.cellAt(dirs[3 * i], dirs[3 * i + 1], dirs[3 * i + 2]);
    cold = Math.min(cold, (now() - t) / N * 1000);
    t = now();
    for (let i = 0; i < N; i++) sink += g.cellAt(dirs[3 * i], dirs[3 * i + 1], dirs[3 * i + 2], g.nbr[6 * ans[i] + i % 5]);
    hinted = Math.min(hinted, (now() - t) / N * 1000);
  }
  let b7 = now(); Geo.build(7); b7 = now() - b7;
  say('L=6 build ' + buildMs.toFixed(1) + ' ms (L=7: ' + b7.toFixed(1) + ' ms); cellAt ' + cold.toFixed(3) +
    ' us without hint, ' + hinted.toFixed(3) + ' us with a neighbouring hint');
  T.ok(sink > 0);
  T.ok(buildMs < 400, 'build(6) took ' + buildMs.toFixed(1) + ' ms');
  T.ok(cold < 10, 'cellAt without hint: ' + cold.toFixed(3) + ' us');
  T.ok(hinted < 2, 'cellAt with hint: ' + hinted.toFixed(3) + ' us');
});

T.test('geo: within and ring cost follows the result size, not the planet size', () => {
  // 30,000 small queries on the 163,842-cell grid: a full scan per call would take many seconds.
  const g = G(7), out = new Int32Array(64);
  const t0 = now();
  let total = 0;
  for (let i = 0; i < 30000; i++) total += g.within((i * 5471) % g.n, 2.1 * g.step, out);
  for (let i = 0; i < 30000; i++) total += g.ring((i * 5471) % g.n, 2, out);
  const ms = now() - t0;
  say('60,000 small within/ring calls at L=7: ' + ms.toFixed(1) + ' ms, ' + (total / 60000).toFixed(1) + ' cells each');
  T.ok(total > 60000 * 15 && total < 60000 * 22, 'about 19 cells per query, got ' + total / 60000);
  T.ok(ms < 1500, '60,000 small queries took ' + ms.toFixed(0) + ' ms');
  // per-result cost stays flat as the radius grows
  const g6 = G(6), big = new Int32Array(g6.n);
  const per = [];
  for (const ang of [0.1, 0.4, 1.6]) {
    let best = Infinity, cnt = 0;
    for (let rep = 0; rep < 5; rep++) {
      const t = now();
      cnt = 0;
      for (let i = 0; i < 20; i++) cnt += g6.within((i * 2003) % g6.n, ang, big);
      best = Math.min(best, (now() - t) * 1e6 / cnt);
    }
    per.push(best);
  }
  say('within cost per result cell at radius 0.1 / 0.4 / 1.6 rad: ' + per.map(x => x.toFixed(1)).join(' / ') + ' ns');
  T.ok(per[2] < 8 * per[0] + 50, 'cost per result grew with radius: ' + per.join(', ') + ' ns');
});
