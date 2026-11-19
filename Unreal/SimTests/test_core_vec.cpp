// DEMIURGE — SimTests/test_core_vec.cpp : Core/Vec.h (port of tests/vec.test.js).
#include <cmath>

#include "Core/Rng.h"
#include "Core/Util.h"
#include "Core/Vec.h"
#include "TestFramework.h"

using namespace dm;
using namespace dmt;

namespace {

bool vnear(const Vec3& a, const Vec3& b, double eps, const std::string& msg, std::source_location loc = std::source_location::current()) {
	for (int i = 0; i < 3; i++) {
		if (!(std::fabs(a[i] - b[i]) <= eps)) {
			recordFailure(msg + ": expected [" + show(b.x) + ", " + show(b.y) + ", " + show(b.z) + "] got [" + show(a.x) + ", " + show(a.y) + ", " + show(a.z) + "]", loc);
			return false;
		}
	}
	return true;
}
bool mnear(const Mat4& a, const std::array<double, 16>& b, double eps, const std::string& msg, std::source_location loc = std::source_location::current()) {
	for (int i = 0; i < 16; i++) {
		if (!(std::fabs(a[i] - b[size_t(i)]) <= eps)) {
			recordFailure(msg + ": element " + std::to_string(i) + " expected " + show(b[size_t(i)]) + " got " + show(a[i]), loc);
			return false;
		}
	}
	return true;
}
bool mnearM(const Mat4& a, const Mat4& b, double eps, const std::string& msg, std::source_location loc = std::source_location::current()) {
	return mnear(a, b.m, eps, msg, loc);
}
Vec3 rndUnit(Rng& r) {
	double x, y, z, l;
	do { x = r.range(-1, 1); y = r.range(-1, 1); z = r.range(-1, 1); l = x * x + y * y + z * z; } while (l > 1 || l < 1e-3);
	l = std::sqrt(l);
	return { x / l, y / l, z / l };
}
Vec3 rndVec(Rng& r, double s) { return { r.range(-s, s), r.range(-s, s), r.range(-s, s) }; }
bool finite(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
std::array<double, 4> apply4(const Mat4& m, const std::array<double, 4>& v) {
	std::array<double, 4> o{};
	for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) o[size_t(r)] += m.at(r, c) * v[size_t(c)];
	return o;
}
const std::array<double, 16> IDENT = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };

} // namespace

DM_TEST("V3: arithmetic, dot, cross, len, dist, norm, lerp") {
	const Vec3 a{ 1, 2, 3 }, b{ 4, -5, 6 };
	ok(a + b == Vec3{ 5, -3, 9 } && a - b == Vec3{ -3, 7, -3 } && a * 2 == Vec3{ 2, 4, 6 } && 2 * a == a * 2, "operators");
	ok(v3::addScaled(a, b, 0.5) == Vec3{ 3, -0.5, 6 }, "addScaled");
	eq(v3::dot(a, b), 12.0);
	ok(v3::cross(Vec3{ 1, 0, 0 }, Vec3{ 0, 1, 0 }) == Vec3{ 0, 0, 1 }, "x cross y = z");
	ok(v3::cross(a, b) == Vec3{ 2 * 6 - 3 * -5, 3 * 4 - 1 * 6, 1 * -5 - 2 * 4 }, "cross");
	near(v3::len(Vec3{ 3, 4, 12 }), 13, 1e-15);
	near(v3::dist(a, b), std::sqrt(9 + 49 + 9.0), 1e-15);
	near(v3::len(v3::norm(Vec3{ 3, 4, 12 })), 1, 1e-15);
	ok(v3::norm(Vec3{ 0, 0, 0 }) == Vec3{ 0, 1, 0 }, "zero -> (0,1,0)");
	ok(v3::norm(Vec3{ std::nan(""), 0, 0 }) == Vec3{ 0, 1, 0 }, "NaN -> (0,1,0)");
	ok(v3::lerp(a, b, 0.5) == Vec3{ 2.5, -1.5, 4.5 }, "lerp");
}

DM_TEST("V3.slerp: endpoints, constant angular speed, unit length, opposites, zero vectors") {
	const Vec3 X{ 1, 0, 0 }, Y{ 0, 1, 0 };
	vnear(v3::slerp(X, Y, 0.5), { std::sqrt(0.5), std::sqrt(0.5), 0 }, 1e-15, "midpoint");
	ok(v3::slerp(X, Y, 0) == X && v3::slerp(X, Y, 1) == Y, "exact endpoints");
	vnear(v3::slerp(X, Y, 2), { -1, 0, 0 }, 1e-15, "t = 2 continues");
	Rng r(3);
	bool good = true;
	for (int i = 0; i < 2000 && good; i++) {
		const Vec3 a = rndUnit(r), b = rndUnit(r);
		const double t = r.range(0, 1);
		const Vec3 p = v3::slerp(a, b, t);
		const double th = v3::angle(a, b);
		good &= near(v3::len(p), 1, 1e-12, "unit");
		good &= near(v3::angle(a, p), t * th, 1e-9, "angle from a is t theta");
		good &= near(v3::dot(p, v3::cross(a, b)), 0, 1e-9, "in the plane");
	}
	for (int i = 0; i < 300 && good; i++) {
		const Vec3 a = rndUnit(r);
		for (double eps : { 0.0, 1e-4, 1e-7, 1e-9, 1e-13 }) {
			const Vec3 b = v3::norm(v3::addScaled(-a, rndUnit(r), eps));
			const double th = v3::angle(a, b);
			for (double t : { 0.1, 0.5, 0.9 }) {
				const Vec3 p = v3::slerp(a, b, t);
				good &= ok(finite(p), "finite for (nearly) opposite inputs");
				good &= near(v3::len(p), 1, 1e-12, "unit");
				good &= near(v3::angle(a, p), t * th, 1e-6, "angle");
			}
		}
	}
	vnear(v3::slerp(X, -X, 0.5), { 0, 0, 1 }, 1e-15, "fixed circle for +X");
	vnear(v3::slerp(Y, -Y, 0.5), { 0, 0, -1 }, 1e-15, "fixed circle for +Y");
	vnear(v3::slerp(Vec3{ 0.6, 0.8, 0 }, Vec3{ -0.6, -0.8, 0 }, 0.5), { 0.8, -0.6, 0 }, 1e-15, "smallest along z");
	const Vec3 Z0{ 0, 0, 0 }, b{ 0, 0.6, 0.8 };
	vnear(v3::slerp(Z0, b, 0.3), b, 1e-15, "zero a");
	vnear(v3::slerp(b, Z0, 0.3), b, 1e-15, "zero b");
	ok(v3::slerp(Z0, Z0, 0.3) == Z0, "both zero");
	Vec3 p{ 1, 0, 0 };
	const Vec3 target = v3::norm(Vec3{ -0.3, 0.9, 0.4 });
	for (int i = 0; i < 5000; i++) p = v3::slerp(p, target, 0.002);
	near(v3::len(p), 1, 1e-12, "no drift after 5000 chained slerps");
}

DM_TEST("V3.angle and rotAxis") {
	near(v3::angle(Vec3{ 1, 0, 0 }, Vec3{ 0, 1, 0 }), kPi / 2, 1e-15);
	eq(v3::angle(Vec3{ 1, 0, 0 }, Vec3{ 1, 0, 0 }), 0.0);
	near(v3::angle(Vec3{ 1, 0, 0 }, Vec3{ -1, 0, 0 }), kPi, 1e-15);
	Rng r(6);
	bool good = true;
	for (int i = 0; i < 500 && good; i++) {
		const Vec3 a = rndUnit(r), b = rndUnit(r);
		const Vec3 axis = v3::norm(v3::cross(a, b));
		for (double want : { 1e-3, 1e-6, 1e-9 }) {
			good &= near(v3::angle(a, v3::rotAxis(a, axis, want)) / want, 1, 1e-3, "small angle");
			good &= near((kPi - v3::angle(a, v3::rotAxis(a, axis, kPi - want))) / want, 1, 1e-2, "near pi");
		}
	}
	vnear(v3::rotAxis(Vec3{ 1, 0, 0 }, Vec3{ 0, 0, 1 }, kPi / 2), { 0, 1, 0 }, 1e-15, "x about z -> y");
	vnear(v3::rotAxis(Vec3{ 0, 1, 0 }, Vec3{ 1, 0, 0 }, kPi / 2), { 0, 0, 1 }, 1e-15, "y about x -> z");
	vnear(v3::rotAxis(Vec3{ 0, 0, 1 }, Vec3{ 0, 1, 0 }, kPi / 2), { 1, 0, 0 }, 1e-15, "z about y -> x");
	for (int i = 0; i < 500 && good; i++) {
		const Vec3 v = rndVec(r, 10), k = rndUnit(r);
		const double ang = r.range(-7, 7);
		const Vec3 p = v3::rotAxis(v, k, ang);
		good &= near(v3::len(p), v3::len(v), 1e-10, "length preserved");
		good &= near(v3::dot(p, k), v3::dot(v, k), 1e-10, "axis component preserved");
		good &= vnear(v3::rotAxis(p, k, -ang), v, 1e-10, "inverse");
	}
}

DM_TEST("V3.basis: east = normalize(cross(Y, n)), north = cross(n, east), poles") {
	Vec3 e, n;
	v3::basis(Vec3{ 1, 0, 0 }, e, n);
	vnear(e, { 0, 0, -1 }, 0, "east at +X"); vnear(n, { 0, 1, 0 }, 0, "north at +X");
	v3::basis(Vec3{ 0, 1, 0 }, e, n);
	ok(e == Vec3{ 1, 0, 0 }, "east at the north pole is +X"); vnear(n, { 0, 0, -1 }, 0, "north at the north pole");
	v3::basis(Vec3{ 0, -1, 0 }, e, n);
	ok(e == Vec3{ 1, 0, 0 }, "east at the south pole is +X"); vnear(n, { 0, 0, 1 }, 0, "north at the south pole");
	Rng r(8);
	bool good = true;
	for (int i = 0; i < 1000 && good; i++) {
		const Vec3 d = rndUnit(r);
		v3::basis(d, e, n);
		good &= vnear(e, v3::norm(v3::cross(Vec3{ 0, 1, 0 }, d)), 1e-12, "east");
		good &= vnear(n, v3::cross(d, e), 1e-12, "north");
		good &= vnear(v3::cross(e, n), d, 1e-12, "right-handed");
		good &= ok(e.y == 0 && n.y >= 0, "east horizontal, north up");
	}
}

DM_TEST("M4: layout, mul order, perspective, ortho, lookAt, invert, rotationAxis, fromBasis") {
	mnear(m4::identity(), IDENT, 0, "identity");
	mnear(m4::translation(2, 3, 4), { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 2, 3, 4, 1 }, 0, "translation in 12..14");
	mnear(m4::scaling(2, 3, 4), { 2, 0, 0, 0, 0, 3, 0, 0, 0, 0, 4, 0, 0, 0, 0, 1 }, 0, "scaling");
	const Mat4 TS = m4::mul(m4::translation(10, 20, 30), m4::scaling(2, 3, 4));
	mnear(TS, { 2, 0, 0, 0, 0, 3, 0, 0, 0, 0, 4, 0, 10, 20, 30, 1 }, 0, "T*S: scale first");
	vnear(m4::transformPoint(TS, { 1, 1, 1 }), { 12, 23, 34 }, 0, "T*S on (1,1,1)");
	Mat4 A, B;
	A.m = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
	B.m = { 1, 0, 2, 0, 0, 1, 0, 3, -1, 0, 1, 0, 2, 2, 2, 2 };
	mnear(m4::mul(A, B), { 19, 22, 25, 28, 44, 48, 52, 56, 8, 8, 8, 8, 56, 64, 72, 80 }, 0, "hand-computed product");
	mnear(m4::perspective(kPi / 2, 2, 1, 3), { 0.5, 0, 0, 0, 0, 1, 0, 0, 0, 0, -2, -1, 0, 0, -3, 0 }, 1e-15, "perspective");
	const Mat4 P = m4::perspective(1.1, 1.7, 0.25, 80);
	near(m4::transformPoint(P, { 0, 0, -0.25 }).z, -1, 1e-12, "near plane -> -1");
	near(m4::transformPoint(P, { 0, 0, -80 }).z, 1, 1e-12, "far plane -> +1");
	const Mat4 Pi = m4::perspective(1, 1.5, 0.1, INFINITY);
	bool fin = true;
	for (int i = 0; i < 16; i++) fin &= std::isfinite(Pi[i]);
	ok(fin, "infinite far plane is finite");
	near(m4::transformPoint(Pi, { 0, 0, -1e12 }).z, 1, 1e-9, "very far -> 1");
	mnear(m4::ortho(-2, 2, -1, 1, 1, 5), { 0.5, 0, 0, 0, 0, 1, 0, 0, 0, 0, -0.5, 0, 0, 0, -1.5, 1 }, 1e-15, "ortho");
	mnear(m4::lookAt({ 0, 0, 5 }, { 0, 0, 0 }, { 0, 1, 0 }), { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, -5, 1 }, 1e-15, "lookAt down -Z");
	const Mat4 V = m4::lookAt({ 10, 0, 0 }, { 0, 0, 0 }, { 0, 1, 0 });
	mnear(V, { 0, 0, 1, 0, 0, 1, 0, 0, -1, 0, 0, 0, 0, 0, -10, 1 }, 1e-15, "lookAt from +X");
	const Mat4 D = m4::lookAt({ 1, 2, 3 }, { 1, 2, 3 }, { 0, 1, 0 });
	fin = true;
	for (int i = 0; i < 16; i++) fin &= std::isfinite(D[i]);
	ok(fin, "eye = target stays finite");
	const Mat4 D2 = m4::lookAt({ 0, 5, 0 }, { 0, 0, 0 }, { 0, 1, 0 });
	fin = true;
	for (int i = 0; i < 16; i++) fin &= std::isfinite(D2[i]);
	ok(fin, "up parallel to the view direction stays finite");
	Mat4 inv;
	ok(m4::invert(inv, m4::translation(2, -3, 4)), "invertible");
	mnearM(inv, m4::translation(-2, 3, -4), 1e-15, "inverse translation");
	ok(m4::invert(inv, m4::perspective(kPi / 2, 1, 1, 3)), "perspective invertible");
	mnear(inv, { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, -1.0 / 3, 0, 0, -1, 2.0 / 3 }, 1e-12, "inverse perspective");
	Mat4 sing; sing.m = { 1, 2, 3, 4, 2, 4, 6, 8, 0, 0, 1, 0, 0, 0, 0, 1 };
	Mat4 untouched = m4::translation(7, 7, 7);
	ok(!m4::invert(untouched, sing), "singular -> false");
	mnearM(untouched, m4::translation(7, 7, 7), 0, "output untouched");
	Rng r(22);
	bool good = true;
	for (int i = 0; i < 200 && good; i++) {
		Mat4 M;
		for (auto& x : M.m) x = r.range(-3, 3);
		Mat4 Mi;
		if (!m4::invert(Mi, M)) continue;
		good &= mnear(m4::mul(M, Mi), IDENT, 1e-8, "M * M^-1 = I");
	}
	mnear(m4::rotationAxis({ 0, 0, 1 }, kPi / 2), { 0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 }, 1e-15, "rotation about Z");
	for (int i = 0; i < 300 && good; i++) {
		const Vec3 k = rndUnit(r), v = rndVec(r, 5);
		const double ang = r.range(-7, 7);
		good &= vnear(m4::transformDir(m4::rotationAxis(k, ang), v), v3::rotAxis(v, k, ang), 1e-12, "same as rotAxis");
	}
	const Mat4 F = m4::fromBasis({ 1, 2, 3 }, { 4, 5, 6 }, { 7, 8, 9 }, { 10, 11, 12 });
	mnear(F, { 1, 2, 3, 0, 4, 5, 6, 0, 7, 8, 9, 0, 10, 11, 12, 1 }, 0, "fromBasis columns");
	vnear(m4::transformDir(F, { 2, -1, 1 }), { 5, 7, 9 }, 0, "directions ignore the origin");
	const Mat4 proj = m4::perspective(0.9, 16.0 / 9, 1, 1000);
	for (int i = 0; i < 300 && good; i++) {
		const Vec3 dir = rndUnit(r);
		const Mat4 vp = m4::mul(proj, m4::lookAt(dir * 330, { 0, 0, 0 }, { 0, 1, 0 }));
		const Vec3 c = m4::transformPoint(vp, { 0, 0, 0 });
		good &= near(c.x, 0, 1e-9, "centre on screen") && near(c.y, 0, 1e-9);
		const Vec3 front = m4::transformPoint(vp, dir * kPlanetR), back = m4::transformPoint(vp, dir * -kPlanetR);
		good &= ok(front.z < c.z && c.z < back.z, "depth order");
		const Vec3 behind = dir * 400;
		good &= ok(apply4(vp, { behind.x, behind.y, behind.z, 1 })[3] < 0, "behind the camera: w < 0");
	}
}
