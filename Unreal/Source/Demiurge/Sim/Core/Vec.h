// DEMIURGE — Sim/Core/Vec.h : 3-vectors and 4x4 matrices (port of js/core/vec.js: DM.V3, DM.M4).
//
// Vec3 is a plain struct of three doubles with the usual operators. The DM.V3 functions are free
// functions in namespace dm::v3 returning values (JS wrote into an output argument; C++ returns):
//   v3::dot cross len dist norm lerp slerp angle rotAxis basis      (+ - * / operators on Vec3)
// Mat4 is column-major (element [row r, column c] is m[c * 4 + r]), double precision, OpenGL-style
// clip space (z in -1..1), right-handed view space looking down -Z. Functions in dm::m4:
//   identity mul perspective ortho lookAt invert transformPoint transformDir translation scaling
//   rotationAxis fromBasis
// The Unreal client uses FVector / FMatrix for rendering; these exist for the simulation, the
// tools and the tests (and keep the JS semantics: right-handed, +Y is the north pole).
#pragma once

#include <array>
#include <cmath>

namespace dm {

struct Vec3 {
	double x = 0, y = 0, z = 0;
	constexpr Vec3() = default;
	constexpr Vec3(double ax, double ay, double az) : x(ax), y(ay), z(az) {}
	constexpr double operator[](int i) const { return i == 0 ? x : i == 1 ? y : z; }
	double& operator[](int i) { return i == 0 ? x : i == 1 ? y : z; }
	constexpr Vec3 operator+(const Vec3& b) const { return { x + b.x, y + b.y, z + b.z }; }
	constexpr Vec3 operator-(const Vec3& b) const { return { x - b.x, y - b.y, z - b.z }; }
	constexpr Vec3 operator-() const { return { -x, -y, -z }; }
	constexpr Vec3 operator*(double s) const { return { x * s, y * s, z * s }; }
	constexpr Vec3 operator/(double s) const { return { x / s, y / s, z / s }; }
	Vec3& operator+=(const Vec3& b) { x += b.x; y += b.y; z += b.z; return *this; }
	Vec3& operator-=(const Vec3& b) { x -= b.x; y -= b.y; z -= b.z; return *this; }
	Vec3& operator*=(double s) { x *= s; y *= s; z *= s; return *this; }
	constexpr bool operator==(const Vec3& b) const { return x == b.x && y == b.y && z == b.z; }
};
constexpr Vec3 operator*(double s, const Vec3& a) { return a * s; }

namespace v3 {
constexpr double dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
constexpr Vec3 cross(const Vec3& a, const Vec3& b) {
	return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}
inline double len(const Vec3& a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }
inline double dist(const Vec3& a, const Vec3& b) { return len(a - b); }
// o = a + b * s
constexpr Vec3 addScaled(const Vec3& a, const Vec3& b, double s) { return { a.x + b.x * s, a.y + b.y * s, a.z + b.z * s }; }
constexpr Vec3 lerp(const Vec3& a, const Vec3& b, double t) {
	return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t };
}
// Normalize; a zero (or NaN) vector becomes (0,1,0).
Vec3 norm(const Vec3& a);
// Great-circle interpolation of unit vectors (always unit length; t = 0 / 1 return exact copies;
// nearly parallel -> normalized lerp; exact opposites -> a fixed great circle; never NaN).
Vec3 slerp(const Vec3& a, const Vec3& b, double t);
// Angle in radians between two vectors: atan2(|a x b|, a . b), accurate everywhere, never NaN.
double angle(const Vec3& a, const Vec3& b);
// Rodrigues rotation of a about a unit axis by ang radians (right-handed).
Vec3 rotAxis(const Vec3& a, const Vec3& axis, double ang);
// Local tangent frame at unit direction n: east = normalize(cross(Y, n)), north = cross(n, east);
// east = +X at the poles. (east, north, n) is right-handed.
void basis(const Vec3& n, Vec3& east, Vec3& north);
} // namespace v3

struct Mat4 {
	std::array<double, 16> m{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };   // identity
	double& operator[](int i) { return m[size_t(i)]; }
	double operator[](int i) const { return m[size_t(i)]; }
	double at(int row, int col) const { return m[size_t(col * 4 + row)]; }
};

namespace m4 {
inline Mat4 identity() { return Mat4{}; }
// a * b (b is applied first to a column vector)
Mat4 mul(const Mat4& a, const Mat4& b);
// Right-handed perspective; zFar may be +infinity.
Mat4 perspective(double fovyRad, double aspect, double zNear, double zFar);
Mat4 ortho(double left, double right, double bottom, double top, double zNear, double zFar);
// View matrix: target on -Z, up in the +Y half of the YZ plane; degenerate inputs handled.
Mat4 lookAt(const Vec3& eye, const Vec3& target, const Vec3& up);
// false (out untouched) if a is singular relative to its scale
bool invert(Mat4& out, const Mat4& a);
Vec3 transformPoint(const Mat4& m, const Vec3& v);   // w = 1, perspective divide (w = 0 -> undivided)
Vec3 transformDir(const Mat4& m, const Vec3& v);     // w = 0, upper-left 3x3 only
Mat4 translation(double x, double y, double z);
Mat4 scaling(double x, double y, double z);
Mat4 rotationAxis(const Vec3& unitAxis, double ang);
// columns = the three axes and the origin
Mat4 fromBasis(const Vec3& xAxis, const Vec3& yAxis, const Vec3& zAxis, const Vec3& origin);
} // namespace m4

} // namespace dm
