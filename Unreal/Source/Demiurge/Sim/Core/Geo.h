// DEMIURGE — Sim/Core/Geo.h : the planet grid, an icosphere whose vertices are the cells.
// Port of js/core/geo.js (DM.Geo). Cell numbering, neighbour order and triangle order are
// identical to the JS build (the tests pin the same topology hashes).
//
//   std::shared_ptr<const Geo> g = Geo::build(L);   L in 0..7 (clamped); 10 * 4^L + 2 cells
//   g->L, g->n, g->nTri
//   g->pos   std::vector<float>(3n)  unit direction of each cell (x, y, z); +Y = north pole
//   g->lat   std::vector<float>(n)   asin(y) in [-pi/2, pi/2];  g->lon atan2(z, x) in [-pi, pi]
//   g->nbr   std::vector<int32_t>(6n) neighbours of c: nbr[6c .. 6c + nbrN[c] - 1], counter-
//            clockwise seen from outside; the unused 6th slot of the 12 pentagons (ids 0-11) is -1
//   g->nbrN  std::vector<uint8_t>(n)  5 or 6
//   g->tris  std::vector<uint32_t>(3 nTri)  counter-clockwise seen from outside
//   g->step  mean angle between neighbouring cells (radians; L = 6: about 0.0189)
//   Geo::prefix(k) = 10 * 4^k + 2   (level k's cells are the first prefix(k) cells of finer levels)
//   g->cellAt(x, y, z, hint = -1) / cellAt(Vec3, hint)  exact nearest cell to a direction
//   g->angle(a, b)                   great-circle angle between cells a and b
//   g->within(c, ang, out) -> count  every cell within angle ang of cell c (c first; inclusive)
//   g->ring(c, steps, out) -> count  every cell within `steps` neighbour hops (BFS order, c first)
//   g->dir(c) -> Vec3                g->basis(c, east, north)   (see v3::basis)
//
// Construction (other code relies on it): vertex 0 = north pole, 11 = south pole, 1-5 upper ring
// (lat atan(1/2), lon 0, 72, ...), 6-10 lower ring (lon 36, 108, ...). Each subdivision appends one
// normalized midpoint per edge after all existing vertices (prefix property).
//
// A Geo is immutable after build(), except for the small scratch buffers within() / ring() use:
// share one Geo between worlds freely on ONE thread, never between threads that call
// within() / ring() concurrently. Building costs about 20 ms at L = 6 (release); worlds normally
// build their own (State.h: CreateOpts::geo lets tests share one).
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "Core/Vec.h"

namespace dm {

class Geo {
public:
	static std::shared_ptr<Geo> build(int L);
	static constexpr int kMaxLevel = 7;
	static int prefix(int k);

	int L = 0, n = 0, nTri = 0;
	std::vector<float> pos, lat, lon;
	std::vector<int32_t> nbr;
	std::vector<uint8_t> nbrN;
	std::vector<uint32_t> tris;
	double step = 0;

	int cellAt(double x, double y, double z, int hint = -1) const;
	int cellAt(const Vec3& d, int hint = -1) const { return cellAt(d.x, d.y, d.z, hint); }
	double angle(int a, int b) const;
	// Clears `out` and fills it; returns out.size(). An invalid c gives 0 cells; ang <= 0 (or NaN)
	// gives c alone; ang >= pi gives every cell. Exact: a cell is included iff angle(c, m) <= ang.
	int within(int c, double ang, std::vector<int32_t>& out) const;
	// Clears `out` and fills it in BFS order; a fractional `steps` counts as its floor.
	int ring(int c, int steps, std::vector<int32_t>& out) const;
	Vec3 dir(int c) const { const size_t o = size_t(3 * c); return { pos[o], pos[o + 1], pos[o + 2] }; }
	void basis(int c, Vec3& east, Vec3& north) const;

	// Neighbour helpers: for (int k = 0; k < g.nbrN[c]; k++) { int m = g.nbr[6 * c + k]; ... }
	const int32_t* nbrs(int c) const { return nbr.data() + 6 * c; }
	int degree(int c) const { return nbrN[size_t(c)]; }
	bool valid(int c) const { return c >= 0 && c < n; }

private:
	int descend(double x, double y, double z, int c) const;
	int lookup(double x, double y, double z) const;
	int nextStamp() const;

	int G_ = 1;                       // lookup buckets per cube-face side
	std::vector<int32_t> table_;      // 6 * G * G start cells for cellAt
	double hintCos2_ = 0;
	// scratch for within / ring (results always live in the caller's vector, so nested use is safe)
	mutable std::vector<int32_t> mark_, queue_;
	mutable int32_t stamp_ = 0;
};

} // namespace dm
