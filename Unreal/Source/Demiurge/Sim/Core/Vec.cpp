// DEMIURGE — Sim/Core/Vec.cpp : see Vec.h (port of js/core/vec.js).
#include "Core/Vec.h"

namespace dm {
namespace v3 {

Vec3 norm(const Vec3& a) {
	const double l = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
	if (!(l > 1e-30)) return { 0, 1, 0 };
	const double k = 1 / l;
	return { a.x * k, a.y * k, a.z * k };
}

Vec3 slerp(const Vec3& a, const Vec3& b, double t) {
	const double ax = a.x, ay = a.y, az = a.z, bx = b.x, by = b.y, bz = b.z;
	if (t == 0) return a;
	if (t == 1) return b;
	const double aa = ax * ax + ay * ay + az * az;
	const double d = ax * bx + ay * by + az * bz;
	double q = aa > 1e-30 ? d / aa : 0;
	double vx = bx - ax * q, vy = by - ay * q, vz = bz - az * q;
	q = aa > 1e-30 ? (vx * ax + vy * ay + vz * az) / aa : 0;      // second Gram-Schmidt pass
	vx -= ax * q; vy -= ay * q; vz -= az * q;
	double s = std::sqrt(vx * vx + vy * vy + vz * vz);
	double rx, ry, rz;
	if ((s < 1e-5 && d >= 0) || !(aa > 1e-30)) {
		rx = ax + (bx - ax) * t; ry = ay + (by - ay) * t; rz = az + (bz - az) * t;
	} else {
		const double la = std::sqrt(aa);
		const double th = std::atan2(s * la, d);
		if (s < 1e-10) {
			// opposite to within round-off: use cross(a, e), e = the axis along which |a| is smallest
			const double fx = std::fabs(ax), fy = std::fabs(ay), fz = std::fabs(az);
			if (fx <= fy && fx <= fz) { vx = 0; vy = az; vz = -ay; }
			else if (fy <= fz) { vx = -az; vy = 0; vz = ax; }
			else { vx = ay; vy = -ax; vz = 0; }
			s = std::sqrt(vx * vx + vy * vy + vz * vz);
		}
		const double c = std::cos(t * th) / la, k = std::sin(t * th) / s;
		rx = ax * c + vx * k; ry = ay * c + vy * k; rz = az * c + vz * k;
	}
	const double l = std::sqrt(rx * rx + ry * ry + rz * rz);
	if (!(l > 1e-30)) return a;
	const double il = 1 / l;
	return { rx * il, ry * il, rz * il };
}

double angle(const Vec3& a, const Vec3& b) {
	const Vec3 c = cross(a, b);
	return std::atan2(std::sqrt(c.x * c.x + c.y * c.y + c.z * c.z), dot(a, b));
}

Vec3 rotAxis(const Vec3& a, const Vec3& axis, double ang) {
	const double x = a.x, y = a.y, z = a.z, kx = axis.x, ky = axis.y, kz = axis.z;
	const double c = std::cos(ang), s = std::sin(ang);
	const double kd = (kx * x + ky * y + kz * z) * (1 - c);
	return { x * c + (ky * z - kz * y) * s + kx * kd,
	         y * c + (kz * x - kx * z) * s + ky * kd,
	         z * c + (kx * y - ky * x) * s + kz * kd };
}

void basis(const Vec3& n, Vec3& east, Vec3& north) {
	const double x = n.x, y = n.y, z = n.z;
	double ex = z, ez = -x;
	const double l = std::sqrt(ex * ex + ez * ez);
	if (l < 1e-6) { ex = 1; ez = 0; } else { ex /= l; ez /= l; }
	east = { ex, 0, ez };
	north = { y * ez, z * ex - x * ez, -y * ex };
}

} // namespace v3

namespace m4 {

Mat4 mul(const Mat4& A, const Mat4& B) {
	Mat4 o;
	for (int c = 0; c < 4; c++) {
		for (int r = 0; r < 4; r++) {
			o.m[size_t(c * 4 + r)] = A.m[size_t(r)] * B.m[size_t(c * 4)] + A.m[size_t(4 + r)] * B.m[size_t(c * 4 + 1)] +
			                        A.m[size_t(8 + r)] * B.m[size_t(c * 4 + 2)] + A.m[size_t(12 + r)] * B.m[size_t(c * 4 + 3)];
		}
	}
	return o;
}

Mat4 perspective(double fovyRad, double aspect, double zNear, double zFar) {
	const double f = 1 / std::tan(fovyRad / 2);
	Mat4 o;
	o.m = { f / aspect, 0, 0, 0, 0, f, 0, 0, 0, 0, 0, -1, 0, 0, 0, 0 };
	if (std::isinf(zFar)) {
		o.m[10] = -1;
		o.m[14] = -2 * zNear;
	} else {
		const double nf = 1 / (zNear - zFar);
		o.m[10] = (zFar + zNear) * nf;
		o.m[14] = 2 * zFar * zNear * nf;
	}
	return o;
}

Mat4 ortho(double left, double right, double bottom, double top, double zNear, double zFar) {
	const double lr = 1 / (left - right), bt = 1 / (bottom - top), nf = 1 / (zNear - zFar);
	Mat4 o;
	o.m = { -2 * lr, 0, 0, 0, 0, -2 * bt, 0, 0, 0, 0, 2 * nf, 0,
	        (left + right) * lr, (top + bottom) * bt, (zFar + zNear) * nf, 1 };
	return o;
}

Mat4 lookAt(const Vec3& eye, const Vec3& target, const Vec3& up) {
	const double ex = eye.x, ey = eye.y, ez = eye.z;
	double ux = up.x, uy = up.y, uz = up.z;
	double zx = ex - target.x, zy = ey - target.y, zz = ez - target.z;
	double l = std::sqrt(zx * zx + zy * zy + zz * zz);
	if (l > 1e-30) { l = 1 / l; zx *= l; zy *= l; zz *= l; } else { zx = 0; zy = 0; zz = 1; }
	double xx = uy * zz - uz * zy, xy = uz * zx - ux * zz, xz = ux * zy - uy * zx;
	l = std::sqrt(xx * xx + xy * xy + xz * xz);
	const double ul = std::sqrt(ux * ux + uy * uy + uz * uz);
	if (!(l > 1e-6 * ul)) {
		const double fx = std::fabs(zx), fy = std::fabs(zy), fz = std::fabs(zz);
		if (fy <= fx && fy <= fz) { ux = 0; uy = 1; uz = 0; }
		else if (fz <= fx) { ux = 0; uy = 0; uz = 1; }
		else { ux = 1; uy = 0; uz = 0; }
		xx = uy * zz - uz * zy; xy = uz * zx - ux * zz; xz = ux * zy - uy * zx;
		l = std::sqrt(xx * xx + xy * xy + xz * xz);
	}
	l = 1 / l; xx *= l; xy *= l; xz *= l;
	const double yx = zy * xz - zz * xy, yy = zz * xx - zx * xz, yz = zx * xy - zy * xx;
	Mat4 o;
	o.m = { xx, yx, zx, 0, xy, yy, zy, 0, xz, yz, zz, 0,
	        -(xx * ex + xy * ey + xz * ez), -(yx * ex + yy * ey + yz * ez), -(zx * ex + zy * ey + zz * ez), 1 };
	return o;
}

bool invert(Mat4& out, const Mat4& A) {
	const auto& a = A.m;
	const double a00 = a[0], a01 = a[1], a02 = a[2], a03 = a[3];
	const double a10 = a[4], a11 = a[5], a12 = a[6], a13 = a[7];
	const double a20 = a[8], a21 = a[9], a22 = a[10], a23 = a[11];
	const double a30 = a[12], a31 = a[13], a32 = a[14], a33 = a[15];
	const double b00 = a00 * a11 - a01 * a10, b01 = a00 * a12 - a02 * a10;
	const double b02 = a00 * a13 - a03 * a10, b03 = a01 * a12 - a02 * a11;
	const double b04 = a01 * a13 - a03 * a11, b05 = a02 * a13 - a03 * a12;
	const double b06 = a20 * a31 - a21 * a30, b07 = a20 * a32 - a22 * a30;
	const double b08 = a20 * a33 - a23 * a30, b09 = a21 * a32 - a22 * a31;
	const double b10 = a21 * a33 - a23 * a31, b11 = a22 * a33 - a23 * a32;
	const double det = b00 * b11 - b01 * b10 + b02 * b09 + b03 * b08 - b04 * b07 + b05 * b06;
	const double bound = std::sqrt(
		(a00 * a00 + a01 * a01 + a02 * a02 + a03 * a03) * (a10 * a10 + a11 * a11 + a12 * a12 + a13 * a13) *
		(a20 * a20 + a21 * a21 + a22 * a22 + a23 * a23) * (a30 * a30 + a31 * a31 + a32 * a32 + a33 * a33));
	if (!(std::fabs(det) > 1e-12 * bound) || !std::isfinite(det)) return false;
	const double id = 1 / det;
	out.m = {
		(a11 * b11 - a12 * b10 + a13 * b09) * id, (a02 * b10 - a01 * b11 - a03 * b09) * id,
		(a31 * b05 - a32 * b04 + a33 * b03) * id, (a22 * b04 - a21 * b05 - a23 * b03) * id,
		(a12 * b08 - a10 * b11 - a13 * b07) * id, (a00 * b11 - a02 * b08 + a03 * b07) * id,
		(a32 * b02 - a30 * b05 - a33 * b01) * id, (a20 * b05 - a22 * b02 + a23 * b01) * id,
		(a10 * b10 - a11 * b08 + a13 * b06) * id, (a01 * b08 - a00 * b10 - a03 * b06) * id,
		(a30 * b04 - a31 * b02 + a33 * b00) * id, (a21 * b02 - a20 * b04 - a23 * b00) * id,
		(a11 * b07 - a10 * b09 - a12 * b06) * id, (a00 * b09 - a01 * b07 + a02 * b06) * id,
		(a31 * b01 - a30 * b03 - a32 * b00) * id, (a20 * b03 - a21 * b01 + a22 * b00) * id };
	return true;
}

Vec3 transformPoint(const Mat4& M, const Vec3& v) {
	const auto& m = M.m;
	const double w = m[3] * v.x + m[7] * v.y + m[11] * v.z + m[15];
	const double iw = w != 0 ? 1 / w : 1;
	return { (m[0] * v.x + m[4] * v.y + m[8] * v.z + m[12]) * iw,
	         (m[1] * v.x + m[5] * v.y + m[9] * v.z + m[13]) * iw,
	         (m[2] * v.x + m[6] * v.y + m[10] * v.z + m[14]) * iw };
}

Vec3 transformDir(const Mat4& M, const Vec3& v) {
	const auto& m = M.m;
	return { m[0] * v.x + m[4] * v.y + m[8] * v.z, m[1] * v.x + m[5] * v.y + m[9] * v.z, m[2] * v.x + m[6] * v.y + m[10] * v.z };
}

Mat4 translation(double x, double y, double z) {
	Mat4 o;
	o.m[12] = x; o.m[13] = y; o.m[14] = z;
	return o;
}

Mat4 scaling(double x, double y, double z) {
	Mat4 o;
	o.m[0] = x; o.m[5] = y; o.m[10] = z;
	return o;
}

Mat4 rotationAxis(const Vec3& u, double ang) {
	const double x = u.x, y = u.y, z = u.z;
	const double c = std::cos(ang), s = std::sin(ang), t = 1 - c;
	Mat4 o;
	o.m = { x * x * t + c, y * x * t + z * s, z * x * t - y * s, 0,
	        x * y * t - z * s, y * y * t + c, z * y * t + x * s, 0,
	        x * z * t + y * s, y * z * t - x * s, z * z * t + c, 0,
	        0, 0, 0, 1 };
	return o;
}

Mat4 fromBasis(const Vec3& xa, const Vec3& ya, const Vec3& za, const Vec3& p) {
	Mat4 o;
	o.m = { xa.x, xa.y, xa.z, 0, ya.x, ya.y, ya.z, 0, za.x, za.y, za.z, 0, p.x, p.y, p.z, 1 };
	return o;
}

} // namespace m4
} // namespace dm
