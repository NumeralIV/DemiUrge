// DEMIURGE — SimTests/test_state.cpp : State.h, Schema.h, the test world helper and the map tool
// (port of tests/state.test.js; the save format is binary in C++).
#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>

#include "MapImg.h"
#include "State.h"
#include "TestFramework.h"
#include "TestWorld.h"

using namespace dm;
using namespace dmt;

namespace {

std::unique_ptr<World> mk(int L, uint32_t seed = 1) {
	state::CreateOpts o;
	o.seed = seed;
	o.L = L;
	o.geo = sharedGeo(L);
	return state::create(o);
}

// Fill every persisted cell field with random in-range (and some out-of-range) values.
void randomCells(World& w, Rng& r) {
	for (int i = 0; i < state::kCellFieldCount; i++) {
		const CellField& f = state::kCellFields[i];
		const CellArray a = state::cellArray(w, i);
		for (size_t c = 0; c < a.size; c++) {
			if (f.save == SaveCodec::Q8 || f.save == SaveCodec::Q16) a.set(c, r.range(f.min, f.max));
			else if (f.type == FieldType::U8) a.set(c, r.integer(0, 255));
			else if (f.type == FieldType::I16) a.set(c, r.integer(-1, 3000));
			else if (f.type == FieldType::I32) a.set(c, r.integer(-1, w.n - 1));
			else a.set(c, r.range(-50, 50));
		}
	}
}

// A world with every kind of entity and non-default value in it.
std::unique_ptr<World> populated(int L) {
	auto w = mk(L, 42);
	Rng r(5);
	randomCells(*w, r);
	w->year = 1234.25; w->tick = 98765; w->phase = Phase::History; w->heat = 0; w->seaLevel = 0.37; w->globalTemp = 1.5;
	w->co2 = 77.5; w->dust = 0.125; w->age = 3; w->name = "Ölmar"; w->rng = Rng(777);
	w->god.awe = 55.5; w->god.aweMax = 300; w->god.mercy = 3.25; w->god.wrath = 1.75; w->god.epithet = "the Rain-Giver";
	w->god.deeds["rain"] = 12; w->god.deeds["meteor"] = 1;
	Culture& c0 = state::newCulture(*w);
	c0.name = "Umar"; c0.adj = "Umaran"; c0.plural = "the Umari"; c0.lang = "v1|C=ptk|V=aiu"; c0.age = 3; c0.science = 321.5;
	c0.capital = 0; c0.godName = "Ashu"; c0.epithet = "the Green Hand"; c0.traits.piety = 0.9; c0.prophet = Prophet{ "Dovak", 1, 1290 };
	c0.edict = Edict::Build; c0.edictSince = 1250; c0.pop = 5000; c0.towns = 2; c0.exhaustion = 0.25; c0.seen["rain"] = 0.75; c0.lastSplit = 900;
	Culture& c1 = state::newCulture(*w, 0);
	c1.name = "Vorn"; c1.dead = true; c1.born = 1000;
	for (int i = 0; i < 3; i++) {
		Settlement& s = state::newSettlement(*w, 100 + i * 7, i == 2 ? 1 : 0);
		s.name = "Tel Asha " + std::to_string(i); s.pop = 1000.5 + i; s.tier = 2; s.K = 1500; s.growth = 12.5; s.radius = 4;
		s.b[Building::SHRINE] = 1; s.b[Building::WONDER] = uint8_t(i == 1); s.devotion = 0.6; s.love = -0.25; s.jaded = 0.1;
		s.plague = 0.3; s.immune = 5; s.inspire = 2; s.blessed = 7; s.famine = 1; s.apart = 3; s.capital = i == 0; s.ark = 0.5;
		if (i == 1) s.prayer = Prayer{ PrayerKind::Heal, 1200 };
		if (i == 2) { s.dead = true; s.deadYear = 1230; s.cause = Cause::Wave; }
		s.cells = { 1, 2, 3 };                    // transient
	}
	// owner[] must name an existing settlement (deserialize rejects dangling references)
	for (int16_t& o : w->owner) if (o >= 3) o = int16_t(o % 3);
	Unit& u = state::newUnit(*w, UnitType::Army);
	u.culture = 0; u.from = 0; u.to = 1; u.dest = 107; u.path = { 100, 101, 107 }; u.i = 1; u.t = 0.5; u.speed = 5; u.size = 60;
	u.naval = NavalMode::Coastal; u.sea = true; u.data["target"] = 1;
	Disaster& d = state::newDisaster(*w, DisasterType::Volcano, 300);
	d.t = 1.5; d.dur = 12; d.power = 0.8; d.ang = 0.04; d.natural = true; d.cells = { 300, 301 }; d.data["lavaN"] = 2;
	Disaster& m = state::newDisaster(*w, DisasterType::Meteor, 12);
	m.eta = 1.2; m.flight = 1.6;
	ChronicleEntry e;
	e.id = w->nextId.entry++; e.year = 1100; e.type = "founded"; e.text = "In the year 1100 the Umari raised Tel Asha."; e.cell = 100; e.culture = 0; e.weight = 2;
	w->chronicle.push_back(e);
	w->stats = { 3001.5, 4000, 1800, 2, 9 };
	w->settings.calamity = 0.5;
	w->vars.seaBase = 0.1; w->vars.iceRef = 812; w->vars.lifeBoost = 1; w->vars.t0 = 400;
	Relation rel; rel.v = -0.6; rel.war = true; rel.since = 1200; rel.truce = 1100; rel.trade = 0.3; rel.contact = true; rel.aggressor = 1;
	w->vars.rel[state::relKey(1, 0)] = rel;
	w->vars.ark = ArkRecord{ 0, 0, 1233 }; w->vars.lastNuke = 1100.5; w->vars.snapEvery = 25; w->vars.snapNext = 1250;
	w->vars.num["civ_lastSearch"] = 1230; w->vars.str["chronicle_lastTemplate"] = "founded:3";
	w->ending = Ending{ EndingType::Ark, Mood::Beloved, 0, 0, 1233 };
	w->cache.landId.assign(size_t(w->n), 0); w->cache.fires = { 5 };     // transient
	return w;
}

bool sameEntities(const World& a, const World& b, std::string& why) {
	auto bad = [&](const std::string& s) { why = s; return false; };
	if (a.settlements.size() != b.settlements.size()) return bad("settlement count");
	for (size_t i = 0; i < a.settlements.size(); i++) {
		const Settlement &x = a.settlements[i], &y = b.settlements[i];
		if (x.id != y.id || x.cell != y.cell || x.name != y.name || x.culture != y.culture || x.founded != y.founded || x.dead != y.dead ||
		    x.deadYear != y.deadYear || x.cause != y.cause || x.pop != y.pop || x.tier != y.tier || x.K != y.K || x.growth != y.growth ||
		    x.radius != y.radius || x.b != y.b || x.devotion != y.devotion || x.love != y.love || x.jaded != y.jaded || x.plague != y.plague ||
		    x.immune != y.immune || x.inspire != y.inspire || x.blessed != y.blessed || x.famine != y.famine || x.prayer != y.prayer ||
		    x.apart != y.apart || x.capital != y.capital || x.ark != y.ark || x.lastExpand != y.lastExpand) return bad("settlement " + std::to_string(i));
	}
	if (a.cultures.size() != b.cultures.size()) return bad("culture count");
	for (size_t i = 0; i < a.cultures.size(); i++) {
		const Culture &x = a.cultures[i], &y = b.cultures[i];
		if (x.id != y.id || x.name != y.name || x.adj != y.adj || x.plural != y.plural || x.color != y.color || x.lang != y.lang ||
		    x.parent != y.parent || x.born != y.born || x.dead != y.dead || x.age != y.age || x.science != y.science || x.capital != y.capital ||
		    x.godName != y.godName || x.epithet != y.epithet || !(x.traits == y.traits) || x.prophet != y.prophet || x.edict != y.edict ||
		    x.edictSince != y.edictSince || x.pop != y.pop || x.towns != y.towns || x.exhaustion != y.exhaustion || x.seen != y.seen ||
		    x.lastSplit != y.lastSplit) return bad("culture " + std::to_string(i));
	}
	if (a.units.size() != b.units.size()) return bad("unit count");
	for (size_t i = 0; i < a.units.size(); i++) {
		const Unit &x = a.units[i], &y = b.units[i];
		if (x.id != y.id || x.type != y.type || x.culture != y.culture || x.from != y.from || x.to != y.to || x.dest != y.dest || x.path != y.path ||
		    x.i != y.i || x.t != y.t || x.speed != y.speed || x.size != y.size || x.naval != y.naval || x.sea != y.sea || x.born != y.born ||
		    x.data != y.data) return bad("unit " + std::to_string(i));
	}
	if (a.disasters.size() != b.disasters.size()) return bad("disaster count");
	for (size_t i = 0; i < a.disasters.size(); i++) {
		const Disaster &x = a.disasters[i], &y = b.disasters[i];
		if (x.id != y.id || x.type != y.type || x.cell != y.cell || x.t != y.t || x.dur != y.dur || x.power != y.power || x.ang != y.ang ||
		    x.natural != y.natural || x.eta != y.eta || x.flight != y.flight || x.front != y.front || x.cells != y.cells || x.data != y.data)
			return bad("disaster " + std::to_string(i));
	}
	if (a.chronicle != b.chronicle) return bad("chronicle");
	return true;
}

} // namespace

// ---------------------------------------------------------------- tables

DM_TEST("state: kCellFields is the contract table (order, types, defaults, codecs, ranges)") {
	struct Row { const char* name; FieldType t; double def; SaveCodec s; double mn, mx; };
	const Row want[] = {
		{ "elev", FieldType::F32, 0, SaveCodec::Q16, -12, 20 }, { "water", FieldType::F32, 0, SaveCodec::Q16, 0, 8 },
		{ "flow", FieldType::F32, 0, SaveCodec::None, 0, 0 }, { "down", FieldType::I32, -1, SaveCodec::None, 0, 0 },
		{ "temp", FieldType::F32, 0, SaveCodec::None, 0, 0 }, { "moist", FieldType::F32, 0, SaveCodec::None, 0, 0 },
		{ "wet", FieldType::F32, 0, SaveCodec::Q8, 0, 1 }, { "veg", FieldType::F32, 0, SaveCodec::Q8, 0, 1 },
		{ "biome", FieldType::U8, 0, SaveCodec::None, 0, 0 }, { "fert", FieldType::F32, 0.5, SaveCodec::Q8, 0, 1.5 },
		{ "ice", FieldType::F32, 0, SaveCodec::Q8, 0, 1 }, { "fire", FieldType::F32, 0, SaveCodec::Q8, 0, 1 },
		{ "scorch", FieldType::F32, 0, SaveCodec::Q8, 0, 1 }, { "lava", FieldType::F32, 0, SaveCodec::Q16, 0, 8 },
		{ "fauna", FieldType::F32, 0, SaveCodec::Q8, 0, 1 }, { "rad", FieldType::F32, 0, SaveCodec::Q8, 0, 1 },
		{ "owner", FieldType::I16, -1, SaveCodec::Raw, 0, 0 }, { "farm", FieldType::F32, 0, SaveCodec::Q8, 0, 1 },
		{ "urban", FieldType::F32, 0, SaveCodec::Q8, 0, 1 }, { "road", FieldType::U8, 0, SaveCodec::Raw, 0, 0 },
		{ "hab", FieldType::F32, 0, SaveCodec::None, 0, 0 }, { "slope", FieldType::F32, 0, SaveCodec::None, 0, 0 },
		{ "coast", FieldType::U8, 255, SaveCodec::None, 0, 0 }, { "fresh", FieldType::U8, 255, SaveCodec::None, 0, 0 },
	};
	if (!eq(state::kCellFieldCount, 24, "field count")) return;
	for (int i = 0; i < 24; i++) {
		const CellField& f = state::kCellFields[i];
		ok(std::string(f.name) == want[i].name && f.type == want[i].t && f.def == want[i].def && f.save == want[i].s &&
		   (f.save == SaveCodec::None || f.save == SaveCodec::Raw || (f.min == want[i].mn && f.max == want[i].mx)), std::string("row ") + want[i].name);
		eq(state::fieldIndex(f.name), i, "fieldIndex");
	}
	eq(state::fieldIndex("nope"), -1);
	eq(state::kElevMin, -12.0); eq(state::kElevMax, 20.0);
	eq(state::kCellFields[0].min, state::kElevMin, "elev q16 range = the elev clamp");
}

DM_TEST("schema: biomes, ages, buildings, colours, VEG_T, enum names") {
	const char* bn[] = { "Sea", "Shallows", "Beach", "Desert", "Savanna", "Grassland", "Forest", "Rainforest", "Taiga", "Tundra", "Ice", "Rock", "Snow peak", "Wetland", "Barren", "Sea ice" };
	for (int i = 0; i < Biome::COUNT; i++) eq(std::string(kBiomeNames[i]), std::string(bn[i]));
	ok(Biome::SEA == 0 && Biome::SHALLOWS == 1 && Biome::RAINFOREST == 7 && Biome::SNOWPEAK == 12 && Biome::SEAICE == 15 && Biome::COUNT == 16, "BIOME ids");
	const char* an[] = { "Stone", "Bronze", "Iron", "Sail", "Industry", "Atomic", "Space" };
	for (int i = 0; i < Age::COUNT; i++) eq(std::string(kAgeNames[i]), std::string(an[i]));
	const char* bl[] = { "shrine", "temple", "granary", "walls", "port", "market", "mine", "factory", "academy", "plant", "launch", "wonder" };
	eq(int(Building::COUNT), 12);
	for (int i = 0; i < Building::COUNT; i++) eq(std::string(kBuildingNames[i]), std::string(bl[i]));
	eq(kCultureColors.size(), size_t(6));
	eq(kCultureColors[1][2], 0.807);
	ok(state::kVegT[0] == 0.22 && state::kVegT[1] == 0.5 && state::kVegT[2] == 0.78, "VEG_T");
	eq(std::string(toStr(Phase::History)), std::string("history"));
	eq(std::string(toStr(MilestoneKind::SeaRise)), std::string("sea-rise"));
	eq(std::string(toStr(Cause::Lightning)), std::string("lightning"));
	Phase p; Mood m; NavalMode nm;
	ok(fromStr("ended", p) && p == Phase::Ended && fromStr("ash", m) && m == Mood::Ash && fromStr("coastal", nm) && nm == NavalMode::Coastal, "fromStr");
	ok(!fromStr("Ended", p), "fromStr is exact");
	for (int i = 0; i < enumCount<Cause>(); i++) { Cause c; ok(fromStr(toStr(Cause(i)), c) && c == Cause(i), "Cause names round-trip"); }
}

// ---------------------------------------------------------------- create

DM_TEST("state::create: cell fields sized n at their defaults; scalars and bags at the contract defaults") {
	auto w = mk(4, 7);
	eq(w->n, 2562); eq(w->L, 4); eq(w->seed, 7u); eq(w->rng.s, 7u); eq(w->name, std::string("Unnamed")); eq(w->version, 1);
	for (int i = 0; i < state::kCellFieldCount; i++) {
		const CellArray a = state::cellArray(*w, i);
		bool all = a.size == size_t(w->n);
		for (size_t c = 0; c < a.size && all; c++) all = a.get(c) == state::kCellFields[i].def;
		ok(all, std::string("field ") + state::kCellFields[i].name);
	}
	ok(w->year == 0 && w->tick == 0 && w->phase == Phase::Molten && w->heat == 1 && w->seaLevel == -9 && w->globalTemp == 0 &&
	   w->co2 == 0 && w->dust == 0 && w->age == 0, "scalars");
	ok(w->god.awe == 0 && w->god.aweMax == 100 && w->god.mercy == 0 && w->god.wrath == 0 && w->god.epithet.empty() && w->god.deeds.empty(), "god");
	ok(w->settlements.empty() && w->cultures.empty() && w->units.empty() && w->disasters.empty() && w->rivers.empty() && w->chronicle.empty() && w->snapshots.empty(), "lists");
	ok(w->nextId.unit == 1 && w->nextId.disaster == 1 && w->nextId.entry == 1, "nextId");
	ok(w->stats.pop == 0 && w->stats.peakPop == 0 && w->stats.believers == 0 && w->stats.wars == 0 && w->stats.miracles == 0, "stats");
	ok(w->settings.calamity == 1 && !w->vars.seaBase && w->vars.lifeBoost == 1 && w->vars.rel.empty() && !w->ending, "settings / vars / ending");
	ok(w->dirty.elev && w->dirty.color && w->dirty.water && w->dirty.objects && w->dirty.rivers && w->dirty.roads, "dirty all set");
	ok(w->need.derive && w->need.climate && w->need.hydro && w->need.life, "need all set");
	ok(w->cache.landId.empty() && w->cache.fires.empty(), "no caches");
	auto d = state::create();
	eq(d->L, 6, "L defaults to 6"); eq(d->n, 40962);
	auto e = state::create({ .seed = 5, .L = 3 });          // the designated-initializer form the docs use
	ok(e->seed == 5 && e->L == 3 && e->name == "Unnamed", "CreateOpts designated initializers");
	state::CreateOpts big; big.L = 99;
	eq(state::create(big)->L, 7, "L clamped");
}

DM_TEST("state::create: same seed -> identical worlds; worlds share nothing but a passed-in geo") {
	auto a = makeWorld({ .L = 4, .seed = 3 }), b = makeWorld({ .L = 4, .seed = 3 }), c = makeWorld({ .L = 4, .seed = 4 });
	eq(hash(*a), hash(*b), "same seed, same hash");
	ok(hash(*a) != hash(*c), "different seed, different hash");
	a->elev[0] += 1;
	ok(hash(*a) != hash(*b), "changing a cell changes the hash");
	ok(a->elev.data() != b->elev.data(), "separate arrays");
	ok(a->geo == b->geo, "makeWorld shares one Geo per level");
	auto p = mk(3), q = mk(3);
	p->settlements.push_back(Settlement{});
	ok(q->settlements.empty(), "entity lists are per world");
}

DM_TEST("state: isSea, groundR, surfaceR") {
	auto w = mk(2);
	w->seaLevel = 0.5;
	w->elev[3] = 0.2f; w->elev[4] = 0.5f; w->elev[5] = 2.0f; w->water[5] = 0.75f; w->water[3] = 9;
	ok(state::isSea(*w, 3) && !state::isSea(*w, 4) && !state::isSea(*w, 5), "sea iff elev < seaLevel");
	near(state::groundR(*w, 5), 102, 1e-6); near(state::groundR(*w, 3), 100.2, 1e-6);
	near(state::surfaceR(*w, 3), 100.5, 1e-12, "sea cell: sea surface (water ignored)");
	near(state::surfaceR(*w, 5), 102.75, 1e-6, "land: ground + lake water");
	near(state::surfaceR(*w, 4), 100.5, 1e-6, "elev == seaLevel is land");
}

// ---------------------------------------------------------------- serialize / deserialize

DM_TEST("serialize: marker, version, layout of the header and of a known cell") {
	auto w = mk(1, 0xABCDEF01u);
	w->elev[0] = 4.0f;                                      // q16: (4 + 12) / 32 * 65535 = 32767.5 -> 32768
	w->veg[1] = 0.5f;                                       // q8: 127.5 -> 128
	w->owner[2] = 7;
	for (int i = 0; i < 8; i++) state::newSettlement(*w, i);   // owner 7 must exist
	std::vector<uint8_t> b;
	if (!ok(state::serialize(*w, b), "serializes")) return;
	ok(std::memcmp(b.data(), "DMWD", 4) == 0, "format marker");
	eq(uint32_t(b[4] | (b[5] << 8)), 1u, "version 1, little-endian");
	eq(uint32_t(b[8]) | uint32_t(b[9]) << 8 | uint32_t(b[10]) << 16 | uint32_t(b[11]) << 24, 0xABCDEF01u, "seed");
	eq(state::quantize(state::kCellFields[0], 4.0), 32768u);
	eq(state::quantize(state::kCellFields[7], 0.5), 128u);
	ok(std::memcmp(b.data() + b.size() - 4, "END!", 4) == 0, "end marker");
	auto back = state::deserialize(b);
	if (!ok(back != nullptr, "loads")) return;
	near(back->elev[0], 4, 32.0 / 65535, "elev within half a q16 step");
	near(back->veg[1], 0.5, 0.5 / 255 + 1e-7, "veg within half a q8 step");
	eq(back->owner[2], int16_t(7), "raw field exact");
	eq(back->seed, 0xABCDEF01u);
}

DM_TEST("round trip: random cells (L = 4 and L = 6), scalars and entities exactly, rng continues") {
	for (int L : { 4, 6 }) {
		auto w = populated(L);
		std::vector<uint8_t> b;
		if (!ok(state::serialize(*w, b), "serializes")) return;
		std::string why;
		auto r = state::deserialize(b, &why);
		if (!ok(r != nullptr, "loads: " + why)) return;
		for (int i = 0; i < state::kCellFieldCount; i++) {
			const CellField& f = state::kCellFields[i];
			const CellArray a = state::cellArray(*w, i), q = state::cellArray(*r, i);
			double worst = 0;
			bool exact = true, defaults = true;
			for (size_t c = 0; c < a.size; c++) {
				if (f.save == SaveCodec::Q8 || f.save == SaveCodec::Q16) worst = std::max(worst, std::fabs(a.get(c) - q.get(c)));
				else if (f.save == SaveCodec::Raw) exact &= a.get(c) == q.get(c);
				else defaults &= q.get(c) == f.def;
			}
			const double step = (f.max - f.min) / (f.save == SaveCodec::Q16 ? 65535.0 : 255.0);
			if (f.save == SaveCodec::Q8 || f.save == SaveCodec::Q16) ok(worst <= step / 2 + 1e-6, std::string(f.name) + " within half a step: " + std::to_string(worst));
			if (f.save == SaveCodec::Raw) ok(exact, std::string(f.name) + " exact");
			if (f.save == SaveCodec::None) ok(defaults, std::string(f.name) + " (derived) back at its default");
		}
		ok(r->name == w->name && r->year == w->year && r->tick == w->tick && r->phase == w->phase && r->heat == w->heat &&
		   r->seaLevel == w->seaLevel && r->globalTemp == w->globalTemp && r->co2 == w->co2 && r->dust == w->dust && r->age == w->age, "scalars");
		ok(r->god.awe == w->god.awe && r->god.aweMax == w->god.aweMax && r->god.mercy == w->god.mercy && r->god.wrath == w->god.wrath &&
		   r->god.epithet == w->god.epithet && r->god.deeds == w->god.deeds, "god");
		std::string ew;
		ok(sameEntities(*w, *r, ew), "entities: " + ew);
		ok(r->nextId.unit == w->nextId.unit && r->nextId.disaster == w->nextId.disaster && r->nextId.entry == w->nextId.entry, "nextId");
		ok(r->stats.pop == w->stats.pop && r->stats.peakPop == w->stats.peakPop && r->stats.believers == w->stats.believers &&
		   r->stats.wars == w->stats.wars && r->stats.miracles == w->stats.miracles && r->settings.calamity == w->settings.calamity, "stats / settings");
		const Vars &a = w->vars, &v = r->vars;
		ok(a.seaBase == v.seaBase && a.iceRef == v.iceRef && a.lifeBoost == v.lifeBoost && a.t0 == v.t0 && a.rel == v.rel && a.ark == v.ark &&
		   a.lastNuke == v.lastNuke && a.snapEvery == v.snapEvery && a.snapNext == v.snapNext && a.num == v.num && a.str == v.str, "vars");
		ok(r->ending == w->ending, "ending");
		ok(r->cache.landId.empty() && r->cache.fires.empty(), "caches not saved");
		bool noCells = true;
		for (const Settlement& s : r->settlements) noCells &= s.cells.empty();
		ok(noCells, "Settlement::cells (transient) not saved");
		ok(r->dirty.elev && r->dirty.objects && r->need.derive && r->need.life, "dirty / need all set after loading");
		std::vector<uint8_t> b2;
		state::serialize(*r, b2);
		ok(b2 == b, "load -> save reproduces the save byte for byte");
		bool same = true;
		for (int i = 0; i < 100; i++) same &= r->rng.next() == w->rng.next();
		ok(same, "rng continues the same stream");
	}
}

DM_TEST("round trip: a new world comes back at its defaults, fert and zero fields exactly") {
	auto w = mk(4, 99);
	std::vector<uint8_t> b;
	state::serialize(*w, b);
	auto r = state::deserialize(b);
	if (!ok(r != nullptr)) return;
	for (int i = 0; i < state::kCellFieldCount; i++) {
		const CellField& f = state::kCellFields[i];
		const CellArray q = state::cellArray(*r, i);
		bool good = true;
		double worst = 0;
		for (size_t c = 0; c < q.size; c++) { good &= float(q.get(c)) == float(f.def); worst = std::max(worst, std::fabs(q.get(c) - f.def)); }
		// elev's default 0 is not on the q16 grid of -12..20 (it comes back within half a step);
		// every other default (0 on 0-based ranges, 0.5 fert, raw and derived fields) is exact
		if (std::string(f.name) == "elev") ok(worst <= 16.0 / 65535 + 1e-9, "elev within half a step");
		else ok(good, std::string(f.name) + " exactly its default");
	}
	std::vector<uint8_t> b2;
	state::serialize(*r, b2);
	auto r2 = state::deserialize(b2);
	eq(hash(*r2), hash(*r), "a second round trip changes nothing");
}

DM_TEST("quantization: clamps out-of-range values, NaN stored as the default; every code is stable") {
	auto w = mk(6);
	const int ie = state::fieldIndex("elev"), iv = state::fieldIndex("veg"), ifr = state::fieldIndex("fert");
	w->elev[0] = 99; w->elev[1] = -99; w->elev[2] = std::nanf(""); w->veg[0] = 2; w->veg[1] = -1; w->fert[0] = std::nanf("");
	w->water[0] = INFINITY;
	std::vector<uint8_t> b;
	ok(state::serialize(*w, b), "NaN / inf in cell arrays do not block saving");
	auto r = state::deserialize(b);
	if (!ok(r != nullptr)) return;
	near(r->elev[0], 20, 1e-6, "clamped high"); near(r->elev[1], -12, 1e-6, "clamped low");
	near(r->elev[2], 0, 32.0 / 65535, "NaN -> default"); near(r->fert[0], 0.5, 1e-7, "NaN fert -> 0.5");
	eq(r->veg[0], 1.0f); eq(r->veg[1], 0.0f); near(r->water[0], 8, 1e-6, "inf clamped");
	// every q16 code of elev and every q8 code of veg / fert survives decode -> float32 -> encode
	const CellField &fe = state::kCellFields[ie], &fv = state::kCellFields[iv], &ff = state::kCellFields[ifr];
	bool stable = true;
	for (uint32_t code = 0; code < 65536; code++) stable &= state::quantize(fe, float(state::dequantize(fe, code))) == code;
	for (uint32_t code = 0; code < 256; code++) {
		stable &= state::quantize(fv, float(state::dequantize(fv, code))) == code;
		stable &= state::quantize(ff, float(state::dequantize(ff, code))) == code;
	}
	ok(stable, "all 65,536 q16 codes and 256 q8 codes are stable through float32");
	for (int c = 0; c < w->n; c++) w->elev[size_t(c)] = float(state::dequantize(fe, uint32_t(c) * 1601u % 65536u));
	state::serialize(*w, b);
	auto r2 = state::deserialize(b);
	std::vector<uint8_t> b2;
	state::serialize(*r2, b2);
	ok(b == b2, "re-saving a loaded L = 6 world gives the same bytes");
}

DM_TEST("serialize refuses a world that could not be loaded back; entity NaN is written as 0") {
	auto w = mk(2);
	std::vector<uint8_t> b;
	std::string why;
	w->seaLevel = std::nan("");
	ok(!state::serialize(*w, b, &why) && b.empty(), "non-finite seaLevel refused");
	ok(why.find("seaLevel") != std::string::npos, "why names the field: " + why);
	w->seaLevel = 0;
	w->god.awe = INFINITY;
	ok(!state::serialize(*w, b, &why), "non-finite awe refused");
	w->god.awe = 0;
	w->veg.resize(3);
	ok(!state::serialize(*w, b, &why) && why.find("veg") != std::string::npos, "a cell array of the wrong length is refused");
	w->veg.assign(size_t(w->n), 0.0f);
	state::newCulture(*w);
	Settlement& s = state::newSettlement(*w, 5, 0);
	s.pop = std::nan("");
	ok(state::serialize(*w, b), "entity NaN does not block saving");
	auto r = state::deserialize(b);
	ok(r && r->settlements[0].pop == 0, "and loads as 0");
	eq(state::sizeOf(*w), b.size());
}

DM_TEST("deserialize: rejects garbage, wrong markers / versions, every truncation, trailing bytes, bad values") {
	auto w = populated(3);
	ok(state::deserialize(state::sizeOf(*w) ? [&] { std::vector<uint8_t> t; state::serialize(*w, t); return t; }() : std::vector<uint8_t>{}) != nullptr, "the intact save loads");
	std::vector<uint8_t> b;
	state::serialize(*w, b);
	std::string why;
	ok(!state::deserialize(nullptr, 0, &why) && why.find("bad save") == 0, "empty");
	std::vector<uint8_t> junk(1000);
	Rng r(3);
	for (auto& x : junk) x = uint8_t(r.next());
	ok(!state::deserialize(junk), "random bytes");
	auto bad = b; bad[0] = 'X';
	ok(!state::deserialize(bad, &why) && why == "bad save: format", "marker: " + why);
	bad = b; bad[4] = 2;
	ok(!state::deserialize(bad, &why) && why == "bad save: version", "version: " + why);
	int accepted = 0;
	for (size_t len = 0; len < b.size(); len++) if (state::deserialize(b.data(), len)) accepted++;
	eq(accepted, 0, "no truncated prefix loads (" + std::to_string(b.size()) + " prefixes tried)");
	bad = b; bad.push_back(0);
	why.clear();
	ok(!state::deserialize(bad, &why), "trailing bytes refused");
	ok(why.find("trailing") != std::string::npos, "why: " + why);
	// flip single bytes all over the file: it either loads or is refused, never crashes
	int loaded = 0, refused = 0;
	for (size_t i = 0; i < b.size(); i += 3) {
		bad = b;
		bad[i] ^= 0x5A;
		if (state::deserialize(bad)) loaded++; else refused++;
	}
	ok(refused > 0 && loaded + refused > 100, "byte flips handled: " + std::to_string(loaded) + " loaded, " + std::to_string(refused) + " refused");
	auto ww = mk(2);
	Unit& u = state::newUnit(*ww, UnitType::Settler);
	u.path = { 1, 2, 3 };
	std::vector<uint8_t> ub;
	state::serialize(*ww, ub);
	// corrupt the path's last cell id into a value beyond n (find the little-endian 3 near the end)
	for (size_t i = ub.size() - 4; i > 8; i--) {
		if (ub[i] == 3 && ub[i + 1] == 0 && ub[i + 2] == 0 && ub[i + 3] == 0 && ub[i - 4] == 2) { ub[i] = 0xFF; ub[i + 1] = 0x7F; break; }
	}
	ok(!state::deserialize(ub, &why), "a cell id beyond n is refused: " + why);
}

DM_TEST("save size: an empty L = 6 world is about 19 bytes per cell, well under 1.2 MB") {
	auto w = mk(6);
	const size_t n = state::sizeOf(*w);
	std::printf("       empty L=6 save: %zu bytes (%.2f bytes per cell)\n", n, double(n) / w->n);
	ok(n < 1200000, "under 1.2 MB");
	ok(n > size_t(19 * w->n) && n < size_t(19 * w->n + 2000), "19 bytes per cell plus a small header");
	const double t = nowMs();
	std::vector<uint8_t> b;
	for (int i = 0; i < 10; i++) state::serialize(*w, b);
	const double ms = (nowMs() - t) / 10;
	const double t2 = nowMs();
	for (int i = 0; i < 10; i++) state::deserialize(b);
	std::printf("       L=6 serialize %.2f ms, deserialize %.2f ms\n", ms, (nowMs() - t2) / 10);
}

// ---------------------------------------------------------------- schema factories

DM_TEST("schema: factories fill defaults, assign ids, push; lookups; erase") {
	auto w = mk(3);
	w->year = 250;
	Settlement& s0 = state::newSettlement(*w, 17, 0);
	eq(s0.id, 0); eq(s0.cell, 17); eq(s0.culture, 0); eq(s0.founded, 250.0); eq(s0.lastExpand, 250.0);
	ok(!s0.dead && !s0.deadYear && s0.cause == Cause::None && s0.pop == 0 && s0.tier == 0 && s0.radius == 2 && s0.devotion == 0.5 &&
	   s0.love == 0 && !s0.prayer && !s0.capital && s0.ark == 0 && s0.cells.empty(), "settlement defaults");
	bool zeroB = true;
	for (uint8_t v : s0.b) zeroB &= v == 0;
	ok(zeroB, "no buildings");
	eq(state::newSettlement(*w).id, 1, "ids are indices");
	Culture& c0 = state::newCulture(*w);
	eq(c0.id, 0); eq(c0.parent, -1); eq(c0.born, 250.0);
	ok(c0.color == kCultureColors[0], "first colour slot");
	ok(c0.traits.piety == 0.5 && c0.edict == Edict::None && c0.edictSince == -9999 && c0.lastSplit == -9999 && c0.capital == -1 && !c0.prophet, "culture defaults");
	Culture& c1 = state::newCulture(*w, 0);
	ok(c1.color == kCultureColors[1] && c1.parent == 0, "next free slot");
	w->cultures[0].dead = true;
	eq(state::freeColorSlot(*w), 0, "a dead culture frees its colour");
	Unit& u = state::newUnit(*w, UnitType::Trader);
	eq(u.id, 1); eq(w->nextId.unit, 2); eq(u.born, 250.0); ok(u.speed == 5 && u.naval == NavalMode::Land && u.path.empty(), "unit defaults");
	eq(state::newUnit(*w, UnitType::Army).id, 2);
	Disaster& d = state::newDisaster(*w, DisasterType::Storm, 9);
	eq(d.id, 1); eq(d.cell, 9); ok(d.power == 1 && d.t == 0 && !d.natural, "disaster defaults");
	ok(state::settlement(*w, 1) && !state::settlement(*w, 2) && !state::settlement(*w, -1), "settlement lookup");
	ok(state::culture(*w, 1) && !state::culture(*w, 5), "culture lookup");
	ok(state::findUnit(*w, 2) && state::findUnit(*w, 2)->type == UnitType::Army && !state::findUnit(*w, 7), "findUnit");
	ok(state::eraseUnit(*w, 1) && !state::eraseUnit(*w, 1) && w->units.size() == 1 && w->units[0].id == 2, "eraseUnit keeps ids stable");
	ok(state::findDisaster(*w, 1) && state::eraseDisaster(*w, 1) && w->disasters.empty(), "disasters");
	ok(state::relKey(5, 2) == std::make_pair(2, 5) && state::relKey(2, 5) == std::make_pair(2, 5), "relKey");
	for (int i = 0; i < 8; i++) state::newCulture(*w);
	eq(w->cultures.size(), size_t(10));
}

// ---------------------------------------------------------------- per-world module slots

namespace {
struct FakeTune { double rate = 0.03; int cap = 160; };
struct FakeScratch { std::vector<int> buf; int builtFor = -1; };
struct OtherTune { double rate = 9; };
} // namespace

DM_TEST("world: tune<T>() / scratch<T>() are per world, default-initialized once, typed by T") {
	auto a = mk(2), b = mk(2);
	eq(a->tune<FakeTune>().rate, 0.03, "default");
	a->tune<FakeTune>().rate = 0.05;
	eq(a->tune<FakeTune>().rate, 0.05, "persists");
	eq(b->tune<FakeTune>().rate, 0.03, "per world");
	eq(a->tune<OtherTune>().rate, 9.0, "distinct types do not collide");
	a->scratch<FakeScratch>().buf.assign(size_t(a->n), 1);
	eq(a->scratch<FakeScratch>().buf.size(), size_t(a->n));
	ok(&a->scratch<FakeScratch>() == &a->scratch<FakeScratch>(), "same instance");
	a->clearScratch();
	ok(a->scratch<FakeScratch>().buf.empty(), "clearScratch drops scratch");
	eq(a->tune<FakeTune>().rate, 0.05, "and keeps tuning");
	const World& ca = *a;
	eq(ca.tune<FakeTune>().cap, 160, "usable through const World&");
}

// ---------------------------------------------------------------- helper world and maps

DM_TEST("helper: makeWorld / fillBasics give a plausible, finite, deterministic planet") {
	for (int L : { 4, 6 }) {
		auto w = makeWorld({ .L = L, .seed = 3 });
		int land = 0, hab = 0;
		std::set<int> biomes;
		for (int c = 0; c < w->n; c++) {
			if (!state::isSea(*w, c)) land++;
			if (w->hab[size_t(c)] > 0.2) hab++;
			biomes.insert(w->biome[size_t(c)]);
		}
		near(double(land) / w->n, 0.4, 0.01, "land fraction at L=" + std::to_string(L));
		ok(hab > w->n / 100, "habitable land exists: " + std::to_string(hab) + " cells with hab > 0.2");
		ok(biomes.size() >= 9, "varied biomes: " + std::to_string(biomes.size()));
		noNaN(*w, "fresh helper world");
		eq(checkFinite(*w), std::string());
		ok(!w->need.derive && !w->need.climate && !w->need.hydro, "derived stand-ins mark derive / climate / hydro done");
		const int bh = bestHab(*w);
		ok(bh >= 0 && !state::isSea(*w, bh) && w->hab[size_t(bh)] > 0.5, "bestHab is good land");
		const int fl = findLand(*w, [&](int c) { return w->hab[size_t(c)] > 0.5; });
		ok(fl >= 0 && w->hab[size_t(fl)] > 0.5, "findLand with a predicate");
		eq(findLand(*w, [](int) { return false; }), -1);
	}
	auto w = makeWorld({ .L = 3, .basics = false });
	ok(w->temp[5] == 0 && w->hab[5] == 0, "basics = false leaves derived fields at defaults");
	w->veg[9] = std::nanf("");
	eq(checkFinite(*w), std::string("veg[9]"), "checkFinite names the first bad cell");
	auto x = makeWorld({ .L = 4, .seed = 8, .land = 0.25, .sea = 0.5 });
	int land = 0;
	for (int c = 0; c < x->n; c++) if (!state::isSea(*x, c)) land++;
	ok(double(land) / x->n < 0.3 && x->seaLevel == 0.5 && x->vars.seaBase == 0.5, "land and sea options");
}

DM_TEST("maps: elevation, biome, field and owner maps of a helper world (L = 6)") {
	auto w = makeWorld({ .L = 6, .seed = 3 });
	const std::string fe = shot("ue-foundation-core-elev.png"), fb = shot("ue-foundation-core-biome.png");
	const std::string ft = shot("ue-foundation-core-temp.png"), fo = shot("ue-foundation-core-owner.png");
	const double t = nowMs();
	ok(mapimg::elev(*w, fe), "elev map written");
	std::printf("       map 1024x512 at L=6: %.0f ms\n", nowMs() - t);
	ok(mapimg::biome(*w, fb), "biome map written");
	ok(mapimg::field(*w, "temp", ft, -30, 30, { .dimSea = true }), "temp map written");
	// a few fake settlements so the owner map has something to show
	state::newCulture(*w); state::newCulture(*w);
	for (int k = 0; k < 6; k++) {
		const int c = findLand(*w, [&](int cc) { return w->hab[size_t(cc)] > 0.4 && cc % 97 == k * 13; });
		if (c < 0) continue;
		Settlement& s = state::newSettlement(*w, c, k % 2);
		std::vector<int32_t> cells;
		w->geo->ring(c, 4, cells);
		for (int32_t m : cells) if (!state::isSea(*w, m)) w->owner[size_t(m)] = int16_t(s.id);
	}
	ok(mapimg::owner(*w, fo), "owner map written");
	ok(!mapimg::field(*w, "nope", ft), "unknown field -> false");
	const std::vector<uint8_t> png = mapimg::encodePng(2, 1, { 255, 0, 0, 0, 0, 255 });
	ok(png.size() > 50 && png[1] == 'P' && png[2] == 'N' && png[3] == 'G', "PNG signature");
	std::vector<float> vals(size_t(w->n));
	for (int c = 0; c < w->n; c++) vals[size_t(c)] = w->geo->lat[size_t(c)];
	ok(mapimg::field(*w, vals, shot("ue-foundation-core-lat.png")), "field from a vector, auto range");
}
