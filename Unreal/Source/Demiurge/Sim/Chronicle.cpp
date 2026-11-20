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
//   budget        (wave 3, design review finding 8) on top of the gates, token buckets per decade cap the
//                 notable and epochal entries; routine notables over budget become minor, story ones only
//                 lose weight; a second minor entry of a kind for a people in one decade, one past the
//                 world's minor bucket, or one whose every template line was written templateCap times is
//                 folded into the decade's digest ("That decade Sarello answered the prayers of seven towns
//                 of the Domafi"). Templates are chosen least-used first. See "the budget" below.
//   drownings     (finding 7) a town the sea covers is the god's drowning (remembered: the Drowner) only
//                 near a recent Lower Land / quake / meteor stroke of the god; otherwise the sea's own rise
//                 or the engines' (the people burning the most are named), and waves count only after the
//                 god shook or struck the world.
//   epithets      each living culture remembers what it saw: increments of culture.seen (written by
//                 Society) decay with a half-life of memoryHalfLife years, plus settlements it lost to
//                 the sea. The dominant memory picks a category (rain, green, meteor, drowner, ...);
//                 the culture's own variant of that category is fixed by a hash, so an epithet only
//                 changes when the god's behaviour changes, and is then kept at least epithetHold
//                 years. Long silence gives 'the Silent One'; with no deed worth remembering,
//                 society::regard decides (Kind: 'the Gentle Hand', Cruel: 'the Unkind Sky', else 'the Maker').
//   voice         the prose, epithets and endings follow the approved voice
//                 (docs/content/chronicle-voice.md); see the "templates" section for the placeholder map.
//                 Memory lives in w.vars.num under "chronicle_*" keys (persisted, deterministic).
//   snapshots     time-lapse frames per SPEC-SIM 3.11; past maxFrames every second history frame is
//                 dropped and the interval doubled.
//   tale          read-only retelling built from the world's entities, stats and epochal entries.
#include "Chronicle.h"

#include <algorithm>
#include <cctype>
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
	std::map<string, std::vector<int>> lastPicks;  // (voice) the last few template indices per kind, newest last
	int64_t answeredTick = -1;                     // prayer:answered seen on this tick and year ...
	double answeredYear = -1e9;
	std::vector<int> answered;                     // ... for these settlements: the miracle:witnessed that
	                                               // follows it (same witness call) is already told
	std::map<int, double> prayerSince;             // settlement id -> year its prayer began
	int64_t bornTick = -1;                         // culture:born seen on this tick and year ...
	double bornYear = -1e9;
	std::vector<int> bornCultures;                 // ... for these cultures (their schism captures are part of it)
	int genesisFrames = -1;                        // leading snapshot frames taken before history (-1 unknown)
	// (voice) the template bodies of the last entries: a body used in the last kRecentTemplates entries
	// is skipped when its bag offers another (templates shared between families never meet twice in a row)
	std::vector<string> recentTpl;
	// (voice) the battle that took a settlement this tick: names the attacker when it is then razed
	int64_t wonTick = -1;
	double wonYear = -1e9;
	int wonSettlement = -1, wonAttacker = -1;
	// (voice review 2) endings held back until the tick is over: a click power's effect is emitted before
	// the miracle that caused it (Powers: effect, power:used, witness), so a plague cured by Heal and a war
	// ended by the Omen of Peace are told by the miracle instead of "the fever burned itself out" / "neither
	// could say it had won" first. Written at the next event of a later tick or by updateEpithets (yearly).
	struct Pending {
		int64_t tick = -1;
		double year = 0;
		bool war = false;      // false: the end of a plague (a = settlement id); true: a war ended in a draw (a, b cultures)
		int a = -1, b = -1;
		// (chronicle budget) a settlement the sea covered (a = settlement id, b = its culture, cell): Powers drowns
		// a lowered town before it emits power:used, so whose drowning it was is decided once the tick is over
		bool drown = false;
		int cell = -1, tier = 0;
	};
	// (chronicle budget) the last write was folded into the decade's digest (it was told, in a way)
	bool foldedLast = false;
	bool inDigest = false;                         // the digest itself is being written (no budget, no flush)
	// (verify) the next entry, a story entry by its type, is tellable this once (a natural eruption, a smaller
	// people renaming its god): notable while the story bucket allows, then minor. budgetGate clears it.
	bool tellNext = false;
	std::vector<Pending> pending;
	// (voice review 2) the omen of peace that named both armies this tick: the other people's omen is that one
	int64_t omenTick = -1;
	double omenYear = -1e9;
	std::vector<int> omenCultures;
};
constexpr size_t kRecentTemplates = 10;

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
// (voice {god_epithet}) the culture's epithet for its god alone, article included ("the Rain-Giver");
// before the culture's first updateEpithets the world's epithet, else "the god" (not the god's name:
// the lines that use {ep} often name {g} too).
string epithetOf(const World& w, int cid) {
	const Culture* c = cult(w, cid);
	if (c && !c->epithet.empty()) return c->epithet;
	if (!w.god.epithet.empty()) return w.god.epithet;
	return "the god";
}
// "a" / "an" for a common noun
string withArticle(const string& noun) {
	if (noun.empty()) return noun;
	const char ch = noun[0];
	const bool vowel = ch == 'a' || ch == 'e' || ch == 'i' || ch == 'o' || ch == 'u' || ch == 'A' || ch == 'E' || ch == 'I' || ch == 'O' || ch == 'U';
	return (vowel ? "an " : "a ") + noun;
}
// (voice {year_ord}) 312 -> "312th", 1 -> "1st", 11 -> "11th", 22 -> "22nd"; before year 1, "first"
string yearOrdinal(double year) {
	const double y = std::floor(fin(year));
	if (y < 1 || y > 1e9) return "first";
	const long long n = static_cast<long long>(y);
	const long long t = n % 100, u = n % 10;
	const char* sfx = (t >= 11 && t <= 13) ? "th" : u == 1 ? "st" : u == 2 ? "nd" : u == 3 ? "rd" : "th";
	return std::to_string(n) + sfx;
}
// (voice {war_years}) a count of years in words: "a single year", "eleven years", "forty-two years"
string yearsInWords(double years) {
	static const char* const kOnes[] = { "", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten", "eleven",
		"twelve", "thirteen", "fourteen", "fifteen", "sixteen", "seventeen", "eighteen", "nineteen" };
	static const char* const kTens[] = { "", "", "twenty", "thirty", "forty", "fifty", "sixty", "seventy", "eighty", "ninety" };
	const int n = int(std::floor(fin(years) + 0.5));
	if (n <= 1) return "a single year";
	if (n >= 200) return "many generations";
	if (n >= 100) return "more than a hundred years";
	string s = n < 20 ? string(kOnes[n]) : string(kTens[n / 10]) + (n % 10 ? string("-") + kOnes[n % 10] : string());
	return s + " years";
}
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
		if (c == '"' && i >= 2 && s[i - 1] == ' ' && s[i - 2] == ':') cap = true;     // a quoted saying: ... spoke: "Lay down ..."
	}
	if (!s.empty() && s.back() != '.' && s.back() != '!' && s.back() != '?' && s.back() != '"') s.push_back('.');
	return s;
}

struct Tpl { const char* const* t; int n; };
template <size_t N> Tpl tpl(const char* const (&a)[N]) { return Tpl{ a, int(N) }; }

bool recentlyUsed(const ChronicleScratch& S, const char* body) {
	for (const string& b : S.recentTpl) if (b == body) return true;
	return false;
}
// position of the body's last use in the ring (larger = more recent), -1 if not in it
int lastUse(const ChronicleScratch& S, const char* body) {
	for (size_t i = S.recentTpl.size(); i-- > 0;) if (S.recentTpl[i] == body) return int(i);
	return -1;
}
void markUsed(ChronicleScratch& S, const char* body) {
	S.recentTpl.push_back(body);
	if (S.recentTpl.size() > kRecentTemplates) S.recentTpl.erase(S.recentTpl.begin());
}

// The next template of a family from its shuffle bag. (voice) A body used by one of the last
// kRecentTemplates entries (the same sentence may sit in two families, e.g. the generic building
// lines) is passed over: first for another in the bag, then for any line of the family not used
// lately, and when every line was used lately, for the one used longest ago.
const ChronicleTune& tuneOf(const World& w);

// (chronicle budget) How often a template body has been written in this history: vars.num
// "chronicle_u.<8 hex digits of its FNV-1a hash>" (saved, so a loaded game keeps rotating where it was).
string useKey(const char* body) {
	uint32_t h = 2166136261u;
	for (const char* p = body; p && *p; ++p) { h ^= uint8_t(*p); h *= 16777619u; }
	static const char* const kHex = "0123456789abcdef";
	string k = "chronicle_u.";
	for (int i = 7; i >= 0; i--) k.push_back(kHex[(h >> (4 * i)) & 15u]);
	return k;
}
int useCount(const World& w, const char* body) {
	const auto it = w.vars.num.find(useKey(body));
	return (it == w.vars.num.end() || !std::isfinite(it->second)) ? 0 : int(it->second);
}
void bumpUse(World& w, const char* body) {
	double& v = w.vars.num[useKey(body)];
	v = fin(v) + 1;
}
// (chronicle budget) every line of the family has been written templateCap times or more
bool familySpent(const World& w, Tpl t, int cap) {
	if (!t.t || t.n <= 0 || cap <= 0) return false;
	for (int i = 0; i < t.n; i++) if (useCount(w, t.t[i]) < cap) return false;
	return true;
}

int pickTemplate(World& w, ChronicleScratch& S, const string& key, Tpl t, Rng& r) {
	const int n = t.n;
	if (n <= 1) { if (n == 1) bumpUse(w, t.t[0]); return 0; }
	std::vector<int>& bag = S.bags[key];
	if (bag.empty()) {
		for (int i = 0; i < n; i++) bag.push_back(i);
		r.shuffle(bag);
		const auto it = S.lastPick.find(key);
		if (it != S.lastPick.end() && bag.back() == it->second) std::swap(bag.front(), bag.back());
		// (voice) the lines that closed the last round open the next one last: a line comes back after
		// about half the family at the least, not two entries later
		const std::vector<int>& last = S.lastPicks[key];
		for (int k : last) {
			const auto at = std::find(bag.begin(), bag.end(), k);
			if (at != bag.end()) { bag.erase(at); bag.insert(bag.begin(), k); }
		}
	}
	int idx = -1;
	if (!tuneOf(w).budget) {
		for (size_t k = bag.size(); k-- > 0 && idx < 0;) {
			const int i = bag[k];
			if (i >= 0 && i < n && !recentlyUsed(S, t.t[i])) idx = i;
		}
	} else {
		// (chronicle budget) least used first: a line written templateCap times gives way to any fresher line of
		// the family; the family's first line opens it; then the shuffle bag's order; then the line used least in
		// this history; never one of the last kRecentTemplates entries' lines while another is free
		const int cap = tuneOf(w).templateCap;
		long long best = -1;
		for (int i = 0; i < n; i++) {
			if (recentlyUsed(S, t.t[i])) continue;
			const int u = useCount(w, t.t[i]);
			const auto at = std::find(bag.begin(), bag.end(), i);
			const long long inBag = at != bag.end() ? 1 + (at - bag.begin()) : 0;   // later in the bag = sooner
			// a family's first line (the approved voice's, where it has one) opens it in every history
			const long long opener = (i == 0 && u == 0) ? 1LL << 45 : 0;
			const long long score = (cap > 0 && u >= cap ? 0 : 1LL << 50) + opener + (inBag > 0 ? 1LL << 40 : 0) + (1LL << 30) - std::min(u, 1 << 29) * 64LL + inBag;
			if (score > best) { best = score; idx = i; }
		}
	}
	if (idx < 0) {
		// every line left in the bag was used lately: the family's least recently used line
		int best = -1, bestUse = int(kRecentTemplates) + 1;
		for (int i = 0; i < n; i++) {
			const int u = lastUse(S, t.t[i]);
			if (u < bestUse) { bestUse = u; best = i; }
		}
		idx = best >= 0 ? best : bag.back();
	}
	const auto at = std::find(bag.begin(), bag.end(), idx);
	if (at != bag.end()) bag.erase(at);
	if (idx < 0 || idx >= n) idx = 0;
	S.lastPick[key] = idx;
	bumpUse(w, t.t[idx]);
	std::vector<int>& last = S.lastPicks[key];
	last.push_back(idx);
	if (int(last.size()) > std::max(1, n / 2)) last.erase(last.begin());
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
	x.set("ep" + s, epithetOf(w, cid));                  // (voice {god_epithet}) the epithet alone
}

void ctxSettlement(Ctx& x, const World& w, int sid, const char* sfx = "") {
	const string s(sfx);
	x.set("s" + s, settlementName(w, sid));
	const Settlement* st = sett(w, sid);
	x.set("sn" + s, string("the ") + tierWord(st ? st->tier : 0));
	x.set("st" + s, tierWord(st ? st->tier : 0));
	x.set("t" + s, st ? terrainPhrase(w, st->cell) : string("in the wilds"));
	x.set("ast" + s, withArticle(tierWord(st ? st->tier : 0)));
	// (voice 4a "by the water") only where there is water: the coast or a river or lake within a hop
	bool water = false;
	if (st && w.valid(st->cell)) {
		const size_t c = size_t(st->cell);
		water = (c < w.coast.size() && w.coast[c] <= 1) || (c < w.fresh.size() && w.fresh[c] <= 1);
	}
	x.set("by" + s, water ? string("by the water") : (st ? terrainPhrase(w, st->cell) : string("in the wilds")));
}

// ====================================================================== writing entries

const ChronicleTune& tuneOf(const World& w) { return w.tune<ChronicleTune>(); }

// An entry of this type for this culture within the last `gap` years (any weight)?
// culture = kAnyCulture: an entry of this type for anyone.
constexpr int kAnyCulture = -99;
bool recent(const World& w, string_view type, int culture, double gap) {
	const double y = fin(w.year);
	// (chronicle budget) an answered prayer folded into the decade's digest was told, in its way: the prayers it
	// quiets (prayerHeard) and the answers' own gaps see it too ("chronicle_f.<culture>.<type>" /
	// "chronicle_fw.<type>" = the year of the last fold). Other folds leave the gates alone, so their repeats
	// reach the digest's count.
	if (type == "prayer-answered") {
		const auto f = w.vars.num.find(culture == kAnyCulture ? "chronicle_fw." + string(type) : "chronicle_f." + std::to_string(culture) + "." + string(type));
		if (f != w.vars.num.end() && std::isfinite(f->second) && f->second <= y + 1e-9 && f->second >= y - gap - 1e-9) return true;
	}
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

// (voice) Time keys every entry may use: {yo} the year as an ordinal ("312th", "first"), and the
// detachable openers {IN} "In the 312th year " / {IN,} "In the 312th year, ". The openers are empty
// when one of the last two entries already opened with "In the", so the feed does not drum
// (style guide 3); templates read as sentences either way.
void ctxYear(Ctx& x, const World& w) {
	const string yo = yearOrdinal(w.year);
	x.set("yo", yo);
	bool drum = false;
	for (size_t i = w.chronicle.size(), k = 0; i-- > 0 && k < 2; k++)
		if (startsWith(w.chronicle[i].text, "In the ")) drum = true;
	x.set("IN", drum ? string() : "In the " + yo + " year ");
	x.set("IN,", drum ? string() : "In the " + yo + " year, ");
}

// ====================================================================== the budget (wave 3, finding 8)
// Every entry a handler writes passes budgetGate (writeComposed) first. Three kinds of entry:
//   story     epithets, the world's first of an age, peoples born and dead, wonders, the Ark, the god's
//             meteors, volcanoes and quakes, climate, genesis, endings: never dropped, never demoted; an epochal
//             one (weight 3) is always epochal
//   tellable  a settlement destroyed, a war declared or ended, a people's later age, prophets and edicts, the
//             god's heal / inspire / omen / plague: never dropped; a city's
//             fall is epochal while the epochal bucket allows (about one a real minute); all of them are notable
//             while the story bucket allows, then minor
//   routine   famines, growth, buildings, trade, prayers and their answers, the god's brush, battles, captures,
//             weather...: notable while the notable bucket allows, else minor; a minor one is written only if
//             it is the first of its kind for its people this decade, the world's minor bucket has a token and
//             its template family has lines left under templateCap; otherwise it is FOLDED: counted in the
//             decade's ledger, and the decade's digest entry tells the biggest folds in one sentence
//             (verify: at most one digest in digestEvery = 30 years; the folds wait and add up, its frames and
//             clauses rotate like every family)
//   (verify) a natural eruption and a smaller people's later name for the god are tellable, not story; a people
//             born or dead is epochal while the epochal bucket allows, else notable; only the world's first Ark is epochal
// Buckets, ledger and marks live in vars.num ("chronicle_bk.*", "chronicle_dg.*", "chronicle_dt.*"): saved,
// deterministic, read by no other module. With ChronicleTune::budget = false nothing of this runs.

// the digest group of a routine entry (nullptr: story or tellable)
const char* groupOf(string_view type, int weight) {
	struct G { const char* type; const char* group; };
	static const G kGroups[] = {
		{ "prayer", "pray" }, { "prayer-answered", "answer" }, { "prayer-unanswered", "unheard" }, { "famine", "famine" },
		{ "plague", "plague" }, { "plague-end", "healed" }, { "miracle-rain", "rain" }, { "miracle-bloom", "bloom" },
		{ "miracle-shape", "shape" }, { "founded", "found" }, { "tier", "grow" }, { "decline", "shrink" }, { "building", "build" },
		{ "trade", "trade" }, { "battle", "battle" }, { "captured", "take" }, { "schism", "take" }, { "settlers-lost", "lost" },
		{ "fire", "weather" }, { "lightning", "weather" }, { "storm", "weather" }, { "drought", "weather" }, { "drought-end", "weather" },
		{ "tsunami", "weather" }, { "volcano-end", "weather" }, { "wrath-lightning", "bolt" },
	};
	if (type == "quake") return weight <= 1 ? "weather" : nullptr;     // a natural tremor; the god's quake is story
	if (type == "destroyed") return weight <= 1 ? "empty" : nullptr;   // a hamlet given up; a town or city destroyed is tellable
	if (type == "tier" && weight >= 3) return nullptr;                  // the world's first city
	for (const G& g : kGroups) if (type == g.type) return g.group;
	return nullptr;
}

// one clause per group: {n} how many (in words), {g} the people's name for the god, and the people as
// {cS} subject ("the Vaeli" / "they"), {cO} object ("the Vaeli" / "them"), {cP} after "of" ("the Vaeli" /
// "theirs"), {cL} their lands ("the lands of the Vaeli" / "their lands"): a second clause about the same people
// says "they". `sig` weighs how much a fold is worth telling: a clause needs n x sig >= digestMin.
// (verify, chronicle-budget) Each group's clause is a family of lines rotated like every other family
// (pickTemplate, least used first): with one sentence and two or three [a|b] choices, a kind god's book wrote
// "Sarello answered the prayers of N towns of the Domafi" some 45 times and each digest frame 20-24 times.
// No line here has an inner ", and": the frames join two clauses with one.
const char* const kClAnswer[] = {
	"{g} answered the prayers of {n} towns of {cP}",
	"{n} towns of {cP} asked {g} for help and were answered",
	"{g} heard {n} towns of {cP} and answered them",
	"{g} gave {n} towns of {cP} what they prayed for",
	"the prayers of {n} towns of {cP} were heard",
	"{n} towns of {cP} prayed to {g} and did not pray in vain",
	"{g} was asked for help by {n} towns of {cP} and gave it",
	"{n} towns of {cP} had their prayers answered",
};
const char* const kClTake[] = { "{n} towns went over to {cO}", "{cS} gained {n} towns that had belonged to others", "{n} towns changed hands and were counted among {cO}" };
const char* const kClFamine[] = { "hunger came to {n} towns of {cP}", "{n} towns of {cP} went hungry", "{n} towns of {cP} knew lean years", "the granaries of {n} towns of {cP} ran low" };
const char* const kClBattle[] = { "{cS} lost {n} battles", "{cS} were beaten back {n} times", "{n} attacks by {cO} came to nothing" };
const char* const kClPlague[] = { "fever came to {n} towns of {cP}", "{n} towns of {cP} took sick", "sickness visited {n} towns of {cP}" };
const char* const kClLost[] = { "{n} bands of settlers of {cP} never arrived", "{n} bands of {cP} set out to found towns and were not heard of again" };
const char* const kClUnheard[] = { "{n} prayers of {cP} went unanswered", "{n} prayers of {cP} were not heard, or not answered", "{n} towns of {cP} prayed and heard nothing" };
const char* const kClRain[] = {
	"{g} sent rain over {n} towns of {cP}", "rain fell by the hand of {g} on {n} towns of {cP}", "{g} watered the fields of {n} towns of {cP}",
	"{n} towns of {cP} had rain from {g}", "the sky opened over {n} towns of {cP}",
};
const char* const kClBloom[] = {
	"{g} greened the fields of {n} towns of {cP}", "the fields of {n} towns of {cP} bloomed out of season", "{n} towns of {cP} saw their fields turn green at a touch of {g}",
	"{g} made the fields of {n} towns of {cP} bloom", "spring came early to {n} towns of {cP}, by the will of {g}",
};
const char* const kClShape[] = { "{g} moved the ground under {n} towns of {cP}", "the land shifted under {n} towns of {cP}" };
const char* const kClBolt[] = { "{g} threw lightning at {cL} {n} times", "lightning from {g} fell on {cL} {n} times" };
const char* const kClFound[] = { "{cS} founded {n} new settlements", "{cS} built {n} new villages in the wild", "{n} new settlements were founded by {cO}" };
const char* const kClEmpty[] = { "{cS} gave up {n} small places", "{n} hamlets of {cP} were left empty" };
const char* const kClShrink[] = { "{n} towns of {cP} dwindled", "{n} towns of {cP} grew smaller" };
const char* const kClHealed[] = { "the fever left {n} towns of {cP}", "{n} towns of {cP} got up from their sickbeds" };
const char* const kClWeather[] = { "fire, storm and drought troubled {cL} {n} times", "the weather was hard on {cO}, {n} times over", "{cL} weathered {n} storms, fires and droughts" };
const char* const kClGrow[] = { "{n} settlements of {cP} grew", "{n} places of {cP} outgrew their old names", "{n} villages and towns of {cP} grew larger" };
const char* const kClTrade[] = { "{cS} opened {n} new trade roads", "{cS} found {n} new roads for their traders", "merchants of {cP} opened {n} new roads" };
const char* const kClPray[] = { "{n} towns of {cP} prayed and waited", "{cS} sent up {n} new prayers", "{n} towns of {cP} turned to prayer" };
const char* const kClBuild[] = {
	"{cS} raised {n} new buildings", "{cS} put up {n} new buildings, each the finest yet by their own account",
	"{n} new buildings went up among {cO}", "the builders of {cP} finished {n} new halls, granaries and walls",
};
struct Clause { const char* group; double sig; Tpl t; };
const Clause kClauses[] = {
	{ "answer", 1.5, tpl(kClAnswer) },
	{ "take", 1.5, tpl(kClTake) },
	{ "famine", 1.3, tpl(kClFamine) },
	{ "battle", 1.3, tpl(kClBattle) },
	{ "plague", 1.3, tpl(kClPlague) },
	{ "lost", 1.2, tpl(kClLost) },
	{ "unheard", 1.2, tpl(kClUnheard) },
	{ "rain", 1.2, tpl(kClRain) },
	{ "bloom", 1.2, tpl(kClBloom) },
	{ "shape", 1.2, tpl(kClShape) },
	{ "bolt", 1.2, tpl(kClBolt) },
	{ "found", 1.0, tpl(kClFound) },
	{ "empty", 1.0, tpl(kClEmpty) },
	{ "shrink", 1.0, tpl(kClShrink) },
	{ "healed", 0.8, tpl(kClHealed) },
	{ "weather", 0.8, tpl(kClWeather) },
	{ "grow", 0.7, tpl(kClGrow) },
	{ "trade", 0.7, tpl(kClTrade) },
	{ "pray", 0.6, tpl(kClPray) },
	{ "build", 0.6, tpl(kClBuild) },
};
const Clause* clauseOf(const string& group) {
	for (const Clause& c : kClauses) if (group == c.group) return &c;
	return nullptr;
}
// the digest itself: {d1} the biggest fold, {d2} the next, {span} the years it covers ("thirty years")
const char* const kDigest1[] = {
	"In those {span} {d1}.",
	"Of the smaller things of those {span}: {d1}.",
	"Another {span} went into the book, and in them {d1}.",
	"The chronicle, which cannot write everything, notes only that in those {span} {d1}.",
	"Among the small news of those {span}: {d1}.",
	"Little of those {span} was written down, except that {d1}.",
	"Below the great events of those {span}, {d1}.",
	"The scribes, counting rather than writing, set down that in {span} {d1}.",
	"Between the greater deeds of those {span}, {d1}.",
	"{d1}. Of such small things in those {span} the chronicle keeps nothing more.",
	"And there were small things, as there always are: {d1}.",
	"Over {span}, and hardly worth a line each time, {d1}.",
};
const char* const kDigest2[] = {
	"In those {span} {d1}, and {d2}.",
	"Of the smaller things of those {span}: {d1}; and {d2}.",
	"Another {span} went into the book: {d1}, and {d2}.",
	"The chronicle, which cannot write everything, notes only that in those {span} {d1}, and {d2}.",
	"Those were busy years, if not great ones: {d1}, and {d2}.",
	"Meanwhile, in the smaller news of those years, {d1}, and {d2}.",
	"Below the great events of those {span}, {d1}; {d2}.",
	"Little of those {span} was written down, except that {d1}, and that {d2}.",
	"The scribes, counting rather than writing, set down that in {span} {d1}, and {d2}.",
	"{d1}; and {d2}. Of such small things in those {span} the chronicle keeps nothing more.",
	"Over {span} {d1}. In the same years {d2}.",
};
// (verify) the years a digest covers, in words
string spanWords(double years) {
	// (every frame reads "in those {span}", "another {span}", "over {span}": a plain count of years; folds wait at most
	// digestStale years, so the span stays under a century)
	static const char* const kTens[] = { "ten years", "ten years", "twenty years", "thirty years", "forty years", "fifty years",
		"sixty years", "seventy years", "eighty years", "ninety years" };
	const int t = int(std::lround(std::clamp(fin(years, 10), 10.0, 90.0) / 10.0));
	return kTens[std::clamp(t, 1, 9)];
}

// how many, in words (the prose never shows digits other than the year's ordinal)
string manyWords(int n) {
	static const char* const kW[] = { "no", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten", "eleven", "twelve" };
	if (n >= 0 && n <= 12) return kW[n];
	if (n <= 19) return "more than a dozen";
	if (n <= 30) return "a score or more";
	return "scores of";
}

long long decadeOf(const World& w) {
	const double span = tuneOf(w).digestYears > 0.5 ? tuneOf(w).digestYears : 10.0;
	return static_cast<long long>(std::floor(fin(w.year) / span));
}

// a bucket's tokens (a new world starts with full buckets)
double& bucket(World& w, const char* name, double burst) {
	const string k = string("chronicle_bk.") + name;
	auto it = w.vars.num.find(k);
	if (it == w.vars.num.end() || !std::isfinite(it->second)) { w.vars.num[k] = std::max(0.0, burst); it = w.vars.num.find(k); }
	return it->second;
}
void refill(World& w) {
	const ChronicleTune& T = tuneOf(w);
	const double year = fin(w.year);
	double& nb = bucket(w, "n", T.notableBurst);
	double& eb = bucket(w, "e", T.epochalBurst);
	double& mb = bucket(w, "m", T.minorBurst);
	double& sb = bucket(w, "s", T.storyBurst);
	auto it = w.vars.num.find("chronicle_bk.y");
	if (it == w.vars.num.end() || !std::isfinite(it->second) || year < it->second) { w.vars.num["chronicle_bk.y"] = year; return; }
	const double dec = (year - it->second) / (T.digestYears > 0.5 ? T.digestYears : 10.0);
	if (!(dec > 0)) return;
	nb = std::min(std::max(0.0, T.notableBurst), nb + std::max(0.0, T.notablePerDecade) * dec);
	eb = std::min(std::max(0.0, T.epochalBurst), eb + std::max(0.0, T.epochalPerDecade) * dec);
	mb = std::min(std::max(0.0, T.minorBurst), mb + std::max(0.0, T.minorPerDecade) * dec);
	sb = std::min(std::max(0.0, T.storyBurst), sb + std::max(0.0, T.storyPerDecade) * dec);
	it->second = year;
}
bool take(double& b) {
	if (b < 1 - 1e-9) return false;
	b -= 1;
	return true;
}

// (verify) one fold of a routine event into the coming digest ("chronicle_dg.s": the year of the first fold since
// the last digest, for the digest's span)
void foldInto(World& w, int culture, const char* group) {
	double& n = w.vars.num["chronicle_dg." + std::to_string(culture) + "." + group];
	n = fin(n) + 1;
	if (!w.vars.num.count("chronicle_dg.s")) w.vars.num["chronicle_dg.s"] = fin(w.year);
}

// The digest, written by the first entry (or the yearly updateEpithets) of a later decade: the biggest fold of
// one kind and people, and the next biggest, in one entry. (verify) At most one digest in digestEvery years: the
// folds wait and add up ("chronicle_dg.y" = the year of the last digest; a digest every decade made its frames
// and clauses the book's most repeated lines). Folds below digestMin wait too, until digestStale years old.
void flushDigest(World& w) {
	const ChronicleTune& T = tuneOf(w);
	if (!T.budget) return;
	const long long cur = decadeOf(w);
	const double year = fin(w.year);
	const auto d = w.vars.num.find("chronicle_dg.d");
	if (d != w.vars.num.end() && std::isfinite(d->second) && static_cast<long long>(d->second) == cur) return;
	{
		const auto ly = w.vars.num.find("chronicle_dg.y");
		if (ly != w.vars.num.end() && std::isfinite(ly->second) && year >= ly->second && year - ly->second < T.digestEvery - 1e-9) return;
	}
	w.vars.num["chronicle_dg.d"] = double(cur);
	const double span = std::max(1.0, T.digestYears);
	const auto s0 = w.vars.num.find("chronicle_dg.s");
	const double since = (s0 != w.vars.num.end() && std::isfinite(s0->second) && s0->second <= year) ? s0->second : year - span;
	const bool stale = year - since >= T.digestStale - 1e-9;
	struct Fold { int n; int cid; string group; double score; };
	std::vector<Fold> folds;
	const string pre = "chronicle_dg.";
	for (auto it = w.vars.num.lower_bound(pre); it != w.vars.num.end() && startsWith(it->first, pre); ++it) {
		const string rest = it->first.substr(pre.size());
		const size_t dot = rest.find('.');
		if (dot == string::npos) continue;
		const int cid = std::atoi(rest.substr(0, dot).c_str());
		const int n = std::isfinite(it->second) ? int(it->second) : 0;
		const Clause* cl = clauseOf(rest.substr(dot + 1));
		if (cl && n >= 2 && double(n) * cl->sig >= double(std::max(2, T.digestMin)) - 1e-9 && cult(w, cid) && !cult(w, cid)->dead)
			folds.push_back(Fold{ n, cid, rest.substr(dot + 1), double(n) * cl->sig });
	}
	if (folds.empty() && !stale) return;                     // nothing worth a line yet: the folds keep counting
	for (auto it = w.vars.num.lower_bound(pre); it != w.vars.num.end() && startsWith(it->first, pre);) {
		if (it->first.find('.', pre.size()) != string::npos) it = w.vars.num.erase(it);
		else ++it;
	}
	w.vars.num.erase("chronicle_dg.s");
	if (folds.empty()) return;
	w.vars.num["chronicle_dg.y"] = year;
	std::stable_sort(folds.begin(), folds.end(), [](const Fold& a, const Fold& b) {
		if (a.score != b.score) return a.score > b.score;
		if (a.n != b.n) return a.n > b.n;
		if (a.cid != b.cid) return a.cid < b.cid;
		return a.group < b.group;
	});
	ChronicleScratch& S = w.scratch<ChronicleScratch>();
	Rng r = w.rng.fork("chron:" + std::to_string(w.nextId.entry));
	auto clause = [&](const Fold& f, bool samePeople, const string& before) {
		const Clause* cl = clauseOf(f.group);
		if (!cl) return string();
		Ctx x;
		ctxCulture(x, w, f.cid);
		// (verify) a god the first clause already named is "the god" in the second
		{
			const string g = godOf(w, f.cid);
			if (!before.empty() && g.size() > 2 && before.find(g) != string::npos) x.set("g", "the god");
		}
		const string c = culturePlural(w, f.cid);
		x.set("cS", samePeople ? string("they") : c);
		x.set("cO", samePeople ? string("them") : c);
		x.set("cP", samePeople ? string("theirs") : c);
		x.set("cL", samePeople ? string("their lands") : "the lands of " + c);
		x.set("n", manyWords(f.n));
		const int k = pickTemplate(w, S, string("dg-") + cl->group, cl->t, r);
		string out;
		expandInto(out, cl->t.t[k], x, r, 0);
		return out;
	};
	const string d1 = clause(folds[0], false, string());
	if (d1.empty()) return;
	// the second clause: another people's fold of another kind if there is one, else another kind of thing of the
	// same people ("they ..."), else the same kind among another people
	size_t second = 0;
	for (int pass = 0; pass < 3 && !second; pass++)
		for (size_t i = 1; i < folds.size() && !second; i++) {
			const bool otherPeople = folds[i].cid != folds[0].cid, otherKind = folds[i].group != folds[0].group;
			if ((pass == 0 && otherPeople && otherKind) || (pass == 1 && otherKind) || (pass == 2 && otherPeople)) second = i;
		}
	const string d2 = second ? clause(folds[second], folds[second].cid == folds[0].cid, d1) : string();
	Ctx x;
	x.set("d1", d1);
	x.set("d2", d2);
	x.set("span", spanWords(std::floor(year / span) * span - std::floor(since / span) * span));
	const Tpl t = d2.empty() ? tpl(kDigest1) : tpl(kDigest2);
	const int idx = pickTemplate(w, S, d2.empty() ? "digest-1" : "digest-2", t, r);
	markUsed(S, t.t[idx]);
	string raw;
	expandInto(raw, t.t[idx], x, r, 0);
	chronicle::EntryOpts o;
	o.type = "digest";
	o.text = tidy(raw);
	o.culture = folds[0].cid;
	o.weight = 1;
	S.inDigest = true;
	chronicle::add(w, o);
	S.inDigest = false;
}

// The budget's verdict on an entry about to be written: its weight, or 0 (folded into the digest).
int budgetGate(World& w, const char* type, int weight, int culture, Tpl t, Tpl alt) {
	const ChronicleTune& T = tuneOf(w);
	ChronicleScratch& S = w.scratch<ChronicleScratch>();
	S.foldedLast = false;
	if (!T.budget || S.inDigest) { S.tellNext = false; return weight; }
	flushDigest(w);
	refill(w);
	const string_view ty(type);
	const char* group = groupOf(ty, weight);
	// tellable: the story's frequent notables (a town destroyed, wars and peace, a people's later age, prophets
	// and their edicts, the god's healing, inspiring, omens and sent plagues): never dropped, notable while
	// their own bucket allows
	const bool tellable = weight >= 2 && (S.tellNext || ty == "destroyed" || ty == "war" || ty == "peace" || (ty == "age" && weight == 2) ||
		ty == "prophet" || ty == "prophet-died" || ty == "edict" || ty == "miracle-heal" || ty == "miracle-inspire" ||
		ty == "miracle-peace" || ty == "wrath-plague");
	S.tellNext = false;
	// (verify) a people born or dead is epochal while the epochal bucket allows, else notable (never minor): five
	// peoples born in one Bronze age were 1.4 epochal entries a real minute
	const bool epochalOrNotable = ty == "culture-born" || ty == "culture-died";
	int wt = weight;
	if (wt >= 3) {
		double& eb = bucket(w, "e", T.epochalBurst);
		if (!take(eb) && (tellable || epochalOrNotable)) wt = 2;   // a once-a-world moment stays epochal (and spends a token if there is one)
	}
	if (wt == 2 && tellable) {
		double& sb = bucket(w, "s", T.storyBurst);
		if (!take(sb)) wt = 1;
	} else if (wt == 2 && group) {
		double& nb = bucket(w, "n", T.notableBurst);
		if (!take(nb)) wt = 1;
	}
	if (wt != 1) return wt;
	double& mb = bucket(w, "m", T.minorBurst);
	if (!group || !cult(w, culture)) return 1;                  // story and tellable minors are always written (and spend no token)
	const long long cur = decadeOf(w);
	const string mark = "chronicle_dt." + std::to_string(culture) + "." + string(ty);   // the first of its kind (type) for its people
	const auto it = w.vars.num.find(mark);
	const bool again = it != w.vars.num.end() && std::isfinite(it->second) && static_cast<long long>(it->second) == cur;
	const bool spent = familySpent(w, t, T.templateCap) && (!alt.t || familySpent(w, alt, T.templateCap));
	// the god's own small acts (answers, rain, bloom, moved ground, bolts) are the player's reply from the world:
	// once per kind and people a decade, but never crowded out by the world's own small news
	const bool godsAct = ty == "prayer-answered" || ty == "miracle-rain" || ty == "miracle-bloom" || ty == "miracle-shape" || ty == "wrath-lightning";
	if (again || spent || (!godsAct && !take(mb))) {
		foldInto(w, culture, group);
		w.vars.num["chronicle_f." + std::to_string(culture) + "." + string(ty)] = fin(w.year);
		w.vars.num["chronicle_fw." + string(ty)] = fin(w.year);
		S.foldedLast = true;
		return 0;
	}
	w.vars.num[mark] = double(cur);
	return 1;
}

// One entry from a template family (shuffle bag `key`), optionally followed by a suffix from a second
// family (bag `sfxKey`). (voice) With an `alt` family, the entry is taken from it instead with
// probability altP (shared voice lines beside an event's own). Every draw comes from the entry's own
// fork of w.rng.
ChronicleEntry* writeComposed(World& w, const char* type, int weight, int cell, int culture, Tpl t, const string& key,
	Tpl sfx, const string& sfxKey, const Ctx& x0, Tpl alt = Tpl{ nullptr, 0 }, const string& altKey = string(), double altP = 0,
	bool altSfx = false) {
	if (!t.t || t.n <= 0) return nullptr;
	ChronicleScratch& S = w.scratch<ChronicleScratch>();
	// (chronicle budget) the weight this entry may have, or 0: folded into the decade's digest
	weight = budgetGate(w, type, weight, culture, t, alt);
	if (weight <= 0) return nullptr;
	Rng r = w.rng.fork("chron:" + std::to_string(w.nextId.entry));
	Ctx x = x0;
	ctxYear(x, w);
	if (x0.get("noIN")) { x.set("IN", string()); x.set("IN,", string()); }   // (voice review 2) the caller knows the next entry opens with the year
	if (alt.t && alt.n > 0 && altP > 0 && r.chance(altP)) {
		// a shared family whose every line was used by one of the last entries gives way to the event's own
		bool fresh = false;
		for (int i = 0; i < alt.n; i++) if (!recentlyUsed(S, alt.t[i])) fresh = true;
		if (fresh) { t = alt; if (!altSfx) sfx = Tpl{ nullptr, 0 }; }
	}
	const string& famKey = (t.t == alt.t && alt.t) ? altKey : key;
	const int idx = pickTemplate(w, S, famKey, t, r);
	string body = t.t[idx];
	markUsed(S, t.t[idx]);
	if (sfx.t && sfx.n > 0) body += sfx.t[pickTemplate(w, S, sfxKey, sfx, r)];
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

// (voice review 2) An entry of this type at this cell within the last `gap` years?
bool toldAt(const World& w, string_view type, int cell, double gap) {
	if (!w.valid(cell)) return false;
	const double y = fin(w.year);
	int scanned = 0;
	for (size_t i = w.chronicle.size(); i-- > 0;) {
		const ChronicleEntry& e = w.chronicle[i];
		if (e.year < y - gap - 1e-9) break;
		if (++scanned > 5000) return false;
		if (e.cell == cell && e.type == type) return true;
	}
	return false;
}

// ====================================================================== templates
// Keys: {s} settlement, {st} its size word, {ast} the same with its article ("a town"), {t} terrain
// phrase at it ("on the plains"), {by} "by the water" where there is water (else {t}), {c} culture
// plural ("the Umari"), {ca} adjective, {cn} name, {g} the culture's name for the god, {ge} god +
// epithet ("Ashvel the Rain-Giver"), {ep} the epithet alone ("the Rain-Giver"), suffix 2 for a second
// settlement / culture, {w} a place phrase, {W} the world, {n} a person, {x} a thing, {want} what a
// prayer asks for, {old} / {e} epithets; every entry also has {yo} (the year as an ordinal) and the
// detachable openers {IN} / {IN,} (see ctxYear).
//
// (voice) The approved voice (docs/content/chronicle-voice.md, sample entries 1-60 and their variants,
// the two extras, the people:lost line and the six endings) is merged into these families; each voice
// line carries a "// vN" note with its number in that file. Its placeholders map to these keys:
//   {year_ord} {yo} (and the {IN} openers)   {culture_plural} {c}      {culture_adj} {ca}
//   {other_plural} {c2}                      {parent_plural} {c2}      {victor_plural} / {loser_plural} {c} / {c2} or {c2} / {c}
//   {god_name} {g}                           {god_epithet} {ep}        {new_epithet} / {old_epithet} {e} / {old}
//   {settlement} {s}                         {origin} {s2}             {other_settlement} {s2}
//   {tier} / {prev_tier} {ast} / {apt}       {building} {bna}          {wonder} {x}
//   {age} {age}                              {prophet} {n}             {prayer_kind} {want}
//   {war_years} {wy}                         {planet} {W}
// Voice lines are never shown with an empty placeholder: a line whose data may be missing sits in a
// family that is only used when the data exists (an origin, a war enemy, the razing army, ...).
// The voice's own rules applied to the older lines: no pronouns for the god (style rule 10), no
// opener "In the beginning" twice in a row at genesis.

// --- genesis
const char* const kGenWater[] = {
	"The fire went out of {W}, and its skin hardened into black stone. [The god waited.|The god looked upon it and was not yet content.]",
	"{W} cooled. Where there had been a sea of fire there was now only stone, [dry and smoking|cracked and silent], and the god turned to the sky.",
	"Before the counting of years {W} burned. Then it cooled, and the god began [the long work|the patient work] of making it a home.",
};
const char* const kGenLife[] = {
	"The deep places of {W} were filled with water, and the god turned to the bare land.",
	"There were seas on {W} now, [grey and restless|wide and cold], and the god bent over the shore to plant the first green.",
	"Water lay upon {W}, and the god said that it was enough, [and it was|and it was nearly].",
};
// (voice) once-a-world moments: the voice's own line is told about two times in three
const char* const kGenPeopleVoice[] = {
	"Then green came out of the wet places of {W}, slowly at first, as if it were not sure it was allowed.",   // v3
};
const char* const kCometFirstVoice[] = {
	"In the beginning {W} was fire and then stone, and the god called ice down out of the dark. Where it fell, the hollows filled, and that was the first sea.",   // v2
};
const char* const kAwakeningVoice[] = {
	"In the first year, before there was counting, {ep} touched the ground at {s}, and {c} woke. They looked up at the sky and did not yet know whom to thank, so they thanked everything.",   // v1
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
	"{IN}a star came down among {c}, and the sky was dark with dust until the harvest after next. They gave {g} a new name that year, and it was not a kind one.",   // v46
	"A star fell {w}, and the ground [rang like a bell|split open|became a bowl of fire].",
	"Fire came down out of a clear sky {w}. [{c} saw it from {s} and fell on their faces.|It was seen as far as {s}.]",
	"{g} threw down a burning stone {w}, and {c} [did not sleep for many nights|counted the dead and feared the sky].",
	"The sky opened {w} and spat out a mountain of fire. {c} said it was the anger of {g}.",
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
	"{ep} threw fire from a clear sky at {s}. No one knew why. {c} made more offerings, to be safe.",   // v45
	"{g} struck the ground {wd} with lightning[, and the people of {s} hid their faces|; {c} counted it a warning].",   // (voice review 2) {wd}: never "struck the ground upon Nish"
	"Lightning fell {w} out of a clear sky. {c} said {g} was [angry|watching|not pleased].",
	"A spear of light came down {w}, and the {ca} elders argued for a long time about what it meant.",
	"There was no cloud in the sky, and still the lightning fell {w}. [{c} knew whose hand had thrown it.|In {s} they spoke of it in whispers.]",
	"A bolt of white fire struck {wd} at noon. {c} said it was {g} clearing the throat of heaven.",
	// (voice review) the cruel god throws a great deal of lightning: three more in the same voice
	"{g} spoke in fire over {s}. No one understood the word, but everyone heard it.",
	"Lightning came down at {s} on a windless day. The {ca} priests said it was a warning; the farmers said it was a waste of a good tree.",
	"Out of a blue sky a white spear fell {w}[, and split the old well-stone|, and the cattle would not graze there after]. {c} said nothing, and brought more bread to the shrine.",
	// (voice review 2) a cruel god's book is a sixth lightning: three more, so no sentence comes back thirty times
	"Fire came down from a clear sky {w}[, and the {ca} priests put ashes on their heads|, and no one in {s} slept that night].",
	"The god's fire fell {w}. In {s} the mothers kept the children indoors, and the dogs hid under the beds.",
	"The sky struck {s} without a cloud in it, and the old people said the god had a long memory.",
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
	"The sea drew back from the shore at {s}, as if to look at something, and then came running. Those who went down to gather the stranded fish did not come back up.",   // v56
	"A wall of water came out of the sea upon {s}.",
	"The sea rose up and walked inland at {s}, [taking boats and houses and cattle|and did not ask leave].",
	"A great wave broke {w}, and the people of {s} [ran for the hills|counted their dead].",
	"A grey wave as tall as a temple broke over the fields of {s}, and salt lay on them for years.",
};
const char* const kVolcanoGod[] = {
	"{ep} split the earth near {s}, and a mountain was born in one summer, roaring like a calf. Its smoke stood over the land for twelve years.",   // v47
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
	"The earth shrugged under {s}. Pots fell, then roofs, then the walls. {c} asked what they had done. No answer came, which was itself an answer.",   // v48
	"{g} shook the earth {w}, and the houses of {s} fell down.",
	"The ground {w} heaved like the sea. {c} said {g} had stamped upon the earth.",
	"At the touch of the god the hills {wd} [cracked|shuddered], and {s} [was shaken to its roots|lost many houses].",
	"{g} struck the earth {w}, and the land rang like a drum. [In {s} the roofs came down.|{c} fell on their faces.]",
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
// (voice review 2) a fire the chronicle has just seen lightning start nearby: no line names a cause again
// ("lightning set the grass alight" and then "the old people said a falling star had started it")
const char* const kFireLit[] = {
	"Fire ran through {x} {wd}.",
	"The country {w} burned [for many days|until the rain came], and the smoke hung over {s}.",
	"A wildfire came close to {s}, [and the people beat it back with green branches|and turned only when the wind did|and the children watched it from the roofs].",
	"{x} {wd} burned, and [{c} watched the glow at night|the herds fled toward {s}|the sky over {s} was brown for days].",
	"[That summer|In a dry season|In the month of dust] a fire ate {x} {wd}.",
	"A red night: {x} {wd} burned, and ash fell on {s}.",
	"Fire walked through {x} {wd}, [and the beasts ran before it|and left only ash behind it|and {c} prayed for rain].",
};
// a fire in woods or forest: the voice's line joins the family
const char* const kFireForest[] = {
	"Lightning found {x} {wd}, and the forest burned for a season. In the spring, from the ash, it began again.",   // v55
	"Fire ran through {x} {wd}.",
	"A wildfire came close to {s}, [and the people beat it back with green branches|and turned only when the wind did|and the children watched it from the roofs].",
	"{x} {wd} burned, and [{c} watched the glow at night|the herds fled toward {s}|the sky over {s} was brown for days].",
	"[That summer|In a dry season|In the month of dust] a fire ate {x} {wd}.",
	"A red night: {x} {wd} burned, and ash fell on {s}.",
	"Fire walked through {x} {wd}, [and the beasts ran before it|and left only ash behind it|and {c} prayed for rain].",
	"The old people of {s} said the fire {w} had been started by [a careless shepherd|the god|a falling star], [and argued about it all winter|and kept their buckets full].",
};
const char* const kStorm[] = {
	"A great storm walked in from the sea and stood over {s} for three days, and the roofs learned to fly.",   // v53
	"Wind and rain beat upon {s} [for three days|for a week], [and the roofs flew|and the rivers rose|and the boats were thrown up on the fields].",
	"A storm such as the old people had never seen passed over {s}.",
	"The sea sent a howling wind against {s}, [and it took the thatch from every roof|and the fishermen did not go out for a month].",
	"Black clouds came over {s} from the sea, [and the hail broke the young barley|and the river ran brown for days].",
	"In {s} they still tell of the great wind, [which carried a goat over the temple|which blew the fires out in every hearth|when even the priests were afraid].",
	"A storm walked inland over the country of {s}, [and {c} counted their dead and their cattle|and left the fields flattened behind it].",
};
const char* const kDroughtStart[] = {
	"No rain fell on the lands of {s}. The river showed its stones, and the people counted them.",   // v54
	"The rains failed around {s}.",
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
	"{ep} drew a hand across the sky above {s}, and the dry fields drank.",   // v42
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
// (voice review 2) the same where the town has prayed lately: the rain was asked for
const char* const kRainSoftAsked[] = {
	"Soft rain fell over {s}.",
	"[It rained on {s}, gently and for a long time|A long gentle rain came to {s}], and {c} [said it was a blessing|did not complain].",
	"Rain came to {s}. [The old people said it smelled of the god.|The fields were glad of it.]",
	"A warm rain fell on {s} [all through the night|for three days], and the roofs leaked, and nobody minded.",
	"Mist and rain came to {s}, and the hills around it [turned a deeper green|ran with little streams].",
};
const char* const kBloom[] = {
	"Where {ep} passed near {s}, the land went green out of season, and the old women said the trees had heard a rumour.",   // v43
	"The fields about {s} grew green [overnight|as if in a dream].",
	"{g} breathed on the land around {s}, and it flowered.",
	"Around {s} the [grass grew to a man's waist|trees bent with fruit], and {c} said {g} had walked there.",
	"The land of {s} [bloomed|blossomed] out of season, and the beekeepers grew rich.",
	"Near {s}, at the touch of the god, [seeds woke in the dry ground|flowers opened in the snow|the orchards fruited twice].",
	"The herds about {s} grew fat on grass that had not been there the week before.",
};
const char* const kHeal[] = {
	"{ep} laid a hand on {s}, and the sick rose from their beds, puzzled, and asked what there was to eat.",   // v50
	"The fever left {s} in a single night. {c} said {g} had walked through the streets.",
	"In {s} the sick were healed, [all at once|between one breath and the next], and the healers lost their trade for a while.",
	"A cool wind passed through {s}, and in every house the fevered slept and woke well.",
	"{s} was healed by {g}. [Its people carved a hand on the temple wall.|For a generation no one there feared the sickness.]",
};
const char* const kInspire[] = {
	"At {s} the craftsmen woke with ideas they could not explain, and claimed them as their own.",   // v51
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
// the omen seen by a people at war: {c2} is the enemy they were fighting
const char* const kOmenPeaceWar[] = {
	"A sign appeared over the armies of {c} and {c2}, and both sides lowered their spears to look at it. When they looked down again, neither remembered why they had come.",   // v52
	"An omen of peace was seen over {s}, and {c} and {c2} laid down their spears.",
	"[A white light|A great calm] lay between the armies of {c} and {c2} for a day, and both sent envoys instead of soldiers.",
};
const char* const kShapeHarm[] = {
	"{ep} took hold of the land near {s} and moved it, the way a sleeper moves a blanket. The farmers measured their fields again and did not like the answer.",   // v44
	"The ground heaved under {s} at the touch of the god.",
	"{g} reshaped the land under {s} while its people slept, and they woke to find their fields [tilted|broken].",
	"The earth moved under {s} like dough under a baker's hand. {c} did not find it amusing.",
};
const char* const kRaise[] = {
	"{ep} took hold of the land near {s} and moved it, the way a sleeper moves a blanket. The farmers measured their fields again and did not like the answer.",   // v44
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
	"{ep} breathed on {s}, and the breath was sick. The priests said it was a punishment. They were not sure for what, so they punished everything.",   // v49
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
// settlers whose home town is known ({s2}: the settlement the settler band set out from)
const char* const kFounded[] = {
	"{IN,}a band went out from {s2} with seed corn and quarrels enough for a new town, and founded {s} {by}.",   // v4a
	"Some of the people of {s2} grew tired of their neighbours' faces. They walked until the faces around them were their own again, and there they built {s}.",   // v4b
	"{s} was founded in the {yo} year. Its founders swore it would one day be greater than {s2}. The chronicle notes that all founders swear this.",   // v4c
	"Settlers of {c} went out from {s2} and made a new home at {s}.",
	"A band from {s2} crossed the country and founded {s} {t}.",
	"Families of {c} who left {s2} built their first fires at {s}, [and called it good|and were not sorry].",
	"{s} was founded {t} by {c}, who had grown too many for {s2}.",
	"[It is written that |]{s} began as a ring of tents {t}, raised by wanderers from {s2}.",
	"Wanderers from {s2} came to {s} [and stayed|and did not go back].",
	"A new village of {c}, {s}, was raised {t}, [a few days' walk from {s2}|where the hunters of {s2} had always camped].",
};
const char* const kFoundedLone[] = {
	"{c} founded {s} {t}.",
	"{IN}a new camp of {c} was raised at {s}, {t}.",
	"Wanderers of {c} settled at {s}, {t}.",
	"Far from their kin, a band of {c} made a home at {s}, {t}.",
	"{s} was founded {t} by {c}, [who had walked a long way to find it|though no one remembered why there].",
};
// (voice 5a-c) growth in any size: drawn instead of the size's own lines about one time in three
const char* const kTierVoice[] = {
	"{IN}{s} could no longer call itself {apt}. It was {ast} now, and its people began to walk a little differently.",   // v5a
	"The houses of {s} reached the old boundary stones and did not stop. From this year {c} call it {ast}.",   // v5b
	"So many children were born in {s} that the elders gave up learning their names. Thus it became {ast}.",   // v5c
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
	"{s} grew into a city of towers, [its lights seen from the next valley|so loud that the priests said the god could hear it].",
	"{s} became a great city, a sea of roofs from one horizon to the other.",
	"The lamps of {s} drowned out the stars: it had become a great city of {c}.",
};
const char* const kTierDown[] = {
	"{s} dwindled to {ast}. Its streets were too wide for the people left in them, and grass came up between the stones.",   // v6
	"Many left {s}, and it was {ast} again.",
	"{s} grew small and was only {ast}, [and the old people remembered better days|and the young went away].",
	"Half the houses of {s} stood empty, and it was no more than {ast}.",
	"{s} shrank to {ast}. [Its young had gone to other places.|Its walls were too big for it now.]",
};
// (voice 7a-c) any ordinary building ({bna}: "a granary", "an academy"): drawn instead of the
// building's own lines about one time in three
const char* const kBuildVoice[] = {
	"{IN}{c} raised {bna} at {s}, and were proud of it for one generation, which is the usual length of pride.",   // v7a
	"At {s} they built {bna}. It took longer than promised and cost more than was said, as is the way of all building.",   // v7b
	"{bna} now stands at {s}, where before there was only mud and opinion.",   // v7c
};
const char* const kBShrine[] = {
	"At {s} they set stones in a ring and laid the first fruits on them for {g}. It was a small shrine. {ep} did not complain.",   // v8
	"The people of {s} raised a shrine to {g}.",
	"At {s} a shrine of [stones|wood|bones and flowers] was made for {g}.",
	"{c} set up a shrine at {s}, and left [bread|flowers|small carved birds] there for the god.",
	"A circle of white stones was laid out at {s}, and {c} came there to speak to {g}.",
	"{s} built a small shrine on the hill above the fields, [and the first thing laid on it was a child's toy|and kept a fire burning in it].",
	"The elders of {s} chose a holy place, and a shrine to {g} was raised there.",
};
const char* const kBShrineFirst[] = {
	"At {s} they set stones in a ring and laid the first fruits on them for {g}. It was a small shrine. {ep} did not complain.",   // v8
	"At {s}, {c} raised their first shrine to {g}, [a ring of stones|a heap of white stones] where they could speak to the sky.",
	"{c} made their first shrine at {s}, and gave the god who had woken them a name: {g}.",
};
// (voice review 2) a daughter people: the god woke their fathers, and they kept or changed the name ({gk})
const char* const kBShrineFirstDaughter[] = {
	"At {s} they set stones in a ring and laid the first fruits on them for {g}. It was a small shrine. {ep} did not complain.",   // v8
	"At {s}, {c} raised their first shrine to {g}, [a ring of stones|a heap of white stones] where they could speak to the sky.",
	"{c} made their first shrine at {s}, {gk}",
};
const char* const kBTemple[] = {
	"{c} of {s} built a temple to {g} so tall that the priests must rest twice on the stair, and they said that this was the point.",   // v9
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
	"{s} built walls. No one said against whom. Everyone knew.",   // v10
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
	"Outside {s}, on flat ground swept clean, the {ca} builders raised a tower of iron and a tower of fire beside it, and pointed them both at the sky.",   // v11
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
	"{IN}the granaries of {s} rang hollow when struck. The people ate the seed corn, and then the stories of better years.",   // v12a
	"Hunger came to {s} and sat down at every table. Many were carried out to the fields that could no longer feed them.",   // v12b
	"There was famine in {s}. The people looked up and asked {ep} for rain, and counted the days, and the days were long.",   // v12c
	"Famine came to {s}.",
	"The harvest failed at {s}[, and the people ate their seed corn|; the children grew thin].",
	"At {s} there was hunger, three years running.",
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
	// (voice review) the dark years come to many peoples at once: more ways to tell them
	"At {s} the summer was only a rumour that year. The barley stood green in the cold until it rotted, and {c} went hungry.",
	"The dust of the sky fell on the fields of {s} like a second winter, and there was nothing to harvest but straw.",
	"In {s} they lit fires at noon to see by, and ate the last of the seed, and waited for a sun that did not come.",
};
const char* const kPlague[] = {
	"A sickness came to {s} by the river road. It went from house to house like a guest no one had invited and no one could turn away.",   // v13
	"Fever spread through {s}, [and the priests could do nothing|and many were buried].",
	"In {s} the people sickened, and the roads to it were empty.",
	"A coughing sickness came to {s} [with the traders|in the spring], and [lingered|did not leave].",
	"The spotted fever broke out in {s}, [and its gates were shut|and its neighbours turned travellers away].",
	"In {s} they painted marks on the doors of the sick, [and there were many marks|and the marks spread from street to street].",
	"A grey sickness settled on {s}. [The healers burned herbs day and night.|{c} said it came from the marshes.]",
};
// (voice 14) the end of a plague whose coming was written down
const char* const kPlagueEnd[] = {
	"The plague left {s} as it had come, without asking. The living swept their doorsteps and were careful, for a while, to be kind.",   // v14
	"The sickness went out of {s} at last, and the bells rang for the living as well as the dead.",
	"In {s} the fever burned itself out. The survivors counted one another, did not like the count, and went back to work.",
};

// destroyed, by cause
const char* const kDDrowned[] = {
	"The sea rose at {s} by a hand's width a year, and then by another. In the {yo} year the last of its people rowed away, and the fish moved into the houses.",   // v15
	"{s} was swallowed by the sea.",
	"The waters rose over {s}, and its people fled to higher ground.",
	"Where {s} stood, fish now swim.",
	"{s} drowned, [and its bells could be heard under the waves, the sailors said|and the sea kept it].",
};
const char* const kDMeteor[] = {
	"Fire fell on {s}. Where it stood there is now a round hollow, and {c} do not go near it, and they do not say its name.",   // v16
	"{s} was struck from the sky and was no more.",
	"A falling star made {s} into a pit of glass.",
};
const char* const kDLava[] = {
	"The mountain opened above {s} and its stone ran like honey, though not like honey. The {st} is beneath it. The new ground is very fertile, the chronicle observes, as though that were a comfort.",   // v17
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
// razed by an army whose people is known ({c2}: the attackers of the battle that took it)
const char* const kDWarBy[] = {
	"{s} was taken by {c2} and burned, and its name was struck from the lists of {c}. The crows kept no lists.",   // v18
	"The army of {c2} came to {s} and left only smoke.",
	"{s} fell to {c2} and was razed, [and salt sown in its fields|and the crows grew fat].",
	"{c2} put {s} to the torch, and its people were carried off or killed.",
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
// a quake on the coast: the sea follows the shaking
const char* const kDQuakeCoast[] = {
	"The ground shook at {s} and the walls lay down. What the shaking spared, the sea came in to take.",   // v19
	"The earth opened under {s}.",
	"{s} was shaken to rubble, [and the survivors would not rebuild it|and its ruins were left as a warning].",
};
const char* const kDNuke[] = {
	"{s} vanished in a light brighter than the sun.",
	"A second sun rose over {s}, and then there was no {s}.",
	"{s} was burned away in a single breath, [and the shadows of its people stayed on the stones|and the sky rained ash for a year].",
};
// the strike itself was just told: only what was left
const char* const kDNukeAfter[] = {
	"When the light faded, {s} was gone.",
	"Nothing of {s} was left standing but shadows on the stones.",
	"{s} did not rise again from it. The ash fell for a year.",
};
const char* const kDAbandoned[] = {
	"No one destroyed {s}. One by one its people left for somewhere better, until the last of them shut the gate behind out of habit.",   // v20
	"{s} was abandoned.",
	"The last families left {s}, and grass grew in its streets.",
	"{c} gave up {s}, [and the wind had it|and the foxes moved in].",
	"{s} was left empty. [No one could say exactly when.|Its people had gone to the bigger towns.]",
};
// (finding 7) the sea covered a town where the god had just lowered the land (or shaken it, or struck it)
const char* const kDLowered[] = {
	"{g} pressed the land down under {s} like a thumb in clay, and the sea came in to see what had happened.",
	"The ground sank under {s} at the touch of the god, and the sea poured into its streets. Its people did not need to be told whose hand it was.",
	"{s} went down into the sea where {g} had lowered the land. {c} rowed away and looked back only once.",
	"The god leaned on the land at {s}, and the sea, which is always waiting, came over the walls.",
};
// (finding 7) the sea rose over a town on its own (no god's hand near it, no engines to blame)
const char* const kDSeaRose[] = {
	"The sea rose at {s} by a hand's width a year, and then by another. In the {yo} year the last of its people rowed away, and the fish moved into the houses.",   // v15
	"The sea came up the shore at {s} a little every year, and its people moved their doors uphill until there was no more uphill.",
	"{s} was given back to the sea, slowly, the way the sea takes things. No one thought to blame the god for it.",
};
// (finding 7) the sea the engines raised ({c2}: the people whose chimneys burn the most, not the drowned)
const char* const kDSeaEngines[] = {
	"The smoke of the {ca2} engines warmed the air, and the sea came up to meet it. At {s} it did not stop.",
	"{s} drowned in a sea that {c2} had warmed with their chimneys. Its people rowed away cursing the smoke, not the sky.",
	"The sea rose over {s}. The scholars of {c2} had measured it coming for years, and went on burning coal while they measured.",
	"Nobody struck {s}. The engines of {c2} breathed out, the ice breathed in, and the water came up the streets a hand's width a year.",
};
// (finding 7) ... and when the drowned people burn the most themselves
const char* const kDSeaEnginesOwn[] = {
	"The smoke of their own engines warmed the air, and the sea came up to meet it. {c} left {s} to the fish and went on building chimneys elsewhere.",
	"{s} drowned in a sea that {c} had warmed with their own chimneys. The priests blamed the god; the scholars, quietly, did not.",
	"The water came up the streets of {s} a hand's width a year, as the chimneys of {c} had invited it to.",
};
const char* const kDWave[] = {
	"The ground shook at {s} and the walls lay down. What the shaking spared, the sea came in to take.",   // v19
	"A great wave carried {s} out to sea.",
	"The sea stood up and fell on {s}, and when it drew back, {s} was gone.",
};
const char* const kDLightning[] = {
	"Lightning set {s} ablaze, and it burned to nothing.",
	"{s} burned after a thunderstorm, [roof by roof|in a single night].",
};
const char* const kCapturedWar[] = {
	"{IN}{s} opened its gates to {c2}, having run out of other ideas. It is theirs now, and pays its taxes in their coin.",   // v21
	"{c2} took {s} from {c}.",
	"{s} fell to {c2}[, and its people learned new songs|, and its {ca} lords were driven out].",
	"The banners of {c2} were raised over {s}.",
	"{s} was lost by {c} and won by {c2}, [{tg}|and its market changed hands with it].",   // {tg}: a new god's name for its temple only when the winners call the god otherwise
	"{c2} marched into {s}, [and the {ca} families who could fled to their kin|and the town paid tribute from that day].",
};
const char* const kCapturedSchism[] = {
	"{s} turned from {c} and joined {c2}.",
	"The people of {s} no longer called themselves {cn}; they were {c2} now.",
};
// a besieged settlement held: {c} the attackers, {c2} the defenders (who won)
const char* const kBattleLost[] = {
	"Before {s}, {c} met {c2}. By evening the field belonged to {c2}, and to the birds.",   // v28a
	"At {s} {c2} won the day. {c} tell it differently, in their own chronicle, which no one has asked to read.",   // v28b
	"The host of {c} came against {s} and broke on its walls like a wave on rock, and went home fewer.",   // v28c
	"{c} came against {s} and were thrown back.",
	"Before {s}, the army of {c} was broken.",
	"{c2} held {s} against {c}, [and the field was red|and sang about it for a hundred years].",
	"An army of {c} besieged {s} and went home with nothing [but its wounded|but stories].",
	"{c} attacked {s} [at dawn|in the rain|on a holy day] and were beaten [by its farmers|by its old men and boys|in the river mud].",
};
const char* const kPrayer[] = {
	"The people of {s} climbed to the shrine and prayed to {g} for {want}. They prayed loudly, in case {ep} was far away.",   // v39
	"The people of {s} prayed for {want}.",
	"In {s} they burned [incense|fat|sweet grass] and asked {g} for {want}.",
	"The priests of {s} went up to the high places and cried out for {want}.",
	"At {s}, {c} prayed to {g} for {want}, [morning and evening|and fasted].",
	"The women of {s} [tied red threads to the shrine|sang at the shrine every evening] and asked for {want}.",
	"In {s} a [white goat|lamb|jar of honey] was given to {g}, with a prayer for {want}.",
	"{c} of {s} wrote their prayer for {want} on [clay|bark] and buried it at the shrine.",
};
// {py}: how long they prayed, in words ("fifteen years")
const char* const kPrayerLost[] = {
	"For {py} {s} prayed for {want}, and the sky said nothing. At last they stopped asking. Some stopped believing. The shrine grew moss.",   // v41
	"The people of {s} stopped praying for {want}; {g} had not answered them.",
	"In {s} they said the god was deaf, and [stopped asking|let the shrine fall into ruin].",
	"{s} gave up asking {g} for {want}. [Its people grew bitter.|They learned to manage without.]",
	"The prayers of {s} went up for {py} and nothing came down. [{c} began to wonder whether anyone was listening.|The shrine there was left without flowers.]",
};
const char* const kAnsRain[] = {
	"{s} prayed for rain, and it was given. The priests were as surprised as anyone, but hid it well.",   // v40
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
	"{s} prayed for healing, and it was given. The priests were as surprised as anyone, but hid it well.",   // v40
	"{g} heard the sick of {s}, and the fever broke.",
	"The people of {s} prayed to be healed, and they were, [and the temple was full for a year|and they did not forget it].",
	"In {s} the sick had prayed, and one morning they woke well.",
	"The healers of {s} had given up, but {g} had not. The fever left the town in a single night.",
	"{s} prayed against the sickness, and {g} [took it away like a cloth from a table|laid a cool hand on every brow].",
	"The sick of {s} were carried to the shrine, and {g} [walked out of it with them|sent them home on their own feet].",
};
const char* const kAnsPeace[] = {
	"{s} prayed for peace, and it was given. The priests were as surprised as anyone, but hid it well.",   // v40
	"{c} prayed for peace, and {g} gave it.",
	"The prayers of {s} for peace were answered, [and the soldiers came home|and the war ended as if a door had closed].",
	"{s} had prayed for an end to the war, and the god heard. The spears were hung on the walls again.",
	"The mothers of {s} prayed for peace, and {g} [sent it|put a quiet on both armies].",
	"{c} had asked {g} to end the war, and the god put a weariness on both armies until they went home.",
};
const char* const kAnsBloom[] = {
	"{s} prayed for green fields, and they were given. The priests were as surprised as anyone, but hid it well.",   // v40
	"{g} answered {s}, and the burnt fields grew green.",
	"The people of {s} had prayed over their blackened fields, and now the fields were green, [and they wept|and they sang].",
	"Where the ground about {s} had been ash, flowers came up in a single week, as the people had prayed.",
	"{s} asked {g} to heal the burnt land, and green shoots answered them.",
	"Over the black fields of {s} the god breathed, as its people had begged, and the ash turned to grass.",
};
const char* const kCultureDistance[] = {
	"From {c2} came {c}, as a branch comes from a tree: the same wood, pointing elsewhere.",   // v23
	"Far from their old capital, the people about {s} began to call themselves {c}[, and spoke a tongue {c2} could barely follow|, and paid no more taxes to {c2}].",
	"In the lands about {s} a new people arose, {c}, [children of {c2}|descended from {c2}] but no longer their subjects.",
	"The {ca2} settlers about {s} had been apart too long. They took a new name, {cn}, and went their own way.",
};
const char* const kCultureSchism[] = {
	"The people of the far valleys about {s} spoke the old tongue so strangely that the capital laughed at them. In the {yo} year they stopped coming to be laughed at, and called themselves {c}.",   // v22
	"{c} broke away from {c2}, [saying that the {ca2} priests had forgotten {g2}|over a quarrel about the god].",
	"There was a schism among {c2}: the faithful of {s} called themselves {c} and would not bow to the old capital.",
	"{c} were born of a quarrel. They had been {c2}, until they disagreed about {g2}.",
};
const char* const kCultureDied[] = {
	"{c} are no more. Their words live on in the mouths of their neighbours, who use them for cursing and for the names of rivers.",   // v24
	"The last of {c} passed from the world, [and their songs with them|and only their ruins remember them].",
	"Of {c}, who once held {s}, nothing remained but names.",
	"{c} [faded|dwindled] and were gone, [and other peoples walked in their streets|and no one spoke their tongue any longer].",
};
// (voice 25a-c) any age ({age}: "Iron", "Sail", "the Atom"): drawn instead of the age's own lines
// about one time in three
const char* const kAgeVoice[] = {
	"{IN}{c} entered the Age of {age}. The grandfathers said it was a mistake. The grandchildren did not hear them over the noise.",   // v25a
	"A new thing was learned at {s}, and then everywhere. So the Age of {age} began among {c}.",   // v25b
	"{c} put down the tools of their mothers and took up the tools of the Age of {age}. {ep} watched, and did not say whether this was wise.",   // v25c
};
// (voice 26) the first people of the world to reach an age: drawn instead of the age's line + "first" suffix half the time
const char* const kAgeWorldFirst[] = {
	"{IN}the world turned a page: {c}, first of all peoples, came into the Age of {age}, and the others began to hurry.",   // v26
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
	"{IN}{c} remembered an old insult from {c2}, and went to war over it.",   // v27a
	"{c} and {c2} could no longer share the river. They tried to share it with spears instead.",   // v27b
	"War between {c} and {c2}. Both sides prayed to the same sky for victory; the chronicle does not record which prayer was heard.",   // v27c
	"{c} made war on {c2}.",
	"{c} marched against {c2}, [and the fields were left unharvested|and the smiths worked day and night].",
	"The {ca} kings swore an oath against {c2}, and there was war.",
	"The long quarrel between {c} and {c2} came to blood.",
	"Envoys of {c} [threw down their staffs|spat on the threshold] before {c2}, and the war began.",
	"{c} sharpened their spears and went against {c2}, [saying the god was with them|though no one could remember who had struck first].",
	"War came between {c} and {c2}, [the old enemies|who had once traded salt and songs].",
};
const char* const kWarDraw[] = {
	"The war of {c} and {c2} ended because both had run out of sons. Each side wrote down that it had won.",   // v30
	"The war between {c} and {c2} ended, and neither could say it had won.",
	"After much killing {c} and {c2} agreed that the border was where it had always been.",
	"The war of {c} and {c2} wore itself out, like a fire with nothing left to burn.",
	"{c} and {c2} laid down their arms. [Nothing had changed but the number of graves.|The poets of both peoples wrote songs of triumph.]",
};
// (voice review 2) the beaten people died with the war: no one was left to ask for peace or pay tribute
const char* const kWarWonGone[] = {
	"The war ended when there were none of {c2} left to fight it.",
	"{c} had won: of {c2} there was no one left to ask for peace.",
	"{c} won their war against {c2}, and there was no one left to pay the tribute.",
};
// {c} the victors, {c2} the beaten; {wy} how long the war lasted, in words
const char* const kWarWon[] = {
	"After {wy} {c2} asked for peace, and {c} granted it, and made them pay for the asking.",   // v29
	"{c} won their war against {c2}.",
	"The war ended in victory for {c}. [{c2} paid tribute for a generation.|In {s} there were feasts for a month.]",
	"{c2} were beaten, and {c} [took their best fields|carried their temple bells home in carts].",
	"The armies of {c} came home in triumph to {s}, and {c2} [counted what they had lost|swore to remember].",
};
const char* const kTrade[] = {
	"{IN}the first caravan came from {s} to {s2}, carrying salt, dyed cloth and gossip. Of the three, the gossip sold best.",   // v31a
	"The feet of traders wore a road between {s} and {s2}. Along it went pots and coins and, later, fevers.",   // v31b
	"Merchants began to travel between {s} and {s2}, [and the road between them grew wide|bringing salt and news].",
	"Ox-carts began to creak along the road from {s} to {s2}, [loaded with grain and gossip|and inns grew up beside it].",
	"{s} and {s2} began to trade, [cloth for copper|pots for fish|wine for wool], and both grew fat on it.",
	"The merchants of {s} found a road to {s2}, [and kept the secret of it for a while|and the road found more merchants].",
};
const char* const kTradeForeign[] = {
	"Merchants of {s} reached {s2} and found that the people there were also people. This was thought remarkable, and then useful.",   // v31c
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
	"{IN}a prophet named {n} stood up in the market of {s} and said that {g} had spoken. Some believed. The rest moved their stalls.",   // v32
	"A prophet named {n} arose in {s}, saying that {g} had spoken in the night.",
	"{n} of {s} heard the voice of {g} and began to preach, [and many believed|and some threw stones].",
	"In {s} a prophet appeared, {n}, [who walked barefoot and spoke of the god|who could not be silenced], and {c} listened.",
	"{n}, a [shepherd|potter|fisher|scribe] of {s}, came down from the hills with a message from {g}.",
	"A voice cried out in the market of {s}: it was {n}, who said the god had chosen {c}.",
};
const char* const kProphetDied[] = {
	"{n} the prophet died, and was buried facing the sunrise. {c} argued over every word {n} had said, which is how they knew the words were holy.",   // v33
	"The prophet {n} was gathered to {g}.",
	"{n}, who had spoken for the god among {c}, [was buried with great honour|died, and the disciples quarrelled at once].",
	"{n} the prophet went into the hills and did not come back. {c} said {g} had taken the prophet home.",
};
const char* const kEdictPeace[] = {
	"{n} spoke for {ep}: \"Lay down the spear.\" And {c} laid it down, though somewhere they could reach it.",   // v34
	"{n} commanded {c} to lay down their spears.",
	"By the word of {n}, {c} swore to make no more wars [while the prophet lived|in the name of {g}].",
};
const char* const kEdictExpand[] = {
	"{n} spoke for {ep}: \"Go out and fill the land.\" The young of {c} took this as permission, which it was.",   // v35
	"{n} told {c} to go out and fill the land.",
	"At the word of {n}, the young of {c} left their homes to found new ones.",
};
const char* const kEdictBuild[] = {
	"{n} spoke for {ep}: \"Build a thing that can be seen from the sky.\" And {c} began to quarry the hills.",   // v36
	"{n} commanded {c} to build a wonder worthy of {g}.",
	"{n} told {c} that {g} wished for a great work, and {c} began to cut timber and haul stone.",
};
// {c2}: the people the crusade is aimed at (the least devout people in contact), else "their neighbours"
const char* const kEdictCrusade[] = {
	"{n} spoke for {ep}: \"{c2} pray badly.\" {c} sharpened their swords to correct them.",   // v37
	"{n} called {c} to carry the word of {g} to the unbelievers, with fire if need be.",
	"{n} preached war against those who did not honour {g}, and {c} sharpened their spears.",
};
const char* const kWonder[] = {
	"{IN}{c} finished {x} at {s}. It took three lifetimes. The first did not live to see it; the last complained of the dust.",   // v38
	"At {s} rose {x}, the greatest work of {c}, and people came from far away to see it.",
	"{x} was completed at {s}, [and it was said the god came down to look at it|and its builders were remembered for a thousand years].",
};
const char* const kNukeFirst[] = {
	"{IN}{c} lit a second sun over {s}. It rose at noon and did not set gently. Afterwards both peoples were very quiet for a long time.",   // v59
	"{c} let loose the fire of the sun upon {s} of {c2}. Nothing like it had been done before.",
	"A light brighter than noon rose over {s}, sent by {c} against {c2}, and the world learned a new fear.",
};
const char* const kNuke[] = {
	"Again the sun-fire fell, this time upon {s}.",
	"{c} burned {s} of {c2} with the hidden fire.",
	"The fire of {c} came down on {s}, and the sky over {c2} was black for a season.",
};
const char* const kArkStarted[] = {
	"At {s} {c} began to build a ship that would not sail on water. The children were told it was for them. It was.",   // extra (ark:started)
	"{c} laid the keel of a great ship at {s}, a ship for the space between the stars.",
};
const char* const kArkLaunched[] = {
	"{IN,}from {s}, {c} went up into the sky on a pillar of fire, carrying seed and songs and the name of {g}. Those who stayed watched until the light was a star among stars.",   // v60
	"The great ship of {c} rose from {s} on a pillar of fire and went out among the stars[, carrying the name of {ge}|, and did not come back].",
	"{c} left {W}. Their ship climbed from {s} into the dark, [and the god watched it go|and its light was the last star of the morning].",
};
const char* const kWarm1[] = {
	"{IN}the old men said the winters had been colder in their youth, as old men always say; but this time the ice agreed with them, and the sea came up to listen.",   // v57
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
	"The winters grew long and the summers forgot to come. Snow lay on the high passes all year, and the trade roads closed one by one.",   // v58
	"A cold age came upon {W}, [and the harvests were thin|and the rivers froze in their beds].",
};
const char* const kSeaRise[] = {
	"{IN}the old men said the winters had been colder in their youth, as old men always say; but this time the ice agreed with them, and the sea came up to listen.",   // v57
	"The sea crept up the shores, and the fishermen moved their houses inland.",
	"The sea was rising. [Old harbours drowned.|Every year the tide came a little higher.]",
};
// (finding 7) the sea-rise milestone when the engines raised it ({c}: the people whose chimneys burn the most)
const char* const kSeaRiseEngines[] = {
	"The sea was rising, and this time not by any god's hand: the chimneys of {c} had warmed the air, and the ice was giving back its water.",
	"{IN}the scholars of {c} wrote that the sea stood higher than in their grandfathers' day, and that the smoke of the engines was to blame. Few read it.",
	"The tide came a little higher every year. The priests looked to the sky; the scholars of {c} looked at the chimneys.",
};
const char* const kSeaFall[] = {
	"The sea drew back from the shores, and new land lay bare.",
	"The waters went down, and ships were left stranded in the mud of old harbours.",
};
const char* const kIceAge[] = {
	"The winters grew long and the summers forgot to come. Snow lay on the high passes all year, and the trade roads closed one by one.",   // v58
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
// (voice review 2) lost close to home: the place is not named twice ("the settlers of Nish were caught at the town")
const char* const kSettlersLostHome[] = {
	"Settlers who had left {s} were never heard from again.",
	"{c} sent out settlers who did not arrive. [Some said the wilds took them.|Their kin waited for years.]",
	"Settlers set out from {s}, and [the wilds took them before the town was out of sight|none of them lived to build anything].",
};
const char* const kPeopleLost[] = {
	"And then there was no one left on {W} to remember.",
	"The last fire on {W} went out. The god was alone again.",
	"The cities of {W} were empty, and the wind read the inscriptions on their walls.",
};
// the last place to fall is known: {s} and its people {c}
const char* const kPeopleLostLast[] = {
	"The last fire of {c} went out at {s}. The land kept their footprints for a season, then the grass forgot them too.",   // voice (people:lost)
	"{c} were the last. When the last hearth of {s} went cold, there was no one left on {W} to remember.",
};
// (voice, section 4) the endings, one paragraph each, by ending id; {callep} is ", whom they called
// {ep}" (empty when that is only the god's name again)
const char* const kEndBeloved[] = {   // ark-beloved
	"In the {yo} year {c} rose from {s} on a pillar of light. They carried seed, and songs, and a carved stone bearing the name of {g}{callep}. Before the clouds closed below them, they looked back at {W} and wept, not from grief, but because they had been loved, and knew it, and were taking that with them too. Wherever they land, there will be a shrine before there is a house.",
};
const char* const kEndFeared[] = {   // ark-feared
	"In the {yo} year {c} fled {W}. They built the ship in secret, as far as anything can be secret from a god, and they launched it at night. They did not pray as they rose. On the new world, the mothers will tell the children of {ep}, who lived in the sky of the old one, and the children will sleep with the lamp lit, and grow up, and build their houses low.",
};
const char* const kEndForgotten[] = {   // ark-forgotten ({s}: a town of theirs with a temple, else the launch site)
	"In the {yo} year {c} left {W} for the stars. They took their machines, their music and their quarrels. They did not take {g}; no one remembered to. In an old temple at {s} the wind still turns the pages of a book no one reads, and in it, in a hand no one living could read, is a name that once meant everything.",
};
const char* const kEndParting[] = {   // ark-parting
	"In the {yo} year {c} went up from {s} to the stars. They were not sure, at the end, what {g} had been to them: a parent, a storm, a story. So they did what grown children do: they said a careful goodbye, and promised to write, and did not say when. Somewhere above the clouds one of them looked back and raised a hand, just in case.",
};
const char* const kEndAsh[] = {   // silence-ash ({s}: the greatest place that fell)
	"They had learned at last to make suns, and they made them. Afterwards the sky over {W} was grey for a hundred years, and then blue again, as if nothing had happened. Nothing had: not any more. In the ruins of {s} a green shoot came up through the floor of the great hall, and there was no one to call it a sign.",
};
const char* const kEndQuiet[] = {   // silence-quiet ({ep}: the world's last name for the god)
	"There was no last battle and no last prayer. The towns emptied, one and then another, and the roads went back to grass, and the grass did not ask where everyone had gone. {W} turns in the light as it did before the first fire. The rivers still find the sea. If {ep} is listening, there is nothing now to hear but the wind.",
};
const char* const kEpiFirst[] = {
	"{c} called the one who had woken them {e}.",
	"Among {c} the god was named {e}.",
	"{c} began to speak of {g} as {e}.",
	"In the first songs of {c}, the god was {e}.",
};
// (voice review 2) a daughter people names the god for the first time: the god woke their fathers, not them
const char* const kEpiFirstDaughter[] = {
	"{c} called the god who had woken their fathers {e}.",
	"Among {c} the god was named {e}.",
	"{c} began to speak of {g} as {e}.",
	"In the first songs of {c}, the god was {e}.",
};
const char* const kEpiChange[] = {
	"{IN}{c} stopped calling their god {old}. From now on, in the market and at the hearth, it was {e}.",   // extra (epithet change)
	"{c} no longer called the god {old}; now they said {e}.",
	"In the songs of {c}, {old} became {e}.",
	"The {ca} priests gave the god a new name: {e}.",
	"Among {c} the god was no longer {old} but {e}, and the old name was forgotten.",
	"{c} had called the god {old}. After what they had seen, they called the god {e}.",
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

// (voice) one entry from `t`, or from `alt` with probability p
ChronicleEntry* writeAlt(World& w, const char* type, int weight, int cell, int culture, Tpl t, const string& key, Tpl alt,
	const string& altKey, double p, const Ctx& x) {
	return writeComposed(w, type, weight, cell, culture, t, key, Tpl{ nullptr, 0 }, string(), x, alt, altKey, p);
}
constexpr double kOwnShare = 0.35;     // a once-a-world moment: its older lines are told this often beside the voice's
constexpr double kVoiceShare = 0.35;   // a shared voice family (ages) beside an event's own lines
constexpr double kCommonShare = 0.2;   // the same for the most frequent events (growth, buildings): each shared line stays rare

// (voice {war_years}, {other_plural} for omens and crusades) small reads of the world
string warKey(int a, int b) { return "chronicle_w." + std::to_string(std::min(a, b)) + "." + std::to_string(std::max(a, b)); }
// a living people this culture is at war with (lowest id), -1 none
int enemyOf(const World& w, int cid) {
	for (const Culture& o : w.cultures) {
		if (o.dead || o.id == cid) continue;
		const Relation* r = society::peek(w, cid, o.id);
		if (r && r->war) return o.id;
	}
	return -1;
}
// the crusade's mark: the least devout living people in contact (as Diplomacy aims it), -1 none
int crusadeMark(const World& w, int cid) {
	int best = -1;
	double bd = 1e9;
	for (const Culture& o : w.cultures) {
		if (o.dead || o.id == cid) continue;
		const Relation* r = society::peek(w, cid, o.id);
		if (!r || !r->contact) continue;
		const double d = fin(society::regard(w, o.id).devotion, 0);
		if (d < bd) { bd = d; best = o.id; }
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
	case Phase::People: writeAlt(w, "genesis", 3, -1, -1, tpl(kGenPeopleVoice), "genesis-people-v", tpl(kGenPeople), "genesis-people", kOwnShare, x); break;
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
		if (k == 1) writeAlt(w, "comet", 2, e.cell, -1, tpl(kCometFirstVoice), "comet-first-v", tpl(kCometFirst), "comet-first", kOwnShare, x);
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

bool godStrokeRecent(const World& w);
void recordStroke(World& w, int cell, double ang);

void onTsunamiHit(World& w, const EvTsunamiHit& e) {
	const ChronicleTune& T = tuneOf(w);
	int sid = -1;
	Ctx x;
	sid = setWhere(x, w, e.cell, T.witnessAng);
	if (sid < 0) return;
	const int cid = sett(w, sid)->culture;
	// the people remember the sea that came for them; (finding 7) as the god's deed only when the god shook or
	// struck the world lately (a wave crosses an ocean), not for the sea's own waves
	if (cult(w, cid) && T.waveWeight > 0 && (!T.godDrownsOnly || godStrokeRecent(w))) {
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
	if (e.natural) {
		w.scratch<ChronicleScratch>().tellNext = true;     // (verify) the mountain's own fire is news, not the god's story
		write(w, "volcano", 2, e.cell, cid, tpl(kVolcanoNat), "volcano-nat", x);
	}
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
	if (!e.natural) recordStroke(w, e.cell, e.ang);       // (finding 7) the god's quake: the waves that follow are the god's
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
	const bool forest = noun.find("woods") != string::npos || noun.find("forest") != string::npos;
	// (voice review 2) the bolt that lit it was just told nearby: no second cause
	bool lit = false;
	if (w.geo) {
		for (size_t i = w.chronicle.size(), k = 0; i-- > 0 && k < 6; k++) {
			const ChronicleEntry& b = w.chronicle[i];
			if (b.year < fin(w.year) - 1 - 1e-9) break;
			if (b.type == "lightning" && w.valid(b.cell) && w.geo->angle(b.cell, e.cell) < 0.15) { lit = true; break; }
		}
	}
	if (lit) write(w, "fire", 1, e.cell, cid, tpl(kFireLit), "fire-lit", x);
	else if (forest) write(w, "fire", 1, e.cell, cid, tpl(kFireForest), "fire-forest", x);
	else write(w, "fire", 1, e.cell, cid, tpl(kFire), "fire", x);
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
		if (!earlier && fin(w.year) < 1) writeAlt(w, "awakening", 3, cell, cid, tpl(kAwakeningVoice), "awakening-v", tpl(kAwakening), "awakening", kOwnShare, x);
		else if (!earlier) write(w, "awakening", 3, cell, cid, tpl(kAwakening), "awakening", x);
		else write(w, "awakening", 3, cell, cid, tpl(kAwakeningAgain), "awakening-again", x);
		return;
	}
	if (!minorOk(w, "founded", cid, T.minorGap * 0.6)) return;
	// (voice {origin}) where the band came from: the settler unit that has just arrived at this cell
	// (Units founds the settlement before it removes the unit); unknown -> the lines without an origin
	int from = -1;
	for (const Unit& u : w.units) {
		if (u.type != UnitType::Settler || u.culture != cid || u.path.empty() || u.path.back() != cell) continue;
		const Settlement* o = sett(w, u.from);
		if (o && !o->dead && o->id != e.id) { from = o->id; break; }
	}
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
	x.set("sn", string("the ") + tierWord(e.tier));
	x.set("ast", withArticle(tierWord(e.tier)));
	x.set("apt", withArticle(tierWord(e.prev)));
	ctxCulture(x, w, cid);
	if (e.tier < e.prev) {
		if (!minorOk(w, "decline", cid, T.minorGap)) return;
		write(w, "decline", 1, s->cell, cid, tpl(kTierDown), "tier-down", x);
		return;
	}
	const Tpl grow = tpl(kTierVoice);
	switch (e.tier) {
	case 1:
		if (!minorOk(w, "tier", cid, T.tierGap, T.tierGap * 0.5)) return;
		writeAlt(w, "tier", 1, s->cell, cid, tpl(kTierVillage), "tier-village", grow, "tier-voice", kCommonShare, x);
		break;
	case 2:
		if (!minorOk(w, "tier", cid, T.tierGap, T.tierGap * 0.5)) return;
		writeAlt(w, "tier", 1, s->cell, cid, tpl(kTierTown), "tier-town", grow, "tier-voice", kCommonShare, x);
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
		writeAlt(w, "tier", 2, s->cell, cid, tpl(kTierCity), "tier-city", grow, "tier-voice", kCommonShare, x);
		break;
	}
	case 4: {
		bool cultureFirst = true;
		for (const Settlement& o : w.settlements) if (o.id != e.id && o.culture == cid && !o.dead && o.tier >= 4) { cultureFirst = false; break; }
		if (!cultureFirst && recentNotable(w, "tier", cid, T.bigTierGap)) break;
		writeAlt(w, "tier", 2, s->cell, cid, tpl(kTierMetro), "tier-metro", grow, "tier-voice", kCommonShare, x);
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
	// (verifier 2026-11-19) "the first of its kind" is the people's first ever, not the first still standing:
	// a granary built by a town that later fell was built before. vars.num (saved; culture death clears it).
	bool first = true;
	if (cult(w, cid)) {
		const string firstKey = "chronicle_b." + std::to_string(cid) + "." + std::to_string(int(e.building));
		first = !w.vars.num.count(firstKey);
		w.vars.num[firstKey] = 1;
	}
	Tpl t{ nullptr, 0 };
	string key;
	switch (e.building) {
	case Building::SHRINE: {
		const Culture* c = cult(w, cid);
		const bool daughter = first && c && c->parent >= 0;
		t = daughter ? tpl(kBShrineFirstDaughter) : first ? tpl(kBShrineFirst) : tpl(kBShrine);
		key = daughter ? "b-shrine-first-d" : first ? "b-shrine-first" : "b-shrine";
		break;
	}
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
	// the first of a kind among a people: "... It was the first academy of the Umari."
	// (voice review 2) a new people's first town raises shrine, granary, harbour, temple, market and mine
	// within two years, and six notable "first of the Wanori" lines drummed: one such line per culture per
	// firstCluster years, the others are told (or not) as ordinary buildings
	bool sfx = first && e.building != Building::SHRINE && e.building != Building::LAUNCH;
	if (sfx && cult(w, cid)) {
		const string fk = "chronicle_bf." + std::to_string(cid);
		const auto it = w.vars.num.find(fk);
		if (it != w.vars.num.end() && std::isfinite(it->second) && fin(w.year) - it->second < T.firstCluster - 1e-9) sfx = false;
		else w.vars.num[fk] = fin(w.year);
	}
	const bool big = e.building == Building::LAUNCH || (first && (e.building == Building::SHRINE || sfx));
	if (!big && !minorOk(w, "building", cid, T.buildingGap, T.buildingGap * 0.5)) return;
	Ctx x;
	ctxSettlement(x, w, e.id);
	ctxCulture(x, w, cid);
	if (e.building == Building::SHRINE && first) {
		const Culture* c = cult(w, cid);
		const Culture* pc = c ? cult(w, c->parent) : nullptr;
		const string g = godOf(w, cid);
		x.set("gk", pc && c && pc->godName == c->godName ? "and kept the name their fathers had given the god: " + g + "."
			: "and called the god by a name of their own: " + g + ".");
	}
	x.set("bn", kBuildingNoun[e.building][0]);
	x.set("bna", kBuildingNoun[e.building][1]);
	// (voice 7a-c) the generic building lines for every ordinary building (not shrines, walls or towers,
	// which have lines of their own)
	const bool generic = e.building != Building::SHRINE && e.building != Building::WALLS && e.building != Building::LAUNCH;
	writeComposed(w, "building", big ? 2 : 1, s->cell, cid, t, key, sfx ? tpl(kBuildFirstSfx) : Tpl{ nullptr, 0 }, "b-first-sfx", x,
		generic ? tpl(kBuildVoice) : Tpl{ nullptr, 0 }, "b-voice", kCommonShare, true);
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
	// (voice review) a darkened sky starves every people at once: one such famine in the world per dustFamineGap years
	const bool dusty = fin(w.dust, 0) >= T.dustFamine;
	if (dusty && recent(w, "famine", kAnyCulture, T.dustFamineGap)) return;
	Ctx x;
	ctxWorld(x, w);
	ctxSettlement(x, w, e.id);
	ctxCulture(x, w, cid);
	if (dusty) write(w, "famine", big ? 2 : 1, s->cell, cid, tpl(kFamineDust), "famine-dust", x);
	else write(w, "famine", big ? 2 : 1, s->cell, cid, tpl(kFamine), "famine", x);
}

void onPlague(World& w, const EvSettlementPlague& e) {
	const Settlement* s = sett(w, e.id);
	if (!s) return;
	const ChronicleTune& T = tuneOf(w);
	const int cid = s->culture;
	if (!e.start) {
		// (voice 14) a plague ends: told only when its coming was written down here (within plagueTold
		// years), at most once per culture per minorGap years; a cure by Heal tells its own story
		bool told = false;
		const double y = fin(w.year);
		for (size_t i = w.chronicle.size(); i-- > 0;) {
			const ChronicleEntry& c = w.chronicle[i];
			if (c.year < y - T.plagueTold - 1e-9) break;
			if (c.cell == s->cell && (c.type == "plague-end" || c.type == "miracle-heal" || c.type == "prayer-answered")) break;
			if (c.cell == s->cell && c.type == "plague") { told = true; break; }
		}
		if (!told) return;
		// (voice review 2) held back: a Heal that cured it tells the story instead (see ChronicleScratch::pending)
		ChronicleScratch& S = w.scratch<ChronicleScratch>();
		ChronicleScratch::Pending pe;
		pe.tick = w.tick;
		pe.year = fin(w.year);
		pe.a = e.id;
		S.pending.push_back(pe);
		return;
	}
	if (!minorOk(w, "plague", cid, T.minorGap)) return;
	Ctx x;
	ctxSettlement(x, w, e.id);
	ctxCulture(x, w, cid);
	write(w, "plague", s->tier >= 3 ? 2 : 1, s->cell, cid, tpl(kPlague), "plague", x);
}

// (finding 7) the god's hand on the land: Lower Land strokes, quakes and meteors, by cell, kept strokeYears
// (vars.num "chronicle_gs.c<cell>" = year, "chronicle_ga.c<cell>" = the stroke's radius)
void recordStroke(World& w, int cell, double ang) {
	if (!tuneOf(w).godDrownsOnly || !w.valid(cell)) return;
	const string c = std::to_string(cell);
	w.vars.num["chronicle_gs.c" + c] = fin(w.year);
	double& a = w.vars.num["chronicle_ga.c" + c];
	a = std::max(fin(a), std::clamp(fin(ang, 0.1), 0.0, 0.5));
}
void onPowerUsed(World& w, const EvPowerUsed& e) {
	if (e.id == "lower" || e.id == "quake" || e.id == "meteor") recordStroke(w, e.cell, e.ang);
}
bool godStrokeNear(const World& w, int cell) {
	if (!w.geo || !w.valid(cell)) return false;
	const ChronicleTune& T = tuneOf(w);
	const string pre = "chronicle_gs.c";
	for (auto it = w.vars.num.lower_bound(pre); it != w.vars.num.end() && startsWith(it->first, pre); ++it) {
		if (!std::isfinite(it->second) || fin(w.year) - it->second > T.strokeYears + 1e-9) continue;
		const string c = it->first.substr(pre.size());
		const int sc = std::atoi(c.c_str());
		if (!w.valid(sc)) continue;
		const auto ga = w.vars.num.find("chronicle_ga.c" + c);
		const double ang = ga != w.vars.num.end() ? fin(ga->second, 0.1) : 0.1;
		if (w.geo->angle(sc, cell) <= ang + T.strokeAng) return true;
	}
	return false;
}
// any stroke of the god's hand on the land, anywhere, within strokeYears (for waves)
bool godStrokeRecent(const World& w) {
	const string pre = "chronicle_gs.c";
	for (auto it = w.vars.num.lower_bound(pre); it != w.vars.num.end() && startsWith(it->first, pre); ++it)
		if (std::isfinite(it->second) && fin(w.year) - it->second <= tuneOf(w).strokeYears + 1e-9 && fin(w.year) >= it->second - 1e-9) return true;
	return false;
}
void pruneStrokes(World& w) {
	const ChronicleTune& T = tuneOf(w);
	const string pre = "chronicle_gs.c";
	for (auto it = w.vars.num.lower_bound(pre); it != w.vars.num.end() && startsWith(it->first, pre);) {
		if (std::isfinite(it->second) && fin(w.year) - it->second <= T.strokeYears + 1e-9 && fin(w.year) >= it->second) { ++it; continue; }
		w.vars.num.erase("chronicle_ga.c" + it->first.substr(pre.size()));
		it = w.vars.num.erase(it);
	}
}
// (finding 7) the people whose engines warm the world: the most factories and power plants in live towns,
// once w.co2 reaches enginesCo2; -1 when the warming is no one's doing
int enginesOf(const World& w) {
	if (!(fin(w.co2, 0) >= tuneOf(w).enginesCo2)) return -1;
	std::vector<int> burn(w.cultures.size(), 0);
	for (const Settlement& s : w.settlements) {
		if (s.dead || s.culture < 0 || s.culture >= int(w.cultures.size())) continue;
		burn[size_t(s.culture)] += (s.has(Building::FACTORY) ? 1 : 0) + (s.has(Building::PLANT) ? 2 : 0);
	}
	int best = -1;
	for (size_t i = 0; i < burn.size(); i++) if (burn[i] > 0 && !w.cultures[i].dead && (best < 0 || burn[i] > burn[size_t(best)])) best = int(i);
	return best;
}
// (finding 7) a town the sea covered: the god's drowning (remembered: the Drowner) only where the god lowered,
// shook or struck the land near it lately; otherwise the sea's own rise, or the engines' (not remembered)
void tellDrowned(World& w, int sid, int cid, int cell, int tier) {
	const bool god = godStrokeNear(w, cell);
	if (god && cult(w, cid)) {
		double& d = w.vars.num["chronicle_d." + std::to_string(cid)];
		d = fin(d) + 1;
	}
	Ctx x;
	ctxWorld(x, w);
	ctxSettlement(x, w, sid);
	ctxCulture(x, w, cid);
	const int weight = tier >= 3 ? 3 : 2;
	if (god) { write(w, "destroyed", weight, cell, cid, tpl(kDLowered), "d-lowered", x); return; }
	const int burner = enginesOf(w);
	if (burner >= 0 && burner == cid) { write(w, "destroyed", weight, cell, cid, tpl(kDSeaEnginesOwn), "d-sea-engines-own", x); return; }
	if (burner >= 0) { ctxCulture(x, w, burner, "2"); write(w, "destroyed", weight, cell, cid, tpl(kDSeaEngines), "d-sea-engines", x); return; }
	write(w, "destroyed", weight, cell, cid, tpl(kDSeaRose), "d-sea-rose", x);
}

void onDestroyed(World& w, const EvSettlementDestroyed& e) {
	const Settlement* s = sett(w, e.id);
	if (!s) return;
	const ChronicleTune& T = tuneOf(w);
	const int cid = e.culture >= 0 ? e.culture : s->culture;
	// (finding 7) the sea covered it: whose doing that was is decided once the tick is over (Powers drowns a
	// lowered town before it emits power:used); a flood on land and a wave are still the god's, as before
	{
		const int at0 = w.valid(e.cell) ? e.cell : s->cell;
		if (e.cause == Cause::Drowned && T.godDrownsOnly && w.valid(at0) && w.elev.size() == size_t(w.n) && state::isSea(w, at0)) {
			ChronicleScratch& PS = w.scratch<ChronicleScratch>();
			ChronicleScratch::Pending pe;
			pe.tick = w.tick;
			pe.year = fin(w.year);
			pe.drown = true;
			pe.a = e.id;
			pe.b = cid;
			pe.cell = at0;
			pe.tier = s->tier;
			PS.pending.push_back(pe);
			return;
		}
	}
	if (e.cause == Cause::Drowned || (e.cause == Cause::Wave && (!T.godDrownsOnly || godStrokeRecent(w)))) {
		if (cult(w, cid)) {
			double& d = w.vars.num["chronicle_d." + std::to_string(cid)];
			d = fin(d) + 1;
		}
	}
	Tpl t{ nullptr, 0 };
	string key;
	Ctx x;
	// (voice 18) the army that took it this tick names the razers
	const ChronicleScratch& S = w.scratch<ChronicleScratch>();
	const bool byArmy = e.cause == Cause::War && S.wonTick == w.tick && S.wonYear == fin(w.year) && S.wonSettlement == e.id &&
		cult(w, S.wonAttacker) && S.wonAttacker != cid;
	if (byArmy) ctxCulture(x, w, S.wonAttacker, "2");
	// (voice 19) a quake by the sea
	const int at = w.valid(e.cell) ? e.cell : s->cell;
	const bool coastal = w.valid(at) && size_t(at) < w.coast.size() && w.coast[size_t(at)] <= 1;
	// the strike that burned it was just told (the nuke entry for this cell, this year)
	bool struck = false;
	for (size_t i = w.chronicle.size(), k = 0; i-- > 0 && k < 6; k++)
		if (w.chronicle[i].type == "nuke" && w.chronicle[i].cell == s->cell && w.chronicle[i].year == fin(w.year)) struck = true;
	switch (e.cause) {
	case Cause::Drowned:
		// (finding 7) a flood on land: where the god just lowered the land it is the god's hand, not the sea's rise
		if (T.godDrownsOnly && godStrokeNear(w, at)) { t = tpl(kDLowered); key = "d-lowered"; }
		else { t = tpl(kDDrowned); key = "d-drowned"; }
		break;
	case Cause::Meteor: t = tpl(kDMeteor); key = "d-meteor"; break;
	case Cause::Lava: t = tpl(kDLava); key = "d-lava"; break;
	case Cause::War: t = byArmy ? tpl(kDWarBy) : tpl(kDWar); key = byArmy ? "d-war-by" : "d-war"; break;
	case Cause::Famine: t = tpl(kDFamine); key = "d-famine"; break;
	case Cause::Plague: t = tpl(kDPlague); key = "d-plague"; break;
	case Cause::Quake: t = coastal ? tpl(kDQuakeCoast) : tpl(kDQuake); key = coastal ? "d-quake-coast" : "d-quake"; break;
	case Cause::Nuke: t = struck ? tpl(kDNukeAfter) : tpl(kDNuke); key = struck ? "d-nuke-after" : "d-nuke"; break;
	case Cause::Wave: t = tpl(kDWave); key = "d-wave"; break;
	case Cause::Lightning: t = tpl(kDLightning); key = "d-lightning"; break;
	default: t = tpl(kDAbandoned); key = "d-abandoned"; break;
	}
	const bool quiet = e.cause == Cause::Abandoned || e.cause == Cause::None || e.cause == Cause::Famine;
	int weight = s->tier >= 3 ? 3 : (quiet && s->tier <= 1 ? 1 : 2);
	if (weight == 1 && !minorOk(w, "destroyed", cid, T.minorGap)) return;
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
	{
		const Culture* cf = cult(w, e.from);
		const Culture* ct = cult(w, e.to);
		const bool renamed = s->has(Building::TEMPLE) && cf && ct && !ct->godName.empty() && cf->godName != ct->godName;
		x.set("tg", renamed ? "and its temple was given a new god's name" : "and its market changed hands with it");
	}
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
		// (voice review 2) "X prayed for rain, and it was given" followed by "the people of X prayed for rain",
		// and the same town praying in every other entry of a kind god's book
		if (toldAt(w, "prayer-answered", s->cell, T.answerQuiet) || toldAt(w, "prayer", s->cell, T.prayerTownGap)) return;
		// a kind god's book was four parts in ten rain: "they prayed", "it was given", "it rained". Where the god
		// has been answering, the answer (or, if it does not come, the unanswered prayer) tells the prayer itself
		if (recent(w, "prayer-answered", cid, T.prayerHeard)) return;
		if (!minorOk(w, "prayer", cid, T.minorGap, T.prayerWorldGap)) return;
		write(w, "prayer", 1, s->cell, cid, tpl(kPrayer), "prayer", x);
		return;
	}
	const auto it = S.prayerSince.find(e.id);
	if (it == S.prayerSince.end()) return;
	const double since = it->second;
	S.prayerSince.erase(it);
	const double limit = fin(w.tune<SocietyTune>().prayerYears, 15);
	if (fin(w.year) - since < limit - 0.5) return;         // the cause went away: nothing to tell
	x.set("py", yearsInWords(fin(w.year) - since));
	if (!minorOk(w, "prayer-unanswered", cid, T.minorGap)) return;
	write(w, "prayer-unanswered", 1, s->cell, cid, tpl(kPrayerLost), "prayer-lost", x);
}

void onPrayerAnswered(World& w, const EvPrayerAnswered& e) {
	const Settlement* s = sett(w, e.id);
	if (!s) return;
	ChronicleScratch& S = w.scratch<ChronicleScratch>();
	S.prayerSince.erase(e.id);
	// (voice review 2) a healed town: the answer tells the end of its plague
	if (e.kind == PrayerKind::Heal) {
		for (size_t i = 0; i < S.pending.size();) {
			const ChronicleScratch::Pending& pe = S.pending[i];
			if (!pe.war && !pe.drown && pe.a == e.id && pe.tick == w.tick && pe.year == fin(w.year)) S.pending.erase(S.pending.begin() + std::ptrdiff_t(i));
			else i++;
		}
	}
	if (S.answeredTick != w.tick || S.answeredYear != fin(w.year)) { S.answeredTick = w.tick; S.answeredYear = fin(w.year); S.answered.clear(); }
	S.answered.push_back(e.id);
	const int cid = s->culture;
	heardBy(w, cid);
	Ctx x;
	ctxSettlement(x, w, e.id);
	ctxCulture(x, w, cid);
	// (integration verifier) a kind god answering every year made hundreds of notable entries: one
	// notable answer per culture per minorGap years, the others still written but minor
	// (verifier 2026-11-19) answers are told one at a time in the whole world: one within answerWorldGap years
	// of another is minor, and the minor rule then drops it (a kind god's answers filled a third of the book
	// and one rain sentence came back forty times)
	int wgt = (recentNotable(w, "prayer-answered", cid, tuneOf(w).minorGap) ||
		recent(w, "prayer-answered", kAnyCulture, tuneOf(w).answerWorldGap)) ? 1 : 2;
	// (chronicle budget, review finding 8) a routine answered prayer is notable only the first time a people is
	// answered in each of its ages; after that it is minor (and folds into the decade's digest)
	if (tuneOf(w).budget && cult(w, cid)) {
		const string pk = "chronicle_pa." + std::to_string(cid);
		const double age = double(cult(w, cid)->age) + 1;
		const auto it = w.vars.num.find(pk);
		if (wgt == 2 && it != w.vars.num.end() && it->second == age) wgt = 1;
		else if (wgt == 2) w.vars.num[pk] = age;
	}
	// (voice review) the minor ones are rate-limited like every minor entry: a kind god answering ten
	// towns a year wrote ten near-identical lines a year
	if (wgt == 1 && !minorOk(w, "prayer-answered", cid, tuneOf(w).minorGap, tuneOf(w).answerWorldGap)) {
		// (chronicle budget) every answer is a town the god really heard: the ones the gaps leave untold are
		// counted for the decade's digest ("That decade Sarello answered the prayers of seven towns of the Domafi")
		if (tuneOf(w).budget && cult(w, cid)) {
			foldInto(w, cid, "answer");
		}
		return;
	}
	switch (e.kind) {
	case PrayerKind::Rain: write(w, "prayer-answered", wgt, s->cell, cid, tpl(kAnsRain), "ans-rain", x); break;
	case PrayerKind::Heal: write(w, "prayer-answered", wgt, s->cell, cid, tpl(kAnsHeal), "ans-heal", x); break;
	case PrayerKind::Peace:
		// (voice review 2) the answer ends the war: a draw held back this tick is told by it
		if (write(w, "prayer-answered", wgt, s->cell, cid, tpl(kAnsPeace), "ans-peace", x) || S.foldedLast) {
			for (size_t i = 0; i < S.pending.size();) {
				const ChronicleScratch::Pending& pe = S.pending[i];
				if (pe.war && (pe.a == cid || pe.b == cid) && pe.tick == w.tick && pe.year == fin(w.year)) S.pending.erase(S.pending.begin() + std::ptrdiff_t(i));
				else i++;
			}
		}
		break;
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
	else x.set("s", "wide lands");                         // (voice review 2) "who once held wide lands", not "their old lands"
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
	// (voice {age}) "the Age of Iron"; the hidden fire and the stars read as nouns
	static const char* const kAgeWord[Age::COUNT] = { "Stone", "Bronze", "Iron", "Sail", "Industry", "the Atom", "the Stars" };
	x.set("age", kAgeWord[e.age]);
	// (voice 25a-c, 26) the generic age lines beside each age's own; the world's first people to reach
	// an age is told by v26 half the time
	writeComposed(w, "age", first ? 3 : 2, sett(w, sid) ? sett(w, sid)->cell : -1, e.id, t, key,
		first ? tpl(kAgeFirstSfx) : Tpl{ nullptr, 0 }, "age-first-sfx", x,
		first ? tpl(kAgeWorldFirst) : tpl(kAgeVoice), first ? "age-world-first" : "age-voice", first ? 0.5 : kVoiceShare);
}

void onWarDeclared(World& w, const EvWarDeclared& e) {
	if (!cult(w, e.a) || !cult(w, e.b)) return;
	Ctx x;
	ctxCulture(x, w, e.a);
	ctxCulture(x, w, e.b, "2");
	w.vars.num[warKey(e.a, e.b)] = fin(w.year);           // (voice {war_years}) read back at war:ended
	write(w, "war", 2, -1, e.a, tpl(kWarDeclared), "war", x);
}

void onWarEnded(World& w, const EvWarEnded& e) {
	if (!cult(w, e.a) || !cult(w, e.b)) return;
	Ctx x;
	// (voice {war_years}) from the declaration this chronicle saw; unknown -> "long fighting"
	const string wk = warKey(e.a, e.b);
	const auto began = w.vars.num.find(wk);
	if (began != w.vars.num.end() && std::isfinite(began->second) && fin(w.year) >= began->second) {
		x.set("wy", yearsInWords(fin(w.year) - began->second));
		w.vars.num.erase(began);
	} else {
		x.set("wy", "long fighting");
	}
	if (e.victor == e.a || e.victor == e.b) {
		const int loser = e.victor == e.a ? e.b : e.a;
		ctxCulture(x, w, e.victor);
		ctxCulture(x, w, loser, "2");
		const Culture* v = cult(w, e.victor);
		ctxSettlement(x, w, v->capital);
		if (!sett(w, v->capital)) x.set("s", "their cities");
		// (voice review 2) the beaten people died with the war (culture:died comes first): nobody asked for peace
		const Culture* lc = cult(w, loser);
		if (lc && lc->dead) write(w, "peace", 2, -1, e.victor, tpl(kWarWonGone), "war-won-gone", x);
		else write(w, "peace", 2, -1, e.victor, tpl(kWarWon), "war-won", x);
	} else {
		// (voice review 2) held back: an Omen of Peace that ended it tells both armies' story instead
		ChronicleScratch& S = w.scratch<ChronicleScratch>();
		ChronicleScratch::Pending pe;
		pe.tick = w.tick;
		pe.year = fin(w.year);
		pe.war = true;
		pe.a = e.a;
		pe.b = e.b;
		S.pending.push_back(pe);
	}
}

void onBattle(World& w, const EvBattle& e) {
	if (e.won) {                                          // a won battle is told as the capture or the razing
		ChronicleScratch& S = w.scratch<ChronicleScratch>();
		S.wonTick = w.tick;
		S.wonYear = fin(w.year);
		S.wonSettlement = e.settlement;
		S.wonAttacker = e.attacker;
		return;
	}
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
		if (!minorOk(w, "trade", a->culture, T.tradeGap, T.tradeWorldGap)) return;
		write(w, "trade", 1, a->cell, a->culture, tpl(kTradeForeignMore), "trade-foreign-more", x);
		return;
	}
	if (!minorOk(w, "trade", a->culture, T.tradeGap, T.tradeWorldGap)) return;
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
	case Edict::Crusade: {
		t = tpl(kEdictCrusade);
		key = "edict-crusade";
		const int mark = crusadeMark(w, e.culture);
		if (mark >= 0) ctxCulture(x, w, mark, "2");
		else x.set("c2", "their neighbours");
		break;
	}
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
	// (verify) the world's first Ark is epochal; another people's Ark after it is notable (four or five Arks begun
	// in the short Space age were up to four epochal entries a real minute)
	double& arks = w.vars.num["chronicle_arks"];
	arks = fin(arks) + 1;
	write(w, "ark", tuneOf(w).budget && arks > 1.5 ? 2 : 3, s ? s->cell : -1, e.culture, tpl(kArkStarted), "ark-started", x);
}

void onArkLaunched(World& w, const EvArkLaunched& e) {
	if (!cult(w, e.culture)) return;
	Ctx x;
	ctxWorld(x, w);
	ctxCulture(x, w, e.culture);
	ctxSettlement(x, w, e.settlement);
	const Settlement* s = sett(w, e.settlement);
	if (!s) x.set("s", "their capital");
	// (voice review 2) in history the launch ends the world (Sim.checkEnding), and the ending passage opens
	// "In the {yo} year": the launch does not open with it too
	if (w.phase == Phase::History) x.set("noIN", "1");
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
	case MilestoneKind::SeaRise: {
		// (finding 7) the people's engines own the rising sea, and the chronicle says so
		const int burner = tuneOf(w).godDrownsOnly ? enginesOf(w) : -1;
		if (burner >= 0) { ctxCulture(x, w, burner); write(w, "climate", 2, -1, burner, tpl(kSeaRiseEngines), "sea-rise-engines", x); }
		else write(w, "climate", 2, -1, -1, tpl(kSeaRise), "sea-rise", x);
		break;
	}
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
	if (home >= 0 && sid == home) write(w, "settlers-lost", 1, e.cell, e.culture, tpl(kSettlersLostHome), "settlers-lost-home", x);
	else write(w, "settlers-lost", 1, e.cell, e.culture, tpl(kSettlersLost), "settlers-lost", x);
}

void onPeopleLost(World& w, const EvPeopleLost&) {
	Ctx x;
	ctxWorld(x, w);
	// (voice, people:lost) the last place to fall: latest deadYear, ties by the higher id
	int last = -1;
	for (const Settlement& s : w.settlements)
		if (s.dead && s.deadYear && std::isfinite(*s.deadYear) && (last < 0 || *s.deadYear >= *w.settlements[size_t(last)].deadYear)) last = s.id;
	if (last >= 0 && cult(w, w.settlements[size_t(last)].culture)) {
		ctxSettlement(x, w, last);
		ctxCulture(x, w, w.settlements[size_t(last)].culture);
		write(w, "lost", 3, w.settlements[size_t(last)].cell, w.settlements[size_t(last)].culture, tpl(kPeopleLostLast), "people-lost-last", x);
		return;
	}
	write(w, "lost", 3, -1, -1, tpl(kPeopleLost), "people-lost", x);
}

// (voice, section 4) the ending passage for an ending id: the family, and the keys it needs.
//   ark-*      {c} {g} {ep} {callep} the departing people; {s} the launch site (ark-forgotten: their
//              largest live town with a temple, else the launch site); {yo} the ending year
//   silence-*  {W}; quiet: {ep} the world's last name for the god; ash: {s} the greatest place that fell
Tpl endingTpl(Mood mood, string& key) {
	switch (mood) {
	case Mood::Beloved: key = "end-beloved"; return tpl(kEndBeloved);
	case Mood::Feared: key = "end-feared"; return tpl(kEndFeared);
	case Mood::Forgotten: key = "end-forgotten"; return tpl(kEndForgotten);
	case Mood::Parting: key = "end-parting"; return tpl(kEndParting);
	case Mood::Ash: key = "end-ash"; return tpl(kEndAsh);
	case Mood::Quiet: break;
	}
	key = "end-quiet";
	return tpl(kEndQuiet);
}
void endingCtx(Ctx& x, const World& w, Mood mood, int culture, int settlement, double year) {
	ctxWorld(x, w);
	x.set("yo", yearOrdinal(year));
	if (cult(w, culture)) ctxCulture(x, w, culture);
	else {
		x.set("c", "the last people");
		x.set("g", "the god");
		x.set("ep", w.god.epithet.empty() ? string("the god") : w.god.epithet);
	}
	const Ctx::Kv* g = x.get("g");
	const Ctx::Kv* ep = x.get("ep");
	x.set("callep", g && ep && g->v != ep->v ? ", whom they called " + ep->v : string());
	int sid = sett(w, settlement) ? settlement : -1;
	if (mood == Mood::Forgotten && cult(w, culture)) {
		int best = -1;
		for (const Settlement& s : w.settlements)
			if (!s.dead && s.culture == culture && s.has(Building::TEMPLE) && (best < 0 || fin(s.pop) > fin(w.settlements[size_t(best)].pop))) best = s.id;
		if (best >= 0) sid = best;
	}
	if (mood == Mood::Ash || mood == Mood::Quiet) {
		x.set("ep", w.god.epithet.empty() ? string("the god") : w.god.epithet);
		sid = -1;
		for (const Settlement& s : w.settlements) {
			if (!s.dead) continue;
			if (sid < 0) { sid = s.id; continue; }
			const Settlement& b = w.settlements[size_t(sid)];
			if (s.tier > b.tier || (s.tier == b.tier && fin(s.pop) > fin(b.pop))) sid = s.id;
		}
	}
	if (sid >= 0) ctxSettlement(x, w, sid);
	else if (cult(w, culture) && sett(w, cult(w, culture)->capital)) ctxSettlement(x, w, cult(w, culture)->capital);
	else x.set("s", mood == Mood::Ash ? "the last city" : "their old capital");
}

void onEnding(World& w, const EvEnding& e) {
	Ctx x;
	endingCtx(x, w, e.mood, e.culture, e.settlement, fin(w.year));
	string key;
	const Tpl t = endingTpl(e.mood, key);
	write(w, "ending", 3, -1, cult(w, e.culture) ? e.culture : -1, t, key, x);
}

void onMiracle(World& w, const EvMiracleWitnessed& e) {
	const ChronicleTune& T = tuneOf(w);
	ChronicleScratch& S = w.scratch<ChronicleScratch>();
	// (voice review 2) the god healed it: the plague's end held back this tick is not "burned itself out"
	if (e.kind == "heal") {
		for (size_t i = 0; i < S.pending.size();) {
			const ChronicleScratch::Pending& pe = S.pending[i];
			const Settlement* ps = (pe.war || pe.drown) ? nullptr : sett(w, pe.a);
			const bool here = ps && (ps->cell == e.cell || std::find(e.ids.begin(), e.ids.end(), pe.a) != e.ids.end());
			if (here && pe.tick == w.tick && pe.year == fin(w.year)) S.pending.erase(S.pending.begin() + std::ptrdiff_t(i));
			else i++;
		}
	}
	if (e.ids.empty()) return;                             // the chronicle is what the people saw
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
	// (voice review 2) the town's answered prayer already told this rain (the brush goes on for a few more
	// strokes): "Thovi heard the dry fields of Nawar, and opened the sky" then "Rain fell on Nawar out of a
	// clear sky"
	if ((k == "rain" || k == "bloom") && toldAt(w, "prayer-answered", s->cell, T.answerQuiet)) return;
	// (voice review 2) one brush miracle per town per brushTownGap years
	if ((k == "rain" && toldAt(w, "miracle-rain", s->cell, T.brushTownGap)) || (k == "bloom" && toldAt(w, "miracle-bloom", s->cell, T.brushTownGap)) ||
		((k == "raise" || k == "lower") && toldAt(w, "miracle-shape", s->cell, T.brushTownGap))) return;
	if (k == "rain") {
		if (!minorOk(w, "miracle-rain", cid, T.brushGap, T.brushGap * 0.35)) return;
		// a town that has prayed lately asked for it: not "though it had not been asked for"
		const bool asked = S.prayerSince.count(sid) > 0 || toldAt(w, "prayer", s->cell, 15);
		if (good >= 0.5) write(w, "miracle-rain", 1, e.cell, cid, tpl(kRainThirst), "rain-thirst", x);
		else if (asked) write(w, "miracle-rain", 1, e.cell, cid, tpl(kRainSoftAsked), "rain-soft-asked", x);
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
		// (voice review) one omen per culture per minorGap years: a kind god's omens over twenty towns
		// in one year wrote twenty entries
		// (voice review 2) both armies saw one omen: the other people's witness is that entry
		if (S.omenTick == w.tick && S.omenYear == fin(w.year) && std::find(S.omenCultures.begin(), S.omenCultures.end(), cid) != S.omenCultures.end()) return;
		if (recentNotable(w, "miracle-peace", cid, T.minorGap)) return;
		// (voice review 2) the omen has already ended the war (Powers: effect first, then the witness), so the
		// enemy is the war held back this tick; v52 was unreachable in play before
		int foe = -1;
		for (size_t i = 0; i < S.pending.size(); i++) {
			const ChronicleScratch::Pending& pe = S.pending[i];
			if (pe.war && (pe.a == cid || pe.b == cid) && pe.tick == w.tick && pe.year == fin(w.year)) {
				foe = pe.a == cid ? pe.b : pe.a;
				S.pending.erase(S.pending.begin() + std::ptrdiff_t(i));
				break;
			}
		}
		if (foe < 0) foe = enemyOf(w, cid);
		if (foe >= 0 && cult(w, foe)) {
			ctxCulture(x, w, foe, "2");
			write(w, "miracle-peace", 2, e.cell, cid, tpl(kOmenPeaceWar), "omen-peace-war", x);
			S.omenTick = w.tick;
			S.omenYear = fin(w.year);
			S.omenCultures = { cid, foe };
		} else {
			write(w, "miracle-peace", 2, e.cell, cid, tpl(kOmenPeace), "omen-peace", x);
		}
	} else if (k == "plague" && good < 0) {
		if (!minorOk(w, "wrath-plague", cid, 2)) return;
		write(w, "wrath-plague", 2, e.cell, cid, tpl(kPlagueSent), "plague-sent", x);
	}
	// lightning, quake, meteor, volcano: told by their own events; prophet: by prophet:risen; nuke: by nuke
}

// (voice review 2) the endings held back (ChronicleScratch::pending): written once their tick is over
// (`all`: every one, from updateEpithets at the end of the yearly group)
void flushPending(World& w, bool all) {
	ChronicleScratch& S = w.scratch<ChronicleScratch>();
	if (S.pending.empty()) return;
	std::vector<ChronicleScratch::Pending> due;
	for (size_t i = 0; i < S.pending.size();) {
		const ChronicleScratch::Pending& pe = S.pending[i];
		if (all || pe.tick != w.tick || pe.year != fin(w.year)) {
			due.push_back(pe);
			S.pending.erase(S.pending.begin() + std::ptrdiff_t(i));
		} else {
			i++;
		}
	}
	const ChronicleTune& T = tuneOf(w);
	for (const ChronicleScratch::Pending& pe : due) {
		if (pe.drown) {
			if (sett(w, pe.a)) tellDrowned(w, pe.a, pe.b, pe.cell, pe.tier);
			continue;
		}
		if (pe.war) {
			if (!cult(w, pe.a) || !cult(w, pe.b)) continue;
			Ctx x;
			ctxCulture(x, w, pe.a);
			ctxCulture(x, w, pe.b, "2");
			write(w, "peace", 2, -1, pe.a, tpl(kWarDraw), "war-draw", x);
		} else {
			const Settlement* s = sett(w, pe.a);
			if (!s || s->dead) continue;
			const int cid = s->culture;
			if (!minorOk(w, "plague-end", cid, T.minorGap)) continue;
			Ctx x;
			ctxSettlement(x, w, pe.a);
			ctxCulture(x, w, cid);
			write(w, "plague-end", 1, s->cell, cid, tpl(kPlagueEnd), "plague-end", x);
		}
	}
}

template <class T> void sub(World& w, ChronicleScratch& S, void (*fn)(World&, const T&)) {
	World* wp = &w;
	S.tokens.push_back(w.bus.on<T>([wp, fn](const T& e) { flushPending(*wp, false); fn(*wp, e); }));
}

// ====================================================================== epithets

enum Cat : int {
	CatWaker = 0, CatAncestral, CatSilent, CatMixed,
	CatRain, CatGreen, CatHealer, CatLamp, CatPeace, CatVoice, CatShaper,
	CatDrowner, CatThunder, CatShaker, CatFever, CatStar, CatFire,
	CatBeloved, CatDreaded,          // from society::regard when no deed stands out
	CatCount
};
// (voice, section 3) the god's epithets by the behaviour that earns them. The voice's 40 come first in
// each group; groups the voice gave fewer than three names keep older ones beside them. The names are
// fixed titles (the voice keeps "She Who Burns the Sky" and "He Who Moves the Hills" as written).
//   Waker       nothing distinct seen yet (voice fallback: "the Maker")
//   Ancestral   a daughter people with nothing of its own seen
//   Silent      voice "Absent": no deed seen for silentYears
//   Mixed       voice "Chaotic": mercy and wrath both remembered
//   Rain / Green / Peace / Drowner                     voice groups of the same name
//   Healer / Lamp / Voice                              voice "Healing and grace": heal / inspire / prophet
//   Thunder / Star                                     voice "Fire-throwing": lightning / meteor
//   Shaper / Shaker / Fire                             voice "Earth-shaking": raise-lower / quake / volcano
//   Fever                                              voice "Plague-sending"
//   Beloved     voice "Kind": loved (regard) with no single deed, or kindness of many kinds
//   Dreaded     voice "Cruel": feared (regard) with no single deed, or wrath of many kinds
const char* const kEpWaker[] = { "the Maker", "the Waker", "the First Voice", "the Dawn-Caller" };
const char* const kEpAncestral[] = { "the God of Our Fathers", "the Old God", "the Remembered One", "the Elder" };
const char* const kEpSilent[] = { "the Silent One", "the Sleeper", "the Empty Chair", "the Rumour" };
const char* const kEpMixed[] = { "the Two-Handed", "the Laughing Storm", "the Coin-Tosser" };
const char* const kEpRain[] = { "the Rain-Giver", "the Cloud-Shepherd", "She Who Opens the Sky", "the Well-Filler" };
const char* const kEpGreen[] = { "the Green Hand", "the Sower", "Lord of the Second Spring" };
const char* const kEpHealer[] = { "the Physician of Towns", "the Breath That Mends", "the Healer" };
const char* const kEpLamp[] = { "the Whisperer of Crafts", "the Lamp-Bearer", "the Teacher" };
const char* const kEpVoice[] = { "the Sender of Prophets", "the Voice", "the Mouth of Heaven" };
const char* const kEpShaper[] = { "He Who Moves the Hills", "the Potter", "the Shaper" };
const char* const kEpDrowner[] = { "the Drowner", "the Sea's Elbow", "the Deep" };
const char* const kEpThunder[] = { "the Lightning-Handed", "the Thunderer", "the Spear of Heaven" };
const char* const kEpShaker[] = { "the Shaker of Floors", "the Earth-Shaker", "the Restless Ground" };
const char* const kEpFever[] = { "the Sick Wind", "the Grey Breath", "the Bringer of Fevers" };
const char* const kEpStar[] = { "She Who Burns the Sky", "the Hammer", "the Thrower of Stars" };
const char* const kEpFire[] = { "the Mountain-Maker", "the Fire-Mountain", "the Ash-Maker" };
const char* const kEpPeace[] = { "the Breaker of Spears", "the Quiet Between Armies", "the Peacemaker" };
const char* const kEpBeloved[] = { "the Gentle Hand", "the Kind Sky", "Mother of Harvests", "the Patient One", "He Who Keeps the Lamp Lit" };
const char* const kEpDreaded[] = { "the Unkind Sky", "the Hungry One", "the Tyrant Above", "the One We Do Not Name" };
const Tpl kEpithets[CatCount] = {
	tpl(kEpWaker), tpl(kEpAncestral), tpl(kEpSilent), tpl(kEpMixed),
	tpl(kEpRain), tpl(kEpGreen), tpl(kEpHealer), tpl(kEpLamp), tpl(kEpPeace), tpl(kEpVoice), tpl(kEpShaper),
	tpl(kEpDrowner), tpl(kEpThunder), tpl(kEpShaker), tpl(kEpFever), tpl(kEpStar), tpl(kEpFire),
	tpl(kEpBeloved), tpl(kEpDreaded),
};
// (voice review 2, lead decision) how many names at the head of each group are the approved voice's 40
// (section 3; "the Maker" is its fallback). They are each people's primary choice: an invented name
// (kept where it fits the voice) is drawn for about one people in five, and only in groups that have one.
constexpr int kEpApproved[CatCount] = {
	1, 0, 4, 3,
	4, 3, 2, 1, 2, 1, 1,
	2, 1, 1, 2, 3, 1,
	5, 4,
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
	// (voice review 2) "Two-Handed" needs blows from the god's own hand: towns lost to a rising sea name the
	// god the Drowner when they dominate, but do not turn a kind god two-handed (a kind god of seed 3 was
	// 'the Two-Handed' to four peoples for 900 years because the warming sea took their coasts)
	const double wrathActs = std::max(0.0, s.wrath - m.m[KDrown]);
	if (s.benev >= mixedShare * s.total && wrathActs >= mixedShare * s.total) {
		// both hands remembered: a people that has come to hate its god only remembers the blows
		if (fin(society::regard(w, c.id).love) <= T.dreadLove) return CatDreaded;
		return CatMixed;
	}
	// (voice "Kind" / "Cruel") kindness or wrath of many kinds, none of them dominant, names the god by
	// how the people feel; a people already calling its god so keeps the name until one deed reaches
	// kindHold of that side (hysteresis)
	{
		double maxB = 0, maxW = 0;
		for (int k = CatRain; k <= CatVoice; k++) maxB = std::max(maxB, s.cat[k]);
		for (int k = CatDrowner; k <= CatFire; k++) maxW = std::max(maxW, s.cat[k]);
		const double spreadB = m.cat == CatBeloved ? T.kindHold : T.kindSpread;
		const double spreadW = m.cat == CatDreaded ? T.kindHold : T.kindSpread;
		const bool manyKind = s.benev >= T.kindShare * s.total && maxB < spreadB * s.benev;
		const bool manyCruel = s.wrath >= T.kindShare * s.total && maxW < spreadW * s.wrath;
		if (manyKind || manyCruel) {
			const double love = fin(society::regard(w, c.id).love);
			if (manyKind && love >= T.kindLove) return CatBeloved;
			if (manyCruel && love <= T.cruelLove) return CatDreaded;
		}
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
	const Tpl& g = kEpithets[cat];
	if (g.n <= 0) return string("the Maker");
	const int approved = std::clamp(kEpApproved[cat], 0, g.n), invented = g.n - approved;
	const bool useInvented = invented > 0 && (approved == 0 || (h / 7u) % 5u == 0);
	if (!useInvented) return string(g.t[h % uint32_t(approved)]);
	return string(g.t[approved + int(h % uint32_t(invented))]);
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
	// (verifier) the words that open the templates themselves (every capitalised literal word that starts a
	// line or an alternative), so no quoted entry keeps a capital in the middle of a sentence
	static const char* const kCommon[] = { "The", "A", "An", "In", "At", "Here", "Again", "Nowhere", "So", "Thus", "From",
		"Far", "Fire", "Of", "Where", "Many", "No", "There", "Settlers", "Families", "Wanderers", "Something", "Lightning",
		"Before", "After", "Among", "Hunger", "War", "Rain", "Green", "Half", "One", "For", "It",
		"Ash", "Black", "Caravans", "Children", "Chimneys", "Clouds", "Enemies", "Envoys", "Every", "Famine", "Fearing",
		"Fever", "Few", "Fishermen", "Generations", "Hills", "Ice", "Its", "Just", "Masts", "Merchants", "Mines", "Mist",
		"Near", "Nothing", "Old", "Once", "Out", "Outside", "Over", "Ox-carts", "Rivers", "Scholars", "Smoke", "Soft",
		"Some", "Still", "Ten", "That", "Their", "Then", "They", "Thunder", "Traders", "Walls", "Water", "Weights",
		"When", "Wind", "Another", "Around", "By", "Each", "Mothers", "Priests", "Women", "Sons", "Stones" };
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

// (voice) "In the 312th year, a band went out ..." -> "a band went out ...": the tale gives the year itself
string dropYearOpener(const string& t) {
	if (!startsWith(t, "In the ")) return t;
	const size_t sp = t.find(' ', 7);
	if (sp == string::npos || t.compare(sp, 5, " year") != 0) return t;
	const string ord = t.substr(7, sp - 7);
	const bool isOrd = ord == "first" || (ord.size() > 2 && std::isdigit(static_cast<unsigned char>(ord[0])) &&
		(startsWith(ord.substr(ord.size() - 2), "st") || startsWith(ord.substr(ord.size() - 2), "nd") ||
		 startsWith(ord.substr(ord.size() - 2), "rd") || startsWith(ord.substr(ord.size() - 2), "th")));
	if (!isOrd) return t;
	size_t k = sp + 5;
	if (k < t.size() && t[k] == ',') k++;
	while (k < t.size() && t[k] == ' ') k++;
	if (k >= t.size()) return t;
	return t.substr(k);                                     // the sentence goes on ("a band went out ...")
}
// (voice) the same entry quoted in the tale: a year named inside it ("In the 3284th year the last of
// its people rowed away") is the quoted year: "In that year ..."
string thatYear(string t) {
	for (const char* lead : { "In the ", "in the " }) {
		for (size_t p = t.find(lead); p != string::npos; p = t.find(lead, p + 1)) {
			const size_t a = p + 7, sp = t.find(' ', a);
			if (sp == string::npos || t.compare(sp, 5, " year") != 0 || a >= t.size() || !std::isdigit(static_cast<unsigned char>(t[a]))) continue;
			t.replace(p, sp + 5 - p, string(lead[0] == 'I' ? "In" : "in") + " that year");
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
	sub<EvPowerUsed>(w, S, onPowerUsed);
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
	flushPending(w, true);                                 // (voice review 2) the year's held-back endings
	flushDigest(w);                                        // (chronicle budget) the last decade's digest, if one is due
	pruneStrokes(w);
	// (verify) the people whose name for the god is the god's own (the most populous, as below): its renames are
	// story; a smaller people's later renames are tellable (in a chaotic god's book renames were two notable
	// entries a real minute)
	int mainC = -1;
	{
		double mp = -1;
		for (size_t i = 0; i < w.cultures.size(); i++) {
			if (w.cultures[i].dead) continue;
			const double p = popOf(w, int(i));
			if (p > mp) { mp = p; mainC = int(i); }
		}
	}
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
			if (!old.empty() && cid != mainC) w.scratch<ChronicleScratch>().tellNext = true;
			if (old.empty() && w.cultures[i].parent >= 0) write(w, "epithet", 2, -1, cid, tpl(kEpiFirstDaughter), "epithet-first-d", x);
			else if (old.empty()) write(w, "epithet", 2, -1, cid, tpl(kEpiFirst), "epithet-first", x);
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
		x.set("seaw", share > 0.75 ? "nearly all" : share > 0.55 ? "the greater part" : share > 0.35 ? "half" : "a little");
		// (voice 2, 3) the comets and the first green, as the voice tells them
		static const char* const kP[] = {
			"In the beginning {W} was fire and then stone, and the god called ice down out of the dark. Where it fell, the hollows filled, and that was the first sea, and in time it covered {seaw} of the world. Then green came out of the wet places, slowly at first, as if it were not sure it was allowed.",
			"In the beginning {W} was fire. The god cooled it and called down the ice of heaven, until the sea covered {seaw} of its face. Then green came upon the land, and beasts, and the god looked for a people to wake.",
			"Before the counting of years, {W} was a ball of molten stone in the dark. The god let it cool, filled its hollows with the water of comets until the sea held {seaw} of the world, and planted the first green on the shores.",
		};
		static const char* const kDry[] = {
			"In the beginning {W} was fire and then stone. The god cooled it but called down little water, and no sea ever lay on it. Then green came out of the damp places, slowly at first, as if it were not sure it was allowed.",
		};
		if (share <= 0.05) out.paragraphs.push_back(expandOne(kDry[0], x, r));
		else {
			const int k = r.pickIndex(4);                      // the voice's telling half the time
			out.paragraphs.push_back(expandOne(kP[k <= 1 ? 0 : k - 1], x, r));
		}
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
				if (c.id == first || culturePlural(w, c.id) == culturePlural(w, first)) continue;   // not "the Exi ... after them came the Exi"
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
		// (voice) the ages as the entries name them: "the Age of Iron", "the Age of the Atom"
	static const char* const kAgeOf[Age::COUNT] = { "Stone", "Bronze", "Iron", "Sail", "Industry", "the Atom", "the Stars" };
	if (topAge > 0) p += string(" The furthest of them reached the Age of ") + kAgeOf[topAge] + ".";
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
		// (voice review 2) "In the year 1,723, the Gigasks are no more" / "..., where Sal Worirsian stood, fish now
		// swim": entries told in the present tense are not quoted after a year
		for (const ChronicleEntry& e : w.chronicle)
			if (e.weight >= 3 && e.type != "genesis" && e.type != "ending" && !(w.ending && (e.type == "ark" || e.type == "lost")) &&
				e.text.find(" are no more") == string::npos && e.text.find(" now swim") == string::npos) big.push_back(&e);
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
				p += " " + string(y < 1 ? "In the first year" : "In the year " + fmtInt(std::floor(y))) + ", " + lowerLead(thatYear(dropYearOpener(e->text)));
			}
			out.paragraphs.push_back(p);
		}
	}

	// ---- 8. the end
	{
		string p;
		if (w.ending) {
			// (voice, section 4) the ending passage of this ending id
			const Ending& en = *w.ending;
			Ctx ex;
			endingCtx(ex, w, en.mood, en.culture, en.settlement, en.year);
			string key;
			const Tpl t = endingTpl(en.mood, key);
			p = expandOne(t.t[r.pickIndex(t.n)], ex, r);
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
