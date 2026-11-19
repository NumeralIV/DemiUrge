// DEMIURGE — Sim/Names.cpp : procedural languages and names (SPEC-SIM 3.1, DM.Names).
// Owner: names engineer (A1, key "names"). Built fresh from SPEC-SIM 3.1 (docs/UNREAL.md owner
// decision: nothing of js/sim/names.js is ported). One idea is taken from the old file: the
// player-god's word belongs to the language and passes through the sound shifts, so related
// cultures call the god by related names.
//
// A language is an opaque text record, "dm1;key=value;...", stored as Culture::lang and saved as
// is. Names parses it on every call (no globals, no caches). The record holds weighted phoneme
// inventories ("th:2,k:3"), per-mille probabilities, the word lists (roots, prefixes, endings) and
// the language's word for the player-god. Every random draw comes from the Rng argument, and every
// container is walked in index order.
#include "Names.h"

#include <algorithm>
#include <cstddef>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dm::names {
namespace {

// ============================================================ the phonology record

struct Tok {
	std::string s;          // a sound or a cluster, lower case: "a", "th", "kr"
	int w = 1;              // relative weight, >= 1
};

struct Phon {
	bool harmony = false;   // vowel harmony: a word is all front (fr) or all back (bk) vowels
	bool hyphen = false;    // places may be joined by a hyphen: "Qe-Luun"
	bool doubles = false;   // a single medial coda may be doubled: "Tessa"
	int pClose = 300;       // per mille: a non-final syllable has a coda
	int pEmpty = 150;       // per mille: an onset is empty
	int pCluster = 0;       // per mille: an onset is a cluster (when clusters exist)
	int pLong = 200;        // per mille: a nucleus is a long vowel or diphthong (not in harmony)
	int pFinal = 400;       // per mille: a word ends in a vowel
	int pApos = 0;          // per mille: places may split with an apostrophe (0 = never)
	int minSyl = 2, maxSyl = 3;
	std::vector<Tok> onset, coda, clusters, codaClu, vowel, longV, front, back;
	std::vector<std::string> roots, prefix, placeEnd, personEnd, godEnd, adjEnd;
	std::string plural = "i";   // culture plural: i | s | en | ae | ar
	std::string god = "ama";    // the player-god's word in this language (lower-case letters)
};

// ============================================================ text record: parse and write

bool isVowelCh(char c) { return c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u'; }
bool isLetterCh(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
char lowerCh(char c) { return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c; }
bool isSep(char c) { return c == ' ' || c == '-' || c == '\''; }
bool godWordOk(const std::string& g);   // defined with the acceptance rules below

void lowerInPlace(std::string& s) {
	for (char& c : s) c = lowerCh(c);
}

// Non-negative or negative decimal integer; anything else gives def.
int parseInt(std::string_view v, int def) {
	size_t i = 0;
	bool neg = false;
	if (i < v.size() && v[i] == '-') { neg = true; i++; }
	if (i >= v.size()) return def;
	long x = 0;
	for (; i < v.size(); i++) {
		const char c = v[i];
		if (c < '0' || c > '9') return def;
		x = x * 10 + (c - '0');
		if (x > 100000000L) return def;
	}
	return int(neg ? -x : x);
}

// Calls fn(piece) for every sep-separated piece of v (empty pieces included). No allocation: parsing a
// language runs on every name, so this must stay cheap.
template <class Fn> void eachPiece(std::string_view v, char sep, Fn&& fn) {
	size_t start = 0;
	for (size_t i = 0; i < v.size(); i++) {
		if (v[i] == sep) {
			fn(v.substr(start, i - start));
			start = i + 1;
		}
	}
	fn(v.substr(start));
}

// "th:2,k:3,m" -> tokens with weights (a missing weight is 1).
void parseTokens(std::string_view v, std::vector<Tok>& out) {
	out.reserve(out.size() + size_t(std::count(v.begin(), v.end(), ',')) + 1);
	eachPiece(v, ',', [&out](std::string_view item) {
		if (item.empty()) return;
		const size_t colon = item.find(':');
		Tok t;
		t.s.assign(item.substr(0, colon));
		t.w = colon == std::string_view::npos ? 1 : std::max(1, parseInt(item.substr(colon + 1), 1));
		if (!t.s.empty()) out.push_back(std::move(t));
	});
}

// "ak,gar,dun" -> word list.
void parseStrings(std::string_view v, std::vector<std::string>& out) {
	out.reserve(out.size() + size_t(std::count(v.begin(), v.end(), ',')) + 1);
	eachPiece(v, ',', [&out](std::string_view item) {
		if (!item.empty()) out.emplace_back(item);
	});
}

// Defaults for anything missing, and clamps so that no record can break the generators.
void finalize(Phon& P) {
	P.pClose = std::clamp(P.pClose, 0, 1000);
	P.pEmpty = std::clamp(P.pEmpty, 0, 1000);
	P.pCluster = std::clamp(P.pCluster, 0, 1000);
	P.pLong = std::clamp(P.pLong, 0, 1000);
	P.pFinal = std::clamp(P.pFinal, 0, 1000);
	P.pApos = std::clamp(P.pApos, 0, 1000);
	P.minSyl = std::clamp(P.minSyl, 1, 5);
	P.maxSyl = std::clamp(P.maxSyl, P.minSyl, 6);
	for (Tok& t : P.onset) lowerInPlace(t.s);
	for (Tok& t : P.coda) lowerInPlace(t.s);
	for (Tok& t : P.clusters) lowerInPlace(t.s);
	for (Tok& t : P.codaClu) lowerInPlace(t.s);
	for (Tok& t : P.vowel) lowerInPlace(t.s);
	for (Tok& t : P.longV) lowerInPlace(t.s);
	for (Tok& t : P.front) lowerInPlace(t.s);
	for (Tok& t : P.back) lowerInPlace(t.s);
	if (P.onset.empty()) parseTokens("l:3,m:2,n:3,r:3,s:2,t:2,d:1,k:1", P.onset);
	if (P.harmony) {
		if (P.front.empty()) parseTokens("e:2,i:2", P.front);
		if (P.back.empty()) parseTokens("a:2,o:2,u:2", P.back);
	} else if (P.vowel.empty()) {
		parseTokens("a:3,e:3,i:3,o:3,u:2", P.vowel);
	}
	if (P.plural != "i" && P.plural != "s" && P.plural != "en" && P.plural != "ae" && P.plural != "ar") P.plural = "i";
	// roots are returned as words: lower case (archetype prefixes stay capitalised, placeOnce capitalises them)
	for (std::string& root : P.roots) lowerInPlace(root);
	// the god's word is returned as a name: 3..12 letters, pronounceable, not blocklisted, else the default
	lowerInPlace(P.god);
	if (!godWordOk(P.god)) P.god = "ama";
}

// Unparsable text gives the built-in default language (never throws, never empty).
Phon parsePhon(std::string_view text, bool& ok) {
	Phon P;
	ok = text.size() >= 3 && text.substr(0, 3) == "dm1";
	if (ok) {
		eachPiece(text.substr(3), ';', [&P](std::string_view field) {
			const size_t eq = field.find('=');
			if (eq == std::string_view::npos) return;
			const std::string_view key = field.substr(0, eq);
			const std::string_view val = field.substr(eq + 1);
			if (key == "h") P.harmony = parseInt(val, 0) != 0;
			else if (key == "hy") P.hyphen = parseInt(val, 0) != 0;
			else if (key == "db") P.doubles = parseInt(val, 0) != 0;
			else if (key == "pc") P.pClose = parseInt(val, P.pClose);
			else if (key == "pe") P.pEmpty = parseInt(val, P.pEmpty);
			else if (key == "pk") P.pCluster = parseInt(val, P.pCluster);
			else if (key == "pl") P.pLong = parseInt(val, P.pLong);
			else if (key == "pf") P.pFinal = parseInt(val, P.pFinal);
			else if (key == "pa") P.pApos = parseInt(val, P.pApos);
			else if (key == "sy") {
				const size_t comma = val.find(',');
				P.minSyl = parseInt(val.substr(0, comma), P.minSyl);
				if (comma != std::string_view::npos) P.maxSyl = parseInt(val.substr(comma + 1), P.maxSyl);
			}
			else if (key == "on") parseTokens(val, P.onset);
			else if (key == "co") parseTokens(val, P.coda);
			else if (key == "cl") parseTokens(val, P.clusters);
			else if (key == "cc") parseTokens(val, P.codaClu);
			else if (key == "vo") parseTokens(val, P.vowel);
			else if (key == "lo") parseTokens(val, P.longV);
			else if (key == "fr") parseTokens(val, P.front);
			else if (key == "bk") parseTokens(val, P.back);
			else if (key == "ro") parseStrings(val, P.roots);
			else if (key == "pr") parseStrings(val, P.prefix);
			else if (key == "pn") parseStrings(val, P.placeEnd);
			else if (key == "qn") parseStrings(val, P.personEnd);
			else if (key == "gn") parseStrings(val, P.godEnd);
			else if (key == "an") parseStrings(val, P.adjEnd);
			else if (key == "pu") P.plural = std::string(val);
			else if (key == "gd") P.god = std::string(val);
		});
	}
	finalize(P);
	return P;
}

void putInt(std::string& o, const char* key, int x) {
	o += ';';
	o += key;
	o += '=';
	o += std::to_string(x);
}
void putText(std::string& o, const char* key, const std::string& x) {
	o += ';';
	o += key;
	o += '=';
	o += x;
}
void putTokens(std::string& o, const char* key, const std::vector<Tok>& v) {
	o += ';';
	o += key;
	o += '=';
	for (size_t i = 0; i < v.size(); i++) {
		if (i > 0) o += ',';
		o += v[i].s;
		o += ':';
		o += std::to_string(v[i].w);
	}
}
void putStrings(std::string& o, const char* key, const std::vector<std::string>& v) {
	o += ';';
	o += key;
	o += '=';
	for (size_t i = 0; i < v.size(); i++) {
		if (i > 0) o += ',';
		o += v[i];
	}
}

std::string writePhon(const Phon& P) {
	std::string o = "dm1";
	putInt(o, "h", P.harmony ? 1 : 0);
	putInt(o, "hy", P.hyphen ? 1 : 0);
	putInt(o, "db", P.doubles ? 1 : 0);
	putInt(o, "pc", P.pClose);
	putInt(o, "pe", P.pEmpty);
	putInt(o, "pk", P.pCluster);
	putInt(o, "pl", P.pLong);
	putInt(o, "pf", P.pFinal);
	putInt(o, "pa", P.pApos);
	o += ";sy=" + std::to_string(P.minSyl) + "," + std::to_string(P.maxSyl);
	putTokens(o, "on", P.onset);
	putTokens(o, "co", P.coda);
	putTokens(o, "cl", P.clusters);
	putTokens(o, "cc", P.codaClu);
	putTokens(o, "vo", P.vowel);
	putTokens(o, "lo", P.longV);
	putTokens(o, "fr", P.front);
	putTokens(o, "bk", P.back);
	putStrings(o, "ro", P.roots);
	putStrings(o, "pr", P.prefix);
	putStrings(o, "pn", P.placeEnd);
	putStrings(o, "qn", P.personEnd);
	putStrings(o, "gn", P.godEnd);
	putStrings(o, "an", P.adjEnd);
	putText(o, "pu", P.plural);
	putText(o, "gd", P.god);
	return o;
}

// ============================================================ acceptance: pronounceable, clean, not the god

// Letters, spaces, hyphens and apostrophes only. Consonant runs of 4+ and runs of 3+ identical
// letters are refused, and so are 3+ vowels in a row (vowels: a e i o u; y is a consonant). Each
// separator must sit between two letters.
bool pronounceable(const std::string& s) {
	if (s.empty()) return false;
	int cons = 0, vow = 0, same = 0;
	char prev = 0;
	for (size_t i = 0; i < s.size(); i++) {
		const char raw = s[i];
		if (isSep(raw)) {
			if (i == 0 || i + 1 >= s.size() || !isLetterCh(s[i - 1]) || !isLetterCh(s[i + 1])) return false;
			cons = 0;
			vow = 0;
			same = 0;
			prev = 0;
			continue;
		}
		if (!isLetterCh(raw)) return false;
		const char c = lowerCh(raw);
		same = (c == prev) ? same + 1 : 1;
		if (same >= 3) return false;
		prev = c;
		if (isVowelCh(c)) {
			vow++;
			cons = 0;
			if (vow >= 3) return false;
		} else {
			cons++;
			vow = 0;
			if (cons >= 4) return false;
		}
	}
	return true;
}

bool isWholeProfanity(const std::string& w) {
	static const char* const kWhole[] = { "ass", "cum", "tit", "fag", "poo", "pee", "gook", "spic", "coon", "kike", "chink", "wop", "dyke",
		"damn", "hell", "crap", "sex", "homo", "butt", "shag", "turd", "nazi", "queer", "poop" };
	for (const char* x : kWhole) {
		if (w == x) return true;
	}
	return false;
}

// A small blocklist: fragments are refused anywhere, short words only as a whole piece.
bool profane(const std::string& s) {
	static const char* const kFrag[] = { "fuck", "fuk", "shit", "cunt", "cock", "dick", "piss", "slut", "whore", "bitch", "twat", "wank", "nigg", "nigr", "fagg", "rape", "anus", "arse", "penis", "vagin", "porn",
		"bugger", "bollocks", "bastard", "pussy", "dildo", "wanker", "tosser", "boob", "retard", "bloody", "prick", "jizz" };
	std::string flat;
	std::string cur;
	for (char c : s) {
		if (isLetterCh(c)) {
			flat += lowerCh(c);
			cur += lowerCh(c);
		} else {
			if (isWholeProfanity(cur)) return true;
			cur.clear();
		}
	}
	if (isWholeProfanity(cur)) return true;
	for (const char* f : kFrag) {
		if (flat.find(f) != std::string::npos) return true;
	}
	return false;
}

std::string lettersLower(const std::string& s) {
	std::string r;
	for (char c : s) {
		if (isLetterCh(c)) r += lowerCh(c);
	}
	return r;
}

// Length, shape, blocklist, and never the player-god's own name (SPEC: no town, person or people
// is given exactly that name).
bool acceptable(const std::string& s, const Phon& P, int lo, int hi) {
	const int n = int(s.size());
	if (n < lo || n > hi) return false;
	if (!pronounceable(s) || profane(s)) return false;
	return lettersLower(s) != P.god;
}

// The god's word (a language field, so it may be hand-written text): 3..14 lower-case letters (a sound
// shift may lengthen a 12-letter word by one), pronounceable and not blocklisted. makeGod and the sound
// shifts always produce such words.
bool godWordOk(const std::string& g) {
	if (g.size() < 3 || g.size() > size_t(kNamesTune.maxLen)) return false;
	for (char c : g) {
		if (!(c >= 'a' && c <= 'z')) return false;
	}
	return pronounceable(g) && !profane(g);
}

// ============================================================ generation primitives

double wOf(const Tok& t) { return double(t.w); }

const std::string* pickTok(const std::vector<Tok>& v, Rng& r) {
	const Tok* t = r.weighted(v, wOf);
	return t ? &t->s : nullptr;
}

std::string onsetOf(const Phon& P, Rng& r) {
	if (!P.clusters.empty() && r.chance(P.pCluster / 1000.0)) {
		if (const std::string* c = pickTok(P.clusters, r)) return *c;
	}
	if (r.chance(P.pEmpty / 1000.0)) return std::string();
	if (const std::string* o = pickTok(P.onset, r)) return *o;
	return std::string();
}

// cls: 1 = front harmony class, 2 = back, 0 = no harmony (the plain vowel list).
std::string vowelOf(const Phon& P, Rng& r, int cls) {
	const std::vector<Tok>* src = &P.vowel;
	if (P.harmony) {
		src = (cls == 1) ? &P.front : &P.back;
	} else if (!P.longV.empty() && r.chance(P.pLong / 1000.0)) {
		src = &P.longV;
	}
	if (const std::string* v = pickTok(*src, r)) return *v;
	return "a";
}

std::string syllableOf(const Phon& P, Rng& r, int cls, bool last) {
	std::string s = onsetOf(P, r);
	s += vowelOf(P, r, cls);
	const bool coda = last ? !r.chance(P.pFinal / 1000.0) : r.chance(P.pClose / 1000.0);
	if (coda) {
		const bool clu = !P.codaClu.empty() && r.chance(0.25);
		const std::string* c = clu ? pickTok(P.codaClu, r) : pickTok(P.coda, r);
		if (c) {
			s += *c;
			if (P.doubles && !last && c->size() == 1 && r.chance(0.2)) s += *c;
		}
	}
	return s;
}

std::vector<std::string> sylsOf(const Phon& P, Rng& r, int n) {
	const int count = std::clamp(n, 1, 6);
	const int cls = P.harmony ? (r.chance(0.5) ? 1 : 2) : 0;
	std::vector<std::string> out;
	for (int i = 0; i < count; i++) out.push_back(syllableOf(P, r, cls, i == count - 1));
	return out;
}

std::string joinSyl(const std::vector<std::string>& syl) {
	std::string o;
	for (const std::string& x : syl) o += x;
	return o;
}

// The syllables with an apostrophe in front of syllable `at` (1 <= at < size).
std::string joinApos(const std::vector<std::string>& syl, size_t at) {
	std::string o;
	for (size_t i = 0; i < syl.size(); i++) {
		if (i == at && i > 0) o += '\'';
		o += syl[i];
	}
	return o;
}

// The syllable nuclei of a bare word, counted as its runs of vowels (a diphthong is one nucleus).
int nucleiOf(const std::string& w) {
	int n = 0;
	bool inVowels = false;
	for (char c : w) {
		const bool v = isVowelCh(c);
		if (v && !inVowels) n++;
		inVowels = v;
	}
	return n;
}

// A bare word: a root (chance 0.3, when there are roots and it has lo..hi nuclei) or lo..hi syllables.
std::string wordCore(const Phon& P, Rng& r, int lo, int hi) {
	if (!P.roots.empty() && r.chance(0.3)) {
		const std::string root = *r.pick(P.roots);
		const int n = nucleiOf(root);
		if (n >= lo && n <= hi) return root;
	}
	return joinSyl(sylsOf(P, r, r.integer(lo, hi)));
}

// Harmony endings: 'V' becomes a vowel of the base's class (e/i after front vowels, a/o/u otherwise).
std::string resolveEnding(const std::string& e, const std::string& base, Rng& r) {
	int cls = 2;
	for (char c : base) {
		if (isVowelCh(c)) cls = (c == 'e' || c == 'i') ? 1 : 2;
	}
	std::string out;
	for (char c : e) {
		if (c == 'V') {
			if (cls == 1) {
				out += r.chance(0.5) ? 'e' : 'i';
			} else {
				const char back[3] = { 'a', 'o', 'u' };
				out += back[r.pickIndex(3)];
			}
		} else {
			out += lowerCh(c);
		}
	}
	return out;
}

// base + suffix; a base's final vowel gives way to a suffix that starts with a vowel.
std::string fuse(std::string base, const std::string& suffix) {
	if (suffix.empty()) return base;
	if (base.empty()) return suffix;
	if (isVowelCh(base.back()) && isVowelCh(suffix.front())) base.pop_back();
	return base + suffix;
}

std::string pluralize(std::string stem, const std::string& mode) {
	if (stem.empty()) return stem;
	const bool endsVowel = isVowelCh(stem.back());
	if (mode == "s") return stem + "s";
	if (mode == "en" || mode == "ae") {
		if (endsVowel) stem.pop_back();
		return stem + mode;
	}
	if (mode == "ar") {
		if (endsVowel) stem.pop_back();
		return stem + (stem.back() == 'r' ? "ak" : "ar");
	}
	if (endsVowel) {
		stem.back() = 'i';
		return stem;
	}
	return stem + "i";
}

// ============================================================ regular sound shifts (daughter languages)

// Regular sound changes applied to the lexicon of a daughter language. id 0..7.
std::string shiftWord(const std::string& s, int id) {
	std::string t = s;
	if (t.empty()) return t;
	switch (id) {
	case 0: if (t[0] == 'k') t[0] = 'h'; break;                              // k > h at the start
	case 1:                                                                  // t > d between vowels
		for (size_t i = 1; i + 1 < t.size(); i++) {
			if (t[i] == 't' && isVowelCh(t[i - 1]) && isVowelCh(t[i + 1])) t[i] = 'd';
		}
		break;
	case 2: if (t[0] == 'p') t[0] = 'f'; break;                              // p > f at the start
	case 3: if (t[0] == 's' && t.size() > 1 && isVowelCh(t[1])) t.insert(size_t(1), 1, 'h'); break; // s > sh before a vowel
	case 4: if (t.size() >= 4 && isVowelCh(t.back())) t.pop_back(); break;   // the final vowel is lost
	case 5:                                                                  // a > o in the first a
		for (char& c : t) {
			if (c == 'a') { c = 'o'; break; }
		}
		break;
	case 6: for (char& c : t) { if (c == 'e') c = 'i'; } break;               // e > i everywhere
	case 7:                                                                  // b > v between vowels
		for (size_t i = 1; i + 1 < t.size(); i++) {
			if (t[i] == 'b' && isVowelCh(t[i - 1]) && isVowelCh(t[i + 1])) t[i] = 'v';
		}
		break;
	default: break;
	}
	return t;
}

// A shifted form when it is still pronounceable, else the original.
std::string shiftedOrSame(const std::string& s, int id) {
	const std::string t = shiftWord(s, id);
	return (!t.empty() && pronounceable(t)) ? t : s;
}

// ============================================================ building a language

std::vector<std::string> makeRoots(const Phon& P, Rng& r, int count) {
	std::vector<std::string> out;
	for (int guard = 0; guard < count * 24 && int(out.size()) < count; guard++) {
		const int n = r.chance(0.4) ? 1 : 2;
		const std::string root = joinSyl(sylsOf(P, r, n));
		if (root.size() < 2 || root.size() > 7) continue;
		if (!pronounceable(root) || profane(root)) continue;
		if (std::find(out.begin(), out.end(), root) != out.end()) continue;
		out.push_back(root);
	}
	return out;
}

std::string makeGod(const Phon& P, Rng& r) {
	for (int t = 0; t < 24; t++) {
		std::string base = (!P.roots.empty() && r.chance(0.5)) ? *r.pick(P.roots) : joinSyl(sylsOf(P, r, r.integer(1, 2)));
		if (!P.godEnd.empty() && r.chance(0.8)) base = fuse(base, resolveEnding(*r.pick(P.godEnd), base, r));
		if (base.size() < 3 || base.size() > 12) continue;
		if (!pronounceable(base) || profane(base)) continue;
		return base;
	}
	return "ama";
}

int jitterP(Rng& r, int v) {
	const int d = v / 6;
	return std::clamp(v + r.integer(-d, d), 0, 1000);
}

// Keeps most of an inventory, jitters its weights; never drops everything.
void thinTokens(Rng& r, std::vector<Tok>& v) {
	// at least three sounds of each kind survive (with one vowel a language repeats its names)
	const size_t goal = std::min<size_t>(3, v.size());
	std::vector<Tok> kept;
	for (size_t i = 0; i < v.size(); i++) {
		const size_t left = v.size() - i;                                  // this token and those after it
		const size_t need = goal > kept.size() ? goal - kept.size() : 0;   // sounds still needed to reach goal
		if (left > need && v.size() > 3 && !r.chance(0.8)) continue;
		Tok k = v[i];
		k.w = std::max(1, v[i].w + r.integer(-1, 1));
		kept.push_back(k);
	}
	v = kept;
}

void thinStrings(Rng& r, std::vector<std::string>& v) {
	if (v.size() <= 1) return;
	std::vector<std::string> kept;
	for (const std::string& x : v) {
		if (r.chance(0.75)) kept.push_back(x);
	}
	if (kept.empty()) kept.push_back(v[size_t(r.pickIndex(int(v.size())))]);
	v = kept;
}

// One culture of the world's language families. The shapes are data; the numbers are jittered.
struct Arch {
	bool harmony, hyphen, doubles;
	int pClose, pEmpty, pCluster, pLong, pFinal, pApos, minSyl, maxSyl;
	const char* on; const char* co; const char* cl; const char* cc;
	const char* vo; const char* lo; const char* fr; const char* bk;
	const char* pre; const char* pn; const char* qn; const char* gn; const char* an;
	const char* plural;
};

constexpr int kArchCount = 6;
const Arch kArchs[kArchCount] = {
	// 0 open: Italianate. Open syllables, soft consonants, long vowels, vowel endings.
	{ false, false, true, 240, 110, 0, 220, 620, 0, 2, 3,
	  "l:4,m:3,n:4,r:4,s:3,v:3,t:2,d:2,th:2,w:1,f:1,sh:1,y:1", "n:4,r:3,l:3,s:2,th:1,m:1", "", "",
	  "a:5,e:4,i:4,o:4,u:2", "ae:3,ia:3,ai:2,ou:2,ea:1,ie:1", "", "",
	  "Ven,Sal,Mira,Teo,Ana,Sol,Ter,Lia,Ost,Bel", "a,ia,on,eth,ar,el,ora,ine,ana", "a,i,o,en,is,ie,ella", "el,ion,an,ith,ael", "an,ese,ic,ian,ine",
	  "i" },
	// 1 hard: Gothic-like. Closed syllables, clusters, short names, hyphens and apostrophes.
	{ false, true, true, 700, 60, 320, 0, 220, 120, 1, 2,
	  "k:4,g:3,t:3,d:3,b:2,kh:2,h:2,ch:1,dh:1,p:1,r:1", "k:3,t:3,d:2,g:2,r:3,n:3,m:1,l:1,sh:1", "kr:2,tr:2,gr:2,dr:2,st:2,sk:1,kl:1,gl:1,br:1,pr:1", "rk:1,st:1,nd:1,rt:1,ld:1,rg:1,nk:1",
	  "a:4,o:3,u:3,e:2", "", "", "",
	  "Dun,Gor,Kar,Thar,Ruk,Tor,Vek,Brak,Dorn", "ak,gar,dun,thar,uk,ok,mar,rak", "ak,uk,ol,ur,ash,ek,ov", "ur,ath,ok,oth,eth", "ic,i,ar,ish,ak",
	  "ar" },
	// 2 harmonic: Finnic-like. Vowel harmony: each word is all front or all back.
	{ true, false, true, 420, 120, 0, 0, 420, 0, 2, 3,
	  "h:3,k:3,l:3,m:3,n:3,r:2,s:3,t:3,v:2,p:2,j:1,y:1", "n:4,l:3,r:3,s:3,t:2,k:2,m:1", "", "nt:1,lt:1,rs:1,st:1,nk:1",
	  "", "", "e:3,i:3", "a:3,o:2,u:2",
	  "Ilo,Ses,Kuol,Hel,Tanu,Mor,Sey,Lok", "lVn,rVk,sVnt,lVma,nVe,tVl", "nVs,lVk,tVn,rV,kVs,mV", "Vmo,lVs,nV,tVr", "lVnen,Vs,nVnen,lVinen",
	  "en" },
	// 3 liquid: lilting, many liquids and some long vowels, two or three syllables, doubled consonants.
	{ false, false, true, 300, 100, 0, 140, 520, 0, 2, 3,
	  "l:4,m:3,n:3,r:4,v:2,w:2,th:2,s:2,d:1,h:1,y:1", "n:3,l:3,r:2,s:2,th:1,m:1", "", "",
	  "a:4,e:4,i:3,o:3,u:2", "ae:2,ia:2,ie:1,ua:1,ea:1,ou:1", "", "",
	  "Vel,Ana,Ith,Lun,Sel,Oren,Ylo", "ndor,ssa,llen,thar,ara,oria,mere,ania", "ion,el,wen,iel,ar,ana", "iel,ion,aen,ith", "ian,ine,iel,ene",
	  "ae" },
	// 4 whisper: sibilant and breathy. s, sh, th, z, long vowels, hyphenated places.
	{ false, true, false, 380, 130, 0, 150, 450, 0, 2, 3,
	  "s:4,sh:3,th:3,z:2,f:2,h:2,y:2,l:1,v:1", "s:3,sh:2,th:2,n:2,r:2,k:1", "", "st:1,sk:1,nt:1",
	  "i:4,a:3,u:2,e:3", "ei:2,ai:2,ia:1,ue:1,ou:1", "", "",
	  "Ish,Sim,Thi,Yse,Zal,Hes,Ysh", "ith,ar,ue,thi,ysh,ine,ash", "ia,ith,ue,ei,esh", "ith,ysh,ael,esh", "ish,ine,ic,esh",
	  "en" },
	// 5 clipped: short, consonant-rich, clusters, apostrophes and hyphens.
	{ false, true, true, 540, 90, 170, 0, 380, 170, 1, 3,
	  "k:3,t:3,d:2,m:2,n:2,r:2,l:2,s:2,z:1,p:2,b:1,g:2", "k:2,t:2,n:3,r:3,s:2,l:2,m:1,d:1", "tr:1,kr:1,pl:1,sl:1,br:1,gr:1", "nd:1,rt:1,sk:1",
	  "a:3,e:3,i:3,o:3,u:2", "", "", "",
	  "Qe,Ta,Ko,Zir,Pen,Bo,Ki", "ton,ri,dar,wick,mar,ven,bek", "ri,ko,ta,lin,ek,ai", "ri,ton,ek,zo", "i,ese,an,ic",
	  "s" },
};

// The culture's own pool, drawn from the archetype's lists (thinned and jittered).
Phon sampleLanguage(Rng& r) {
	const Arch& A = kArchs[r.pickIndex(kArchCount)];
	Phon P;
	P.harmony = A.harmony;
	P.hyphen = A.hyphen;
	P.doubles = A.doubles;
	P.pClose = jitterP(r, A.pClose);
	P.pEmpty = jitterP(r, A.pEmpty);
	P.pCluster = A.pCluster > 0 ? jitterP(r, A.pCluster) : 0;
	P.pLong = jitterP(r, A.pLong);
	P.pFinal = jitterP(r, A.pFinal);
	P.pApos = A.pApos > 0 ? jitterP(r, A.pApos) : 0;
	P.minSyl = A.minSyl;
	// some languages have shorter words; never one-syllable-only (their names repeat too often)
	P.maxSyl = (r.chance(0.2) && A.maxSyl - 1 >= std::max(2, A.minSyl)) ? A.maxSyl - 1 : A.maxSyl;
	parseTokens(A.on, P.onset);
	parseTokens(A.co, P.coda);
	parseTokens(A.cl, P.clusters);
	parseTokens(A.cc, P.codaClu);
	parseTokens(A.vo, P.vowel);
	parseTokens(A.lo, P.longV);
	parseTokens(A.fr, P.front);
	parseTokens(A.bk, P.back);
	thinTokens(r, P.onset);
	thinTokens(r, P.coda);
	thinTokens(r, P.clusters);
	thinTokens(r, P.codaClu);
	thinTokens(r, P.vowel);
	thinTokens(r, P.longV);
	thinTokens(r, P.front);
	thinTokens(r, P.back);
	parseStrings(A.pre, P.prefix);
	parseStrings(A.pn, P.placeEnd);
	parseStrings(A.qn, P.personEnd);
	parseStrings(A.gn, P.godEnd);
	parseStrings(A.an, P.adjEnd);
	thinStrings(r, P.prefix);
	thinStrings(r, P.placeEnd);
	thinStrings(r, P.personEnd);
	thinStrings(r, P.godEnd);
	thinStrings(r, P.adjEnd);
	P.plural = A.plural;
	finalize(P);
	P.roots = makeRoots(P, r, 14);
	P.god = makeGod(P, r);
	return P;
}

// A daughter: the parent's lexicon after 1..4 regular sound shifts, a few habits of its own.
Phon daughterOf(const Phon& parent, Rng& r) {
	Phon D = parent;
	// only the shifts that change something in this lexicon are candidates, in a drawn order
	constexpr int kShiftCount = 8;
	std::vector<int> ids;
	for (int id = 0; id < kShiftCount; id++) {
		bool hit = shiftWord(D.god, id) != D.god;
		for (const std::string& x : D.roots) hit = hit || shiftWord(x, id) != x;
		for (const std::string& x : D.prefix) hit = hit || shiftWord(x, id) != x;
		if (hit) ids.push_back(id);
	}
	r.shuffle(ids);
	const int k = std::min(int(ids.size()), r.integer(1, std::max(1, kNamesTune.newShifts)));
	for (int i = 0; i < k; i++) {
		const int id = ids[size_t(i)];
		for (std::string& root : D.roots) root = shiftedOrSame(root, id);
		for (std::string& p : D.prefix) p = shiftedOrSame(p, id);
		D.god = shiftedOrSame(D.god, id);
	}
	// a new onset consonant comes in, sometimes an old one goes out
	if (r.chance(0.5) && !D.onset.empty()) {
		static const char* const kBank[] = { "b", "d", "g", "h", "k", "l", "m", "n", "p", "r", "s", "t", "v", "z", "sh", "th", "kh", "ch" };
		const int bankSize = int(sizeof(kBank) / sizeof(kBank[0]));
		const std::string add = kBank[r.pickIndex(bankSize)];
		bool have = false;
		for (const Tok& t : D.onset) have = have || t.s == add;
		if (!have) D.onset.push_back(Tok{ add, 1 });
		if (D.onset.size() > 4) D.onset.erase(D.onset.begin() + r.pickIndex(int(D.onset.size())));
	}
	// two or three roots of its own
	const std::vector<std::string> fresh = makeRoots(D, r, 6);
	int added = 0;
	const int want = r.integer(2, 3);
	for (const std::string& root : fresh) {
		if (added >= want) break;
		if (std::find(D.roots.begin(), D.roots.end(), root) != D.roots.end()) continue;
		D.roots.push_back(root);
		added++;
	}
	// the lexicon keeps at most kNamesTune.maxRoots stems (a long chain of daughters would grow without bound)
	const size_t keep = size_t(std::max(1, kNamesTune.maxRoots));
	if (D.roots.size() > keep) {
		D.roots.erase(D.roots.begin(), D.roots.begin() + std::ptrdiff_t(D.roots.size() - keep));
	}
	// and sometimes a new place ending
	if (r.chance(0.5) && !D.placeEnd.empty()) {
		const std::string e = joinSyl(sylsOf(D, r, 1));
		if (e.size() >= 2 && e.size() <= 4 && pronounceable(e)) D.placeEnd[size_t(r.pickIndex(int(D.placeEnd.size())))] = e;
	}
	return D;
}

// ============================================================ names

std::string fallbackWord(Rng& r) {
	static const char kCons[] = "kmnlrstv";
	static const char kVow[] = "aeiou";
	std::string w;
	const int n = r.integer(2, 3);
	for (int i = 0; i < n; i++) {
		w += kCons[r.pickIndex(8)];
		w += kVow[r.pickIndex(5)];
	}
	return w;
}

// The shorter forms: a person's name at most 10 letters, a people's stem at most 9 (so that the
// adjective and the plural stay under the 14-character limit and read aloud).
constexpr int kPersonMaxLen = 10;
constexpr size_t kCultureStemMax = 9;

std::string placeOnce(const Phon& P, Rng& r) {
	const double u = r.f();
	const double tPre = P.prefix.empty() ? 0.0 : 0.15;
	const double tHy = tPre + (P.hyphen ? 0.10 : 0.0);
	const double tAp = tHy + (P.pApos > 0 ? 0.08 : 0.0);
	if (u < tPre) {                                                          // "Tel Asha"
		const std::string pre = cap(*r.pick(P.prefix));
		return pre + " " + cap(wordCore(P, r, 2, 3));
	}
	if (u < tHy) {                                                           // "Qe-Luun"
		const std::string first = cap(wordCore(P, r, 1, 2));
		return first + "-" + cap(wordCore(P, r, 1, 2));
	}
	if (u < tAp) {                                                           // "Qe'luun"
		const std::vector<std::string> syl = sylsOf(P, r, r.integer(2, 3));
		const size_t at = size_t(1 + r.pickIndex(int(syl.size()) - 1));
		return cap(joinApos(syl, at));
	}
	if (u < tAp + 0.45) {                                                    // "Vornhal"
		std::string base = (!P.roots.empty() && r.chance(0.3)) ? *r.pick(P.roots) : joinSyl(sylsOf(P, r, r.integer(P.minSyl, P.maxSyl)));
		if (!P.placeEnd.empty() && r.chance(0.8)) base = fuse(base, resolveEnding(*r.pick(P.placeEnd), base, r));
		return cap(base);
	}
	return cap(joinSyl(sylsOf(P, r, r.integer(std::max(2, P.minSyl), std::max(2, P.maxSyl)))));   // "Ulemai"
}

std::string personOnce(const Phon& P, Rng& r) {
	std::string base;
	if (r.chance(0.04)) base = P.god;                                        // now and then, built on the god's name
	else if (!P.roots.empty() && r.chance(0.06)) base = *r.pick(P.roots);
	else base = joinSyl(sylsOf(P, r, r.integer(P.minSyl, std::min(P.maxSyl, 3))));
	if (!P.personEnd.empty() && r.chance(0.6)) base = fuse(base, resolveEnding(*r.pick(P.personEnd), base, r));
	return cap(base);
}

// Planets: language-agnostic. A neutral invented word, or an evocative pair.
const char* const kPlanetEnds[] = { "ia", "on", "ar", "is", "ara", "ine", "eth", "or", "el", "os" };
const char* const kEvoA[] = { "Ember", "Gloam", "Hollow", "Ash", "Mere", "Sable", "Pale", "Dawn", "Thorn", "Star", "Deep", "Mist", "Salt", "Iron", "Glass", "Veil", "Amber", "Cinder", "Frost", "Silver", "Umber", "Lark", "Wren", "Halcyon", "Vesper", "Ivory" };
const char* const kEvoB[] = { "reach", "hold", "watch", "fall", "haven", "drift", "march", "shore", "vale", "spire", "wake", "glen", "cairn", "sound", "keep", "well", "gate", "fen", "crest", "mere" };
constexpr int kPlanetEndCount = int(sizeof(kPlanetEnds) / sizeof(kPlanetEnds[0]));
constexpr int kEvoACount = int(sizeof(kEvoA) / sizeof(kEvoA[0]));
constexpr int kEvoBCount = int(sizeof(kEvoB) / sizeof(kEvoB[0]));
constexpr std::string_view kPlanetLang = "dm1;h=0;hy=0;db=0;pc=350;pe=250;pk=0;pl=300;pf=500;pa=0;sy=2,3;"
                                         "on=l:3,m:3,n:3,r:3,s:2,v:2,th:2,d:2,k:1,t:1,f:1,h:1,w:1;"
                                         "co=n:3,l:2,r:2,s:2,th:1;vo=a:3,e:3,i:2,o:3,u:2;lo=ae:2,ia:2,ei:1,ou:1;pu=i;gd=ama";

bool isRealPlanetName(const std::string& s) {
	static const char* const kReal[] = { "earth", "mars", "venus", "terra", "gaia", "gaea", "tellus", "pluto", "saturn", "neptune", "uranus", "mercury", "jupiter", "luna", "titan", "eris", "ceres", "europa", "vulcan", "nibiru" };
	const std::string low = lettersLower(s);
	for (const char* x : kReal) {
		if (low == x) return true;
	}
	return false;
}

std::string planetOnce(const Phon& P, Rng& r) {
	const double u = r.f();
	if (u < 0.5) {
		std::string s = joinSyl(sylsOf(P, r, r.integer(2, 3)));
		if (r.chance(0.35)) s = fuse(s, kPlanetEnds[r.pickIndex(kPlanetEndCount)]);
		return cap(s);
	}
	const std::string a = kEvoA[r.pickIndex(kEvoACount)];
	const std::string b = kEvoB[r.pickIndex(kEvoBCount)];
	if (u < 0.78) return a + b;                                              // "Gloamreach"
	return a + " " + cap(b);                                                 // "Mere Haven"
}

// The generators on a parsed language (the public functions parse once and call these).
std::string placeIn(const Phon& P, Rng& rng) {
	for (int t = 0; t < kNamesTune.tries; t++) {
		const std::string name = placeOnce(P, rng);
		if (acceptable(name, P, kNamesTune.minLen, kNamesTune.maxLen)) return name;
	}
	for (int t = 0; t < kNamesTune.tries; t++) {
		const std::string name = cap(fallbackWord(rng));
		if (acceptable(name, P, kNamesTune.minLen, kNamesTune.maxLen)) return name;
	}
	return "Kamo";
}

std::string personIn(const Phon& P, Rng& rng) {
	for (int t = 0; t < kNamesTune.tries; t++) {
		const std::string name = personOnce(P, rng);
		if (acceptable(name, P, kNamesTune.minLen, kPersonMaxLen)) return name;
	}
	for (int t = 0; t < kNamesTune.tries; t++) {
		const std::string name = cap(fallbackWord(rng));
		if (acceptable(name, P, kNamesTune.minLen, kNamesTune.maxLen)) return name;
	}
	return "Umar";
}

CultureNames cultureIn(const Phon& P, Rng& rng) {
	for (int t = 0; t < kNamesTune.tries; t++) {
		const std::string stem = joinSyl(sylsOf(P, rng, rng.integer(2, 3)));   // a people's name reads aloud: short
		if (stem.size() > kCultureStemMax) continue;
		const std::string suffix = P.adjEnd.empty() ? std::string("an") : resolveEnding(*rng.pick(P.adjEnd), stem, rng);
		const std::string adj = fuse(stem, suffix);
		const std::string plural = pluralize(stem, P.plural);
		const CultureNames c{ cap(stem), cap(adj), "the " + cap(plural) };
		// every field is a name of 3..14 characters: the plural counts with its "the " (at most 14 in all)
		if (acceptable(c.name, P, kNamesTune.minLen, kNamesTune.maxLen) &&
		    acceptable(c.adj, P, kNamesTune.minLen, kNamesTune.maxLen) &&
		    acceptable(cap(plural), P, kNamesTune.minLen, kNamesTune.maxLen) &&
		    int(c.plural.size()) <= kNamesTune.maxLen) return c;
	}
	const std::string fb = fallbackWord(rng);
	return CultureNames{ cap(fb), cap(fb + "an"), "the " + cap(fb + "i") };
}

// The variety of a language: the smallest share of distinct names among `sample` places, `sample` people
// and `sample` peoples. The probe rng is seeded from the language's own text, so the answer depends on
// the language alone and never on the caller's rng.
double varietyOf(const Phon& P) {
	Rng probe(hashStr(writePhon(P)));
	const int n = std::max(1, kNamesTune.sample);
	std::set<std::string> ps, qs, cs;
	for (int k = 0; k < n; k++) ps.insert(placeIn(P, probe));
	for (int k = 0; k < n; k++) qs.insert(personIn(P, probe));
	for (int k = 0; k < n; k++) cs.insert(cultureIn(P, probe).name);
	const size_t least = std::min(ps.size(), std::min(qs.size(), cs.size()));
	return double(least) / double(n);
}

} // namespace

// ============================================================ the public API

std::string cap(const std::string& s) {
	std::string r = s;
	if (!r.empty() && r[0] >= 'a' && r[0] <= 'z') r[0] = char(r[0] - 'a' + 'A');
	return r;
}

std::string language(Rng& rng, const std::string& parent) {
	bool haveParent = false;
	Phon old;
	if (!parent.empty()) old = parsePhon(parent, haveParent);
	// A language whose names repeat too often (a tiny inventory) is drawn again, a few rounds at most;
	// the most varied draw is kept. The rng decides the draws; varietyOf depends on the language alone.
	Phon best;
	double bestShare = -1.0;
	for (int round = 0; round < std::max(1, kNamesTune.rounds); round++) {
		Phon cand = haveParent ? daughterOf(old, rng) : sampleLanguage(rng);
		const double share = varietyOf(cand);
		if (share > bestShare) { bestShare = share; best = std::move(cand); }
		if (share >= kNamesTune.distinct) break;
	}
	return writePhon(best);
}

std::string word(const std::string& lang, Rng& rng, int minSyl, int maxSyl) {
	bool ok = false;
	const Phon P = parsePhon(lang, ok);
	const int lo = std::clamp(std::min(minSyl, maxSyl), 1, 5);
	const int hi = std::clamp(std::max(minSyl, maxSyl), lo, 5);
	for (int t = 0; t < kNamesTune.tries; t++) {
		const std::string w = wordCore(P, rng, lo, hi);
		const int n = int(w.size());
		if (n >= 3 && n <= kNamesTune.maxLen && pronounceable(w) && !profane(w)) return w;
	}
	return fallbackWord(rng);
}

std::string place(const std::string& lang, Rng& rng) {
	bool ok = false;
	return placeIn(parsePhon(lang, ok), rng);
}

std::string person(const std::string& lang, Rng& rng) {
	bool ok = false;
	return personIn(parsePhon(lang, ok), rng);
}

CultureNames culture(const std::string& lang, Rng& rng) {
	bool ok = false;
	return cultureIn(parsePhon(lang, ok), rng);
}

std::string god(const std::string& lang, Rng& rng) {
	static_cast<void>(rng);   // the god's word is fixed in the language, so related cultures share it
	bool ok = false;
	const Phon P = parsePhon(lang, ok);
	return cap(P.god);
}

std::string planet(Rng& rng) {
	bool ok = false;
	const Phon P = parsePhon(kPlanetLang, ok);
	for (int t = 0; t < 40; t++) {
		const std::string name = planetOnce(P, rng);
		const int n = int(name.size());
		if (n >= kNamesTune.minLen && n <= kNamesTune.maxLen && pronounceable(name) && !profane(name) && !isRealPlanetName(name)) return name;
	}
	return "Caelith";
}

} // namespace dm::names
