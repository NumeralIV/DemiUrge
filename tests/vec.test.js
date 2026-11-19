// Tests for js/core/vec.js (FOUNDATION §2): DM.V3 and DM.M4.
// Matrix maths is checked against hand-computed values and against identities, with plain
// arrays / Float64Array where precision matters and Float32Array where the contract names it.
'use strict';
const T = require('../tools/t.js');
const DM = require('../tools/load.js')({ only: [] });      // foundation only: no sim modules needed
const V3 = DM.V3, M4 = DM.M4, U = DM.U;

// ---------------------------------------------------------------- helpers

function vnear(a, b, eps, msg) {
  eps = eps === undefined ? 1e-6 : eps;
  for (let i = 0; i < 3; i++) {
    if (!(Math.abs(a[i] - b[i]) <= eps)) {
      T.ok(false, (msg ? msg + ': ' : '') + 'expected [' + Array.from(b).slice(0, 3).join(', ') + '] ± ' + eps + ', got [' + Array.from(a).slice(0, 3).join(', ') + ']');
    }
  }
}
function mnear(a, b, eps, msg) {
  eps = eps === undefined ? 1e-6 : eps;
  for (let i = 0; i < 16; i++) {
    if (!(Math.abs(a[i] - b[i]) <= eps)) {
      T.ok(false, (msg ? msg + ': ' : '') + 'element ' + i + ' expected ' + b[i] + ' ± ' + eps + ', got ' + a[i] + '\n  got      [' + Array.from(a).join(', ') + ']\n  expected [' + Array.from(b).join(', ') + ']');
    }
  }
}
const IDENT = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1];
const m64 = () => new Float64Array(16);
function rndUnit(r) {
  let x, y, z, l;
  do { x = r.range(-1, 1); y = r.range(-1, 1); z = r.range(-1, 1); l = x * x + y * y + z * z; } while (l > 1 || l < 1e-3);
  l = Math.sqrt(l);
  return [x / l, y / l, z / l];
}
function rndVec(r, s) { return [r.range(-s, s), r.range(-s, s), r.range(-s, s)]; }
function rndMat(r) { const m = m64(); for (let i = 0; i < 16; i++) m[i] = r.range(-2, 2); return m; }
// Row-major reference multiply written independently of M4.mul: (a·b)[row][col] = Σ a[row][k] b[k][col]
function refMul(a, b) {
  const o = m64();
  for (let col = 0; col < 4; col++) for (let row = 0; row < 4; row++) {
    let s = 0;
    for (let k = 0; k < 4; k++) s += a[k * 4 + row] * b[col * 4 + k];
    o[col * 4 + row] = s;
  }
  return o;
}
// Apply a column-major matrix to a homogeneous vector, independently of M4.transform*.
function apply4(m, v) {
  const o = [0, 0, 0, 0];
  for (let row = 0; row < 4; row++) for (let col = 0; col < 4; col++) o[row] += m[col * 4 + row] * v[col];
  return o;
}

// ================================================================ V3

T.test('V3.create / set / copy', () => {
  const z = V3.create();
  T.ok(z instanceof Float32Array && z.length === 3, 'Float32Array(3)');
  T.eq(Array.from(z), [0, 0, 0]);
  T.eq(Array.from(V3.create(1, 2, 3)), [1, 2, 3]);
  T.eq(Array.from(V3.create(5)), [5, 0, 0]);
  T.ok(V3.create() !== V3.create(), 'a fresh array each time');
  const o = V3.create();
  T.ok(V3.set(o, 4, 5, 6) === o, 'returns the output');
  T.eq(Array.from(o), [4, 5, 6]);
  const c = [0, 0, 0];
  T.ok(V3.copy(c, o) === c);
  T.eq(c, [4, 5, 6]);
  const longer = [9, 9, 9, 9];
  V3.copy(longer, o);
  T.eq(longer, [4, 5, 6, 9], 'only the first three components are touched');
});

T.test('V3.add / sub / scale / addScaled (including aliasing)', () => {
  const a = [1, 2, 3], b = [10, 20, 30], o = [0, 0, 0];
  T.ok(V3.add(o, a, b) === o); T.eq(o, [11, 22, 33]);
  T.ok(V3.sub(o, a, b) === o); T.eq(o, [-9, -18, -27]);
  T.ok(V3.scale(o, a, -2) === o); T.eq(o, [-2, -4, -6]);
  T.ok(V3.addScaled(o, a, b, 0.5) === o); T.eq(o, [6, 12, 18]);
  const x = [1, 2, 3];
  V3.add(x, x, x); T.eq(x, [2, 4, 6], 'o = a = b');
  V3.sub(x, b, x); T.eq(x, [8, 16, 24], 'o = b');
  V3.scale(x, x, 0.5); T.eq(x, [4, 8, 12]);
  V3.addScaled(x, x, x, 2); T.eq(x, [12, 24, 36], 'o = a = b');
  V3.addScaled(x, a, x, 1); T.eq(x, [13, 26, 39], 'o = b');
  const f = V3.create(1, 2, 3);
  V3.add(f, f, [0.5, 0.5, 0.5]);
  T.eq(Array.from(f), [1.5, 2.5, 3.5], 'Float32Array output');
});

T.test('V3.dot / cross / len / dist', () => {
  T.eq(V3.dot([1, 2, 3], [4, -5, 6]), 12);
  T.eq(V3.dot([1, 0, 0], [0, 1, 0]), 0);
  const o = [0, 0, 0];
  T.ok(V3.cross(o, [1, 0, 0], [0, 1, 0]) === o);
  T.eq(o, [0, 0, 1], 'x × y = z (right-handed)');
  T.eq(V3.cross([0, 0, 0], [0, 1, 0], [0, 0, 1]), [1, 0, 0], 'y × z = x');
  T.eq(V3.cross([0, 0, 0], [0, 0, 1], [1, 0, 0]), [0, 1, 0], 'z × x = y');
  T.eq(V3.cross([0, 0, 0], [1, 2, 3], [4, 5, 6]), [-3, 6, -3]);
  T.eq(V3.cross([0, 0, 0], [4, 5, 6], [1, 2, 3]), [3, -6, 3], 'anticommutative');
  T.eq(V3.cross([0, 0, 0], [2, 4, 6], [1, 2, 3]), [0, 0, 0], 'parallel vectors');
  const a = [1, 2, 3], b = [4, 5, 6];
  V3.cross(a, a, b); T.eq(a, [-3, 6, -3], 'o = a');
  const a2 = [1, 2, 3];
  V3.cross(b, a2, b); T.eq(b, [-3, 6, -3], 'o = b');
  const r = U.rng(1);
  for (let i = 0; i < 200; i++) {
    const p = rndVec(r, 5), q = rndVec(r, 5), c = V3.cross([0, 0, 0], p, q);
    T.near(V3.dot(c, p), 0, 1e-9, 'perpendicular to a');
    T.near(V3.dot(c, q), 0, 1e-9, 'perpendicular to b');
    const sinArea = Math.sqrt(Math.max(0, V3.dot(p, p) * V3.dot(q, q) - V3.dot(p, q) * V3.dot(p, q)));
    T.near(V3.len(c), sinArea, 1e-9, '|a × b| = |a||b| sin θ');
  }
  T.eq(V3.len([3, 4, 0]), 5);
  T.eq(V3.len([0, 0, 0]), 0);
  T.near(V3.len([1, 1, 1]), Math.sqrt(3), 1e-15);
  T.eq(V3.dist([1, 2, 3], [4, 6, 3]), 5);
  T.eq(V3.dist([7, 7, 7], [7, 7, 7]), 0);
});

T.test('V3.norm: unit length, zero vector → (0,1,0)', () => {
  const o = [0, 0, 0];
  T.ok(V3.norm(o, [3, 0, 4]) === o);
  vnear(o, [0.6, 0, 0.8], 1e-15);
  T.eq(V3.norm([9, 9, 9], [0, 0, 0]), [0, 1, 0], 'zero vector');
  T.eq(V3.norm([9, 9, 9], [NaN, 0, 0]), [0, 1, 0], 'NaN never propagates');
  T.eq(Array.from(V3.norm(V3.create(), V3.create())), [0, 1, 0]);
  vnear(V3.norm([0, 0, 0], [0, -1e-20, 0]), [0, -1, 0], 1e-12, 'tiny vectors still normalize');
  vnear(V3.norm([0, 0, 0], [1e20, 0, 0]), [1, 0, 0], 1e-12, 'huge vectors');
  const a = [0, 5, 0];
  V3.norm(a, a); T.eq(a, [0, 1, 0], 'o = a');
  const r = U.rng(2);
  for (let i = 0; i < 300; i++) {
    const v = rndVec(r, 100), n = V3.norm([0, 0, 0], v);
    T.near(V3.len(n), 1, 1e-12);
    T.near(V3.dot(n, v), V3.len(v), 1e-9, 'same direction');
    const f = V3.norm(V3.create(), v);
    T.near(V3.len(f), 1, 1e-6, 'Float32Array output');
  }
});

T.test('V3.lerp', () => {
  const a = [1, 2, 3], b = [3, 6, -1], o = [0, 0, 0];
  T.ok(V3.lerp(o, a, b, 0.5) === o);
  T.eq(o, [2, 4, 1]);
  T.eq(V3.lerp([0, 0, 0], a, b, 0), a);
  T.eq(V3.lerp([0, 0, 0], a, b, 1), b);
  T.eq(V3.lerp([0, 0, 0], a, b, 2), [5, 10, -5], 'extrapolates');
  const x = [1, 2, 3];
  V3.lerp(x, x, b, 0.5); T.eq(x, [2, 4, 1], 'o = a');
  const y = [3, 6, -1];
  V3.lerp(y, a, y, 0.5); T.eq(y, [2, 4, 1], 'o = b');
});

T.test('V3.slerp: endpoints, midpoint, constant angular speed, unit length', () => {
  const X = [1, 0, 0], Y = [0, 1, 0], o = [0, 0, 0];
  T.ok(V3.slerp(o, X, Y, 0.5) === o, 'returns the output');
  vnear(o, [Math.SQRT1_2, Math.SQRT1_2, 0], 1e-15, 'midpoint of x and y');
  T.eq(V3.slerp([0, 0, 0], X, Y, 0), [1, 0, 0], 't = 0 is exactly a');
  T.eq(V3.slerp([0, 0, 0], X, Y, 1), [0, 1, 0], 't = 1 is exactly b');
  vnear(V3.slerp([0, 0, 0], X, Y, 1 / 3), [Math.cos(Math.PI / 6), Math.sin(Math.PI / 6), 0], 1e-15);
  vnear(V3.slerp([0, 0, 0], X, Y, 2), [-1, 0, 0], 1e-15, 't = 2 continues along the great circle');
  vnear(V3.slerp([0, 0, 0], X, Y, -1), [0, -1, 0], 1e-15, 't = -1 goes backwards');

  const r = U.rng(3);
  for (let i = 0; i < 2000; i++) {
    const a = rndUnit(r), b = rndUnit(r), t = r.range(0, 1);
    const p = V3.slerp([0, 0, 0], a, b, t);
    T.near(V3.len(p), 1, 1e-12, 'unit length');
    const th = V3.angle(a, b);
    T.near(V3.angle(a, p), t * th, 1e-9, 'angle from a is t·θ');
    T.near(V3.angle(p, b), (1 - t) * th, 1e-9, 'angle to b is (1−t)·θ');
    const n = V3.cross([0, 0, 0], a, b);
    T.near(V3.dot(p, n), 0, 1e-9, 'stays in the plane of a and b');
    vnear(V3.slerp([0, 0, 0], a, b, 0), a, 0, 't = 0');
    vnear(V3.slerp([0, 0, 0], a, b, 1), b, 0, 't = 1');
    // approaching the endpoints is continuous
    vnear(V3.slerp([0, 0, 0], a, b, 1e-9), a, 1e-8, 't → 0');
    vnear(V3.slerp([0, 0, 0], a, b, 1 - 1e-9), b, 1e-8, 't → 1');
  }
});

T.test('V3.slerp: identical and nearly parallel inputs', () => {
  const r = U.rng(4);
  for (let i = 0; i < 500; i++) {
    const a = rndUnit(r);
    for (const t of [0, 0.25, 0.5, 1]) vnear(V3.slerp([0, 0, 0], a, a, t), a, 1e-15, 'a = b');
    for (const eps of [1e-3, 1e-5, 1e-6, 1e-8, 1e-12, 1e-16]) {
      const b = V3.norm([0, 0, 0], V3.addScaled([0, 0, 0], a, rndUnit(r), eps));
      for (const t of [0, 1e-3, 0.3, 0.5, 0.9, 1]) {
        const p = V3.slerp([0, 0, 0], a, b, t);
        T.ok(Number.isFinite(p[0]) && Number.isFinite(p[1]) && Number.isFinite(p[2]), 'finite, eps = ' + eps);
        T.near(V3.len(p), 1, 1e-12, 'unit, eps = ' + eps);
        // lies between a and b: closer to each than they are to one another (plus round-off)
        const d = V3.dist(a, b);
        T.ok(V3.dist(p, a) <= d + 1e-12 && V3.dist(p, b) <= d + 1e-12, 'between, eps = ' + eps);
        T.near(V3.dist(p, a), t * d, 1e-9 + d * 1e-4, 'proportional, eps = ' + eps);
      }
    }
  }
});

T.test('V3.slerp: opposite and nearly opposite inputs never give NaN', () => {
  const r = U.rng(5);
  const cases = [[1, 0, 0], [0, 1, 0], [0, 0, 1], [-1, 0, 0], [0, -1, 0], [0, 0, -1]];
  for (let i = 0; i < 400; i++) cases.push(rndUnit(r));
  for (const a of cases) {
    const exact = [-a[0], -a[1], -a[2]];
    const variants = [exact];
    for (const eps of [1e-4, 1e-6, 1e-7, 1e-9, 1e-13]) variants.push(V3.norm([0, 0, 0], V3.addScaled([0, 0, 0], exact, rndUnit(r), eps)));
    for (const b of variants) {
      const th = V3.angle(a, b);
      T.ok(th > 3.14, 'test input is (nearly) antipodal');
      for (const t of [0, 0.1, 0.25, 0.5, 0.75, 0.9, 1]) {
        const p = V3.slerp([0, 0, 0], a, b, t);
        T.ok(Number.isFinite(p[0]) && Number.isFinite(p[1]) && Number.isFinite(p[2]), 'finite');
        T.near(V3.len(p), 1, 1e-12, 'unit length');
        T.near(V3.angle(a, p), t * th, 1e-6, 'angle from a is t·θ');
      }
      vnear(V3.slerp([0, 0, 0], a, b, 0), a, 0, 't = 0');
      vnear(V3.slerp([0, 0, 0], a, b, 1), b, 0, 't = 1');
      const mid = V3.slerp([0, 0, 0], a, b, 0.5);
      T.near(V3.dot(mid, a), Math.cos(th / 2), 1e-6, 'midpoint is half-way (a quarter turn for exact opposites)');
      T.near(V3.dot(mid, a), V3.dot(mid, b), 1e-6, 'midpoint is equidistant from a and b');
      vnear(V3.slerp([0, 0, 0], a, b, 1 - 1e-9), b, 1e-5, 'continuous at t → 1');
    }
    // deterministic choice of great circle for exact opposites
    vnear(V3.slerp([0, 0, 0], a, exact, 0.5), V3.slerp([0, 0, 0], a, exact, 0.5), 0);
  }
});

T.test('V3.slerp: the great circle chosen for exact opposites is fixed', () => {
  // through the coordinate axis along which |a| is smallest (ties: x before y before z)
  const mid = (a) => V3.slerp([0, 0, 0], a, [-a[0], -a[1], -a[2]], 0.5);
  vnear(mid([1, 0, 0]), [0, 0, 1], 1e-15, 'a = +X');
  vnear(mid([0, 1, 0]), [0, 0, -1], 1e-15, 'a = +Y');
  vnear(mid([0, 0, 1]), [0, 1, 0], 1e-15, 'a = +Z');
  vnear(mid([-1, 0, 0]), [0, 0, -1], 1e-15, 'a = −X');
  vnear(mid([0.6, 0.8, 0]), [0.8, -0.6, 0], 1e-15, 'smallest along z');
  vnear(mid([0.8, 0, 0.6]), [-0.6, 0, 0.8], 1e-15, 'smallest along y');
  vnear(mid([0, 0.8, 0.6]), [0, 0.6, -0.8], 1e-15, 'smallest along x');
  vnear(mid([0.6, 0, 0.8]), [-0.8, 0, 0.6], 1e-15, 'x = 0.6, y = 0: y wins');
  // a quarter of the way round the same circle
  vnear(V3.slerp([0, 0, 0], [1, 0, 0], [-1, 0, 0], 0.25), [Math.SQRT1_2, 0, Math.SQRT1_2], 1e-15);
});

T.test('V3.slerp: inputs need not be exactly unit; zero vectors do not give NaN', () => {
  const r = U.rng(9);
  for (let i = 0; i < 500; i++) {
    const a = rndUnit(r), b = rndUnit(r), t = r.range(0, 1);
    const want = V3.slerp([0, 0, 0], a, b, t);
    for (const [sa, sb] of [[2, 1], [1, 3], [0.25, 0.5], [7, 0.1], [1 + 1e-7, 1 - 1e-7]]) {
      const p = V3.slerp([0, 0, 0], V3.scale([0, 0, 0], a, sa), V3.scale([0, 0, 0], b, sb), t);
      T.near(V3.len(p), 1, 1e-12, 'unit result for input lengths ' + sa + ', ' + sb);
      vnear(p, want, 1e-9, 'same point as for the normalized inputs (lengths ' + sa + ', ' + sb + ')');
    }
    // scaled exact opposites take the same fixed great circle
    const opp = V3.slerp([0, 0, 0], a, [-a[0], -a[1], -a[2]], t);
    vnear(V3.slerp([0, 0, 0], V3.scale([0, 0, 0], a, 4), V3.scale([0, 0, 0], a, -0.5), t), opp, 1e-9, 'scaled opposites');
  }
  const Z0 = [0, 0, 0], b = [0, 0.6, 0.8];
  for (const t of [0, 0.3, 1]) {
    for (const p of [V3.slerp([9, 9, 9], Z0, b, t), V3.slerp([9, 9, 9], b, Z0, t), V3.slerp([9, 9, 9], Z0, Z0, t)]) {
      T.ok(Number.isFinite(p[0]) && Number.isFinite(p[1]) && Number.isFinite(p[2]), 'finite with a zero vector, t = ' + t);
    }
  }
  vnear(V3.slerp([9, 9, 9], Z0, b, 0.3), b, 1e-15, 'zero a: the direction of b');
  vnear(V3.slerp([9, 9, 9], b, Z0, 0.3), b, 1e-15, 'zero b: the direction of a');
  T.eq(V3.slerp([9, 9, 9], Z0, Z0, 0.3), [0, 0, 0], 'both zero: returns a');
});

T.test('V3.slerp: aliasing and Float32Array', () => {
  const a = [1, 0, 0], b = [0, 0, 1];
  const expect = V3.slerp([0, 0, 0], a, b, 0.3);
  const x = a.slice(); V3.slerp(x, x, b, 0.3); vnear(x, expect, 0, 'o = a');
  const y = b.slice(); V3.slerp(y, a, y, 0.3); vnear(y, expect, 0, 'o = b');
  const z = a.slice(); V3.slerp(z, z, z, 0.3); vnear(z, a, 1e-15, 'o = a = b');
  const fa = V3.create(0.6, 0.8, 0), fb = V3.create(0, 0.6, -0.8), fo = V3.create();
  for (let i = 0; i <= 20; i++) {
    V3.slerp(fo, fa, fb, i / 20);
    T.near(V3.len(fo), 1, 2e-7, 'unit to float32 precision');
  }
  // walking a great circle in many small steps stays on the sphere (no drift)
  const p = V3.create(1, 0, 0), target = V3.norm(V3.create(), [-0.3, 0.9, 0.4]);
  for (let i = 0; i < 5000; i++) V3.slerp(p, p, target, 0.002);
  T.near(V3.len(p), 1, 2e-7, 'no drift after 5000 chained slerps');
});

T.test('V3.angle: accurate at every scale, never NaN', () => {
  T.near(V3.angle([1, 0, 0], [0, 1, 0]), Math.PI / 2, 1e-15);
  T.eq(V3.angle([1, 0, 0], [1, 0, 0]), 0);
  T.near(V3.angle([1, 0, 0], [-1, 0, 0]), Math.PI, 1e-15);
  T.near(V3.angle([1, 0, 0], [Math.SQRT1_2, 0, Math.SQRT1_2]), Math.PI / 4, 1e-15);
  const r = U.rng(6);
  for (let i = 0; i < 500; i++) {
    const a = rndUnit(r), b = rndUnit(r);
    const th = V3.angle(a, b);
    T.ok(th >= 0 && th <= Math.PI);
    T.near(th, V3.angle(b, a), 1e-15, 'symmetric');
    T.near(Math.cos(th), V3.dot(a, b), 1e-12, 'agrees with the dot product');
    // tiny and near-π angles, where acos(dot) would lose everything
    const axis = V3.norm([0, 0, 0], V3.cross([0, 0, 0], a, b));
    for (const want of [1e-3, 1e-6, 1e-9, 1e-12]) {
      const c = V3.rotAxis([0, 0, 0], a, axis, want);
      T.near(V3.angle(a, c) / want, 1, 1e-3, 'small angle ' + want);
      const d = V3.rotAxis([0, 0, 0], a, axis, Math.PI - want);
      T.near((Math.PI - V3.angle(a, d)) / want, 1, want >= 1e-9 ? 1e-3 : 0.5, 'near π − ' + want);
    }
    // slightly non-unit inputs (float32 round-off) must not produce NaN
    const fa = V3.create(a[0], a[1], a[2]);
    const self = V3.angle(fa, fa);
    T.ok(self === 0, 'angle with itself is exactly 0, got ' + self);
    const s = V3.scale([0, 0, 0], a, 1 + 1e-7);
    T.ok(!Number.isNaN(V3.angle(s, s)) && !Number.isNaN(V3.angle(s, V3.scale([0, 0, 0], s, -1))));
  }
});

T.test('V3.rotAxis: Rodrigues rotation, right-handed', () => {
  const Z = [0, 0, 1], o = [0, 0, 0];
  T.ok(V3.rotAxis(o, [1, 0, 0], Z, Math.PI / 2) === o);
  vnear(o, [0, 1, 0], 1e-15, 'x about z by +90° → y');
  vnear(V3.rotAxis([0, 0, 0], [0, 1, 0], [1, 0, 0], Math.PI / 2), [0, 0, 1], 1e-15, 'y about x → z');
  vnear(V3.rotAxis([0, 0, 0], [0, 0, 1], [0, 1, 0], Math.PI / 2), [1, 0, 0], 1e-15, 'z about y → x');
  vnear(V3.rotAxis([0, 0, 0], [1, 0, 0], Z, Math.PI), [-1, 0, 0], 1e-15);
  vnear(V3.rotAxis([0, 0, 0], [3, 4, 5], Z, 0), [3, 4, 5], 0, 'zero angle');
  vnear(V3.rotAxis([0, 0, 0], [0, 0, 7], Z, 1.234), [0, 0, 7], 1e-15, 'vector on the axis is unchanged');
  const r = U.rng(7);
  for (let i = 0; i < 500; i++) {
    const v = rndVec(r, 10), k = rndUnit(r), ang = r.range(-7, 7);
    const p = V3.rotAxis([0, 0, 0], v, k, ang);
    T.near(V3.len(p), V3.len(v), 1e-10, 'length preserved');
    T.near(V3.dot(p, k), V3.dot(v, k), 1e-10, 'component along the axis preserved');
    // angle between the parts perpendicular to the axis equals |ang| (mod 2π)
    const vp = V3.addScaled([0, 0, 0], v, k, -V3.dot(v, k)), pp = V3.addScaled([0, 0, 0], p, k, -V3.dot(p, k));
    const sin = V3.dot(V3.cross([0, 0, 0], vp, pp), k), cos = V3.dot(vp, pp);
    const turned = Math.atan2(sin, cos);
    T.near(Math.cos(turned - ang), 1, 1e-10, 'turns by ang, counter-clockwise seen from the axis tip');
    T.near(Math.sin(turned - ang), 0, 1e-8);
    vnear(V3.rotAxis([0, 0, 0], p, k, -ang), v, 1e-10, 'inverse');
    const a2 = r.range(-3, 3);
    vnear(V3.rotAxis([0, 0, 0], p, k, a2), V3.rotAxis([0, 0, 0], v, k, ang + a2), 1e-10, 'angles add');
    vnear(V3.rotAxis([0, 0, 0], v, k, U.TAU), v, 1e-10, 'full turn');
    const x = v.slice(); V3.rotAxis(x, x, k, ang); vnear(x, p, 0, 'o = a');
    const kk = k.slice(); V3.rotAxis(kk, v, kk, ang); vnear(kk, p, 0, 'o = axis');
  }
});

T.test('V3.basis: east = normalize(cross(Y, n)), north = cross(n, east)', () => {
  const e = [0, 0, 0], n = [0, 0, 0];
  T.ok(V3.basis([1, 0, 0], e, n) === e, 'returns outEast');
  vnear(e, [0, 0, -1], 0, 'east at (1,0,0)');
  vnear(n, [0, 1, 0], 0, 'north at (1,0,0)');
  V3.basis([0, 0, 1], e, n);
  vnear(e, [1, 0, 0], 0, 'east at (0,0,1)');
  vnear(n, [0, 1, 0], 0, 'north at (0,0,1)');
  const r = U.rng(8);
  for (let i = 0; i < 1000; i++) {
    const d = rndUnit(r);
    V3.basis(d, e, n);
    const want = V3.norm([0, 0, 0], V3.cross([0, 0, 0], [0, 1, 0], d));
    vnear(e, want, 1e-12, 'east = normalize(Y × n)');
    vnear(n, V3.cross([0, 0, 0], d, e), 1e-12, 'north = n × east');
    T.near(V3.len(e), 1, 1e-12); T.near(V3.len(n), 1, 1e-12);
    T.near(V3.dot(e, n), 0, 1e-12); T.near(V3.dot(e, d), 0, 1e-12); T.near(V3.dot(n, d), 0, 1e-12);
    vnear(V3.cross([0, 0, 0], e, n), d, 1e-12, 'east × north = up (right-handed)');
    T.eq(e[1], 0, 'east is horizontal');
    T.ok(n[1] >= 0, 'north points toward the +Y pole');
    // moving north increases latitude; moving east keeps it
    const stepN = V3.norm([0, 0, 0], V3.addScaled([0, 0, 0], d, n, 1e-4));
    const stepE = V3.norm([0, 0, 0], V3.addScaled([0, 0, 0], d, e, 1e-4));
    if (Math.abs(d[1]) < 0.999) {
      T.ok(stepN[1] > d[1], 'north increases y');
      T.near(stepE[1], d[1], 1e-7, 'east keeps y');
    }
  }
});

T.test('V3.basis: well-defined at and near the poles, aliasing, Float32Array', () => {
  const e = [9, 9, 9], n = [9, 9, 9];
  V3.basis([0, 1, 0], e, n);
  T.eq(e, [1, 0, 0], 'east at the north pole is +X');
  vnear(n, [0, 0, -1], 0, 'north at the north pole');
  V3.basis([0, -1, 0], e, n);
  T.eq(e, [1, 0, 0], 'east at the south pole is +X');
  vnear(n, [0, 0, 1], 0, 'north at the south pole');
  for (const d of [[1e-9, 1, 0], [0, 1, -1e-12], [1e-7, -1, 1e-7], [1e-5, 1, 0], [0, 1, 1e-3], [-1e-4, -1, 1e-4]]) {
    const u = V3.norm([0, 0, 0], d);
    V3.basis(u, e, n);
    for (const v of [e, n]) T.ok(Number.isFinite(v[0]) && Number.isFinite(v[1]) && Number.isFinite(v[2]), 'finite near the pole');
    T.near(V3.len(e), 1, 1e-9); T.near(V3.len(n), 1, 1e-9);
    T.near(V3.dot(e, n), 0, 1e-9);
    T.near(V3.dot(e, u), 0, 1e-5); T.near(V3.dot(n, u), 0, 1e-5);
    vnear(V3.cross([0, 0, 0], e, n), u, 1e-5, 'right-handed near the pole');
  }
  const fd = V3.norm(V3.create(), [0.3, 0.5, -0.8]), fe = V3.create(), fn = V3.create();
  V3.basis(fd, fe, fn);
  T.near(V3.len(fe), 1, 1e-6); T.near(V3.len(fn), 1, 1e-6); T.near(V3.dot(fe, fn), 0, 1e-6);
  vnear(V3.cross([0, 0, 0], fe, fn), fd, 1e-6);
  // outputs may alias the input
  const d = V3.norm([0, 0, 0], [0.3, 0.5, -0.8]), e0 = [0, 0, 0], n0 = [0, 0, 0];
  V3.basis(d, e0, n0);
  const d1 = d.slice(), nn = [0, 0, 0];
  V3.basis(d1, d1, nn); vnear(d1, e0, 0, 'outEast = n'); vnear(nn, n0, 0);
  const d2 = d.slice(), ee = [0, 0, 0];
  V3.basis(d2, ee, d2); vnear(d2, n0, 0, 'outNorth = n'); vnear(ee, e0, 0);
});

// ================================================================ M4

T.test('M4.create / identity / copy', () => {
  const m = M4.create();
  T.ok(m instanceof Float32Array && m.length === 16, 'Float32Array(16)');
  T.eq(Array.from(m), IDENT);
  T.ok(M4.create() !== M4.create());
  const o = new Float32Array(16).fill(7);
  T.ok(M4.identity(o) === o);
  T.eq(Array.from(o), IDENT);
  const src = Float32Array.from({ length: 16 }, (_, i) => i * 1.5), dst = M4.create();
  T.ok(M4.copy(dst, src) === dst);
  T.eq(Array.from(dst), Array.from(src));
  src[3] = 99;
  T.ok(dst[3] !== 99, 'a copy, not a view');
  const plain = new Array(16).fill(0);
  M4.identity(plain); T.eq(plain, IDENT, 'plain arrays work');
  M4.copy(plain, src);
  T.eq(plain.length, 16, 'copy writes exactly 16 elements');
  T.eq(plain, Array.from(src));
  const longer = new Array(18).fill(7);
  M4.copy(longer, IDENT); M4.identity(longer);
  T.eq(longer.slice(16), [7, 7], 'elements past 16 are left alone');
});

T.test('M4: every builder overwrites all 16 elements of a dirty output', () => {
  const dirty = () => new Float64Array(16).fill(9);
  const same = (a, b, msg) => mnear(a, b, 0, msg);
  same(M4.identity(dirty()), IDENT, 'identity');
  same(M4.translation(dirty(), 1, 2, 3), M4.translation(m64(), 1, 2, 3), 'translation');
  same(M4.scaling(dirty(), 1, 2, 3), M4.scaling(m64(), 1, 2, 3), 'scaling');
  same(M4.rotationAxis(dirty(), [0, 0.6, 0.8], 0.7), M4.rotationAxis(m64(), [0, 0.6, 0.8], 0.7), 'rotationAxis');
  same(M4.fromBasis(dirty(), [1, 2, 3], [4, 5, 6], [7, 8, 9], [10, 11, 12]), M4.fromBasis(m64(), [1, 2, 3], [4, 5, 6], [7, 8, 9], [10, 11, 12]), 'fromBasis');
  same(M4.perspective(dirty(), 1, 1.5, 0.1, 50), M4.perspective(m64(), 1, 1.5, 0.1, 50), 'perspective');
  same(M4.perspective(dirty(), 1, 1.5, 0.1, Infinity), M4.perspective(m64(), 1, 1.5, 0.1, Infinity), 'perspective, infinite far');
  same(M4.ortho(dirty(), -1, 2, -3, 4, 0.5, 9), M4.ortho(m64(), -1, 2, -3, 4, 0.5, 9), 'ortho');
  same(M4.lookAt(dirty(), [1, 2, 3], [4, 6, -2], [0, 1, 0]), M4.lookAt(m64(), [1, 2, 3], [4, 6, -2], [0, 1, 0]), 'lookAt');
  same(M4.lookAt(dirty(), [0, 5, 0], [0, 0, 0], [0, 1, 0]), M4.lookAt(m64(), [0, 5, 0], [0, 0, 0], [0, 1, 0]), 'lookAt, degenerate up');
  const a = M4.lookAt(m64(), [1, 2, 3], [4, 6, -2], [0, 1, 0]), b = M4.perspective(m64(), 1, 1.5, 0.1, 50);
  same(M4.mul(dirty(), b, a), M4.mul(m64(), b, a), 'mul');
  same(M4.copy(dirty(), a), a, 'copy');
  const inv = dirty();
  T.eq(M4.invert(inv, a), true);
  same(inv, (() => { const o = m64(); M4.invert(o, a); return o; })(), 'invert');
});

T.test('M4.translation / scaling: column-major layout', () => {
  const t = M4.create();
  T.ok(M4.translation(t, 2, 3, 4) === t);
  T.eq(Array.from(t), [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 2, 3, 4, 1], 'translation lives in elements 12, 13, 14');
  const s = M4.create();
  T.ok(M4.scaling(s, 2, 3, 4) === s);
  T.eq(Array.from(s), [2, 0, 0, 0, 0, 3, 0, 0, 0, 0, 4, 0, 0, 0, 0, 1]);
  const dirty = new Float32Array(16).fill(5);
  M4.translation(dirty, 1, 1, 1); T.eq(Array.from(dirty), [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 1, 1, 1, 1], 'overwrites every element');
  M4.scaling(dirty.fill(5), 1, 1, 1); T.eq(Array.from(dirty), IDENT);
});

T.test('M4.mul: o = a · b applies b first', () => {
  const Tm = M4.translation(m64(), 10, 20, 30), S = M4.scaling(m64(), 2, 3, 4), o = m64();
  T.ok(M4.mul(o, Tm, S) === o);
  // T·S: scale first, then translate
  T.eq(Array.from(o), [2, 0, 0, 0, 0, 3, 0, 0, 0, 0, 4, 0, 10, 20, 30, 1]);
  vnear(M4.transformPoint([0, 0, 0], o, [1, 1, 1]), [12, 23, 34], 0, 'T·S on (1,1,1)');
  // S·T: translate first, then scale
  M4.mul(o, S, Tm);
  T.eq(Array.from(o), [2, 0, 0, 0, 0, 3, 0, 0, 0, 0, 4, 0, 20, 60, 120, 1]);
  vnear(M4.transformPoint([0, 0, 0], o, [1, 1, 1]), [22, 63, 124], 0, 'S·T on (1,1,1)');
  // hand-computed general product
  const a = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16];       // columns (1,2,3,4), (5,6,7,8) …
  const b = [1, 0, 2, 0, 0, 1, 0, 3, -1, 0, 1, 0, 2, 2, 2, 2];
  // column j of a·b = a · (column j of b)
  T.eq(Array.from(M4.mul(m64(), a, b)), [
    1 + 18, 2 + 20, 3 + 22, 4 + 24,
    5 + 39, 6 + 42, 7 + 45, 8 + 48,
    -1 + 9, -2 + 10, -3 + 11, -4 + 12,
    56, 64, 72, 80,
  ]);
});

T.test('M4.mul: identities, associativity, aliasing, Float32Array', () => {
  const r = U.rng(20), I = M4.identity(m64());
  for (let i = 0; i < 100; i++) {
    const a = rndMat(r), b = rndMat(r), c = rndMat(r);
    mnear(M4.mul(m64(), a, I), a, 0, 'a·I');
    mnear(M4.mul(m64(), I, a), a, 0, 'I·a');
    const ab = M4.mul(m64(), a, b);
    mnear(ab, refMul(a, b), 1e-12, 'matches an independent row/column product');
    mnear(M4.mul(m64(), ab, c), M4.mul(m64(), a, M4.mul(m64(), b, c)), 1e-10, '(ab)c = a(bc)');
    const x = Float64Array.from(a); M4.mul(x, x, b); mnear(x, ab, 0, 'o = a');
    const y = Float64Array.from(b); M4.mul(y, a, y); mnear(y, ab, 0, 'o = b');
    const z = Float64Array.from(a); M4.mul(z, z, z); mnear(z, refMul(a, a), 1e-12, 'o = a = b');
    // (a·b)·v = a·(b·v)
    const v = [r.range(-3, 3), r.range(-3, 3), r.range(-3, 3), 1];
    const lhs = apply4(ab, v), rhs = apply4(a, apply4(b, v));
    for (let k = 0; k < 4; k++) T.near(lhs[k], rhs[k], 1e-10, 'b is applied first');
    const f = M4.mul(M4.create(), Float32Array.from(a), Float32Array.from(b));
    T.ok(f instanceof Float32Array);
    mnear(f, ab, 2e-5, 'Float32Array');
  }
});

T.test('M4.perspective: hand-computed values for WebGL clip space', () => {
  const o = M4.create();
  T.ok(M4.perspective(o, Math.PI / 2, 2, 1, 3) === o);
  // f = 1/tan(45°) = 1; [0] = f/aspect; [5] = f; [10] = (far+near)/(near−far) = −2;
  // [11] = −1; [14] = 2·far·near/(near−far) = −3
  mnear(o, [0.5, 0, 0, 0, 0, 1, 0, 0, 0, 0, -2, -1, 0, 0, -3, 0], 1e-7);
  const p = M4.perspective(m64(), Math.PI / 3, 16 / 9, 0.5, 200);
  const f = 1 / Math.tan(Math.PI / 6);
  mnear(p, [f / (16 / 9), 0, 0, 0, 0, f, 0, 0, 0, 0, -(200.5) / 199.5, -1, 0, 0, -2 * 200 * 0.5 / 199.5, 0], 1e-12);
  const dirty = new Float32Array(16).fill(9);
  M4.perspective(dirty, Math.PI / 2, 2, 1, 3);
  mnear(dirty, o, 0, 'overwrites every element');
});

T.test('M4.perspective: near → −1, far → +1, frustum edges → ±1, w = −z', () => {
  const fovy = 1.1, aspect = 1.7, near = 0.25, far = 80;
  const p = M4.perspective(m64(), fovy, aspect, near, far), o = [0, 0, 0];
  const ty = Math.tan(fovy / 2), tx = ty * aspect;
  T.near(M4.transformPoint(o, p, [0, 0, -near])[2], -1, 1e-12, 'near plane');
  T.near(M4.transformPoint(o, p, [0, 0, -far])[2], 1, 1e-12, 'far plane');
  const mid = M4.transformPoint(o, p, [0, 0, -(near + far) / 2])[2];
  T.ok(mid > 0.9 && mid < 1, 'depth is non-linear and increasing: ' + mid);
  for (const z of [near, 1, 10, far]) {
    vnear(M4.transformPoint(o, p, [tx * z, ty * z, -z]), [1, 1, o[2]], 1e-12, 'top-right frustum edge');
    vnear(M4.transformPoint(o, p, [-tx * z, -ty * z, -z]), [-1, -1, o[2]], 1e-12, 'bottom-left frustum edge');
    T.near(apply4(p, [0.3, -0.2, -z, 1])[3], z, 1e-12, 'clip w = −z_view');
  }
  let prev = -2;
  for (let z = near; z <= far; z *= 1.3) { const d = M4.transformPoint(o, p, [0, 0, -z])[2]; T.ok(d > prev, 'monotone depth'); prev = d; }
  // +x view → +x clip, +y view → +y clip (no flips)
  M4.transformPoint(o, p, [0.1, 0.2, -1]);
  T.ok(o[0] > 0 && o[1] > 0);
});

T.test('M4.perspective: infinite far plane', () => {
  const p = M4.perspective(m64(), 1, 1.5, 0.1, Infinity), o = [0, 0, 0];
  for (let i = 0; i < 16; i++) T.ok(Number.isFinite(p[i]), 'finite element ' + i);
  T.near(M4.transformPoint(o, p, [0, 0, -0.1])[2], -1, 1e-12, 'near plane');
  T.near(M4.transformPoint(o, p, [0, 0, -1e12])[2], 1, 1e-9, 'very far → 1');
  T.ok(M4.transformPoint(o, p, [0, 0, -5])[2] < 1);
});

T.test('M4.ortho: hand-computed values, box corners → clip cube corners', () => {
  const o = M4.create();
  T.ok(M4.ortho(o, -2, 2, -1, 1, 1, 5) === o);
  // [0] = 2/(r−l) = 0.5; [5] = 2/(t−b) = 1; [10] = −2/(f−n) = −0.5; [14] = −(f+n)/(f−n) = −1.5
  mnear(o, [0.5, 0, 0, 0, 0, 1, 0, 0, 0, 0, -0.5, 0, 0, 0, -1.5, 1], 1e-7);
  const l = -3, r = 7, b = 2, t = 10, n = 0.5, f = 40, m = M4.ortho(m64(), l, r, b, t, n, f);
  mnear(m, [2 / 10, 0, 0, 0, 0, 2 / 8, 0, 0, 0, 0, -2 / 39.5, 0, -4 / 10, -12 / 8, -40.5 / 39.5, 1], 1e-12, 'asymmetric box');
  const p = [0, 0, 0];
  vnear(M4.transformPoint(p, m, [l, b, -n]), [-1, -1, -1], 1e-12, 'left-bottom-near');
  vnear(M4.transformPoint(p, m, [r, t, -f]), [1, 1, 1], 1e-12, 'right-top-far');
  vnear(M4.transformPoint(p, m, [(l + r) / 2, (b + t) / 2, -(n + f) / 2]), [0, 0, 0], 1e-12, 'centre');
  T.eq(apply4(m, [1, 2, -3, 1])[3], 1, 'w stays 1');
  const dirty = new Float32Array(16).fill(9);
  M4.ortho(dirty, -2, 2, -1, 1, 1, 5);
  mnear(dirty, o, 0, 'overwrites every element');
});

T.test('M4.lookAt: hand-computed cases', () => {
  const o = M4.create();
  T.ok(M4.lookAt(o, [0, 0, 5], [0, 0, 0], [0, 1, 0]) === o);
  mnear(o, [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, -5, 1], 1e-7, 'looking down −Z from z = 5 is a translation');
  // camera at +X looking at the origin, Y up: view x axis = world −Z, view z axis = world +X
  M4.lookAt(o, [10, 0, 0], [0, 0, 0], [0, 1, 0]);
  mnear(o, [0, 0, 1, 0, 0, 1, 0, 0, -1, 0, 0, 0, 0, 0, -10, 1], 1e-7, 'from +X');
  const p = [0, 0, 0];
  vnear(M4.transformPoint(p, o, [0, 0, 0]), [0, 0, -10], 1e-6, 'target 10 units down −Z');
  vnear(M4.transformPoint(p, o, [0, 0, -1]), [1, 0, -10], 1e-6, 'world −Z is to the camera\'s right');
  vnear(M4.transformPoint(p, o, [0, 1, 0]), [0, 1, -10], 1e-6, 'world +Y is up');
  // looking straight down from above with −Z as up
  M4.lookAt(o, [0, 8, 0], [0, 0, 0], [0, 0, -1]);
  vnear(M4.transformPoint(p, o, [0, 0, 0]), [0, 0, -8], 1e-6);
  vnear(M4.transformPoint(p, o, [1, 0, 0]), [1, 0, -8], 1e-6, 'world +X is right');
  vnear(M4.transformPoint(p, o, [0, 0, -1]), [0, 1, -8], 1e-6, 'world −Z is up');
});

T.test('M4.lookAt: identities for random cameras', () => {
  const r = U.rng(21), p = [0, 0, 0];
  for (let i = 0; i < 500; i++) {
    const eye = rndVec(r, 300), target = rndVec(r, 300), up = rndUnit(r);
    const fwd = V3.norm([0, 0, 0], V3.sub([0, 0, 0], target, eye));
    if (Math.abs(V3.dot(fwd, up)) > 0.98) continue;
    const m = M4.lookAt(m64(), eye, target, up), d = V3.dist(eye, target);
    vnear(M4.transformPoint(p, m, eye), [0, 0, 0], 1e-9, 'eye → origin');
    vnear(M4.transformPoint(p, m, target), [0, 0, -d], 1e-9, 'target → −Z axis at its distance');
    const upv = M4.transformDir([0, 0, 0], m, up);
    T.near(upv[0], 0, 1e-12, 'up has no sideways component');
    T.ok(upv[1] > 0, 'up maps into the +Y half-plane');
    // rotation part is orthonormal with determinant +1 (a rigid transform, no mirroring)
    const X = [m[0], m[4], m[8]], Y = [m[1], m[5], m[9]], Z = [m[2], m[6], m[10]];
    T.near(V3.len(X), 1, 1e-12); T.near(V3.len(Y), 1, 1e-12); T.near(V3.len(Z), 1, 1e-12);
    T.near(V3.dot(X, Y), 0, 1e-12); T.near(V3.dot(Y, Z), 0, 1e-12); T.near(V3.dot(X, Z), 0, 1e-12);
    vnear(V3.cross([0, 0, 0], X, Y), Z, 1e-12, 'right-handed');
    T.eq([m[3], m[7], m[11], m[15]], [0, 0, 0, 1], 'affine');
    // distances are preserved
    const a = rndVec(r, 50), b = rndVec(r, 50);
    T.near(V3.dist(M4.transformPoint([0, 0, 0], m, a), M4.transformPoint([0, 0, 0], m, b)), V3.dist(a, b), 1e-9);
    // a point to the camera's right (fwd × up) has positive view x
    const right = V3.cross([0, 0, 0], fwd, up);
    T.ok(M4.transformPoint(p, m, V3.add([0, 0, 0], eye, right))[0] > 0, 'right is +X');
    // aliasing: eye / target / up are read before o is written (o is a different kind of object
    // here, but inputs may be Float32Array)
    const f = M4.lookAt(M4.create(), Float32Array.from(eye), Float32Array.from(target), Float32Array.from(up));
    T.ok(f instanceof Float32Array);
    vnear(M4.transformPoint(p, f, target), [0, 0, -d], 1e-2 + d * 1e-5, 'Float32Array');
  }
});

T.test('M4.lookAt: degenerate inputs stay finite', () => {
  const p = [0, 0, 0];
  const finite = (m) => { for (let i = 0; i < 16; i++) if (!Number.isFinite(m[i])) return false; return true; };
  // up parallel to the view direction (looking straight down / up / along a diagonal)
  for (const [eye, target, up] of [
    [[0, 10, 0], [0, 0, 0], [0, 1, 0]], [[0, -10, 0], [0, 0, 0], [0, 1, 0]], [[0, 0, 0], [0, 5, 0], [0, 1, 0]],
    [[3, 3, 3], [0, 0, 0], [1, 1, 1]], [[1, 2, 3], [4, 2, 3], [-7, 0, 0]], [[0, 0, 9], [0, 0, 0], [0, 0, 1]],
    [[5, 6, 7], [1, 1, 1], [0, 0, 0]],
  ]) {
    const m = M4.lookAt(m64(), eye, target, up);
    T.ok(finite(m), 'finite');
    vnear(M4.transformPoint(p, m, target), [0, 0, -V3.dist(eye, target)], 1e-9, 'target still on −Z');
    vnear(M4.transformPoint(p, m, eye), [0, 0, 0], 1e-9);
    const X = [m[0], m[4], m[8]], Y = [m[1], m[5], m[9]], Z = [m[2], m[6], m[10]];
    T.near(V3.len(X), 1, 1e-12); T.near(V3.len(Y), 1, 1e-12);
    vnear(V3.cross([0, 0, 0], X, Y), Z, 1e-12, 'still a proper rotation');
    T.ok(M4.invert(m64(), m), 'invertible');
  }
  // eye = target
  const m = M4.lookAt(m64(), [4, 5, 6], [4, 5, 6], [0, 1, 0]);
  T.ok(finite(m));
  vnear(M4.transformPoint(p, m, [4, 5, 6]), [0, 0, 0], 1e-12, 'eye still maps to the origin');
  T.ok(M4.invert(m64(), m));
  mnear(m, M4.translation(m64(), -4, -5, -6), 0, 'eye = target with Y up is a pure translation');
});

T.test('M4.lookAt: the fallback for an up vector parallel to the view direction is fixed', () => {
  // The replacement up is the world axis least aligned with the view direction (ties: Y, then Z).
  // Rows of the rotation part are the camera's right / up / back axes in world space.
  const axes = (eye, target, up) => {
    const m = M4.lookAt(m64(), eye, target, up);
    return { right: [m[0], m[4], m[8]], up: [m[1], m[5], m[9]], back: [m[2], m[6], m[10]] };
  };
  let a = axes([0, 10, 0], [0, 0, 0], [0, 1, 0]);                 // straight down on the north pole
  vnear(a.back, [0, 1, 0], 0); vnear(a.right, [-1, 0, 0], 0, 'looking down: right is −X'); vnear(a.up, [0, 0, 1], 0, 'looking down: up is +Z');
  a = axes([0, -10, 0], [0, 0, 0], [0, 1, 0]);                    // straight up at the south pole
  vnear(a.back, [0, -1, 0], 0); vnear(a.right, [1, 0, 0], 0, 'looking up: right is +X'); vnear(a.up, [0, 0, 1], 0, 'looking up: up is +Z');
  a = axes([0, 0, 9], [0, 0, 0], [0, 0, 1]);                      // up along the view axis Z: Y is used
  vnear(a.right, [1, 0, 0], 0); vnear(a.up, [0, 1, 0], 0);
  a = axes([3, 3, 3], [0, 0, 0], [1, 1, 1]);                      // three-way tie: Y is used
  vnear(a.right, [Math.SQRT1_2, 0, -Math.SQRT1_2], 1e-15, 'diagonal view: right');
  T.ok(a.up[1] > 0.8, 'diagonal view: up leans toward +Y');
  a = axes([6, 8, 0], [0, 0, 0], [3, 4, 0]);                      // view in the XY plane: Z is used
  vnear(a.back, [0.6, 0.8, 0], 1e-15); vnear(a.right, [-0.8, 0.6, 0], 1e-15, 'XY-plane view: right'); vnear(a.up, [0, 0, 1], 1e-15, 'XY-plane view: up is +Z');
  a = axes([8, 6, 0], [0, 0, 0], [4, 3, 0]);                      // |z| < |y| < |x|: still Z
  vnear(a.right, [-0.6, 0.8, 0], 1e-15, 'XY-plane view, mostly along X: right'); vnear(a.up, [0, 0, 1], 1e-15);
  a = axes([0, 6, 8], [0, 0, 0], [0, 3, 4]);                      // view in the YZ plane: X is used
  vnear(a.back, [0, 0.6, 0.8], 1e-15); vnear(a.right, [0, -0.8, 0.6], 1e-15, 'YZ-plane view: right'); vnear(a.up, [1, 0, 0], 1e-15, 'YZ-plane view: up is +X');
  a = axes([5, 0, 0], [0, 0, 0], [0, 0, 0]);                      // zero up: Y
  vnear(a.right, [0, 0, -1], 0); vnear(a.up, [0, 1, 0], 0);
});

T.test('M4.lookAt: the length of the up vector does not matter', () => {
  const r = U.rng(28);
  for (let i = 0; i < 200; i++) {
    const eye = rndVec(r, 100), target = rndVec(r, 100), up = rndUnit(r);
    const fwd = V3.norm([0, 0, 0], V3.sub([0, 0, 0], target, eye));
    if (Math.abs(V3.dot(fwd, up)) > 0.98) continue;
    const want = M4.lookAt(m64(), eye, target, up);
    for (const s of [1e-9, 1e-3, 250, 1e9]) {
      mnear(M4.lookAt(m64(), eye, target, V3.scale([0, 0, 0], up, s)), want, 1e-9, 'up scaled by ' + s);
    }
  }
  // a tiny up vector that is not parallel to the view direction is still honoured
  const m = M4.lookAt(m64(), [0, 0, 5], [0, 0, 0], [1e-9, 1e-9, 0]);
  vnear([m[1], m[5], m[9]], [Math.SQRT1_2, Math.SQRT1_2, 0], 1e-12, 'camera up follows the tiny up vector');
});

T.test('M4.invert: hand-computed inverses', () => {
  const o = m64();
  T.eq(M4.invert(o, M4.identity(m64())), true);
  mnear(o, IDENT, 0);
  T.eq(M4.invert(o, M4.translation(m64(), 2, -3, 4)), true);
  mnear(o, M4.translation(m64(), -2, 3, -4), 1e-15, 'translation');
  T.eq(M4.invert(o, M4.scaling(m64(), 2, 4, -8)), true);
  mnear(o, M4.scaling(m64(), 0.5, 0.25, -0.125), 1e-15, 'scaling');
  // T(1,2,3)·S(2,2,2): inverse is S(½)·T(−1,−2,−3) = scale ½ with translation (−½,−1,−1.5)
  M4.invert(o, [2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 2, 0, 1, 2, 3, 1]);
  mnear(o, [0.5, 0, 0, 0, 0, 0.5, 0, 0, 0, 0, 0.5, 0, -0.5, -1, -1.5, 1], 1e-15);
  // perspective(90°, 1, 1, 3): [[1,0,0,0],[0,1,0,0],[0,0,−2,−3],[0,0,−1,0]] (rows);
  // inverse rows: [[1,0,0,0],[0,1,0,0],[0,0,0,−1],[0,0,−1/3,2/3]]
  M4.invert(o, M4.perspective(m64(), Math.PI / 2, 1, 1, 3));
  mnear(o, [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, -1 / 3, 0, 0, -1, 2 / 3], 1e-12, 'perspective');
  const f = M4.create();
  T.eq(M4.invert(f, M4.translation(M4.create(), 1, 2, 3)), true);
  T.ok(f instanceof Float32Array);
  mnear(f, M4.translation(m64(), -1, -2, -3), 1e-7, 'Float32Array');
});

T.test('M4.invert: m · m⁻¹ = m⁻¹ · m = I for random and structured matrices', () => {
  const r = U.rng(22);
  let done = 0;
  for (let i = 0; i < 400; i++) {
    const m = rndMat(r), inv = m64();
    const ok = M4.invert(inv, m);
    T.eq(ok, true, 'random matrices are invertible');
    // scale tolerance by the size of the inverse (conditioning)
    let big = 0; for (let k = 0; k < 16; k++) big = Math.max(big, Math.abs(inv[k]));
    const eps = 1e-11 * Math.max(1, big * big);
    mnear(M4.mul(m64(), m, inv), IDENT, eps, 'm · inverse');
    mnear(M4.mul(m64(), inv, m), IDENT, eps, 'inverse · m');
    const back = m64();
    T.eq(M4.invert(back, inv), true);
    mnear(back, m, eps * 10, 'inverse of the inverse');
    const x = Float64Array.from(m);
    T.eq(M4.invert(x, x), true);
    mnear(x, inv, 0, 'in place (o = a)');
    done++;
  }
  T.eq(done, 400);
  // a full camera: projection · view, and unprojecting clip-space points back to the world
  for (let i = 0; i < 200; i++) {
    const eye = rndVec(r, 250), target = rndVec(r, 50);
    const view = M4.lookAt(m64(), eye, target, [0, 1, 0]);
    const proj = M4.perspective(m64(), r.range(0.3, 1.5), r.range(0.5, 2.5), 0.1, 2000);
    const vp = M4.mul(m64(), proj, view), ivp = m64();
    T.eq(M4.invert(ivp, vp), true, 'view-projection is invertible');
    const world = V3.addScaled([0, 0, 0], target, rndUnit(r), r.range(0, 20));
    const ndc = M4.transformPoint([0, 0, 0], vp, world);
    const back = M4.transformPoint([0, 0, 0], ivp, ndc);
    vnear(back, world, 1e-6, 'unproject(project(p)) = p');
    // the centre of the screen unprojects onto the line of sight
    const nearPt = M4.transformPoint([0, 0, 0], ivp, [0, 0, -1]);
    const dir = V3.norm([0, 0, 0], V3.sub([0, 0, 0], nearPt, eye));
    const fwd = V3.norm([0, 0, 0], V3.sub([0, 0, 0], target, eye));
    vnear(dir, fwd, 1e-6, 'screen centre ray points at the target');
    T.near(V3.dist(nearPt, eye), 0.1, 1e-6, 'near-plane point is `near` away');
    // Float32 storage, as the renderer will use it
    const ivp32 = M4.create();
    T.eq(M4.invert(ivp32, Float32Array.from(vp)), true);
    const back32 = M4.transformPoint([0, 0, 0], ivp32, M4.transformPoint([0, 0, 0], Float32Array.from(vp), world));
    T.ok(V3.dist(back32, world) < 0.05 * Math.max(1, V3.dist(world, eye)) , 'float32 round trip stays close');
  }
});

T.test('M4.invert: singular matrices return false and leave the output untouched', () => {
  const sentinel = () => Float64Array.from({ length: 16 }, (_, i) => 100 + i);
  const untouched = (o) => { for (let i = 0; i < 16; i++) if (o[i] !== 100 + i) return false; return true; };
  const r = U.rng(23);
  const cases = {
    'zero matrix': new Float64Array(16),
    'scale with a zero': M4.scaling(m64(), 1, 0, 1),
    'two equal columns': [1, 2, 3, 4, 1, 2, 3, 4, 0, 0, 1, 0, 0, 0, 0, 1],
    'two equal rows': [1, 5, 1, 0, 2, 6, 2, 0, 3, 7, 3, 0, 4, 8, 4, 1],
    'last row zero': [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0],
    'counting matrix (rank 2)': [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16],
    'NaN': [NaN, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1],
    'Infinity': [Infinity, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1],
  };
  // rank-3 matrices built as (random invertible) · (projection onto a plane) · (random invertible):
  // singular up to double round-off, the determinant is not exactly zero
  for (let i = 0; i < 20; i++) {
    const flat = M4.scaling(m64(), 1, 1, 0);
    cases['rank 3 #' + i] = M4.mul(m64(), rndMat(r), M4.mul(m64(), flat, rndMat(r)));
  }
  for (const name of Object.keys(cases)) {
    const o = sentinel();
    T.eq(M4.invert(o, cases[name]), false, name);
    T.ok(untouched(o), name + ': output untouched');
  }
  const self = M4.scaling(m64(), 2, 0, 2), before = Array.from(self);
  T.eq(M4.invert(self, self), false);
  T.eq(Array.from(self), before, 'in place: unchanged when singular');
  // small but honest matrices are not mistaken for singular
  const o = m64();
  T.eq(M4.invert(o, M4.scaling(m64(), 1e-6, 1e-6, 1e-6)), true, 'tiny uniform scale');
  mnear(o, M4.scaling(m64(), 1e6, 1e6, 1e6), 1e-6);
  T.eq(M4.invert(o, M4.scaling(m64(), 1e-12, 1e9, 1)), true, 'very uneven scale');
  T.eq(M4.invert(o, M4.mul(m64(), M4.translation(m64(), 1e5, -1e5, 1e5), M4.scaling(m64(), 1e-3, 1e-3, 1e-3))), true, 'far translation of a small object');
  T.eq(M4.invert(o, M4.perspective(m64(), 1, 1.6, 1e-3, 1e6)), true, 'extreme depth range');
  T.eq(M4.invert(o, M4.perspective(m64(), 1, 1.6, 0.1, Infinity)), true, 'infinite far plane');
  T.eq(M4.invert(o, M4.ortho(m64(), -300, 300, -300, 300, 1, 2000)), true, 'shadow-map style ortho');
  // the singularity test is relative to the size of each column, whichever column is small or large
  for (let col = 0; col < 4; col++) {
    for (const s of [1e-9, 1e9]) {
      const m = M4.identity(m64());
      m[col * 4 + col] = s;
      T.eq(M4.invert(o, m), true, 'column ' + col + ' scaled by ' + s);
      T.near(o[col * 4 + col] * s, 1, 1e-12);
      const g = rndMat(r);
      for (let k = 0; k < 4; k++) g[col * 4 + k] *= s;
      T.eq(M4.invert(o, g), true, 'random matrix with column ' + col + ' scaled by ' + s);
    }
  }
});

T.test('M4.transformPoint / transformDir', () => {
  const o = [0, 0, 0];
  const Tm = M4.translation(m64(), 5, 6, 7);
  T.ok(M4.transformPoint(o, Tm, [1, 2, 3]) === o);
  T.eq(o, [6, 8, 10], 'points are translated');
  T.ok(M4.transformDir(o, Tm, [1, 2, 3]) === o);
  T.eq(o, [1, 2, 3], 'directions are not');
  const S = M4.scaling(m64(), 2, 3, 4);
  T.eq(M4.transformDir([0, 0, 0], S, [1, 1, 1]), [2, 3, 4]);
  // perspective divide: m = perspective(90°, 1, 1, 3); (2, 1, −2) → clip (2, 1, 1, 2) → (1, 0.5, 0.5)
  const P = M4.perspective(m64(), Math.PI / 2, 1, 1, 3);
  vnear(M4.transformPoint([0, 0, 0], P, [2, 1, -2]), [1, 0.5, 0.5], 1e-12, 'divides by w');
  vnear(M4.transformDir([0, 0, 0], P, [2, 1, -2]), [2, 1, 4], 1e-12, 'transformDir does not divide');
  // w = 0 (a point in the plane of the eye): no Infinity / NaN
  const w0 = M4.transformPoint([0, 0, 0], P, [1, 2, 0]);
  T.ok(Number.isFinite(w0[0]) && Number.isFinite(w0[1]) && Number.isFinite(w0[2]), 'w = 0 stays finite');
  vnear(w0, [1, 2, -3], 1e-12, 'w = 0: the undivided clip coordinates');
  T.eq(M4.transformPoint([0, 0, 0], [2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 2, 0, 1, 1, 1, 0], [3, 4, 5]), [7, 9, 11], 'w = 0 exactly: no divide');
  const r = U.rng(24);
  for (let i = 0; i < 200; i++) {
    const m = rndMat(r), v = rndVec(r, 5);
    const h = apply4(m, [v[0], v[1], v[2], 1]);
    vnear(M4.transformPoint([0, 0, 0], m, v), [h[0] / h[3], h[1] / h[3], h[2] / h[3]], 1e-9 * Math.max(1, Math.abs(1 / h[3])), 'point');
    const d = apply4(m, [v[0], v[1], v[2], 0]);
    vnear(M4.transformDir([0, 0, 0], m, v), d, 1e-12, 'direction');
    const x = v.slice(); M4.transformPoint(x, m, x);
    vnear(x, M4.transformPoint([0, 0, 0], m, v), 0, 'point: o = v');
    const y = v.slice(); M4.transformDir(y, m, y);
    vnear(y, d, 1e-12, 'direction: o = v');
    const f = M4.transformPoint(V3.create(), Float32Array.from(m), V3.create(v[0], v[1], v[2]));
    T.ok(f instanceof Float32Array);
  }
});

T.test('M4.rotationAxis: hand-computed, right-handed, agrees with V3.rotAxis', () => {
  const o = M4.create();
  T.ok(M4.rotationAxis(o, [0, 0, 1], Math.PI / 2) === o);
  mnear(o, [0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1], 1e-7, 'about Z by 90°: x → y, y → −x');
  mnear(M4.rotationAxis(m64(), [1, 0, 0], Math.PI / 2), [1, 0, 0, 0, 0, 0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 1], 1e-15, 'about X: y → z');
  mnear(M4.rotationAxis(m64(), [0, 1, 0], Math.PI / 2), [0, 0, -1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1], 1e-15, 'about Y: z → x');
  mnear(M4.rotationAxis(m64(), [0, 1, 0], 0), IDENT, 0, 'zero angle');
  const c = Math.cos(0.5), s = Math.sin(0.5);
  mnear(M4.rotationAxis(m64(), [0, 0, 1], 0.5), [c, s, 0, 0, -s, c, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1], 1e-15);
  const dirty = new Float32Array(16).fill(3);
  M4.rotationAxis(dirty, [0, 0, 1], Math.PI / 2);
  mnear(dirty, o, 0, 'overwrites every element');
  const r = U.rng(25);
  for (let i = 0; i < 300; i++) {
    const k = rndUnit(r), ang = r.range(-7, 7), v = rndVec(r, 5);
    const m = M4.rotationAxis(m64(), k, ang);
    vnear(M4.transformDir([0, 0, 0], m, v), V3.rotAxis([0, 0, 0], v, k, ang), 1e-12, 'same as V3.rotAxis');
    vnear(M4.transformPoint([0, 0, 0], m, v), V3.rotAxis([0, 0, 0], v, k, ang), 1e-12, 'no translation');
    vnear(M4.transformDir([0, 0, 0], m, k), k, 1e-12, 'axis is fixed');
    const inv = m64();
    T.eq(M4.invert(inv, m), true);
    mnear(inv, M4.rotationAxis(m64(), k, -ang), 1e-12, 'inverse is the opposite rotation');
    // orthonormal: inverse = transpose
    for (let a = 0; a < 3; a++) for (let b = 0; b < 3; b++) T.near(inv[a * 4 + b], m[b * 4 + a], 1e-12, 'transpose');
    const a2 = r.range(-3, 3);
    mnear(M4.mul(m64(), M4.rotationAxis(m64(), k, a2), m), M4.rotationAxis(m64(), k, ang + a2), 1e-12, 'angles add');
    const kk = Float64Array.from([k[0], k[1], k[2], 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]);
    M4.rotationAxis(kk, kk, ang);
    mnear(kk, m, 0, 'o = axis');
  }
});

T.test('M4.fromBasis: columns are the axes and the origin', () => {
  const o = M4.create();
  T.ok(M4.fromBasis(o, [1, 2, 3], [4, 5, 6], [7, 8, 9], [10, 11, 12]) === o);
  T.eq(Array.from(o), [1, 2, 3, 0, 4, 5, 6, 0, 7, 8, 9, 0, 10, 11, 12, 1]);
  const p = [0, 0, 0];
  T.eq(M4.transformPoint(p, o, [0, 0, 0]), [10, 11, 12], 'origin');
  T.eq(M4.transformPoint([0, 0, 0], o, [1, 0, 0]), [11, 13, 15], 'origin + x axis');
  T.eq(M4.transformPoint([0, 0, 0], o, [0, 1, 0]), [14, 16, 18], 'origin + y axis');
  T.eq(M4.transformPoint([0, 0, 0], o, [0, 0, 1]), [17, 19, 21], 'origin + z axis');
  T.eq(M4.transformDir([0, 0, 0], o, [2, -1, 1]), [5, 7, 9], 'directions ignore the origin');
  mnear(M4.fromBasis(m64(), [1, 0, 0], [0, 1, 0], [0, 0, 1], [0, 0, 0]), IDENT, 0, 'standard basis');
  // typical use: stand an object on the planet surface at direction n
  const r = U.rng(26);
  for (let i = 0; i < 200; i++) {
    const n = rndUnit(r), e = [0, 0, 0], nth = [0, 0, 0];
    V3.basis(n, e, nth);
    const pos = V3.scale([0, 0, 0], n, DM.R + 2.5);
    const m = M4.fromBasis(m64(), e, n, V3.scale([0, 0, 0], nth, -1), pos);   // x = east, y = up, z = south
    vnear(M4.transformPoint([0, 0, 0], m, [0, 1, 0]), V3.scale([0, 0, 0], n, DM.R + 3.5), 1e-9, 'local +Y is away from the planet');
    const X = [m[0], m[1], m[2]], Y = [m[4], m[5], m[6]], Z = [m[8], m[9], m[10]];
    vnear(V3.cross([0, 0, 0], X, Y), Z, 1e-12, 'a proper (unmirrored) frame');
    const inv = m64();
    T.eq(M4.invert(inv, m), true);
    vnear(M4.transformPoint([0, 0, 0], inv, pos), [0, 0, 0], 1e-9);
    // outputs may alias the inputs
    const a = Float64Array.from([e[0], e[1], e[2], 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]);
    M4.fromBasis(a, a, n, V3.scale([0, 0, 0], nth, -1), pos);
    mnear(a, m, 0, 'o = xAxis');
  }
});

T.test('M4: a full camera pipeline puts what is in front of the camera inside the clip cube', () => {
  const r = U.rng(27);
  const proj = M4.perspective(M4.create(), 0.9, 16 / 9, 1, 1000);
  for (let i = 0; i < 300; i++) {
    const dir = rndUnit(r);
    const eye = V3.scale(V3.create(), dir, 330);                 // orbiting the planet
    const view = M4.lookAt(M4.create(), eye, [0, 0, 0], [0, 1, 0]);
    const vp = M4.mul(M4.create(), proj, view);
    const c = M4.transformPoint(V3.create(), vp, [0, 0, 0]);
    T.near(c[0], 0, 1e-4, 'planet centre is at the screen centre');
    T.near(c[1], 0, 1e-4);
    T.ok(c[2] > -1 && c[2] < 1, 'inside the depth range');
    // the surface point facing the camera is nearer than the centre, the far side is farther
    const front = M4.transformPoint(V3.create(), vp, V3.scale(V3.create(), dir, DM.R));
    const back = M4.transformPoint(V3.create(), vp, V3.scale(V3.create(), dir, -DM.R));
    T.ok(front[2] < c[2] && c[2] < back[2], 'depth order: front < centre < back');
    // a point behind the camera has negative clip w
    const behind = V3.scale([0, 0, 0], dir, 400);
    T.ok(apply4(vp, [behind[0], behind[1], behind[2], 1])[3] < 0, 'behind the camera: w < 0');
    // the north pole is in the upper half of the screen unless we look along the axis
    if (Math.abs(dir[1]) < 0.9) T.ok(M4.transformPoint(V3.create(), vp, [0, DM.R, 0])[1] > 0, 'north is up');
  }
});
