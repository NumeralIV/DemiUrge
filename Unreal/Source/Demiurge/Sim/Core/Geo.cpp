// DEMIURGE — Sim/Core/Geo.cpp : see Geo.h (port of js/core/geo.js).
#include "Core/Geo.h"

#include <algorithm>
#include <cmath>

#include "Core/Util.h"

namespace dm {

namespace {

// Largest float32 <= x (x > 0), so stored lat / lon never exceed pi/2 and pi when read as doubles.
float f32Below(double x) {
	float f = float(x);
	if (double(f) > x) f = std::nextafter(f, 0.0f);
	return f;
}

// Dot products of float32 unit vectors differ from the true cosine by at most ~1.2e-7; within()
// settles anything closer than this to its threshold with the exact angle formula.
constexpr double BAND = 1e-6;

} // namespace

int Geo::prefix(int k) {
	int p = 1;
	for (int i = 0; i < k; i++) p *= 4;
	return 10 * p + 2;
}

std::shared_ptr<Geo> Geo::build(int Lin) {
	const int Lv = Lin < 0 ? 0 : Lin > kMaxLevel ? kMaxLevel : Lin;
	auto geo = std::make_shared<Geo>();
	Geo& g = *geo;
	const int nv = prefix(Lv);
	int ntri = 20;
	for (int i = 0; i < Lv; i++) ntri *= 4;
	g.L = Lv; g.n = nv; g.nTri = ntri;

	// ---- 1. vertices (double while building) and triangles
	const double S5 = std::sqrt(5.0);
	const double CH = 2 / S5, SH = 1 / S5;
	const double C72 = (S5 - 1) / 4, S72 = std::sqrt(10 + 2 * S5) / 4;
	const double C36 = (S5 + 1) / 4, S36 = std::sqrt(10 - 2 * S5) / 4;
	const double UP_COS[5] = { 1, C72, -C36, -C36, C72 }, UP_SIN[5] = { 0, S72, S36, -S36, -S72 };
	const double LO_COS[5] = { C36, -C72, -1, -C72, C36 }, LO_SIN[5] = { S36, S72, 0, -S72, -S36 };

	std::vector<double> p(size_t(3 * nv), 0.0);
	p[1] = 1;
	p[3 * 11 + 1] = -1;
	for (int k = 0; k < 5; k++) {
		size_t o = size_t(3 * (1 + k));
		p[o] = CH * UP_COS[k]; p[o + 1] = SH; p[o + 2] = CH * UP_SIN[k];
		o = size_t(3 * (6 + k));
		p[o] = CH * LO_COS[k]; p[o + 1] = -SH; p[o + 2] = CH * LO_SIN[k];
	}

	std::vector<uint32_t> tris;
	tris.reserve(60);
	for (uint32_t k = 0; k < 5; k++) { tris.push_back(0); tris.push_back(1 + (k + 1) % 5); tris.push_back(1 + k); }
	for (uint32_t k = 0; k < 5; k++) { tris.push_back(1 + k); tris.push_back(1 + (k + 1) % 5); tris.push_back(6 + k); }
	for (uint32_t k = 0; k < 5; k++) { tris.push_back(6 + k); tris.push_back(1 + (k + 1) % 5); tris.push_back(6 + (k + 1) % 5); }
	for (uint32_t k = 0; k < 5; k++) { tris.push_back(6 + k); tris.push_back(6 + (k + 1) % 5); tris.push_back(11); }

	int nt = 20, count = 12;
	if (Lv > 0) {
		// edge-midpoint cache: each edge filed under its lower vertex id (at most 6 per vertex)
		const int nOld = prefix(Lv - 1);
		std::vector<uint8_t> eCnt(static_cast<size_t>(nOld));
		std::vector<int32_t> eHi(static_cast<size_t>(6 * nOld)), eMid(static_cast<size_t>(6 * nOld));
		auto mid = [&](uint32_t a, uint32_t b) -> uint32_t {
			const uint32_t lo = a < b ? a : b, hi = a < b ? b : a;
			const size_t o = size_t(6 * lo);
			const int k = eCnt[lo];
			for (int j = 0; j < k; j++) if (eHi[o + size_t(j)] == int32_t(hi)) return uint32_t(eMid[o + size_t(j)]);
			const int id = count++;
			eHi[o + size_t(k)] = int32_t(hi); eMid[o + size_t(k)] = id; eCnt[lo] = uint8_t(k + 1);
			const size_t ia = size_t(3 * a), ib = size_t(3 * b), io = size_t(3 * id);
			const double x = p[ia] + p[ib], y = p[ia + 1] + p[ib + 1], z = p[ia + 2] + p[ib + 2];
			const double inv = 1 / std::sqrt(x * x + y * y + z * z);
			p[io] = x * inv; p[io + 1] = y * inv; p[io + 2] = z * inv;
			return uint32_t(id);
		};
		for (int lev = 0; lev < Lv; lev++) {
			std::fill(eCnt.begin(), eCnt.begin() + count, uint8_t(0));
			std::vector<uint32_t> next(size_t(nt) * 12);
			size_t w = 0;
			for (int t = 0; t < nt; t++) {
				const uint32_t a = tris[size_t(3 * t)], b = tris[size_t(3 * t + 1)], c = tris[size_t(3 * t + 2)];
				const uint32_t ab = mid(a, b), bc = mid(b, c), ca = mid(c, a);
				next[w++] = a; next[w++] = ab; next[w++] = ca;
				next[w++] = b; next[w++] = bc; next[w++] = ab;
				next[w++] = c; next[w++] = ca; next[w++] = bc;
				next[w++] = ab; next[w++] = bc; next[w++] = ca;
			}
			tris.swap(next);
			nt *= 4;
		}
	}
	// (count == nv and nt == ntri by construction)

	g.pos.resize(size_t(3 * nv));
	for (size_t i = 0; i < p.size(); i++) g.pos[i] = float(p[i]);
	g.lat.resize(size_t(nv));
	g.lon.resize(size_t(nv));
	const float LAT_MAX = f32Below(kPi / 2), LON_MAX = f32Below(kPi);
	for (int i = 0; i < nv; i++) {
		const double x = p[size_t(3 * i)], y = p[size_t(3 * i + 1)], z = p[size_t(3 * i + 2)];
		float la = float(std::atan2(y, std::sqrt(x * x + z * z)));
		float lo = float(std::atan2(z, x));
		la = std::clamp(la, -LAT_MAX, LAT_MAX);
		lo = std::clamp(lo, -LON_MAX, LON_MAX);
		g.lat[size_t(i)] = la; g.lon[size_t(i)] = lo;
	}
	g.tris = std::move(tris);

	// ---- 2. neighbours, counter-clockwise seen from outside
	g.nbr.assign(size_t(6 * nv), -1);
	g.nbrN.assign(size_t(nv), 0);
	{
		std::vector<int32_t> from(static_cast<size_t>(6 * nv)), to(static_cast<size_t>(6 * nv));
		std::vector<uint8_t>& cnt = g.nbrN;
		for (int t = 0; t < ntri; t++) {
			const uint32_t a = g.tris[size_t(3 * t)], b = g.tris[size_t(3 * t + 1)], c = g.tris[size_t(3 * t + 2)];
			size_t o = size_t(6 * a) + cnt[a]++; from[o] = int32_t(b); to[o] = int32_t(c);
			o = size_t(6 * b) + cnt[b]++; from[o] = int32_t(c); to[o] = int32_t(a);
			o = size_t(6 * c) + cnt[c]++; from[o] = int32_t(a); to[o] = int32_t(b);
		}
		for (int v = 0; v < nv; v++) {
			const size_t o = size_t(6 * v);
			const int k = cnt[size_t(v)];
			int32_t cur = from[o];
			for (int i = 0; i < k; i++) {
				g.nbr[o + size_t(i)] = cur;
				for (int j = 0; j < k; j++) if (from[o + size_t(j)] == cur) { cur = to[o + size_t(j)]; break; }
			}
		}
	}

	// ---- 3. mean neighbour angle
	{
		double sum = 0;
		long long edges = 0;
		for (int v = 0; v < nv; v++) {
			const double x = p[size_t(3 * v)], y = p[size_t(3 * v + 1)], z = p[size_t(3 * v + 2)];
			for (int j = 0; j < 6; j++) {
				const int m = g.nbr[size_t(6 * v + j)];
				if (m <= v) continue;
				const double d = x * p[size_t(3 * m)] + y * p[size_t(3 * m + 1)] + z * p[size_t(3 * m + 2)];
				sum += std::acos(d < 1 ? d : 1);
				edges++;
			}
		}
		g.step = sum / double(edges);
	}

	// ---- 4. nearest-cell lookup table (cube map of start cells)
	g.G_ = 1 << Lv;
	const int GG = g.G_;
	g.table_.assign(size_t(6 * GG * GG), 0);
	{
		int c = 0;
		size_t idx = 0;
		for (int f = 0; f < 6; f++) {
			const double s = (f & 1) ? -1.0 : 1.0;
			for (int j = 0; j < GG; j++) {
				const double v = (j + 0.5) / GG * 2 - 1;
				for (int i = 0; i < GG; i++, idx++) {
					const double u = (i + 0.5) / GG * 2 - 1;
					if (i == 0 && j > 0) c = g.table_[idx - size_t(GG)];
					if (f < 2) c = g.descend(s, u, v, c);
					else if (f < 4) c = g.descend(u, s, v, c);
					else c = g.descend(u, v, s, c);
					g.table_[idx] = c;
				}
			}
		}
	}
	const double hintAng = std::min(3 * g.step, 1.2);
	g.hintCos2_ = std::cos(hintAng) * std::cos(hintAng);

	g.mark_.assign(size_t(nv), 0);
	g.queue_.assign(size_t(nv), 0);
	g.stamp_ = 0;
	return geo;
}

// Greedy walk to the neighbour with the largest dot product: exact because the icosphere is the
// convex hull of its vertices (a Delaunay triangulation of the sphere).
int Geo::descend(double x, double y, double z, int c) const {
	const float* P = pos.data();
	size_t o = size_t(3 * c);
	double best = x * P[o] + y * P[o + 1] + z * P[o + 2];
	for (;;) {
		const int32_t* nb = nbr.data() + 6 * c;
		int go = c;
		for (int j = 0; j < 6; j++) {
			const int m = nb[j];
			if (m < 0) break;
			o = size_t(3 * m);
			const double d = x * P[o] + y * P[o + 1] + z * P[o + 2];
			if (d > best) { best = d; go = m; }
		}
		if (go == c) return c;
		c = go;
	}
}

int Geo::lookup(double x, double y, double z) const {
	const double ax = std::fabs(x), ay = std::fabs(y), az = std::fabs(z);
	int f;
	double u, v;
	if (ax >= ay && ax >= az) { f = x > 0 ? 0 : 1; u = y / ax; v = z / ax; }
	else if (ay >= az) { f = y > 0 ? 2 : 3; u = x / ay; v = z / ay; }
	else { f = z > 0 ? 4 : 5; u = x / az; v = y / az; }
	const double fi = (u + 1) * 0.5 * G_, fj = (v + 1) * 0.5 * G_;
	int i = (fi >= 0 && fi < G_) ? int(fi) : (fi >= G_ ? G_ - 1 : 0);     // NaN -> 0
	int j = (fj >= 0 && fj < G_) ? int(fj) : (fj >= G_ ? G_ - 1 : 0);
	if (i >= G_) i = G_ - 1;
	if (j >= G_) j = G_ - 1;
	return table_[size_t((f * G_ + j) * G_ + i)];
}

int Geo::cellAt(double x, double y, double z, int hint) const {
	double len2 = x * x + y * y + z * z;
	// rescale absurdly short or long vectors by exact powers of two (direction unchanged)
	if (len2 < 1e-200 ? (x != 0 || y != 0 || z != 0) : len2 > 1e200) {
		double big = std::max(std::fabs(x), std::max(std::fabs(y), std::fabs(z)));
		if (big > 0 && std::isfinite(big)) {
			const double DOWN = std::ldexp(1.0, -300), UP = std::ldexp(1.0, 300);
			while (big >= 1e100) { x *= DOWN; y *= DOWN; z *= DOWN; big *= DOWN; }
			while (big <= 1e-100) { x *= UP; y *= UP; z *= UP; big *= UP; }
		}
		len2 = x * x + y * y + z * z;
	}
	if (hint >= 0 && hint < n) {
		const size_t o = size_t(3 * hint);
		const double d = x * pos[o] + y * pos[o + 1] + z * pos[o + 2];
		if (d > 0 && d * d >= hintCos2_ * len2) return descend(x, y, z, hint);
	}
	return descend(x, y, z, lookup(x, y, z));
}

double Geo::angle(int a, int b) const {
	const size_t ia = size_t(3 * a), ib = size_t(3 * b);
	const double ax = pos[ia], ay = pos[ia + 1], az = pos[ia + 2];
	const double bx = pos[ib], by = pos[ib + 1], bz = pos[ib + 2];
	const double cx = ay * bz - az * by, cy = az * bx - ax * bz, cz = ax * by - ay * bx;
	return std::atan2(std::sqrt(cx * cx + cy * cy + cz * cz), ax * bx + ay * by + az * bz);
}

int Geo::nextStamp() const {
	if (stamp_ == 0x7fffffff) { std::fill(mark_.begin(), mark_.end(), 0); stamp_ = 0; }
	return ++stamp_;
}

int Geo::within(int c, double ang, std::vector<int32_t>& out) const {
	out.clear();
	if (!(c >= 0 && c < n)) return 0;
	const int stamp = nextStamp();
	int32_t* mark = mark_.data();
	int32_t* queue = queue_.data();
	mark[c] = stamp; queue[0] = c; out.push_back(c);
	int count = 1;
	if (!(ang > 0)) return 1;
	const bool all = ang >= kPi;
	const double cosA = all ? -2 : std::cos(ang);
	const double lo = cosA - BAND, hi = cosA + BAND;
	const double cx = pos[size_t(3 * c)], cy = pos[size_t(3 * c + 1)], cz = pos[size_t(3 * c + 2)];
	const float* P = pos.data();
	for (int head = 0; head < count; head++) {
		const int32_t* nb = nbr.data() + 6 * queue[head];
		for (int j = 0; j < 6; j++) {
			const int m = nb[j];
			if (m < 0 || mark[m] == stamp) continue;
			mark[m] = stamp;
			if (!all) {
				const double d = cx * P[3 * m] + cy * P[3 * m + 1] + cz * P[3 * m + 2];
				if (d < lo) continue;
				if (d < hi && angle(c, m) > ang) continue;      // borderline: exact angle decides
			}
			queue[count++] = m;
			out.push_back(m);
		}
	}
	return count;
}

int Geo::ring(int c, int steps, std::vector<int32_t>& out) const {
	out.clear();
	if (!(c >= 0 && c < n)) return 0;
	const int stamp = nextStamp();
	int32_t* mark = mark_.data();
	int32_t* queue = queue_.data();
	mark[c] = stamp; queue[0] = c; out.push_back(c);
	int count = 1, head = 0;
	for (int s = 1; s <= steps && head < count; s++) {
		for (const int end = count; head < end; head++) {
			const int32_t* nb = nbr.data() + 6 * queue[head];
			for (int j = 0; j < 6; j++) {
				const int m = nb[j];
				if (m < 0 || mark[m] == stamp) continue;
				mark[m] = stamp;
				queue[count++] = m;
				out.push_back(m);
			}
		}
	}
	return count;
}

void Geo::basis(int c, Vec3& east, Vec3& north) const {
	const size_t o = size_t(3 * c);
	double x = pos[o], y = pos[o + 1], z = pos[o + 2];
	const double inv = 1 / std::sqrt(x * x + y * y + z * z);
	x *= inv; y *= inv; z *= inv;
	const double h = std::sqrt(x * x + z * z);
	if (h < 1e-9) {
		east = { 1, 0, 0 };
		north = { 0, 0, y > 0 ? -1.0 : 1.0 };
	} else {
		east = { z / h, 0, -x / h };
		north = { -x * y / h, h, -y * z / h };
	}
}

} // namespace dm
