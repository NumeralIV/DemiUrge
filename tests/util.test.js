// Tests for js/core/util.js (FOUNDATION §1): scalar helpers, hashes, Rng, noise, base64, formatting.
// Everything is deterministic: fixed seeds, statistical bounds at ≥ 5 sigma.
'use strict';
const T = require('../tools/t.js');
const DM = require('../tools/load.js')({ only: [] });      // foundation only: no sim modules needed
const U = DM.U;
const imul = Math.imul;

// ---------------------------------------------------------------- helpers

function chi2(counts, total) {
  const e = total / counts.length;
  let c = 0;
  for (let i = 0; i < counts.length; i++) c += (counts[i] - e) * (counts[i] - e) / e;
  return c;
}
// 5-sigma upper bound of a chi-square statistic with k bins.
function chi2Max(k) { return (k - 1) + 5 * Math.sqrt(2 * (k - 1)); }
function corr(a, b) {
  const n = a.length;
  let sa = 0, sb = 0, saa = 0, sbb = 0, sab = 0;
  for (let i = 0; i < n; i++) { sa += a[i]; sb += b[i]; saa += a[i] * a[i]; sbb += b[i] * b[i]; sab += a[i] * b[i]; }
  const ma = sa / n, mb = sb / n;
  return (sab / n - ma * mb) / Math.sqrt((saa / n - ma * ma) * (sbb / n - mb * mb));
}
// Order-sensitive 32-bit digest of the exact bit patterns of a sequence of doubles: one flipped
// bit anywhere in the sequence changes it.
function bitDigest() {
  const dv = new DataView(new ArrayBuffer(8));
  let h = 0x811C9DC5 | 0;
  return {
    add(v) { dv.setFloat64(0, v, true); h = imul(h ^ dv.getUint32(0, true), 0x01000193); h = imul(h ^ dv.getUint32(4, true), 0x01000193); },
    value() { return h >>> 0; },
  };
}
function popcount(x) { let c = 0; x >>>= 0; while (x) { x &= x - 1; c++; } return c; }
function isU32(x) { return Number.isInteger(x) && x >= 0 && x <= 0xFFFFFFFF; }
function unitDir(r) {
  let x, y, z, l;
  do { x = r.range(-1, 1); y = r.range(-1, 1); z = r.range(-1, 1); l = x * x + y * y + z * z; } while (l > 1 || l < 1e-3);
  l = Math.sqrt(l);
  return [x / l, y / l, z / l];
}

// ---------------------------------------------------------------- constants and scalar helpers

T.test('DM.R and TAU', () => {
  T.eq(DM.R, 100);
  T.near(U.TAU, 6.283185307179586, 1e-15);
});

T.test('clamp / sat', () => {
  T.eq(U.clamp(5, 0, 10), 5);
  T.eq(U.clamp(-5, 0, 10), 0);
  T.eq(U.clamp(15, 0, 10), 10);
  T.eq(U.clamp(0, 0, 10), 0);
  T.eq(U.clamp(10, 0, 10), 10);
  T.eq(U.clamp(-3, -8, -4), -4);
  T.eq(U.sat(0.25), 0.25);
  T.eq(U.sat(-1e9), 0);
  T.eq(U.sat(7), 1);
  T.eq(U.sat(0), 0);
  T.eq(U.sat(1), 1);
  // just outside and just inside each bound
  T.eq(U.sat(1.5), 1); T.eq(U.sat(1.0000001), 1); T.eq(U.sat(0.9999999), 0.9999999);
  T.eq(U.sat(-0.5), 0); T.eq(U.sat(-1e-9), 0); T.eq(U.sat(1e-9), 1e-9);
  T.eq(U.clamp(10.5, 0, 10), 10); T.eq(U.clamp(-0.5, 0, 10), 0); T.eq(U.clamp(9.5, 0, 10), 9.5); T.eq(U.clamp(0.5, 0, 10), 0.5);
  T.eq(U.clamp(2, 3, 3), 3, 'degenerate range');
});

T.test('lerp', () => {
  T.near(U.lerp(0, 10, 0.5), 5, 1e-12);
  T.eq(U.lerp(2, 8, 0), 2);
  T.eq(U.lerp(2, 8, 1), 8);
  T.near(U.lerp(2, 8, 1.5), 11, 1e-12, 'extrapolates');
  T.near(U.lerp(10, -10, 0.25), 5, 1e-12);
});

T.test('smooth: clamped smoothstep of a 0..1 input', () => {
  T.eq(U.smooth(0), 0);
  T.eq(U.smooth(1), 1);
  T.near(U.smooth(0.5), 0.5, 1e-12);
  T.near(U.smooth(0.25), 0.15625, 1e-12);
  T.near(U.smooth(0.75), 0.84375, 1e-12);
  T.eq(U.smooth(-3), 0);
  T.eq(U.smooth(42), 1);
  T.eq(U.smooth(1.5), 1, 'clamped just above 1 (the raw cubic would give 0)');
  T.eq(U.smooth(1.0001), 1);
  T.eq(U.smooth(-0.5), 0, 'clamped just below 0 (the raw cubic would give 1)');
  T.eq(U.sstep(0, 1, 1.5), 1); T.eq(U.sstep(0, 1, -0.5), 0);
  T.eq(U.remap(1.5, 0, 1, 0, 1), 1); T.eq(U.remap(-0.5, 0, 1, 0, 1), 0);
  let prev = -1;
  for (let i = 0; i <= 100; i++) { const v = U.smooth(i / 100); T.ok(v >= prev, 'monotone'); prev = v; }
  T.near((U.smooth(1e-4) - U.smooth(0)) / 1e-4, 0, 1e-3, 'zero slope at 0');
  T.near((U.smooth(1) - U.smooth(1 - 1e-4)) / 1e-4, 0, 1e-3, 'zero slope at 1');
});

T.test('sstep: GLSL smoothstep', () => {
  T.eq(U.sstep(2, 6, 1), 0);
  T.eq(U.sstep(2, 6, 2), 0);
  T.eq(U.sstep(2, 6, 6), 1);
  T.eq(U.sstep(2, 6, 99), 1);
  T.near(U.sstep(2, 6, 4), 0.5, 1e-12);
  T.near(U.sstep(2, 6, 3), 0.15625, 1e-12);
  T.near(U.sstep(-1, 1, 0), 0.5, 1e-12);
  // reversed edges give the mirrored ramp
  T.eq(U.sstep(6, 2, 7), 0);
  T.eq(U.sstep(6, 2, 1), 1);
  T.near(U.sstep(6, 2, 5), 0.15625, 1e-12);
  // zero-width edge is a step, never NaN
  T.eq(U.sstep(3, 3, 2.9), 0);
  T.eq(U.sstep(3, 3, 3), 1);
  T.eq(U.sstep(3, 3, 3.1), 1);
});

T.test('remap: linear, clamped to the output range', () => {
  T.near(U.remap(5, 0, 10, 100, 200), 150, 1e-12);
  T.eq(U.remap(-5, 0, 10, 100, 200), 100);
  T.eq(U.remap(50, 0, 10, 100, 200), 200);
  T.eq(U.remap(0, 0, 10, 100, 200), 100);
  T.eq(U.remap(10, 0, 10, 100, 200), 200);
  T.near(U.remap(2.5, 0, 10, 200, 100), 175, 1e-12, 'descending output');
  T.eq(U.remap(-1, 0, 10, 200, 100), 200);
  T.eq(U.remap(11, 0, 10, 200, 100), 100);
  T.near(U.remap(7.5, 10, 0, 0, 1), 0.25, 1e-12, 'descending input');
  T.eq(U.remap(12, 10, 0, 0, 1), 0);
  T.eq(U.remap(-2, 10, 0, 0, 1), 1);
  T.eq(U.remap(1, 3, 3, 5, 9), 5, 'degenerate input range, below');
  T.eq(U.remap(4, 3, 3, 5, 9), 9, 'degenerate input range, above');
  T.ok(!Number.isNaN(U.remap(3, 3, 3, 5, 9)));
});

T.test('approach: bounded step, no overshoot', () => {
  T.eq(U.approach(0, 10, 3), 3);
  T.eq(U.approach(9, 10, 3), 10);
  T.eq(U.approach(10, 0, 4), 6);
  T.eq(U.approach(1, 0, 4), 0);
  T.eq(U.approach(5, 5, 2), 5);
  T.eq(U.approach(-2, -10, 3), -5);
  T.eq(U.approach(7, 3, 0), 7, 'zero step does not move');
  let x = 0, steps = 0;
  while (x !== 1 && steps < 100) { x = U.approach(x, 1, 0.3); steps++; }
  T.eq(steps, 4);
});

T.test('damp: frame-rate independent exponential approach', () => {
  T.eq(U.damp(3, 10, 5, 0), 3, 'dt = 0 does not move');
  T.near(U.damp(3, 10, 5, 100), 10, 1e-9, 'long time reaches the target');
  T.near(U.damp(0, 1, Math.LN2, 1), 0.5, 1e-12, 'lambda = ln 2 halves the distance per unit time');
  // one step of dt equals n steps of dt / n
  const one = U.damp(2, -7, 3.3, 0.25);
  let many = 2;
  for (let i = 0; i < 10; i++) many = U.damp(many, -7, 3.3, 0.025);
  T.near(many, one, 1e-12);
  T.eq(U.damp(4, 4, 9, 0.1), 4, 'already at the target');
  const v = U.damp(0, 10, 2, 0.1);
  T.ok(v > 0 && v < 10, 'never overshoots');
});

// ---------------------------------------------------------------- hashes

T.test('hash32 / hash2 / hashf: pinned values (changing them changes every generated world)', () => {
  T.eq([0, 1, 2, -1, 12345, 0x7fffffff].map(U.hash32),
    [33350994, 2672842292, 127880910, 2767685996, 1084875187, 3623524728]);
  T.eq([[0, 0], [0, 1], [1, 0], [1, 1], [-5, 77], [40961, 3]].map(p => U.hash2(p[0], p[1])),
    [2685500393, 999151060, 1156163646, 1007823597, 996928262, 232623060]);
  T.eq(U.hashf(7), 0.4849326182156801);
  T.eq(U.hashf(7, 0), U.hashf(7), 'b defaults to 0');
  T.eq(U.hashf(7, 1), 0.31403061165474355);
});

T.test('hash32: uint32 output, bijective on a range, coerces its input to an int', () => {
  const seen = new Set();
  for (let i = -50000; i < 150000; i++) {
    const h = U.hash32(i);
    if (i % 997 === 0) T.ok(isU32(h), 'uint32');
    seen.add(h);
  }
  T.eq(seen.size, 200000, 'no collisions');
  T.eq(U.hash32(5.9), U.hash32(5));
  T.eq(U.hash32(4294967296 + 5), U.hash32(5), 'input taken mod 2^32');
  T.eq(U.hash32(0xFFFFFFFF), U.hash32(-1));
});

T.test('hash32: avalanche (each input bit flips each output bit about half the time)', () => {
  const N = 2000, flips = new Float64Array(32 * 32);
  let total = 0;
  for (let n = 0; n < N; n++) {
    const a = imul(n, 0x9E3779B1) ^ (n * 7919);
    const h = U.hash32(a);
    for (let b = 0; b < 32; b++) {
      const d = h ^ U.hash32(a ^ (1 << b));
      total += popcount(d);
      for (let o = 0; o < 32; o++) if (d & (1 << o)) flips[b * 32 + o]++;
    }
  }
  T.near(total / (N * 32), 16, 0.2, 'mean flipped bits');
  let worst = 0;
  for (let i = 0; i < flips.length; i++) worst = Math.max(worst, Math.abs(flips[i] / N - 0.5));
  T.ok(worst < 0.07, 'worst input-bit/output-bit bias ' + worst.toFixed(3));
});

T.test('hash2: ordered, bijective per argument, avalanche on both arguments', () => {
  T.ok(U.hash2(1, 2) !== U.hash2(2, 1), 'order matters');
  T.ok(isU32(U.hash2(-123456, 987654321)));
  const sa = new Set(), sb = new Set();
  for (let i = 0; i < 100000; i++) { sa.add(U.hash2(i, 17)); sb.add(U.hash2(17, i)); }
  T.eq(sa.size, 100000, 'bijective in a');
  T.eq(sb.size, 100000, 'bijective in b');
  // a 400 x 400 grid of small ints: 160,000 values in a 2^32 space → about 3 colliding pairs expected
  const grid = new Set();
  for (let a = 0; a < 400; a++) for (let b = 0; b < 400; b++) grid.add(U.hash2(a, b));
  T.ok(grid.size >= 160000 - 15, 'grid collisions: ' + (160000 - grid.size));
  let total = 0;
  const N = 1500;
  for (let n = 0; n < N; n++) {
    const a = imul(n, 0x85EBCA6B) | 0, b = imul(n, 0xC2B2AE35) ^ n;
    const h = U.hash2(a, b);
    for (let bit = 0; bit < 32; bit++) {
      total += popcount(h ^ U.hash2(a ^ (1 << bit), b));
      total += popcount(h ^ U.hash2(a, b ^ (1 << bit)));
    }
  }
  T.near(total / (N * 64), 16, 0.2, 'mean flipped bits');
});

T.test('hashf: [0,1), uniform, and uncorrelated along both arguments for small ints', () => {
  const N = 200, a = [], right = [], down = [], bins = new Float64Array(32);
  let sum = 0, mn = 1, mx = 0;
  for (let i = 0; i < N; i++) {
    for (let j = 0; j < N; j++) {
      const v = U.hashf(i, j);
      mn = Math.min(mn, v); mx = Math.max(mx, v); sum += v;
      bins[Math.floor(v * 32)]++;
      a.push(v); right.push(U.hashf(i + 1, j)); down.push(U.hashf(i, j + 1));
    }
  }
  T.ok(mn >= 0 && mx < 1, 'range');
  T.near(sum / (N * N), 0.5, 5 * 0.2887 / N, 'mean');
  T.ok(chi2(bins, N * N) < chi2Max(32), 'uniform, chi2 = ' + chi2(bins, N * N).toFixed(1));
  T.ok(Math.abs(corr(a, right)) < 5 / N, 'neighbours in a uncorrelated: ' + corr(a, right).toFixed(4));
  T.ok(Math.abs(corr(a, down)) < 5 / N, 'neighbours in b uncorrelated: ' + corr(a, down).toFixed(4));
  // typical use: one random number per cell, a different salt per purpose
  const s1 = [], s2 = [];
  for (let c = 0; c < 40000; c++) { s1.push(U.hashf(c, 1)); s2.push(U.hashf(c, 2)); }
  T.ok(Math.abs(corr(s1, s2)) < 5 / 200, 'salts are independent');
});

// ---------------------------------------------------------------- Rng

// Independent reference: mulberry32 in BigInt arithmetic modulo 2^32.
function refMulberry32(seed) {
  const M = 0xFFFFFFFFn;
  let s = BigInt(seed >>> 0);
  return function () {
    s = (s + 0x6D2B79F5n) & M;
    let t = s;
    t = ((t ^ (t >> 15n)) * (t | 1n)) & M;
    t = t ^ ((t + (((t ^ (t >> 7n)) * (t | 61n)) & M)) & M);
    return Number((t ^ (t >> 14n)) & M);
  };
}

T.test('rng: pinned streams', () => {
  const take = (r, n) => { const a = []; for (let i = 0; i < n; i++) a.push(r.next()); return a; };
  const r0 = U.rng(0);
  T.eq(take(r0, 5), [1144304738, 1416247, 958946056, 627933444, 2007157716]);
  T.eq(r0.s, 567894473);
  T.eq(take(U.rng(1), 5), [2693262067, 11749833, 2265367787, 4213581821, 4159151403]);
  T.eq(take(U.rng(12345), 5), [4207900869, 1317490944, 2079646450, 3513001552, 2187978186]);
  T.eq(take(U.rng(0xFFFFFFFF), 3), [3850105811, 813802916, 3073704848]);
});

T.test('rng: matches a BigInt reference implementation of mulberry32', () => {
  for (const seed of [0, 1, 2, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF, 0x92D68CA2, 20240229]) {
    const r = U.rng(seed), ref = refMulberry32(seed);
    for (let i = 0; i < 1500; i++) {
      const a = r.next(), b = ref();
      if (a !== b) T.eq(a, b, 'seed ' + seed + ' draw ' + i);
    }
  }
  T.ok(true);
});

T.test('rng: seed coercion (>>> 0), 0 is valid, the state is exactly one uint32', () => {
  T.eq(U.rng(0).s, 0);
  T.eq(U.rng(-1).s, 0xFFFFFFFF);
  T.eq(U.rng(4294967296 + 5).s, 5);
  T.eq(U.rng(7.9).s, 7);
  T.eq(U.rng().s, 0);
  T.eq(U.rng('42').s, 42);
  const r = U.rng(0);
  T.eq(Object.keys(r), ['s'], 'the only own property is the state');
  T.eq(JSON.stringify(r), '{"s":0}');
  T.ok(r instanceof U.Rng);
  // seed 0 does not produce a degenerate stream
  const seen = new Set();
  for (let i = 0; i < 1000; i++) { const v = r.next(); T.ok(isU32(v)); T.ok(isU32(r.s)); seen.add(v); }
  T.eq(seen.size, 1000);
  T.eq(r.next() === U.rng(0).next(), false);
});

T.test('rng: assigning rng.s restores the stream exactly, for every method', () => {
  const r = U.rng(987654321);
  for (let i = 0; i < 37; i++) r.next();
  const saved = r.s;
  const arr = [1, 2, 3, 4, 5, 6, 7, 8, 9];
  const run = () => [
    r.next(), r.f(), r.range(-3, 9), r.int(-5, 5), r.chance(0.5), r.pick(arr),
    r.weighted(arr, x => x), r.gauss(), r.gauss(), r.shuffle(arr.slice()).join(''), r.fork(3).next(),
    r.fork('abc').f(), r.next(), r.s,
  ];
  const first = run();
  r.s = saved;
  T.eq(run(), first, 'same stream after restoring s');
  // a different Rng object given the same state continues identically
  const other = U.rng(0);
  other.s = saved;
  r.s = saved;
  for (let i = 0; i < 100; i++) T.eq(other.next(), r.next());
  // serialization round trip through JSON
  const st = JSON.parse(JSON.stringify({ s: r.s })).s;
  const again = U.rng(0); again.s = st;
  T.eq(again.gauss(), r.gauss());
});

T.test('rng.f: [0,1), mean, variance, uniformity, no serial correlation', () => {
  const r = U.rng(2024), N = 200000, a = new Float64Array(N), bins = new Float64Array(64);
  let s = 0, s2 = 0, mn = 1, mx = 0;
  for (let i = 0; i < N; i++) {
    const v = r.f();
    a[i] = v; s += v; s2 += v * v;
    if (v < mn) mn = v; if (v > mx) mx = v;
    bins[Math.floor(v * 64)]++;
  }
  T.ok(mn >= 0 && mx < 1, 'range');
  const mean = s / N, variance = s2 / N - mean * mean;
  T.near(mean, 0.5, 5 * 0.2887 / Math.sqrt(N), 'mean');
  T.near(variance, 1 / 12, 0.001, 'variance');
  T.ok(chi2(bins, N) < chi2Max(64), 'chi2 = ' + chi2(bins, N).toFixed(1));
  for (const lag of [1, 2, 7]) {
    const c = corr(a.subarray(0, N - lag), a.subarray(lag));
    T.ok(Math.abs(c) < 5 / Math.sqrt(N), 'lag ' + lag + ' correlation ' + c.toFixed(4));
  }
  // every bit of next() is balanced
  const ones = new Float64Array(32), M = 50000;
  for (let i = 0; i < M; i++) { const v = r.next(); for (let b = 0; b < 32; b++) if (v & (1 << b)) ones[b]++; }
  for (let b = 0; b < 32; b++) T.near(ones[b] / M, 0.5, 5 * 0.5 / Math.sqrt(M), 'bit ' + b);
});

T.test('rng: nearby seeds give unrelated streams', () => {
  const firsts = [], bins = new Float64Array(16);
  for (let seed = 0; seed < 10000; seed++) {
    const v = U.rng(seed).f();
    firsts.push(v);
    bins[Math.floor(v * 16)]++;
  }
  T.ok(chi2(bins, 10000) < chi2Max(16), 'first outputs uniform, chi2 = ' + chi2(bins, 10000).toFixed(1));
  const c = corr(firsts.slice(0, -1), firsts.slice(1));
  T.ok(Math.abs(c) < 0.05, 'first outputs of consecutive seeds uncorrelated: ' + c.toFixed(4));
  const a = U.rng(100), b = U.rng(101), xa = [], xb = [];
  for (let i = 0; i < 20000; i++) { xa.push(a.f()); xb.push(b.f()); }
  T.ok(Math.abs(corr(xa, xb)) < 5 / Math.sqrt(20000), 'streams of seeds 100 and 101 uncorrelated');
});

T.test('rng.range: [a,b)', () => {
  const r = U.rng(5), N = 100000;
  let mn = Infinity, mx = -Infinity, s = 0;
  for (let i = 0; i < N; i++) { const v = r.range(-3, 7); mn = Math.min(mn, v); mx = Math.max(mx, v); s += v; }
  T.ok(mn >= -3 && mx < 7, 'range');
  T.ok(mn < -2.99 && mx > 6.99, 'covers the range');
  T.near(s / N, 2, 5 * 10 * 0.2887 / Math.sqrt(N), 'mean');
  T.eq(r.range(4, 4), 4, 'empty range');
  // never returns b even when (b - a) is at the limit of float resolution
  for (let i = 0; i < 20000; i++) {
    const v = r.range(1e9, 1e9 + 2e-7);
    if (!(v >= 1e9 && v < 1e9 + 2e-7)) T.ok(false, 'range excluded upper bound violated: ' + v);
  }
});

T.test('rng.int: both ends inclusive, uniform', () => {
  const r = U.rng(6), N = 60000, counts = new Float64Array(5);
  for (let i = 0; i < N; i++) {
    const v = r.int(3, 7);
    T.ok(Number.isInteger(v) && v >= 3 && v <= 7, 'in [3,7]: ' + v);
    counts[v - 3]++;
  }
  T.ok(counts[0] > 0 && counts[4] > 0, 'both ends occur');
  T.ok(chi2(counts, N) < chi2Max(5), 'uniform, chi2 = ' + chi2(counts, N).toFixed(1));
  const neg = new Set();
  for (let i = 0; i < 2000; i++) neg.add(r.int(-2, 2));
  T.eq([...neg].sort((a, b) => a - b), [-2, -1, 0, 1, 2]);
  T.eq(r.int(9, 9), 9, 'single value');
  const coin = new Set();
  for (let i = 0; i < 200; i++) coin.add(r.int(0, 1));
  T.eq(coin.size, 2);
  const rev = new Set();
  for (let i = 0; i < 500; i++) rev.add(r.int(4, 2));
  T.eq([...rev].sort(), [2, 3, 4], 'arguments in either order');
  for (let i = 0; i < 2000; i++) {
    const v = r.int(0, 0x7FFFFFFF);
    T.ok(Number.isInteger(v) && v >= 0 && v <= 0x7FFFFFFF);
  }
});

T.test('rng.chance', () => {
  const r = U.rng(7), N = 100000;
  let hits = 0;
  for (let i = 0; i < N; i++) if (r.chance(0.3)) hits++;
  T.near(hits / N, 0.3, 5 * Math.sqrt(0.21 / N), 'p = 0.3');
  for (let i = 0; i < 5000; i++) {
    T.eq(r.chance(0), false); T.eq(r.chance(1), true);
    T.eq(r.chance(-2), false); T.eq(r.chance(2), true);
  }
  // always exactly one draw, so later draws do not depend on p
  const a = U.rng(11), b = U.rng(11);
  a.chance(0); b.next();
  T.eq(a.s, b.s);
  // the comparison is strict: p = 0 can never succeed, even on a draw of exactly 0
  const chanceAt = (f, p) => U.Rng.prototype.chance.call({ f() { return f; } }, p);
  T.eq(chanceAt(0, 0), false, 'draw 0, p = 0');
  T.eq(chanceAt(0, 1e-12), true);
  T.eq(chanceAt(0.3, 0.3), false, 'draw equal to p');
  T.eq(chanceAt(0.9999999999, 1), true, 'p = 1 always succeeds');
});

T.test('rng.pick', () => {
  const r = U.rng(8);
  const s0 = r.s;
  T.eq(r.pick([]), undefined);
  T.eq(r.s, s0, 'empty array consumes no draw');
  T.eq(r.pick(['only']), 'only');
  const arr = ['a', 'b', 'c', 'd', 'e', 'f', 'g'], counts = new Float64Array(7), N = 70000;
  for (let i = 0; i < N; i++) counts[arr.indexOf(r.pick(arr))]++;
  T.ok(chi2(counts, N) < chi2Max(7), 'uniform, chi2 = ' + chi2(counts, N).toFixed(1));
  const ta = new Float32Array([1.5, 2.5, 3.5]);
  T.ok(ta.indexOf(r.pick(ta)) >= 0, 'typed arrays work');
  const objs = [{ id: 1 }, { id: 2 }];
  T.ok(objs.indexOf(r.pick(objs)) >= 0, 'returns the element itself');
});

T.test('rng.weighted: proportional, never picks a zero weight', () => {
  const r = U.rng(9), N = 80000;
  const items = [{ k: 'a', w: 0 }, { k: 'b', w: 1 }, { k: 'c', w: 0 }, { k: 'd', w: 3 }, { k: 'e', w: 0 }];
  const counts = { a: 0, b: 0, c: 0, d: 0, e: 0 };
  for (let i = 0; i < N; i++) {
    const el = r.weighted(items, it => it.w);
    T.ok(items.indexOf(el) >= 0, 'returns an element of the array');
    counts[el.k]++;
  }
  T.eq(counts.a + counts.c + counts.e, 0, 'zero weights are never chosen');
  T.near(counts.b / N, 0.25, 5 * Math.sqrt(0.25 * 0.75 / N));
  T.near(counts.d / N, 0.75, 5 * Math.sqrt(0.25 * 0.75 / N));
});

T.test('rng.weighted: edge cases', () => {
  const r = U.rng(10);
  let s0 = r.s;
  T.eq(r.weighted([], x => 1), undefined, 'empty array');
  T.eq(r.weighted([1, 2, 3], x => 0), undefined, 'all weights zero');
  T.eq(r.weighted([1, 2, 3], x => -1), undefined, 'negative weights count as zero');
  T.eq(r.weighted([1, 2, 3], x => NaN), undefined, 'NaN weights count as zero');
  T.eq(r.s, s0, 'no draw is consumed when nothing can be chosen');
  // exactly one draw when something is chosen
  const twin = U.rng(0); twin.s = r.s;
  r.weighted([1, 2, 3], x => x); twin.next();
  T.eq(r.s, twin.s, 'one draw');
  for (let i = 0; i < 2000; i++) {
    T.eq(r.weighted([10, 20, 30], x => (x === 20 ? 1e-9 : 0)), 20, 'only positive weight, however small');
    T.eq(r.weighted([10, 20, 30], x => (x === 30 ? 5 : x === 10 ? -5 : NaN)), 30);
    T.eq(r.weighted([10, 20, 30], x => (x === 10 ? Infinity : 1)), 10, 'an infinite weight always wins');
  }
  // trailing zero weights are not reachable through rounding at the end of the list
  for (let i = 0; i < 20000; i++) {
    const v = r.weighted([1, 2, 3, 4], x => (x <= 2 ? 0.1 + 0.2 : 0));
    if (v !== 1 && v !== 2) T.ok(false, 'picked zero-weight element ' + v);
  }
  // wfn is called exactly once per element, in order
  const calls = [];
  r.weighted(['x', 'y', 'z'], el => { calls.push(el); return 1; });
  T.eq(calls, ['x', 'y', 'z']);
  // large arrays (beyond the initial scratch size)
  const big = []; for (let i = 0; i < 5000; i++) big.push(i);
  const hit = new Float64Array(2);
  for (let i = 0; i < 4000; i++) {
    const v = r.weighted(big, x => (x === 123 ? 1 : x === 4999 ? 1 : 0));
    T.ok(v === 123 || v === 4999);
    hit[v === 123 ? 0 : 1]++;
  }
  T.ok(chi2(hit, 4000) < chi2Max(2));
  T.ok(Math.abs(r.weighted(new Float32Array([0.5, 1.5]), x => x) - 1) === 0.5, 'typed arrays work');
  // several infinite weights: chosen uniformly among themselves, finite weights never
  const inf = new Float64Array(5);
  for (let i = 0; i < 9000; i++) inf[r.weighted([0, 1, 2, 3, 4], x => (x === 1 || x === 2 || x === 4 ? Infinity : 1e300))]++;
  T.eq(inf[0] + inf[3], 0, 'finite weights lose to infinite ones');
  T.ok(chi2([inf[1], inf[2], inf[4]], 9000) < chi2Max(3), 'infinite weights are equally likely: ' + Array.from(inf));
});

T.test('rng.weighted: exact interval boundaries (driven with chosen draws)', () => {
  // weighted() only touches this.f(), so it can be driven with a chosen draw.
  const at = (f, arr, wfn) => U.Rng.prototype.weighted.call({ f() { return f; } }, arr, wfn);
  const four = ['a', 'b', 'c', 'd'], one = () => 1;
  // element i owns the half-open interval [i/4, (i+1)/4)
  T.eq([0, 0.2499, 0.25, 0.4999, 0.5, 0.7499, 0.75, 0.9999].map(f => at(f, four, one)).join(''), 'aabbccdd');
  T.eq(at(0, ['x', 'y', 'z'], el => (el === 'x' ? 0 : 1)), 'y', 'a draw of 0 skips leading zero weights');
  T.eq(at(0.5, ['a', 'b'], one), 'b');
  T.eq(at(0.75, [1, 2, 3], x => x), 3, 'weights 1, 2, 3: [0, 1/6) [1/6, 1/2) [1/2, 1)');
  T.eq(at(0.49, [1, 2, 3], x => x), 2);
  T.eq(at(0.16, [1, 2, 3], x => x), 1);
  // if rounding ever leaves the draw at or past the total, the last element with a positive
  // weight is returned, never a trailing zero-weight one (simulated here with a draw of 1)
  T.eq(at(1, [1, 2, 3, 4], x => (x <= 2 ? 0.3 : 0)), 2);
  T.eq(at(1, [1, 2, 3, 4], x => (x === 1 ? 5 : 0)), 1);
  T.eq(at(1, four, one), 'd');
});

T.test('rng.weighted: re-entrant (wfn may itself call weighted) and safe after wfn throws', () => {
  const outer = U.rng(21), inner = U.rng(22);
  const groups = [];
  for (let g = 0; g < 40; g++) { const m = []; for (let i = 0; i < 300; i++) m.push(i === g ? 1 : 0); groups.push({ g, members: m.map((w, i) => ({ i, w })) }); }
  for (let rep = 0; rep < 50; rep++) {
    const el = outer.weighted(groups, grp => {
      const m = inner.weighted(grp.members, x => x.w);         // nested call uses the scratch stack too
      T.eq(m.i, grp.g, 'nested pick is correct');
      return grp.g === 7 ? 1 : 0;
    });
    T.eq(el.g, 7, 'outer pick is not corrupted by the nested calls');
  }
  T.throws(() => outer.weighted([1, 2, 3], x => { if (x === 2) throw new Error('boom'); return 1; }));
  for (let i = 0; i < 200; i++) T.eq(outer.weighted([5, 6, 7], x => (x === 6 ? 2 : 0)), 6, 'still correct after an exception');
});

T.test('rng.gauss: standard normal, two draws per call, no hidden state', () => {
  const r = U.rng(31), N = 200000;
  let s = 0, s2 = 0, s3 = 0, s4 = 0, in1 = 0, in2 = 0, mx = 0;
  for (let i = 0; i < N; i++) {
    const v = r.gauss();
    if (!Number.isFinite(v)) T.ok(false, 'not finite');
    s += v; s2 += v * v; s3 += v * v * v; s4 += v * v * v * v;
    const a = Math.abs(v);
    if (a < 1) in1++;
    if (a < 2) in2++;
    if (a > mx) mx = a;
  }
  const mean = s / N, variance = s2 / N - mean * mean;
  T.near(mean, 0, 5 / Math.sqrt(N), 'mean');
  T.near(variance, 1, 5 * Math.sqrt(2 / N), 'variance');
  T.near(s3 / N, 0, 5 * Math.sqrt(15 / N), 'skew');
  T.near(s4 / N, 3, 5 * Math.sqrt(96 / N), 'fourth moment');
  T.near(in1 / N, 0.682689, 0.006, 'within 1 sigma');
  T.near(in2 / N, 0.954500, 0.003, 'within 2 sigma');
  T.ok(mx > 3.5 && mx < 7, 'has tails: max |x| = ' + mx.toFixed(2));
  const a = U.rng(77), b = U.rng(77);
  a.gauss(); b.next(); b.next();
  T.eq(a.s, b.s, 'exactly two draws');
  const c = U.rng(78), s0 = c.s;
  const g1 = c.gauss(); c.s = s0;
  T.eq(c.gauss(), g1, 'no cached second value');
  T.eq(Object.keys(c), ['s']);
});

// gauss() only touches this.next(), so it can be driven with chosen draws: n sets the radius
// (u = 1 − n / 2^32), j the angle (j / 2^32 of a turn).
function gaussOf(n, j) {
  const draws = [n, j];
  let i = 0;
  return U.Rng.prototype.gauss.call({ next() { return draws[i++]; } });
}

T.test('rng.gauss: pinned values (bit-exact on every engine: it does not use Math.log / Math.cos)', () => {
  const r = U.rng(77), got = [];
  for (let i = 0; i < 6; i++) got.push(r.gauss());
  T.eq(got, [1.0161914987439675, -1.5371570923207079, 0.22827116512377127, 0.43924356944125464,
    0.14306319778741486, -1.893097504621608]);
  T.eq(r.s, 503953353);
  T.eq(gaussOf(0x12345678, 0x9ABCDEF0), -0.30431793207672686);
  // agrees with the textbook formula evaluated with the engine's own Math functions
  const a = U.rng(4321), b = U.rng(4321);
  let worst = 0;
  for (let i = 0; i < 100000; i++) {
    const u = 1 - b.f(), v = b.f();
    const want = Math.sqrt(-2 * Math.log(u)) * Math.cos(U.TAU * v);
    worst = Math.max(worst, Math.abs(a.gauss() - want));
  }
  T.ok(worst < 1e-14, 'max difference from sqrt(-2 ln u) cos(2 pi v): ' + worst);
  T.eq(a.s, b.s);
});

T.test('rng.gauss: extreme and boundary draws', () => {
  const RAD = Math.sqrt(2 * Math.LN2);                    // radius for u = 1/2 (n = 2^31)
  const H = 0x80000000;
  // radius: u = 1 gives exactly 0; the smallest u gives the largest possible value
  T.eq(gaussOf(0, 0x12345678), 0, 'u = 1');
  T.eq(gaussOf(0, 0), 0);
  T.near(gaussOf(0xFFFFFFFF, 0), Math.sqrt(64 * Math.LN2), 1e-14, 'u = 2^-32: sqrt(64 ln 2) = 6.66');
  T.near(gaussOf(0xFFFFFFFF, H), -Math.sqrt(64 * Math.LN2), 1e-14);
  T.near(gaussOf(1, 0), Math.sqrt(-2 * Math.log(1 - 1 / 4294967296)), 1e-19, 'u just below 1: tiny radius, full relative precision');
  T.near(gaussOf(H, 0), RAD, 1e-15, 'u = 1/2');
  // every power of two and its neighbours, for u and for 1 − u (all normalization shifts, both
  // sides of the √2 split)
  let worst = 0;
  const radius = (n) => { const want = Math.sqrt(-2 * Math.log(1 - n / 4294967296)); worst = Math.max(worst, Math.abs(gaussOf(n, 0) - want) / (want || 1)); };
  for (let bit = 0; bit < 32; bit++) {
    const p = Math.pow(2, bit);
    for (const d of [-1, 0, 1]) {
      if (p + d >= 1 && p + d <= 0xFFFFFFFF) { radius(p + d); radius(4294967296 - (p + d)); }
      const k = Math.floor(Math.SQRT2 * p) + d;           // mantissa right at √2
      if (k >= 1 && k <= 0xFFFFFFFF) radius(4294967296 - k);
    }
  }
  T.ok(worst < 4e-16, 'radius relative error ' + worst);
  // angle: exact at the quarter turns, right sign in every octant, continuous across the folds
  T.eq(gaussOf(H, 0), -gaussOf(H, H), 'cos(0) = −cos(π)');
  T.ok(gaussOf(H, 0x40000000) === 0 && gaussOf(H, 0xC0000000) === 0, 'cos is exactly 0 at the quarter turns');
  for (let oct = 0; oct < 8; oct++) {
    const j0 = oct * 0x20000000;
    for (const d of [-2, -1, 0, 1, 2, 12345, -12345, 0x10000000]) {
      const j = (j0 + d) >>> 0;
      const want = RAD * Math.cos(U.TAU * (j / 4294967296));
      T.near(gaussOf(H, j), want, 1.5e-15, 'octant ' + oct + ' offset ' + d);
    }
    // crossing an octant boundary moves the value by at most the slope times the step
    const step = Math.abs(gaussOf(H, (j0 + 1) >>> 0) - gaussOf(H, (j0 - 1) >>> 0));
    T.ok(step <= RAD * U.TAU * 2 / 4294967296 * 1.0001, 'no jump at octant boundary ' + oct + ': ' + step);
  }
  // tiny angles keep full relative precision (sin of the folded angle, not 1 − cos)
  T.near(gaussOf(H, 0x40000001) / (-RAD * U.TAU / 4294967296), 1, 1e-12, 'just past a quarter turn');
  // symmetric: cos(−x) = cos(x), cos(π − x) = −cos(x), bit for bit
  const r = U.rng(55);
  for (let i = 0; i < 20000; i++) {
    const n = r.next(), j = r.next();
    const g = gaussOf(n, j);
    if (!(Math.abs(g) <= 6.6605)) T.ok(false, 'bounded by sqrt(64 ln 2): ' + g);
    if (gaussOf(n, (4294967296 - j) >>> 0) !== g) T.ok(false, 'mirror symmetry in the angle');
    if (gaussOf(n, (H - j) >>> 0) !== -g) T.ok(false, 'half-turn antisymmetry');
  }
});

T.test('engine independence: only damp() calls an implementation-approximated Math function', () => {
  // Math.log / exp / sin / cos / … are not specified to the last bit (Node 24 and Edge 157 differ
  // in 3–23 % of arguments). Everything world generation relies on must therefore avoid them.
  // Replace them with functions that throw, then exercise the whole module.
  const names = ['log', 'exp', 'sin', 'cos', 'tan', 'atan', 'atan2', 'acos', 'asin', 'pow', 'log2', 'log10', 'log1p',
    'expm1', 'cbrt', 'hypot', 'sinh', 'cosh', 'tanh', 'asinh', 'acosh', 'atanh'];
  const saved = {};
  let dampThrew = false, sum = 0;
  for (const k of names) { saved[k] = Math[k]; Math[k] = function () { throw new Error('Math.' + k + ' was called'); }; }
  try {
    const r = U.rng(2025);
    for (let i = 0; i < 2000; i++) {
      sum += r.next() + r.f() + r.range(-2, 5) + r.int(-9, 9) + (r.chance(0.4) ? 1 : 0) + r.gauss();
      sum += r.pick([1, 2, 3]) + r.weighted([1, 2, 3], x => x) + r.shuffle([1, 2, 3])[0] + r.fork(i).f() + r.fork('k' + i).f();
      const x = r.range(-30, 30), y = r.range(-30, 30), z = r.range(-30, 30);
      sum += U.noise3(x, y, z) + U.fbm3(x, y, z, 5) + U.ridged3(x, y, z, 4, 2.3, 0.6);
      sum += U.hash32(i) + U.hash2(i, 7) + U.hashf(i, 3) + U.hashf(i);
      sum += U.clamp(x, -1, 1) + U.sat(x) + U.lerp(x, y, 0.3) + U.smooth(x) + U.sstep(-1, 1, x) + U.remap(x, -30, 30, 0, 1) + U.approach(x, y, 0.5);
    }
    const bytes = new Uint8Array(5000);
    for (let i = 0; i < bytes.length; i++) bytes[i] = r.next() >>> 24;
    sum += U.b64dec(U.b64enc(bytes)).length + U.fmtInt(1234567).length + U.fmtBig(1234567).length;
    try { U.damp(0, 1, 2, 0.1); } catch (e) { dampThrew = true; }
  } finally {
    for (const k of names) Math[k] = saved[k];
  }
  T.ok(Number.isFinite(sum), 'everything ran');
  T.ok(dampThrew, 'damp() is the documented exception (Math.exp)');
  T.near(Math.cos(0), 1, 0, 'Math restored');
});

T.test('rng.shuffle: in place, a permutation, uniform', () => {
  const r = U.rng(41);
  const arr = [];
  for (let i = 0; i < 500; i++) arr.push(i);
  const out = r.shuffle(arr);
  T.ok(out === arr, 'returns the same array');
  T.eq(arr.slice().sort((a, b) => a - b), Array.from({ length: 500 }, (_, i) => i), 'a permutation');
  let moved = 0;
  for (let i = 0; i < 500; i++) if (arr[i] !== i) moved++;
  T.ok(moved > 450, 'actually shuffled');
  T.eq(r.shuffle([]), []);
  T.eq(r.shuffle([9]), [9]);
  // all 24 permutations of 4 elements equally likely
  const counts = new Map(), N = 48000;
  for (let i = 0; i < N; i++) { const k = r.shuffle([0, 1, 2, 3]).join(''); counts.set(k, (counts.get(k) || 0) + 1); }
  T.eq(counts.size, 24);
  T.ok(chi2([...counts.values()], N) < chi2Max(24), 'uniform over permutations');
  const ta = r.shuffle(new Int32Array([1, 2, 3, 4, 5, 6]));
  T.eq(Array.from(ta).sort(), [1, 2, 3, 4, 5, 6], 'typed arrays work');
  T.eq(U.rng(5).shuffle([1, 2, 3, 4, 5, 6, 7, 8]), U.rng(5).shuffle([1, 2, 3, 4, 5, 6, 7, 8]), 'deterministic');
  T.eq(U.rng(5).shuffle([1, 2, 3, 4, 5, 6, 7, 8]), [5, 3, 7, 1, 4, 2, 8, 6], 'pinned');
  // n elements consume exactly n − 1 draws (none for 0 or 1 elements)
  for (const n of [0, 1, 2, 3, 10, 257]) {
    const a = U.rng(900 + n), b = U.rng(900 + n);
    a.shuffle(new Array(n).fill(0));
    for (let i = 0; i < n - 1; i++) b.next();
    T.eq(a.s, b.s, n + ' elements');
  }
});

T.test('bit-exact digests of long streams (identical on every engine; changing them changes every generated world)', () => {
  let d = bitDigest();
  const g = U.rng(20261117);
  for (let i = 0; i < 100000; i++) d.add(g.gauss());
  T.eq([d.value(), g.s], [1538461678, 4012119869], 'gauss');

  d = bitDigest();
  const q = U.rng(1234567);
  for (let i = 0; i < 200000; i++) {
    const s = i % 4 === 0 ? 2 : i % 4 === 1 ? 40 : i % 4 === 2 ? 3000 : 1e6;
    d.add(U.noise3(q.range(-s, s), q.range(-s, s), q.range(-s, s)));
  }
  T.eq([d.value(), q.s], [2413778205, 3386876231], 'noise3');

  d = bitDigest();
  const p = U.rng(7654321);
  for (let i = 0; i < 30000; i++) {
    const x = p.range(-9, 9), y = p.range(-9, 9), z = p.range(-9, 9);
    d.add(U.fbm3(x, y, z, 6)); d.add(U.fbm3(x, y, z, 3, 2.3, 0.45));
    d.add(U.ridged3(x, y, z, 5)); d.add(U.ridged3(x, y, z, 4, 1.9, 0.6));
  }
  T.eq([d.value(), p.s], [570148942, 86003841], 'fbm3 / ridged3');

  d = bitDigest();
  for (let i = 0; i < 50000; i++) { d.add(U.hashf(i, i * 7 - 3)); d.add(U.hash32(i * 2654435761)); d.add(U.hash2(-i, i)); }
  T.eq(d.value(), 2925838791, 'hashes');

  d = bitDigest();
  const w = U.rng(99);
  for (let i = 0; i < 20000; i++) {
    d.add(w.f()); d.add(w.range(-3, 11)); d.add(w.int(-7, 7)); d.add(w.chance(0.37) ? 1 : 0);
    d.add(w.pick([3, 1, 4, 1, 5, 9, 2, 6])); d.add(w.weighted([1, 2, 3, 4, 5], x => x * x));
    d.add(w.fork(i).next()); d.add(w.fork('s' + i).next());
    if (i % 100 === 0) d.add(w.shuffle([1, 2, 3, 4, 5, 6, 7])[3]);
  }
  T.eq([d.value(), w.s], [1318555029, 391841939], 'every other Rng method');
});

T.test('rng.fork: does not advance the parent, deterministic, pinned', () => {
  const p = U.rng(2024);
  T.eq([p.fork(0).s, p.fork(1).s, p.fork(-1).s, p.fork('terrain').s, p.fork('climate').s, p.fork('').s, p.fork('1').s],
    [2505585638, 960821982, 494661753, 4095389963, 3280744687, 1043667120, 4084727250]);
  T.eq(p.s, 2024, 'parent state untouched');
  T.eq(p.next(), U.rng(2024).next(), 'parent stream untouched');
  const q = U.rng(555);
  for (let i = 0; i < 10; i++) q.next();
  const before = q.s;
  const c1 = q.fork(9), c2 = q.fork(9), c3 = q.fork('nine'), c4 = q.fork('nine');
  T.eq(q.s, before);
  T.ok(c1 instanceof U.Rng && c1 !== c2);
  T.eq(Object.keys(c1), ['s']);
  for (let i = 0; i < 50; i++) { T.eq(c1.next(), c2.next(), 'same tag, same stream'); T.eq(c3.next(), c4.next()); }
  T.eq(q.s, before, 'drawing from children does not touch the parent');
  // the fork depends on the parent state
  const f1 = q.fork(1).next();
  q.next();
  T.ok(q.fork(1).next() !== f1, 'different parent state, different child');
});

T.test('rng.fork: different tags (ints and strings) give distinct, decorrelated streams', () => {
  const p = U.rng(31337);
  const states = new Set();
  for (let t = -2000; t < 2000; t++) states.add(p.fork(t).s);
  for (let t = 0; t < 2000; t++) states.add(p.fork('tag' + t).s);
  for (const t of ['', 'a', 'b', 'ab', 'ba', 'A', 'terrain', 'Terrain', 'climate', '0', '1', '-1']) states.add(p.fork(t).s);
  states.add(p.s);
  T.eq(states.size, 4000 + 2000 + 12 + 1, 'no two tags share a state, and none equals the parent');

  const N = 20000, lim = 5 / Math.sqrt(N);
  const stream = (r) => { const a = new Float64Array(N); for (let i = 0; i < N; i++) a[i] = r.f(); return a; };
  const parent = stream(U.rng(31337));
  const s = {
    i0: stream(p.fork(0)), i1: stream(p.fork(1)), i2: stream(p.fork(2)), im: stream(p.fork(-1)),
    sa: stream(p.fork('a')), sb: stream(p.fork('b')), s1: stream(p.fork('1')), big: stream(p.fork(0x7FFFFFFF)),
  };
  const names = Object.keys(s);
  for (let i = 0; i < names.length; i++) {
    const cp = corr(s[names[i]], parent);
    T.ok(Math.abs(cp) < lim, names[i] + ' vs parent: ' + cp.toFixed(4));
    for (let j = i + 1; j < names.length; j++) {
      const c = corr(s[names[i]], s[names[j]]);
      T.ok(Math.abs(c) < lim, names[i] + ' vs ' + names[j] + ': ' + c.toFixed(4));
      // not the same sequence shifted by a few steps either
      const c1 = corr(s[names[i]].subarray(1), s[names[j]].subarray(0, N - 1));
      T.ok(Math.abs(c1) < lim, names[i] + ' vs ' + names[j] + ' (lag 1): ' + c1.toFixed(4));
    }
  }
  // streams do not overlap: no shared values among the first 2000 outputs of several forks
  const seen = new Map();
  let shared = 0;
  for (let t = 0; t < 20; t++) {
    const c = p.fork(t);
    for (let i = 0; i < 2000; i++) { const v = c.next(); if (seen.has(v) && seen.get(v) !== t) shared++; seen.set(v, t); }
  }
  T.ok(shared <= 2, 'shared outputs between forks: ' + shared);   // ~0.2 expected by chance
});

T.test('rng.fork: the first outputs over many tags are uniform (a fork per cell / per entity is fine)', () => {
  const p = U.rng(8675309), bins = new Float64Array(32), v = [];
  for (let t = 0; t < 40000; t++) { const x = p.fork(t).f(); bins[Math.floor(x * 32)]++; v.push(x); }
  T.ok(chi2(bins, 40000) < chi2Max(32), 'chi2 = ' + chi2(bins, 40000).toFixed(1));
  T.ok(Math.abs(corr(v.slice(0, -1), v.slice(1))) < 5 / 200, 'consecutive tags uncorrelated');
  // forks of forks
  const a = p.fork('a').fork('b'), b = p.fork('b').fork('a');
  T.ok(a.s !== b.s, 'fork order matters');
});

// ---------------------------------------------------------------- noise3

// Reference implementation straight from the definition: sum over every point of the
// body-centred cubic lattice (integer points and integer + ½ points, here in half-units) near
// the sample, kernel (3/4 − d²)⁴, gradient chosen by the lattice hash.
const REF_GRAD = (function () {
  const phi = (1 + Math.sqrt(5)) / 2, inv = 1 / phi, g = [];
  const add = (x, y, z) => { const l = Math.sqrt(x * x + y * y + z * z); g.push([x / l, y / l, z / l]); };
  for (let a = -1; a <= 1; a += 2) {
    for (let b = -1; b <= 1; b += 2) {
      add(0, a, b * phi); add(a, b * phi, 0); add(b * phi, 0, a);
      add(0, a * inv, b * phi); add(a * inv, b * phi, 0); add(b * phi, 0, a * inv);
      add(a, b, 1); add(a, b, -1);
    }
  }
  return g;
})();
function refNoise3(x, y, z) {
  const r = (x + y + z) * (2 / 3);
  const xr = r - x, yr = r - y, zr = r - z;
  const i = Math.floor(xr), j = Math.floor(yr), k = Math.floor(zr);
  let n = 0;
  for (let X = 2 * i - 3; X <= 2 * i + 5; X++) {
    for (let Y = 2 * j - 3; Y <= 2 * j + 5; Y++) {
      if ((Y & 1) !== (X & 1)) continue;
      for (let Z = 2 * k - 3; Z <= 2 * k + 5; Z++) {
        if ((Z & 1) !== (X & 1)) continue;
        const dx = xr - X / 2, dy = yr - Y / 2, dz = zr - Z / 2;
        const t = 0.75 - dx * dx - dy * dy - dz * dz;
        if (t <= 0) continue;
        let h = imul(X, 0x9E3779B1) ^ imul(Y, 0x85EBCA77) ^ imul(Z, 0xC2B2AE3D);
        h = imul(h ^ (h >>> 15), 0x2C1B3C6D);
        h = imul(h ^ (h >>> 12), 0x297A2D39);
        const g = REF_GRAD[h >>> 27];
        n += t * t * t * t * (g[0] * dx + g[1] * dy + g[2] * dz);
      }
    }
  }
  return 11.524 * n;
}
// The noise rotates its input by 180° about (1,1,1); the rotation is its own inverse.
function rot(p) { const r = (p[0] + p[1] + p[2]) * (2 / 3); return [r - p[0], r - p[1], r - p[2]]; }

T.test('noise3: pinned values (bit-exact; changing them changes every generated world)', () => {
  T.eq(U.noise3(0, 0, 0), 0);
  T.eq(U.noise3(0.5, 0.5, 0.5), 0);
  T.eq(U.noise3(0.3, 0.7, 1.9), 0.22144954838254213);
  T.eq(U.noise3(-3.25, 8.125, 100.0625), 0.3912439635911244);
  T.eq(U.noise3(12.34, -56.78, 9.1), 0.2819660259977534);
  T.eq(U.noise3(1000.5, -2000.25, 3000.125), 0.006423964719195348);
  T.eq(U.noise3(1e-3, 2e-3, -3e-3), -0.005470782167853851);
  T.eq(U.noise3(0.3, 0.7, 1.9), U.noise3(0.3, 0.7, 1.9), 'pure function');
});

T.test('noise3: equals the brute-force lattice sum everywhere (random points, cell faces, edges, lattice points)', () => {
  const r = U.rng(101);
  let worst = 0;
  for (let n = 0; n < 12000; n++) {
    const s = n % 3 === 0 ? 3 : n % 3 === 1 ? 60 : 4000;
    const x = r.range(-s, s), y = r.range(-s, s), z = r.range(-s, s);
    worst = Math.max(worst, Math.abs(U.noise3(x, y, z) - refNoise3(x, y, z)));
  }
  // points whose lattice coordinates are multiples of ¼ in one, two or three axes: exactly the
  // places where the nearest lattice point changes and the kernel boundaries meet
  for (let n = 0; n < 6000; n++) {
    const q = () => r.int(-40, 40) / 4;
    const c = () => (r.chance(0.6) ? q() : r.range(-10, 10));
    const p = rot([c(), c(), c()]);
    worst = Math.max(worst, Math.abs(U.noise3(p[0], p[1], p[2]) - refNoise3(p[0], p[1], p[2])));
  }
  T.ok(worst < 1e-12, 'max difference ' + worst);
});

T.test('noise3: range [-1,1], uses the whole range, zero mean', () => {
  const r = U.rng(102), N = 600000;
  let mn = 1, mx = -1, s = 0, s2 = 0;
  for (let n = 0; n < N; n++) {
    const v = U.noise3(r.range(-300, 300), r.range(-300, 300), r.range(-300, 300));
    if (v < mn) mn = v; if (v > mx) mx = v;
    s += v; s2 += v * v;
  }
  T.ok(mn >= -1 && mx <= 1, 'within [-1,1]: ' + mn + ' … ' + mx);
  T.ok(mn < -0.9 && mx > 0.9, 'reaches beyond ±0.9: ' + mn.toFixed(3) + ' … ' + mx.toFixed(3));
  const mean = s / N, sd = Math.sqrt(s2 / N - mean * mean);
  T.near(mean, 0, 0.004, 'mean');
  T.ok(sd > 0.32 && sd < 0.38, 'standard deviation ' + sd.toFixed(4));
});

T.test('noise3: stays within [-1,1] at the worst-case points (midway between neighbouring lattice points)', () => {
  // The largest possible value occurs at the midpoint between an integer lattice point and an
  // adjacent half-integer one, when both gradients point along the line joining them.
  let mx = 0;
  for (let i = -24; i < 24; i++) {
    for (let j = -24; j < 24; j++) {
      for (let k = -24; k < 24; k++) {
        for (let c = 0; c < 8; c++) {
          const p = rot([i + (c & 1 ? 0.25 : 0.75), j + (c & 2 ? 0.25 : 0.75), k + (c & 4 ? 0.25 : 0.75)]);
          const v = Math.abs(U.noise3(p[0], p[1], p[2]));
          if (v > mx) mx = v;
        }
      }
    }
  }
  T.ok(mx <= 1, 'max |noise3| = ' + mx);
  T.ok(mx > 0.97, 'the bound is nearly attained: ' + mx);
});

T.test('noise3: continuous and smooth (bounded first and second differences at every scale)', () => {
  const r = U.rng(103);
  for (const h of [1e-2, 1e-4, 1e-6]) {
    let maxSlope = 0, maxCurv = 0;
    for (let n = 0; n < 60000; n++) {
      const x = r.range(-20, 20), y = r.range(-20, 20), z = r.range(-20, 20), d = unitDir(r);
      const a = U.noise3(x - d[0] * h, y - d[1] * h, z - d[2] * h);
      const b = U.noise3(x, y, z);
      const c = U.noise3(x + d[0] * h, y + d[1] * h, z + d[2] * h);
      maxSlope = Math.max(maxSlope, Math.abs(c - b) / h, Math.abs(b - a) / h);
      if (h >= 1e-4) maxCurv = Math.max(maxCurv, Math.abs(a - 2 * b + c) / (h * h));
    }
    // measured: slope ≤ 4.7, curvature ≤ 27. A jump of size J would show as a slope of J / h.
    T.ok(maxSlope < 7, 'h = ' + h + ': max slope ' + maxSlope.toFixed(3));
    T.ok(maxCurv < 45, 'h = ' + h + ': max curvature ' + maxCurv.toFixed(2));
  }
});

T.test('noise3: no seams across lattice cell faces, cell centres planes or the coordinate planes', () => {
  const r = U.rng(104), e = 1e-9;
  let worst = 0;
  for (let n = 0; n < 30000; n++) {
    // a point exactly on a plane where the implementation switches branch (lattice coordinate
    // an integer or a half-integer), and the two points just either side of it
    const axis = r.int(0, 2), q = [r.range(-30, 30), r.range(-30, 30), r.range(-30, 30)];
    q[axis] = r.int(-60, 60) / 2;
    const lo = q.slice(), hi = q.slice();
    lo[axis] -= e; hi[axis] += e;
    const p = rot(q), pl = rot(lo), ph = rot(hi);
    const v = U.noise3(p[0], p[1], p[2]);
    worst = Math.max(worst, Math.abs(v - U.noise3(pl[0], pl[1], pl[2])), Math.abs(v - U.noise3(ph[0], ph[1], ph[2])));
  }
  for (let n = 0; n < 30000; n++) {
    // across x = 0, y = 0, z = 0 and other integer world planes (sign changes, floor of negatives)
    const axis = r.int(0, 2), q = [r.range(-5, 5), r.range(-5, 5), r.range(-5, 5)];
    q[axis] = r.int(-3, 3);
    const lo = q.slice(), hi = q.slice();
    lo[axis] -= e; hi[axis] += e;
    worst = Math.max(worst, Math.abs(U.noise3(lo[0], lo[1], lo[2]) - U.noise3(hi[0], hi[1], hi[2])));
  }
  T.ok(worst < 5e-8, 'largest step across a seam: ' + worst);
});

T.test('noise3: isotropic (same statistics along the axes, diagonals and arbitrary directions)', () => {
  const r = U.rng(105), N = 60000;
  const k = Math.SQRT1_2, t = 1 / Math.sqrt(3);
  const dirs = [[1, 0, 0], [0, 1, 0], [0, 0, 1], [k, k, 0], [k, -k, 0], [0, k, k], [k, 0, -k], [t, t, t], [t, -t, t], [-t, t, t],
    unitDir(r), unitDir(r), unitDir(r)];
  for (const dist of [0.2, 0.5]) {
    const sf = dirs.map(d => {
      let s = 0;
      for (let n = 0; n < N; n++) {
        const x = r.range(-200, 200), y = r.range(-200, 200), z = r.range(-200, 200);
        const v = U.noise3(x + d[0] * dist, y + d[1] * dist, z + d[2] * dist) - U.noise3(x, y, z);
        s += v * v;
      }
      return s / N;
    });
    const lo = Math.min.apply(null, sf), hi = Math.max.apply(null, sf);
    // measured spread with 400k samples per direction is < 1 %; allow for sampling noise here
    T.ok(hi / lo < 1.06, 'distance ' + dist + ': mean squared increment varies by ' + ((hi / lo - 1) * 100).toFixed(1) + ' %');
  }
  // same variance on axis-aligned planes as in the volume
  const sd = (f) => { let s = 0, s2 = 0; for (let n = 0; n < N; n++) { const v = f(); s += v; s2 += v * v; } return Math.sqrt(s2 / N - (s / N) * (s / N)); };
  const all = sd(() => U.noise3(r.range(-300, 300), r.range(-300, 300), r.range(-300, 300)));
  for (const [name, f] of [
    ['x = 0', () => U.noise3(0, r.range(-300, 300), r.range(-300, 300))],
    ['y = 0', () => U.noise3(r.range(-300, 300), 0, r.range(-300, 300))],
    ['z = 7', () => U.noise3(r.range(-300, 300), r.range(-300, 300), 7)],
    ['sphere', () => { const d = unitDir(r); return U.noise3(d[0] * 5, d[1] * 5, d[2] * 5); }],
  ]) {
    const v = sd(f);
    T.ok(Math.abs(v / all - 1) < 0.04, 'plane ' + name + ': sd ' + v.toFixed(4) + ' vs ' + all.toFixed(4));
  }
});

T.test('noise3: offsetting the inputs gives an unrelated field; no short period', () => {
  const r = U.rng(106), N = 40000;
  const pts = [];
  for (let n = 0; n < N; n++) pts.push([r.range(-40, 40), r.range(-40, 40), r.range(-40, 40)]);
  const base = pts.map(p => U.noise3(p[0], p[1], p[2]));
  for (const off of [[100, 0, 0], [0, 37.5, 0], [3, 3, 3], [256, 0, 0], [0, 256, 0], [0, 0, 1024], [4096, 4096, 4096], [-65536, 0, 0]]) {
    const c = corr(base, pts.map(p => U.noise3(p[0] + off[0], p[1] + off[1], p[2] + off[2])));
    T.ok(Math.abs(c) < 5 / Math.sqrt(N), 'offset ' + JSON.stringify(off) + ': correlation ' + c.toFixed(4));
  }
  // nearby points are strongly correlated (it is smooth noise, not white noise)
  const near = corr(base, pts.map(p => U.noise3(p[0] + 0.1, p[1], p[2])));
  T.ok(near > 0.9, 'correlation at distance 0.1: ' + near.toFixed(3));
});

T.test('noise3: large and odd inputs', () => {
  const r = U.rng(107);
  for (let n = 0; n < 20000; n++) {
    const s = Math.pow(10, r.range(0, 8));
    const v = U.noise3(r.range(-s, s), r.range(-s, s), r.range(-s, s));
    if (!(v >= -1 && v <= 1)) T.ok(false, 'out of range at scale ' + s + ': ' + v);
  }
  // still noise far from the origin: not constant, not degenerate
  let s2 = 0;
  for (let n = 0; n < 5000; n++) { const v = U.noise3(1e6 + r.range(0, 50), -1e6 + r.range(0, 50), 5e5 + r.range(0, 50)); s2 += v * v; }
  T.ok(Math.sqrt(s2 / 5000) > 0.3, 'healthy variance at 1e6');
  T.eq(U.noise3(NaN, 0, 0), 0, 'NaN in → 0 out (never NaN)');
  T.ok(Number.isFinite(U.noise3(Infinity, 1, 2)));
  T.eq(U.noise3(-0, -0, -0), 0);
});

// ---------------------------------------------------------------- fbm3 / ridged3

T.test('fbm3: octave structure, defaults, range', () => {
  const r = U.rng(110);
  T.eq(U.fbm3(1.1, 2.2, 3.3, 5), 0.2251045471662534, 'pinned');
  T.eq(U.fbm3(1.1, 2.2, 3.3, 4, 2.5, 0.6), 0.2987020207204446, 'pinned');
  let mn = 1, mx = -1, s = 0, s2 = 0;
  const N = 100000;
  for (let n = 0; n < N; n++) {
    const x = r.range(-50, 50), y = r.range(-50, 50), z = r.range(-50, 50);
    const v = U.fbm3(x, y, z, 5);
    if (v < mn) mn = v; if (v > mx) mx = v;
    s += v; s2 += v * v;
    if (n < 300) {
      T.eq(U.fbm3(x, y, z, 1), U.noise3(x, y, z), 'one octave is noise3');
      T.eq(U.fbm3(x, y, z, 5, 2, 0.5), v, 'defaults are lac = 2, gain = 0.5');
      T.eq(U.fbm3(x, y, z, 6, 2, 0), U.noise3(x, y, z), 'gain 0 leaves only the first octave');
      // two octaves by hand: the second octave is noise at double frequency (somewhere else)
      const two = U.fbm3(x, y, z, 2) * 1.5 - U.noise3(x, y, z);
      T.ok(Math.abs(two) <= 0.5 + 1e-12, 'second octave has half the amplitude');
    }
  }
  T.ok(mn >= -1 && mx <= 1, 'within [-1,1]');
  T.near(s / N, 0, 0.01, 'zero mean');
  const sd = Math.sqrt(s2 / N);
  T.ok(sd > 0.15 && sd < 0.3, 'standard deviation ' + sd.toFixed(3));
  T.eq(U.fbm3(1, 2, 3, 0), 0, 'zero octaves');
  T.ok(U.fbm3(0, 0, 0, 4) !== 0, 'octaves do not all vanish at the origin');
});

T.test('fbm3: more octaves add finer detail; lacunarity and gain behave', () => {
  const r = U.rng(111), N = 20000, h = 0.004;
  // mean squared increment over a small step h
  const rough = (oct, lac, gain) => {
    const q = U.rng(999);
    let s = 0;
    for (let n = 0; n < N; n++) {
      const x = q.range(-20, 20), y = q.range(-20, 20), z = q.range(-20, 20);
      const d = U.fbm3(x + h, y, z, oct, lac, gain) - U.fbm3(x, y, z, oct, lac, gain);
      s += d * d;
    }
    return s / N;
  };
  // With lac = 2 and gain = 0.5 every octave has the same slope (amplitude halves, frequency
  // doubles), so the squared slope of the un-normalized sum grows linearly with the octave
  // count. fbm3 divides by the amplitude sum 2 − 2^(1−oct); undo that here.
  const un = (oct) => rough(oct) * Math.pow(2 - Math.pow(2, 1 - oct), 2);
  const u1 = un(1), u3 = un(3), u6 = un(6);
  T.ok(u3 / u1 > 2.5 && u3 / u1 < 3.5, '3 octaves carry 3x the squared slope of 1: ' + (u3 / u1).toFixed(2));
  T.ok(u6 / u1 > 5 && u6 / u1 < 7, '6 octaves carry 6x: ' + (u6 / u1).toFixed(2));
  T.ok(rough(4, 3, 0.5) > rough(4, 2, 0.5), 'higher lacunarity is rougher');
  T.ok(rough(4, 2, 0.7) > rough(4, 2, 0.3), 'higher gain is rougher');
  // continuity
  let worst = 0;
  for (let n = 0; n < 20000; n++) {
    const x = r.range(-20, 20), y = r.range(-20, 20), z = r.range(-20, 20), d = unitDir(r);
    worst = Math.max(worst, Math.abs(U.fbm3(x + d[0] * 1e-6, y + d[1] * 1e-6, z + d[2] * 1e-6, 6) - U.fbm3(x, y, z, 6)) / 1e-6);
  }
  T.ok(worst < 80, 'continuous: max slope ' + worst.toFixed(1));
});

T.test('ridged3: [0,1] with sharp ridges at 1', () => {
  const r = U.rng(112), N = 100000;
  T.eq(U.ridged3(1.1, 2.2, 3.3, 5), 0.3785580604897896, 'pinned');
  T.eq(U.ridged3(-4.5, 0.25, 9, 3, 2.1, 0.45), 0.7104932525278534, 'pinned');
  let mn = 1, mx = 0, s = 0;
  for (let n = 0; n < N; n++) {
    const x = r.range(-50, 50), y = r.range(-50, 50), z = r.range(-50, 50);
    const v = U.ridged3(x, y, z, 5);
    if (v < mn) mn = v; if (v > mx) mx = v;
    s += v;
    if (n < 300) {
      const a = 1 - Math.abs(U.noise3(x, y, z));
      T.near(U.ridged3(x, y, z, 1), a * a, 1e-15, 'one octave is (1 - |noise3|)^2');
      T.eq(U.ridged3(x, y, z, 5, 2, 0.5), v, 'defaults are lac = 2, gain = 0.5');
    }
  }
  T.ok(mn >= 0 && mx <= 1, 'within [0,1]: ' + mn + ' … ' + mx);
  T.ok(mn < 0.05 && mx > 0.9, 'uses the range: ' + mn.toFixed(3) + ' … ' + mx.toFixed(3));
  T.ok(s / N > 0.3 && s / N < 0.65, 'mean ' + (s / N).toFixed(3));
  T.eq(U.ridged3(1, 2, 3, 0), 0, 'zero octaves');

  // A ridge is a crease: walk along a line, find where noise3 changes sign, and check that
  // the single-octave ridged value peaks at 1 there with a slope that flips sign.
  let ridges = 0;
  for (let n = 0; n < 200 && ridges < 50; n++) {
    const p = [r.range(-20, 20), r.range(-20, 20), r.range(-20, 20)], d = unitDir(r);
    const f = (t) => U.noise3(p[0] + d[0] * t, p[1] + d[1] * t, p[2] + d[2] * t);
    const g = (t) => U.ridged3(p[0] + d[0] * t, p[1] + d[1] * t, p[2] + d[2] * t, 1);
    let lo = 0, hi = 0.3;
    if (f(lo) * f(hi) >= 0) continue;
    for (let it = 0; it < 60; it++) { const mid = (lo + hi) / 2; if (f(lo) * f(mid) <= 0) hi = mid; else lo = mid; }
    const t0 = (lo + hi) / 2, e = 1e-4;
    if (Math.abs(f(t0 + e) - f(t0 - e)) / (2 * e) < 0.3) continue;       // skip grazing crossings
    ridges++;
    T.near(g(t0), 1, 1e-9, 'ridge top is 1');
    const left = (g(t0) - g(t0 - e)) / e, right = (g(t0 + e) - g(t0)) / e;
    T.ok(left > 0.5 && right < -0.5, 'crease: slope ' + left.toFixed(2) + ' before, ' + right.toFixed(2) + ' after');
  }
  T.ok(ridges >= 30, 'found ridges to test: ' + ridges);
});

// ---------------------------------------------------------------- base64

function bytesOf(str) { const a = new Uint8Array(str.length); for (let i = 0; i < str.length; i++) a[i] = str.charCodeAt(i); return a; }
function randomBytes(n, seed) {
  const r = U.rng(seed), a = new Uint8Array(n);
  for (let i = 0; i < n; i++) a[i] = r.next() >>> 24;
  return a;
}
function sameBytes(a, b) {
  if (a.length !== b.length) return false;
  for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false;
  return true;
}

T.test('b64enc / b64dec: RFC 4648 test vectors', () => {
  const vec = [['', ''], ['f', 'Zg=='], ['fo', 'Zm8='], ['foo', 'Zm9v'], ['foob', 'Zm9vYg=='], ['fooba', 'Zm9vYmE='], ['foobar', 'Zm9vYmFy']];
  for (const [plain, enc] of vec) {
    T.eq(U.b64enc(bytesOf(plain)), enc, 'encode "' + plain + '"');
    const d = U.b64dec(enc);
    T.ok(d instanceof Uint8Array, 'decodes to a Uint8Array');
    T.ok(sameBytes(d, bytesOf(plain)), 'decode "' + enc + '"');
  }
  T.eq(U.b64enc(new Uint8Array([0xFB, 0xFF, 0xFE])), '+//+', 'uses + and /');
  T.eq(U.b64enc(new Uint8Array([0, 0, 0])), 'AAAA');
  T.eq(U.b64enc(new Uint8Array([255, 255, 255, 255])), '/////w==');
});

T.test('b64: round-trips every length 0…200 and every byte value, identical to Node\'s encoder', () => {
  for (let n = 0; n <= 200; n++) {
    const b = randomBytes(n, 1000 + n);
    const s = U.b64enc(b);
    T.eq(s.length, Math.ceil(n / 3) * 4, 'length for n = ' + n);
    T.eq(s, Buffer.from(b).toString('base64'), 'matches reference, n = ' + n);
    const d = U.b64dec(s);
    T.ok(sameBytes(d, b), 'round trip, n = ' + n);
    T.eq(d.byteOffset, 0);
    T.eq(d.buffer.byteLength, n, 'result owns a buffer of exactly its length (safe to view as Uint16Array etc.)');
  }
  const all = new Uint8Array(256 * 3);
  for (let i = 0; i < 256; i++) { all[i * 3] = i; all[i * 3 + 1] = 255 - i; all[i * 3 + 2] = (i * 7) & 255; }
  T.ok(sameBytes(U.b64dec(U.b64enc(all)), all), 'all byte values');
  T.ok(/^[A-Za-z0-9+/]*={0,2}$/.test(U.b64enc(all)), 'only the standard alphabet');
});

T.test('b64: 1 MiB payloads, chunk boundaries', () => {
  for (const n of [1 << 20, (1 << 20) + 1, (1 << 20) + 2]) {
    const b = randomBytes(n, n);
    const s = U.b64enc(b);
    T.eq(typeof s, 'string');
    T.eq(s.length, Math.ceil(n / 3) * 4);
    T.ok(s === Buffer.from(b).toString('base64'), 'matches reference encoder, n = ' + n);
    const d = U.b64dec(s);
    T.eq(d.length, n);
    T.ok(sameBytes(d, b), 'round trip, n = ' + n);
    T.eq(d.buffer.byteLength, n);
  }
  // sizes around the internal string-building chunk (8184 characters = 6138 bytes)
  for (const n of [6135, 6136, 6137, 6138, 6139, 6140, 6141, 12275, 12276, 12277, 18414, 65536]) {
    const b = randomBytes(n, 77 + n);
    const s = U.b64enc(b);
    T.ok(s === Buffer.from(b).toString('base64'), 'n = ' + n);
    T.ok(sameBytes(U.b64dec(s), b), 'round trip n = ' + n);
  }
  // typical save payload: quantized 16-bit field for 40,962 cells, viewed back as Uint16Array
  const q = new Uint16Array(40962);
  for (let i = 0; i < q.length; i++) q[i] = (i * 2654435761) >>> 16;
  const back = new Uint16Array(U.b64dec(U.b64enc(new Uint8Array(q.buffer))).buffer);
  T.eq(back.length, q.length);
  T.ok(sameBytes(back, q), 'Uint16Array round trip through the byte buffer');
});

T.test('b64enc: input types', () => {
  const b = randomBytes(50, 5);
  const ref = Buffer.from(b).toString('base64');
  T.eq(U.b64enc(Array.from(b)), ref, 'plain array of bytes');
  T.eq(U.b64enc(b.buffer), ref, 'ArrayBuffer');
  T.eq(U.b64enc(new Uint8ClampedArray(b)), ref, 'Uint8ClampedArray');
  const big = randomBytes(100, 6);
  T.eq(U.b64enc(big.subarray(10, 60)), Buffer.from(big.subarray(10, 60)).toString('base64'), 'subarray view (offset respected)');
  const u16 = new Uint16Array([0x0201, 0x0403, 0xFFFE]);
  T.eq(U.b64enc(u16), Buffer.from(new Uint8Array(u16.buffer)).toString('base64'), 'other typed arrays are encoded as their raw bytes');
  const f32 = new Float32Array([1.5, -2.25]);
  T.ok(sameBytes(new Float32Array(U.b64dec(U.b64enc(f32)).buffer), f32), 'Float32Array round trip');
  const before = Array.from(b);
  U.b64enc(b);
  T.eq(Array.from(b), before, 'input is not modified');
});

T.test('b64dec: tolerant of whitespace, missing padding and the URL-safe alphabet; rejects garbage', () => {
  const b = randomBytes(100, 9);
  const s = U.b64enc(b);
  const wrapped = s.replace(/(.{20})/g, '$1\r\n') + '\n';
  T.ok(sameBytes(U.b64dec(wrapped), b), 'line-wrapped');
  T.ok(sameBytes(U.b64dec('  ' + s.slice(0, 7) + ' \t' + s.slice(7) + '  '), b), 'spaces and tabs');
  T.eq(U.b64dec(wrapped).buffer.byteLength, 100, 'exact buffer even when trimmed');
  T.ok(sameBytes(U.b64dec('Zm9vYg'), bytesOf('foob')), 'no padding, 2 leftover chars');
  T.ok(sameBytes(U.b64dec('Zm9vYmE'), bytesOf('fooba')), 'no padding, 3 leftover chars');
  T.ok(sameBytes(U.b64dec('-__-'), new Uint8Array([0xFB, 0xFF, 0xFE])), 'URL-safe alphabet');
  T.eq(U.b64dec('').length, 0);
  T.eq(U.b64dec('====').length, 0);
  T.throws(() => U.b64dec('Zm9v!A=='), 'invalid character');
  T.throws(() => U.b64dec('Zm9vY'), 'impossible length');
  T.throws(() => U.b64dec('Zg==Zg=='), 'data after padding');
  T.throws(() => U.b64dec('Zm9vé'), 'non-ASCII');
  T.throws(() => U.b64dec('Zm9vŁm9v'), 'non-ASCII that aliases an ASCII letter in the low byte');
  // the first code points outside the lookup table, and DEL just inside it, in every position
  // of a group (whole groups take a fast path, leftovers a slower one)
  for (const bad of ['\u0080', '\u007f', '\u0081', 'ÿ', 'Ā', '\u0000', '*', '.', '~']) {
    for (let pos = 0; pos < 8; pos++) {
      const str = 'Zm9vYmFy'.slice(0, pos) + bad + 'Zm9vYmFy'.slice(pos + 1);
      T.throws(() => U.b64dec(str), 'char code ' + bad.charCodeAt(0) + ' at position ' + pos);
      if (pos < 7) T.throws(() => U.b64dec(str.slice(0, 7)), 'char code ' + bad.charCodeAt(0) + ' at position ' + pos + ' of an unpadded string');
    }
  }
  T.throws(() => U.b64dec(null), 'not a string');
  T.throws(() => U.b64dec(undefined), 'not a string');
  T.throws(() => U.b64dec(new Uint8Array(4)), 'not a string');
  let msg = '';
  try { U.b64dec('****'); } catch (e) { msg = e.message; }
  T.eq(msg, 'bad base64');
});

// ---------------------------------------------------------------- number formatting

T.test('fmtInt', () => {
  T.eq(U.fmtInt(12345), '12,345');
  T.eq(U.fmtInt(0), '0');
  T.eq(U.fmtInt(7), '7');
  T.eq(U.fmtInt(999), '999');
  T.eq(U.fmtInt(1000), '1,000');
  T.eq(U.fmtInt(100000), '100,000');
  T.eq(U.fmtInt(1234567), '1,234,567');
  T.eq(U.fmtInt(1234567890123), '1,234,567,890,123');
  T.eq(U.fmtInt(12345.4), '12,345', 'rounds down');
  T.eq(U.fmtInt(12345.5), '12,346', 'rounds up');
  T.eq(U.fmtInt(999.5), '1,000');
  T.eq(U.fmtInt(-12345), '-12,345');
  T.eq(U.fmtInt(-999), '-999');
  T.eq(U.fmtInt(-1000), '-1,000');
  T.eq(U.fmtInt(-1234.5), '-1,235', 'negatives round away from zero like positives');
  T.eq(U.fmtInt(-0.4), '0', 'no "-0"');
  T.eq(U.fmtInt(-0), '0');
  T.eq(U.fmtInt(0.49), '0');
  T.eq(U.fmtInt(0.5), '1');
  T.eq(U.fmtInt(0.6), '1', 'a positive fraction that rounds up has no sign');
  T.eq(U.fmtInt(-0.6), '-1');
  T.eq(U.fmtInt(123456), '123,456');
  T.eq(U.fmtInt(-123456), '-123,456', 'no comma after the sign');
  T.eq(U.fmtInt('1500'), '1,500', 'numeric strings');
  T.eq(U.fmtInt(NaN), 'NaN');
  T.eq(U.fmtInt(Infinity), 'Infinity');
  T.eq(U.fmtInt(1e21), '1e+21');
  T.eq(U.fmtInt(123456789012345680000), '123,456,789,012,345,680,000');
});

T.test('fmtBig', () => {
  T.eq(U.fmtBig(950), '950');
  T.eq(U.fmtBig(12345), '12.3k');
  T.eq(U.fmtBig(4560000), '4.56M');
  T.eq(U.fmtBig(1.2e9), '1.2B');
  T.eq(U.fmtBig(0), '0');
  T.eq(U.fmtBig(7), '7');
  T.eq(U.fmtBig(12.7), '13', 'below 1000: rounded integer');
  T.eq(U.fmtBig(999), '999');
  T.eq(U.fmtBig(999.4), '999');
  T.eq(U.fmtBig(999.5), '1k');
  T.eq(U.fmtBig(1000), '1k');
  T.eq(U.fmtBig(1001), '1k');
  T.eq(U.fmtBig(1049), '1.05k');
  T.eq(U.fmtBig(1500), '1.5k');
  T.eq(U.fmtBig(9994), '9.99k');
  T.eq(U.fmtBig(9995), '10k', 'never "10.00k"');
  T.eq(U.fmtBig(10000), '10k');
  T.eq(U.fmtBig(10050), '10.1k');
  T.eq(U.fmtBig(99949), '99.9k');
  T.eq(U.fmtBig(99950), '100k', 'never "100.0k"');
  T.eq(U.fmtBig(123456), '123k');
  T.eq(U.fmtBig(999499), '999k');
  T.eq(U.fmtBig(999500), '1M', 'never "1000k"');
  T.eq(U.fmtBig(1e6), '1M');
  T.eq(U.fmtBig(123456789), '123M');
  T.eq(U.fmtBig(999999999), '1B');
  T.eq(U.fmtBig(7.25e9), '7.25B');
  T.eq(U.fmtBig(1.5e12), '1.5T');
  T.eq(U.fmtBig(2.5e15), '2,500T', 'beyond trillions');
  T.eq(U.fmtBig(999.4e12), '999T');
  T.eq(U.fmtBig(999.7e12), '1,000T', 'the last suffix does not roll over');
  T.eq(U.fmtBig(0.7), '1', 'a positive fraction that rounds up has no sign');
  T.eq(U.fmtBig(-0.7), '-1');
  T.eq(U.fmtBig(-1.5e6), '-1.5M');
  T.eq(U.fmtBig(-12345), '-12.3k');
  T.eq(U.fmtBig(-950), '-950');
  T.eq(U.fmtBig(-0.2), '0', 'no "-0"');
  T.eq(U.fmtBig(NaN), 'NaN');
  // never more than 3 significant digits + suffix, for a sweep of magnitudes
  const r = U.rng(120);
  for (let i = 0; i < 5000; i++) {
    const n = Math.pow(10, r.range(0, 14.9));
    const s = U.fmtBig(n);
    T.ok(/^(\d{1,3}|\d\.\d{1,2}|\d\d\.\d)[kMBT]?$/.test(s), n + ' → ' + s);
    const mult = { k: 1e3, M: 1e6, B: 1e9, T: 1e12 }[s.slice(-1)] || 1;
    const back = parseFloat(s) * mult;
    T.ok(Math.abs(back - n) / n < 0.0051 || Math.abs(back - n) <= 0.5, n + ' → ' + s + ' is within rounding');
  }
});
