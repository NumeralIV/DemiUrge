// DEMIURGE — tests/geo.adv.test.js : adversarial, independent verification of DM.Geo against
// FOUNDATION.md §4. Nothing here trusts geo.js's own helpers: adjacency is rebuilt from the triangle
// list, angles are recomputed with a different formula, and searches are checked by brute force.
//   node tools/test.js geo.adv        (T_VERBOSE=1 also prints the measured timings and margins)
'use strict';
const T = require('../tools/t.js');
const DM = require('../tools/load.js')();
const Geo = DM.Geo;

const PI = Math.PI;
const LEVELS = [0, 1, 2, 3, 4, 5, 6, 7];
const nOf = L => 10 * Math.pow(4, L) + 2;
const G = L => Geo.get(L);
const nowMs = () => Number(process.hrtime.bigint()) / 1e6;
const say = msg => { if (process.env.T_VERBOSE) console.log('       ' + msg); };
const fail = msg => T.ok(false, msg);

// Deterministic test-local random numbers (integer arithmetic only).
function rng(seed) {
  let a = seed >>> 0;
  return function () {
    a = (a + 0x9E3779B9) >>> 0;
    let t = a ^ (a >>> 16);
    t = Math.imul(t, 0x21F0AAAD);
    t ^= t >>> 15;
    t = Math.imul(t, 0x735A2D97);
    t ^= t >>> 15;
    return (t >>> 0) / 4294967296;
  };
}
// Uniformly distributed direction, deliberately NOT normalized (length 0.1 … 1).
function randDir(r, o) {
  let x, y, z, d;
  do { x = r() * 2 - 1; y = r() * 2 - 1; z = r() * 2 - 1; d = x * x + y * y + z * z; } while (d > 1 || d < 0.01);
  o[0] = x; o[1] = y; o[2] = z;
  return o;
}
function dotQ(g, x, y, z, c) { return x * g.pos[3 * c] + y * g.pos[3 * c + 1] + z * g.pos[3 * c + 2]; }

// Brute-force nearest cell: the largest dot product over every cell.
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

// Directed-edge table rebuilt from geo.tris alone: for vertex a, the edges a→to[6a+k] with the third
// corner of that triangle, k < cnt[a]. Throws if the triangle list is not a consistent surface.
const edgeCache = [];
function edges(L) {
  if (edgeCache[L]) return edgeCache[L];
  const g = G(L), n = g.n;
  const cnt = new Uint8Array(n), to = new Int32Array(6 * n).fill(-1), third = new Int32Array(6 * n).fill(-1);
  for (let t = 0; t < g.nTri; t++) {
    for (let e = 0; e < 3; e++) {
      const a = g.tris[3 * t + e], b = g.tris[3 * t + (e + 1) % 3], c = g.tris[3 * t + (e + 2) % 3];
      if (!(a < n && b < n && c < n)) fail('L=' + L + ' triangle ' + t + ' has a vertex id out of range');
      if (a === b || a === c) fail('L=' + L + ' triangle ' + t + ' is degenerate');
      const k = cnt[a];
      if (k >= 6) fail('L=' + L + ' vertex ' + a + ' has more than 6 outgoing triangle edges');
      for (let i = 0; i < k; i++) if (to[6 * a + i] === b) fail('L=' + L + ' directed edge ' + a + '->' + b + ' is in two triangles');
      to[6 * a + k] = b; third[6 * a + k] = c; cnt[a] = k + 1;
    }
  }
  const find = (a, b) => { for (let k = 0; k < cnt[a]; k++) if (to[6 * a + k] === b) return k; return -1; };
  return (edgeCache[L] = { cnt, to, third, find });
}

// Float64 unit vectors recomputed from the stored float32 directions.
const unitCache = [];
function units(L) {
  if (unitCache[L]) return unitCache[L];
  const g = G(L), u = new Float64Array(3 * g.n);
  for (let i = 0; i < g.n; i++) {
    const x = g.pos[3 * i], y = g.pos[3 * i + 1], z = g.pos[3 * i + 2];
    const len = Math.sqrt(x * x + y * y + z * z);
    u[3 * i] = x / len; u[3 * i + 1] = y / len; u[3 * i + 2] = z / len;
  }
  return (unitCache[L] = u);
}
// Independent great-circle angle (chord form 2·atan2(|a−b|, |a+b|); geo.js uses atan2(|a×b|, a·b)).
function indAngle(u, a, b) {
  const ax = u[3 * a], ay = u[3 * a + 1], az = u[3 * a + 2], bx = u[3 * b], by = u[3 * b + 1], bz = u[3 * b + 2];
  const mx = ax - bx, my = ay - by, mz = az - bz, px = ax + bx, py = ay + by, pz = az + bz;
  return 2 * Math.atan2(Math.sqrt(mx * mx + my * my + mz * mz), Math.sqrt(px * px + py * py + pz * pz));
}
// Float32 positions are within ~1e-7 rad of the ideal ones; cells this close to a radius may fall
// on either side when judged by an independent formula.
const ANG_TOL = 5e-7;

// Hop distances from c over the adjacency rebuilt from the triangles (not geo.nbr).
function hopDist(L, c) {
  const g = G(L), E = edges(L), dist = new Int32Array(g.n).fill(-1), q = new Int32Array(g.n);
  let head = 0, tail = 0;
  q[tail++] = c; dist[c] = 0;
  while (head < tail) {
    const a = q[head++];
    for (let k = 0; k < E.cnt[a]; k++) {
      const m = E.to[6 * a + k];
      if (dist[m] < 0) { dist[m] = dist[a] + 1; q[tail++] = m; }
    }
  }
  if (tail !== g.n) fail('L=' + L + ' the triangle mesh is not connected');
  return dist;
}

// ===================================================================================================
// construction
// ===================================================================================================

T.test('adv: sizes, types, counts for L = 0..7; get() caches; build(7) completes', () => {
  T.ok(Geo && typeof Geo.build === 'function' && typeof Geo.get === 'function', 'DM.Geo.build / DM.Geo.get');
  for (const L of LEVELS) {
    const g = Geo.get(L), n = nOf(L), nTri = 20 * Math.pow(4, L);
    T.ok(Geo.get(L) === g, 'get(' + L + ') must return the same instance every time');
    T.eq(g.L, L, 'geo.L');
    T.eq(g.n, n, 'geo.n at L=' + L);
    T.eq(g.nTri, nTri, 'geo.nTri at L=' + L);
    T.ok(g.pos instanceof Float32Array && g.pos.length === 3 * n, 'pos Float32Array(3n) at L=' + L);
    T.ok(g.lat instanceof Float32Array && g.lat.length === n, 'lat Float32Array(n) at L=' + L);
    T.ok(g.lon instanceof Float32Array && g.lon.length === n, 'lon Float32Array(n) at L=' + L);
    T.ok(g.nbr instanceof Int32Array && g.nbr.length === 6 * n, 'nbr Int32Array(6n) at L=' + L);
    T.ok(g.nbrN instanceof Uint8Array && g.nbrN.length === n, 'nbrN Uint8Array(n) at L=' + L);
    T.ok(g.tris instanceof Uint32Array && g.tris.length === 3 * nTri, 'tris Uint32Array(3 nTri) at L=' + L);
    T.ok(typeof g.step === 'number' && isFinite(g.step) && g.step > 0, 'step');
    for (const f of ['prefix', 'cellAt', 'angle', 'within', 'ring', 'dir', 'basis']) T.eq(typeof g[f], 'function', 'geo.' + f);
    for (let k = 0; k <= 7; k++) T.eq(g.prefix(k), nOf(k), 'prefix(' + k + ')');
  }
  T.eq(G(6).n, 40962, 'L=6 cell count');
  // a fresh level-7 build: completes, has the right sizes, is a different object but equal data
  const t0 = nowMs();
  const b = Geo.build(7), c = Geo.get(7);
  say('Geo.build(7): ' + (nowMs() - t0).toFixed(1) + ' ms');
  T.ok(b !== c && b.pos !== c.pos, 'build() must return a fresh instance');
  T.eq([b.n, b.nTri, b.pos.length, b.lat.length, b.lon.length, b.nbr.length, b.nbrN.length, b.tris.length],
    [163842, 327680, 491526, 163842, 163842, 983052, 163842, 983040], 'level-7 sizes');
  for (const f of ['pos', 'lat', 'lon', 'nbr', 'nbrN', 'tris']) {
    for (let i = 0; i < b[f].length; i++) if (b[f][i] !== c[f][i]) fail('build(7).' + f + ' differs from get(7).' + f + ' at ' + i);
  }
  T.eq(b.step, c.step, 'step is reproducible');
  // the cache is not disturbed by build()
  T.ok(Geo.get(7) === c, 'get(7) still returns the cached instance after build(7)');
});

T.test('adv: the 12 level-0 vertices sit where the contract puts them, at every level', () => {
  const ringLat = Math.atan(0.5), rad = PI / 180;
  const want = [[0, 1, 0]];
  for (let k = 0; k < 5; k++) want.push([Math.cos(ringLat) * Math.cos(72 * k * rad), Math.sin(ringLat), Math.cos(ringLat) * Math.sin(72 * k * rad)]);
  for (let k = 0; k < 5; k++) want.push([Math.cos(ringLat) * Math.cos((36 + 72 * k) * rad), -Math.sin(ringLat), Math.cos(ringLat) * Math.sin((36 + 72 * k) * rad)]);
  want.push([0, -1, 0]);
  for (const L of LEVELS) {
    const g = G(L);
    for (let v = 0; v < 12; v++) {
      for (let k = 0; k < 3; k++) {
        if (!(Math.abs(g.pos[3 * v + k] - want[v][k]) <= 1e-7)) fail('L=' + L + ' vertex ' + v + ' component ' + k + ': ' + g.pos[3 * v + k] + ', expected ' + want[v][k]);
      }
      const wantLat = v === 0 ? PI / 2 : v === 11 ? -PI / 2 : v <= 5 ? ringLat : -ringLat;
      T.near(g.lat[v], wantLat, 3e-7, 'L=' + L + ' lat of vertex ' + v);
      if (v >= 1 && v <= 10) {
        const deg = v <= 5 ? 72 * (v - 1) : 36 + 72 * (v - 6);
        let d = g.lon[v] - deg * rad;
        d -= 2 * PI * Math.round(d / (2 * PI));
        T.near(d, 0, 4e-7, 'L=' + L + ' lon of vertex ' + v + ' (expected ' + deg + ' degrees)');
      }
    }
    // the poles are exact
    T.ok(g.pos[0] === 0 && g.pos[1] === 1 && g.pos[2] === 0, 'vertex 0 is exactly (0,1,0)');
    T.ok(g.pos[33] === 0 && g.pos[34] === -1 && g.pos[35] === 0, 'vertex 11 is exactly (0,-1,0)');
    // nothing else is as far north / south as the poles
    for (let i = 12; i < g.n; i++) if (Math.abs(g.pos[3 * i + 1]) >= 1) fail('L=' + L + ' cell ' + i + ' is as high as a pole');
  }
});

T.test('adv: positions are unit vectors; lat = asin(y), lon = atan2(z, x), both usable without NaN', () => {
  for (const L of LEVELS) {
    const g = G(L);
    let worstLen = 0;
    for (let i = 0; i < g.n; i++) {
      const x = g.pos[3 * i], y = g.pos[3 * i + 1], z = g.pos[3 * i + 2];
      const e = Math.abs(Math.sqrt(x * x + y * y + z * z) - 1);
      if (e > worstLen) worstLen = e;
      const la = g.lat[i], lo = g.lon[i], h = Math.sqrt(x * x + z * z);
      if (!(Math.abs(la - Math.atan2(y, h)) <= 4e-7)) fail('L=' + L + ' lat of cell ' + i + ': ' + la + ' vs ' + Math.atan2(y, h));
      // asin(y) of a float32 y is ill-conditioned next to the poles, so scale the tolerance by 1/cos
      const tol = 5e-7 + 1.5e-7 / Math.max(h, 1e-3);
      if (!(Math.abs(la - Math.asin(Math.max(-1, Math.min(1, y)))) <= tol)) fail('L=' + L + ' lat of cell ' + i + ' vs asin(y)');
      let d = lo - Math.atan2(z, x);
      d -= 2 * PI * Math.round(d / (2 * PI));
      if (!(Math.abs(d) <= 4e-7)) fail('L=' + L + ' lon of cell ' + i + ': ' + lo + ' vs ' + Math.atan2(z, x));
      // ranges that downstream code relies on: cos(lat) >= 0 and lon inside (-pi, pi]
      if (!(Math.cos(la) >= 0) || !(la >= -PI / 2 && la <= PI / 2)) fail('L=' + L + ' lat of cell ' + i + ' is outside [-pi/2, pi/2]: ' + la);
      if (!(lo > -PI && lo <= PI)) fail('L=' + L + ' lon of cell ' + i + ' is outside (-pi, pi]: ' + lo);
    }
    if (!(worstLen < 1.5e-7)) fail('L=' + L + ' worst |pos| error ' + worstLen);
  }
});

T.test('adv: triangles are a closed, outward-CCW 2-manifold with Euler characteristic 2', () => {
  for (const L of LEVELS) {
    const g = G(L), p = g.pos, n = g.n, E = edges(L);          // edges() rejects duplicate directed edges
    let directed = 0, minArea = Infinity;
    for (let v = 0; v < n; v++) {
      directed += E.cnt[v];
      if (E.cnt[v] < 5) fail('L=' + L + ' vertex ' + v + ' is used by only ' + E.cnt[v] + ' triangles');
      // every directed edge has its reverse (closed surface: each undirected edge in exactly two triangles)
      for (let k = 0; k < E.cnt[v]; k++) if (E.find(E.to[6 * v + k], v) < 0) fail('L=' + L + ' edge ' + v + '->' + E.to[6 * v + k] + ' has no reverse');
    }
    T.eq(directed, 3 * g.nTri, 'directed edges at L=' + L);
    const nEdges = directed / 2;
    T.eq(nEdges, 30 * Math.pow(4, L), 'undirected edges at L=' + L);
    T.eq(n - nEdges + g.nTri, 2, 'V - E + F at L=' + L);
    // winding: (b-a) x (c-a) points away from the centre, with a healthy margin
    for (let t = 0; t < g.nTri; t++) {
      const a = 3 * g.tris[3 * t], b = 3 * g.tris[3 * t + 1], c = 3 * g.tris[3 * t + 2];
      const ux = p[b] - p[a], uy = p[b + 1] - p[a + 1], uz = p[b + 2] - p[a + 2];
      const vx = p[c] - p[a], vy = p[c + 1] - p[a + 1], vz = p[c + 2] - p[a + 2];
      const nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
      const out = (nx * (p[a] + p[b] + p[c]) + ny * (p[a + 1] + p[b + 1] + p[c + 1]) + nz * (p[a + 2] + p[b + 2] + p[c + 2])) / 3;
      const rel = out / (g.step * g.step);
      if (rel < minArea) minArea = rel;
      if (!(rel > 0.4)) fail('L=' + L + ' triangle ' + t + ' is not counter-clockwise seen from outside (signed area ' + rel + ' step^2)');
    }
    // total area of the flat triangles approaches the sphere's 4*pi from below
    if (L >= 3) {
      let area = 0;
      for (let t = 0; t < g.nTri; t++) {
        const a = 3 * g.tris[3 * t], b = 3 * g.tris[3 * t + 1], c = 3 * g.tris[3 * t + 2];
        const ux = p[b] - p[a], uy = p[b + 1] - p[a + 1], uz = p[b + 2] - p[a + 2];
        const vx = p[c] - p[a], vy = p[c + 1] - p[a + 1], vz = p[c + 2] - p[a + 2];
        const nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
        area += 0.5 * Math.sqrt(nx * nx + ny * ny + nz * nz);
      }
      T.ok(area < 4 * PI && area > 4 * PI * (1 - 2 * g.step * g.step), 'L=' + L + ' mesh area ' + area + ' covers the sphere exactly once');
    }
  }
});

T.test('adv: neighbour lists: 5 for ids 0-11 (with -1 padding), 6 otherwise, symmetric, equal to the triangle fan', () => {
  for (const L of LEVELS) {
    const g = G(L), E = edges(L);
    for (let c = 0; c < g.n; c++) {
      const k = g.nbrN[c];
      if (k !== (c < 12 ? 5 : 6)) fail('L=' + L + ' cell ' + c + ' has nbrN ' + k);
      if (c < 12 && g.nbr[6 * c + 5] !== -1) fail('L=' + L + ' pentagon ' + c + ': 6th slot is ' + g.nbr[6 * c + 5] + ', expected -1');
      if (E.cnt[c] !== k) fail('L=' + L + ' cell ' + c + ': nbrN ' + k + ' but ' + E.cnt[c] + ' triangles meet there');
      for (let j = 0; j < k; j++) {
        const m = g.nbr[6 * c + j];
        if (!(m >= 0 && m < g.n) || m === c) fail('L=' + L + ' cell ' + c + ' has bad neighbour ' + m);
        for (let i = 0; i < j; i++) if (g.nbr[6 * c + i] === m) fail('L=' + L + ' cell ' + c + ' lists neighbour ' + m + ' twice');
        if (E.find(c, m) < 0) fail('L=' + L + ' neighbour ' + m + ' of cell ' + c + ' shares no triangle edge with it');
        let back = false;
        for (let i = 0; i < g.nbrN[m]; i++) if (g.nbr[6 * m + i] === c) back = true;
        if (!back) fail('L=' + L + ' adjacency is not symmetric: ' + m + ' is a neighbour of ' + c + ' but not the reverse');
      }
    }
  }
});

T.test('adv: neighbours turn counter-clockwise about the outward direction, one full turn, consecutive ones adjacent', () => {
  for (const L of LEVELS) {
    const g = G(L), p = g.pos, E = edges(L), u = units(L);
    let minDet = Infinity;
    for (let c = 0; c < g.n; c++) {
      const k = g.nbrN[c];
      // the outward direction, renormalized in float64 so that the tangent-plane projection is exact
      const nx = u[3 * c], ny = u[3 * c + 1], nz = u[3 * c + 2];
      let turn = 0;
      for (let j = 0; j < k; j++) {
        const a = g.nbr[6 * c + j], b = g.nbr[6 * c + (j + 1) % k];
        const ax = p[3 * a], ay = p[3 * a + 1], az = p[3 * a + 2];
        const bx = p[3 * b], by = p[3 * b + 1], bz = p[3 * b + 2];
        // n . (a x b) > 0  <=>  going from a to b is a counter-clockwise turn about n, seen from outside
        const det = nx * (ay * bz - az * by) + ny * (az * bx - ax * bz) + nz * (ax * by - ay * bx);
        const rel = det / (g.step * g.step);
        if (rel < minDet) minDet = rel;
        if (!(rel > 0.3)) fail('L=' + L + ' cell ' + c + ': neighbours ' + a + ' -> ' + b + ' do not turn counter-clockwise (det ' + rel + ' step^2)');
        // signed angle between the two neighbours, measured in the tangent plane at c
        const an = ax * nx + ay * ny + az * nz, bn = bx * nx + by * ny + bz * nz;
        const th = Math.atan2(det, ax * bx + ay * by + az * bz - an * bn);
        if (!(th > 0 && th < PI)) fail('L=' + L + ' cell ' + c + ': turn of ' + th + ' rad between consecutive neighbours');
        turn += th;
        // consecutive neighbours are adjacent to each other, and (c, a, b) is a triangle of geo.tris
        const s = E.find(c, a);
        if (s < 0 || E.third[6 * c + s] !== b) fail('L=' + L + ' cell ' + c + ': (' + c + ',' + a + ',' + b + ') is not a triangle of geo.tris');
        if (E.find(a, b) < 0 || E.find(b, a) < 0) fail('L=' + L + ' cell ' + c + ': consecutive neighbours ' + a + ' and ' + b + ' are not adjacent');
        let listed = false;
        for (let i = 0; i < g.nbrN[a]; i++) if (g.nbr[6 * a + i] === b) listed = true;
        if (!listed) fail('L=' + L + ' cell ' + c + ': ' + b + ' is missing from the neighbour list of ' + a);
      }
      if (!(Math.abs(turn - 2 * PI) < 1e-9)) fail('L=' + L + ' cell ' + c + ': neighbours wind ' + turn + ' rad, expected exactly one turn');
    }
    say('L=' + L + ' smallest n.(a x b) between consecutive neighbours: ' + minDet.toFixed(3) + ' step^2');
  }
});

// Greedy descent on the dot product (cellAt) and the flood fill of a cap (within) are exact only if
// the triangle mesh is the convex hull of the STORED float32 positions: then every cell that is not
// the best one has a neighbour that is strictly better. A closed star-shaped surface is convex iff it
// is convex across every edge, so check that: for the two triangles (a,b,c) and (b,a,d) sharing an
// edge, d must lie strictly below the plane of (a,b,c), by a margin far above float32 rounding.
T.test('adv: the stored float32 mesh is strictly convex across every edge (why cellAt and within can be exact)', () => {
  for (const L of LEVELS) {
    const g = G(L), p = g.pos, E = edges(L);
    let min = Infinity, count = 0;
    for (let a = 0; a < g.n; a++) {
      for (let k = 0; k < E.cnt[a]; k++) {
        const b = E.to[6 * a + k];
        if (b < a) continue;                                   // each undirected edge once
        const c = E.third[6 * a + k];                          // triangle (a, b, c), counter-clockwise
        const s = E.find(b, a);
        if (s < 0) fail('L=' + L + ' edge ' + a + '-' + b + ' has no triangle on its other side');
        const d = E.third[6 * b + s];                          // triangle (b, a, d) on the other side
        if (d === c || d === a || d === b) fail('L=' + L + ' edge ' + a + '-' + b + ' has a degenerate pair of triangles');
        const ax = p[3 * a], ay = p[3 * a + 1], az = p[3 * a + 2];
        const ux = p[3 * b] - ax, uy = p[3 * b + 1] - ay, uz = p[3 * b + 2] - az;
        const vx = p[3 * c] - ax, vy = p[3 * c + 1] - ay, vz = p[3 * c + 2] - az;
        const wx = p[3 * d] - ax, wy = p[3 * d + 1] - ay, wz = p[3 * d + 2] - az;
        const nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;   // outward normal of (a,b,c)
        const depth = -(wx * nx + wy * ny + wz * nz) / Math.sqrt(nx * nx + ny * ny + nz * nz);
        const rel = depth / (g.step * g.step);
        if (rel < min) min = rel;
        if (!(rel > 0.2)) fail('L=' + L + ' the mesh is not convex across edge ' + a + '-' + b + ': cell ' + d + ' is ' + depth + ' below the face (' + a + ',' + b + ',' + c + ')');
        count++;
      }
    }
    T.eq(count, 30 * Math.pow(4, L), 'edges checked at L=' + L);
    say('L=' + L + ' convexity: every opposite corner is at least ' + min.toFixed(3) + ' step^2 = ' + (min * g.step * g.step).toExponential(2) + ' below the neighbouring face (float32 noise is about 1e-7)');
  }
});

T.test('adv: prefix property, bitwise, for every pair of levels 0..7', () => {
  for (let k = 0; k < 7; k++) {
    const a = G(k);
    for (let m = k + 1; m <= 7; m++) {
      const b = G(m);
      T.eq(b.prefix(k), a.n, 'prefix(' + k + ') at L=' + m);
      for (let i = 0; i < 3 * a.n; i++) if (a.pos[i] !== b.pos[i]) fail('pos of level ' + k + ' is not a prefix of level ' + m + ' (index ' + i + ')');
      for (let i = 0; i < a.n; i++) {
        if (a.lat[i] !== b.lat[i] || a.lon[i] !== b.lon[i]) fail('lat/lon of level ' + k + ' is not a prefix of level ' + m + ' (cell ' + i + ')');
        if (a.nbrN[i] !== b.nbrN[i]) fail('nbrN of level ' + k + ' is not a prefix of level ' + m + ' (cell ' + i + ')');
      }
    }
  }
});

T.test('adv: each subdivision adds exactly one normalized midpoint per edge, appended, and splits every triangle in 4', () => {
  for (let k = 0; k < 7; k++) {
    const a = G(k), b = G(k + 1), Ea = edges(k), Eb = edges(k + 1), p = a.pos, q = b.pos;
    const used = new Uint8Array(b.n - a.n);
    const midOf = new Int32Array(6 * a.n).fill(-1);          // midpoint cell of the edge v → Ea.to[6v+s]
    let nEdges = 0, worst = 0;
    for (let v = 0; v < a.n; v++) {
      for (let s = 0; s < Ea.cnt[v]; s++) {
        const w = Ea.to[6 * v + s];
        if (w < v) continue;                                 // each undirected edge once
        let x = p[3 * v] + p[3 * w], y = p[3 * v + 1] + p[3 * w + 1], z = p[3 * v + 2] + p[3 * w + 2];
        const len = Math.sqrt(x * x + y * y + z * z);
        x /= len; y /= len; z /= len;
        // find the fine cell at that midpoint (verified by position below, so cellAt is not trusted here)
        const m = b.cellAt(x, y, z);
        if (!(m >= a.n && m < b.n)) fail('L=' + k + ' edge ' + v + '-' + w + ': the cell at its midpoint is ' + m + ', not a new cell (new ids start at ' + a.n + ')');
        const e = Math.max(Math.abs(q[3 * m] - x), Math.abs(q[3 * m + 1] - y), Math.abs(q[3 * m + 2] - z));
        if (e > worst) worst = e;
        if (!(e <= 3e-7)) fail('L=' + k + ' edge ' + v + '-' + w + ': cell ' + m + ' is ' + e + ' away from the normalized midpoint');
        if (used[m - a.n]) fail('L=' + k + ': new cell ' + m + ' is the midpoint of two edges');
        used[m - a.n] = 1;
        midOf[6 * v + s] = m;
        midOf[6 * w + Ea.find(w, v)] = m;
        nEdges++;
      }
    }
    T.eq(nEdges, b.n - a.n, 'one new cell per edge going from L=' + k + ' to L=' + (k + 1));
    // each coarse triangle (a,b,c) becomes (a,ab,ca) (b,bc,ab) (c,ca,bc) (ab,bc,ca), all CCW
    const thirdB = (u, v) => { const s = Eb.find(u, v); return s < 0 ? -1 : Eb.third[6 * u + s]; };
    for (let t = 0; t < a.nTri; t++) {
      const A = a.tris[3 * t], B = a.tris[3 * t + 1], C = a.tris[3 * t + 2];
      const ab = midOf[6 * A + Ea.find(A, B)], bc = midOf[6 * B + Ea.find(B, C)], ca = midOf[6 * C + Ea.find(C, A)];
      if (thirdB(A, ab) !== ca || thirdB(B, bc) !== ab || thirdB(C, ca) !== bc || thirdB(ab, bc) !== ca) {
        fail('L=' + k + ' triangle ' + t + ' (' + A + ',' + B + ',' + C + ') is not split into four at L=' + (k + 1));
      }
      // coarse edges do not survive as fine edges
      if (Eb.find(A, B) >= 0) fail('L=' + k + ' edge ' + A + '-' + B + ' still exists at L=' + (k + 1));
    }
    T.eq(b.nTri, 4 * a.nTri, 'four times as many triangles');
  }
});

T.test('adv: step is the mean angle over all neighbouring pairs', () => {
  T.near(G(0).step, Math.acos(1 / Math.sqrt(5)), 1e-7, 'icosahedron edge angle (63.43 degrees)');
  for (const L of LEVELS) {
    const g = G(L), E = edges(L), u = units(L);
    let sum = 0, cnt = 0, lo = Infinity, hi = 0;
    for (let v = 0; v < g.n; v++) {
      for (let k = 0; k < E.cnt[v]; k++) {
        const w = E.to[6 * v + k];
        if (w < v) continue;
        const a = indAngle(u, v, w);
        sum += a; cnt++;
        if (a < lo) lo = a;
        if (a > hi) hi = a;
      }
    }
    const mean = sum / cnt;
    if (!(Math.abs(g.step - mean) <= 2e-7 * mean)) fail('L=' + L + ' step ' + g.step + ' vs independently measured mean ' + mean);
    say('L=' + L + ' step ' + g.step.toFixed(7) + ' rad; edge angles ' + (lo / mean).toFixed(3) + ' .. ' + (hi / mean).toFixed(3) + ' of the mean');
    if (L > 0) T.ok(lo > 0.75 * mean && hi < 1.25 * mean, 'L=' + L + ' edge lengths stay within 25% of the mean');
  }
  // each level roughly halves the spacing
  for (let L = 1; L <= 7; L++) T.ok(G(L).step < 0.56 * G(L - 1).step && G(L).step > 0.45 * G(L - 1).step, 'step halves at L=' + L);
});

T.test('adv: angle is the great-circle angle (symmetric, exact 0 on the diagonal, pi at antipodes)', () => {
  for (const L of [0, 3, 6, 7]) {
    const g = G(L), u = units(L), r = rng(40 + L);
    for (let i = 0; i < 4000; i++) {
      const a = (r() * g.n) | 0, b = i % 9 === 0 ? g.nbr[6 * a + 2] : (r() * g.n) | 0;
      const x = g.angle(a, b);
      if (!(x >= 0 && x <= PI)) fail('L=' + L + ' angle(' + a + ',' + b + ') = ' + x + ' is out of range');
      if (x !== g.angle(b, a)) fail('L=' + L + ' angle is not symmetric for ' + a + ',' + b);
      if (!(Math.abs(x - indAngle(u, a, b)) <= 1e-12)) fail('L=' + L + ' angle(' + a + ',' + b + ') = ' + x + ', independent formula gives ' + indAngle(u, a, b));
      if (g.angle(a, a) !== 0) fail('L=' + L + ' angle(' + a + ',' + a + ') is not exactly 0');
    }
    T.near(g.angle(0, 11), PI, 1e-15, 'pole to pole');
    T.near(g.angle(1, 8), PI, 1e-7, 'vertex 1 and vertex 8 are antipodal');
    T.near(g.angle(0, 1), PI / 2 - Math.atan(0.5), 2e-7, 'pole to upper ring');
  }
});

T.test('adv: dir copies pos; basis is an orthonormal right-handed tangent frame, including at the poles', () => {
  for (const L of [0, 1, 2, 5, 6, 7]) {
    const g = G(L), p = g.pos;
    const E = [9, 9, 9], N = [9, 9, 9], d = [0, 0, 0, 'untouched'];
    for (let c = 0; c < g.n; c++) {
      if (g.dir(c, d) !== d) fail('dir must return its out argument');
      if (d[0] !== p[3 * c] || d[1] !== p[3 * c + 1] || d[2] !== p[3 * c + 2] || d[3] !== 'untouched') fail('L=' + L + ' dir(' + c + ') is wrong');
      g.basis(c, E, N);
      const x = d[0], y = d[1], z = d[2];
      const ee = E[0] * E[0] + E[1] * E[1] + E[2] * E[2], nn = N[0] * N[0] + N[1] * N[1] + N[2] * N[2];
      if (!(Math.abs(ee - 1) < 1e-12 && Math.abs(nn - 1) < 1e-12)) fail('L=' + L + ' cell ' + c + ': basis vectors are not unit length (' + ee + ', ' + nn + ')');
      if (!(Math.abs(E[0] * N[0] + E[1] * N[1] + E[2] * N[2]) < 1e-12)) fail('L=' + L + ' cell ' + c + ': east and north are not perpendicular');
      if (!(Math.abs(E[0] * x + E[1] * y + E[2] * z) < 2e-7 && Math.abs(N[0] * x + N[1] * y + N[2] * z) < 2e-7)) fail('L=' + L + ' cell ' + c + ': basis is not tangent');
      // right-handed: east x north = outward direction
      const ux = E[1] * N[2] - E[2] * N[1], uy = E[2] * N[0] - E[0] * N[2], uz = E[0] * N[1] - E[1] * N[0];
      if (!(Math.abs(ux - x) < 2e-7 && Math.abs(uy - y) < 2e-7 && Math.abs(uz - z) < 2e-7)) fail('L=' + L + ' cell ' + c + ': east x north is not the outward direction');
      if (c === 0 || c === 11) {
        if (!(E[0] === 1 && E[1] === 0 && E[2] === 0)) fail('L=' + L + ' east at pole ' + c + ' must be +X, got ' + E);
        if (!(N[0] === 0 && N[1] === 0 && N[2] === (c === 0 ? -1 : 1))) fail('L=' + L + ' north at pole ' + c + ' must be cross(n, east), got ' + N);
      } else {
        // east = normalize(cross(Y, n)) = normalize(z, 0, -x); north = cross(n, east) has a positive Y part
        const h = Math.sqrt(x * x + z * z);
        if (!(Math.abs(E[0] - z / h) < 1e-9 && E[1] === 0 && Math.abs(E[2] + x / h) < 1e-9)) fail('L=' + L + ' cell ' + c + ': east is not normalize(cross(Y, n))');
        const wx = y * E[2] - z * E[1], wy = z * E[0] - x * E[2], wz = x * E[1] - y * E[0];
        if (!(Math.abs(N[0] - wx) < 2e-7 && Math.abs(N[1] - wy) < 2e-7 && Math.abs(N[2] - wz) < 2e-7)) fail('L=' + L + ' cell ' + c + ': north is not cross(n, east)');
        if (!(N[1] > 0)) fail('L=' + L + ' cell ' + c + ': north does not point toward +Y');
      }
      // agreement with DM.V3.basis when vec.js is loaded
      if (DM.V3 && typeof DM.V3.basis === 'function' && (c < 400 || c % 53 === 0)) {
        const E2 = [0, 0, 0], N2 = [0, 0, 0];
        DM.V3.basis(d, E2, N2);
        for (let i = 0; i < 3; i++) if (!(Math.abs(E[i] - E2[i]) < 1e-6 && Math.abs(N[i] - N2[i]) < 1e-6)) fail('L=' + L + ' cell ' + c + ': geo.basis and V3.basis disagree');
      }
    }
    // typed-array outputs work too
    const e32 = new Float32Array(3), n32 = new Float32Array(3);
    g.basis(0, e32, n32);
    T.ok(e32[0] === 1 && e32[1] === 0 && e32[2] === 0 && n32[0] === 0 && n32[1] === 0 && n32[2] === -1, 'north-pole basis into Float32Array');
    g.basis(11, e32, n32);
    T.ok(e32[0] === 1 && e32[1] === 0 && e32[2] === 0 && n32[0] === 0 && n32[1] === 0 && n32[2] === 1, 'south-pole basis into Float32Array');
  }
});

// FOUNDATION §0: core files must not touch window, document, performance, Date, Math.random, timers or
// console. geo.js is loaded into a fresh vm context in which every one of those throws when it is
// read, called or even probed with typeof, then built and exercised there.
T.test('adv: geo.js runs in a bare JS context, never touches host objects / Date / Math.random, adds no global but DM, and is deterministic', () => {
  const fs = require('fs'), path = require('path'), vm = require('vm');
  const core = path.join(__dirname, '..', 'js', 'core');
  const ctx = vm.createContext({});
  // util.js and vec.js precede geo.js in the page; load them if present (they are not under test here)
  for (const f of ['util.js', 'vec.js']) {
    try { vm.runInContext(fs.readFileSync(path.join(core, f), 'utf8'), ctx, { filename: f }); } catch (e) { /* not under test */ }
  }
  vm.runInContext('(function () {' +
    '["window", "document", "navigator", "performance", "console", "setTimeout", "setInterval", "requestAnimationFrame",' +
    ' "process", "require", "localStorage", "fetch"].forEach(function (k) {' +
    '  Object.defineProperty(globalThis, k, { configurable: true,' +
    '    get: function () { throw new Error(k + " was touched"); }, set: function () { throw new Error(k + " was assigned"); } });' +
    '});' +
    'Math.random = function () { throw new Error("Math.random was called"); };' +
    'Date = function () { throw new Error("Date was used"); }; Date.now = Date;' +
    '})()', ctx);
  const names = () => vm.runInContext('Object.getOwnPropertyNames(globalThis).join(",")', ctx).split(',');
  const before = names();
  vm.runInContext(fs.readFileSync(path.join(core, 'geo.js'), 'utf8'), ctx, { filename: 'geo.js' });
  T.ok(vm.runInContext('typeof DM === "object" && typeof DM.Geo === "object" && typeof DM.Geo.build === "function"', ctx), 'geo.js attaches DM.Geo to the global DM');
  const got = vm.runInContext('(function () {' +
    'var g = DM.Geo.build(4), out = [], o2 = new Int32Array(g.n), e = [0, 0, 0], nn = [0, 0, 0], d = [0, 0, 0];' +
    'var a = g.cellAt(0.3, -0.2, 0.9), b = g.cellAt(30, -20, 90, a), far = g.cellAt(-1e-320, 3e-320, 2e-320, 5);' +
    'var w = g.within(a, 0.4, out), r = g.ring(a, 3, o2); g.basis(a, e, nn); g.dir(a, d);' +
    'return { n: g.n, a: a, b: b, far: far, w: w, r: r, out: out, ring: o2, e: e, nn: nn, d: d, ang: g.angle(0, a), step: g.step,' +
    ' pos: g.pos, lat: g.lat, lon: g.lon, nbr: g.nbr, nbrN: g.nbrN, tris: g.tris, cached: DM.Geo.get(4) === DM.Geo.get(4) };' +
    '})()', ctx);
  T.eq(names().filter(k => k !== 'DM' && before.indexOf(k) < 0), [], 'loading and using geo.js must not create globals other than DM');
  // the same calls on this realm's instance must give bit-identical results
  const g = G(4), out = [], o2 = new Int32Array(g.n), e = [0, 0, 0], nn = [0, 0, 0], d = [0, 0, 0];
  const a = g.cellAt(0.3, -0.2, 0.9);
  T.eq([got.n, got.a, got.b, got.far, got.cached], [g.n, a, g.cellAt(30, -20, 90, a), g.cellAt(-1e-320, 3e-320, 2e-320, 5), true], 'results inside the bare context');
  T.eq(got.b, a, 'a scaled copy of the direction finds the same cell');
  T.eq([got.w, got.r], [g.within(a, 0.4, out), g.ring(a, 3, o2)], 'within / ring counts inside the bare context');
  for (let i = 0; i < got.w; i++) if (got.out[i] !== out[i]) fail('within result differs in the bare context at ' + i);
  for (let i = 0; i < got.r; i++) if (got.ring[i] !== o2[i]) fail('ring result differs in the bare context at ' + i);
  g.basis(a, e, nn); g.dir(a, d);
  for (let i = 0; i < 3; i++) if (got.e[i] !== e[i] || got.nn[i] !== nn[i] || got.d[i] !== d[i]) fail('basis / dir differ in the bare context');
  T.ok(got.ang === g.angle(0, a) && got.step === g.step, 'angle and step are reproducible');
  for (const f of ['pos', 'lat', 'lon', 'nbr', 'nbrN', 'tris']) {
    if (got[f].length !== g[f].length) fail(f + ' has a different length in the bare context');
    for (let i = 0; i < g[f].length; i++) if (got[f][i] !== g[f][i]) fail('a second build gives a different ' + f + ' at index ' + i);
  }
});

// The header of geo.js promises that the methods are closures that may be detached from the instance.
T.test('adv: methods do not depend on `this`', () => {
  const g = G(5);
  const { prefix, cellAt, angle, within, ring, dir, basis } = g;
  T.eq(prefix(5), g.n, 'detached prefix');
  const c = cellAt(-0.4, 0.5, 0.2);
  T.eq(c, g.cellAt(-0.4, 0.5, 0.2), 'detached cellAt');
  T.eq(cellAt.call(null, -0.4, 0.5, 0.2, c), c, 'cellAt with this = null');
  T.eq(angle(c, 7), g.angle(c, 7), 'detached angle');
  const o1 = [], o2 = [];
  T.eq(within(c, 0.2, o1), g.within(c, 0.2, o2), 'detached within');
  T.eq(o1, o2, 'detached within result');
  T.eq(ring(c, 2, o1), g.ring(c, 2, o2), 'detached ring');
  T.eq(o1.slice(0, 19), o2.slice(0, 19), 'detached ring result');
  T.eq(dir(c, [0, 0, 0]), g.dir(c, [0, 0, 0]), 'detached dir');
  const e1 = [0, 0, 0], n1 = [0, 0, 0], e2 = [0, 0, 0], n2 = [0, 0, 0];
  basis(c, e1, n1); g.basis(c, e2, n2);
  T.eq([e1, n1], [e2, n2], 'detached basis');
});

// ===================================================================================================
// cellAt
// ===================================================================================================

// Hints that are not cell ids at all; every one of them must be ignored without crashing.
const BAD_HINTS = L => [-1, nOf(L), nOf(L) + 1, NaN, -Infinity, Infinity, undefined, 1e12, -1e12, 4294967296 + 5, -0.5, {}, 'x'];

T.test('adv: cellAt = brute force on 20,000 random directions at L = 6 (no hint, good / bad / invalid hints, scaled input)', () => {
  const g = G(6), r = rng(2026), d = [0, 0, 0], bad = BAD_HINTS(6);
  const N = 20000;
  let antiSeen = 0;
  for (let i = 0; i < N; i++) {
    randDir(r, d);
    const x = d[0], y = d[1], z = d[2];
    const want = brute(g, x, y, z), best = BR.dot;
    const check = (got, what) => {
      if (got !== want && !(got >= 0 && got < g.n && dotQ(g, x, y, z, got) === best)) {
        fail('direction #' + i + ' (' + x + ',' + y + ',' + z + ') ' + what + ': cellAt gave ' + got + ', nearest is ' + want);
      }
    };
    check(g.cellAt(x, y, z), 'without hint');
    check(g.cellAt(x, y, z, -1), 'hint -1');
    // good hints: the cell itself, each neighbour, a cell two hops away
    check(g.cellAt(x, y, z, want), 'hint = answer');
    const nb = g.nbr[6 * want + i % g.nbrN[want]];
    check(g.cellAt(x, y, z, nb), 'hint = neighbour ' + nb);
    check(g.cellAt(x, y, z, g.nbr[6 * nb + (i >> 3) % g.nbrN[nb]]), 'hint = two hops away');
    // bad hints: the antipodal cell, a random cell, cells 0 and n-1
    const anti = g.cellAt(-x, -y, -z);
    if (dotQ(g, x, y, z, anti) < -0.999 * Math.sqrt(x * x + y * y + z * z)) antiSeen++;
    check(g.cellAt(x, y, z, anti), 'hint = antipodal cell ' + anti);
    check(g.cellAt(x, y, z, (r() * g.n) | 0), 'hint = random cell');
    check(g.cellAt(x, y, z, 0), 'hint = 0');
    check(g.cellAt(x, y, z, g.n - 1), 'hint = n-1');
    // invalid hints
    check(g.cellAt(x, y, z, bad[i % bad.length]), 'hint = ' + String(bad[i % bad.length]));
    check(g.cellAt(x, y, z, g.n), 'hint = n');
    check(g.cellAt(x, y, z, NaN), 'hint = NaN');
    // non-normalized input: scaled by 0.001 and by 1000 (rounding differs, so allow numerical ties)
    for (const s of [0.001, 1000]) {
      const sx = x * s, sy = y * s, sz = z * s;
      for (const h of [-1, want, anti, NaN]) {
        const got = g.cellAt(sx, sy, sz, h);
        if (got !== want && !(got >= 0 && got < g.n && Math.abs(dotQ(g, x, y, z, got) - best) <= 1e-13)) {
          fail('direction #' + i + ' scaled by ' + s + ' with hint ' + h + ': cellAt gave ' + got + ', nearest is ' + want);
        }
      }
    }
  }
  T.eq(antiSeen, N, 'the antipodal hints really were antipodal');
});

T.test('adv: cellAt = brute force for every cell centre at L = 5; every centre maps to itself at L = 6 and 7', () => {
  const g = G(5), p = g.pos, bad = BAD_HINTS(5);
  for (let c = 0; c < g.n; c++) {
    const x = p[3 * c], y = p[3 * c + 1], z = p[3 * c + 2];
    if (brute(g, x, y, z) !== c) fail('brute force says cell ' + BR.id + ' is nearer to the centre of cell ' + c + ' than the cell itself');
    const anti = brute(g, -x, -y, -z);
    if (g.cellAt(-x, -y, -z) !== anti) fail('cellAt at the antipode of cell ' + c + ': ' + g.cellAt(-x, -y, -z) + ', nearest is ' + anti);
    const hints = [-1, c, g.nbr[6 * c + c % g.nbrN[c]], anti, (c * 7919) % g.n, bad[c % bad.length], g.n, NaN];
    for (const h of hints) {
      if (g.cellAt(x, y, z, h) !== c) fail('centre of cell ' + c + ' with hint ' + String(h) + ' gave ' + g.cellAt(x, y, z, h));
      if (g.cellAt(x * 0.001, y * 0.001, z * 0.001, h) !== c) fail('centre of cell ' + c + ' scaled by 0.001, hint ' + String(h) + ', gave ' + g.cellAt(x * 0.001, y * 0.001, z * 0.001, h));
      if (g.cellAt(x * 1000, y * 1000, z * 1000, h) !== c) fail('centre of cell ' + c + ' scaled by 1000, hint ' + String(h) + ', gave ' + g.cellAt(x * 1000, y * 1000, z * 1000, h));
    }
  }
  for (const L of [6, 7]) {
    const gg = G(L), q = gg.pos;
    for (let c = 0; c < gg.n; c++) {
      const x = q[3 * c], y = q[3 * c + 1], z = q[3 * c + 2];
      if (gg.cellAt(x, y, z) !== c) fail('L=' + L + ' centre of cell ' + c + ' gave ' + gg.cellAt(x, y, z));
      if (gg.cellAt(x * 1000, y * 1000, z * 1000, (c + 1) % gg.n) !== c) fail('L=' + L + ' centre of cell ' + c + ' (x1000, hint c+1) is wrong');
      if (gg.cellAt(x * 0.001, y * 0.001, z * 0.001, gg.nbr[6 * c + 4]) !== c) fail('L=' + L + ' centre of cell ' + c + ' (x0.001, neighbour hint) is wrong');
    }
  }
});

T.test('adv: cellAt at the poles and along the axes, every level, every scale and hint', () => {
  const axes = [[0, 1, 0], [0, -1, 0], [1, 0, 0], [-1, 0, 0], [0, 0, 1], [0, 0, -1],
    [1, 1, 0], [1, -1, 0], [0, 1, 1], [0, 1, -1], [1, 0, 1], [-1, 0, 1], [1, 1, 1], [-1, 1, 1], [1, -1, 1], [-1, -1, -1], [1, 1, -1]];
  for (const L of LEVELS) {
    const g = G(L), hints = [-1, 0, 11, 5, g.n - 1, g.n >> 1].concat(BAD_HINTS(L));
    for (const s of [1, 0.001, 1000, 1e-12, 123456.789]) {
      for (const h of hints) {
        if (g.cellAt(0, s, 0, h) !== 0) fail('L=' + L + ' north pole (scale ' + s + ', hint ' + String(h) + ') gave ' + g.cellAt(0, s, 0, h));
        if (g.cellAt(0, -s, 0, h) !== 11) fail('L=' + L + ' south pole (scale ' + s + ', hint ' + String(h) + ') gave ' + g.cellAt(0, -s, 0, h));
        // a hair off the pole is still the pole
        if (g.cellAt(1e-9 * s, s, -1e-9 * s, h) !== 0) fail('L=' + L + ' just off the north pole gave another cell');
      }
      for (const a of axes) {
        const x = a[0] * s, y = a[1] * s, z = a[2] * s;
        brute(g, x, y, z);
        for (const h of hints) {
          const got = g.cellAt(x, y, z, h);
          // exact ties happen on symmetry axes: accept any cell that is exactly as near as the best
          if (!(got >= 0 && got < g.n && dotQ(g, x, y, z, got) === BR.dot)) fail('L=' + L + ' direction (' + a + ') x ' + s + ' hint ' + String(h) + ': got ' + got + ', nearest is ' + BR.id);
        }
      }
    }
    // +X is vertex 1 (longitude 0 on the upper ring is not on the axis, so check through brute force only),
    // but the level-0 cells themselves must be found from anywhere
    for (let v = 0; v < 12; v++) {
      if (g.cellAt(g.pos[3 * v] * 55, g.pos[3 * v + 1] * 55, g.pos[3 * v + 2] * 55, 11 - v) !== v) fail('L=' + L + ' level-0 vertex ' + v + ' is not found from its opposite');
    }
  }
});

T.test('adv: cellAt on edge midpoints, triangle centres and just beside them', () => {
  // (1) one edge of every triangle at L = 3 and of 700 random triangles at L = 6, against brute force
  const offsets = [0, 0.3, -0.3, 1e-3, -1e-3, 1e-6, -1e-6, 1e-9, -1e-9, 1e-13, -1e-13];
  for (const [L, count] of [[3, 0], [6, 700]]) {
    const g = G(L), p = g.pos, r = rng(600 + L);
    const total = count || g.nTri;
    for (let i = 0; i < total; i++) {
      const t = count ? (r() * g.nTri) | 0 : i;
      const rot = i % 3;                                     // which edge of the triangle
      const a = g.tris[3 * t + rot], b = g.tris[3 * t + (rot + 1) % 3], c = g.tris[3 * t + (rot + 2) % 3];
      const mx = p[3 * a] + p[3 * b], my = p[3 * a + 1] + p[3 * b + 1], mz = p[3 * a + 2] + p[3 * b + 2];
      const dx = p[3 * a] - p[3 * b], dy = p[3 * a + 1] - p[3 * b + 1], dz = p[3 * a + 2] - p[3 * b + 2];
      // exactly on the midpoint, then pushed toward a or b by amounts from large to far below float32
      // noise (at L = 6 each triangle gets the exact midpoint and four of the ten offsets)
      const list = count ? [0, offsets[1 + i % 10], offsets[1 + (i + 3) % 10], offsets[1 + (i + 6) % 10], offsets[1 + (i + 9) % 10]] : offsets;
      for (const e of list) {
        const x = mx + e * dx, y = my + e * dy, z = mz + e * dz;
        brute(g, x, y, z);
        for (const h of [-1, a, b, c, 0]) {
          const got = g.cellAt(x, y, z, h);
          if (!(got >= 0 && got < g.n && dotQ(g, x, y, z, got) === BR.dot)) fail('L=' + L + ' edge ' + a + '-' + b + ' offset ' + e + ' hint ' + h + ': got ' + got + ', nearest is ' + BR.id);
          if (got !== a && got !== b) fail('L=' + L + ' edge ' + a + '-' + b + ' offset ' + e + ': got ' + got + ', which is neither end');
          if (Math.abs(e) >= 1e-3 && got !== (e > 0 ? a : b)) fail('L=' + L + ' edge ' + a + '-' + b + ' offset ' + e + ': got the far end');
        }
      }
      // the triangle's circumcentre direction (equidistant from all three corners) and its centroid
      const ux = -dx, uy = -dy, uz = -dz;
      const vx = p[3 * c] - p[3 * a], vy = p[3 * c + 1] - p[3 * a + 1], vz = p[3 * c + 2] - p[3 * a + 2];
      const cc = [uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx];
      const ce = [mx + p[3 * c], my + p[3 * c + 1], mz + p[3 * c + 2]];
      for (const q of [cc, ce]) {
        brute(g, q[0], q[1], q[2]);
        for (const h of [-1, a, c]) {
          const got = g.cellAt(q[0], q[1], q[2], h);
          if (!(got >= 0 && got < g.n && dotQ(g, q[0], q[1], q[2], got) === BR.dot)) fail('L=' + L + ' centre of triangle ' + t + ' hint ' + h + ': got ' + got + ', nearest is ' + BR.id);
          if (got !== a && got !== b && got !== c) fail('L=' + L + ' centre of triangle ' + t + ': got ' + got + ', not one of its corners');
        }
      }
    }
  }
  // (2) every edge midpoint at L = 6 and 7: the answer must be one of the two ends, and the nearer one
  for (const L of [6, 7]) {
    const g = G(L), p = g.pos, E = edges(L);
    for (let a = 0; a < g.n; a++) {
      for (let k = 0; k < E.cnt[a]; k++) {
        const b = E.to[6 * a + k];
        if (b < a) continue;
        const x = (p[3 * a] + p[3 * b]) * 0.37, y = (p[3 * a + 1] + p[3 * b + 1]) * 0.37, z = (p[3 * a + 2] + p[3 * b + 2]) * 0.37;
        const got = g.cellAt(x, y, z), got2 = g.cellAt(x, y, z, E.third[6 * a + k]);
        const da = dotQ(g, x, y, z, a), db = dotQ(g, x, y, z, b), top = da > db ? da : db;
        if ((got !== a && got !== b) || dotQ(g, x, y, z, got) !== top) fail('L=' + L + ' midpoint of edge ' + a + '-' + b + ' gave ' + got);
        if ((got2 !== a && got2 !== b) || dotQ(g, x, y, z, got2) !== top) fail('L=' + L + ' midpoint of edge ' + a + '-' + b + ' with a hint gave ' + got2);
      }
    }
  }
});

T.test('adv: cellAt on the seams and bucket borders of a cube-map lookup, and near-degenerate directions', () => {
  for (const L of [3, 6]) {
    const g = G(L), r = rng(77 + L), N = L === 6 ? 3000 : 20000;
    const res = 1 << L;
    for (let i = 0; i < N; i++) {
      // a point on a face of the cube whose first coordinate sits on (or a hair beside) a grid line
      const line = ((r() * (2 * res + 1)) | 0) / res - 1;
      const eps = [0, 1e-15, -1e-15, 1e-9, -1e-9, 1e-4][i % 6];
      const u = line + eps, v = i % 7 === 0 ? ((r() * (2 * res + 1)) | 0) / res - 1 : r() * 2 - 1;
      const s = r() < 0.5 ? -1 : 1, sc = [1, 0.001, 1000][i % 3];
      let x, y, z;
      switch (i % 6) {
        case 0: x = s; y = u; z = v; break;
        case 1: x = s; y = v; z = u; break;
        case 2: x = u; y = s; z = v; break;
        case 3: x = v; y = s; z = u; break;
        case 4: x = u; y = v; z = s; break;
        default: x = v; y = u; z = s;
      }
      x *= sc; y *= sc; z *= sc;
      brute(g, x, y, z);
      for (const h of [-1, BR.id, (r() * g.n) | 0]) {
        const got = g.cellAt(x, y, z, h);
        if (!(got >= 0 && got < g.n && dotQ(g, x, y, z, got) === BR.dot)) fail('L=' + L + ' cube point (' + x + ',' + y + ',' + z + ') hint ' + h + ': got ' + got + ', nearest is ' + BR.id);
      }
    }
  }
  // signed zeros and tiny components must not send the search to the wrong face
  const g = G(6);
  for (const v of [[-0, 1, -0], [-0, -1, 0], [1, -0, -0], [-1, 0, -0], [1e-300, 1, 1e-300], [-1e-300, -1, 1e-300], [1, 1e-17, -1e-17],
    [1, 1, 1e-17], [1, -1, -1e-17], [1e-8, 1e-8, 1e-8], [1, 0.9999999999999999, 0.9999999999999999], [5e-8, -3e7, 2e-3]]) {
    brute(g, v[0], v[1], v[2]);
    for (const h of [-1, 0, 11, BR.id, 20000]) {
      const got = g.cellAt(v[0], v[1], v[2], h);
      if (!(got >= 0 && got < g.n && dotQ(g, v[0], v[1], v[2], got) === BR.dot)) fail('direction (' + v + ') hint ' + h + ': got ' + got + ', nearest is ' + BR.id);
    }
  }
});

T.test('adv: cellAt on random directions at every other level (0..5 and 7)', () => {
  const d = [0, 0, 0];
  for (const [L, N] of [[0, 3000], [1, 3000], [2, 3000], [3, 3000], [4, 3000], [5, 3000], [7, 1500]]) {
    const g = G(L), r = rng(900 + L), bad = BAD_HINTS(L);
    for (let i = 0; i < N; i++) {
      randDir(r, d);
      const s = [1, 0.001, 1000][i % 3];
      const x = d[0] * s, y = d[1] * s, z = d[2] * s;
      const want = brute(g, x, y, z);
      const hints = [-1, want, g.nbr[6 * want + i % 5], g.cellAt(-x, -y, -z), (r() * g.n) | 0, bad[i % bad.length]];
      for (const h of hints) {
        const got = g.cellAt(x, y, z, h);
        if (got !== want && !(got >= 0 && got < g.n && dotQ(g, x, y, z, got) === BR.dot)) fail('L=' + L + ' direction #' + i + ' hint ' + String(h) + ': got ' + got + ', nearest is ' + want);
      }
    }
  }
});

T.test('adv: cellAt stays exact for absurdly small and large vectors (subnormal to near-overflow)', () => {
  const g = G(6), r = rng(31337), d = [0, 0, 0];
  const UP = Math.pow(2, 1000), DOWN = Math.pow(2, -600);    // exact rescaling for the brute-force reference
  for (const s of [5e-324, 1e-322, 1e-320, 1e-315, 1e-310, 1e-305, 1e-250, 1e-160, 1e-100, 1e100, 1e160, 1e250, 1e300, 1e305, 1e307, 1.7e308]) {
    for (let i = 0; i < 150; i++) {
      randDir(r, d);
      let x, y, z;
      if (s < 1e-300) {
        // build the vector out of whole multiples of the scale so that subnormals keep a direction
        const k = s < 1e-320 ? 60 : 1;
        x = Math.round(d[0] * 1000 * k) * s; y = Math.round(d[1] * 1000 * k) * s; z = Math.round(d[2] * 1000 * k) * s;
      } else {
        const m = Math.max(Math.abs(d[0]), Math.abs(d[1]), Math.abs(d[2]));
        x = d[0] / m * s; y = d[1] / m * s; z = d[2] / m * s;   // largest component = ±s, never overflows
      }
      if (x === 0 && y === 0 && z === 0) continue;
      if (!(isFinite(x) && isFinite(y) && isFinite(z))) fail('test bug: non-finite input');
      // reference: the same vector moved into the normal range by an exact power of two
      const f = s < 1 ? UP : DOWN;
      const rx = x * f, ry = y * f, rz = z * f;
      const want = brute(g, rx, ry, rz);
      for (const h of [-1, want, g.nbr[6 * want], 0, (r() * g.n) | 0, NaN]) {
        const got = g.cellAt(x, y, z, h);
        if (got !== want && !(got >= 0 && got < g.n && dotQ(g, rx, ry, rz, got) === BR.dot)) {
          fail('vector (' + x + ',' + y + ',' + z + ') hint ' + h + ': cellAt gave ' + got + ', nearest is ' + want);
        }
      }
    }
  }
  // mixed magnitudes
  T.eq(g.cellAt(0, 1.7e308, 0), 0, 'huge north');
  T.eq(g.cellAt(0, -5e-324, 0), 11, 'smallest possible south');
  T.eq(g.cellAt(1.7e308, 1.7e308, 1.7e308), g.cellAt(1, 1, 1), 'huge diagonal');
  T.eq(g.cellAt(-5e-324, 5e-324, 5e-324, 3), g.cellAt(-1, 1, 1), 'tiny diagonal');
  T.eq(g.cellAt(1e-300, 1e300, 1e-300, 11), 0, 'component range of 1e600');
});

T.test('adv: cellAt never crashes or hangs on degenerate input and always returns a cell id', () => {
  const vals = [0, -0, NaN, Infinity, -Infinity, 1, -1, 1e308, -1e308, 5e-324];
  for (const L of [0, 4, 6]) {
    const g = G(L), hints = [-1, 0, 7, g.n - 1].concat(BAD_HINTS(L)).concat([null, true, false, '3', [], 2.5, g.n - 0.5, -0]);
    for (const x of vals) for (const y of vals) for (const z of vals) {
      for (const h of hints) {
        const got = g.cellAt(x, y, z, h);
        if (!(Number.isInteger(got) && got >= 0 && got < g.n)) fail('L=' + L + ' cellAt(' + x + ',' + y + ',' + z + ', ' + String(h) + ') returned ' + got);
      }
    }
    // and afterwards the instance still works
    T.eq(g.cellAt(0, 1, 0), 0);
    T.eq(g.cellAt(0, -2, 0, NaN), 11);
  }
});

// ===================================================================================================
// within
// ===================================================================================================

// Verifies one within() call against both angle definitions. `ind` / `own` are the angles from c to
// every cell (independent formula / geo.angle). Returns the count.
const seenStamp = { v: 0 };
function checkWithin(g, c, ang, out, ind, own, seen, label) {
  const cnt = g.within(c, ang, out);
  if (!(Number.isInteger(cnt) && cnt >= 1 && cnt <= g.n)) fail(label + ': count ' + cnt);
  if (out[0] !== c) fail(label + ': out[0] is ' + out[0] + ', the centre must come first');
  const stamp = ++seenStamp.v;
  for (let i = 0; i < cnt; i++) {
    const v = out[i];
    if (!(Number.isInteger(v) && v >= 0 && v < g.n)) fail(label + ': out[' + i + '] = ' + v + ' is not a cell id');
    if (seen[v] === stamp) fail(label + ': cell ' + v + ' appears twice');
    seen[v] = stamp;
    if (i > 0 && !(own[v] <= ang)) fail(label + ': cell ' + v + ' is at geo.angle ' + own[v] + ', outside the radius');
    if (i > 0 && !(ind[v] <= ang + ANG_TOL)) fail(label + ': cell ' + v + ' is at ' + ind[v] + ' rad, outside the radius');
  }
  for (let i = 0; i < g.n; i++) {
    if (seen[i] !== stamp && (own[i] <= ang || ind[i] <= ang - ANG_TOL)) fail(label + ': cell ' + i + ' at ' + ind[i] + ' rad is missing');
  }
  return cnt;
}
function anglesFrom(L, c, ind, own) {
  const g = G(L), u = units(L);
  for (let i = 0; i < g.n; i++) { ind[i] = indAngle(u, c, i); own[i] = g.angle(c, i); }
}

T.test('adv: within = brute force for random cells and radii 0, step/2, 3 step, 0.5, 2, pi (Array and Int32Array)', () => {
  for (const [L, nRandom] of [[2, 30], [4, 30], [5, 14], [6, 16], [7, 2]]) {
    const g = G(L), r = rng(5000 + L);
    const ind = new Float64Array(g.n), own = new Float64Array(g.n), seen = new Int32Array(g.n);
    const typed = new Int32Array(g.n), arr = [];
    const centres = L === 7 ? [0, g.n - 1] : [0, 11, 3, 8, 12, g.n - 1];
    for (let i = 0; i < nRandom; i++) centres.push((r() * g.n) | 0);
    const radii = [0, 0.5 * g.step, 3 * g.step, 0.5, 2, PI];
    for (const c of centres) {
      anglesFrom(L, c, ind, own);
      const extra = [g.step, 1e-12, 0.9999 * g.step * 0.8, PI / 2, PI - 1e-7, 3.2, r() * PI, r() * 6 * g.step, PI - r() * 3 * g.step];
      let prevCnt = 0, prevAng = -1;
      for (const ang of radii.concat(L === 7 ? [] : extra)) {
        const label = 'L=' + L + ' within(' + c + ', ' + ang + ')';
        const nT = checkWithin(g, c, ang, typed, ind, own, seen, label + ' into Int32Array');
        arr.length = 0;
        const nA = checkWithin(g, c, ang, arr, ind, own, seen, label + ' into Array');
        if (nA !== nT) fail(label + ': Array count ' + nA + ' != Int32Array count ' + nT);
        if (arr.length !== nA) fail(label + ': an empty Array grew to ' + arr.length + ' entries for count ' + nA);
        for (let i = 0; i < nA; i++) if (arr[i] !== typed[i]) fail(label + ': Array and Int32Array results differ at ' + i);
        if (g.within(c, ang) !== nT) fail(label + ': default out gives a different count');
        if (ang > prevAng && nT < prevCnt) fail(label + ': a larger radius returned fewer cells');
        if (ang >= prevAng) { prevAng = ang; prevCnt = nT; }
      }
      // fixed expectations
      if (g.within(c, 0, typed) !== 1 || typed[0] !== c) fail('L=' + L + ' radius 0 around ' + c + ' must be the cell alone');
      if (g.within(c, 0.5 * g.step, typed) !== 1) fail('L=' + L + ' half a step around ' + c + ' must reach no neighbour');
      if (g.within(c, PI, typed) !== g.n) fail('L=' + L + ' radius pi around ' + c + ' must be the whole sphere');
      if (L > 0 && g.within(c, 2, typed) >= g.n) fail('L=' + L + ' radius 2 must not be the whole sphere');
    }
  }
});

T.test('adv: within on the knife edge: radius exactly at a cell, one ulp below and above', () => {
  for (const L of [3, 6]) {
    const g = G(L), r = rng(8100 + L);
    const ind = new Float64Array(g.n), own = new Float64Array(g.n), seen = new Int32Array(g.n), out = new Int32Array(g.n);
    for (let q = 0; q < 12; q++) {
      const c = q < 2 ? q * 11 : (r() * g.n) | 0;
      anglesFrom(L, c, ind, own);
      const targets = [g.nbr[6 * c], g.nbr[6 * c + 3], g.nbr[6 * g.nbr[6 * c + 1] + 1], (r() * g.n) | 0, (r() * g.n) | 0, (r() * g.n) | 0];
      for (const far of targets) {
        if (far === c) continue;
        const a = own[far];
        const below = a * (1 - 1.2e-16), above = a * (1 + 2.3e-16);
        T.ok(below < a && above > a, 'ulp steps');
        let cnt = checkWithin(g, c, a, out, ind, own, seen, 'L=' + L + ' within(' + c + ', angle to ' + far + ')');
        let has = false;
        for (let i = 0; i < cnt; i++) if (out[i] === far) has = true;
        if (!has) fail('L=' + L + ' cell ' + far + ' at exactly the radius from ' + c + ' must be included');
        cnt = checkWithin(g, c, below, out, ind, own, seen, 'L=' + L + ' within(' + c + ', just under the angle to ' + far + ')');
        for (let i = 0; i < cnt; i++) if (out[i] === far) fail('L=' + L + ' cell ' + far + ' just outside the radius from ' + c + ' must be excluded');
        checkWithin(g, c, above, out, ind, own, seen, 'L=' + L + ' within(' + c + ', just over the angle to ' + far + ')');
      }
    }
  }
});

T.test('adv: within / ring argument edge cases do not corrupt later calls', () => {
  const g = G(5), out = new Int32Array(g.n), arr = [];
  const ref = g.within(1234, 0.3, out);
  const keep = out.slice(0, ref);
  // an Array that already holds more entries than the result: the first `count` entries are the result
  const stale = new Array(5000).fill(-7);
  const nS = g.within(1234, 0.3, stale);
  T.eq(nS, ref, 'count into a pre-filled Array');
  for (let i = 0; i < ref; i++) if (stale[i] !== keep[i]) fail('pre-filled Array: entry ' + i + ' differs');
  // odd radii and ids must not throw, and must leave the instance healthy
  for (const ang of [-1, -0, NaN, -Infinity, undefined, null, 'x']) {
    const o = [];
    const n1 = g.within(77, ang, o);
    if (!(n1 === 1 && o[0] === 77)) fail('within(77, ' + String(ang) + ') should give the cell alone, got count ' + n1);
  }
  T.eq(g.within(77, Infinity, out), g.n, 'infinite radius is the whole sphere');
  T.eq(g.within(77, 1e300, out), g.n, 'huge radius is the whole sphere');
  for (const c of [-1, g.n, g.n + 100, NaN, undefined, -Infinity, Infinity]) {
    let n1, n2;
    try { n1 = g.within(c, 0.2, arr); n2 = g.ring(c, 2, arr); } catch (e) { fail('within/ring threw for cell id ' + String(c) + ': ' + e.message); }
    if (n1 !== 0 || n2 !== 0) fail('within/ring for a non-cell id ' + String(c) + ' should report 0 cells, got ' + n1 + ' / ' + n2);
  }
  for (const steps of [-1, NaN, undefined, null, 0]) T.eq(g.ring(500, steps, arr), 1, 'ring with steps = ' + String(steps));
  T.eq(g.ring(500, Infinity, out), g.n, 'ring with infinite steps');
  // "within `steps` hops": a fractional step count reaches exactly as far as its floor
  // (callers may well pass radius / geo.step without rounding)
  for (const c of [0, 11, 500, g.n - 1]) {
    for (const [steps, whole] of [[0.5, 0], [0.999999, 0], [1.5, 1], [2.0000001, 2], [2.5, 2], [2.999, 2], [7.75, 7], [-0.5, 0], [1e-300, 0]]) {
      const want = g.ring(c, whole, out), keepRing = out.slice(0, want);
      const cnt = g.ring(c, steps, arr);
      if (cnt !== want) fail('ring(' + c + ', ' + steps + ') returned ' + cnt + ' cells; ' + whole + ' whole hops give ' + want);
      for (let i = 0; i < want; i++) if (arr[i] !== keepRing[i]) fail('ring(' + c + ', ' + steps + ') differs from ring(' + c + ', ' + whole + ') at index ' + i);
    }
    T.eq(g.ring(c, 1, out), 1 + g.nbrN[c], 'one whole hop is the cell and its neighbours');
  }
  // a typed array that is too small: no throw, and the next call is still right
  const small = new Int32Array(4);
  g.within(1234, 0.3, small);
  g.ring(1234, 5, small);
  T.eq(small[0], 1234, 'undersized output still starts with the centre');
  T.eq(g.within(1234, 0.3, out), ref, 'same count after the odd calls');
  for (let i = 0; i < ref; i++) if (out[i] !== keep[i]) fail('within is not repeatable after the odd calls (entry ' + i + ')');
});

T.test('adv: within and ring are re-entrant: nested calls inside loops over outer results', () => {
  for (const L of [4, 6]) {
    const g = G(L), g2 = Geo.build(L);                       // a second instance of the same level, too
    const ind = new Float64Array(g.n), own = new Float64Array(g.n), seen = new Int32Array(g.n);
    const outer = [], inner = new Int32Array(g.n), inner2 = [], innerRing = new Int32Array(g.n), other = new Int32Array(g.n);
    const c0 = L === 4 ? 1500 : 23456, R = L === 4 ? 0.5 : 0.12;
    const nOuter = g.within(c0, R, outer);
    const copy = outer.slice();
    T.ok(nOuter > 100 && outer.length === nOuter, 'outer result size ' + nOuter);
    let checked = 0;
    for (let i = 0; i < nOuter; i++) {
      const c = outer[i];
      if (c !== copy[i]) fail('L=' + L + ' outer result changed under the loop at index ' + i);
      const nIn = g.within(c, 3 * g.step, inner);
      const innerCopy = inner.slice(0, nIn);
      if (inner[0] !== c) fail('L=' + L + ' inner within does not start with its centre');
      const nR = g.ring(c, 2, innerRing);
      if (innerRing[0] !== c || nR < 16 || nR > 19) fail('L=' + L + ' inner ring(' + c + ', 2) returned ' + nR + ' cells');
      // third level: calls inside a loop over the inner result, on this instance and on its twin
      for (let k = 0; k < nIn; k += 4) {
        const cc = inner[k];
        const n3 = g.within(cc, 1.5 * g.step, inner2);
        if (inner2[0] !== cc || n3 < 6 || n3 > 19) fail('L=' + L + ' third-level within returned ' + n3 + ' cells');
        if (g2.within(cc, 1.5 * g.step, other) !== n3) fail('L=' + L + ' the second instance disagrees');
        g.ring(cc, 1);
        g.cellAt(g.pos[3 * cc], g.pos[3 * cc + 1], g.pos[3 * cc + 2], c);
      }
      for (let k = 0; k < nIn; k++) if (inner[k] !== innerCopy[k]) fail('L=' + L + ' inner result was disturbed by nested calls');
      for (let k = 0; k < nR; k++) if (k > 0 && g.angle(c, innerRing[k]) > 2.5 * g.step) fail('L=' + L + ' ring result was disturbed by nested calls');
      if (i % 61 === 0) {                                    // full brute-force check of some inner results
        anglesFrom(L, c, ind, own);
        checkWithin(g, c, 3 * g.step, inner, ind, own, seen, 'L=' + L + ' nested within(' + c + ')');
        checked++;
      }
    }
    T.ok(checked >= 3, 'some nested results were brute-force checked');
    T.eq(outer.length, nOuter, 'outer array length unchanged');
    for (let i = 0; i < nOuter; i++) if (outer[i] !== copy[i]) fail('L=' + L + ' outer result was corrupted by nested calls (index ' + i + ')');
    anglesFrom(L, c0, ind, own);
    checkWithin(g, c0, R, inner, ind, own, seen, 'L=' + L + ' outer query repeated afterwards');
    // the same with ring() as the outer loop
    const outerRing = [];
    const nRing = g.ring(c0, 6, outerRing);
    const ringCopy = outerRing.slice();
    for (let i = 0; i < nRing; i++) {
      g.ring(outerRing[i], 3, innerRing);
      g.within(outerRing[i], 2 * g.step, inner);
      g.ring(outerRing[i], 1);
    }
    for (let i = 0; i < nRing; i++) if (outerRing[i] !== ringCopy[i]) fail('L=' + L + ' outer ring result was corrupted by nested calls');
  }
});

// ===================================================================================================
// ring
// ===================================================================================================

T.test('adv: ring = independent breadth-first search (count, set, hop order, c first)', () => {
  for (const L of [0, 1, 2, 3, 4, 5, 6]) {
    const g = G(L), r = rng(7000 + L), out = new Int32Array(g.n), arr = [], seen = new Int32Array(g.n);
    let stamp = 0;
    const centres = [0, 11, 4, 9, 12 % g.n, g.n - 1];
    const extra = L <= 2 ? g.n : 14;                         // every cell at the small levels
    for (let i = 0; i < extra; i++) centres.push(L <= 2 ? i : (r() * g.n) | 0);
    for (const c of centres) {
      const dist = hopDist(L, c);
      let far = 0;
      const perHop = [];
      for (let i = 0; i < g.n; i++) { const d = dist[i]; if (d > far) far = d; perHop[d] = (perHop[d] || 0) + 1; }
      // canonical first-in-first-out order over geo.nbr
      const fifo = new Int32Array(g.n);
      { const m = new Uint8Array(g.n); let h = 0, t = 0; fifo[t++] = c; m[c] = 1;
        while (h < t) { const a = fifo[h++]; for (let j = 0; j < g.nbrN[a]; j++) { const b = g.nbr[6 * a + j]; if (!m[b]) { m[b] = 1; fifo[t++] = b; } } } }
      for (const steps of [0, 1, 2, 3, 4, 7, 12, 25, far - 1, far, far + 1, 1000000]) {
        if (steps < 0) continue;
        let want = 0;
        for (let d = 0; d <= Math.min(steps, far); d++) want += perHop[d];
        const useArr = (steps & 1) === 1;
        if (useArr) arr.length = 0;
        const o = useArr ? arr : out;
        const cnt = g.ring(c, steps, o);
        const label = 'L=' + L + ' ring(' + c + ', ' + steps + ')';
        if (cnt !== want) fail(label + ': count ' + cnt + ', BFS says ' + want);
        if (o[0] !== c) fail(label + ': the centre must come first');
        if (useArr && arr.length !== cnt) fail(label + ': Array length ' + arr.length + ' for count ' + cnt);
        stamp++;
        for (let i = 0; i < cnt; i++) {
          const v = o[i];
          if (!(v >= 0 && v < g.n)) fail(label + ': out[' + i + '] = ' + v);
          if (seen[v] === stamp) fail(label + ': cell ' + v + ' appears twice');
          seen[v] = stamp;
          if (dist[v] > steps) fail(label + ': cell ' + v + ' is ' + dist[v] + ' hops away');
          if (i > 0 && dist[v] < dist[o[i - 1]]) fail(label + ': not in breadth-first order at index ' + i);
          if (v !== fifo[i]) fail(label + ': order differs from first-in-first-out BFS over the neighbour lists at index ' + i);
        }
      }
      // one hop is the cell followed by its neighbours in list order
      const n1 = g.ring(c, 1, out);
      if (n1 !== 1 + g.nbrN[c]) fail('L=' + L + ' ring(' + c + ', 1) has ' + n1 + ' cells');
      for (let j = 0; j < g.nbrN[c]; j++) if (out[1 + j] !== g.nbr[6 * c + j]) fail('L=' + L + ' ring(' + c + ', 1) is not in neighbour order');
    }
  }
  // closed form around a pentagon: 1 + 5k(k+1)/2 cells within k hops (while k stays below the next pentagon)
  for (const L of [4, 6, 7]) {
    const g = G(L), o = [];
    for (let k = 0; k <= Math.min(1 << L, 40) / 2; k++) T.eq(g.ring(0, k, o), 1 + 5 * k * (k + 1) / 2, 'L=' + L + ' hops ' + k + ' around the north pole');
    T.eq(g.ring(11, 3), 31, 'default out');
  }
});

// ===================================================================================================
// timing targets (FOUNDATION §4: build < 400 ms, cellAt < 2 us with a good hint, < 10 us without,
// within linear in the result size)
// ===================================================================================================

T.test('adv: timing targets at L = 6', () => {
  // build
  const builds = [];
  for (let i = 0; i < 4; i++) { const t = nowMs(); Geo.build(6); builds.push(nowMs() - t); }
  const buildBest = Math.min.apply(null, builds), buildWorst = Math.max.apply(null, builds);

  const g = G(6), r = rng(4242), d = [0, 0, 0], N = 200000;
  const dirs = new Float64Array(3 * N), ans = new Int32Array(N), nbHint = new Int32Array(N), anti = new Int32Array(N), rnd = new Int32Array(N);
  for (let i = 0; i < N; i++) { randDir(r, d); dirs[3 * i] = d[0]; dirs[3 * i + 1] = d[1]; dirs[3 * i + 2] = d[2]; }
  for (let i = 0; i < N; i++) {
    ans[i] = g.cellAt(dirs[3 * i], dirs[3 * i + 1], dirs[3 * i + 2]);
    nbHint[i] = g.nbr[6 * ans[i] + i % 5];
    anti[i] = g.cellAt(-dirs[3 * i], -dirs[3 * i + 1], -dirs[3 * i + 2]);
    rnd[i] = (r() * g.n) | 0;
  }
  // a smooth path over the sphere (a unit walking, a camera ray sweeping): hint = previous answer
  const path = new Float64Array(3 * N);
  { let x = 0.3, y = 0.5, z = -0.8;
    for (let i = 0; i < N; i++) {
      x += (r() - 0.5) * 0.012; y += (r() - 0.5) * 0.012; z += (r() - 0.5) * 0.012;
      const inv = 1 / Math.sqrt(x * x + y * y + z * z); x *= inv; y *= inv; z *= inv;
      path[3 * i] = x; path[3 * i + 1] = y; path[3 * i + 2] = z;
    } }
  const time = fn => { let best = Infinity; for (let rep = 0; rep < 5; rep++) { const t = nowMs(); fn(); best = Math.min(best, nowMs() - t); } return best * 1000 / N; };
  let sink = 0;
  const cold = time(() => { for (let i = 0; i < N; i++) sink += g.cellAt(dirs[3 * i], dirs[3 * i + 1], dirs[3 * i + 2]); });
  const exact = time(() => { for (let i = 0; i < N; i++) sink += g.cellAt(dirs[3 * i], dirs[3 * i + 1], dirs[3 * i + 2], ans[i]); });
  const near = time(() => { for (let i = 0; i < N; i++) sink += g.cellAt(dirs[3 * i], dirs[3 * i + 1], dirs[3 * i + 2], nbHint[i]); });
  const walk = time(() => { let h = -1; for (let i = 0; i < N; i++) { h = g.cellAt(path[3 * i], path[3 * i + 1], path[3 * i + 2], h); sink += h; } });
  const far = time(() => { for (let i = 0; i < N; i++) sink += g.cellAt(dirs[3 * i], dirs[3 * i + 1], dirs[3 * i + 2], anti[i]); });
  const random = time(() => { for (let i = 0; i < N; i++) sink += g.cellAt(dirs[3 * i], dirs[3 * i + 1], dirs[3 * i + 2], rnd[i]); });
  T.ok(sink > 0);
  say('build(6): best ' + buildBest.toFixed(1) + ' ms, worst ' + buildWorst.toFixed(1) + ' ms of 4 (target < 400 ms)');
  say('cellAt at L=6, us per call: no hint ' + cold.toFixed(3) + ' (target < 10); hint = exact cell ' + exact.toFixed(3) +
    ', neighbour ' + near.toFixed(3) + ', previous cell on a smooth path ' + walk.toFixed(3) + ' (target < 2); antipodal hint ' +
    far.toFixed(3) + ', random hint ' + random.toFixed(3));
  T.ok(buildBest < 400, 'build(6) took ' + buildBest.toFixed(1) + ' ms');
  T.ok(cold < 10, 'cellAt without hint: ' + cold.toFixed(3) + ' us');
  T.ok(exact < 2 && near < 2 && walk < 2, 'cellAt with a good hint: ' + exact.toFixed(3) + ' / ' + near.toFixed(3) + ' / ' + walk.toFixed(3) + ' us');
  T.ok(far < 10 && random < 10, 'cellAt with a useless hint must not be slower than the no-hint target: ' + far.toFixed(3) + ' / ' + random.toFixed(3) + ' us');
});

T.test('adv: within costs time proportional to the result, not to the planet', () => {
  const g = G(6), out = new Int32Array(g.n);
  const rows = [];
  for (const ang of [3 * g.step, 0.1, 0.3, 1, 2, PI]) {
    const calls = Math.max(3, Math.min(4000, Math.round(40000 / (1 + 6 * ang * ang * 2600))));
    let best = Infinity, cells = 0;
    for (let rep = 0; rep < 5; rep++) {
      const t = nowMs();
      cells = 0;
      for (let i = 0; i < calls; i++) cells += g.within((i * 7919 + 13) % g.n, ang, out);
      best = Math.min(best, nowMs() - t);
    }
    rows.push({ ang, per: best * 1e6 / cells, size: cells / calls });
  }
  say('within at L=6, ns per result cell: ' + rows.map(x => x.per.toFixed(1) + ' (' + Math.round(x.size) + ' cells)').join(', '));
  const small = rows[1].per;
  for (const row of rows) T.ok(row.per < 1000, 'within(' + row.ang + ') costs ' + row.per.toFixed(0) + ' ns per result cell');
  for (const row of rows.slice(2)) {
    T.ok(row.per < 4 * small + 25, 'cost per result cell grows with the radius: ' + row.per.toFixed(1) + ' ns at ' + row.ang + ' rad vs ' + small.toFixed(1) + ' ns at 0.1 rad');
  }
  // the same small query on a 4x and a 16x bigger planet must cost about the same
  const per = [];
  for (const L of [5, 6, 7]) {
    const gg = G(L), o = new Int32Array(64);
    let best = Infinity, cells = 0;
    for (let rep = 0; rep < 5; rep++) {
      const t = nowMs();
      cells = 0;
      for (let i = 0; i < 20000; i++) { cells += gg.within((i * 7919 + 13) % gg.n, 2.1 * gg.step, o); cells += gg.ring((i * 104729 + 7) % gg.n, 2, o); }
      best = Math.min(best, nowMs() - t);
    }
    T.ok(cells > 40000 * 15 && cells < 40000 * 22, 'about 19 cells per query at L=' + L);
    per.push(best / 40);
  }
  say('20-cell within/ring query, us per call at L=5 / 6 / 7: ' + per.map(x => x.toFixed(3)).join(' / '));
  T.ok(per[2] < 6 * per[0] + 1, 'small queries got slower on a bigger planet: ' + per.map(x => x.toFixed(3)).join(' / ') + ' us');
  T.ok(per[2] < 12, 'a 20-cell query at L=7 takes ' + per[2].toFixed(3) + ' us');
});
