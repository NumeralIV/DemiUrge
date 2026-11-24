// DEMIURGE — Sim/State.cpp : see State.h (port of js/sim/state.js; the save format is binary).
//
// Save layout, version 2 (v4 mythos, M0): exactly the version-1 layout below (with u32 version = 2 and the
// version-2 cell field wild in the field list), then, before 'E' 'N' 'D' '!', a list of extension blocks
//   { char tag[4], u16 block version, u32 length, length bytes } ... and the 4-byte terminator 'E' 'N' 'D' '_'
// "BASE" v1: u32 cultures, per culture f64 compassion, ambition, courage, i32 prophet.figure (-1 without a
//            prophet); u8 settings.archetype; u8 ending.legacy (0 without an ending)
// "MYTH" v1: its own block list (same framing, own "END_"): "MCOR" (presence, presenceSince, archetype, faith,
//            factors, towns, acts, withdrawal, renewal, num, nextId), "CARD" (cards), "DOCT" (decrees, words,
//            pacts, revelations), "CHAR" (figures), "LEGN" (legends), "FAIT" (houses, rivals), "OMEN" (omens),
//            "KEEP" (keepers); see putMythos / readMythos.
// A loader skips a block whose tag it does not know or whose version is newer than it reads (the state stays
// at its defaults), refuses a block that appears twice or is not consumed exactly, and validates every id in
// MYTH (mythosRefsOk). A version-1 blob (no blocks) loads with wild 0 and an empty MythosState.
// serialize(w, out, why, 1) writes the version-1 layout byte for byte (no wild, no blocks).
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

#include <algorithm>
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

// ---- (v4 mythos, M0) extension blocks of version 2: { char tag[4], u16 version, u32 length, bytes }
void putBlock(ByteWriter& o, const char* tag, uint16_t ver, const ByteWriter& body) {
	o.bytes(reinterpret_cast<const uint8_t*>(tag), 4);
	o.u16(ver);
	o.u32(uint32_t(body.buf.size()));
	o.bytes(body.buf.data(), body.buf.size());
}
void putI8(ByteWriter& o, int v) { o.u8(uint8_t(int8_t(v))); }

void putFaith(ByteWriter& o, const Faith& f) {
	o.u8(uint8_t(f.aspect)); o.f64(fin(f.aspectSince));
	for (double v : f.image) o.f64(fin(v));
	o.f64(fin(f.agency)); o.f64(fin(f.obedience)); o.f64(fin(f.dependence)); o.f64(fin(f.selfReliance));
	o.f64(fin(f.clarity)); o.f64(fin(f.visible)); o.f64(fin(f.judgedBy)); o.f64(fin(f.mercy)); o.f64(fin(f.sternness));
	o.f64(fin(f.survivorLove)); o.f64(fin(f.survivorFear)); o.f64(fin(f.survivorUntil));
	putI8(o, f.death); putI8(o, f.deathSure);
	o.f64(fin(f.bearer)); o.f64(fin(f.superiority)); putI8(o, f.charge); o.f64(fin(f.grown));
	putBag(o, f.data);
}
void putFactors(ByteWriter& o, const Factors& f) {
	const double d[] = { f.growthR, f.K, f.famineLoss, f.plagueDeath, f.research, f.expandCooldown, f.farmConvert,
		f.devotionBase, f.loveTarget, f.prayerChance, f.answeredGain, f.unansweredPenalty, f.aweS, f.schism,
		f.warV, f.aggression, f.battle, f.defence, f.relDrift, f.plagueHop, f.warStrength, f.tradeEvery,
		f.naturalRate, f.harmTaken, f.co2Uptake };
	for (double v : d) o.f64(fin(v));
	o.boolean(f.farmAncient); o.boolean(f.claimAncient);
	for (double v : f.expect) o.f64(fin(v));
}
void putTown(ByteWriter& o, const TownMyth& t) {
	o.i32(t.settlement); o.u8(uint8_t(t.blessing)); o.f64(fin(t.blessedSince)); o.f64(fin(t.blessedUntil));
	o.i32(t.healer); o.f64(fin(t.remnantUntil)); putBag(o, t.data);
}
void putAct(ByteWriter& o, const MythAct& a) {
	o.f64(fin(a.year)); o.str(a.kind); o.i32(a.cell); o.i32(a.settlement); o.i32(a.culture); o.f64(fin(a.good)); o.boolean(a.natural);
}
void putDecree(ByteWriter& o, const Decree& d) {
	o.i32(d.culture); o.u8(uint8_t(d.area)); putI8(o, d.stance); o.u8(uint8_t(d.clarity));
	o.f64(fin(d.since)); o.f64(fin(d.kept)); o.i32(d.by); o.boolean(d.custom); putBag(o, d.data);
}
void putWord(ByteWriter& o, const Utterance& u) {
	o.i32(u.id); o.i32(u.culture); o.i32(u.figure); o.u8(u.content); o.u8(uint8_t(u.clarity)); o.u8(uint8_t(u.channel));
	o.f64(fin(u.year)); o.f64(fin(u.fidelity)); putI8(o, u.meant); putI8(o, u.heard); o.u8(u.outcome); putI8(o, u.area);
	putBag(o, u.data);
}
void putPact(ByteWriter& o, const Pact& p) {
	o.i32(p.id); o.i32(p.culture); o.u8(p.godTerm); o.u8(p.peopleTerm); o.f64(fin(p.sworn)); o.f64(fin(p.until));
	o.f64(fin(p.godKept)); o.f64(fin(p.peopleKept)); o.u8(p.state); o.i32(p.figure); putBag(o, p.data);
}
void putRevelation(ByteWriter& o, const Revelation& r) {
	o.i32(r.culture); o.u8(r.kind); o.u8(r.outcome); o.f64(fin(r.year)); o.boolean(r.early); putBag(o, r.data);
}
void putFigure(ByteWriter& o, const Figure& f) {
	o.i32(f.id); o.u8(f.kind); o.u8(f.archetype); o.i32(f.culture); o.i32(f.home); o.str(f.name); o.str(f.byname);
	o.f64(fin(f.born)); o.f64(fin(f.died)); o.f64(fin(f.credibility)); o.f64(fin(f.reach)); o.f64(fin(f.fame));
	for (float v : f.person) o.f32(std::isfinite(v) ? v : 0.0f);
	for (float v : f.heart) o.f32(std::isfinite(v) ? v : 0.0f);
	o.u8(f.fate); o.u16(f.flags); o.i32(f.trial); putBag(o, f.data);
}
void putCard(ByteWriter& o, const Card& c) {
	o.i32(c.id); o.u8(uint8_t(c.kind)); o.u8(c.state); o.i32(c.culture); o.i32(c.other); o.i32(c.settlement); o.i32(c.figure);
	o.i32(c.cell); o.f64(fin(c.opened)); o.f64(fin(c.deadline)); o.f64(fin(c.severity)); putI8(o, c.choice); putI8(o, c.lapse);
	for (int v : c.ref) o.i32(v);
	putBag(o, c.data);
}
void putLegend(ByteWriter& o, const Legend& l) {
	o.i32(l.id); o.i32(l.culture); o.i32(l.settlement); o.i32(l.cell); o.str(l.deed);
	o.f64(fin(l.year)); o.f64(fin(l.truth)); o.f64(fin(l.told)); o.f64(fin(l.vitality));
	o.u8(l.moral); o.u8(l.credit); o.u8(l.versions); o.i32(l.source); putBag(o, l.data);
}
void putHouse(ByteWriter& o, const House& h) {
	o.i32(h.culture); o.f64(fin(h.strength)); o.f64(fin(h.wealth)); o.f64(fin(h.authority)); o.f64(fin(h.corruption));
	o.f64(fin(h.since)); putBag(o, h.data);
}
void putRival(ByteWriter& o, const RivalVoice& r) {
	o.i32(r.id); o.i32(r.culture); o.i32(r.figure); o.f64(fin(r.following)); o.f64(fin(r.since)); o.i32(r.way); o.u8(r.state);
	putBag(o, r.data);
}
void putOmen(ByteWriter& o, const Omen& m) {
	o.i32(m.id); o.u8(m.kind); o.i32(m.cell); o.i32(m.culture); o.f64(fin(m.due)); o.f64(fin(m.power)); o.f64(fin(m.ang));
	o.u8(m.warned); o.u8(m.response); o.i32(m.figure); putBag(o, m.data);
}
void putKeeper(ByteWriter& o, const Keeper& k) {
	o.i32(k.id); o.u8(k.domain); o.u8(k.disposition); o.i32(k.culture); o.str(k.name); o.f64(fin(k.appointed));
	o.f64(fin(k.loyalty)); o.f64(fin(k.will)); o.f64(fin(k.rogueUntil)); putBag(o, k.data);
}
template <class T, class F> void putList(ByteWriter& o, const std::vector<T>& v, F put) {
	o.u32(uint32_t(v.size()));
	for (const T& x : v) put(o, x);
}

// The "MYTH" block: one sub-block per module (so modules can version their part alone), then "END_".
void putMythos(ByteWriter& o, const MythosState& m) {
	ByteWriter b;
	b.u8(uint8_t(m.presence)); b.f64(fin(m.presenceSince)); b.u8(uint8_t(m.archetype));
	putList(b, m.faith, putFaith); putList(b, m.factors, putFactors); putList(b, m.towns, putTown); putList(b, m.acts, putAct);
	b.f64(fin(m.withdrawnUntil)); b.boolean(m.withdrawAnnounced); b.i32(m.renewal); b.f64(fin(m.renewedYear));
	putBag(b, m.num); b.i32(m.nextId);
	putBlock(o, "MCOR", 1, b);
	b.buf.clear(); putList(b, m.cards, putCard); putBlock(o, "CARD", 1, b);
	b.buf.clear(); putList(b, m.decrees, putDecree); putList(b, m.words, putWord); putList(b, m.pacts, putPact);
	putList(b, m.revelations, putRevelation); putBlock(o, "DOCT", 1, b);
	b.buf.clear(); putList(b, m.figures, putFigure); putBlock(o, "CHAR", 1, b);
	b.buf.clear(); putList(b, m.legends, putLegend); putBlock(o, "LEGN", 1, b);
	b.buf.clear(); putList(b, m.houses, putHouse); putList(b, m.rivals, putRival); putBlock(o, "FAIT", 1, b);
	b.buf.clear(); putList(b, m.omens, putOmen); putBlock(o, "OMEN", 1, b);
	b.buf.clear(); putList(b, m.keepers, putKeeper); putBlock(o, "KEEP", 1, b);
	o.bytes(reinterpret_cast<const uint8_t*>("END_"), 4);
}

// The "BASE" block: the fields v4 added to the version-1 entities (Traits, Prophet, Settings, Ending).
void putBase(ByteWriter& o, const World& w) {
	o.u32(uint32_t(w.cultures.size()));
	for (const Culture& c : w.cultures) {
		o.f64(fin(c.traits.compassion)); o.f64(fin(c.traits.ambition)); o.f64(fin(c.traits.courage));
		o.i32(c.prophet ? c.prophet->figure : -1);
	}
	o.u8(uint8_t(w.settings.archetype));
	o.u8(uint8_t(w.ending ? w.ending->legacy : Legacy::None));
}

const char* const kV2Fields[] = { "wild" };
bool isV2Field(const char* nm) {
	for (const char* f : kV2Fields) if (std::strcmp(f, nm) == 0) return true;
	return false;
}

bool mythosDefault(const MythosState& m) {
	const MythosState d;
	return m.presence == d.presence && m.presenceSince == d.presenceSince && m.archetype == d.archetype &&
		m.faith.empty() && m.factors.empty() && m.decrees.empty() && m.words.empty() && m.pacts.empty() &&
		m.figures.empty() && m.cards.empty() && m.legends.empty() && m.houses.empty() && m.rivals.empty() &&
		m.omens.empty() && m.keepers.empty() && m.revelations.empty() && m.towns.empty() && m.acts.empty() &&
		m.withdrawnUntil == d.withdrawnUntil && m.withdrawAnnounced == d.withdrawAnnounced &&
		m.renewal == d.renewal && m.renewedYear == d.renewedYear && m.num.empty() && m.nextId == d.nextId;
}

} // namespace

uint32_t fieldSince(int fieldIdx) {
	if (fieldIdx < 0 || fieldIdx >= kCellFieldCount) return 0;
	return isV2Field(kCellFields[fieldIdx].name) ? 2u : 1u;
}

bool extensionsDefault(const World& w) {
	for (uint8_t v : w.wild) if (v != 0) return false;
	for (const Culture& c : w.cultures) {
		if (c.traits.compassion != 0.5 || c.traits.ambition != 0.5 || c.traits.courage != 0.5) return false;
		if (c.prophet && c.prophet->figure != -1) return false;
	}
	if (w.settings.archetype != Archetype::Balanced) return false;
	if (w.ending && w.ending->legacy != Legacy::None) return false;
	return mythosDefault(w.mythos);
}

bool serialize(const World& w, std::vector<uint8_t>& out, std::string* why, uint32_t version) {
	if (version != 1 && version != kVersion) return refuse(out, why, "unsupported save version " + std::to_string(version));
	if (version >= 2 && w.mythos.presence == Presence::Walk) return refuse(out, why, "world.mythos.presence is Walk (reserved)");
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
	o.u32(version);
	o.u32(w.seed);
	o.str(w.name);
	o.u8(uint8_t(w.L));
	o.u32(uint32_t(w.n));
	o.u32(w.rng.s);
	o.f64(w.year); o.i64(w.tick); o.u8(uint8_t(w.phase)); o.f64(w.heat); o.f64(w.seaLevel);
	o.f64(w.globalTemp); o.f64(w.co2); o.f64(w.dust); o.i32(w.age);

	int persisted = 0;
	for (int i = 0; i < kCellFieldCount; i++) if (kCellFields[i].save != SaveCodec::None && fieldSince(i) <= version) persisted++;
	o.u8(uint8_t(persisted));
	for (int i = 0; i < kCellFieldCount; i++) {
		const CellField& f = kCellFields[i];
		if (f.save == SaveCodec::None || fieldSince(i) > version) continue;
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
	if (version >= 2) {
		ByteWriter b;
		putBase(b, w);
		putBlock(o, "BASE", 1, b);
		b.buf.clear();
		putMythos(b, w.mythos);
		putBlock(o, "MYTH", 1, b);
		o.bytes(reinterpret_cast<const uint8_t*>("END_"), 4);
	}
	o.bytes(reinterpret_cast<const uint8_t*>("END!"), 4);
	out = std::move(o.buf);
	return true;
}

// sizeOf measures the current version (kVersion).
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

// ---- (v4 mythos, M0) the extension blocks of version 2

int8_t i8(Loader& L) { return int8_t(L.r.u8()); }
template <class E> uint8_t enumByte(Loader& L, const char* what) { return uint8_t(L.enm<E>(what)); }

void readFaith(Loader& L, Faith& f) {
	ByteReader& r = L.r;
	f.aspect = L.enm<Aspect>("faith.aspect"); f.aspectSince = L.num();
	for (double& v : f.image) v = L.num();
	f.agency = L.num(); f.obedience = L.num(); f.dependence = L.num(); f.selfReliance = L.num();
	f.clarity = L.num(); f.visible = L.num(); f.judgedBy = L.num(); f.mercy = L.num(); f.sternness = L.num();
	f.survivorLove = L.num(); f.survivorFear = L.num(); f.survivorUntil = L.num();
	f.death = i8(L); f.deathSure = i8(L);
	if (f.death < 0 || f.death >= enumCount<DeathBelief>() || f.deathSure < 0 || f.deathSure >= enumCount<Certainty>()) L.bad("faith.death");
	f.bearer = L.num(); f.superiority = L.num(); f.charge = i8(L); f.grown = L.num();
	if (f.charge < 0 || f.charge >= enumCount<Charge>()) L.bad("faith.charge");
	f.data = L.bag();
	(void)r;
}
void readFactors(Loader& L, Factors& f) {
	double* d[] = { &f.growthR, &f.K, &f.famineLoss, &f.plagueDeath, &f.research, &f.expandCooldown, &f.farmConvert,
		&f.devotionBase, &f.loveTarget, &f.prayerChance, &f.answeredGain, &f.unansweredPenalty, &f.aweS, &f.schism,
		&f.warV, &f.aggression, &f.battle, &f.defence, &f.relDrift, &f.plagueHop, &f.warStrength, &f.tradeEvery,
		&f.naturalRate, &f.harmTaken, &f.co2Uptake };
	for (double* p : d) *p = L.num();
	f.farmAncient = L.r.boolean(); f.claimAncient = L.r.boolean();
	for (double& v : f.expect) v = L.num();
}
void readTown(Loader& L, TownMyth& t) {
	t.settlement = L.r.i32(); t.blessing = L.enm<BlessingKind>("town.blessing"); t.blessedSince = L.num(); t.blessedUntil = L.num();
	t.healer = L.r.i32(); t.remnantUntil = L.num(); t.data = L.bag();
}
void readAct(Loader& L, MythAct& a) {
	a.year = L.num(); a.kind = L.r.str(256); a.cell = L.cellRef("act.cell"); a.settlement = L.r.i32(); a.culture = L.r.i32();
	a.good = L.num(); a.natural = L.r.boolean();
}
void readDecree(Loader& L, Decree& d) {
	d.culture = L.r.i32(); d.area = L.enm<DecreeArea>("decree.area"); d.stance = i8(L); d.clarity = L.enm<Clarity>("decree.clarity");
	if (d.stance < -1 || d.stance >= stanceCount(d.area)) L.bad("decree.stance");
	d.since = L.num(); d.kept = L.num(); d.by = L.r.i32(); d.custom = L.r.boolean(); d.data = L.bag();
}
void readWord(Loader& L, Utterance& u) {
	u.id = L.r.i32(); u.culture = L.r.i32(); u.figure = L.r.i32(); u.content = enumByte<WordContent>(L, "word.content");
	u.clarity = L.enm<Clarity>("word.clarity"); u.channel = L.enm<Channel>("word.channel");
	u.year = L.num(); u.fidelity = L.num(); u.meant = i8(L); u.heard = i8(L); u.outcome = enumByte<WordOutcome>(L, "word.outcome");
	u.area = i8(L);
	if (u.area < -1 || u.area >= enumCount<DecreeArea>()) L.bad("word.area");
	if (u.meant < -1 || u.meant > 4 || u.heard < -1 || u.heard > 4) L.bad("word.stance");
	u.data = L.bag();
}
void readPact(Loader& L, Pact& p) {
	p.id = L.r.i32(); p.culture = L.r.i32(); p.godTerm = enumByte<GodTerm>(L, "pact.godTerm"); p.peopleTerm = enumByte<PeopleTerm>(L, "pact.peopleTerm");
	p.sworn = L.num(); p.until = L.num(); p.godKept = L.num(); p.peopleKept = L.num(); p.state = enumByte<PactState>(L, "pact.state");
	p.figure = L.r.i32(); p.data = L.bag();
}
void readRevelation(Loader& L, Revelation& v) {
	v.culture = L.r.i32(); v.kind = enumByte<RevelationKind>(L, "revelation.kind"); v.outcome = enumByte<RevelationOutcome>(L, "revelation.outcome");
	v.year = L.num(); v.early = L.r.boolean(); v.data = L.bag();
}
void readFigure(Loader& L, Figure& f) {
	ByteReader& r = L.r;
	f.id = r.i32(); f.kind = enumByte<FigureKind>(L, "figure.kind"); f.archetype = enumByte<MessengerArchetype>(L, "figure.archetype");
	f.culture = r.i32(); f.home = r.i32(); f.name = r.str(4096); f.byname = r.str(4096);
	f.born = L.num(); f.died = L.num(); f.credibility = L.num(); f.reach = L.num(); f.fame = L.num();
	for (float& v : f.person) { v = r.f32(); if (!std::isfinite(v)) L.bad("figure.person"); }
	for (float& v : f.heart) { v = r.f32(); if (!std::isfinite(v)) L.bad("figure.heart"); }
	f.fate = enumByte<FigureFate>(L, "figure.fate"); f.flags = r.u16(); f.trial = r.i32();
	if (f.trial < -1 || f.trial >= enumCount<TrialKind>()) L.bad("figure.trial");
	f.data = L.bag();
}
void readCard(Loader& L, Card& c) {
	ByteReader& r = L.r;
	c.id = r.i32(); c.kind = L.enm<CardKind>("card.kind"); c.state = enumByte<CardState>(L, "card.state");
	c.culture = r.i32(); c.other = r.i32(); c.settlement = r.i32(); c.figure = r.i32(); c.cell = L.cellRef("card.cell");
	c.opened = L.num(); c.deadline = L.num(); c.severity = L.num(); c.choice = i8(L); c.lapse = i8(L);
	for (int& v : c.ref) v = r.i32();
	c.data = L.bag();
}
void readLegend(Loader& L, Legend& l) {
	ByteReader& r = L.r;
	l.id = r.i32(); l.culture = r.i32(); l.settlement = r.i32(); l.cell = L.cellRef("legend.cell"); l.deed = r.str(4096);
	l.year = L.num(); l.truth = L.num(); l.told = L.num(); l.vitality = L.num();
	l.moral = enumByte<LegendMoral>(L, "legend.moral"); l.credit = enumByte<LegendCredit>(L, "legend.credit"); l.versions = r.u8();
	l.source = r.i32(); l.data = L.bag();
}
void readHouse(Loader& L, House& h) {
	h.culture = L.r.i32(); h.strength = L.num(); h.wealth = L.num(); h.authority = L.num(); h.corruption = L.num();
	h.since = L.num(); h.data = L.bag();
}
void readRival(Loader& L, RivalVoice& v) {
	v.id = L.r.i32(); v.culture = L.r.i32(); v.figure = L.r.i32(); v.following = L.num(); v.since = L.num(); v.way = L.r.i32();
	v.state = enumByte<RivalState>(L, "rival.state"); v.data = L.bag();
}
void readOmen(Loader& L, Omen& m) {
	m.id = L.r.i32(); m.kind = enumByte<OmenKind>(L, "omen.kind"); m.cell = L.cellRef("omen.cell"); m.culture = L.r.i32();
	m.due = L.num(); m.power = L.num(); m.ang = L.num(); m.warned = enumByte<WarningWay>(L, "omen.warned");
	m.response = enumByte<OmenResponse>(L, "omen.response"); m.figure = L.r.i32(); m.data = L.bag();
}
void readKeeper(Loader& L, Keeper& k) {
	k.id = L.r.i32(); k.domain = enumByte<KeeperDomain>(L, "keeper.domain"); k.disposition = enumByte<Temper>(L, "keeper.temper");
	k.culture = L.r.i32(); k.name = L.r.str(4096); k.appointed = L.num(); k.loyalty = L.num(); k.will = L.num();
	k.rogueUntil = L.num(); k.data = L.bag();
}
template <class T, class F> void readList(Loader& L, std::vector<T>& v, size_t minItemBytes, F read) {
	const uint32_t k = L.count(minItemBytes);
	v.clear();
	if (!L.good()) return;
	v.resize(k);
	for (uint32_t i = 0; i < k && L.good(); i++) read(L, v[i]);
	if (!L.good()) v.clear();
}

// One { tag, version, length } block; calls body(sub, version) on a Loader over its bytes, which must be
// consumed exactly. Returns false at the "END_" terminator or on an error (L.bad set).
template <class F> bool readBlocks(Loader& L, const char* where, F body) {
	std::vector<std::string> seen;
	for (;;) {
		const uint8_t* tag = L.r.take(4);
		if (!tag) { L.bad(std::string(where) + ": truncated block list"); return false; }
		const std::string t(reinterpret_cast<const char*>(tag), 4);
		if (t == "END_") return true;
		const uint16_t ver = L.r.u16();
		const uint32_t len = L.r.u32();
		const uint8_t* p = len > 0 ? L.r.take(len) : nullptr;
		if (!L.good() || (len > 0 && !p)) { L.bad(std::string(where) + "." + t + ": truncated"); return false; }
		if (std::find(seen.begin(), seen.end(), t) != seen.end()) { L.bad(std::string(where) + "." + t + ": twice"); return false; }
		seen.push_back(t);
		static const uint8_t kEmpty[1] = { 0 };
		Loader sub(p ? p : kEmpty, len);
		sub.n = L.n;
		const int handled = body(t, ver, sub);         // 1 parsed, 0 unknown tag or newer version (skipped)
		if (handled == 1 && (!sub.good() || !sub.r.atEnd())) {
			L.bad(std::string(where) + "." + t + ": " + (sub.err.empty() ? std::string("length") : sub.err));
			return false;
		}
	}
}

void readMythos(Loader& L, MythosState& m) {
	readBlocks(L, "MYTH", [&](const std::string& t, uint16_t ver, Loader& S) -> int {
		if (ver != 1) return 0;
		if (t == "MCOR") {
			m.presence = S.enm<Presence>("presence");
			if (m.presence == Presence::Walk) S.bad("presence Walk (reserved)");
			m.presenceSince = S.num(); m.archetype = S.enm<Archetype>("archetype");
			readList(S, m.faith, 100, readFaith); readList(S, m.factors, 200, readFactors);
			readList(S, m.towns, 30, readTown); readList(S, m.acts, 30, readAct);
			m.withdrawnUntil = S.num(); m.withdrawAnnounced = S.r.boolean(); m.renewal = S.r.i32(); m.renewedYear = S.num();
			if (m.renewal < 0 || m.renewal >= enumCount<RenewalKind>()) S.bad("renewal");
			m.num = S.bag(); m.nextId = S.r.i32();
			return 1;
		}
		if (t == "CARD") { readList(S, m.cards, 60, readCard); return 1; }
		if (t == "DOCT") {
			readList(S, m.decrees, 20, readDecree); readList(S, m.words, 30, readWord);
			readList(S, m.pacts, 40, readPact); readList(S, m.revelations, 15, readRevelation);
			return 1;
		}
		if (t == "CHAR") { readList(S, m.figures, 60, readFigure); return 1; }
		if (t == "LEGN") { readList(S, m.legends, 50, readLegend); return 1; }
		if (t == "FAIT") { readList(S, m.houses, 40, readHouse); readList(S, m.rivals, 30, readRival); return 1; }
		if (t == "OMEN") { readList(S, m.omens, 40, readOmen); return 1; }
		if (t == "KEEP") { readList(S, m.keepers, 40, readKeeper); return 1; }
		return 0;
	});
}

void readBase(Loader& L, World& w) {
	const uint32_t k = L.count(28);
	if (L.good() && k != w.cultures.size()) { L.bad("culture count"); return; }
	for (uint32_t i = 0; i < k && L.good(); i++) {
		Culture& c = w.cultures[i];
		c.traits.compassion = L.num(); c.traits.ambition = L.num(); c.traits.courage = L.num();
		const int fig = L.r.i32();
		if (c.prophet) c.prophet->figure = fig;
		else if (fig != -1) L.bad("prophet.figure without a prophet");
	}
	w.settings.archetype = L.enm<Archetype>("settings.archetype");
	const Legacy lg = L.enm<Legacy>("ending.legacy");
	if (w.ending) w.ending->legacy = lg;
	else if (lg != Legacy::None) L.bad("ending.legacy without an ending");
}

// Every id inside the MYTH block names an existing entity: cultures and settlements by index, figures and
// cards by their ids (-1 always allowed); record ids are unique and below nextId.
bool mythosRefsOk(Loader& L, const World& w) {
	const MythosState& m = w.mythos;
	const int ns = int(w.settlements.size()), nc = int(w.cultures.size());
	auto in = [](int v, int count) { return v >= -1 && v < count; };
	auto culture = [&](int v) { return v >= 0 && v < nc; };
	std::vector<int> figs;
	for (const Figure& f : m.figures) figs.push_back(f.id);
	std::sort(figs.begin(), figs.end());
	auto fig = [&](int v) { return v == -1 || std::binary_search(figs.begin(), figs.end(), v); };
	auto ids = [&](std::vector<int> v, const char* what) {
		std::sort(v.begin(), v.end());
		for (size_t i = 0; i < v.size(); i++) {
			if (v[i] < 1 || v[i] >= m.nextId || (i > 0 && v[i] == v[i - 1])) return L.bad(std::string("mythos.") + what + " ids");
		}
		return true;
	};
	if (m.nextId < 1) return L.bad("mythos.nextId");
	if (m.faith.size() > size_t(nc) || m.factors.size() > size_t(nc)) return L.bad("mythos.faith / factors beyond the cultures");
	if (!ids(figs, "figures")) return false;
	{ std::vector<int> v; for (const Card& c : m.cards) v.push_back(c.id); if (!ids(v, "cards")) return false; }
	{ std::vector<int> v; for (const Utterance& u : m.words) v.push_back(u.id); if (!ids(v, "words")) return false; }
	{ std::vector<int> v; for (const Pact& p : m.pacts) v.push_back(p.id); if (!ids(v, "pacts")) return false; }
	{ std::vector<int> v; for (const Legend& l : m.legends) v.push_back(l.id); if (!ids(v, "legends")) return false; }
	{ std::vector<int> v; for (const RivalVoice& r : m.rivals) v.push_back(r.id); if (!ids(v, "rivals")) return false; }
	{ std::vector<int> v; for (const Omen& o : m.omens) v.push_back(o.id); if (!ids(v, "omens")) return false; }
	{ std::vector<int> v; for (const Keeper& k : m.keepers) v.push_back(k.id); if (!ids(v, "keepers")) return false; }
	int prevTown = -1;
	for (const TownMyth& t : m.towns) {
		if (t.settlement < 0 || t.settlement >= ns || t.settlement <= prevTown) return L.bad("mythos.towns settlement");
		prevTown = t.settlement;
		if (!fig(t.healer)) return L.bad("mythos.towns healer");
	}
	for (const MythAct& a : m.acts) if (!in(a.settlement, ns) || !in(a.culture, nc)) return L.bad("mythos.acts ids");
	for (const Decree& d : m.decrees) if (!culture(d.culture) || !fig(d.by)) return L.bad("mythos.decrees ids");
	for (const Utterance& u : m.words) if (!culture(u.culture) || !fig(u.figure)) return L.bad("mythos.words ids");
	for (const Pact& p : m.pacts) if (!culture(p.culture) || !fig(p.figure)) return L.bad("mythos.pacts ids");
	for (const Revelation& r : m.revelations) if (!culture(r.culture)) return L.bad("mythos.revelations culture");
	for (const Figure& f : m.figures) if (!in(f.culture, nc) || !in(f.home, ns)) return L.bad("mythos.figures ids");
	for (const Card& c : m.cards)
		if (!in(c.culture, nc) || !in(c.other, nc) || !in(c.settlement, ns) || !fig(c.figure)) return L.bad("mythos.cards ids");
	// (a legend of culture -1 is an Old Story of a reseeded world that no culture has inherited yet, 3.30)
	for (const Legend& l : m.legends) if (!in(l.culture, nc) || !in(l.settlement, ns) || l.source < -1) return L.bad("mythos.legends ids");
	for (const House& h : m.houses) if (!culture(h.culture)) return L.bad("mythos.houses culture");
	for (const RivalVoice& r : m.rivals) if (!culture(r.culture) || !fig(r.figure) || !in(r.way, nc)) return L.bad("mythos.rivals ids");
	for (const Omen& o : m.omens) if (!in(o.culture, nc) || !fig(o.figure)) return L.bad("mythos.omens ids");
	for (const Keeper& k : m.keepers) if (!in(k.culture, nc)) return L.bad("mythos.keepers culture");
	for (const Culture& c : w.cultures) if (c.prophet && !fig(c.prophet->figure)) return L.bad("culture.prophet.figure");
	return true;
}

std::unique_ptr<World> load(Loader& L) {
	ByteReader& r = L.r;
	const uint8_t* magic = r.take(4);
	if (!magic || std::memcmp(magic, kFormat, 4) != 0) { L.bad("format"); return nullptr; }
	const uint32_t fileVersion = r.u32();
	if (fileVersion != 1 && fileVersion != kVersion) { L.bad("version"); return nullptr; }
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
	if (fileVersion >= 2 && L.good()) {
		// (v4 mythos, M0) the extension blocks; unknown tags (and newer block versions) are skipped
		readBlocks(L, "blocks", [&](const std::string& t, uint16_t ver, Loader& S) -> int {
			if (ver != 1) return 0;
			if (t == "BASE") { readBase(S, *w); return 1; }
			if (t == "MYTH") { readMythos(S, w->mythos); return 1; }
			return 0;
		});
	}
	const uint8_t* tail = r.take(4);
	if (!tail || std::memcmp(tail, "END!", 4) != 0) L.bad("end marker");
	if (!r.atEnd()) L.bad("trailing bytes");
	if (!L.good()) return nullptr;
	if (!refsOk(L, *w)) return nullptr;
	if (!mythosRefsOk(L, *w)) return nullptr;
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
