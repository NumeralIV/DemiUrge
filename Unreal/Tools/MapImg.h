// DEMIURGE — Unreal/Tools/MapImg.h : equirectangular PNG maps of a world (port of tools/mapimg.js),
// so sim engineers can SEE what their module produces without the renderer. No dependencies.
//
//   namespace mi = dm::mapimg;
//   mi::elev(w, dmt::shot("ue-terrain-elev.png"));                  hypsometric tint, sea in blues
//   mi::biome(w, file);                                              Biome colours
//   mi::field(w, "moist", file, 0, 1);                               any cell field by name, blue-yellow-red ramp
//   mi::field(w, values, file, min, max);                            any std::vector<float> / <double> of n values
//   mi::field(w, "temp", file);                                      min / max from the data (NaN min/max = auto)
//   mi::owner(w, file);                                              territories by culture colour, centres white
//   mi::custom(w, file, [&](int c) { return mi::Rgb{ r, g, b }; });  your own colours (0..255)
// Options (last argument): { .w = 1024, .h = 512, .rivers = true, .marks = {cells}, .grid = false, .dimSea = false }
// North is up; longitude -180 deg is at the left edge. Rivers (world.rivers) are drawn on top when
// present, lake cells (water > 0.03 on land) in lake blue on elev / biome maps. Returns false if
// the file could not be written. Then open the PNG with the image-reading tool and LOOK at it.
#pragma once

#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <vector>

#include "State.h"

namespace dm::mapimg {

struct Rgb { double r = 0, g = 0, b = 0; };

struct Opts {
	int w = 1024, h = 512;
	bool rivers = true;            // draw world.rivers on top
	bool lakes = true;             // elev / biome: lake cells in lake blue
	std::vector<int> marks;        // cells drawn white
	bool grid = false;             // 30-degree graticule
	bool dimSea = false;           // field(): darken sea cells
};

// Raw writer: rgb is w * h * 3 bytes, rows top to bottom. Stored (uncompressed) deflate blocks.
bool writePng(const std::string& file, int w, int h, const std::vector<uint8_t>& rgb);
// Encode only (for tests).
std::vector<uint8_t> encodePng(int w, int h, const std::vector<uint8_t>& rgb);

bool custom(const World& world, const std::string& file, const std::function<Rgb(int)>& colorFn, const Opts& o = {});
bool elev(const World& world, const std::string& file, const Opts& o = {});
bool biome(const World& world, const std::string& file, const Opts& o = {});
inline constexpr double kAuto = std::numeric_limits<double>::quiet_NaN();
bool field(const World& world, const std::string& name, const std::string& file, double min = kAuto, double max = kAuto, const Opts& o = {});
bool field(const World& world, const std::vector<float>& values, const std::string& file, double min = kAuto, double max = kAuto, const Opts& o = {});
bool field(const World& world, const std::vector<double>& values, const std::string& file, double min = kAuto, double max = kAuto, const Opts& o = {});
bool owner(const World& world, const std::string& file, const Opts& o = {});

// The colour ramps (for legends and custom maps).
Rgb ramp(const std::vector<std::pair<double, Rgb>>& stops, double t);
Rgb heat(double t01);                 // the field() ramp
extern const Rgb kBiomeColors[Biome::COUNT];

} // namespace dm::mapimg
