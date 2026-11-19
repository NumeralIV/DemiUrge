// DEMIURGE — Sim/State.cpp : see State.h (port of js/sim/state.js; the save format is binary).
//
// Save layout, version 1 (all little-endian; str = u32 length + bytes; opt<x> = u8 0/1 + x):
//   'D' 'M' 'W' 'D', u32 version, u32 seed, str name, u8 L, u32 n, u32 rng.s
//   f64 year, i64 tick, u8 phase, f64 heat, f64 seaLevel, f64 globalTemp, f64 co2, f64 dust, i32 age
//   u8 fieldCount, then per persisted field: str name, u8 codec, u8 type, n codes
//        (Q8: 1 byte; Q16: 2 bytes; Raw: the element itself)
//   god, settlements, cultures, units, disasters, chronicle, nextId, stats, settings, vars, ending
//   (see writeX / readX below for each; lists are u32 count + items), then 'E' 'N' 'D' '!'.
// Loading accepts a save that lacks a persisted field (it keeps its default) and skips fields it
// does not know, so adding a row to DM_CELL_FIELDS keeps older saves loadable.
#include "State.h"

#include <cmath>
#include <cstring>

namespace dm {

World::World() = default;
World::~World() = default;

// ------------------------------------------------------------------ cell field table

namespace state {

const CellField kCellFields[] = {
#define DM_CELL_ROW(nm, T, ft, def, sv, mn, mx) { #nm, FieldType::ft, def, SaveCodec::sv, mn, mx },
	DM_CELL_FIELDS(DM_CELL_ROW)
#undef DM_CELL_ROW
};
const int kCellFieldCount = int(sizeof(kCellFields) / sizeof(kCellFields[0]));

int fieldIndex(std::string_view nm) {
	for (int i = 0; i < kCellFieldCount; i++) if (nm == kCellFields[i].name) return i;
	return -1;
}

CellArray cellArray(World& w, int fieldIdx) {
	CellArray a;
	int k = 0;
#define DM_CELL_CASE(nm, T, ft, def, sv, mn, mx) \
	if (k++ == fieldIdx) { a.type = FieldType::ft; a.data = w.nm.data(); a.size = w.nm.size(); return a; }
	DM_CELL_FIELDS(DM_CELL_CASE)
#undef DM_CELL_CASE
	return a;
}

CellArray cellArray(const World& w, int fieldIdx) { return cellArray(const_cast<World&>(w), fieldIdx); }

} // namespace state

double CellArray::get(size_t c) const {
	switch (type) {
	case FieldType::F32: return static_cast<const float*>(data)[c];
	case FieldType::U8: return static_cast<const uint8_t*>(data)[c];
	case FieldType::I16: return static_cast<const int16_t*>(data)[c];
	case FieldType::I32: return static_cast<const int32_t*>(data)[c];
	}
	return 0;
}

void CellArray::set(size_t c, double v) const {
	switch (type) {
	case FieldType::F32: static_cast<float*>(data)[c] = float(v); break;
	case FieldType::U8: static_cast<uint8_t*>(data)[c] = uint8_t(v); break;
	case FieldType::I16: static_cast<int16_t*>(data)[c] = int16_t(v); break;
	case FieldType::I32: static_cast<int32_t*>(data)[c] = int32_t(v); break;
	}
}

namespace state {

// ------------------------------------------------------------------ create

std::unique_ptr<World> create(const CreateOpts& opts) {
	auto w = std::make_unique<World>();
	w->geo = opts.geo ? opts.geo : std::shared_ptr<const Geo>(Geo::build(opts.L));
	w->L = w->geo->L;
	w->n = w->geo->n;
	w->seed = opts.seed;
	w->name = opts.name;
	w->rng = Rng(opts.seed);
	const size_t n = size_t(w->n);
#define DM_CELL_INIT(nm, T, ft, def, sv, mn, mx) w->nm.assign(n, T(def));
	DM_CELL_FIELDS(DM_CELL_INIT)
#undef DM_CELL_INIT
	return w;
}

// ------------------------------------------------------------------ quantization

namespace {
// JavaScript Math.round: nearest integer, ties toward +infinity.
double jsRound(double x) {
	const double r = std::floor(x);
	return (x - r >= 0.5) ? r + 1 : r;
}
uint32_t topOf(SaveCodec c) { return c == SaveCodec::Q16 ? 65535u : 255u; }
} // namespace

uint32_t quantize(const CellField& f, double v) {
	const double top = double(topOf(f.save));
	const double k = top / (f.max - f.min);
	if (std::isnan(v)) v = f.def;                       // (never `v != v`: see FOUNDATION-CPP 1.9)
	double q = jsRound((v - f.min) * k);
	q = q > top ? top : q > 0 ? q : 0;
	return uint32_t(q);
}

double dequantize(const CellField& f, uint32_t code) {
	const double top = double(topOf(f.save));
	return f.min + (f.max - f.min) * (double(code) / top);
}

// ------------------------------------------------------------------ serialize

namespace {

double fin(double v) { return std::isfinite(v) ? v : 0.0; }   // entity numbers: NaN / inf -> 0

void putOptF(ByteWriter& o, const std::optional<double>& v) {
	o.boolean(v.has_value() && std::isfinite(*v));
	if (v.has_value() && std::isfinite(*v)) o.f64(*v);
}
void putBag(ByteWriter& o, const NumBag& b) {
	o.u32(uint32_t(b.size()));
	for (const auto& kv : b) { o.str(kv.first); o.f64(fin(kv.second)); }
}
void putCells(ByteWriter& o, const std::vector<int32_t>& v) {
	o.u32(uint32_t(v.size()));
	for (int32_t x : v) o.i32(x);
}

void putSettlement(ByteWriter& o, const Settlement& s) {
	o.i32(s.id); o.i32(s.cell); o.str(s.name); o.i32(s.culture);
	o.f64(fin(s.founded)); o.boolean(s.dead); putOptF(o, s.deadYear); o.u8(uint8_t(s.cause));
	o.f64(fin(s.pop)); o.i32(s.tier); o.f64(fin(s.K)); o.f64(fin(s.growth)); o.i32(s.radius);
	o.u8(uint8_t(Building::COUNT));
	for (uint8_t v : s.b) o.u8(v ? 1 : 0);
	o.f64(fin(s.devotion)); o.f64(fin(s.love)); o.f64(fin(s.jaded));
	o.f64(fin(s.plague)); o.f64(fin(s.immune)); o.f64(fin(s.inspire)); o.f64(fin(s.blessed));
	o.i32(s.famine);
	o.boolean(s.prayer.has_value());
	if (s.prayer) { o.u8(uint8_t(s.prayer->kind)); o.f64(fin(s.prayer->since)); }
	o.i32(s.apart); o.boolean(s.capital); o.f64(fin(s.ark)); o.f64(fin(s.lastExpand));
}

void putCulture(ByteWriter& o, const Culture& c) {
	o.i32(c.id); o.str(c.name); o.str(c.adj); o.str(c.plural);
	for (double v : c.color) o.f64(fin(v));
	o.str(c.lang); o.i32(c.parent); o.f64(fin(c.born)); o.boolean(c.dead);
	o.i32(c.age); o.f64(fin(c.science)); o.i32(c.capital); o.str(c.godName); o.str(c.epithet);
	o.f64(fin(c.traits.piety)); o.f64(fin(c.traits.aggression)); o.f64(fin(c.traits.curiosity)); o.f64(fin(c.traits.seafaring));
	o.boolean(c.prophet.has_value());
	if (c.prophet) { o.str(c.prophet->name); o.i32(c.prophet->settlement); o.f64(fin(c.prophet->until)); }
	o.u8(uint8_t(c.edict)); o.f64(fin(c.edictSince));
	o.f64(fin(c.pop)); o.i32(c.towns); o.f64(fin(c.exhaustion));
	putBag(o, c.seen);
	o.f64(fin(c.lastSplit));
}

void putUnit(ByteWriter& o, const Unit& u) {
	o.i32(u.id); o.u8(uint8_t(u.type)); o.i32(u.culture); o.i32(u.from); o.i32(u.to); o.i32(u.dest);
	putCells(o, u.path);
	o.i32(u.i); o.f64(fin(u.t)); o.f64(fin(u.speed)); o.f64(fin(u.size));
	o.u8(uint8_t(u.naval)); o.boolean(u.sea); o.f64(fin(u.born));
	putBag(o, u.data);
}

void putDisaster(ByteWriter& o, const Disaster& d) {
	o.i32(d.id); o.u8(uint8_t(d.type)); o.i32(d.cell);
	o.f64(fin(d.t)); o.f64(fin(d.dur)); o.f64(fin(d.power)); o.f64(fin(d.ang)); o.boolean(d.natural);
	o.f64(fin(d.eta)); o.f64(fin(d.flight)); o.f64(fin(d.front));
	putCells(o, d.cells);
	putBag(o, d.data);
}

void putEntry(ByteWriter& o, const ChronicleEntry& e) {
	o.i32(e.id); o.f64(fin(e.year)); o.str(e.type); o.str(e.text); o.i32(e.cell); o.i32(e.culture); o.i32(e.weight);
}

bool refuse(std::vector<uint8_t>& out, std::string* why, const std::string& what) {
	out.clear();
	if (why) *why = what;
	return false;
}

} // namespace

bool serialize(const World& w, std::vector<uint8_t>& out, std::string* why) {
	const struct { const char* nm; double v; } scalars[] = {
		{ "year", w.year }, { "heat", w.heat }, { "seaLevel", w.seaLevel }, { "globalTemp", w.globalTemp },
		{ "co2", w.co2 }, { "dust", w.dust }, { "god.awe", w.god.awe }, { "god.aweMax", w.god.aweMax },
		{ "god.mercy", w.god.mercy }, { "god.wrath", w.god.wrath }, { "stats.pop", w.stats.pop },
		{ "stats.peakPop", w.stats.peakPop }, { "stats.believers", w.stats.believers },
		{ "settings.calamity", w.settings.calamity }, { "vars.lifeBoost", w.vars.lifeBoost },
	};
	for (const auto& s : scalars) if (!std::isfinite(s.v)) return refuse(out, why, std::string("world.") + s.nm + " is not finite");
	if (!w.geo || w.n != w.geo->n || w.L != w.geo->L) return refuse(out, why, "world geometry does not match n / L");
	const size_t n = size_t(w.n);
	for (int i = 0; i < kCellFieldCount; i++) {
		if (cellArray(w, i).size != n) return refuse(out, why, std::string("world.") + kCellFields[i].name + " is not an array of n cells");
	}

	ByteWriter o;
	o.buf.reserve(n * 20 + 4096);
	o.bytes(reinterpret_cast<const uint8_t*>(kFormat), 4);
	o.u32(kVersion);
	o.u32(w.seed);
	o.str(w.name);
	o.u8(uint8_t(w.L));
	o.u32(uint32_t(w.n));
	o.u32(w.rng.s);
	o.f64(w.year); o.i64(w.tick); o.u8(uint8_t(w.phase)); o.f64(w.heat); o.f64(w.seaLevel);
	o.f64(w.globalTemp); o.f64(w.co2); o.f64(w.dust); o.i32(w.age);

	int persisted = 0;
	for (int i = 0; i < kCellFieldCount; i++) if (kCellFields[i].save != SaveCodec::None) persisted++;
	o.u8(uint8_t(persisted));
	for (int i = 0; i < kCellFieldCount; i++) {
		const CellField& f = kCellFields[i];
		if (f.save == SaveCodec::None) continue;
		o.str(f.name);
		o.u8(uint8_t(f.save));
		o.u8(uint8_t(f.type));
		const CellArray a = cellArray(w, i);
		if (f.save == SaveCodec::Q8) {
			for (size_t c = 0; c < n; c++) o.u8(uint8_t(quantize(f, a.get(c))));
		} else if (f.save == SaveCodec::Q16) {
			for (size_t c = 0; c < n; c++) o.u16(uint16_t(quantize(f, a.get(c))));
		} else {
			switch (f.type) {
			case FieldType::U8: o.bytes(static_cast<const uint8_t*>(a.data), n); break;
			case FieldType::I16: for (size_t c = 0; c < n; c++) o.i16(static_cast<const int16_t*>(a.data)[c]); break;
			case FieldType::I32: for (size_t c = 0; c < n; c++) o.i32(static_cast<const int32_t*>(a.data)[c]); break;
			case FieldType::F32: for (size_t c = 0; c < n; c++) o.f32(static_cast<const float*>(a.data)[c]); break;
			}
		}
	}

	o.f64(w.god.awe); o.f64(w.god.aweMax); o.f64(w.god.mercy); o.f64(w.god.wrath); o.str(w.god.epithet);
	o.u32(uint32_t(w.god.deeds.size()));
	for (const auto& kv : w.god.deeds) { o.str(kv.first); o.i32(kv.second); }

	o.u32(uint32_t(w.settlements.size()));
	for (const Settlement& s : w.settlements) putSettlement(o, s);
	o.u32(uint32_t(w.cultures.size()));
	for (const Culture& c : w.cultures) putCulture(o, c);
	o.u32(uint32_t(w.units.size()));
	for (const Unit& u : w.units) putUnit(o, u);
	o.u32(uint32_t(w.disasters.size()));
	for (const Disaster& d : w.disasters) putDisaster(o, d);
	o.u32(uint32_t(w.chronicle.size()));
	for (const ChronicleEntry& e : w.chronicle) putEntry(o, e);

	o.i32(w.nextId.unit); o.i32(w.nextId.disaster); o.i32(w.nextId.entry);
	o.f64(w.stats.pop); o.f64(w.stats.peakPop); o.f64(w.stats.believers); o.i32(w.stats.wars); o.i32(w.stats.miracles);
	o.f64(w.settings.calamity);

	const Vars& v = w.vars;
	putOptF(o, v.seaBase); putOptF(o, v.iceRef); o.f64(v.lifeBoost); o.i64(v.t0);
	o.u32(uint32_t(v.rel.size()));
	for (const auto& kv : v.rel) {
		const Relation& r = kv.second;
		o.i32(kv.first.first); o.i32(kv.first.second);
		o.f64(fin(r.v)); o.boolean(r.war); o.f64(fin(r.since)); o.f64(fin(r.truce)); o.f64(fin(r.trade));
		o.boolean(r.contact); o.i32(r.aggressor);
	}
	o.boolean(v.ark.has_value());
	if (v.ark) { o.i32(v.ark->culture); o.i32(v.ark->settlement); o.f64(fin(v.ark->year)); }
	putOptF(o, v.lastNuke); putOptF(o, v.snapEvery); putOptF(o, v.snapNext);
	putBag(o, v.num);
	o.u32(uint32_t(v.str.size()));
	for (const auto& kv : v.str) { o.str(kv.first); o.str(kv.second); }

	o.boolean(w.ending.has_value());
	if (w.ending) {
		o.u8(uint8_t(w.ending->type)); o.u8(uint8_t(w.ending->mood));
		o.i32(w.ending->culture); o.i32(w.ending->settlement); o.f64(fin(w.ending->year));
	}
	o.bytes(reinterpret_cast<const uint8_t*>("END!"), 4);
	out = std::move(o.buf);
	return true;
}

size_t sizeOf(const World& w) {
	std::vector<uint8_t> b;
	return serialize(w, b) ? b.size() : 0;
}

// ------------------------------------------------------------------ deserialize

namespace {

struct Loader {
	ByteReader r;
	int n = 0;
	std::string err;

	Loader(const uint8_t* d, size_t s) : r(d, s) {}
	bool bad(const std::string& what) { if (err.empty()) err = what; r.fail(); return false; }
	bool good() const { return r.ok() && err.empty(); }

	double num() {
		const double v = r.f64();
		if (!std::isfinite(v)) bad("non-finite number");
		return v;
	}
	std::optional<double> optNum() {
		if (!r.boolean()) return std::nullopt;
		return num();
	}
	template <class E> E enm(const char* what) {
		const uint8_t v = r.u8();
		if (int(v) >= enumCount<E>()) bad(std::string("bad ") + what);
		return E(v);
	}
	int cellRef(const char* what) {             // a cell id or -1
		const int32_t c = r.i32();
		if (c < -1 || c >= n) bad(std::string("cell out of range: ") + what);
		return c;
	}
	uint32_t count(size_t minItemBytes) {        // a list length that can fit in what is left
		const uint32_t k = r.u32();
		if (minItemBytes > 0 && size_t(k) > r.remaining() / minItemBytes) bad("list length");
		return k;
	}
	NumBag bag() {
		NumBag b;
		const uint32_t k = count(12);
		for (uint32_t i = 0; i < k && good(); i++) { std::string key = r.str(); b[key] = num(); }
		return b;
	}
	std::vector<int32_t> cells(const char* what) {
		std::vector<int32_t> v;
		const uint32_t k = count(4);
		v.reserve(k);
		for (uint32_t i = 0; i < k && good(); i++) v.push_back(cellRef(what));
		return v;
	}
};

bool readSettlement(Loader& L, Settlement& s) {
	ByteReader& r = L.r;
	s.id = r.i32(); s.cell = L.cellRef("settlement.cell"); s.name = r.str(); s.culture = r.i32();
	s.founded = L.num(); s.dead = r.boolean(); s.deadYear = L.optNum(); s.cause = L.enm<Cause>("cause");
	s.pop = L.num(); s.tier = r.i32(); s.K = L.num(); s.growth = L.num(); s.radius = r.i32();
	const uint8_t nb = r.u8();
	for (uint8_t k = 0; k < nb && L.good(); k++) {
		const uint8_t v = r.u8();
		if (k < Building::COUNT) s.b[k] = v ? 1 : 0;
	}
	s.devotion = L.num(); s.love = L.num(); s.jaded = L.num();
	s.plague = L.num(); s.immune = L.num(); s.inspire = L.num(); s.blessed = L.num();
	s.famine = r.i32();
	if (r.boolean()) { Prayer p; p.kind = L.enm<PrayerKind>("prayer"); p.since = L.num(); s.prayer = p; }
	s.apart = r.i32(); s.capital = r.boolean(); s.ark = L.num(); s.lastExpand = L.num();
	return L.good();
}

bool readCulture(Loader& L, Culture& c) {
	ByteReader& r = L.r;
	c.id = r.i32(); c.name = r.str(); c.adj = r.str(); c.plural = r.str();
	for (double& v : c.color) v = L.num();
	c.lang = r.str(); c.parent = r.i32(); c.born = L.num(); c.dead = r.boolean();
	c.age = r.i32(); c.science = L.num(); c.capital = r.i32(); c.godName = r.str(); c.epithet = r.str();
	c.traits.piety = L.num(); c.traits.aggression = L.num(); c.traits.curiosity = L.num(); c.traits.seafaring = L.num();
	if (r.boolean()) { Prophet p; p.name = r.str(); p.settlement = r.i32(); p.until = L.num(); c.prophet = p; }
	c.edict = L.enm<Edict>("edict"); c.edictSince = L.num();
	c.pop = L.num(); c.towns = r.i32(); c.exhaustion = L.num();
	c.seen = L.bag();
	c.lastSplit = L.num();
	if (c.age < 0 || c.age >= Age::COUNT) L.bad("culture.age");
	return L.good();
}

bool readUnit(Loader& L, Unit& u) {
	ByteReader& r = L.r;
	u.id = r.i32(); u.type = L.enm<UnitType>("unit.type"); u.culture = r.i32(); u.from = r.i32(); u.to = r.i32();
	u.dest = L.cellRef("unit.dest");
	u.path = L.cells("unit.path");
	u.i = r.i32(); u.t = L.num(); u.speed = L.num(); u.size = L.num();
	u.naval = L.enm<NavalMode>("unit.naval"); u.sea = r.boolean(); u.born = L.num();
	u.data = L.bag();
	return L.good();
}

bool readDisaster(Loader& L, Disaster& d) {
	ByteReader& r = L.r;
	d.id = r.i32(); d.type = L.enm<DisasterType>("disaster.type"); d.cell = L.cellRef("disaster.cell");
	d.t = L.num(); d.dur = L.num(); d.power = L.num(); d.ang = L.num(); d.natural = r.boolean();
	d.eta = L.num(); d.flight = L.num(); d.front = L.num();
	d.cells = L.cells("disaster.cells");
	d.data = L.bag();
	return L.good();
}

bool readEntry(Loader& L, ChronicleEntry& e) {
	ByteReader& r = L.r;
	e.id = r.i32(); e.year = L.num(); e.type = r.str(); e.text = r.str();
	e.cell = L.cellRef("chronicle.cell"); e.culture = r.i32(); e.weight = r.i32();
	return L.good();
}

// Cross-references between entities (added by the foundation verifier): modules index arrays with
// these ids (w.settlements[w.owner[c]], EconomyTune::radius[s.tier], u.path[u.i]), so a save whose ids
// point outside the lists is rejected instead of crashing the game later. -1 always means "none".
bool refsOk(Loader& L, const World& w) {
	const int ns = int(w.settlements.size()), nc = int(w.cultures.size());
	auto in = [](int v, int count) { return v >= -1 && v < count; };
	if (w.age < 0 || w.age >= Age::COUNT) return L.bad("world.age");
	for (int c = 0; c < w.n; c++) if (!in(w.owner[size_t(c)], ns)) return L.bad("owner[] refers to no settlement");
	for (const Settlement& s : w.settlements) {
		if (!in(s.culture, nc)) return L.bad("settlement.culture");
		if (s.tier < 0 || s.tier > 4) return L.bad("settlement.tier");
		if (s.radius < 0 || s.radius > 64) return L.bad("settlement.radius");
	}
	for (const Culture& c : w.cultures) {
		if (!in(c.parent, nc) || !in(c.capital, ns)) return L.bad("culture.parent / capital");
		if (c.prophet && !in(c.prophet->settlement, ns)) return L.bad("culture.prophet.settlement");
	}
	for (const Unit& u : w.units) {
		if (!in(u.culture, nc) || !in(u.from, ns) || !in(u.to, ns)) return L.bad("unit culture / from / to");
		if (u.i < 0 || (u.i > 0 && size_t(u.i) >= u.path.size())) return L.bad("unit.i");
	}
	for (const auto& kv : w.vars.rel) {
		if (kv.first.first < 0 || kv.first.second >= nc || !in(kv.second.aggressor, nc)) return L.bad("vars.rel ids");
	}
	if (w.vars.ark && (!in(w.vars.ark->culture, nc) || !in(w.vars.ark->settlement, ns))) return L.bad("vars.ark ids");
	if (w.ending && (!in(w.ending->culture, nc) || !in(w.ending->settlement, ns))) return L.bad("ending ids");
	for (const ChronicleEntry& e : w.chronicle) if (!in(e.culture, nc)) return L.bad("chronicle.culture");
	return true;
}

std::unique_ptr<World> load(Loader& L) {
	ByteReader& r = L.r;
	const uint8_t* magic = r.take(4);
	if (!magic || std::memcmp(magic, kFormat, 4) != 0) { L.bad("format"); return nullptr; }
	if (r.u32() != kVersion) { L.bad("version"); return nullptr; }
	const uint32_t seed = r.u32();
	std::string nm = r.str(1u << 16);
	const int Lv = r.u8();
	const uint32_t n = r.u32();
	if (!r.ok()) { L.bad("truncated header"); return nullptr; }
	if (Lv > Geo::kMaxLevel) { L.bad("L"); return nullptr; }
	if (n != uint32_t(Geo::prefix(Lv))) { L.bad("n"); return nullptr; }
	L.n = int(n);
	const uint32_t rngState = r.u32();

	CreateOpts co;
	co.seed = seed; co.name = nm; co.L = Lv;
	std::unique_ptr<World> w = create(co);
	w->rng.s = rngState;
	w->year = L.num(); w->tick = r.i64(); w->phase = L.enm<Phase>("phase"); w->heat = L.num();
	w->seaLevel = L.num(); w->globalTemp = L.num(); w->co2 = L.num(); w->dust = L.num(); w->age = r.i32();
	if (!L.good()) return nullptr;

	const int nf = r.u8();
	for (int k = 0; k < nf && L.good(); k++) {
		const std::string fname = r.str(64);
		const uint8_t codec = r.u8(), type = r.u8();
		if (codec > uint8_t(SaveCodec::Raw) || codec == 0 || type > uint8_t(FieldType::I32)) { L.bad("cells." + fname + ": codec"); break; }
		const SaveCodec sc = SaveCodec(codec);
		const FieldType ft = FieldType(type);
		const size_t bytesPer = sc == SaveCodec::Q8 ? 1 : sc == SaveCodec::Q16 ? 2 : ft == FieldType::U8 ? 1 : ft == FieldType::I16 ? 2 : 4;
		const uint8_t* p = r.take(bytesPer * n);
		if (!p) { L.bad("cells." + fname + ": truncated"); break; }
		const int idx = fieldIndex(fname);
		if (idx < 0) continue;                                  // a field this build does not know
		const CellField& f = kCellFields[idx];
		if (f.save != sc || f.type != ft) { L.bad("cells." + fname + ": encoding differs"); break; }
		const CellArray a = cellArray(*w, idx);
		for (size_t c = 0; c < n; c++) {
			const uint8_t* q = p + c * bytesPer;
			if (sc == SaveCodec::Q8) a.set(c, dequantize(f, q[0]));
			else if (sc == SaveCodec::Q16) a.set(c, dequantize(f, uint32_t(q[0] | (q[1] << 8))));
			else {
				switch (ft) {
				case FieldType::U8: static_cast<uint8_t*>(a.data)[c] = q[0]; break;
				case FieldType::I16: static_cast<int16_t*>(a.data)[c] = int16_t(uint16_t(q[0] | (q[1] << 8))); break;
				case FieldType::I32: {
					uint32_t u = uint32_t(q[0]) | (uint32_t(q[1]) << 8) | (uint32_t(q[2]) << 16) | (uint32_t(q[3]) << 24);
					static_cast<int32_t*>(a.data)[c] = int32_t(u);
					break;
				}
				case FieldType::F32: {
					uint32_t u = uint32_t(q[0]) | (uint32_t(q[1]) << 8) | (uint32_t(q[2]) << 16) | (uint32_t(q[3]) << 24);
					float fv; std::memcpy(&fv, &u, 4);
					static_cast<float*>(a.data)[c] = std::isfinite(fv) ? fv : float(f.def);
					break;
				}
				}
			}
		}
	}
	if (!L.good()) return nullptr;

	God& g = w->god;
	g.awe = L.num(); g.aweMax = L.num(); g.mercy = L.num(); g.wrath = L.num(); g.epithet = r.str();
	{
		const uint32_t k = L.count(8);
		for (uint32_t i = 0; i < k && L.good(); i++) { std::string key = r.str(); g.deeds[key] = r.i32(); }
	}

	uint32_t k = L.count(40);
	w->settlements.resize(L.good() ? k : 0);
	for (uint32_t i = 0; i < k && L.good(); i++) {
		readSettlement(L, w->settlements[i]);
		if (w->settlements[i].id != int(i)) L.bad("settlement id");
	}
	k = L.count(40);
	w->cultures.resize(L.good() ? k : 0);
	for (uint32_t i = 0; i < k && L.good(); i++) {
		readCulture(L, w->cultures[i]);
		if (w->cultures[i].id != int(i)) L.bad("culture id");
	}
	k = L.count(20);
	w->units.resize(L.good() ? k : 0);
	for (uint32_t i = 0; i < k && L.good(); i++) readUnit(L, w->units[i]);
	k = L.count(20);
	w->disasters.resize(L.good() ? k : 0);
	for (uint32_t i = 0; i < k && L.good(); i++) readDisaster(L, w->disasters[i]);
	k = L.count(20);
	w->chronicle.resize(L.good() ? k : 0);
	for (uint32_t i = 0; i < k && L.good(); i++) readEntry(L, w->chronicle[i]);
	if (!L.good()) return nullptr;

	w->nextId.unit = r.i32(); w->nextId.disaster = r.i32(); w->nextId.entry = r.i32();
	w->stats.pop = L.num(); w->stats.peakPop = L.num(); w->stats.believers = L.num();
	w->stats.wars = r.i32(); w->stats.miracles = r.i32();
	w->settings.calamity = L.num();

	Vars& v = w->vars;
	v.seaBase = L.optNum(); v.iceRef = L.optNum(); v.lifeBoost = L.num(); v.t0 = r.i64();
	k = L.count(30);
	for (uint32_t i = 0; i < k && L.good(); i++) {
		const int a = r.i32(), b = r.i32();
		Relation rel;
		rel.v = L.num(); rel.war = r.boolean(); rel.since = L.num(); rel.truce = L.num(); rel.trade = L.num();
		rel.contact = r.boolean(); rel.aggressor = r.i32();
		if (a >= b) L.bad("vars.rel key");
		v.rel[std::pair<int, int>(a, b)] = rel;
	}
	if (r.boolean()) { ArkRecord ark; ark.culture = r.i32(); ark.settlement = r.i32(); ark.year = L.num(); v.ark = ark; }
	v.lastNuke = L.optNum(); v.snapEvery = L.optNum(); v.snapNext = L.optNum();
	v.num = L.bag();
	k = L.count(8);
	for (uint32_t i = 0; i < k && L.good(); i++) { std::string key = r.str(); v.str[key] = r.str(); }

	if (r.boolean()) {
		Ending e;
		e.type = L.enm<EndingType>("ending.type"); e.mood = L.enm<Mood>("ending.mood");
		e.culture = r.i32(); e.settlement = r.i32(); e.year = L.num();
		w->ending = e;
	}
	const uint8_t* tail = r.take(4);
	if (!tail || std::memcmp(tail, "END!", 4) != 0) L.bad("end marker");
	if (!r.atEnd()) L.bad("trailing bytes");
	if (!L.good()) return nullptr;
	if (!refsOk(L, *w)) return nullptr;
	return w;
}

} // namespace

std::unique_ptr<World> deserialize(const uint8_t* data, size_t size, std::string* why) {
	if (!data || size == 0) { if (why) *why = "bad save: empty"; return nullptr; }
	Loader L(data, size);
	std::unique_ptr<World> w = load(L);
	if (!w || !L.good()) {
		if (why) *why = "bad save: " + (L.err.empty() ? std::string("truncated") : L.err);
		return nullptr;
	}
	return w;
}

} // namespace state
} // namespace dm
