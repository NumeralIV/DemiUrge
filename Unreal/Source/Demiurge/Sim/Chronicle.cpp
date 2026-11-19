// DEMIURGE — Sim/Chronicle.cpp : the history as prose, epithets, time-lapse snapshots, the tale
// (SPEC-SIM 3.11). Owner: chronicle engineer (A13, key "chronicle").
//
// How it works
//   attach()      subscribes one typed handler per chronicled event on w.bus (tokens kept in
//                 ChronicleScratch, so a second attach first removes the first set: never doubled).
//   handlers      turn an event into one entry (or none) through a small template engine:
//                   {key}      a value from the context (names of settlements, cultures, the god, places)
//                   [a|b|c]    one alternative, chosen by the entry's own rng (may nest)
//                 Each event kind has several templates, drawn from a shuffle bag so a kind never uses
//                 the same template twice in a row and uses all of them before repeating.
//                 Every random choice comes from w.rng.fork("chron:" + entryId): the sim stream is
//                 never advanced (SPEC-SIM 0.9 rule 6).
//                 A settlement named twice in one entry is called "the village" the second time.
//   rate limits   minor (weight 1) entries of one type are written at most once per culture per
//                 minorGap years (brushGap / fireGap / tierGap / buildingGap for the frequent kinds,
//                 plus a world-wide gap for fires, natural lightning and storms); notable and epochal
//                 entries are never dropped. Events far from every settlement mostly go unwritten: the
//                 chronicle is what the people saw.
//   epithets      each living culture remembers what it saw: increments of culture.seen (written by
//                 Society) decay with a half-life of memoryHalfLife years, plus settlements it lost to
//                 the sea. The dominant memory picks a category (rain, green, meteor, drowner, ...);
//                 the culture's own variant of that category is fixed by a hash, so an epithet only
//                 changes when the god's behaviour changes, and is then kept at least epithetHold
//                 years. Long silence gives 'the Silent One'; with no deed worth remembering,
//                 society::regard decides ('the Beloved', 'the Dreaded', else 'the Waker').
//                 Memory lives in w.vars.num under "chronicle_*" keys (persisted, deterministic).
//   snapshots     time-lapse frames per SPEC-SIM 3.11; past maxFrames every second history frame is
//                 dropped and the interval doubled.
//   tale          read-only retelling built from the world's entities, stats and epochal entries.
#include "Chronicle.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Civ.h"
#include "Names.h"
#include "Society.h"

namespace dm {
namespace {

using std::string;
using std::string_view;

// ====================================================================== scratch (transient)

struct ChronicleScratch {
	std::vector<EventBus::Token> tokens;           // attach(): one per handler
	std::map<string, std::vector<int>> bags;       // template shuffle bags per kind
	std::map<string, int> lastPick;                // last template index per kind
	int64_t answeredTick = -1;                     // prayer:answered seen on this tick and year ...
	double answeredYear = -1e9;
	std::vector<int> answered;                     // ... for these settlements: the miracle:witnessed that
	                                               // follows it (same witness call) is already told
	std::map<int, double> prayerSince;             // settlement id -> year its prayer began
	int64_t bornTick = -1;                         // culture:born seen on this tick and year ...
	double bornYear = -1e9;
	std::vector<int> bornCultures;                 // ... for these cultures (their schism captures are part of it)
	int genesisFrames = -1;                        // leading snapshot frames taken before history (-1 unknown)
};

// ====================================================================== small helpers

double fin(double x, double d = 0) { return std::isfinite(x) ? x : d; }

const Settlement* sett(const World& w, int id) { return state::settlement(w, id); }
const Culture* cult(const World& w, int id) { return state::culture(w, id); }

const char* const kTierWord[5] = { "camp", "village", "town", "city", "great city" };
const char* tierWord(int t) { return kTierWord[t < 0 ? 0 : t > 4 ? 4 : t]; }

string settlementName(const World& w, int sid) {
	const Settlement* s = sett(w, sid);
	if (!s) return "a nameless camp";
	if (!s->name.empty()) return s->name;
	return string("a nameless ") + tierWord(s->tier);
}
string culturePlural(const World& w, int cid) {
	const Culture* c = cult(w, cid);
	if (!c) return "the people";
	if (!c->plural.empty()) return c->plural;
	if (!c->name.empty()) return "the " + c->name;
	return "a nameless people";
}
string cultureAdj(const World& w, int cid) {
	const Culture* c = cult(w, cid);
	if (!c) return "nameless";
	if (!c->adj.empty()) return c->adj;
	if (!c->name.empty()) return c->name;
	return "nameless";
}
string cultureName(const World& w, int cid) {
	const Culture* c = cult(w, cid);
	if (!c || c->name.empty()) return "a nameless people";
	return c->name;
}
string godOf(const World& w, int cid) {
	const Culture* c = cult(w, cid);
	if (!c || c->godName.empty()) return "the god";
	return c->godName;
}
// The culture imagines its god as a woman or a man (fixed per culture and world).
bool godIsShe(const World& w, int cid) { return hashf(uint32_t(cid + 7), w.seed ^ 0x9e3779b9u) < 0.55; }
string worldName(const World& w) { return w.name.empty() ? string("the world") : w.name; }

bool startsWith(string_view s, string_view p) { return s.size() >= p.size() && s.substr(0, p.size()) == p; }

// ====================================================================== template engine

struct Ctx {
	struct Kv { string k, v; bool tpl; };
	std::vector<Kv> kv;
	void set(string_view k, string v, bool tpl = false) {
		for (Kv& e : kv) if (e.k == k) { e.v = std::move(v); e.tpl = tpl; return; }
		kv.push_back(Kv{ string(k), std::move(v), tpl });
	}
	const Kv* get(string_view k) const {
		for (const Kv& e : kv) if (e.k == k) return &e;
		return nullptr;
	}
};

void expandInto(string& out, string_view t, const Ctx& ctx, Rng& r, int depth) {
	size_t i = 0;
	while (i < t.size()) {
		const char ch = t[i];
		if (ch == '[') {
			int d = 0;
			size_t j = i;
			std::vector<size_t> bars;
			for (; j < t.size(); j++) {
				if (t[j] == '[') d++;
				else if (t[j] == ']') { d--; if (d == 0) break; }
				else if (t[j] == '|' && d == 1) bars.push_back(j);
			}
			if (j >= t.size()) { i++; continue; }           // unmatched: drop the bracket
			const int nopt = int(bars.size()) + 1;
			const int k = r.pickIndex(nopt);
			const size_t a = (k == 0) ? i + 1 : bars[size_t(k - 1)] + 1;
			const size_t b = (k == nopt - 1) ? j : bars[size_t(k)];
			if (depth < 8) expandInto(out, t.substr(a, b - a), ctx, r, depth + 1);
			i = j + 1;
		} else if (ch == '{') {
			const size_t j = t.find('}', i);
			if (j == string_view::npos) { i++; continue; }
			if (const Ctx::Kv* v = ctx.get(t.substr(i + 1, j - i - 1))) {
				if (v->tpl && depth < 8) expandInto(out, v->v, ctx, r, depth + 1);
				else out += v->v;
			}
			i = j + 1;
		} else if (ch == ']' || ch == '}' || ch == '|') {
			i++;                                            // stray syntax never reaches the page
		} else {
			out.push_back(ch);
			i++;
		}
	}
}

bool isUpperStart(char c) { return c >= 'a' && c <= 'z'; }

// Spacing, punctuation and capitals after expansion.
string tidy(const string& in) {
	string s;
	s.reserve(in.size());
	for (char c : in) {
		if (c == ' ' && (s.empty() || s.back() == ' ')) continue;
		if ((c == ',' || c == '.' || c == ';' || c == ':' || c == '!' || c == '?') && !s.empty() && s.back() == ' ') s.pop_back();
		if (c == '.' && !s.empty() && (s.back() == '.' || s.back() == ',')) { s.back() = '.'; continue; }
		if (c == ',' && !s.empty() && (s.back() == ',' || s.back() == '.')) continue;
		s.push_back(c);
	}
	while (!s.empty() && s.back() == ' ') s.pop_back();
	for (size_t p0 = 0; (p0 = s.find("the the ", p0)) != string::npos;) {
		if (p0 == 0 || s[p0 - 1] == ' ') s.erase(p0, 4); else p0++;
	}
	for (size_t p0 = 0; (p0 = s.find("The the ", p0)) != string::npos;) {
		if (p0 == 0 || s[p0 - 1] == ' ') s.erase(p0 + 4, 4); else p0++;
	}
	bool cap = true;
	for (size_t i = 0; i < s.size(); i++) {
		const char c = s[i];
		if (cap && c != ' ' && c != '"' && c != '\'') {
			if (isUpperStart(c)) s[i] = char(c - 'a' + 'A');
			cap = false;
		}
		if ((c == '.' || c == '!' || c == '?') && i + 1 < s.size() && s[i + 1] == ' ') cap = true;
	}
	if (!s.empty() && s.back() != '.' && s.back() != '!' && s.back() != '?' && s.back() != '"') s.push_back('.');
	return s;
}

struct Tpl { const char* const* t; int n; };
template <size_t N> Tpl tpl(const char* const (&a)[N]) { return Tpl{ a, int(N) }; }

int pickTemplate(ChronicleScratch& S, const string& key, int n, Rng& r) {
	if (n <= 1) return 0;
	std::vector<int>& bag = S.bags[key];
	if (bag.empty()) {
		for (int i = 0; i < n; i++) bag.push_back(i);
		r.shuffle(bag);
		const auto it = S.lastPick.find(key);
		if (it != S.lastPick.end() && bag.back() == it->second) std::swap(bag.front(), bag.back());
	}
	int idx = bag.back();
	bag.pop_back();
	if (idx < 0 || idx >= n) idx = 0;
	S.lastPick[key] = idx;
	return idx;
}

// ====================================================================== places

struct BiomeWord { const char* prep; const char* noun; };
const BiomeWord kBiomeWords[Biome::COUNT] = {
	{ "on", "the open sea" }, { "in", "the shallows" }, { "on", "the shore" }, { "in", "the sands" },
	{ "on", "the dry grasslands" }, { "on", "the plains" }, { "in", "the woods" }, { "in", "the deep forest" },
	{ "in", "the pine woods" }, { "on", "the cold barrens" }, { "on", "the ice" }, { "in", "the hills" },
	{ "on", "the high peaks" }, { "in", "the marshes" }, { "on", "the burnt ground" }, { "on", "the frozen sea" },
};
const BiomeWord& biomeWord(const World& w, int c) {
	if (!w.valid(c) || w.biome.size() != size_t(w.n)) return kBiomeWords[Biome::GRASSLAND];
	int b = w.biome[size_t(c)];
	if (b < 0 || b >= Biome::COUNT) b = Biome::GRASSLAND;
	const bool sea = w.elev.size() == size_t(w.n) && state::isSea(w, c);
	if (sea && b != Biome::SEA && b != Biome::SHALLOWS && b != Biome::SEAICE) b = Biome::SEA;
	if (!sea && (b == Biome::SEA || b == Biome::SHALLOWS || b == Biome::SEAICE)) b = Biome::GRASSLAND;
	return kBiomeWords[b];
}
string terrainPhrase(const World& w, int c) {
	const BiomeWord& bw = biomeWord(w, c);
	return string(bw.prep) + " " + bw.noun;
}

const char* bandOf(const World& w, int c) {
	if (!w.geo || !w.valid(c)) return "the world";
	const double deg = fin(double(w.geo->lat[size_t(c)])) * 180.0 / kPi;
	if (deg > 58) return "the far north";
	if (deg > 22) return "the north";
	if (deg >= -22) return "the middle lands";
	if (deg >= -58) return "the south";
	return "the far south";
}

const char* compass(const World& w, int from, int to) {
	if (!w.geo || !w.valid(from) || !w.valid(to)) return "north";
	Vec3 east, north;
	w.geo->basis(from, east, north);
	const Vec3 d = w.geo->dir(to) - w.geo->dir(from);
	const double ex = v3::dot(d, east), ny = v3::dot(d, north);
	if (!(std::fabs(ex) + std::fabs(ny) > 1e-12)) return "north";
	double a = std::atan2(ex, ny) * 180.0 / kPi;        // 0 = north, 90 = east
	if (a < 0) a += 360;
	static const char* const kDirs[8] = { "north", "north-east", "east", "south-east", "south", "south-west", "west", "north-west" };
	const int k = int(std::floor((a + 22.5) / 45.0)) % 8;
	return kDirs[k < 0 ? 0 : k];
}

// Nearest live settlement to the cell (ties by id); -1 if none. ang = its angle.
int nearestLive(const World& w, int cell, double& ang, int cultureFilter = -1, int exclude = -1) {
	ang = 1e9;
	int best = -1;
	if (!w.geo || !w.valid(cell)) return -1;
	for (const Settlement& s : w.settlements) {
		if (s.dead || !w.valid(s.cell) || s.id == exclude) continue;
		if (cultureFilter >= 0 && s.culture != cultureFilter) continue;
		const double a = w.geo->angle(cell, s.cell);
		if (a < ang) { ang = a; best = s.id; }
	}
	return best;
}

// Place phrases (template values, may hold [a|b] choices):
//   {w}   "in the woods east of Vornhal" / "at Vornhal" / "in the empty sands of the south"
//   {wd}  the same without the terrain, for templates that name it themselves: "east of Vornhal",
//         "of the south" (so "{x} {wd}" reads "the woods east of Vornhal")
// Returns the settlement the place is described by (-1: none within maxAng).
struct Where { string w, wd; };
Where wherePhrase(const World& w, int cell, double maxAng, int* sidOut) {
	if (sidOut) *sidOut = -1;
	if (!w.valid(cell)) return Where{ "[beyond the edge of the known lands|in a place no one has named]", "beyond the edge of the known lands" };
	double a = 0;
	const int sid = nearestLive(w, cell, a);
	const BiomeWord& bw = biomeWord(w, cell);
	const string terrain = string(bw.prep) + " " + bw.noun;
	if (sid >= 0 && a <= maxAng) {
		if (sidOut) *sidOut = sid;
		const string nm = settlementName(w, sid);
		const Settlement* s = sett(w, sid);
		if (a < 0.02) return Where{ "[at|upon] " + nm, "at " + nm };
		const string dir = compass(w, s->cell, cell);
		if (a < 0.06) return Where{ "[near|close by|hard by] " + nm, "[near|close by|outside] " + nm };
		if (a > 0.16) return Where{ terrain + " far to the " + dir + " of " + nm, "far to the " + dir + " of " + nm };
		return Where{ "[" + terrain + " " + dir + " of " + nm + "|" + dir + " of " + nm + "]", dir + " of " + nm };
	}
	const bool sea = w.elev.size() == size_t(w.n) && state::isSea(w, cell);
	const string band = bandOf(w, cell);
	if (sea) return Where{ "[far out on the open sea|on the empty sea of " + band + "|on the lonely waters of " + band + "]", "of " + band };
	return Where{ string(bw.prep) + " [the empty|the wild|the lonely] " + string(bw.noun).substr(4) + " of " + band, "of " + band };
}
// Sets {w} and {wd}; returns the describing settlement (-1 none).
int setWhere(Ctx& x, const World& w, int cell, double maxAng) {
	int sid = -1;
	const Where wh = wherePhrase(w, cell, maxAng, &sid);
	x.set("w", wh.w, true);
	x.set("wd", wh.wd, true);
	return sid;
}

// ====================================================================== context builders

void ctxWorld(Ctx& x, const World& w) { x.set("W", worldName(w)); }

void ctxCulture(Ctx& x, const World& w, int cid, const char* sfx = "") {
	const string s(sfx);
	x.set("c" + s, culturePlural(w, cid));
	x.set("ca" + s, cultureAdj(w, cid));
	x.set("cn" + s, cultureName(w, cid));
	const string g = godOf(w, cid);
	x.set("g" + s, g);
	const Culture* c = cult(w, cid);
	if (c && !c->epithet.empty()) {
		const bool article = startsWith(c->epithet, "the ");
		x.set("ge" + s, (c->godName.empty() ? string("the god, ") : c->godName + (article ? " " : ", ")) + c->epithet);
	}
	else x.set("ge" + s, g);
	const bool she = godIsShe(w, cid);
	x.set("she" + s, she ? "she" : "he");
	x.set("her" + s, she ? "her" : "his");
	x.set("herm" + s, she ? "her" : "him");
}

void ctxSettlement(Ctx& x, const World& w, int sid, const char* sfx = "") {
	const string s(sfx);
	x.set("s" + s, settlementName(w, sid));
	const Settlement* st = sett(w, sid);
	x.set("sn" + s, string("the ") + tierWord(st ? st->tier : 0));
	x.set("st" + s, tierWord(st ? st->tier : 0));
	x.set("t" + s, st ? terrainPhrase(w, st->cell) : string("in the wilds"));
}

// ====================================================================== writing entries

const ChronicleTune& tuneOf(const World& w) { return w.tune<ChronicleTune>(); }

// An entry of this type for this culture within the last `gap` years (any weight)?
// culture = kAnyCulture: an entry of this type for anyone.
constexpr int kAnyCulture = -99;
bool recent(const World& w, string_view type, int culture, double gap) {
	const double y = fin(w.year);
	int scanned = 0;
	for (size_t i = w.chronicle.size(); i-- > 0;) {
		const ChronicleEntry& e = w.chronicle[i];
		if (e.year < y - gap - 1e-9) break;
		// a window this crowded may hide the same kind further back: count it as recent (the rate limit holds)
		if (++scanned > 5000) return true;
		if ((culture == kAnyCulture || e.culture == culture) && e.type == type) return true;
	}
	return false;
}

bool isWordChar(char ch) { return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '-' || ch == 39; }

// A settlement named twice in one entry ("Thunder rolled over Nish, and lightning fell near Nish")
// is called by its size the second time ("... near the village"); {sn} is "the village". A repeat
// right after "no" is deliberate and kept ("and then there was no Brennick").
string renameRepeats(const string& t, const Ctx& x) {
	const Ctx::Kv* nm = x.get("s");
	const Ctx::Kv* sn = x.get("sn");
	if (!nm || !sn || nm->tpl || nm->v.size() < 3) return t;
	const string& name = nm->v;
	string out;
	size_t i = 0;
	int seen = 0;
	while (i < t.size()) {
		const size_t p = t.find(name, i);
		if (p == string::npos) { out.append(t, i, string::npos); break; }
		const bool lb = p == 0 || !isWordChar(t[p - 1]);
		const bool rb = p + name.size() >= t.size() || !isWordChar(t[p + name.size()]);
		out.append(t, i, p - i);
		if (lb && rb) {
			seen++;
			const bool afterNo = out.size() >= 3 && out.compare(out.size() - 3, 3, "no ") == 0;
			if (seen >= 2 && !afterNo) out += sn->v;
			else out += name;
		} else {
			out += name;
		}
		i = p + name.size();
	}
	return out;
}

// One entry from a template family (shuffle bag `key`), optionally followed by a suffix from a second
// family (bag `sfxKey`). Every draw comes from the entry's own fork of w.rng.
ChronicleEntry* writeComposed(World& w, const char* type, int weight, int cell, int culture, Tpl t, const string& key,
	Tpl sfx, const string& sfxKey, const Ctx& x) {
	if (!t.t || t.n <= 0) return nullptr;
	ChronicleScratch& S = w.scratch<ChronicleScratch>();
	Rng r = w.rng.fork("chron:" + std::to_string(w.nextId.entry));
	string body = t.t[pickTemplate(S, key, t.n, r)];
	if (sfx.t && sfx.n > 0) body += sfx.t[pickTemplate(S, sfxKey, sfx.n, r)];
	string raw;
	expandInto(raw, body, x, r, 0);
	const string text = tidy(renameRepeats(raw, x));
	if (text.size() < 2) return nullptr;
	chronicle::EntryOpts o;
	o.type = type;
	o.text = text;
	o.cell = cell;
	o.culture = culture;
	o.weight = weight;
	return chronicle::add(w, o);
}

ChronicleEntry* write(World& w, const char* type, int weight, int cell, int culture, Tpl t, const string& key, const Ctx& x) {
	return writeComposed(w, type, weight, cell, culture, t, key, Tpl{ nullptr, 0 }, string(), x);
}

// Minor entries pass through this gate.
// Minor entries pass through this gate: none of this type for this culture within `gap` years, and
// (globalGap > 0) none of this type for anyone within `globalGap` years.
bool minorOk(const World& w, string_view type, int culture, double gap, double globalGap = 0) {
	if (recent(w, type, culture, gap)) return false;
	return !(globalGap > 0 && recent(w, type, kAnyCulture, globalGap));
}

// (integration verifier) A notable (weight >= 2) entry of this type for this culture within `gap` years?
// exceptCell >= 0: entries at that cell do not count (the same place may be told again).
bool recentNotable(const World& w, string_view type, int culture, double gap, int exceptCell = -1) {
	const double y = fin(w.year);
	int scanned = 0;
	for (size_t i = w.chronicle.size(); i-- > 0;) {
		const ChronicleEntry& e = w.chronicle[i];
		if (e.year < y - gap - 1e-9) break;
		if (++scanned > 5000) return true;
		if (e.culture == culture && e.weight >= 2 && e.type == type && (exceptCell < 0 || e.cell != exceptCell)) return true;
	}
	return false;
}

// ====================================================================== templates
// Keys: {s} settlement, {st} its size word, {t} terrain phrase at it ("on the plains"), {c} culture plural
// ("the Umari"), {ca} adjective, {cn} name, {g} the culture's name for the god, {ge} god + epithet,
// {she} / {her} / {herm} the god's pronouns in that culture, suffix 2 for a second settlement / culture,
// {w} a place phrase, {W} the world, {n} a person, {x} a thing, {want} what a prayer asks for,
// {old} / {e} epithets.

// --- genesis
const char* const kGenWater[] = {
	"The fire went out of {W}, and its skin hardened into black stone. [The god waited.|The god looked upon it and was not yet content.]",
	"{W} cooled. Where there had been a sea of fire there was now only stone, [dry and smoking|cracked and silent], and the god turned to the sky.",
	"[In the beginning|Before the counting of years] {W} burned. Then it cooled, and the god began [the long work|the patient work] of making it a home.",
};
const char* const kGenLife[] = {
	"The deep places of {W} were filled with water, and the god turned to the bare land.",
	"There were seas on {W} now, [grey and restless|wide and cold], and the god bent over the shore to plant the first green.",
	"Water lay upon {W}, and the god said that it was enough, [and it was|and it was nearly].",
};
const char* const kGenPeople[] = {
	"Green covered the lands of {W}, and beasts walked in it, but no one yet knew their names.",
	"{W} was alive, [with forests and grazing herds|with reeds and birds and herds], and the god [looked for a place to wake a people|wondered who should live there].",
	"The forests of {W} grew tall, and the god grew lonely among them.",
};
const char* const kGenHistory[] = {
	"Here the counting of years begins.",
	"[So|Thus] the years began to be counted, [and the scribes began their work|and this book was begun].",
	"From this day the people of {W} counted the years, and this chronicle with them.",
};
const char* const kCometFirst[] = {
	"The first ice fell burning out of heaven, and where it struck, the first water of {W} gathered in the deep places.",
	"A star of ice fell upon the stone of {W}, and the steam of it became the first rain.",
};
const char* const kCometMore[] = {
	"Still the ice fell from heaven, and the seas of {W} grew [deep|wide|restless].",
	"Another ice-star fell, and the waters crept higher up the black shores.",
	"The god called down more ice, and the [grey|young] sea of {W} [rose|swelled] to meet it.",
	"[Again|Once more] the sky burned white, and more water came to {W}.",
};

// --- the god's acts
const char* const kMeteor[] = {
	"A star fell {w}, and the ground [rang like a bell|split open|became a bowl of fire].",
	"Fire came down out of a clear sky {w}. [{c} saw it from {s} and fell on their faces.|It was seen as far as {s}.]",
	"{g} threw down a burning stone {w}, and {c} [did not sleep for many nights|counted the dead and feared the sky].",
	"[In that year |]the sky opened {w} and spat out a mountain of fire. {c} said it was the anger of {g}.",
	"A light like a second sun crossed the sky and fell {w}. [After it came thunder, and after the thunder, silence.|The ground shook under every house in {s}.]",
};
const char* const kMeteorSea[] = {
	"A star fell into the sea {w}, and the waters stood up like a wall.",
	"Fire fell from the sky {w} and the sea boiled. [Fishermen of {s} saw it and fled inland.|{c} watched the waves come.]",
	"{g} threw a stone of fire into the sea {w}, and the sea threw it back as a wave.",
};
const char* const kMeteorFar[] = {
	"A star fell {w}, [and the sky was red for many nights|so far away that only its thunder reached the people].",
	"Something burning crossed the sky and fell {w}. [No one saw where.|The wise argued for years about what it had been.]",
	"Far away, {w}, the sky fell. The dust of it reddened the sunsets for many years.",
};
const char* const kLightningGod[] = {
	"{g} struck the ground {w} with lightning[, and the people of {s} hid their faces|; {c} counted it a warning].",
	"Lightning fell {w} out of a clear sky. {c} said {g} was [angry|watching|not pleased].",
	"A spear of light came down {w}, and the {ca} elders argued for a long time about what it meant.",
	"There was no cloud in the sky, and still the lightning fell {w}. [{c} knew whose hand had thrown it.|In {s} they spoke of it in whispers.]",
	"{g} threw fire from heaven {w}, [and the people of {s} brought offerings to the shrine that night|and {c} asked one another what they had done wrong].",
	"A bolt of white fire struck {w} at noon. {c} said it was {g} clearing {her} throat.",
};
const char* const kLightningNat[] = {
	"Lightning struck {w} in a summer storm.",
	"A storm passed over {s}, and lightning [split an old tree|set the grass alight|killed a herd of goats] {wd}.",
	"Thunder rolled over {s}, and lightning fell {w}.",
	"In a night of thunder a bolt came down {w}, [and in the morning the shepherds found the stones blackened|and the dogs of {s} howled until dawn].",
	"Lightning walked on the hills {wd}, [and {c} stayed indoors|and the priests of {s} read it as a sign|and no one was hurt, that time].",
	"A tree {wd} was split by lightning. [The wood was kept as a holy thing in {s}.|The children of {s} dared each other to touch it.]",
	"The sky over {s} cracked open with fire, [and the rain that followed was warm|and the old oak {wd} burned like a torch].",
};
const char* const kWave[] = {
	"A wall of water came out of the sea upon {s}.",
	"The sea rose up and walked inland at {s}, [taking boats and houses and cattle|and did not ask leave].",
	"A great wave broke {w}, and the people of {s} [ran for the hills|counted their dead].",
	"The sea went out from the shore of {s}, very far, and then it came back all at once.",
	"A grey wave as tall as a temple broke over the fields of {s}, and salt lay on them for years.",
};
const char* const kVolcanoGod[] = {
	"{g} split the earth {w}, and a mountain of fire rose out of it.",
	"At the word of {g}, the ground {w} [opened|tore] and bled fire. {c} watched from {s} and were afraid.",
	"The god built a mountain of fire {w}. [Ash fell on {s} like grey snow.|Its glow lit the nights of {s}.]",
};
const char* const kVolcanoNat[] = {
	"The mountain {w} woke and wept fire.",
	"Smoke rose from the earth {w}, and then fire. [Ash fell on {s} like grey snow.|{c} said the mountain was angry.]",
	"A fire-mountain [woke|was born] {w}, and the sky above {s} turned [black|the colour of iron].",
};
const char* const kVolcanoFar[] = {
	"Far away, {w}, a mountain was born in fire, and its ash darkened the sky.",
	"The earth opened {w}, where no one lived, and fire poured out [for many years|until the sea quenched it].",
};
const char* const kVolcanoEnd[] = {
	"The fire-mountain {w} fell asleep again.",
	"The fires {w} cooled into black stone, [and in time the ash made the fields rich|and the people of {s} went back to their fields].",
	"The mountain {w} stopped smoking, [and {c} gave thanks|though no one trusted it].",
};
const char* const kQuakeGod[] = {
	"{g} shook the earth {w}, and the houses of {s} fell down.",
	"The ground {w} heaved like the sea. {c} said {g} had stamped {her} foot.",
	"At the touch of the god the hills {wd} [cracked|shuddered], and {s} [was shaken to its roots|lost many houses].",
	"{g} struck the earth {w}, and the land rang like a drum. [In {s} the roofs came down.|{c} fell on their faces.]",
	"The earth opened its mouth {w}, and {c} knew that {g} was angry with them.",
};
const char* const kQuakeNat[] = {
	"The earth shook {w}.",
	"The ground {w} [trembled|rolled like a wave] for the space of a hundred breaths, and [walls fell in {s}|cracks opened in the fields of {s}].",
	"There was an earthquake {w}, and the people of {s} slept outside for a month.",
	"The earth groaned under {s}, [and the well water turned cloudy|and the temple bell rang by itself].",
	"A tremor ran through the land about {s}, [and pots fell from the shelves|and the old wall finally came down|and the cattle would not go into their byres].",
};
const char* const kFire[] = {
	"Fire ran through {x} {wd}.",
	"The country {w} burned [for many days|until the rain came], and the smoke hung over {s}.",
	"A wildfire came close to {s}, [and the people beat it back with green branches|and turned only when the wind did|and the children watched it from the roofs].",
	"{x} {wd} burned, and [{c} watched the glow at night|the herds fled toward {s}|the sky over {s} was brown for days].",
	"[That summer|In a dry season|In the month of dust] a fire ate {x} {wd}.",
	"A red night: {x} {wd} burned, and ash fell on {s}.",
	"Fire walked through {x} {wd}, [and the beasts ran before it|and left only ash behind it|and {c} prayed for rain].",
	"The old people of {s} said the fire {w} had been started by [a careless shepherd|the god|a falling star], [and argued about it all winter|and kept their buckets full].",
};
const char* const kStorm[] = {
	"A great storm came in from the sea upon {s}.",
	"Wind and rain beat upon {s} [for three days|for a week], [and the roofs flew|and the rivers rose|and the boats were thrown up on the fields].",
	"A storm such as the old people had never seen passed over {s}.",
	"The sea sent a howling wind against {s}, [and it took the thatch from every roof|and the fishermen did not go out for a month].",
	"Black clouds came over {s} from the sea, [and the hail broke the young barley|and the river ran brown for days].",
	"In {s} they still tell of the great wind, [which carried a goat over the temple|which blew the fires out in every hearth|when even the priests were afraid].",
	"A storm walked inland over the country of {s}, [and {c} counted their dead and their cattle|and left the fields flattened behind it].",
};
const char* const kDroughtStart[] = {
	"The rains failed around {s}.",
	"No rain fell on the fields of {s} [that year|for a long time], and the wells [grew bitter|ran dry].",
	"There was drought in the country of {s}, and {c} looked at the sky every morning.",
	"The river by {s} shrank to a string of muddy pools, [and the frogs were the first to leave|and the cattle stood in it and lowed].",
	"A hard sun stood over {s} [all that summer|year after year], and the barley died standing.",
	"The rains forgot the country of {s}. [The priests climbed the hills and called to them.|The children grew up not knowing the smell of rain.]",
	"In {s} the ground cracked like old pottery, and {c} [carried water from far away|began to pray for rain].",
};
const char* const kDroughtEnd[] = {
	"Rain returned to {s} at last.",
	"The drought about {s} broke, and the children ran out into the rain.",
	"The rains came back to {s}, [and {c} said the god had relented|as rains do, in their own time].",
	"Clouds came back over {s}, and the dry river filled in a single night.",
	"The long thirst of {s} ended in a storm, [and {c} danced in the mud|and the old people wept].",
	"Green came back to the fields of {s}, and the wells were sweet again.",
};
const char* const kRainThirst[] = {
	"{g} sent rain on the thirsty fields of {s}.",
	"Rain fell on {s} out of a clear sky, and {c} danced in it.",
	"The dry country about {s} drank rain [sent by {g}|that came from nowhere], and turned green again.",
	"Clouds gathered over {s} where no clouds had been, and wept on the dry fields. {c} gave thanks to {g}.",
	"Just when the wells of {s} were failing, the god sent rain[, and the frogs sang all night|; the priests said they had known it would come].",
	"A dark cloud stood over the parched fields of {s} and rained on them, and on nowhere else.",
};
const char* const kRainSoft[] = {
	"Soft rain fell over {s}.",
	"[It rained on {s}, gently and for a long time|A long gentle rain came to {s}], and {c} [said it was a blessing|did not complain].",
	"Rain came to {s}. [The old people said it smelled of the god.|The fields were glad of it.]",
	"A warm rain fell on {s} [all through the night|for three days], and the roofs leaked, and nobody minded.",
	"The god let rain fall on {s}, though it had not been asked for. [The farmers said it was too much.|The children made mud houses.]",
	"Mist and rain came to {s}, and the hills around it [turned a deeper green|ran with little streams].",
};
const char* const kBloom[] = {
	"The fields about {s} grew green [overnight|as if in a dream].",
	"{g} breathed on the land around {s}, and it flowered.",
	"Around {s} the [grass grew to a man's waist|trees bent with fruit], and {c} said {g} had walked there.",
	"The land of {s} [bloomed|blossomed] out of season, and the beekeepers grew rich.",
	"Where the god passed near {s}, [seeds woke in the dry ground|flowers opened in the snow|the orchards fruited twice].",
	"The herds about {s} grew fat on grass that had not been there the week before.",
};
const char* const kHeal[] = {
	"{g} laid a hand on {s}, and the sick rose from their beds.",
	"The fever left {s} in a single night. {c} said {g} had walked through the streets.",
	"In {s} the sick were healed, [all at once|between one breath and the next], and the healers lost their trade for a while.",
	"A cool wind passed through {s}, and in every house the fevered slept and woke well.",
	"{s} was healed by {g}. [Its people carved a hand on the temple wall.|For a generation no one there feared the sickness.]",
};
const char* const kInspire[] = {
	"The craftsmen of {s} dreamed the same dream, and woke knowing new things.",
	"A fire of thought was lit in {s}[, and {c} built things no one had built before|; the scribes could hardly keep up].",
	"{g} whispered to the wise of {s}, and they [argued about it for years|wrote it all down].",
	"In {s} a potter invented a wheel, a weaver a loom, and a priest a reason why the god had done it.",
	"The young of {s} were seized with questions, [and the old could not answer them|and went looking for answers in the stars].",
};
const char* const kOmenPeace[] = {
	"An omen of peace was seen over {s}, and {c} laid down their spears.",
	"[A white light|A great calm] lay over {s} for a day, and {c} [could not remember why they had been angry|sent envoys instead of armies].",
	"{g} spoke peace over {s}, and for once {c} listened.",
};
const char* const kShapeHarm[] = {
	"The ground heaved under {s} at the touch of the god.",
	"{g} reshaped the land under {s} while its people slept, and they woke to find their fields [tilted|broken].",
	"The earth moved under {s} like dough under a baker's hand. {c} did not find it amusing.",
};
const char* const kRaise[] = {
	"Hills rose near {s} where there had been none.",
	"The land near {s} lifted itself up, [and the shepherds had new pastures|and the old maps were wrong].",
	"{g} raised the ground {w}, and {c} [climbed it to see what the god had made|built a cairn on top].",
};
const char* const kLower[] = {
	"The land {w} sank, and water came in.",
	"The ground {w} went down like a sigh, [and became a valley|and the sea followed it].",
	"{g} pressed the land {w} down, and {c} watched new water spread where fields had been.",
};
const char* const kPlagueSent[] = {
	"{c} said the sickness in {s} was sent by {g}, and they were afraid.",
	"In {s} they whispered that the fever was the god's own breath. [Few went to the temples that year.|Many went to the temples that year.]",
	"The priests of {s} said {g} had sent the sickness for their sins, though no one could agree which sins.",
};

// --- the peoples
const char* const kAwakening[] = {
	"{g} touched the sleeping clay {t}, and the first of {c} opened their eyes. They named their camp {s}.",
	"Here begins the story of {c}. The god woke them {t}, and they gathered at {s}, [not knowing who had called them|afraid of the dark|and looked up].",
	"At {s}, {c} woke [at dawn|under strange stars|in the rain]. [The first word they spoke was a name for the one who woke them.|They were few, and they were hungry, and they were alive.]",
};
const char* const kAwakeningAgain[] = {
	"Again the god woke a people: {c}, at {s}.",
	"{g} called a new people out of the earth {t}. They were {c}, and their first camp was {s}.",
	"At {s}, {t}, {c} woke and looked about them, [and found the world already old|and saw the ruins of others].",
};
const char* const kFounded[] = {
	"Settlers of {c} went out from {s2} and made a new home at {s}.",
	"[In that year |]a band from {s2} crossed the country and founded {s} {t}.",
	"Families of {c} who left {s2} built their first fires at {s}, [and called it good|and were not sorry].",
	"{s} was founded {t} by {c}, who had grown too many for {s2}.",
	"[It is written that |]{s} began as a ring of tents {t}, raised by wanderers from {s2}.",
	"Wanderers from {s2} came to {s} [and stayed|and did not go back].",
	"The young people of {s2} grew tired of their fathers' fields and went out to found {s} {t}.",
	"A new village of {c}, {s}, was raised {t}, [a few days' walk from {s2}|where the hunters of {s2} had always camped].",
};
const char* const kFoundedLone[] = {
	"{c} founded {s} {t}.",
	"[In that year |]a new camp of {c} was raised at {s}, {t}.",
	"Wanderers of {c} settled at {s}, {t}.",
	"Far from their kin, a band of {c} made a home at {s}, {t}.",
	"{s} was founded {t} by {c}, [who had walked a long way to find it|though no one remembered why there].",
};
const char* const kTierVillage[] = {
	"{s} grew from a camp into a village.",
	"There were now enough hearths at {s} to call it a village.",
	"The camp at {s} put down roots and became a village.",
	"{s} was a village now, with [a well and a headman|a fence against the wolves|its own small quarrels].",
	"The tents of {s} gave way to houses of [mud and reed|timber|stone], and it was called a village.",
	"Children were born at {s} who had never lived anywhere else, and it became a village.",
};
const char* const kTierTown[] = {
	"{s} became a town, with [streets|a market-day|a wall of thorns].",
	"The village of {s} grew into a town[, and its people began to look down on villages|; it had a name even among strangers].",
	"{s} was a town now, [the busiest of {c}|loud and crowded and proud].",
	"{s} grew until a man could get lost in it, and they called it a town.",
	"The fields around {s} filled with new houses, and it became a town of {c}.",
	"{s} became a town, [and chose a council of elders, who argued|and built a gate, though it had no wall yet].",
};
const char* const kTierCity[] = {
	"{s} became a city, [and its people forgot they had ever lived in tents|and its towers could be seen from far off|the pride of {c}].",
	"The town of {s} grew into a city of {c}, [with paved streets and quarrelsome councils|full of smoke and music].",
	"Many roads now led to {s}, and it was called a city.",
	"{s} had become a city, with more people in it than [there were sheep in the hills|anyone could name].",
	"Walls of cut stone went up around {s}, and the {ca} scribes began to call it a city.",
	"{s} grew past counting into a city, [and its markets never closed|and its temple could be seen from a day's walk away].",
	"The scribes of {c} began to write {s} with the sign for 'city', [and its people were insufferable about it|and its taxes rose accordingly].",
	"Ten thousand hearths burned in {s}. It was a city now, [the jewel of {c}|and it knew it].",
	"{s} outgrew its walls twice and became a city of {c}.",
};
const char* const kTierFirstCity[] = {
	"{s} became the first city in the world, [and its people did not let anyone forget it|and its walls held more people than the whole world had once held].",
	"Nowhere had so many people lived together as at {s}, the first city of {W}.",
};
const char* const kTierMetro[] = {
	"{s} became a great city such as the world had not seen, [numberless as ants|lit all night|and the sky over it never grew dark].",
	"{s} swallowed the villages around it and became a great city of {c}.",
	"So many people lived in {s} that no one could count them, [though the scribes tried|and so they stopped trying].",
	"{s} grew into a city of towers, [its lights seen from the next valley|and its noise heard by the god, the priests said].",
	"{s} became a great city, a sea of roofs from one horizon to the other.",
	"The lamps of {s} drowned out the stars: it had become a great city of {c}.",
};
const char* const kTierDown[] = {
	"{s} dwindled to a {st}.",
	"Many left {s}, and it was a {st} again.",
	"{s} grew small and was only a {st}, [and the old people remembered better days|and grass grew in its empty streets].",
	"Half the houses of {s} stood empty, and it was no more than a {st}.",
	"{s} shrank to a {st}. [Its young had gone to other places.|Its walls were too big for it now.]",
};
const char* const kBShrine[] = {
	"The people of {s} raised a shrine to {g}.",
	"At {s} a shrine of [stones|wood|bones and flowers] was made for {g}.",
	"{c} set up a shrine at {s}, and left [bread|flowers|small carved birds] there for the god.",
	"A circle of white stones was laid out at {s}, and {c} came there to speak to {g}.",
	"{s} built a small shrine on the hill above the fields, [and the first thing laid on it was a child's toy|and kept a fire burning in it].",
	"The elders of {s} chose a holy place, and a shrine to {g} was raised there.",
};
const char* const kBShrineFirst[] = {
	"At {s}, {c} raised their first shrine to {g}, [a ring of stones|a heap of white stones] where they could speak to the sky.",
	"{c} made their first shrine at {s}, and gave the god who had woken them a name: {g}.",
};
const char* const kBTemple[] = {
	"A temple to {g} was built in {s}.",
	"The priests of {s} built a house for the god [of cut stone|with a golden roof|with a hundred steps].",
	"{s} raised a temple to {g}, [and the priests grew fat|and its bells could be heard in the fields].",
	"In {s} the old shrine was pulled down and a temple to {g} put up in its place.",
	"{c} of {s} spent [ten|twenty] years on a temple to {g}, [and were very proud of it|and then argued about the colour of the doors].",
};
const char* const kBGranary[] = {
	"{s} built a granary against lean years.",
	"The people of {s} began to store grain [in great jars|in a house of clay], [as the wise had advised|after a hungry winter].",
	"A granary was raised in {s}, and its keepers became the most important people in town.",
	"{s} dug storage pits and lined them with clay, [and slept better for it|and the mice rejoiced].",
	"The headmen of {s} ordered a tithe of every harvest kept in a great barn, against the bad years.",
};
const char* const kBWalls[] = {
	"{s} raised walls [of stone|of earth and timber], for there was war.",
	"Walls rose around {s}, and the {ca} watchmen stood upon them at night.",
	"The people of {s} built a wall and a gate, [and slept behind them|and shut it every night at dusk].",
	"Fearing the war, {s} ringed itself with a ditch and a palisade.",
};
const char* const kBPort[] = {
	"{s} built a harbour, and ships came and went.",
	"A port was made at {s}, and the {ca} sailors [learned the winds|grew bold].",
	"The fishermen of {s} built a stone quay, [and the big ships began to call|and the town turned its face to the sea].",
	"{s} cut a harbour into the shore, and its boats went further every year.",
	"Masts filled the bay at {s}, where a harbour had been built.",
};
const char* const kBMarket[] = {
	"A market was founded in {s}, and strangers came to it.",
	"{s} became a market town, [loud with haggling|full of salt and cloth and lies].",
	"Every seventh day the square of {s} filled with traders, [and the priests complained of the noise|and the pickpockets prospered].",
	"{s} built a market hall, and its merchants grew rich and pompous.",
	"Weights and measures were set in stone at {s}, and its market became the fairest in the land, or so it claimed.",
};
const char* const kBMine[] = {
	"The people of {s} dug into the hills for [copper|ore|good stone].",
	"Mines were opened near {s}, and the hills were never quite the same.",
	"The miners of {s} went deep into the mountain, [and came back black with dust and rich|and some did not come back].",
	"A vein of ore was found above {s}, and the town grew loud with hammers.",
};
const char* const kBFactory[] = {
	"Chimneys rose over {s}, and the sky above it turned grey.",
	"{s} built its first manufactory, [and the smiths grumbled|and the river ran a strange colour].",
	"The engines of {s} began to turn, and their noise did not stop at night.",
	"In {s} great sheds were built where a thousand hands did the work of ten thousand, and the air tasted of iron.",
};
const char* const kBAcademy[] = {
	"An academy was founded at {s}, where the wise argued about everything, the god included.",
	"Scholars gathered at {s} and built a house of learning, [and filled it with books|and filled it with arguments].",
	"At {s} a school was opened where the young learned numbers, stars and the names of things.",
	"The wise of {c} came together at {s}, [and wrote down everything they knew|and began to ask questions the priests did not like].",
};
const char* const kBPlant[] = {
	"{c} built at {s} a house where the smallest fire burns.",
	"A great power plant hummed at {s}, [and its lights never went out|and the {ca} priests did not like it].",
	"At {s} the hidden fire was put to work, and the lamps of a whole country were lit from it.",
};
const char* const kBLaunch[] = {
	"At {s}, {c} built a tower to throw ships at the stars.",
	"A launch field was cleared at {s}, and {c} began to speak of leaving.",
	"{c} built a great tower of fire at {s}. Its makers looked up at night, [and the god could not tell whether they were longing or afraid|and dreamed of other skies].",
};
const char* const kBuildFirstSfx[] = { " It was the first {bn} of {c}.", " No one among {c} had built {bna} before." };
const char* const kBuildingNoun[Building::COUNT][2] = {
	{ "shrine", "a shrine" }, { "temple", "a temple" }, { "granary", "a granary" }, { "wall", "walls" }, { "harbour", "a harbour" },
	{ "market", "a market" }, { "mine", "a mine" }, { "manufactory", "a manufactory" }, { "academy", "an academy" },
	{ "power plant", "a power plant" }, { "launch tower", "a launch tower" }, { "wonder", "a wonder" },
};
const char* const kFamine[] = {
	"Famine came to {s}.",
	"The harvest failed at {s}[, and the people ate their seed corn|; the children grew thin].",
	"At {s} there was hunger, three years running.",
	"The granaries of {s} stood empty, and its people [went begging to their neighbours|boiled the bark of trees].",
	"There were too many mouths in {s} and too little bread, [and the old went hungry so the young could eat|and the weak did not see the spring].",
	"[That winter|In those lean years] {s} ate its oxen, and then its dogs.",
	"Hunger walked the streets of {s}, [and {c} sold their tools for grain|and the fields were not enough for all of them].",
	"The fields about {s} could not feed it any longer. [Some families left in the night.|The priests asked {g} for bread.]",
};
// famines while dust darkens the sky (volcanic or nuclear winter): the hunger is everywhere
const char* const kFamineDust[] = {
	"Ash hid the sun from {W}, the summers did not come, and {s} went hungry[, and many places besides| with all its neighbours].",
	"The sky stayed grey over {W} for years. The harvest failed at {s}, and not only there.",
	"In the dark years the barley would not ripen, and hunger came to {s} and to many towns of {c}.",
	"The sun was a pale coin behind the dust, and the granaries of {s} stood empty. {c} remembered those years as the hungry ones.",
};
const char* const kPlague[] = {
	"A sickness came to {s}.",
	"Fever spread through {s}, [and the priests could do nothing|and many were buried].",
	"In {s} the people sickened, and the roads to it were empty.",
	"A coughing sickness came to {s} [with the traders|in the spring], and [lingered|did not leave].",
	"The spotted fever broke out in {s}, [and its gates were shut|and its neighbours turned travellers away].",
	"In {s} they painted marks on the doors of the sick, [and there were many marks|and the marks spread from street to street].",
	"A grey sickness settled on {s}. [The healers burned herbs day and night.|{c} said it came from the marshes.]",
};

// destroyed, by cause
const char* const kDDrowned[] = {
	"{s} was swallowed by the sea.",
	"The waters rose over {s}, and its people fled to higher ground.",
	"Where {s} stood, fish now swim.",
	"{s} drowned, [and its bells could be heard under the waves, the sailors said|and the sea kept it].",
	"The sea came up the streets of {s} a little more each year, until the last family rowed away.",
};
const char* const kDMeteor[] = {
	"{s} was struck from the sky and was no more.",
	"A falling star made {s} into a pit of glass.",
	"Fire from heaven fell on {s}, and when the smoke lifted there was nothing left to name.",
};
const char* const kDLava[] = {
	"Rivers of fire ran through {s} and buried it.",
	"{s} was lost beneath the lava, [all but its name|and the black stone kept its shape].",
	"The fire-mountain came down upon {s} and ate it.",
};
const char* const kDWar[] = {
	"{s} was sacked and burned, and its people scattered.",
	"Of {s}, nothing was left but ash and the names of the dead.",
	"Enemies came to {s} and left only smoke.",
	"{s} fell in the war and was razed, [and salt sown in its fields|and the crows grew fat].",
	"The war came to {s} and did not leave anything standing.",
	"{s} was put to the torch, and its people were carried off or killed.",
};
const char* const kDFamine[] = {
	"{s} starved, and those who lived left it empty.",
	"Hunger emptied {s}. [Its fields went back to the wild.|No one came back.]",
	"The last people of {s} ate what they could find, and then they left.",
	"{s} died of hunger, and its granary stood open and empty for a hundred years.",
};
const char* const kDPlague[] = {
	"The sickness emptied {s}.",
	"No one was left in {s} to bury the dead.",
	"{s} died of the fever, house by house.",
	"The plague took {s}, and travellers went round it for a generation.",
};
const char* const kDQuake[] = {
	"The earth opened under {s}.",
	"{s} was shaken to rubble, [and the survivors would not rebuild it|and its ruins were left as a warning].",
};
const char* const kDNuke[] = {
	"{s} vanished in a light brighter than the sun.",
	"A second sun rose over {s}, and then there was no {s}.",
	"{s} was burned away in a single breath, [and the shadows of its people stayed on the stones|and the sky rained ash for a year].",
};
const char* const kDAbandoned[] = {
	"{s} was abandoned.",
	"The last families left {s}, and grass grew in its streets.",
	"{c} gave up {s}, [and the wind had it|and the foxes moved in].",
	"One by one the hearths of {s} went cold, until none were left.",
	"{s} was left empty. [No one could say exactly when.|Its people had gone to the bigger towns.]",
};
const char* const kDWave[] = {
	"A great wave carried {s} out to sea.",
	"The sea stood up and fell on {s}, and when it drew back, {s} was gone.",
};
const char* const kDLightning[] = {
	"Lightning set {s} ablaze, and it burned to nothing.",
	"{s} burned after a thunderstorm, [roof by roof|in a single night].",
};
const char* const kCapturedWar[] = {
	"{c2} took {s} from {c}.",
	"{s} fell to {c2}[, and its people learned new songs|, and its {ca} lords were driven out].",
	"The banners of {c2} were raised over {s}.",
	"After a hard siege {s} opened its gates to {c2}.",
	"{s} was lost by {c} and won by {c2}, [and its temple was given a new god's name|and its market changed hands with it].",
	"{c2} marched into {s}, [and the {ca} families who could fled to their kin|and the town paid tribute from that day].",
};
const char* const kCapturedSchism[] = {
	"{s} turned from {c} and joined {c2}.",
	"The people of {s} no longer called themselves {cn}; they were {c2} now.",
};
const char* const kBattleLost[] = {
	"{c} came against {s} and were thrown back.",
	"Before {s}, the army of {c} was broken.",
	"{c2} held {s} against {c}, [and the field was red|and sang about it for a hundred years].",
	"An army of {c} besieged {s} and went home with nothing [but its wounded|but stories].",
	"The walls of {s} held, and {c} left their dead before them.",
	"{c} attacked {s} [at dawn|in the rain|on a holy day] and were beaten [by its farmers|by its old men and boys|in the river mud].",
};
const char* const kPrayer[] = {
	"The people of {s} prayed for {want}.",
	"In {s} they burned [incense|fat|sweet grass] and asked {g} for {want}.",
	"The priests of {s} went up to the high places and cried out for {want}.",
	"At {s}, {c} prayed to {g} for {want}, [morning and evening|and fasted].",
	"The women of {s} [tied red threads to the shrine|sang at the shrine every evening] and asked for {want}.",
	"In {s} a [white goat|lamb|jar of honey] was given to {g}, with a prayer for {want}.",
	"{c} of {s} wrote their prayer for {want} on [clay|bark] and buried it at the shrine.",
};
const char* const kPrayerLost[] = {
	"The people of {s} stopped praying for {want}; {g} had not answered them.",
	"In {s} they said the god was deaf, and [stopped asking|let the shrine fall into ruin].",
	"For many years {s} prayed for {want}, and no answer came. [The priests had no explanation.|The young stopped going to the shrine.]",
	"{s} gave up asking {g} for {want}. [Its people grew bitter.|They learned to manage without.]",
	"The prayers of {s} went up for fifteen years and nothing came down. [{c} began to wonder whether anyone was listening.|The shrine there was left without flowers.]",
};
const char* const kAnsRain[] = {
	"The people of {s} prayed for rain, and {g} sent it.",
	"{s} had prayed for rain, and the rain came in the night, [and the priests were proven right|and no one slept for joy].",
	"{g} heard the dry fields of {s}, and opened the sky.",
	"The priests of {s} were still singing for rain when it began to fall on them.",
	"Rain answered the prayers of {s}, [warm and heavy and long|so suddenly that the priests were soaked at the altar].",
	"For {s}, thirsty and praying, {g} [gathered the clouds|sent a long grey rain], and the wells filled again.",
	"The rain-dancers of {s} had danced for three days when {g} answered them with thunder and a long rain.",
	"{s} asked for rain, and {g} gave it, [and more besides: the river flooded the lower fields|and the barley stood up again].",
	"In {s} they had prayed for rain until their voices broke. Then the sky darkened, and it came.",
};
const char* const kAnsHeal[] = {
	"{g} heard the sick of {s}, and the fever broke.",
	"The people of {s} prayed to be healed, and they were, [and the temple was full for a year|and they did not forget it].",
	"In {s} the sick had prayed, and one morning they woke well.",
	"The healers of {s} had given up, but {g} had not. The fever left the town in a single night.",
	"{s} prayed against the sickness, and {g} [took it away like a cloth from a table|laid a cool hand on every brow].",
	"The sick of {s} were carried to the shrine, and {g} [walked out of it with them|sent them home on their own feet].",
};
const char* const kAnsPeace[] = {
	"{c} prayed for peace, and {g} gave it.",
	"The prayers of {s} for peace were answered, [and the soldiers came home|and the war ended as if a door had closed].",
	"{s} had prayed for an end to the war, and the god heard. The spears were hung on the walls again.",
	"The mothers of {s} prayed for peace, and {g} [sent it|put a quiet on both armies].",
	"{c} had asked {g} to end the war, and the god put a weariness on both armies until they went home.",
};
const char* const kAnsBloom[] = {
	"{g} answered {s}, and the burnt fields grew green.",
	"The people of {s} had prayed over their blackened fields, and now the fields were green, [and they wept|and they sang].",
	"Where the ground about {s} had been ash, flowers came up in a single week, as the people had prayed.",
	"{s} asked {g} to heal the burnt land, and green shoots answered them.",
	"Over the black fields of {s} the god breathed, as its people had begged, and the ash turned to grass.",
};
const char* const kCultureDistance[] = {
	"Far from their old capital, the people about {s} began to call themselves {c}[, and spoke a tongue {c2} could barely follow|, and paid no more taxes to {c2}].",
	"In the lands about {s} a new people arose, {c}, [children of {c2}|descended from {c2}] but no longer their subjects.",
	"The {ca2} settlers about {s} had been apart too long. They took a new name, {cn}, and went their own way.",
};
const char* const kCultureSchism[] = {
	"{c} broke away from {c2}, [saying that the {ca2} priests had forgotten {g2}|over a quarrel about the god].",
	"There was a schism among {c2}: the faithful of {s} called themselves {c} and would not bow to the old capital.",
	"{c} were born of a quarrel. They had been {c2}, until they disagreed about {g2}.",
};
const char* const kCultureDied[] = {
	"The last of {c} passed from the world, [and their songs with them|and only their ruins remember them].",
	"{c} are no more.",
	"Of {c}, who once held {s}, nothing remained but names.",
	"{c} [faded|dwindled] and were gone, [and other peoples walked in their streets|and no one spoke their tongue any longer].",
};
const char* const kAgeBronze[] = {
	"{c} learned to marry copper and tin, and the age of bronze began among them.",
	"In the forges of {s}, {c} made bronze for the first time, [and their spears grew sharp|and their kings grew proud].",
	"{c} discovered bronze. [Their bells rang for the first time.|Their tools would not break.]",
};
const char* const kAgeIron[] = {
	"{c} drew iron out of red stone, and the age of iron began among them.",
	"The smiths of {s} learned the secret of iron, and {c} were never weak again.",
	"{c} worked iron, [and their ploughs bit deep|and their swords grew long].",
};
const char* const kAgeSail[] = {
	"{c} raised sails and went out upon the great sea.",
	"The shipwrights of {c} learned to sail against the wind, [and the world grew wider|and new shores appeared on their maps].",
	"{c} took to the open sea, [and nowhere was far any more|and came back with strange fruit and stranger stories].",
};
const char* const kAgeIndustry[] = {
	"{c} built engines that breathed fire, and the age of smoke began among them.",
	"In {s} the first engines turned, and {c} did more work in a day than their grandfathers in a year.",
	"{c} harnessed fire and steam. [The forests grew thin and the cities grew tall.|Their nights grew bright and their skies grew grey.]",
};
const char* const kAgeAtomic[] = {
	"{c} split the smallest thing in the world, and found a fire inside it.",
	"The wise of {c} learned to wake the fire inside stones, [and the god was not consulted|and some of them were afraid].",
	"{c} entered the age of the hidden fire, [with great hopes|with great fears|with both hope and fear].",
};
const char* const kAgeSpace[] = {
	"{c} turned their eyes to the stars, and built towers of fire to reach them.",
	"The first ship of {c} went up beyond the sky and came back, [and its sailors spoke of the world as a small blue stone|and the priests were very quiet].",
	"{c} reached the edge of heaven, [and found it empty|and looked back at {W}].",
};
const char* const kAgeFirstSfx[] = {
	" No people had done so before them.",
	" They were the first in all the world.",
	" The other peoples of {W} had not yet dreamed of it.",
};
const char* const kWarDeclared[] = {
	"{c} made war on {c2}.",
	"{c} marched against {c2}, [and the fields were left unharvested|and the smiths worked day and night].",
	"The {ca} kings swore an oath against {c2}, and there was war.",
	"The long quarrel between {c} and {c2} came to blood.",
	"{c} and {c2} could no longer share the world, and went to war [over a border|over an insult|over the god, both said].",
	"Envoys of {c} [threw down their staffs|spat on the threshold] before {c2}, and the war began.",
	"{c} sharpened their spears and went against {c2}, [saying the god was with them|though no one could remember who had struck first].",
	"War came between {c} and {c2}, [the old enemies|who had once traded salt and songs].",
};
const char* const kWarDraw[] = {
	"The war between {c} and {c2} ended, and neither could say it had won.",
	"{c} and {c2} made peace, being tired of burying their sons.",
	"After much killing {c} and {c2} agreed that the border was where it had always been.",
	"The war of {c} and {c2} wore itself out, [like a fire with nothing left to burn|and both sides claimed the victory].",
	"{c} and {c2} laid down their arms. [Nothing had changed but the number of graves.|The poets of both peoples wrote songs of triumph.]",
};
const char* const kWarWon[] = {
	"{c} won their war against {c2}.",
	"{c2} sued for peace, and {c} granted it, [at a price|graciously, for once].",
	"The war ended in victory for {c}. [{c2} paid tribute for a generation.|In {s} there were feasts for a month.]",
	"{c2} were beaten, and {c} [took their best fields|carried their gods home in carts].",
	"The banners of {c} came home in triumph to {s}, and {c2} [counted what they had lost|swore to remember].",
};
const char* const kTrade[] = {
	"The first caravan went from {s} to {s2}.",
	"Merchants began to travel between {s} and {s2}, [and the road between them grew wide|bringing salt and news].",
	"A road was worn between {s} and {s2} by the feet of traders.",
	"Ox-carts began to creak along the road from {s} to {s2}, [loaded with grain and gossip|and inns grew up beside it].",
	"{s} and {s2} began to trade, [cloth for copper|pots for fish|wine for wool], and both grew fat on it.",
	"The merchants of {s} found a road to {s2}, [and kept the secret of it for a while|and the road found more merchants].",
};
const char* const kTradeForeign[] = {
	"Merchants of {c} reached {s2} of {c2}, and the two peoples traded [salt and stories|cloth for copper|songs and diseases].",
	"The first caravan from {s} came to {s2}, and {c} and {c2} learned each other's words for 'price'.",
	"Traders of {c} and {c2} met at {s2}, and each thought they had cheated the other.",
	"A road was opened between {s} of {c} and {s2} of {c2}, [and strange coins began to turn up in both markets|and the two peoples began to borrow each other's words].",
	"{c2} of {s2} saw their first {ca} merchants, [and laughed at their hats|and bought everything they had].",
};
// later routes between two peoples who already trade (minor)
const char* const kTradeForeignMore[] = {
	"Caravans of {c} now came to {s2} of {c2} as well.",
	"Another road joined {s} to {s2} of {c2}, [and the inns along it prospered|and the tolls on it were argued over].",
	"{s2} of {c2} [began to buy|grew used to] the goods of {s}, and its merchants learned the {ca} way of haggling.",
	"Traders from {s} found their way to {s2} of {c2}, where {ca} coin was already known.",
};
const char* const kProphetRisen[] = {
	"A prophet named {n} arose in {s}, saying that {g} had spoken to [her|him|them].",
	"{n} of {s} heard the voice of {g} and began to preach, [and many believed|and some threw stones].",
	"In {s} a prophet appeared, {n}, [who walked barefoot and spoke of the god|who could not be silenced], and {c} listened.",
	"{n}, a [shepherd|potter|fisher|scribe] of {s}, came down from the hills with a message from {g}.",
	"A voice cried out in the market of {s}: it was {n}, who said the god had chosen {c}.",
};
const char* const kProphetDied[] = {
	"{n} the prophet died, and {c} mourned.",
	"The prophet {n} was gathered to {g}.",
	"{n}, who had spoken for the god among {c}, [was buried with great honour|died, and the disciples quarrelled at once].",
	"{n} the prophet went into the hills and did not come back. {c} said {g} had taken the prophet home.",
};
const char* const kEdictPeace[] = {
	"{n} commanded {c} to lay down their spears.",
	"By the word of {n}, {c} swore to make no more wars [while the prophet lived|in the name of {g}].",
};
const char* const kEdictExpand[] = {
	"{n} told {c} to go out and fill the land.",
	"At the word of {n}, the young of {c} left their homes to found new ones.",
};
const char* const kEdictBuild[] = {
	"{n} commanded {c} to build a wonder worthy of {g}.",
	"{n} told {c} that {g} wished for a great work, and {c} began to quarry stone.",
};
const char* const kEdictCrusade[] = {
	"{n} called {c} to carry the word of {g} to the unbelievers, with fire if need be.",
	"{n} preached war against those who did not honour {g}, and {c} sharpened their swords.",
};
const char* const kWonder[] = {
	"{c} finished {x} at {s}, and people came from far away to see it.",
	"At {s} rose {x}, the greatest work of {c}.",
	"{x} was completed at {s}, [and it was said the god came down to look at it|and its builders were remembered for a thousand years].",
};
const char* const kNukeFirst[] = {
	"{c} let loose the fire of the sun upon {s} of {c2}. Nothing like it had been done before.",
	"A light brighter than noon rose over {s}, sent by {c} against {c2}, and the world learned a new fear.",
};
const char* const kNuke[] = {
	"Again the sun-fire fell, this time upon {s}.",
	"{c} burned {s} of {c2} with the hidden fire.",
	"The fire of {c} came down on {s}, and the sky over {c2} was black for a season.",
};
const char* const kArkStarted[] = {
	"At {s}, {c} began to build a ship to carry them to the stars.",
	"{c} laid the keel of a great ship at {s}, a ship for the space between the stars.",
};
const char* const kArkLaunched[] = {
	"The great ship of {c} rose from {s} on a pillar of fire and went out among the stars[, carrying the name of {ge}|, and did not come back].",
	"{c} left {W}. Their ship climbed from {s} into the dark, [and the god watched it go|and its light was the last star of the morning].",
};
const char* const kWarm1[] = {
	"The summers grew longer and hotter, and the old people said it had not been so in their youth.",
	"The world was growing warmer, [the wise said|said the scholars, who measured such things].",
};
const char* const kWarm2[] = {
	"The ice in the north began to weep, and the summers were cruel.",
	"The world grew warmer still, and the snows on the mountains [thinned|retreated].",
};
const char* const kWarm3[] = {
	"{W} grew hot as a kiln. [The old crops failed and new deserts grew.|The forests of the south withered.]",
	"The heat of the world became a thing every child knew. [The scholars said it was the smoke of the cities.|The priests said it was the god's fever.]",
};
const char* const kCool[] = {
	"The winters grew long, and the snow came early and left late.",
	"A cold age came upon {W}, [and the harvests were thin|and the rivers froze in their beds].",
};
const char* const kSeaRise[] = {
	"The sea crept up the shores, and the fishermen moved their houses inland.",
	"The sea was rising. [Old harbours drowned.|Every year the tide came a little higher.]",
};
const char* const kSeaFall[] = {
	"The sea drew back from the shores, and new land lay bare.",
	"The waters went down, and ships were left stranded in the mud of old harbours.",
};
const char* const kIceAge[] = {
	"Ice crept down from the poles, [and the northern towns were buried in snow|swallowing fields and forests].",
	"The great ice came south, and the world grew small and cold.",
};
const char* const kThaw[] = {
	"The great ice retreated, and the old lands came out from under it.",
	"The ice withdrew toward the poles, and green came back to the north.",
};
const char* const kSettlersLost[] = {
	"A band of settlers of {c} was lost {w}.",
	"Settlers who had left {s} were never heard from again.",
	"{c} sent out settlers who did not arrive. [Some said the wilds took them.|Their kin waited for years.]",
	"The settlers of {s} were caught {w}, and none came home to tell of it.",
};
const char* const kPeopleLost[] = {
	"And then there was no one left on {W} to remember.",
	"The last fire on {W} went out. The god was alone again.",
	"The cities of {W} were empty, and the wind read the inscriptions on their walls.",
};
const char* const kEndBeloved[] = {
	"So {c} went out among the stars, and carried the name of {ge} with them, and sang it in the dark.",
	"{c} left {W} with love, and the last thing they did was give thanks to {g}.",
};
const char* const kEndFeared[] = {
	"{c} fled from {ge} into the dark, and did not look back.",
	"{c} left {W} as a slave leaves a cruel master, [quietly and for ever|praying that the god would not follow].",
};
const char* const kEndForgotten[] = {
	"{c} left {W} without a prayer. The shrines of {g} were full of dust.",
	"{c} went to the stars, and did not think to say goodbye to the god.",
};
const char* const kEndParting[] = {
	"{c} left, and the god watched them go.",
	"{c} rose from {W} into the dark, [and the god was not sure what they had thought of {herm}|and the world was quiet behind them].",
};
const char* const kEndAsh[] = {
	"Ash fell on {W} for a hundred years, and there was no one to see it.",
	"The fires the peoples had made burned out at last, and {W} was silent.",
};
const char* const kEndQuiet[] = {
	"{W} grew quiet. Grass covered the roads.",
	"The peoples of {W} were gone, and the forests came back over their cities.",
};
const char* const kEpiFirst[] = {
	"{c} called the one who had woken them {e}.",
	"Among {c} the god was named {e}.",
	"{c} began to speak of {g} as {e}.",
	"In the first songs of {c}, the god was {e}.",
};
const char* const kEpiChange[] = {
	"{c} no longer called the god {old}; now they said {e}.",
	"In the songs of {c}, {old} became {e}.",
	"The {ca} priests gave the god a new name: {e}.",
	"Among {c} the god was no longer {old} but {e}, and the old name was forgotten.",
	"{c} had called the god {old}. After what they had seen, they called {herm} {e}.",
	"Among {c}, mothers who had once told their children of {old} now told them of {e}.",
};
// (integration verifier) the change to the silent names: nothing was seen
const char* const kEpiSilent[] = {
	"Generations passed and {c} saw nothing of {old}; they began to call the god {e}.",
	"The god did nothing that {c} could see for longer than anyone remembered, and {old} became {e}.",
	"{c} still kept the shrines, but the priests now spoke of {e}, not {old}.",
};

// ====================================================================== event handlers

using Tok = EventBus::Token;

// Largest of the given settlement ids that is valid (ties: lower id). -1 if none.
int largestOf(const World& w, const std::vector<int>& ids) {
	int best = -1;
	double bp = -1;
	for (int id : ids) {
		const Settlement* s = sett(w, id);
		if (!s) continue;
		const double p = fin(s->pop);
		if (p > bp || (p == bp && id < best)) { bp = p; best = id; }
	}
	return best;
}

void heardBy(World& w, int cid) {
	if (!cult(w, cid)) return;
	w.vars.num["chronicle_h." + std::to_string(cid)] = fin(w.year);
}

void onPhase(World& w, const EvPhase& e) {
	Ctx x;
	ctxWorld(x, w);
	switch (e.phase) {
	case Phase::Water: write(w, "genesis", 3, -1, -1, tpl(kGenWater), "genesis-water", x); break;
	case Phase::Life: write(w, "genesis", 3, -1, -1, tpl(kGenLife), "genesis-life", x); break;
	case Phase::People: write(w, "genesis", 3, -1, -1, tpl(kGenPeople), "genesis-people", x); break;
	case Phase::History: write(w, "genesis", 3, -1, -1, tpl(kGenHistory), "genesis-history", x); break;
	default: break;
	}
}

void onMeteorImpact(World& w, const EvMeteorImpact& e) {
	const ChronicleTune& T = tuneOf(w);
	Ctx x;
	ctxWorld(x, w);
	if (e.kind == ImpactKind::Comet) {
		double& count = w.vars.num["chronicle_comets"];
		count = fin(count) + 1;
		const int k = int(count);
		const int every = T.cometEvery > 0 ? T.cometEvery : 4;
		if (k == 1) write(w, "comet", 2, e.cell, -1, tpl(kCometFirst), "comet-first", x);
		else if ((k - 1) % every == 0) write(w, "comet", 1, e.cell, -1, tpl(kCometMore), "comet", x);
		return;
	}
	int sid = -1;
	sid = setWhere(x, w, e.cell, T.nearAng * 2);
	if (sid < 0) { write(w, "meteor", 2, e.cell, -1, tpl(kMeteorFar), "meteor-far", x); return; }
	const int cid = sett(w, sid)->culture;
	ctxSettlement(x, w, sid);
	ctxCulture(x, w, cid);
	if (e.sea) write(w, "meteor", 2, e.cell, cid, tpl(kMeteorSea), "meteor-sea", x);
	else write(w, "meteor", 2, e.cell, cid, tpl(kMeteor), "meteor", x);
}

void onLightning(World& w, const EvLightning& e) {
	const ChronicleTune& T = tuneOf(w);
	int sid = -1;
	Ctx x;
	sid = setWhere(x, w, e.cell, T.witnessAng);
	if (sid < 0) return;                                   // no one saw it
	const int cid = sett(w, sid)->culture;
	// the god's own bolt is the player's act: told more often than the weather's
	const char* type = e.natural ? "lightning" : "wrath-lightning";
	if (e.natural ? !minorOk(w, type, cid, T.fireGap, T.noisyGap) : !minorOk(w, type, cid, T.minorGap)) return;
	ctxSettlement(x, w, sid);
	ctxCulture(x, w, cid);
	if (e.natural) write(w, type, 1, e.cell, cid, tpl(kLightningNat), "lightning-nat", x);
	else write(w, type, 1, e.cell, cid, tpl(kLightningGod), "lightning", x);
}

void onTsunamiHit(World& w, const EvTsunamiHit& e) {
	const ChronicleTune& T = tuneOf(w);
	int sid = -1;
	Ctx x;
	sid = setWhere(x, w, e.cell, T.witnessAng);
	if (sid < 0) return;
	const int cid = sett(w, sid)->culture;
	if (cult(w, cid) && T.waveWeight > 0) {                // the people remember the sea that came for them
		double& d = w.vars.num["chronicle_d." + std::to_string(cid)];
		d = fin(d) + T.waveWeight;
	}
	if (!minorOk(w, "tsunami", cid, T.minorGap)) return;
	ctxSettlement(x, w, sid);
	ctxCulture(x, w, cid);
	write(w, "tsunami", 1, e.cell, cid, tpl(kWave), "wave", x);
}

void onVolcanoStart(World& w, const EvVolcanoStart& e) {
	const ChronicleTune& T = tuneOf(w);
	int sid = -1;
	Ctx x;
	ctxWorld(x, w);
	sid = setWhere(x, w, e.cell, T.nearAng);
	if (sid < 0) { write(w, "volcano", e.natural ? 1 : 2, e.cell, -1, tpl(kVolcanoFar), "volcano-far", x); return; }
	const int cid = sett(w, sid)->culture;
	ctxSettlement(x, w, sid);
	ctxCulture(x, w, cid);
	if (e.natural) write(w, "volcano", 2, e.cell, cid, tpl(kVolcanoNat), "volcano-nat", x);
	else write(w, "volcano", 2, e.cell, cid, tpl(kVolcanoGod), "volcano", x);
}

void onVolcanoEnd(World& w, const EvVolcanoEnd& e) {
	const ChronicleTune& T = tuneOf(w);
	int sid = -1;
	Ctx x;
	sid = setWhere(x, w, e.cell, T.nearAng);
	if (sid < 0) return;
	const int cid = sett(w, sid)->culture;
	if (!minorOk(w, "volcano-end", cid, T.minorGap)) return;
	ctxSettlement(x, w, sid);
	ctxCulture(x, w, cid);
	write(w, "volcano-end", 1, e.cell, cid, tpl(kVolcanoEnd), "volcano-end", x);
}

void onQuake(World& w, const EvQuake& e) {
	const ChronicleTune& T = tuneOf(w);
	int sid = -1;
	Ctx x;
	const double reach = std::max(T.witnessAng, fin(e.ang) + 0.1);
	sid = setWhere(x, w, e.cell, reach);
	if (sid < 0) return;                                   // nobody felt it
	const int cid = sett(w, sid)->culture;
	ctxSettlement(x, w, sid);
	ctxCulture(x, w, cid);
	if (e.natural) {
		if (!minorOk(w, "quake", cid, T.minorGap)) return;
		write(w, "quake", 1, e.cell, cid, tpl(kQuakeNat), "quake-nat", x);
	} else {
		write(w, "quake", 2, e.cell, cid, tpl(kQuakeGod), "quake", x);
	}
}

void onFireStart(World& w, const EvFireStart& e) {
	const ChronicleTune& T = tuneOf(w);
	int sid = -1;
	Ctx x;
	sid = setWhere(x, w, e.cell, T.witnessAng);
	if (sid < 0) return;
	const int cid = sett(w, sid)->culture;
	if (!minorOk(w, "fire", cid, T.fireGap, T.noisyGap)) return;
	ctxSettlement(x, w, sid);
	ctxCulture(x, w, cid);
	string noun = biomeWord(w, e.cell).noun;
	if (noun == "the open sea" || noun == "the shallows" || noun == "the frozen sea" || noun == "the ice" || noun == "the shore") noun = "the grass";
	x.set("x", noun);
	write(w, "fire", 1, e.cell, cid, tpl(kFire), "fire", x);
}

void onStorm(World& w, const EvStorm& e) {
	if (!e.start) return;
	const ChronicleTune& T = tuneOf(w);
	double a = 0;
	const int sid = nearestLive(w, e.cell, a);
	if (sid < 0 || a > fin(e.ang) + 0.05) return;
	const int cid = sett(w, sid)->culture;
	if (!minorOk(w, "storm", cid, T.fireGap, T.noisyGap)) return;
	Ctx x;
	ctxSettlement(x, w, sid);
	ctxCulture(x, w, cid);
	write(w, "storm", 1, e.cell, cid, tpl(kStorm), "storm", x);
}

void onDrought(World& w, const EvDrought& e) {
	const ChronicleTune& T = tuneOf(w);
	double a = 0;
	const int sid = nearestLive(w, e.cell, a);
	if (sid < 0 || a > fin(e.ang) + 0.02) return;
	const int cid = sett(w, sid)->culture;
	const char* type = e.start ? "drought" : "drought-end";
	if (!minorOk(w, type, cid, T.minorGap)) return;
	Ctx x;
	ctxSettlement(x, w, sid);
	ctxCulture(x, w, cid);
	if (e.start) write(w, type, 1, e.cell, cid, tpl(kDroughtStart), "drought", x);
	else write(w, type, 1, e.cell, cid, tpl(kDroughtEnd), "drought-end", x);
}

void onFounded(World& w, const EvSettlementFounded& e) {
	const Settlement* s = sett(w, e.id);
	if (!s) return;
	const ChronicleTune& T = tuneOf(w);
	const int cid = e.culture >= 0 ? e.culture : s->culture;
	Ctx x;
	ctxWorld(x, w);
	ctxSettlement(x, w, e.id);
	ctxCulture(x, w, cid);
	const int cell = w.valid(e.cell) ? e.cell : s->cell;
	if (e.by == FoundedBy::Awaken) {
		// the first awakening of the world, or a later one
		bool earlier = false;
		for (const Settlement& o : w.settlements) if (o.id != e.id && o.founded <= s->founded && o.id < e.id) { earlier = true; break; }
		if (!earlier) write(w, "awakening", 3, cell, cid, tpl(kAwakening), "awakening", x);
		else write(w, "awakening", 3, cell, cid, tpl(kAwakeningAgain), "awakening-again", x);
		return;
	}
	if (!minorOk(w, "founded", cid, T.minorGap * 0.6)) return;
	double a = 0;
	const int from = nearestLive(w, cell, a, cid, e.id);
	if (from >= 0) {
		ctxSettlement(x, w, from, "2");
		write(w, "founded", 1, cell, cid, tpl(kFounded), "founded", x);
	} else {
		write(w, "founded", 1, cell, cid, tpl(kFoundedLone), "founded-lone", x);
	}
}

void onTier(World& w, const EvSettlementTier& e) {
	const Settlement* s = sett(w, e.id);
	if (!s) return;
	const ChronicleTune& T = tuneOf(w);
	const int cid = s->culture;
	Ctx x;
	ctxWorld(x, w);
	ctxSettlement(x, w, e.id);
	x.set("st", tierWord(e.tier));
	x.set("sn", string("the ") + tierWord(e.prev));
	ctxCulture(x, w, cid);
	if (e.tier < e.prev) {
		if (!minorOk(w, "decline", cid, T.minorGap)) return;
		write(w, "decline", 1, s->cell, cid, tpl(kTierDown), "tier-down", x);
		return;
	}
	switch (e.tier) {
	case 1:
		if (!minorOk(w, "tier", cid, T.tierGap, T.tierGap * 0.5)) return;
		write(w, "tier", 1, s->cell, cid, tpl(kTierVillage), "tier-village", x);
		break;
	case 2:
		if (!minorOk(w, "tier", cid, T.tierGap, T.tierGap * 0.5)) return;
		write(w, "tier", 1, s->cell, cid, tpl(kTierTown), "tier-town", x);
		break;
	case 3: {
		bool first = true;
		for (const Settlement& o : w.settlements) if (o.id != e.id && o.tier >= 3) { first = false; break; }
		if (first) { write(w, "tier", 3, s->cell, cid, tpl(kTierFirstCity), "tier-first-city", x); break; }
		// (integration verifier) every later city was notable and unlimited: when an age raises the
		// gate twenty towns become cities within a few years. A culture's first city always; then one
		// per bigTierGap years.
		bool cultureFirst = true;
		for (const Settlement& o : w.settlements) if (o.id != e.id && o.culture == cid && !o.dead && o.tier >= 3) { cultureFirst = false; break; }
		if (!cultureFirst && recentNotable(w, "tier", cid, T.bigTierGap)) break;
		write(w, "tier", 2, s->cell, cid, tpl(kTierCity), "tier-city", x);
		break;
	}
	case 4: {
		bool cultureFirst = true;
		for (const Settlement& o : w.settlements) if (o.id != e.id && o.culture == cid && !o.dead && o.tier >= 4) { cultureFirst = false; break; }
		if (!cultureFirst && recentNotable(w, "tier", cid, T.bigTierGap)) break;
		write(w, "tier", 2, s->cell, cid, tpl(kTierMetro), "tier-metro", x);
		break;
	}
	default: break;
	}
}

void onBuilding(World& w, const EvSettlementBuilding& e) {
	const Settlement* s = sett(w, e.id);
	if (!s || e.building == Building::WONDER) return;     // wonder:built tells that story
	if (int(e.building) < 0 || int(e.building) >= Building::COUNT) return;   // Settlement::has would index out of range
	const ChronicleTune& T = tuneOf(w);
	const int cid = s->culture;
	bool first = true;
	for (const Settlement& o : w.settlements)
		if (o.id != e.id && o.culture == cid && o.has(e.building)) { first = false; break; }
	Tpl t{ nullptr, 0 };
	string key;
	switch (e.building) {
	case Building::SHRINE: t = first ? tpl(kBShrineFirst) : tpl(kBShrine); key = first ? "b-shrine-first" : "b-shrine"; break;
	case Building::TEMPLE: t = tpl(kBTemple); key = "b-temple"; break;
	case Building::GRANARY: t = tpl(kBGranary); key = "b-granary"; break;
	case Building::WALLS: t = tpl(kBWalls); key = "b-walls"; break;
	case Building::PORT: t = tpl(kBPort); key = "b-port"; break;
	case Building::MARKET: t = tpl(kBMarket); key = "b-market"; break;
	case Building::MINE: t = tpl(kBMine); key = "b-mine"; break;
	case Building::FACTORY: t = tpl(kBFactory); key = "b-factory"; break;
	case Building::ACADEMY: t = tpl(kBAcademy); key = "b-academy"; break;
	case Building::PLANT: t = tpl(kBPlant); key = "b-plant"; break;
	case Building::LAUNCH: t = tpl(kBLaunch); key = "b-launch"; break;
	default: return;
	}
	const bool big = e.building == Building::LAUNCH || first;
	if (!big && !minorOk(w, "building", cid, T.buildingGap, T.buildingGap * 0.5)) return;
	Ctx x;
	ctxSettlement(x, w, e.id);
	ctxCulture(x, w, cid);
	// the first of a kind among a people: "... It was the first academy of the Umari."
	const bool sfx = first && e.building != Building::SHRINE && e.building != Building::LAUNCH;
	x.set("bn", kBuildingNoun[e.building][0]);
	x.set("bna", kBuildingNoun[e.building][1]);
	writeComposed(w, "building", big ? 2 : 1, s->cell, cid, t, key, sfx ? tpl(kBuildFirstSfx) : Tpl{ nullptr, 0 }, "b-first-sfx", x);
}

void onFamine(World& w, const EvSettlementFamine& e) {
	const Settlement* s = sett(w, e.id);
	if (!s) return;
	const ChronicleTune& T = tuneOf(w);
	const int cid = s->culture;
	const bool big = s->tier >= 3;
	// (integration verifier) a city famine is notable, but a volcanic or nuclear winter starves every
	// city at once (25 entries in one year): after one city of a culture is told, its other cities'
	// famines wait famineGap years
	if (!big && !minorOk(w, "famine", cid, T.minorGap)) return;
	if (big && recentNotable(w, "famine", cid, T.famineGap, s->cell)) return;
	Ctx x;
	ctxWorld(x, w);
	ctxSettlement(x, w, e.id);
	ctxCulture(x, w, cid);
	if (fin(w.dust, 0) >= T.dustFamine) write(w, "famine", big ? 2 : 1, s->cell, cid, tpl(kFamineDust), "famine-dust", x);
	else write(w, "famine", big ? 2 : 1, s->cell, cid, tpl(kFamine), "famine", x);
}

void onPlague(World& w, const EvSettlementPlague& e) {
	if (!e.start) return;                                  // a passing sickness is not news; Heal tells its own story
	const Settlement* s = sett(w, e.id);
	if (!s) return;
	const ChronicleTune& T = tuneOf(w);
	const int cid = s->culture;
	if (!minorOk(w, "plague", cid, T.minorGap)) return;
	Ctx x;
	ctxSettlement(x, w, e.id);
	ctxCulture(x, w, cid);
	write(w, "plague", s->tier >= 3 ? 2 : 1, s->cell, cid, tpl(kPlague), "plague", x);
}

void onDestroyed(World& w, const EvSettlementDestroyed& e) {
	const Settlement* s = sett(w, e.id);
	if (!s) return;
	const ChronicleTune& T = tuneOf(w);
	const int cid = e.culture >= 0 ? e.culture : s->culture;
	if (e.cause == Cause::Drowned || e.cause == Cause::Wave) {
		if (cult(w, cid)) {
			double& d = w.vars.num["chronicle_d." + std::to_string(cid)];
			d = fin(d) + 1;
		}
	}
	Tpl t{ nullptr, 0 };
	string key;
	switch (e.cause) {
	case Cause::Drowned: t = tpl(kDDrowned); key = "d-drowned"; break;
	case Cause::Meteor: t = tpl(kDMeteor); key = "d-meteor"; break;
	case Cause::Lava: t = tpl(kDLava); key = "d-lava"; break;
	case Cause::War: t = tpl(kDWar); key = "d-war"; break;
	case Cause::Famine: t = tpl(kDFamine); key = "d-famine"; break;
	case Cause::Plague: t = tpl(kDPlague); key = "d-plague"; break;
	case Cause::Quake: t = tpl(kDQuake); key = "d-quake"; break;
	case Cause::Nuke: t = tpl(kDNuke); key = "d-nuke"; break;
	case Cause::Wave: t = tpl(kDWave); key = "d-wave"; break;
	case Cause::Lightning: t = tpl(kDLightning); key = "d-lightning"; break;
	default: t = tpl(kDAbandoned); key = "d-abandoned"; break;
	}
	const bool quiet = e.cause == Cause::Abandoned || e.cause == Cause::None || e.cause == Cause::Famine;
	int weight = s->tier >= 3 ? 3 : (quiet && s->tier <= 1 ? 1 : 2);
	if (weight == 1 && !minorOk(w, "destroyed", cid, T.minorGap)) return;
	Ctx x;
	ctxSettlement(x, w, e.id);
	ctxCulture(x, w, cid);
	write(w, "destroyed", weight, w.valid(e.cell) ? e.cell : s->cell, cid, t, key, x);
}

void onCaptured(World& w, const EvSettlementCaptured& e) {
	const Settlement* s = sett(w, e.id);
	if (!s) return;
	const ChronicleTune& T = tuneOf(w);
	Ctx x;
	ctxSettlement(x, w, e.id);
	ctxCulture(x, w, e.from);
	ctxCulture(x, w, e.to, "2");
	if (e.how == CaptureHow::War) {
		write(w, "captured", 2, s->cell, e.to, tpl(kCapturedWar), "captured", x);
		return;
	}
	const ChronicleScratch& S = w.scratch<ChronicleScratch>();
	if (S.bornTick == w.tick && S.bornYear == fin(w.year) && std::find(S.bornCultures.begin(), S.bornCultures.end(), e.to) != S.bornCultures.end()) return;
	if (!minorOk(w, "schism", e.to, T.minorGap)) return;
	write(w, "schism", 1, s->cell, e.to, tpl(kCapturedSchism), "captured-schism", x);
}

const char* wantOf(PrayerKind k) {
	switch (k) {
	case PrayerKind::Rain: return "rain";
	case PrayerKind::Heal: return "healing";
	case PrayerKind::Peace: return "peace";
	case PrayerKind::Bloom: return "green fields";
	}
	return "mercy";
}

void onPrayer(World& w, const EvPrayer& e) {
	const Settlement* s = sett(w, e.id);
	if (!s) return;
	const ChronicleTune& T = tuneOf(w);
	ChronicleScratch& S = w.scratch<ChronicleScratch>();
	const int cid = s->culture;
	Ctx x;
	ctxSettlement(x, w, e.id);
	ctxCulture(x, w, cid);
	x.set("want", wantOf(e.kind));
	if (e.start) {
		S.prayerSince[e.id] = fin(w.year);
		if (!minorOk(w, "prayer", cid, T.minorGap)) return;
		write(w, "prayer", 1, s->cell, cid, tpl(kPrayer), "prayer", x);
		return;
	}
	const auto it = S.prayerSince.find(e.id);
	if (it == S.prayerSince.end()) return;
	const double since = it->second;
	S.prayerSince.erase(it);
	const double limit = fin(w.tune<SocietyTune>().prayerYears, 15);
	if (fin(w.year) - since < limit - 0.5) return;         // the cause went away: nothing to tell
	if (!minorOk(w, "prayer-unanswered", cid, T.minorGap)) return;
	write(w, "prayer-unanswered", 1, s->cell, cid, tpl(kPrayerLost), "prayer-lost", x);
}

void onPrayerAnswered(World& w, const EvPrayerAnswered& e) {
	const Settlement* s = sett(w, e.id);
	if (!s) return;
	ChronicleScratch& S = w.scratch<ChronicleScratch>();
	S.prayerSince.erase(e.id);
	if (S.answeredTick != w.tick || S.answeredYear != fin(w.year)) { S.answeredTick = w.tick; S.answeredYear = fin(w.year); S.answered.clear(); }
	S.answered.push_back(e.id);
	const int cid = s->culture;
	heardBy(w, cid);
	Ctx x;
	ctxSettlement(x, w, e.id);
	ctxCulture(x, w, cid);
	// (integration verifier) a kind god answering every year made hundreds of notable entries: one
	// notable answer per culture per minorGap years, the others still written but minor
	const int wgt = recentNotable(w, "prayer-answered", cid, tuneOf(w).minorGap) ? 1 : 2;
	switch (e.kind) {
	case PrayerKind::Rain: write(w, "prayer-answered", wgt, s->cell, cid, tpl(kAnsRain), "ans-rain", x); break;
	case PrayerKind::Heal: write(w, "prayer-answered", wgt, s->cell, cid, tpl(kAnsHeal), "ans-heal", x); break;
	case PrayerKind::Peace: write(w, "prayer-answered", wgt, s->cell, cid, tpl(kAnsPeace), "ans-peace", x); break;
	case PrayerKind::Bloom: write(w, "prayer-answered", wgt, s->cell, cid, tpl(kAnsBloom), "ans-bloom", x); break;
	}
}

void onCultureBorn(World& w, const EvCultureBorn& e) {
	ChronicleScratch& S = w.scratch<ChronicleScratch>();
	if (S.bornTick != w.tick || S.bornYear != fin(w.year)) { S.bornTick = w.tick; S.bornYear = fin(w.year); S.bornCultures.clear(); }
	S.bornCultures.push_back(e.id);
	const Culture* c = cult(w, e.id);
	if (!c) return;
	// the culture remembers silence from its birth
	w.vars.num["chronicle_h." + std::to_string(e.id)] = fin(w.year);
	if (e.reason == BirthReason::Awaken) return;          // settlement:founded (awaken) tells the story with its camp
	double a = 0;
	int sid = nearestLive(w, e.cell, a);
	Ctx x;
	ctxWorld(x, w);
	ctxCulture(x, w, e.id);
	const int parent = e.parent >= 0 ? e.parent : c->parent;
	ctxCulture(x, w, parent, "2");
	if (sid < 0) sid = -1;
	ctxSettlement(x, w, sid);
	if (sid < 0) x.set("s", "the borderlands");
	if (e.reason == BirthReason::Distance) write(w, "culture-born", 3, e.cell, e.id, tpl(kCultureDistance), "born-distance", x);
	else write(w, "culture-born", 3, e.cell, e.id, tpl(kCultureSchism), "born-schism", x);
}

void onCultureDied(World& w, const EvCultureDied& e) {
	const Culture* c = cult(w, e.id);
	if (!c) return;
	Ctx x;
	ctxCulture(x, w, e.id);
	int sid = c->capital;
	if (!sett(w, sid)) {
		sid = -1;
		double bp = -1;
		for (const Settlement& s : w.settlements) if (s.culture == e.id && fin(s.pop) > bp) { bp = fin(s.pop); sid = s.id; }
	}
	Tpl t = tpl(kCultureDied);
	if (sid >= 0) ctxSettlement(x, w, sid);
	else x.set("s", "their old lands");
	write(w, "culture-died", 3, -1, e.id, t, "culture-died", x);
	// its epithet memory is no longer needed
	const string id = std::to_string(e.id);
	for (auto it = w.vars.num.begin(); it != w.vars.num.end();) {
		const string& k = it->first;
		if (startsWith(k, "chronicle_") && k.size() > id.size() + 1) {
			const size_t dot = k.find('.');
			if (dot != string::npos) {
				const size_t end = k.find('.', dot + 1);
				const string who = k.substr(dot + 1, end == string::npos ? string::npos : end - dot - 1);
				if (who == id) { it = w.vars.num.erase(it); continue; }
			}
		}
		++it;
	}
}

void onCultureAge(World& w, const EvCultureAge& e) {
	const Culture* c = cult(w, e.id);
	if (!c || e.age <= e.prev || e.age < 1 || e.age >= Age::COUNT) return;
	bool first = true;
	for (const Culture& o : w.cultures) if (o.id != e.id && o.age >= e.age) { first = false; break; }
	Ctx x;
	ctxWorld(x, w);
	ctxCulture(x, w, e.id);
	int sid = c->capital;
	if (!sett(w, sid)) { sid = -1; for (const Settlement& s : w.settlements) if (!s.dead && s.culture == e.id) { sid = s.id; break; } }
	ctxSettlement(x, w, sid);
	if (sid < 0) x.set("s", "their towns");
	Tpl t = tpl(kAgeBronze);
	switch (e.age) {
	case Age::BRONZE: t = tpl(kAgeBronze); break;
	case Age::IRON: t = tpl(kAgeIron); break;
	case Age::SAIL: t = tpl(kAgeSail); break;
	case Age::INDUSTRY: t = tpl(kAgeIndustry); break;
	case Age::ATOMIC: t = tpl(kAgeAtomic); break;
	default: t = tpl(kAgeSpace); break;
	}
	const string key = string("age-") + std::to_string(e.age);
	writeComposed(w, "age", first ? 3 : 2, sett(w, sid) ? sett(w, sid)->cell : -1, e.id, t, key,
		first ? tpl(kAgeFirstSfx) : Tpl{ nullptr, 0 }, "age-first-sfx", x);
}

void onWarDeclared(World& w, const EvWarDeclared& e) {
	if (!cult(w, e.a) || !cult(w, e.b)) return;
	Ctx x;
	ctxCulture(x, w, e.a);
	ctxCulture(x, w, e.b, "2");
	write(w, "war", 2, -1, e.a, tpl(kWarDeclared), "war", x);
}

void onWarEnded(World& w, const EvWarEnded& e) {
	if (!cult(w, e.a) || !cult(w, e.b)) return;
	Ctx x;
	if (e.victor == e.a || e.victor == e.b) {
		const int loser = e.victor == e.a ? e.b : e.a;
		ctxCulture(x, w, e.victor);
		ctxCulture(x, w, loser, "2");
		const Culture* v = cult(w, e.victor);
		ctxSettlement(x, w, v->capital);
		if (!sett(w, v->capital)) x.set("s", "their cities");
		write(w, "peace", 2, -1, e.victor, tpl(kWarWon), "war-won", x);
	} else {
		ctxCulture(x, w, e.a);
		ctxCulture(x, w, e.b, "2");
		write(w, "peace", 2, -1, e.a, tpl(kWarDraw), "war-draw", x);
	}
}

void onBattle(World& w, const EvBattle& e) {
	if (e.won) return;                                    // a won battle is told as the capture or the razing
	const Settlement* s = sett(w, e.settlement);
	if (!s || !cult(w, e.attacker)) return;
	const ChronicleTune& T = tuneOf(w);
	if (!minorOk(w, "battle", e.attacker, T.minorGap)) return;
	Ctx x;
	ctxSettlement(x, w, e.settlement);
	ctxCulture(x, w, e.attacker);
	ctxCulture(x, w, e.defender >= 0 ? e.defender : s->culture, "2");
	write(w, "battle", 1, w.valid(e.cell) ? e.cell : s->cell, e.attacker, tpl(kBattleLost), "battle", x);
}

void onTradeRoute(World& w, const EvTradeRoute& e) {
	const Settlement* a = sett(w, e.a);
	const Settlement* b = sett(w, e.b);
	if (!a || !b) return;
	const ChronicleTune& T = tuneOf(w);
	Ctx x;
	ctxSettlement(x, w, e.a);
	ctxSettlement(x, w, e.b, "2");
	ctxCulture(x, w, a->culture);
	ctxCulture(x, w, b->culture, "2");
	if (a->culture != b->culture) {
		// (integration verifier) trade:route is per settlement pair, so two trading peoples produced
		// dozens of notable "first caravan" / "saw their first merchants" entries. The first route
		// between two peoples is notable (kept in vars.num, so it survives a save); later ones are
		// minor, rate-limited, and told without "first".
		const int lo = std::min(a->culture, b->culture), hi = std::max(a->culture, b->culture);
		const string key = "chronicle_tp." + std::to_string(lo) + "." + std::to_string(hi);
		if (!w.vars.num.count(key)) {
			w.vars.num[key] = fin(w.year);
			write(w, "trade", 2, a->cell, a->culture, tpl(kTradeForeign), "trade-foreign", x);
			return;
		}
		if (!minorOk(w, "trade", a->culture, T.minorGap * 2)) return;
		write(w, "trade", 1, a->cell, a->culture, tpl(kTradeForeignMore), "trade-foreign-more", x);
		return;
	}
	if (!minorOk(w, "trade", a->culture, T.minorGap * 2)) return;
	write(w, "trade", 1, a->cell, a->culture, tpl(kTrade), "trade", x);
}

void onProphetRisen(World& w, const EvProphetRisen& e) {
	if (!cult(w, e.culture)) return;
	Ctx x;
	ctxCulture(x, w, e.culture);
	ctxSettlement(x, w, e.settlement);
	if (!sett(w, e.settlement)) x.set("s", "the wilderness");
	x.set("n", e.name.empty() ? string("a nameless wanderer") : e.name);
	const Settlement* s = sett(w, e.settlement);
	write(w, "prophet", 2, s ? s->cell : -1, e.culture, tpl(kProphetRisen), "prophet", x);
}

void onProphetDied(World& w, const EvProphetDied& e) {
	if (!cult(w, e.culture)) return;
	Ctx x;
	ctxCulture(x, w, e.culture);
	x.set("n", e.name.empty() ? string("The nameless prophet") : e.name);
	write(w, "prophet-died", 2, -1, e.culture, tpl(kProphetDied), "prophet-died", x);
}

void onEdict(World& w, const EvEdict& e) {
	const Culture* c = cult(w, e.culture);
	if (!c || e.edict == Edict::None) return;
	Ctx x;
	ctxCulture(x, w, e.culture);
	x.set("n", c->prophet && !c->prophet->name.empty() ? c->prophet->name : string("the prophet"));
	Tpl t = tpl(kEdictPeace);
	string key = "edict-peace";
	switch (e.edict) {
	case Edict::Expand: t = tpl(kEdictExpand); key = "edict-expand"; break;
	case Edict::Build: t = tpl(kEdictBuild); key = "edict-build"; break;
	case Edict::Crusade: t = tpl(kEdictCrusade); key = "edict-crusade"; break;
	default: break;
	}
	const Settlement* s = c->prophet ? sett(w, c->prophet->settlement) : nullptr;
	write(w, "edict", 2, s ? s->cell : -1, e.culture, t, key, x);
}

void onWonder(World& w, const EvWonderBuilt& e) {
	const Settlement* s = sett(w, e.settlement);
	if (!s) return;
	Ctx x;
	ctxSettlement(x, w, e.settlement);
	ctxCulture(x, w, s->culture);
	x.set("x", e.name.empty() ? string("a great wonder") : e.name);
	write(w, "wonder", 3, s->cell, s->culture, tpl(kWonder), "wonder", x);
}

void onNuke(World& w, const EvNuke& e) {
	if (!cult(w, e.from)) return;
	double& count = w.vars.num["chronicle_nukes"];
	count = fin(count) + 1;
	// the struck place: the settlement at the cell (alive or just destroyed), else the nearest
	int sid = -1;
	for (const Settlement& s : w.settlements) if (s.cell == e.cell && s.culture == e.to) { sid = s.id; }
	if (sid < 0) for (const Settlement& s : w.settlements) if (s.cell == e.cell) { sid = s.id; break; }
	if (sid < 0) { double a = 0; sid = nearestLive(w, e.cell, a); }
	Ctx x;
	ctxCulture(x, w, e.from);
	ctxCulture(x, w, e.to, "2");
	ctxSettlement(x, w, sid);
	if (sid < 0) x.set("s", "the cities");
	if (count < 1.5) write(w, "nuke", 3, e.cell, e.from, tpl(kNukeFirst), "nuke-first", x);
	else write(w, "nuke", 2, e.cell, e.from, tpl(kNuke), "nuke", x);
}

void onArkStarted(World& w, const EvArkStarted& e) {
	if (!cult(w, e.culture)) return;
	Ctx x;
	ctxCulture(x, w, e.culture);
	ctxSettlement(x, w, e.settlement);
	const Settlement* s = sett(w, e.settlement);
	if (!s) x.set("s", "their capital");
	write(w, "ark", 3, s ? s->cell : -1, e.culture, tpl(kArkStarted), "ark-started", x);
}

void onArkLaunched(World& w, const EvArkLaunched& e) {
	if (!cult(w, e.culture)) return;
	Ctx x;
	ctxWorld(x, w);
	ctxCulture(x, w, e.culture);
	ctxSettlement(x, w, e.settlement);
	const Settlement* s = sett(w, e.settlement);
	if (!s) x.set("s", "their capital");
	write(w, "ark", 3, s ? s->cell : -1, e.culture, tpl(kArkLaunched), "ark-launched", x);
}

void onClimate(World& w, const EvClimateMilestone& e) {
	Ctx x;
	ctxWorld(x, w);
	switch (e.kind) {
	case MilestoneKind::Warming:
		if (fin(e.value) >= 2.5) write(w, "climate", 2, -1, -1, tpl(kWarm3), "warm3", x);
		else if (fin(e.value) >= 1.5) write(w, "climate", 2, -1, -1, tpl(kWarm2), "warm2", x);
		else write(w, "climate", 2, -1, -1, tpl(kWarm1), "warm1", x);
		break;
	case MilestoneKind::Cooling: write(w, "climate", 2, -1, -1, tpl(kCool), "cool", x); break;
	case MilestoneKind::SeaRise: write(w, "climate", 2, -1, -1, tpl(kSeaRise), "sea-rise", x); break;
	case MilestoneKind::SeaFall: write(w, "climate", 2, -1, -1, tpl(kSeaFall), "sea-fall", x); break;
	case MilestoneKind::IceAge: write(w, "climate", 2, -1, -1, tpl(kIceAge), "ice-age", x); break;
	case MilestoneKind::Thaw: write(w, "climate", 2, -1, -1, tpl(kThaw), "thaw", x); break;
	}
}

void onUnitDied(World& w, const EvUnitDied& e) {
	if (e.type != UnitType::Settler) return;              // armies are told as battles, prophets by prophet:died
	if (e.cause.empty() || e.cause == "disbanded" || e.cause == "arrived") return;
	if (!cult(w, e.culture)) return;
	const ChronicleTune& T = tuneOf(w);
	if (!minorOk(w, "settlers-lost", e.culture, T.minorGap)) return;
	Ctx x;
	ctxCulture(x, w, e.culture);
	int sid = -1;
	sid = setWhere(x, w, e.cell, T.nearAng);
	double a = 0;
	const int home = nearestLive(w, e.cell, a, e.culture);
	ctxSettlement(x, w, home);
	if (home < 0) x.set("s", "their homes");
	write(w, "settlers-lost", 1, e.cell, e.culture, tpl(kSettlersLost), "settlers-lost", x);
}

void onPeopleLost(World& w, const EvPeopleLost&) {
	Ctx x;
	ctxWorld(x, w);
	write(w, "lost", 3, -1, -1, tpl(kPeopleLost), "people-lost", x);
}

void onEnding(World& w, const EvEnding& e) {
	Ctx x;
	ctxWorld(x, w);
	ctxCulture(x, w, e.culture);
	if (!cult(w, e.culture)) x.set("c", "the last people");
	Tpl t = tpl(kEndQuiet);
	string key = "end-quiet";
	switch (e.mood) {
	case Mood::Beloved: t = tpl(kEndBeloved); key = "end-beloved"; break;
	case Mood::Feared: t = tpl(kEndFeared); key = "end-feared"; break;
	case Mood::Forgotten: t = tpl(kEndForgotten); key = "end-forgotten"; break;
	case Mood::Parting: t = tpl(kEndParting); key = "end-parting"; break;
	case Mood::Ash: t = tpl(kEndAsh); key = "end-ash"; break;
	case Mood::Quiet: t = tpl(kEndQuiet); key = "end-quiet"; break;
	}
	write(w, "ending", 3, -1, cult(w, e.culture) ? e.culture : -1, t, key, x);
}

void onMiracle(World& w, const EvMiracleWitnessed& e) {
	if (e.ids.empty()) return;                             // the chronicle is what the people saw
	const ChronicleTune& T = tuneOf(w);
	ChronicleScratch& S = w.scratch<ChronicleScratch>();
	// every culture that saw it heard from its god
	for (int id : e.ids) if (const Settlement* s = sett(w, id)) heardBy(w, s->culture);
	// an answered prayer was already told
	if (S.answeredTick == w.tick && S.answeredYear == fin(w.year) && !S.answered.empty()) {
		bool told = false;
		for (int id : e.ids) if (std::find(S.answered.begin(), S.answered.end(), id) != S.answered.end()) told = true;
		S.answered.clear();                                // consumed by the witness call that answered it
		if (told) return;
	}
	const int sid = largestOf(w, e.ids);
	const Settlement* s = sett(w, sid);
	if (!s) return;
	const int cid = s->culture;
	Ctx x;
	ctxSettlement(x, w, sid);
	ctxCulture(x, w, cid);
	setWhere(x, w, e.cell, T.nearAng);
	const string& k = e.kind;
	const double good = fin(e.good);
	if (k == "rain") {
		if (!minorOk(w, "miracle-rain", cid, T.brushGap, T.brushGap * 0.35)) return;
		if (good >= 0.5) write(w, "miracle-rain", 1, e.cell, cid, tpl(kRainThirst), "rain-thirst", x);
		else write(w, "miracle-rain", 1, e.cell, cid, tpl(kRainSoft), "rain-soft", x);
	} else if (k == "bloom") {
		if (!minorOk(w, "miracle-bloom", cid, T.brushGap, T.brushGap * 0.35)) return;
		write(w, "miracle-bloom", 1, e.cell, cid, tpl(kBloom), "bloom", x);
	} else if (k == "raise" || k == "lower") {
		if (!minorOk(w, "miracle-shape", cid, T.brushGap, T.brushGap * 0.35)) return;
		if (good < 0) write(w, "miracle-shape", 1, e.cell, cid, tpl(kShapeHarm), "shape-harm", x);
		else if (k == "raise") write(w, "miracle-shape", 1, e.cell, cid, tpl(kRaise), "raise", x);
		else write(w, "miracle-shape", 1, e.cell, cid, tpl(kLower), "lower", x);
	} else if (k == "heal") {
		if (!minorOk(w, "miracle-heal", cid, 2)) return;
		write(w, "miracle-heal", 2, e.cell, cid, tpl(kHeal), "heal", x);
	} else if (k == "inspire") {
		if (!minorOk(w, "miracle-inspire", cid, 2)) return;
		write(w, "miracle-inspire", 2, e.cell, cid, tpl(kInspire), "inspire", x);
	} else if (k == "peace") {
		write(w, "miracle-peace", 2, e.cell, cid, tpl(kOmenPeace), "omen-peace", x);
	} else if (k == "plague" && good < 0) {
		if (!minorOk(w, "wrath-plague", cid, 2)) return;
		write(w, "wrath-plague", 2, e.cell, cid, tpl(kPlagueSent), "plague-sent", x);
	}
	// lightning, quake, meteor, volcano: told by their own events; prophet: by prophet:risen; nuke: by nuke
}

template <class T> void sub(World& w, ChronicleScratch& S, void (*fn)(World&, const T&)) {
	World* wp = &w;
	S.tokens.push_back(w.bus.on<T>([wp, fn](const T& e) { fn(*wp, e); }));
}

// ====================================================================== epithets

enum Cat : int {
	CatWaker = 0, CatAncestral, CatSilent, CatMixed,
	CatRain, CatGreen, CatHealer, CatLamp, CatPeace, CatVoice, CatShaper,
	CatDrowner, CatThunder, CatShaker, CatFever, CatStar, CatFire,
	CatBeloved, CatDreaded,          // from society::regard when no deed stands out
	CatCount
};
const char* const kEpithets[CatCount][4] = {
	{ "the Waker", "{Who} Woke Us", "the First Voice", "the Dawn-Caller" },
	{ "the God of Our Fathers", "the Old God", "the Remembered One", "the Elder" },
	{ "the Silent One", "the Sleeper", "{Who} Does Not Answer", "the Far-Away" },
	{ "the Giver and Taker", "the Two-Handed", "the Storm and the Harvest", "the Double-Hearted" },
	{ "the Rain-Giver", "the Cloud-Shepherd", "{Who} Brings the Rain", "the Wet Wind" },
	{ "the Green Hand", "the Gardener", "{Who} Wakes the Seed", "the Sower" },
	{ "the Healer", "the Cool Hand", "{Who} Breaks Fevers", "the Mender" },
	{ "the Lamp-Bearer", "the Teacher", "{Who} Kindles Minds", "the Whisperer" },
	{ "the Peacemaker", "the Oath-Binder", "{Who} Stills Spears", "the Quiet Voice" },
	{ "the Voice", "the Caller of Prophets", "{Who} Speaks Through Us", "the Mouth of Heaven" },
	{ "the Shaper", "the Potter", "{Who} Lifts the Hills", "the Mountain-Maker" },
	{ "the Drowner", "the Sea-Bringer", "{Who} Swallows Cities", "the Deep" },
	{ "the Thunderer", "the Spear of Heaven", "{Who} Strikes the Oaks", "the Bright Spear" },
	{ "the Earth-Shaker", "the Breaker of Hills", "{Who} Walks Beneath", "the Restless Ground" },
	{ "the Bringer of Fevers", "the Grey Breath", "{Who} Sends the Sickness", "the Withering" },
	{ "{Who} Burns the Sky", "the Hammer", "the Star-Thrower", "the Falling Fire" },
	{ "the Smith Below", "the Fire-Mountain", "{Who} Opens the Mountains", "the Ash-Maker" },
	{ "the Beloved", "the Kind One", "{Who} Keeps the Hearth", "the Shepherd" },
	{ "the Dreaded", "the Jealous One", "{Who} Must Be Appeased", "the Terrible" },
};

// What a culture remembers: kinds of culture.seen plus "drown" (settlements lost to the sea).
const char* const kKinds[] = { "rain", "bloom", "heal", "inspire", "peace", "prophet", "raise", "lower",
	"lightning", "quake", "plague", "meteor", "volcano", "awaken", "drown" };
constexpr int kKindCount = int(sizeof(kKinds) / sizeof(kKinds[0]));
enum Kind : int { KRain, KBloom, KHeal, KInspire, KPeace, KProphet, KRaise, KLower, KLightning, KQuake, KPlague, KMeteor, KVolcano, KAwaken, KDrown };

struct Memory {
	double m[kKindCount] = {};
	double cum[kKindCount] = {};
	double heard = 0;
	double since = -1e9;             // year the current category was taken
	int cat = -1;
};

double numOr(const World& w, const string& key, double d) {
	const auto it = w.vars.num.find(key);
	return (it == w.vars.num.end() || !std::isfinite(it->second)) ? d : it->second;
}

double cumulative(const World& w, const Culture& c, int k) {
	if (k == KDrown) return std::max(0.0, numOr(w, "chronicle_d." + std::to_string(c.id), 0)) * fin(tuneOf(w).drownWeight, 0.3);
	const auto it = c.seen.find(kKinds[k]);
	return it == c.seen.end() ? 0.0 : std::max(0.0, fin(it->second));
}

// The memory as of now, without writing (epithet() is read-only).
Memory project(const World& w, const Culture& c) {
	const ChronicleTune& T = tuneOf(w);
	const string id = std::to_string(c.id);
	Memory mem;
	const double year = fin(w.year);
	const double t0 = numOr(w, "chronicle_t." + id, year);
	const double dt = std::max(0.0, year - t0);
	const double half = T.memoryHalfLife > 1 ? T.memoryHalfLife : 1;
	const double f = std::pow(0.5, dt / half);
	mem.heard = numOr(w, "chronicle_h." + id, fin(c.born));
	mem.cat = int(numOr(w, "chronicle_c." + id, -1));
	mem.since = numOr(w, "chronicle_y." + id, -1e9);
	for (int k = 0; k < kKindCount; k++) {
		const double m0 = numOr(w, "chronicle_m." + id + "." + kKinds[k], 0);
		const double l0 = numOr(w, "chronicle_l." + id + "." + kKinds[k], 0);
		const double cum = cumulative(w, c, k);
		const double delta = cum - l0;
		mem.cum[k] = cum;
		mem.m[k] = std::max(0.0, m0 * f + std::max(0.0, delta));
		if (delta > 1e-9 && k != KAwaken) mem.heard = year;
	}
	return mem;
}

void commit(World& w, const Culture& c, const Memory& mem, int cat) {
	const string id = std::to_string(c.id);
	for (int k = 0; k < kKindCount; k++) {
		const string kk = string(".") + kKinds[k];
		if (mem.m[k] > 1e-6 || w.vars.num.count("chronicle_m." + id + kk)) w.vars.num["chronicle_m." + id + kk] = mem.m[k] > 1e-6 ? mem.m[k] : 0;
		if (mem.cum[k] > 0 || w.vars.num.count("chronicle_l." + id + kk)) w.vars.num["chronicle_l." + id + kk] = mem.cum[k];
	}
	w.vars.num["chronicle_t." + id] = fin(w.year);
	w.vars.num["chronicle_h." + id] = fin(mem.heard);
	if (cat != mem.cat || !w.vars.num.count("chronicle_y." + id)) w.vars.num["chronicle_y." + id] = fin(w.year);
	w.vars.num["chronicle_c." + id] = double(cat);
}

struct Scores { double cat[CatCount] = {}; double benev = 0, wrath = 0, total = 0; };

Scores scoresOf(const Memory& m) {
	Scores s;
	s.cat[CatRain] = m.m[KRain];
	s.cat[CatGreen] = m.m[KBloom];
	s.cat[CatHealer] = m.m[KHeal];
	s.cat[CatLamp] = m.m[KInspire];
	s.cat[CatPeace] = m.m[KPeace];
	s.cat[CatVoice] = m.m[KProphet];
	s.cat[CatShaper] = m.m[KRaise] + 0.5 * m.m[KLower];
	s.cat[CatDrowner] = m.m[KDrown] + 0.5 * m.m[KLower];
	s.cat[CatThunder] = m.m[KLightning];
	s.cat[CatShaker] = m.m[KQuake];
	s.cat[CatFever] = m.m[KPlague];
	s.cat[CatStar] = m.m[KMeteor];
	s.cat[CatFire] = m.m[KVolcano];
	s.cat[CatWaker] = m.m[KAwaken];
	for (int k = CatRain; k <= CatVoice; k++) s.benev += s.cat[k];
	for (int k = CatDrowner; k <= CatFire; k++) s.wrath += s.cat[k];
	s.total = s.benev + s.wrath + s.cat[CatShaper];
	return s;
}

int chooseRaw(const World& w, const Culture& c, const Memory& m);

// A name, once given, is kept for at least epithetHold years (no flapping between two deeds of
// similar weight), except that the waker / ancestral / silent names give way at once.
int chooseCat(const World& w, const Culture& c, const Memory& m) {
	const int raw = chooseRaw(w, c, m);
	if (m.cat < 0 || raw == m.cat) return raw;
	// (integration verifier) the Silent One is left only for a deed worth a name: a passing natural
	// event used to flip 'the Silent One' -> 'the Waker' -> 'the Silent One' every few centuries
	if (m.cat == CatSilent && (raw == CatWaker || raw == CatAncestral)) return m.cat;
	if (m.cat == CatWaker || m.cat == CatAncestral || m.cat == CatSilent) return raw;
	if (raw == CatSilent) return raw;
	const ChronicleTune& T = tuneOf(w);
	if (fin(w.year) - m.since < T.epithetHold) return m.cat;
	return raw;
}

int chooseRaw(const World& w, const Culture& c, const Memory& m) {
	const ChronicleTune& T = tuneOf(w);
	const double year = fin(w.year);
	if (year - std::max(m.heard, fin(c.born)) >= T.silentYears) return CatSilent;
	const Scores s = scoresOf(m);
	// (integration verifier) hysteresis: a god named for a deed keeps that name until the memory has
	// faded below forgetDeeds x minDeeds (natural eruptions hovering at the threshold flipped 'the
	// Elder' <-> 'She Who Opens the Mountains' five times in 300 years)
	const bool deedNamed = (m.cat >= CatRain && m.cat <= CatFire) || m.cat == CatMixed;
	const double need = deedNamed ? T.minDeeds * fin(T.forgetDeeds, 0.5) : T.minDeeds;
	if (s.total < need) {
		// nothing distinct to remember: how the people feel about their god names it
		const society::Regard rg = society::regard(w, c.id);
		const double love = fin(rg.love), dev = fin(rg.devotion);
		if (love >= T.belovedLove && dev >= T.belovedDevotion) return CatBeloved;
		if (love <= T.dreadLove) return CatDreaded;
		return c.parent >= 0 && s.cat[CatWaker] < 0.5 * T.minDeeds ? CatAncestral : CatWaker;
	}
	const double mixedShare = m.cat == CatMixed ? T.mixedShare * 0.67 : T.mixedShare;
	if (s.benev >= mixedShare * s.total && s.wrath >= mixedShare * s.total) {
		// both hands remembered: a people that has come to hate its god only remembers the blows
		if (fin(society::regard(w, c.id).love) <= T.dreadLove) return CatDreaded;
		return CatMixed;
	}
	int best = CatRain;
	for (int k = CatRain; k <= CatFire; k++) if (s.cat[k] > s.cat[best]) best = k;
	if (m.cat >= CatRain && m.cat <= CatFire && m.cat != best) {
		const double cur = s.cat[m.cat];
		if (cur > 0 && s.cat[best] < cur * T.switchMargin) return m.cat;
	}
	return best;
}

string epithetText(const World& w, int cid, int cat) {
	if (cat < 0 || cat >= CatCount) cat = CatWaker;
	const uint32_t h = hash2(uint32_t(cid * 131 + cat * 7 + 1), w.seed);
	string e = kEpithets[cat][h % 4u];
	const size_t p = e.find("{Who}");
	if (p != string::npos) e.replace(p, 5, godIsShe(w, cid) ? "She Who" : "He Who");
	return e;
}

// Population per living culture from its live settlements (culture.pop may be a year stale).
double popOf(const World& w, int cid) {
	double p = 0;
	for (int sid : civ::live(w)) {
		const Settlement* s = sett(w, sid);
		if (s && s->culture == cid) p += std::max(0.0, fin(s->pop));
	}
	return p;
}

// ====================================================================== tale helpers

string listNames(const std::vector<string>& v) {
	if (v.empty()) return "";
	if (v.size() == 1) return v[0];
	string s;
	for (size_t i = 0; i < v.size(); i++) {
		if (i > 0) s += (i + 1 == v.size()) ? " and " : ", ";
		s += v[i];
	}
	return s;
}

string countWord(int n) {
	static const char* const kW[] = { "no", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten", "eleven", "twelve" };
	if (n >= 0 && n <= 12) return kW[n];
	return fmtInt(double(n));
}

// An entry quoted after "In the year 312, ...": lower-case its first word when it is a common word
// (never a name).
string lowerLead(const string& t) {
	static const char* const kCommon[] = { "The", "A", "An", "In", "At", "Here", "Again", "Nowhere", "So", "Thus", "From",
		"Far", "Fire", "Of", "Where", "Many", "No", "There", "Settlers", "Families", "Wanderers", "Something", "Lightning",
		"Before", "After", "Among", "Hunger", "War", "Rain", "Green", "Half", "One", "For", "It" };
	const size_t sp = t.find_first_of(" ,;:");
	const string first = t.substr(0, sp);
	for (const char* k : kCommon) {
		if (first == k) {
			string out = t;
			out[0] = char(out[0] - 'A' + 'a');
			return out;
		}
	}
	return t;
}

// "in the year 1,816" / "in the first year" (lower case: callers capitalise at a sentence start)
string inYear(double y) {
	y = std::floor(fin(y));
	if (y < 1) return "in the first year";
	return "in the year " + fmtInt(y);
}

string expandOne(string_view t, const Ctx& x, Rng& r) {
	string raw;
	expandInto(raw, t, x, r, 0);
	return tidy(raw);
}

} // namespace

// ====================================================================== public API

namespace chronicle {

void attach(World& w) {
	ChronicleScratch& S = w.scratch<ChronicleScratch>();
	for (Tok t : S.tokens) w.bus.off(t);
	S.tokens.clear();
	sub<EvPhase>(w, S, onPhase);
	sub<EvMiracleWitnessed>(w, S, onMiracle);
	sub<EvLightning>(w, S, onLightning);
	sub<EvMeteorImpact>(w, S, onMeteorImpact);
	sub<EvTsunamiHit>(w, S, onTsunamiHit);
	sub<EvVolcanoStart>(w, S, onVolcanoStart);
	sub<EvVolcanoEnd>(w, S, onVolcanoEnd);
	sub<EvQuake>(w, S, onQuake);
	sub<EvFireStart>(w, S, onFireStart);
	sub<EvStorm>(w, S, onStorm);
	sub<EvDrought>(w, S, onDrought);
	sub<EvSettlementFounded>(w, S, onFounded);
	sub<EvSettlementTier>(w, S, onTier);
	sub<EvSettlementBuilding>(w, S, onBuilding);
	sub<EvSettlementFamine>(w, S, onFamine);
	sub<EvSettlementPlague>(w, S, onPlague);
	sub<EvSettlementDestroyed>(w, S, onDestroyed);
	sub<EvSettlementCaptured>(w, S, onCaptured);
	sub<EvPrayer>(w, S, onPrayer);
	sub<EvPrayerAnswered>(w, S, onPrayerAnswered);
	sub<EvCultureBorn>(w, S, onCultureBorn);
	sub<EvCultureDied>(w, S, onCultureDied);
	sub<EvCultureAge>(w, S, onCultureAge);
	sub<EvWarDeclared>(w, S, onWarDeclared);
	sub<EvWarEnded>(w, S, onWarEnded);
	sub<EvBattle>(w, S, onBattle);
	sub<EvTradeRoute>(w, S, onTradeRoute);
	sub<EvProphetRisen>(w, S, onProphetRisen);
	sub<EvProphetDied>(w, S, onProphetDied);
	sub<EvEdict>(w, S, onEdict);
	sub<EvWonderBuilt>(w, S, onWonder);
	sub<EvNuke>(w, S, onNuke);
	sub<EvArkStarted>(w, S, onArkStarted);
	sub<EvArkLaunched>(w, S, onArkLaunched);
	sub<EvClimateMilestone>(w, S, onClimate);
	sub<EvUnitDied>(w, S, onUnitDied);
	sub<EvPeopleLost>(w, S, onPeopleLost);
	sub<EvEnding>(w, S, onEnding);
}

ChronicleEntry* add(World& w, const EntryOpts& opts) {
	if (opts.text.empty()) return nullptr;
	const ChronicleTune& T = tuneOf(w);
	if (T.maxEntries > 0) {
		while (int(w.chronicle.size()) >= T.maxEntries && !w.chronicle.empty()) {
			size_t drop = w.chronicle.size();
			for (int weight = 1; weight <= 3 && drop == w.chronicle.size(); weight++)
				for (size_t i = 0; i < w.chronicle.size(); i++) if (w.chronicle[i].weight <= weight) { drop = i; break; }
			if (drop >= w.chronicle.size()) drop = 0;
			w.chronicle.erase(w.chronicle.begin() + std::ptrdiff_t(drop));
		}
	}
	ChronicleEntry e;
	e.id = w.nextId.entry++;
	e.year = fin(w.year);
	e.type = opts.type.empty() ? string("note") : opts.type;
	e.text = opts.text;
	e.cell = w.valid(opts.cell) ? opts.cell : -1;
	e.culture = (opts.culture >= 0 && opts.culture < int(w.cultures.size())) ? opts.culture : -1;
	e.weight = opts.weight < 1 ? 1 : opts.weight > 3 ? 3 : opts.weight;
	const int id = e.id;
	w.chronicle.push_back(e);
	w.bus.emit(EvChronicleEntry{ e });
	for (size_t i = w.chronicle.size(); i-- > 0;) if (w.chronicle[i].id == id) return &w.chronicle[i];
	return nullptr;
}

std::string yearStr(const World& w, std::optional<double> year) {
	const double y = year ? *year : w.year;
	return "Year " + std::to_string(isFinite(y) ? static_cast<long long>(std::floor(y)) : 0LL);
}

std::string epithet(const World& w, int cultureId) {
	const Culture* c = cult(w, cultureId);
	if (!c) return std::string();
	const Memory m = project(w, *c);
	return epithetText(w, cultureId, chooseCat(w, *c, m));
}

void updateEpithets(World& w) {
	int bestC = -1;
	double bestP = -1;
	for (size_t i = 0; i < w.cultures.size(); i++) {
		const int cid = int(i);
		if (w.cultures[i].dead) continue;
		if (w.cultures[i].godName.empty()) {
			Rng r = w.rng.fork("chron:god:" + std::to_string(cid));
			w.cultures[i].godName = names::god(w.cultures[i].lang, r);
		}
		const Memory m = project(w, w.cultures[i]);
		const int cat = chooseCat(w, w.cultures[i], m);
		commit(w, w.cultures[i], m, cat);
		const string e = epithetText(w, cid, cat);
		if (e != w.cultures[i].epithet) {
			const string old = w.cultures[i].epithet;
			w.cultures[i].epithet = e;
			Ctx x;
			ctxCulture(x, w, cid);
			x.set("e", e);
			x.set("old", old);
			if (old.empty()) write(w, "epithet", 2, -1, cid, tpl(kEpiFirst), "epithet-first", x);
			else if (cat == CatSilent) write(w, "epithet", 2, -1, cid, tpl(kEpiSilent), "epithet-silent", x);   // (integration verifier) not "after what they had seen"
			else write(w, "epithet", 2, -1, cid, tpl(kEpiChange), "epithet-change", x);
		}
		const double p = popOf(w, cid);
		if (p > bestP) { bestP = p; bestC = cid; }
	}
	if (bestC >= 0) w.god.epithet = w.cultures[size_t(bestC)].epithet;
}

bool snapshot(World& w, bool force) {
	const ChronicleTune& T = tuneOf(w);
	ChronicleScratch& S = w.scratch<ChronicleScratch>();
	if (!w.vars.snapEvery || !std::isfinite(*w.vars.snapEvery) || !(*w.vars.snapEvery > 0))
		w.vars.snapEvery = T.snapEvery > 0 ? T.snapEvery : 25.0;
	const double year = fin(w.year);
	const bool due = !w.vars.snapNext || !std::isfinite(*w.vars.snapNext) || year >= *w.vars.snapNext - 1e-9;
	if (!force && !due) return false;
	if (w.snapshots.empty()) S.genesisFrames = 0;
	else if (S.genesisFrames < 0 || S.genesisFrames > int(w.snapshots.size())) S.genesisFrames = 0;   // unknown: all history

	Snapshot f;
	f.year = year;
	f.heat = fin(w.heat);
	f.seaLevel = fin(w.seaLevel);
	double pop = 0;
	for (const Settlement& s : w.settlements) if (!s.dead) pop += std::max(0.0, fin(s.pop));
	f.pop = pop;
	const size_t n = size_t(std::max(0, w.n));
	f.biome.assign(n, 0);
	f.owner.assign(n, int8_t(-1));
	f.urban.assign(n, 0);
	for (size_t c = 0; c < n; c++) {
		if (c < w.biome.size()) f.biome[c] = w.biome[c];
		if (c < w.owner.size()) {
			const int o = w.owner[c];
			if (o >= 0 && o < int(w.settlements.size())) {
				const int cu = w.settlements[size_t(o)].culture;
				if (cu >= 0) f.owner[c] = int8_t(cu > 127 ? 127 : cu);
			}
		}
		if (c < w.urban.size()) f.urban[c] = uint8_t(std::lround(sat(fin(w.urban[c])) * 255.0));
	}
	const bool genesis = w.phase != Phase::History && w.phase != Phase::Ended;
	if (genesis && S.genesisFrames == int(w.snapshots.size())) S.genesisFrames++;
	w.snapshots.push_back(std::move(f));

	const int maxFrames = T.maxFrames > 8 ? T.maxFrames : 8;
	if (int(w.snapshots.size()) > maxFrames) {
		// drop every second history frame (never the first or the newest), double the interval
		const size_t g = size_t(std::max(0, S.genesisFrames));
		std::vector<Snapshot> kept;
		kept.reserve(w.snapshots.size());
		for (size_t i = 0; i < w.snapshots.size(); i++) {
			const bool history = i >= g;
			const size_t j = i - (history ? g : 0);
			const bool newest = i + 1 == w.snapshots.size();
			if (!history || newest || j % 2 == 0) kept.push_back(std::move(w.snapshots[i]));
		}
		w.snapshots = std::move(kept);
		w.vars.snapEvery = *w.vars.snapEvery * 2;
		w.vars.snapNext = year + *w.vars.snapEvery;
	} else if (!w.vars.snapNext || !std::isfinite(*w.vars.snapNext) || *w.vars.snapNext <= year + 1e-9) {
		w.vars.snapNext = year + *w.vars.snapEvery;
	}
	return true;
}

Tale tale(const World& w) {
	Tale out;
	Rng r = w.rng.fork("chron:tale");
	Ctx x;
	ctxWorld(x, w);
	const string W = worldName(w);

	// ---- facts
	const std::vector<int> liveIds = civ::live(w);
	int founded = int(w.settlements.size()), destroyed = 0, livingCultures = 0;
	int lostBy[enumCount<Cause>()] = {};
	for (const Settlement& s : w.settlements) if (s.dead) { destroyed++; lostBy[int(s.cause)]++; }
	for (const Culture& c : w.cultures) if (!c.dead) livingCultures++;
	double peak = fin(w.stats.peakPop), now = 0;
	for (int sid : liveIds) now += std::max(0.0, fin(sett(w, sid)->pop));
	peak = std::max(peak, now);
	int answered = 0, unanswered = 0, battles = 0, captures = 0, famines = 0, plagues = 0, meteors = 0, volcanoes = 0, wonders = 0;
	for (const ChronicleEntry& e : w.chronicle) {
		if (e.type == "prayer-answered") answered++;
		else if (e.type == "prayer-unanswered") unanswered++;
		else if (e.type == "battle") battles++;
		else if (e.type == "captured") captures++;
		else if (e.type == "famine") famines++;
		else if (e.type == "plague") plagues++;
		else if (e.type == "meteor") meteors++;
		else if (e.type == "volcano") volcanoes++;
		else if (e.type == "wonder") wonders++;
	}
	const double years = fin(w.year);
	int topAge = std::clamp(w.age, 0, int(Age::COUNT) - 1);
	for (const Culture& c : w.cultures) topAge = std::max(topAge, std::clamp(c.age, 0, int(Age::COUNT) - 1));

	// ---- title
	{
		static const char* const kTitles[] = { "The Chronicle of {W}", "The Book of {W}", "What Was Written on {W}", "The Long Story of {W}", "The Annals of {W}" };
		string t;
		expandInto(t, kTitles[r.pickIndex(5)], x, r, 0);
		out.title = t;
	}

	// ---- 1. genesis
	{
		int sea = 0;
		for (int c = 0; c < w.n; c++) if (c < int(w.elev.size()) && state::isSea(w, c)) sea++;
		const double share = w.n > 0 ? double(sea) / double(w.n) : 0;
		x.set("seaw", share > 0.75 ? "nearly all" : share > 0.55 ? "the greater part" : share > 0.35 ? "half" : share > 0.05 ? "a little" : "none");
		static const char* const kP[] = {
			"In the beginning {W} was fire. The god cooled it and called down the ice of heaven, until the sea covered {seaw} of its face. Then green came upon the land, and beasts, and the god looked for a people to wake.",
			"Before the counting of years, {W} was a ball of molten stone in the dark. The god let it cool, filled its hollows with the water of comets until the sea held {seaw} of the world, and planted the first green on the shores.",
		};
		out.paragraphs.push_back(expandOne(kP[r.pickIndex(2)], x, r));
	}

	// ---- 2. the first people
	{
		int first = -1;
		for (const Culture& c : w.cultures) if (c.parent < 0) { first = c.id; break; }
		if (first < 0 && !w.cultures.empty()) first = 0;
		int home = -1;
		for (const Settlement& s : w.settlements) if (s.culture == first || home < 0) { if (s.culture == first) { home = s.id; break; } }
		if (home < 0 && !w.settlements.empty()) home = 0;
		if (first >= 0) {
			ctxCulture(x, w, first);
			ctxSettlement(x, w, home);
			if (home < 0) x.set("s", "a place no one remembers");
			std::vector<string> others;
			for (const Culture& c : w.cultures) {
				if (c.id == first) continue;
				string nm = culturePlural(w, c.id);
				if (std::find(others.begin(), others.end(), nm) == others.end()) others.push_back(nm);   // distinct names only
			}
			string p = expandOne("The first people were {c}. The god woke them {t} at {s}, and they called the god {g}.", x, r);
			if (!others.empty()) {
				if (others.size() > 6) others.resize(6);
				p += " After them came " + listNames(others) + ", " + (others.size() == 1 ? string("a people") : string("peoples")) + " with their own tongues and their own names for the god.";
			}
			out.paragraphs.push_back(p);
		} else {
			out.paragraphs.push_back(expandOne("No people ever woke on {W}. Its forests grew and fell with no one to name them, and the god watched alone.", x, r));
		}
	}

	// ---- 3. growth
	if (founded > 0) {
		int biggest = -1;
		double bp = -1;
		for (const Settlement& s : w.settlements) if (fin(s.pop) > bp) { bp = fin(s.pop); biggest = s.id; }
		ctxSettlement(x, w, biggest);
		string p = (years >= 1.5 ? "In " + fmtInt(years) + " years" : string("In their first years")) + " the peoples of " + W + " founded " + countWord(founded) + (founded == 1 ? " settlement" : " settlements");
		p += ", and at their height they numbered " + fmtInt(peak) + " souls.";
		p += " The greatest of their places was " + settlementName(w, biggest) + ".";
		if (topAge > 0) p += string(" The furthest of them reached the age of ") + kAgeNames[topAge] + ".";
		else p += " None of them ever learned to work metal.";
		out.paragraphs.push_back(p);
	}

	// ---- 4. strife
	if (w.stats.wars > 0 || captures > 0 || w.vars.lastNuke) {
		const int razed = lostBy[int(Cause::War)];
		string p = "They were not always at peace.";
		if (w.stats.wars > 0) {
			p += " They fought " + (w.stats.wars == 1 ? string("one war") : countWord(w.stats.wars) + " wars");
			if (captures > 0 || razed > 0) p += ", in which ";
			if (captures > 0) p += countWord(captures) + (captures == 1 ? " town changed hands" : " towns changed hands");
			if (captures > 0 && razed > 0) p += " and ";
			if (razed > 0) p += countWord(razed) + (captures > 0 ? string("") : string(razed == 1 ? " town" : " towns")) + (razed == 1 ? " was burned to the ground" : " were burned to the ground");
			p += ".";
		} else if (captures > 0) {
			p += " " + countWord(captures) + (captures == 1 ? " town was taken by force." : " towns were taken by force.");
		}
		if (w.vars.lastNuke) p += " The last time they burned cities with the fire of the sun was " + inYear(*w.vars.lastNuke) + ".";
		out.paragraphs.push_back(tidy(p));
	}

	// ---- 5. the god
	{
		std::vector<std::pair<int, string>> deeds;
		for (const auto& kv : w.god.deeds) if (kv.second > 0 && kv.first != "awaken") deeds.push_back({ kv.second, kv.first });
		std::sort(deeds.begin(), deeds.end(), [](const auto& a, const auto& b) { return a.first != b.first ? a.first > b.first : a.second < b.second; });
		string p;
		if (deeds.empty()) {
			p = "The god did little that the scribes could see.";
		} else {
			std::vector<string> what;
			for (size_t i = 0; i < deeds.size() && i < 3; i++) {
				const string& id = deeds[i].second;
				string word = id;
				if (id == "rain") word = "rain"; else if (id == "bloom") word = "green growth"; else if (id == "raise") word = "the raising of hills";
				else if (id == "lower") word = "the sinking of lands"; else if (id == "lightning") word = "lightning"; else if (id == "quake") word = "earthquakes";
				else if (id == "plague") word = "sickness"; else if (id == "meteor") word = "falling stars"; else if (id == "volcano") word = "mountains of fire";
				else if (id == "heal") word = "healing"; else if (id == "inspire") word = "inspiration"; else if (id == "prophet") word = "prophets";
				else if (id == "peace") word = "omens of peace"; else if (id == "comet") word = "comets"; else if (id == "seed") word = "the first seeds";
				what.push_back(word);
			}
			p = "The god was known for " + listNames(what) + ".";
		}
		const double mercy = fin(w.god.mercy), wrath = fin(w.god.wrath);
		if (mercy > 2 * wrath + 0.05) p += " The scribes wrote more of the god's mercy than of the god's anger.";
		else if (wrath > 2 * mercy + 0.05) p += " The scribes wrote more of the god's anger than of the god's mercy, and they wrote it carefully.";
		else if (mercy + wrath > 0.05) p += " Mercy and anger came from the same hand, and the people never knew which to expect.";
		if (answered > 0 && unanswered > 0) p += " Of the prayers the scribes wrote down, " + countWord(answered) + (answered == 1 ? " was" : " were") + " answered and " + countWord(unanswered) + " went unheard.";
		else if (answered > 0) p += " Of the prayers the scribes wrote down, " + countWord(answered) + (answered == 1 ? " was" : " were") + " answered, and none went unheard.";
		else if (unanswered > 0) p += " The peoples prayed, and " + countWord(unanswered) + (unanswered == 1 ? " prayer" : " prayers") + " went unheard.";
		if (!w.god.epithet.empty()) p += " In the end, most of them called the god " + w.god.epithet + ".";
		out.paragraphs.push_back(tidy(p));
	}

	// ---- 6. calamities
	{
		std::vector<string> bits;
		if (meteors > 0) bits.push_back(countWord(meteors) + (meteors == 1 ? " star fell" : " stars fell"));
		if (volcanoes > 0) bits.push_back(countWord(volcanoes) + (volcanoes == 1 ? " mountain woke in fire" : " mountains woke in fire"));
		if (famines > 0) bits.push_back("hunger came " + (famines == 1 ? string("once") : countWord(famines) + " times"));
		if (plagues > 0) bits.push_back("sickness came " + (plagues == 1 ? string("once") : countWord(plagues) + " times"));
		const int drowned = lostBy[int(Cause::Drowned)] + lostBy[int(Cause::Wave)];
		if (drowned > 0) bits.push_back(countWord(drowned) + (drowned == 1 ? " town was taken by the sea" : " towns were taken by the sea"));
		if (!bits.empty() && out.paragraphs.size() < 7) {
			string p = "There were hard times. " + listNames(bits) + ".";
			if (destroyed > 0) p += " Of all the places the peoples built, " + countWord(destroyed) + " lay in ruins.";
			out.paragraphs.push_back(tidy(p));
		}
	}

	// ---- 7. what the scribes remembered
	{
		std::vector<const ChronicleEntry*> big;
		// (the Ark and the end are the last paragraph's)
		for (const ChronicleEntry& e : w.chronicle)
			if (e.weight >= 3 && e.type != "genesis" && e.type != "ending" && !(w.ending && (e.type == "ark" || e.type == "lost"))) big.push_back(&e);
		if (big.size() > 4) {
			// the first, the last and two between, evenly
			std::vector<const ChronicleEntry*> pickd;
			const size_t m = big.size();
			for (size_t k = 0; k < 4; k++) pickd.push_back(big[k * (m - 1) / 3]);
			big = pickd;
		}
		if (!big.empty() && out.paragraphs.size() < 7) {
			string p = "These things the scribes remembered above all.";
			for (const ChronicleEntry* e : big) {
				const double y = fin(e->year);
				p += " " + string(y < 1 ? "In the first year" : "In the year " + fmtInt(std::floor(y))) + ", " + lowerLead(e->text);
			}
			out.paragraphs.push_back(p);
		}
	}

	// ---- 8. the end
	{
		string p;
		if (w.ending) {
			const Ending& en = *w.ending;
			ctxCulture(x, w, en.culture);
			if (en.type == EndingType::Ark) {
				switch (en.mood) {
				case Mood::Beloved: p = expandOne(inYear(en.year) + ", {c} went out among the stars, and they carried the name of {ge} with them. They were loved, and they loved in return.", x, r); break;
				case Mood::Feared: p = expandOne(inYear(en.year) + ", {c} fled to the stars. They were glad to leave the god behind, and the god knew it.", x, r); break;
				case Mood::Forgotten: p = expandOne(inYear(en.year) + ", {c} left for the stars. By then they hardly remembered the god at all.", x, r); break;
				default: p = expandOne(inYear(en.year) + ", {c} left for the stars, and the god watched them go, not knowing quite what they had been to each other.", x, r); break;
				}
			} else if (en.mood == Mood::Ash) {
				p = inYear(en.year) + ", the last of the peoples died in the ash of their own fires. " + W + " was silent.";
			} else {
				p = inYear(en.year) + ", the last of the peoples passed away, and " + W + " grew quiet, and green, and empty.";
			}
		} else if (liveIds.empty() && !w.settlements.empty()) {
			p = "Now no one lives on " + W + ", and the god waits.";
		} else if (!liveIds.empty()) {
			p = "The story of " + W + " is not finished. " + countWord(livingCultures) + (livingCultures == 1 ? " people still lives" : " peoples still live") + " there, in " + countWord(int(liveIds.size())) + (liveIds.size() == 1 ? " place" : " places") + ", and the scribes are still writing.";
		} else {
			p = "The world is still waiting for its people.";
		}
		out.paragraphs.push_back(tidy(p));
	}

	// 4..8 paragraphs
	const ChronicleTune& T = tuneOf(w);
	const size_t lo = size_t(std::max(1, T.taleMinParagraphs)), hi = size_t(std::max(T.taleMinParagraphs, T.taleMaxParagraphs));
	while (out.paragraphs.size() > hi) out.paragraphs.erase(out.paragraphs.end() - 2);
	while (out.paragraphs.size() < lo) out.paragraphs.insert(out.paragraphs.end() - 1, tidy("The rest is told in the chronicle, year by year, for those who have the patience to read it"));

	out.stats["years"] = years;
	out.stats["peakPop"] = peak;
	out.stats["pop"] = now;
	out.stats["settlements"] = founded;
	out.stats["live"] = double(liveIds.size());
	out.stats["destroyed"] = destroyed;
	out.stats["cultures"] = double(w.cultures.size());
	out.stats["livingCultures"] = livingCultures;
	out.stats["wars"] = w.stats.wars;
	out.stats["miracles"] = w.stats.miracles;
	out.stats["entries"] = double(w.chronicle.size());
	out.stats["highestAge"] = topAge;
	out.stats["prayersAnswered"] = answered;
	out.stats["wonders"] = wonders;
	return out;
}

} // namespace chronicle
} // namespace dm
