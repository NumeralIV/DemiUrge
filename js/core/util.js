// DEMIURGE — js/core/util.js : DM.R and DM.U (FOUNDATION §1). Runs unchanged in Node and browsers.
//
//   DM.R = 100                         planet datum radius
//   DM.U.TAU
//   DM.U.clamp sat lerp smooth sstep remap approach damp          scalar helpers
//   DM.U.hash32(a) hash2(a, b) → uint32    hashf(a, b = 0) → [0,1)   integer hashes
//   DM.U.rng(seed) → Rng               mulberry32; the whole state is the single uint32 `rng.s`
//       rng.next() f() range(a, b) int(a, b) chance(p) pick(arr) weighted(arr, wfn) gauss()
//       shuffle(arr) fork(tag)
//   DM.U.Rng                           the class behind rng() (for instanceof checks)
//   DM.U.noise3(x, y, z) → [-1,1]      smooth 3D gradient noise (BCC lattice), seedless, C3-continuous,
//                                      features about 1 unit across, standard deviation ≈ 0.35
//   DM.U.fbm3(x, y, z, oct, lac = 2, gain = 0.5)    → [-1,1]
//   DM.U.ridged3(x, y, z, oct, lac = 2, gain = 0.5) → [0,1]
//   DM.U.b64enc(Uint8Array) → string   DM.U.b64dec(string) → Uint8Array   (no Buffer/atob/btoa)
//   DM.U.fmtInt(n) → "12,345"          DM.U.fmtBig(n) → "950", "12.3k", "4.56M", "1.2B"
//
// Everything here is deterministic, and everything except damp() is bit-identical on every JS
// engine: the hashes, every Rng method (gauss included) and noise3/fbm3/ridged3 use only integer
// ops, IEEE + - * /, Math.floor and Math.sqrt (which IEEE 754 requires to be correctly rounded).
// Engines do NOT agree on Math.log / exp / sin / cos / tan / atan2 / acos: measured, Node 24 and
// Edge 157 differ in the last bit for 3–23 % of arguments. That is why gauss() carries its own ln
// and cos. damp() calls Math.exp and is the one exception (it is meant for visual smoothing).
(function (DM) {
  'use strict';

  DM.R = 100;
  const U = DM.U = DM.U || {};
  const imul = Math.imul;
  const TWO32 = 4294967296;

  U.TAU = Math.PI * 2;

  // ---------------------------------------------------------------- scalar helpers

  U.clamp = function (x, a, b) { return x < a ? a : x > b ? b : x; };
  U.sat = function (x) { return x < 0 ? 0 : x > 1 ? 1 : x; };
  U.lerp = function (a, b, t) { return a + (b - a) * t; };

  // Smoothstep of an input already in 0..1 (clamped).
  U.smooth = function (t) {
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    return t * t * (3 - 2 * t);
  };

  // GLSL smoothstep(e0, e1, x). A zero-width edge behaves as a step; e0 > e1 gives the mirrored ramp.
  U.sstep = function (e0, e1, x) {
    if (e0 === e1) return x < e0 ? 0 : 1;
    let t = (x - e0) / (e1 - e0);
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    return t * t * (3 - 2 * t);
  };

  // Map x from a..b to c..d linearly, clamped to c..d. Works with a > b and/or c > d.
  U.remap = function (x, a, b, c, d) {
    if (a === b) return x < a ? c : d;
    let t = (x - a) / (b - a);
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    return c + (d - c) * t;
  };

  // Move x toward target by at most maxDelta (never overshoots).
  U.approach = function (x, target, maxDelta) {
    if (x < target) { x += maxDelta; return x > target ? target : x; }
    x -= maxDelta;
    return x < target ? target : x;
  };

  // Frame-rate independent exponential approach: the remaining distance shrinks by e^(-lambda·dt).
  U.damp = function (x, target, lambda, dt) {
    return target + (x - target) * Math.exp(-lambda * dt);
  };

  // ---------------------------------------------------------------- integer hashes

  // Bijective 32-bit mixer ("lowbias32" finalizer with a pre-added constant so that 0 does not map to 0).
  function hash32(a) {
    a = (a + 0x9E3779B9) | 0;
    a ^= a >>> 16; a = imul(a, 0x7FEB352D);
    a ^= a >>> 15; a = imul(a, 0x846CA68B);
    a ^= a >>> 16;
    return a >>> 0;
  }

  // Hash of an ordered pair of ints. For a fixed a it is a bijection of b and vice versa.
  function hash2(a, b) {
    return hash32(hash32(a) ^ imul(b, 0x9E3779B1));
  }

  U.hash32 = hash32;
  U.hash2 = hash2;
  U.hashf = function (a, b) { return hash2(a, b === undefined ? 0 : b) / TWO32; };

  // 32-bit FNV-1a over the UTF-16 code units of a string (used for string fork tags).
  function hashStr(s) {
    let h = 0x811C9DC5 | 0;
    for (let i = 0; i < s.length; i++) h = imul(h ^ s.charCodeAt(i), 0x01000193);
    return h;
  }

  // ---------------------------------------------------------------- Rng (mulberry32)

  // Engine-independent ln and cos for gauss(). Only exact integer steps and + - * / are used, so
  // every engine computes the same bits (Math.log and Math.cos differ between engines in the
  // last place). Both are accurate to about 1e-16.

  // ln(k / 2^32) for an integer 1 ≤ k < 2^32; the result is in [-22.2, 0).
  // k = m · 2^(31−z) with m in [1,2); m is then halved if above √2, so that ln m = 2·atanh(s)
  // with s = (m−1)/(m+1), |s| < 0.1716, where 11 terms of the series reach double precision.
  const LN2_HI = 0.6931471803691238, LN2_LO = 1.9082149292705877e-10;     // sum = ln 2; HI has 32 significant bits
  const A3 = 1 / 3, A5 = 1 / 5, A7 = 1 / 7, A9 = 1 / 9, A11 = 1 / 11, A13 = 1 / 13, A15 = 1 / 15,
    A17 = 1 / 17, A19 = 1 / 19, A21 = 1 / 21;
  function lnUnit(k) {
    const z = Math.clz32(k);
    let m = ((k << z) >>> 0) / 2147483648;
    let e = -1 - z;
    if (m > 1.4142135623730951) { m *= 0.5; e++; }
    const s = (m - 1) / (m + 1), w = s * s;
    const p = (((((((((A21 * w + A19) * w + A17) * w + A15) * w + A13) * w + A11) * w + A9) * w + A7) * w + A5) * w + A3) * w + 1;
    return e * LN2_HI + (2 * s * p + e * LN2_LO);
  }

  // cos(2π · j / 2^32) for an integer 0 ≤ j < 2^32. The top two bits of j select the quadrant
  // and the rest is folded into [0, π/4], where the Taylor series of sin and cos need 9 and 10
  // terms. Exactly 1, 0, −1, 0 at the quarter turns.
  const QUARTER = Math.PI / 2 / 1073741824;       // radians per unit of the 30-bit in-quadrant position
  const S3 = -1 / 6, S5 = 1 / 120, S7 = -1 / 5040, S9 = 1 / 362880, S11 = -1 / 39916800, S13 = 1 / 6227020800,
    S15 = -1 / 1307674368000, S17 = 1 / 355687428096000;
  const C2 = -1 / 2, C4 = 1 / 24, C6 = -1 / 720, C8 = 1 / 40320, C10 = -1 / 3628800, C12 = 1 / 479001600,
    C14 = -1 / 87178291200, C16 = 1 / 20922789888000, C18 = -1 / 6402373705728000;
  function cosTurn(j) {
    const q = j >>> 30;
    let r = j & 0x3FFFFFFF, useSin = q & 1;
    if (r > 0x20000000) { r = 0x40000000 - r; useSin ^= 1; }      // cos(x) = sin(π/2 − x)
    const a = r * QUARTER, w = a * a;
    const v = useSin
      ? a * ((((((((S17 * w + S15) * w + S13) * w + S11) * w + S9) * w + S7) * w + S5) * w + S3) * w + 1)
      : ((((((((C18 * w + C16) * w + C14) * w + C12) * w + C10) * w + C8) * w + C6) * w + C4) * w + C2) * w + 1;
    return ((q + 1) & 2) ? -v : v;                                 // negative in quadrants 1 and 2
  }

  // Scratch stack for weighted(): weights of the call in progress live at WBUF[base … base+n).
  // A stack (rather than one flat buffer) keeps weighted() re-entrant if wfn itself calls weighted().
  let WBUF = new Float64Array(256);
  let wtop = 0;

  class Rng {
    constructor(seed) { this.s = seed >>> 0; }

    // Next uint32. The state is a Weyl counter; the output is a mix of it (period 2^32).
    next() {
      let t = this.s = (this.s + 0x6D2B79F5) >>> 0;
      t = imul(t ^ (t >>> 15), t | 1);
      t ^= t + imul(t ^ (t >>> 7), t | 61);
      return (t ^ (t >>> 14)) >>> 0;
    }

    // Float in [0,1) with 32 bits of resolution.
    f() { return this.next() / TWO32; }

    // Float in [a,b).
    range(a, b) {
      const v = a + (b - a) * this.f();
      return (v >= b && b > a) ? a : v;      // guards the rare round-up onto b
    }

    // Integer in [a,b], both ends inclusive (arguments may come in either order).
    int(a, b) {
      if (b < a) { const t = a; a = b; b = t; }
      return a + Math.floor(this.f() * (b - a + 1));
    }

    // True with probability p. Always consumes exactly one draw.
    chance(p) { return this.f() < p; }

    // Uniform element; undefined (and no draw) for an empty array.
    pick(arr) {
      const n = arr.length;
      return n ? arr[Math.floor(this.f() * n)] : undefined;
    }

    // Element chosen with probability proportional to wfn(el). Weights ≤ 0 or NaN count as 0 and
    // are never chosen. Returns undefined (and consumes no draw) if no weight is positive;
    // otherwise consumes exactly one draw. wfn is called once per element.
    weighted(arr, wfn) {
      const n = arr.length, base = wtop;
      if (base + n > WBUF.length) {
        const grown = new Float64Array(Math.max(WBUF.length * 2, base + n));
        grown.set(WBUF);
        WBUF = grown;
      }
      wtop = base + n;
      try {
        let total = 0;
        for (let i = 0; i < n; i++) {
          let w = +wfn(arr[i]);
          if (!(w > 0)) w = 0;
          WBUF[base + i] = w;               // WBUF re-read here: a nested call may have grown it
          total += w;
        }
        if (!(total > 0)) return undefined;
        if (total === Infinity) {           // infinite weights: choose uniformly among them
          total = 0;
          for (let i = 0; i < n; i++) total += (WBUF[base + i] = WBUF[base + i] === Infinity ? 1 : 0);
        }
        let r = this.f() * total, last = -1;
        for (let i = 0; i < n; i++) {
          const w = WBUF[base + i];
          if (w > 0) {
            last = i;
            if ((r -= w) < 0) return arr[i];
          }
        }
        return arr[last];                   // float round-off fell off the end
      } finally {
        wtop = base;
      }
    }

    // Standard normal (Box–Muller): sqrt(−2 ln u) · cos(2π v) with u = 1 − next()/2^32 in (0,1]
    // and v = next()/2^32. Consumes exactly two draws; nothing is cached, so the stream is still
    // fully described by `s`. Bit-identical on every engine (see lnUnit / cosTurn above).
    // |result| ≤ sqrt(64 ln 2) ≈ 6.66.
    gauss() {
      const n = this.next(), j = this.next();
      if (n === 0) return 0;                // u = 1: radius 0
      return Math.sqrt(-2 * lnUnit(4294967296 - n)) * cosTurn(j);
    }

    // Fisher–Yates, in place. Works on arrays and typed arrays.
    shuffle(arr) {
      for (let i = arr.length - 1; i > 0; i--) {
        const j = Math.floor(this.f() * (i + 1));
        const t = arr[i]; arr[i] = arr[j]; arr[j] = t;
      }
      return arr;
    }

    // Independent stream derived from (current state, tag). Does not advance this stream.
    // tag: int (coerced with | 0) or string. The same state and tag always give the same child.
    fork(tag) {
      const t = typeof tag === 'string' ? hashStr(tag) ^ 0x5F356495 : tag | 0;
      return new Rng(hash2(this.s ^ 0x2545F491, t));
    }
  }

  U.Rng = Rng;
  U.rng = function (seed) { return new Rng(seed); };

  // ---------------------------------------------------------------- gradient noise

  // noise3 is gradient noise on a body-centred cubic lattice with the radial kernel
  // (3/4 − d²)⁴ — the construction known as "SuperSimplex" / OpenSimplex2S, derived here from
  // scratch. Compared with classic 3D simplex noise (same lattice, but only the 4 corners of one
  // tetrahedron and a kernel of radius² 0.5–0.6) every lattice point within the kernel radius
  // contributes, so there are no low-amplitude holes between lattice points, no discontinuities,
  // and no directional streaks: it looks isotropic in any slice and on the sphere.
  //
  // The lattice is two interleaved unit cubic lattices: A = integer points, B = integer + ½.
  // The kernel radius √¾ equals the nearest-neighbour distance, which bounds the work: within a
  // unit cell, each sub-lattice can only contribute its nearest point, the three points one step
  // away from it along one axis, and the three two steps away along two axes (7 + 7 candidates;
  // 5.4 contribute on average).
  //
  // Lattice gradients come from a fixed integer hash of the lattice coordinates (not a
  // permutation table), so the pattern does not repeat for |coordinates| < 2^30 and is
  // bit-identical on every engine. 32 gradient directions: the vertices of an icosahedron and
  // a dodecahedron (antipodally symmetric, so the noise has zero mean, and nearly uniform).
  const NPX = 0x9E3779B1 | 0, NPY = 0x85EBCA77 | 0, NPZ = 0xC2B2AE3D | 0;
  const GRAD = new Float64Array(32 * 4);         // x, y, z, pad
  (function buildGradients() {
    const phi = (1 + Math.sqrt(5)) / 2, inv = 1 / phi;
    let n = 0;
    function add(x, y, z) {
      const l = Math.sqrt(x * x + y * y + z * z);
      GRAD[n] = x / l; GRAD[n + 1] = y / l; GRAD[n + 2] = z / l;
      n += 4;
    }
    for (let a = -1; a <= 1; a += 2) {
      for (let b = -1; b <= 1; b += 2) {
        add(0, a, b * phi); add(a, b * phi, 0); add(b * phi, 0, a);
        add(0, a * inv, b * phi); add(a * inv, b * phi, 0); add(b * phi, 0, a * inv);
        add(a, b, 1); add(a, b, -1);
      }
    }
  })();

  // 1 / (largest value the unscaled sum can reach for any assignment of the 32 gradients above
  // to lattice points: 0.0867726392…, at the midpoint between an A point and a neighbouring
  // B point), rounded down. So |noise3| ≤ 0.99997 always, and values above 0.99 do occur.
  const NOISE_SCALE = 11.524;

  // One lattice point's contribution: t = 3/4 − d² (> 0), h = xor of the three hashed
  // coordinates, (dx,dy,dz) = offset from the lattice point.
  function corner(t, h, dx, dy, dz) {
    h = imul(h ^ (h >>> 15), 0x2C1B3C6D);
    h = imul(h ^ (h >>> 12), 0x297A2D39);
    const g = (h >>> 25) & 124;
    t *= t;
    return t * t * (GRAD[g] * dx + GRAD[g + 1] * dy + GRAD[g + 2] * dz);
  }

  function noise3(x, y, z) {
    // Rotate the domain by 180° about (1,1,1) so the cubic axes of the lattice are not the
    // world axes (axis-aligned slices then show no lattice rows).
    const r = (x + y + z) * (2 / 3);
    const xr = r - x, yr = r - y, zr = r - z;
    const i = Math.floor(xr), j = Math.floor(yr), k = Math.floor(zr);
    const fx = xr - i, fy = yr - j, fz = zr - k;           // position in the unit cell, [0,1)

    // Per axis: h = nearest cell corner (0 or 1), s = step from it to the far corner (±1),
    // d = offset from the nearest corner, e = offset from the cell centre.
    const hx = (fx + 0.5) | 0, hy = (fy + 0.5) | 0, hz = (fz + 0.5) | 0;
    const sx = 1 - (hx << 1), sy = 1 - (hy << 1), sz = 1 - (hz << 1);
    const dx = fx - hx, dy = fy - hy, dz = fz - hz;
    const ex = fx - 0.5, ey = fy - 0.5, ez = fz - 0.5;
    // 2|d|. Stepping one lattice point along an axis changes t by 2|d| − 1 on lattice A and by
    // −2|d| on lattice B (where the offset is ½ − |d|).
    const ax = (dx < 0 ? -dx : dx) * 2, ay = (dy < 0 ? -dy : dy) * 2, az = (dz < 0 ? -dz : dz) * 2;

    // Hashed coordinates in half-units: A points are even, B points odd.
    const i2 = i << 1, j2 = j << 1, k2 = k << 1;
    let n = 0;

    // ---- lattice A: cell corners
    const X0 = imul(i2 + (hx << 1), NPX), X1 = imul(i2 + 2 - (hx << 1), NPX);
    const Y0 = imul(j2 + (hy << 1), NPY), Y1 = imul(j2 + 2 - (hy << 1), NPY);
    const Z0 = imul(k2 + (hz << 1), NPZ), Z1 = imul(k2 + 2 - (hz << 1), NPZ);
    const tA = 0.75 - dx * dx - dy * dy - dz * dz;
    if (tA > 0) n += corner(tA, X0 ^ Y0 ^ Z0, dx, dy, dz);
    const tAx = tA - 1 + ax, tAy = tA - 1 + ay, tAz = tA - 1 + az;
    if (tAx > 0) {
      n += corner(tAx, X1 ^ Y0 ^ Z0, dx - sx, dy, dz);
      if (tAy > 0) { const t = tAx - 1 + ay; if (t > 0) n += corner(t, X1 ^ Y1 ^ Z0, dx - sx, dy - sy, dz); }
      if (tAz > 0) { const t = tAx - 1 + az; if (t > 0) n += corner(t, X1 ^ Y0 ^ Z1, dx - sx, dy, dz - sz); }
    }
    if (tAy > 0) {
      n += corner(tAy, X0 ^ Y1 ^ Z0, dx, dy - sy, dz);
      if (tAz > 0) { const t = tAy - 1 + az; if (t > 0) n += corner(t, X0 ^ Y1 ^ Z1, dx, dy - sy, dz - sz); }
    }
    if (tAz > 0) n += corner(tAz, X0 ^ Y0 ^ Z1, dx, dy, dz - sz);

    // ---- lattice B: cell centres (the far centre along an axis lies on the side opposite to s)
    const BX0 = imul(i2 + 1, NPX), BX1 = imul(i2 + 1 - (sx << 1), NPX);
    const BY0 = imul(j2 + 1, NPY), BY1 = imul(j2 + 1 - (sy << 1), NPY);
    const BZ0 = imul(k2 + 1, NPZ), BZ1 = imul(k2 + 1 - (sz << 1), NPZ);
    const tB = 0.75 - ex * ex - ey * ey - ez * ez;
    if (tB > 0) n += corner(tB, BX0 ^ BY0 ^ BZ0, ex, ey, ez);
    const tBx = tB - ax, tBy = tB - ay, tBz = tB - az;
    if (tBx > 0) {
      n += corner(tBx, BX1 ^ BY0 ^ BZ0, ex + sx, ey, ez);
      if (tBy > 0) { const t = tBx - ay; if (t > 0) n += corner(t, BX1 ^ BY1 ^ BZ0, ex + sx, ey + sy, ez); }
      if (tBz > 0) { const t = tBx - az; if (t > 0) n += corner(t, BX1 ^ BY0 ^ BZ1, ex + sx, ey, ez + sz); }
    }
    if (tBy > 0) {
      n += corner(tBy, BX0 ^ BY1 ^ BZ0, ex, ey + sy, ez);
      if (tBz > 0) { const t = tBy - az; if (t > 0) n += corner(t, BX0 ^ BY1 ^ BZ1, ex, ey + sy, ez + sz); }
    }
    if (tBz > 0) n += corner(tBz, BX0 ^ BY0 ^ BZ1, ex, ey, ez + sz);

    return NOISE_SCALE * n;
  }
  U.noise3 = noise3;

  // Each octave samples a different region of the noise so that octaves never line up
  // (plain fbm is exactly 0 at the origin and at every point shared by all octave lattices).
  const OCT_X = 31.416, OCT_Y = -47.853, OCT_Z = 12.679;

  // Fractal sum of `oct` octaves, normalized by the amplitude sum, so the result stays in [-1,1].
  U.fbm3 = function (x, y, z, oct, lac, gain) {
    if (lac === undefined) lac = 2;
    if (gain === undefined) gain = 0.5;
    let sum = 0, norm = 0, amp = 1, f = 1;
    for (let o = 0; o < oct; o++) {
      sum += amp * noise3(x * f + o * OCT_X, y * f + o * OCT_Y, z * f + o * OCT_Z);
      norm += amp;
      amp *= gain;
      f *= lac;
    }
    return norm > 0 ? sum / norm : 0;
  };

  // Ridged multifractal in [0,1]: every octave is (1 - |noise|)², which is 1 with a sharp crease
  // along the zero set of the noise. Each octave is weighted by the previous one, so fine detail
  // gathers on the ridges and the valleys stay smooth.
  U.ridged3 = function (x, y, z, oct, lac, gain) {
    if (lac === undefined) lac = 2;
    if (gain === undefined) gain = 0.5;
    let sum = 0, norm = 0, amp = 1, f = 1, w = 1;
    for (let o = 0; o < oct; o++) {
      let v = noise3(x * f + o * OCT_X, y * f + o * OCT_Y, z * f + o * OCT_Z);
      v = 1 - (v < 0 ? -v : v);
      v *= v;
      sum += amp * w * v;
      norm += amp;
      w = v * 2;
      if (w > 1) w = 1;
      amp *= gain;
      f *= lac;
    }
    return norm > 0 ? sum / norm : 0;
  };

  // ---------------------------------------------------------------- base64

  const B64_CHARS = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
  const B64_ENC = new Uint8Array(64);
  const B64_DEC = new Int8Array(128).fill(-1);    // -1 invalid, -2 whitespace, -3 padding
  for (let i = 0; i < 64; i++) { B64_ENC[i] = B64_CHARS.charCodeAt(i); B64_DEC[B64_ENC[i]] = i; }
  B64_DEC[45] = 62; B64_DEC[95] = 63;             // also accept the URL-safe alphabet: - and _
  B64_DEC[9] = B64_DEC[10] = B64_DEC[13] = B64_DEC[32] = -2;
  B64_DEC[61] = -3;
  const B64_CHUNK = 8184;                         // characters per String.fromCharCode call

  // Standard base64 with padding. Accepts a Uint8Array; any other typed array / DataView /
  // ArrayBuffer is encoded as its raw bytes, a plain array as bytes (values & 255).
  U.b64enc = function (bytes) {
    if (!(bytes instanceof Uint8Array)) {
      if (ArrayBuffer.isView(bytes)) bytes = new Uint8Array(bytes.buffer, bytes.byteOffset, bytes.byteLength);
      else bytes = new Uint8Array(bytes);
    }
    const n = bytes.length, full = n - n % 3;
    const out = new Uint8Array(Math.ceil(n / 3) * 4);
    const E = B64_ENC;
    let o = 0, i = 0;
    for (; i < full; i += 3) {
      const v = (bytes[i] << 16) | (bytes[i + 1] << 8) | bytes[i + 2];
      out[o] = E[v >>> 18];
      out[o + 1] = E[(v >>> 12) & 63];
      out[o + 2] = E[(v >>> 6) & 63];
      out[o + 3] = E[v & 63];
      o += 4;
    }
    if (n - full === 1) {
      const v = bytes[i];
      out[o] = E[v >>> 2]; out[o + 1] = E[(v & 3) << 4]; out[o + 2] = 61; out[o + 3] = 61;
    } else if (n - full === 2) {
      const v = (bytes[i] << 8) | bytes[i + 1];
      out[o] = E[v >>> 10]; out[o + 1] = E[(v >>> 4) & 63]; out[o + 2] = E[(v & 15) << 2]; out[o + 3] = 61;
    }
    if (out.length <= B64_CHUNK) return String.fromCharCode.apply(null, out);
    const parts = [];
    for (let p = 0; p < out.length; p += B64_CHUNK) {
      parts.push(String.fromCharCode.apply(null, out.subarray(p, p + B64_CHUNK)));
    }
    return parts.join('');
  };

  // Inverse of b64enc. Whitespace is skipped, padding is optional, and the URL-safe alphabet is
  // accepted. Throws Error('bad base64') on any other character, on data after the padding, or
  // on an impossible length. The result always owns a buffer of exactly its own length.
  U.b64dec = function (str) {
    if (typeof str !== 'string') throw new Error('bad base64');
    const n = str.length, D = B64_DEC;
    // Exact size for well-formed padded input (the common case, no copy at the end); otherwise
    // an upper bound that is trimmed below.
    let cap = (n >>> 2) * 3 + 3;
    if (n > 0 && (n & 3) === 0) {
      cap = (n >>> 2) * 3;
      if (str.charCodeAt(n - 1) === 61) cap -= str.charCodeAt(n - 2) === 61 ? 2 : 1;
    }
    const out = new Uint8Array(cap);
    let o = 0, i = 0;
    // Fast path: whole groups of four data characters.
    for (const end = n - 3; i < end; i += 4) {
      const c0 = str.charCodeAt(i), c1 = str.charCodeAt(i + 1), c2 = str.charCodeAt(i + 2), c3 = str.charCodeAt(i + 3);
      if ((c0 | c1 | c2 | c3) > 127) break;
      const a = D[c0], b = D[c1], c = D[c2], d = D[c3];
      if ((a | b | c | d) < 0) break;
      out[o] = (a << 2) | (b >> 4);
      out[o + 1] = ((b & 15) << 4) | (c >> 2);
      out[o + 2] = ((c & 3) << 6) | d;
      o += 3;
    }
    // General path: the tail, padding, whitespace, errors.
    let acc = 0, have = 0, padded = false;
    for (; i < n; i++) {
      const code = str.charCodeAt(i);
      const v = code < 128 ? D[code] : -1;
      if (v === -2) continue;
      if (v === -3) { padded = true; continue; }
      if (v < 0 || padded) throw new Error('bad base64');
      acc = (acc << 6) | v;
      if (++have === 4) {
        out[o] = (acc >> 16) & 255; out[o + 1] = (acc >> 8) & 255; out[o + 2] = acc & 255;
        o += 3; acc = 0; have = 0;
      }
    }
    if (have === 1) throw new Error('bad base64');
    if (have === 2) { out[o++] = (acc >> 4) & 255; }
    else if (have === 3) { out[o++] = (acc >> 10) & 255; out[o++] = (acc >> 2) & 255; }
    return o === out.length ? out : out.slice(0, o);
  };

  // ---------------------------------------------------------------- number formatting

  // Rounded integer with thousands separators: 12345.4 → "12,345", -1234.5 → "-1,235".
  U.fmtInt = function (n) {
    n = +n;
    if (!Number.isFinite(n)) return String(n);
    const r = Math.round(Math.abs(n));
    if (r === 0) return '0';
    const s = String(r);
    if (s.indexOf('e') >= 0) return (n < 0 ? '-' : '') + s;     // ≥ 1e21: leave as is
    let out = s.slice(-3);
    for (let i = s.length - 3; i > 0; i -= 3) out = s.slice(i > 3 ? i - 3 : 0, i) + ',' + out;
    return (n < 0 ? '-' : '') + out;
  };

  // Compact number with three significant digits and a k / M / B / T suffix:
  // 950 → "950", 12345 → "12.3k", 4560000 → "4.56M", 1.2e9 → "1.2B". Trailing zeros are dropped.
  const BIG_SUFFIX = ['k', 'M', 'B', 'T'];
  U.fmtBig = function (n) {
    n = +n;
    if (!Number.isFinite(n)) return String(n);
    const sign = n < 0 ? '-' : '';
    let a = Math.abs(n);
    if (a < 999.5) {
      const r = Math.round(a);
      return r === 0 ? '0' : sign + r;
    }
    let u = -1;
    while (a >= 999.5 && u < BIG_SUFFIX.length - 1) { a /= 1000; u++; }
    if (a >= 999.5) return sign + U.fmtInt(a) + BIG_SUFFIX[u];      // beyond trillions
    let s = a.toFixed(a < 9.995 ? 2 : a < 99.95 ? 1 : 0);
    if (s.indexOf('.') >= 0) s = s.replace(/\.?0+$/, '');
    return sign + s + BIG_SUFFIX[u];
  };
})(globalThis.DM = globalThis.DM || {});
