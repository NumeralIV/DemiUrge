// DEMIURGE — js/core/geo.js : the planet grid, an icosphere whose vertices are the cells.
//
// Exposes (FOUNDATION.md §4):
//   DM.Geo.build(L) → geo      build subdivision level L (0..7) from scratch
//   DM.Geo.get(L)   → geo      cached instance (built on first use)
//
//   geo.L, geo.n (= 10·4^L + 2), geo.nTri (= 20·4^L)
//   geo.pos   Float32Array(3n)   unit direction of each cell
//   geo.lat   Float32Array(n)    latitude  = asin(y), radians, always within [-π/2, π/2]
//   geo.lon   Float32Array(n)    longitude = atan2(z, x), radians, always within [-π, π]
//   geo.nbr   Int32Array(6n)     neighbours, counter-clockwise seen from outside; −1 in the unused
//                                6th slot of the 12 pentagon cells (ids 0–11)
//   geo.nbrN  Uint8Array(n)      5 or 6
//   geo.tris  Uint32Array(3·nTri) triangles, counter-clockwise seen from outside
//   geo.step  number             mean angle between neighbouring cells (radians)
//   geo.prefix(k) → 10·4^k + 2   number of cells of level k (they are the first cells of finer levels)
//   geo.cellAt(x, y, z, hint = -1) → nearest cell (exact)
//   geo.angle(a, b) → radians    geo.within(c, ang, out = []) → count    geo.ring(c, steps, out = []) → count
//   geo.dir(c, out) → out        geo.basis(c, outEast, outNorth)
//
// Guaranteed extras (tested): neighbour slot j of a cell c of level k and slot j of the same cell at
// level k+1 point along the same edge (the finer neighbour is that edge's midpoint).
// All methods are closures: they do not use `this` and may be detached (const cellAt = geo.cellAt).
(function (DM) {
  'use strict';

  const PI = Math.PI;

  // Level 0: icosahedron with vertex 0 at the north pole, 1–5 on the upper ring (longitudes 0°, 72°, …),
  // 6–10 on the lower ring (36°, 108°, …) and 11 at the south pole. Closed forms with sqrt only, so
  // positions are bit-identical in every JS engine (sin/cos are not correctly rounded everywhere).
  const S5 = Math.sqrt(5);
  const CH = 2 / S5, SH = 1 / S5;                               // cos, sin of the ring latitude atan(1/2)
  const C72 = (S5 - 1) / 4, S72 = Math.sqrt(10 + 2 * S5) / 4;
  const C36 = (S5 + 1) / 4, S36 = Math.sqrt(10 - 2 * S5) / 4;
  const UP_COS = [1, C72, -C36, -C36, C72], UP_SIN = [0, S72, S36, -S36, -S72];
  const LO_COS = [C36, -C72, -1, -C72, C36], LO_SIN = [S36, S72, 0, -S72, -S36];

  // With +Y up and lon = atan2(z, x), increasing longitude runs clockwise seen from above the north
  // pole, so the counter-clockwise (outward-facing) triangles are these:
  const ICO_TRIS = [];
  for (let k = 0; k < 5; k++) ICO_TRIS.push(0, 1 + (k + 1) % 5, 1 + k);              // north cap
  for (let k = 0; k < 5; k++) ICO_TRIS.push(1 + k, 1 + (k + 1) % 5, 6 + k);          // band, pointing down
  for (let k = 0; k < 5; k++) ICO_TRIS.push(6 + k, 1 + (k + 1) % 5, 6 + (k + 1) % 5); // band, pointing up
  for (let k = 0; k < 5; k++) ICO_TRIS.push(6 + k, 6 + (k + 1) % 5, 11);             // south cap

  // Largest float32 that is <= x (x > 0). Used so the stored lat/lon never exceed π/2 and π when read
  // back as doubles (Math.fround(Math.PI / 2) > Math.PI / 2, which would make cos(lat) negative).
  function f32Below(x) {
    const f = new Float32Array(1), u = new Uint32Array(f.buffer);
    f[0] = x;
    if (f[0] > x) u[0]--;
    return f[0];
  }
  const LAT_MAX = f32Below(PI / 2), LON_MAX = f32Below(PI);

  // Dot products of float32-rounded unit vectors differ from the true cosine by at most ~1.2e-7;
  // `within` settles anything closer than this to its threshold with the exact angle formula.
  const BAND = 1e-6;

  // Exact powers of two that bring absurdly long or short cellAt inputs into a safe range.
  const P2_UP = Math.pow(2, 300), P2_DOWN = Math.pow(2, -300);

  function prefix(k) { return 10 * Math.pow(4, k) + 2; }

  function checkLevel(L) {
    if (!Number.isInteger(L) || L < 0 || L > 7) {
      throw new RangeError('DM.Geo: L must be an integer in 0..7, got ' + L);
    }
  }

  function build(L) {
    checkLevel(L);
    const n = prefix(L);
    const nTri = 20 * Math.pow(4, L);

    // ---- 1. vertices (float64 while building) and triangles -------------------------------------
    const p = new Float64Array(3 * n);
    p[1] = 1;
    p[3 * 11 + 1] = -1;
    for (let k = 0; k < 5; k++) {
      let o = 3 * (1 + k);
      p[o] = CH * UP_COS[k]; p[o + 1] = SH; p[o + 2] = CH * UP_SIN[k];
      o = 3 * (6 + k);
      p[o] = CH * LO_COS[k]; p[o + 1] = -SH; p[o + 2] = CH * LO_SIN[k];
    }

    let tris = Uint32Array.from(ICO_TRIS);
    let nt = 20, nv = 12;
    if (L > 0) {
      // Edge-midpoint cache keyed by the vertex pair: each edge is filed under its lower vertex id.
      // A vertex has at most 6 edges, so 6 slots per vertex are enough.
      const nOld = prefix(L - 1);
      const eCnt = new Uint8Array(nOld);
      const eHi = new Int32Array(6 * nOld), eMid = new Int32Array(6 * nOld);
      const mid = function (a, b) {
        const lo = a < b ? a : b, hi = a < b ? b : a;
        const o = 6 * lo, k = eCnt[lo];
        for (let j = 0; j < k; j++) if (eHi[o + j] === hi) return eMid[o + j];
        const id = nv++;
        eHi[o + k] = hi; eMid[o + k] = id; eCnt[lo] = k + 1;
        const ia = 3 * a, ib = 3 * b, io = 3 * id;
        const x = p[ia] + p[ib], y = p[ia + 1] + p[ib + 1], z = p[ia + 2] + p[ib + 2];
        const inv = 1 / Math.sqrt(x * x + y * y + z * z);
        p[io] = x * inv; p[io + 1] = y * inv; p[io + 2] = z * inv;
        return id;
      };
      for (let lev = 0; lev < L; lev++) {
        eCnt.fill(0, 0, nv);
        const next = new Uint32Array(nt * 12);
        for (let t = 0, w = 0; t < nt; t++) {
          const a = tris[3 * t], b = tris[3 * t + 1], c = tris[3 * t + 2];
          const ab = mid(a, b), bc = mid(b, c), ca = mid(c, a);
          next[w++] = a; next[w++] = ab; next[w++] = ca;
          next[w++] = b; next[w++] = bc; next[w++] = ab;
          next[w++] = c; next[w++] = ca; next[w++] = bc;
          next[w++] = ab; next[w++] = bc; next[w++] = ca;
        }
        tris = next;
        nt *= 4;
      }
    }
    if (nv !== n || nt !== nTri) throw new Error('DM.Geo.build: internal error (vertex/triangle count)');

    const pos = new Float32Array(p);
    const lat = new Float32Array(n), lon = new Float32Array(n);
    for (let i = 0; i < n; i++) {
      const x = p[3 * i], y = p[3 * i + 1], z = p[3 * i + 2];
      let la = Math.fround(Math.atan2(y, Math.sqrt(x * x + z * z)));   // = asin(y), well-conditioned at the poles
      let lo = Math.fround(Math.atan2(z, x));
      if (la > LAT_MAX) la = LAT_MAX; else if (la < -LAT_MAX) la = -LAT_MAX;
      if (lo > LON_MAX) lo = LON_MAX; else if (lo < -LON_MAX) lo = -LON_MAX;
      lat[i] = la; lon[i] = lo;
    }

    // ---- 2. neighbours, counter-clockwise seen from outside --------------------------------------
    // In a CCW triangle (a, b, c) the neighbour c follows the neighbour b around a. Collect those
    // "b → c" links per vertex, then walk the cycle starting from the first link recorded.
    const nbr = new Int32Array(6 * n).fill(-1);
    const nbrN = new Uint8Array(n);
    {
      const from = new Int32Array(6 * n), to = new Int32Array(6 * n);
      const cnt = nbrN;                                   // reused: ends up as the neighbour count
      for (let t = 0; t < nTri; t++) {
        const a = tris[3 * t], b = tris[3 * t + 1], c = tris[3 * t + 2];
        let o = 6 * a + cnt[a]++; from[o] = b; to[o] = c;
        o = 6 * b + cnt[b]++; from[o] = c; to[o] = a;
        o = 6 * c + cnt[c]++; from[o] = a; to[o] = b;
      }
      for (let v = 0; v < n; v++) {
        const o = 6 * v, k = cnt[v];
        let cur = from[o];
        for (let i = 0; i < k; i++) {
          nbr[o + i] = cur;
          for (let j = 0; j < k; j++) if (from[o + j] === cur) { cur = to[o + j]; break; }
        }
      }
    }

    // ---- 3. mean neighbour angle ------------------------------------------------------------------
    let step = 0;
    {
      let sum = 0, edges = 0;
      for (let v = 0; v < n; v++) {
        const x = p[3 * v], y = p[3 * v + 1], z = p[3 * v + 2];
        for (let j = 0; j < 6; j++) {
          const m = nbr[6 * v + j];
          if (m <= v) continue;                           // each edge once; also skips the −1 slot
          const d = x * p[3 * m] + y * p[3 * m + 1] + z * p[3 * m + 2];
          sum += Math.acos(d < 1 ? d : 1);
          edges++;
        }
      }
      step = sum / edges;
    }

    // ---- 4. nearest-cell search ---------------------------------------------------------------------
    // The icosphere is a Delaunay triangulation (it is the convex hull of its vertices), so walking to
    // the neighbour with the largest dot product until none is larger always ends at the true nearest
    // cell, from any start. The start comes from the caller's hint or from a cube-face lookup table.
    function descend(x, y, z, c) {
      let o = 3 * c;
      let best = x * pos[o] + y * pos[o + 1] + z * pos[o + 2];
      for (;;) {
        const b = 6 * c;
        let go = c;
        for (let j = 0; j < 6; j++) {
          const m = nbr[b + j];
          if (m < 0) break;
          o = 3 * m;
          const d = x * pos[o] + y * pos[o + 1] + z * pos[o + 2];
          if (d > best) { best = d; go = m; }
        }
        if (go === c) return c;
        c = go;
      }
    }

    const G = 1 << L;                                     // buckets per cube-face side (about 1.6 cells wide)
    const table = new Int32Array(6 * G * G);
    {
      let c = 0, idx = 0;
      for (let f = 0; f < 6; f++) {
        const s = (f & 1) ? -1 : 1;
        for (let j = 0; j < G; j++) {
          const v = (j + 0.5) / G * 2 - 1;
          for (let i = 0; i < G; i++, idx++) {
            const u = (i + 0.5) / G * 2 - 1;
            if (i === 0 && j > 0) c = table[idx - G];     // start each row from the bucket above
            if (f < 2) c = descend(s, u, v, c);
            else if (f < 4) c = descend(u, s, v, c);
            else c = descend(u, v, s, c);
            table[idx] = c;
          }
        }
      }
    }

    function lookup(x, y, z) {
      const ax = x < 0 ? -x : x, ay = y < 0 ? -y : y, az = z < 0 ? -z : z;
      let f, u, v;
      if (ax >= ay && ax >= az) { f = x > 0 ? 0 : 1; u = y / ax; v = z / ax; }
      else if (ay >= az) { f = y > 0 ? 2 : 3; u = x / ay; v = z / ay; }
      else { f = z > 0 ? 4 : 5; u = x / az; v = y / az; }
      let i = ((u + 1) * 0.5 * G) | 0, j = ((v + 1) * 0.5 * G) | 0;   // NaN (zero / non-finite input) → 0
      if (i >= G) i = G - 1; else if (i < 0) i = 0;
      if (j >= G) j = G - 1; else if (j < 0) j = 0;
      return table[(f * G + j) * G + i];
    }

    // A hint is used only if it is roughly within 3 cells of the target; a stale, far-away hint would
    // cost a long walk, so the lookup table takes over in that case. The result is exact either way.
    const hintAng = Math.min(3 * step, 1.2);
    const hintCos2 = Math.cos(hintAng) * Math.cos(hintAng);

    // Vectors shorter than about 1e-150 or longer than about 1e150 make the products in cellAt
    // underflow or overflow, which would break the comparison of dot products. rescale() multiplies
    // such a vector by an exact power of two (the direction is unchanged, bit for bit) and leaves the
    // result in `scaled`. Non-finite vectors are passed through untouched.
    const scaled = new Float64Array(3);
    function rescale(x, y, z) {
      const ax = Math.abs(x), ay = Math.abs(y), az = Math.abs(z);
      let big = ax > ay ? ax : ay;
      if (az > big) big = az;
      if (big > 0 && big < Infinity) {
        while (big >= 1e100) { x *= P2_DOWN; y *= P2_DOWN; z *= P2_DOWN; big *= P2_DOWN; }
        while (big <= 1e-100) { x *= P2_UP; y *= P2_UP; z *= P2_UP; big *= P2_UP; }
      }
      scaled[0] = x; scaled[1] = y; scaled[2] = z;
    }

    function cellAt(x, y, z, hint) {
      let len2 = x * x + y * y + z * z;
      // Out-of-range length (never true for ordinary input; zero and NaN vectors are not rescaled and
      // end up at an arbitrary but valid cell).
      if (len2 < 1e-200 ? (x !== 0 || y !== 0 || z !== 0) : len2 > 1e200) {
        rescale(x, y, z);
        x = scaled[0]; y = scaled[1]; z = scaled[2];
        len2 = x * x + y * y + z * z;
      }
      if (hint >= 0 && hint < n) {
        const h = hint | 0, o = 3 * h;
        const d = x * pos[o] + y * pos[o + 1] + z * pos[o + 2];
        if (d > 0 && d * d >= hintCos2 * len2) return descend(x, y, z, h);
      }
      return descend(x, y, z, lookup(x, y, z));
    }

    // ---- 5. angles and neighbourhood queries --------------------------------------------------------
    // atan2(|a × b|, a · b): accurate for tiny angles and exactly 0 for a === b, unlike acos(dot).
    function angle(a, b) {
      const ia = 3 * a, ib = 3 * b;
      const ax = pos[ia], ay = pos[ia + 1], az = pos[ia + 2];
      const bx = pos[ib], by = pos[ib + 1], bz = pos[ib + 2];
      const cx = ay * bz - az * by, cy = az * bx - ax * bz, cz = ax * by - ay * bx;
      return Math.atan2(Math.sqrt(cx * cx + cy * cy + cz * cz), ax * bx + ay * by + az * bz);
    }

    // Scratch shared by within() and ring(). Neither calls back into user code, so one call always
    // finishes before the next begins; results live in the caller's `out`, which keeps nested use safe.
    const mark = new Int32Array(n);
    const queue = new Int32Array(n);
    let stampCtr = 0;
    function nextStamp() {
      if (stampCtr === 0x7fffffff) { mark.fill(0); stampCtr = 0; }
      return ++stampCtr;
    }

    // Flood fill outward from c. Every cell inside a spherical cap centred on a cell has a neighbour
    // that is closer to the centre (Delaunay property), so the cap is connected and the fill finds all
    // of it while touching only the result and its one-cell rim.
    function within(c, ang, out) {
      if (out === undefined) out = [];
      if (!(c >= 0 && c < n)) return 0;
      c |= 0;
      const stamp = nextStamp();
      mark[c] = stamp; queue[0] = c; out[0] = c;
      let count = 1;
      if (!(ang > 0)) return 1;
      const all = ang >= PI;
      const cosA = all ? -2 : Math.cos(ang);
      const lo = cosA - BAND, hi = cosA + BAND;
      const cx = pos[3 * c], cy = pos[3 * c + 1], cz = pos[3 * c + 2];
      for (let head = 0; head < count; head++) {
        const b = 6 * queue[head];
        for (let j = 0; j < 6; j++) {
          const m = nbr[b + j];
          if (m < 0 || mark[m] === stamp) continue;
          mark[m] = stamp;
          if (!all) {
            const d = cx * pos[3 * m] + cy * pos[3 * m + 1] + cz * pos[3 * m + 2];
            if (d < lo) continue;
            if (d < hi && angle(c, m) > ang) continue;    // borderline: decide with the exact angle
          }
          queue[count] = m; out[count] = m; count++;
        }
      }
      return count;
    }

    function ring(c, steps, out) {
      if (out === undefined) out = [];
      if (!(c >= 0 && c < n)) return 0;
      c |= 0;
      const stamp = nextStamp();
      mark[c] = stamp; queue[0] = c; out[0] = c;
      let count = 1, head = 0;
      // Hop s is added only while s <= steps, so a fractional `steps` counts as its floor (cells one
      // hop away are not "within 0.5 hops") and NaN / negative values give the cell alone.
      for (let s = 1; s <= steps && head < count; s++) {
        for (const end = count; head < end; head++) {
          const b = 6 * queue[head];
          for (let j = 0; j < 6; j++) {
            const m = nbr[b + j];
            if (m < 0 || mark[m] === stamp) continue;
            mark[m] = stamp;
            queue[count] = m; out[count] = m; count++;
          }
        }
      }
      return count;
    }

    function dir(c, out) {
      const o = 3 * c;
      out[0] = pos[o]; out[1] = pos[o + 1]; out[2] = pos[o + 2];
      return out;
    }

    // east = normalize(cross(Y, n)), north = cross(n, east); east = +X at the two poles (as V3.basis).
    function basis(c, outEast, outNorth) {
      const o = 3 * c;
      let x = pos[o], y = pos[o + 1], z = pos[o + 2];
      const inv = 1 / Math.sqrt(x * x + y * y + z * z);
      x *= inv; y *= inv; z *= inv;
      const h = Math.sqrt(x * x + z * z);
      if (h < 1e-9) {
        outEast[0] = 1; outEast[1] = 0; outEast[2] = 0;
        outNorth[0] = 0; outNorth[1] = 0; outNorth[2] = y > 0 ? -1 : 1;
      } else {
        outEast[0] = z / h; outEast[1] = 0; outEast[2] = -x / h;
        outNorth[0] = -x * y / h; outNorth[1] = h; outNorth[2] = -y * z / h;
      }
    }

    return {
      L, n, nTri, pos, lat, lon, nbr, nbrN, tris, step,
      prefix, cellAt, angle, within, ring, dir, basis,
    };
  }

  const cache = [];
  function get(L) {
    checkLevel(L);
    return cache[L] || (cache[L] = build(L));
  }

  DM.Geo = { build, get };
})(globalThis.DM = globalThis.DM || {});
