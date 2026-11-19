// DEMIURGE — js/core/vec.js : DM.V3 and DM.M4 (FOUNDATION §2). Runs unchanged in Node and browsers.
//
//   DM.V3  create set copy add sub scale addScaled dot cross len dist norm lerp slerp angle
//          rotAxis basis
//   DM.M4  create identity copy mul perspective ortho lookAt invert transformPoint transformDir
//          translation scaling rotationAxis fromBasis
//
// Vectors are any array-like of length ≥ 3. Matrices are column-major Float32Array(16)
// (element [row r, column c] is m[c * 4 + r]) for WebGL clip space (z in −1..1), right-handed
// view space looking down −Z. The output parameter comes first and may alias any input: every
// function reads all of its inputs before it writes. Nothing here allocates except create().
//
// Cross-engine note: V3.slerp, V3.angle, V3.rotAxis, M4.rotationAxis and M4.perspective call
// Math.sin / cos / tan / atan2, which engines only approximate: measured, Node 24 and Edge 157
// disagree in the last bit for 3–23 % of arguments. Their results are deterministic on one
// engine but may differ by one unit in the last place between engines. Everything else here
// (IEEE + - * / and Math.sqrt only) is bit-identical everywhere.
(function (DM) {
  'use strict';

  const V3 = DM.V3 = DM.V3 || {};
  const M4 = DM.M4 = DM.M4 || {};

  // ================================================================ V3

  V3.create = function (x, y, z) {
    const o = new Float32Array(3);
    o[0] = x || 0; o[1] = y || 0; o[2] = z || 0;
    return o;
  };

  V3.set = function (o, x, y, z) { o[0] = x; o[1] = y; o[2] = z; return o; };
  V3.copy = function (o, a) { o[0] = a[0]; o[1] = a[1]; o[2] = a[2]; return o; };
  V3.add = function (o, a, b) { o[0] = a[0] + b[0]; o[1] = a[1] + b[1]; o[2] = a[2] + b[2]; return o; };
  V3.sub = function (o, a, b) { o[0] = a[0] - b[0]; o[1] = a[1] - b[1]; o[2] = a[2] - b[2]; return o; };
  V3.scale = function (o, a, s) { o[0] = a[0] * s; o[1] = a[1] * s; o[2] = a[2] * s; return o; };

  // o = a + b * s
  V3.addScaled = function (o, a, b, s) {
    o[0] = a[0] + b[0] * s; o[1] = a[1] + b[1] * s; o[2] = a[2] + b[2] * s;
    return o;
  };

  V3.dot = function (a, b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };

  V3.cross = function (o, a, b) {
    const ax = a[0], ay = a[1], az = a[2], bx = b[0], by = b[1], bz = b[2];
    o[0] = ay * bz - az * by;
    o[1] = az * bx - ax * bz;
    o[2] = ax * by - ay * bx;
    return o;
  };

  V3.len = function (a) { return Math.sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]); };

  V3.dist = function (a, b) {
    const x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
    return Math.sqrt(x * x + y * y + z * z);
  };

  // Normalize. A zero (or NaN) vector becomes (0,1,0).
  V3.norm = function (o, a) {
    const x = a[0], y = a[1], z = a[2];
    const l = Math.sqrt(x * x + y * y + z * z);
    if (!(l > 1e-30)) { o[0] = 0; o[1] = 1; o[2] = 0; return o; }
    const k = 1 / l;
    o[0] = x * k; o[1] = y * k; o[2] = z * k;
    return o;
  };

  V3.lerp = function (o, a, b, t) {
    const ax = a[0], ay = a[1], az = a[2];
    o[0] = ax + (b[0] - ax) * t;
    o[1] = ay + (b[1] - ay) * t;
    o[2] = az + (b[2] - az) * t;
    return o;
  };

  // Great-circle interpolation of unit vectors. The result is always unit length.
  //  - t = 0 and t = 1 return exact copies of a and b; other t (including outside 0..1) follow
  //    the great circle through a and b at constant angular speed.
  //  - Nearly parallel inputs (angle < 1e-5 rad) fall back to normalized lerp, which differs
  //    from the exact arc by less than 1e-15.
  //  - Exactly opposite inputs have no unique great circle: a deterministic one is chosen
  //    (through the coordinate axis least aligned with a). No input produces NaN.
  //  - Inputs that are only unit to float32 precision are handled exactly (the construction
  //    does not assume |a| = 1).
  V3.slerp = function (o, a, b, t) {
    const ax = a[0], ay = a[1], az = a[2], bx = b[0], by = b[1], bz = b[2];
    if (t === 0) { o[0] = ax; o[1] = ay; o[2] = az; return o; }
    if (t === 1) { o[0] = bx; o[1] = by; o[2] = bz; return o; }
    const aa = ax * ax + ay * ay + az * az;
    const d = ax * bx + ay * by + az * bz;                     // |a||b| cos θ
    // v = part of b perpendicular to a; |v| = |b| sin θ
    let q = aa > 1e-30 ? d / aa : 0;
    let vx = bx - ax * q, vy = by - ay * q, vz = bz - az * q;
    // Second Gram–Schmidt pass: when b ≈ −a the subtraction above cancels almost everything
    // and leaves a round-off component along a, which would shift the point along the arc.
    q = aa > 1e-30 ? (vx * ax + vy * ay + vz * az) / aa : 0;
    vx -= ax * q; vy -= ay * q; vz -= az * q;
    let s = Math.sqrt(vx * vx + vy * vy + vz * vz);
    let rx, ry, rz;
    if ((s < 1e-5 && d >= 0) || !(aa > 1e-30)) {
      rx = ax + (bx - ax) * t; ry = ay + (by - ay) * t; rz = az + (bz - az) * t;
    } else {
      const la = Math.sqrt(aa);
      const th = Math.atan2(s * la, d);
      if (s < 1e-10) {
        // Opposite to within round-off, so v is noise: use cross(a, e) instead, with e the
        // coordinate axis along which |a| is smallest.
        const fx = ax < 0 ? -ax : ax, fy = ay < 0 ? -ay : ay, fz = az < 0 ? -az : az;
        if (fx <= fy && fx <= fz) { vx = 0; vy = az; vz = -ay; }
        else if (fy <= fz) { vx = -az; vy = 0; vz = ax; }
        else { vx = ay; vy = -ax; vz = 0; }
        s = Math.sqrt(vx * vx + vy * vy + vz * vz);
      }
      const c = Math.cos(t * th) / la, k = Math.sin(t * th) / s;
      rx = ax * c + vx * k; ry = ay * c + vy * k; rz = az * c + vz * k;
    }
    const l = Math.sqrt(rx * rx + ry * ry + rz * rz);
    if (!(l > 1e-30)) { o[0] = ax; o[1] = ay; o[2] = az; return o; }
    const il = 1 / l;
    o[0] = rx * il; o[1] = ry * il; o[2] = rz * il;
    return o;
  };

  // Angle in radians between two unit vectors. atan2(|a×b|, a·b) stays accurate for tiny angles
  // and near π, where acos(dot) loses most of its digits; it also never returns NaN.
  V3.angle = function (a, b) {
    const ax = a[0], ay = a[1], az = a[2], bx = b[0], by = b[1], bz = b[2];
    const cx = ay * bz - az * by, cy = az * bx - ax * bz, cz = ax * by - ay * bx;
    return Math.atan2(Math.sqrt(cx * cx + cy * cy + cz * cz), ax * bx + ay * by + az * bz);
  };

  // Rodrigues rotation of a about a unit axis by ang radians (right-handed: counter-clockwise
  // when the axis points at the viewer).
  V3.rotAxis = function (o, a, axis, ang) {
    const x = a[0], y = a[1], z = a[2], kx = axis[0], ky = axis[1], kz = axis[2];
    const c = Math.cos(ang), s = Math.sin(ang);
    const kd = (kx * x + ky * y + kz * z) * (1 - c);
    o[0] = x * c + (ky * z - kz * y) * s + kx * kd;
    o[1] = y * c + (kz * x - kx * z) * s + ky * kd;
    o[2] = z * c + (kx * y - ky * x) * s + kz * kd;
    return o;
  };

  // Local tangent frame at unit direction n: east = normalize(cross(Y, n)) = normalize(nz, 0, −nx),
  // north = cross(n, east). (east, north, n) is right-handed, and seen from outside the planet
  // with north up, east points right. At the poles east = +X. Returns outEast.
  V3.basis = function (n, outEast, outNorth) {
    const x = n[0], y = n[1], z = n[2];
    let ex = z, ez = -x;
    const l = Math.sqrt(ex * ex + ez * ez);
    if (l < 1e-6) { ex = 1; ez = 0; } else { ex /= l; ez /= l; }
    outEast[0] = ex; outEast[1] = 0; outEast[2] = ez;
    outNorth[0] = y * ez;
    outNorth[1] = z * ex - x * ez;
    outNorth[2] = -y * ex;
    return outEast;
  };

  // ================================================================ M4

  M4.create = function () {
    const o = new Float32Array(16);
    o[0] = o[5] = o[10] = o[15] = 1;
    return o;
  };

  M4.identity = function (o) {
    o[0] = 1; o[1] = 0; o[2] = 0; o[3] = 0;
    o[4] = 0; o[5] = 1; o[6] = 0; o[7] = 0;
    o[8] = 0; o[9] = 0; o[10] = 1; o[11] = 0;
    o[12] = 0; o[13] = 0; o[14] = 0; o[15] = 1;
    return o;
  };

  M4.copy = function (o, a) {
    for (let i = 0; i < 16; i++) o[i] = a[i];
    return o;
  };

  // o = a · b (b is applied first to a column vector). o may alias a and/or b.
  M4.mul = function (o, a, b) {
    const a00 = a[0], a01 = a[1], a02 = a[2], a03 = a[3];
    const a10 = a[4], a11 = a[5], a12 = a[6], a13 = a[7];
    const a20 = a[8], a21 = a[9], a22 = a[10], a23 = a[11];
    const a30 = a[12], a31 = a[13], a32 = a[14], a33 = a[15];
    const b00 = b[0], b01 = b[1], b02 = b[2], b03 = b[3];
    const b10 = b[4], b11 = b[5], b12 = b[6], b13 = b[7];
    const b20 = b[8], b21 = b[9], b22 = b[10], b23 = b[11];
    const b30 = b[12], b31 = b[13], b32 = b[14], b33 = b[15];
    o[0] = a00 * b00 + a10 * b01 + a20 * b02 + a30 * b03;
    o[1] = a01 * b00 + a11 * b01 + a21 * b02 + a31 * b03;
    o[2] = a02 * b00 + a12 * b01 + a22 * b02 + a32 * b03;
    o[3] = a03 * b00 + a13 * b01 + a23 * b02 + a33 * b03;
    o[4] = a00 * b10 + a10 * b11 + a20 * b12 + a30 * b13;
    o[5] = a01 * b10 + a11 * b11 + a21 * b12 + a31 * b13;
    o[6] = a02 * b10 + a12 * b11 + a22 * b12 + a32 * b13;
    o[7] = a03 * b10 + a13 * b11 + a23 * b12 + a33 * b13;
    o[8] = a00 * b20 + a10 * b21 + a20 * b22 + a30 * b23;
    o[9] = a01 * b20 + a11 * b21 + a21 * b22 + a31 * b23;
    o[10] = a02 * b20 + a12 * b21 + a22 * b22 + a32 * b23;
    o[11] = a03 * b20 + a13 * b21 + a23 * b22 + a33 * b23;
    o[12] = a00 * b30 + a10 * b31 + a20 * b32 + a30 * b33;
    o[13] = a01 * b30 + a11 * b31 + a21 * b32 + a31 * b33;
    o[14] = a02 * b30 + a12 * b31 + a22 * b32 + a32 * b33;
    o[15] = a03 * b30 + a13 * b31 + a23 * b32 + a33 * b33;
    return o;
  };

  // Right-handed perspective projection: the camera looks down −Z, view z = −near maps to
  // clip z/w = −1 and z = −far to +1. `far` may be Infinity.
  M4.perspective = function (o, fovyRad, aspect, near, far) {
    const f = 1 / Math.tan(fovyRad / 2);
    o[0] = f / aspect; o[1] = 0; o[2] = 0; o[3] = 0;
    o[4] = 0; o[5] = f; o[6] = 0; o[7] = 0;
    o[8] = 0; o[9] = 0; o[11] = -1;
    o[12] = 0; o[13] = 0; o[15] = 0;
    if (far === Infinity) {
      o[10] = -1;
      o[14] = -2 * near;
    } else {
      const nf = 1 / (near - far);
      o[10] = (far + near) * nf;
      o[14] = 2 * far * near * nf;
    }
    return o;
  };

  // Orthographic projection of the view-space box [left,right] × [bottom,top] × [−near,−far]
  // onto the clip cube.
  M4.ortho = function (o, left, right, bottom, top, near, far) {
    const lr = 1 / (left - right), bt = 1 / (bottom - top), nf = 1 / (near - far);
    o[0] = -2 * lr; o[1] = 0; o[2] = 0; o[3] = 0;
    o[4] = 0; o[5] = -2 * bt; o[6] = 0; o[7] = 0;
    o[8] = 0; o[9] = 0; o[10] = 2 * nf; o[11] = 0;
    o[12] = (left + right) * lr;
    o[13] = (top + bottom) * bt;
    o[14] = (far + near) * nf;
    o[15] = 1;
    return o;
  };

  // View matrix for a camera at `eye` looking at `target`: the target ends up on the −Z axis,
  // `up` in the +Y half of the YZ plane. Degenerate inputs are handled: eye = target gives a
  // pure translation, and an `up` parallel to the view direction is replaced by a world axis.
  M4.lookAt = function (o, eye, target, up) {
    const ex = eye[0], ey = eye[1], ez = eye[2];
    let ux = up[0], uy = up[1], uz = up[2];
    // z axis = from target to eye
    let zx = ex - target[0], zy = ey - target[1], zz = ez - target[2];
    let l = Math.sqrt(zx * zx + zy * zy + zz * zz);
    if (l > 1e-30) { l = 1 / l; zx *= l; zy *= l; zz *= l; } else { zx = 0; zy = 0; zz = 1; }
    // x axis = up × z
    let xx = uy * zz - uz * zy, xy = uz * zx - ux * zz, xz = ux * zy - uy * zx;
    l = Math.sqrt(xx * xx + xy * xy + xz * xz);
    const ul = Math.sqrt(ux * ux + uy * uy + uz * uz);
    if (!(l > 1e-6 * ul)) {
      // up is zero or parallel to the view direction: use the world axis least aligned with z
      const fx = zx < 0 ? -zx : zx, fy = zy < 0 ? -zy : zy, fz = zz < 0 ? -zz : zz;
      if (fy <= fx && fy <= fz) { ux = 0; uy = 1; uz = 0; }
      else if (fz <= fx) { ux = 0; uy = 0; uz = 1; }
      else { ux = 1; uy = 0; uz = 0; }
      xx = uy * zz - uz * zy; xy = uz * zx - ux * zz; xz = ux * zy - uy * zx;
      l = Math.sqrt(xx * xx + xy * xy + xz * xz);
    }
    l = 1 / l; xx *= l; xy *= l; xz *= l;
    // y axis = z × x
    const yx = zy * xz - zz * xy, yy = zz * xx - zx * xz, yz = zx * xy - zy * xx;
    o[0] = xx; o[1] = yx; o[2] = zx; o[3] = 0;
    o[4] = xy; o[5] = yy; o[6] = zy; o[7] = 0;
    o[8] = xz; o[9] = yz; o[10] = zz; o[11] = 0;
    o[12] = -(xx * ex + xy * ey + xz * ez);
    o[13] = -(yx * ex + yy * ey + yz * ez);
    o[14] = -(zx * ex + zy * ey + zz * ez);
    o[15] = 1;
    return o;
  };

  // General 4×4 inverse (cofactor expansion, evaluated in double precision). Returns false and
  // leaves o untouched if a is singular: determinant zero, not finite, or below 1e-12 of the
  // product of the column lengths (the largest value the determinant could have).
  M4.invert = function (o, a) {
    const a00 = a[0], a01 = a[1], a02 = a[2], a03 = a[3];
    const a10 = a[4], a11 = a[5], a12 = a[6], a13 = a[7];
    const a20 = a[8], a21 = a[9], a22 = a[10], a23 = a[11];
    const a30 = a[12], a31 = a[13], a32 = a[14], a33 = a[15];
    const b00 = a00 * a11 - a01 * a10, b01 = a00 * a12 - a02 * a10;
    const b02 = a00 * a13 - a03 * a10, b03 = a01 * a12 - a02 * a11;
    const b04 = a01 * a13 - a03 * a11, b05 = a02 * a13 - a03 * a12;
    const b06 = a20 * a31 - a21 * a30, b07 = a20 * a32 - a22 * a30;
    const b08 = a20 * a33 - a23 * a30, b09 = a21 * a32 - a22 * a31;
    const b10 = a21 * a33 - a23 * a31, b11 = a22 * a33 - a23 * a32;
    const det = b00 * b11 - b01 * b10 + b02 * b09 + b03 * b08 - b04 * b07 + b05 * b06;
    const bound = Math.sqrt(
      (a00 * a00 + a01 * a01 + a02 * a02 + a03 * a03) * (a10 * a10 + a11 * a11 + a12 * a12 + a13 * a13) *
      (a20 * a20 + a21 * a21 + a22 * a22 + a23 * a23) * (a30 * a30 + a31 * a31 + a32 * a32 + a33 * a33));
    if (!(Math.abs(det) > 1e-12 * bound) || !Number.isFinite(det)) return false;
    const id = 1 / det;
    o[0] = (a11 * b11 - a12 * b10 + a13 * b09) * id;
    o[1] = (a02 * b10 - a01 * b11 - a03 * b09) * id;
    o[2] = (a31 * b05 - a32 * b04 + a33 * b03) * id;
    o[3] = (a22 * b04 - a21 * b05 - a23 * b03) * id;
    o[4] = (a12 * b08 - a10 * b11 - a13 * b07) * id;
    o[5] = (a00 * b11 - a02 * b08 + a03 * b07) * id;
    o[6] = (a32 * b02 - a30 * b05 - a33 * b01) * id;
    o[7] = (a20 * b05 - a22 * b02 + a23 * b01) * id;
    o[8] = (a10 * b10 - a11 * b08 + a13 * b06) * id;
    o[9] = (a01 * b08 - a00 * b10 - a03 * b06) * id;
    o[10] = (a30 * b04 - a31 * b02 + a33 * b00) * id;
    o[11] = (a21 * b02 - a20 * b04 - a23 * b00) * id;
    o[12] = (a11 * b07 - a10 * b09 - a12 * b06) * id;
    o[13] = (a00 * b09 - a01 * b07 + a02 * b06) * id;
    o[14] = (a31 * b01 - a30 * b03 - a32 * b00) * id;
    o[15] = (a20 * b03 - a21 * b01 + a22 * b00) * id;
    return true;
  };

  // v as a point (w = 1), with the perspective divide. (A point exactly on the w = 0 plane is
  // returned undivided rather than as infinities.)
  M4.transformPoint = function (o, m, v) {
    const x = v[0], y = v[1], z = v[2];
    const w = m[3] * x + m[7] * y + m[11] * z + m[15];
    const iw = w !== 0 ? 1 / w : 1;
    o[0] = (m[0] * x + m[4] * y + m[8] * z + m[12]) * iw;
    o[1] = (m[1] * x + m[5] * y + m[9] * z + m[13]) * iw;
    o[2] = (m[2] * x + m[6] * y + m[10] * z + m[14]) * iw;
    return o;
  };

  // v as a direction (w = 0): upper-left 3×3 only, no translation, no divide.
  M4.transformDir = function (o, m, v) {
    const x = v[0], y = v[1], z = v[2];
    o[0] = m[0] * x + m[4] * y + m[8] * z;
    o[1] = m[1] * x + m[5] * y + m[9] * z;
    o[2] = m[2] * x + m[6] * y + m[10] * z;
    return o;
  };

  M4.translation = function (o, x, y, z) {
    o[0] = 1; o[1] = 0; o[2] = 0; o[3] = 0;
    o[4] = 0; o[5] = 1; o[6] = 0; o[7] = 0;
    o[8] = 0; o[9] = 0; o[10] = 1; o[11] = 0;
    o[12] = x; o[13] = y; o[14] = z; o[15] = 1;
    return o;
  };

  M4.scaling = function (o, x, y, z) {
    o[0] = x; o[1] = 0; o[2] = 0; o[3] = 0;
    o[4] = 0; o[5] = y; o[6] = 0; o[7] = 0;
    o[8] = 0; o[9] = 0; o[10] = z; o[11] = 0;
    o[12] = 0; o[13] = 0; o[14] = 0; o[15] = 1;
    return o;
  };

  // Rotation by ang radians about a unit axis (right-handed; matches V3.rotAxis).
  M4.rotationAxis = function (o, unitAxis, ang) {
    const x = unitAxis[0], y = unitAxis[1], z = unitAxis[2];
    const c = Math.cos(ang), s = Math.sin(ang), t = 1 - c;
    o[0] = x * x * t + c; o[1] = y * x * t + z * s; o[2] = z * x * t - y * s; o[3] = 0;
    o[4] = x * y * t - z * s; o[5] = y * y * t + c; o[6] = z * y * t + x * s; o[7] = 0;
    o[8] = x * z * t + y * s; o[9] = y * z * t - x * s; o[10] = z * z * t + c; o[11] = 0;
    o[12] = 0; o[13] = 0; o[14] = 0; o[15] = 1;
    return o;
  };

  // Matrix whose columns are the three axes and the origin: maps local (x,y,z) to
  // origin + x·xAxis + y·yAxis + z·zAxis.
  M4.fromBasis = function (o, xAxis, yAxis, zAxis, origin) {
    const xx = xAxis[0], xy = xAxis[1], xz = xAxis[2];
    const yx = yAxis[0], yy = yAxis[1], yz = yAxis[2];
    const zx = zAxis[0], zy = zAxis[1], zz = zAxis[2];
    const px = origin[0], py = origin[1], pz = origin[2];
    o[0] = xx; o[1] = xy; o[2] = xz; o[3] = 0;
    o[4] = yx; o[5] = yy; o[6] = yz; o[7] = 0;
    o[8] = zx; o[9] = zy; o[10] = zz; o[11] = 0;
    o[12] = px; o[13] = py; o[14] = pz; o[15] = 1;
    return o;
  };
})(globalThis.DM = globalThis.DM || {});
