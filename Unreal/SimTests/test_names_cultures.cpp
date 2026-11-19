// DEMIURGE — SimTests/test_names_cultures.cpp : the eight culture families of docs/content/cultures.md.
// Written by the names-cultures track. Checks, from the outside (only Names.h), that every family names
// cleanly and variedly, that a family is recognisable from place names alone, that no world holds two
// names one letter apart, that daughters drift by regular laws and stay in the family, and that rulers,
// epithets and ships follow the rules. DM_NAMES_DUMP=1 prints 300 names per family for reading.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "Core/Rng.h"
#include "Core/Util.h"
#include "Names.h"
#include "TestFramework.h"

using namespace dm;
using namespace dmt;

namespace {

constexpr int kFam = 8;
const char* const kFamName[kFam] = { "Velessan", "Korvagh", "Aavik", "Qa'tiri", "Hanuwe", "Mbendi", "Thrask", "Pheraxi" };

bool vowelC(char c) { return c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u'; }
bool letterC(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
char lowC(char c) { return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c; }
bool sepC(char c) { return c == ' ' || c == '-' || c == '\''; }

std::string flatC(const std::string& s) {
	std::string r;
	for (char c : s) if (letterC(c)) r += lowC(c);
	return r;
}

// The contract's sound rule, written again here (independent of Names.cpp).
std::string soundReason(const std::string& s) {
	int cons = 0, vow = 0, same = 0;
	char prev = 0;
	for (size_t i = 0; i < s.size(); i++) {
		const char c = s[i];
		if (sepC(c)) {
			if (i == 0 || i + 1 >= s.size() || !letterC(s[i - 1]) || !letterC(s[i + 1])) return "bad separator";
			cons = vow = same = 0;
			prev = 0;
			continue;
		}
		if (!letterC(c)) return "bad character";
		const char l = lowC(c);
		same = (l == prev) ? same + 1 : 1;
		prev = l;
		if (same >= 3) return "3 identical letters";
		if (vowelC(l)) { vow++; cons = 0; if (vow >= 3) return "3 vowels"; }
		else { cons++; vow = 0; if (cons >= 4) return "4 consonants"; }
	}
	return "";
}

// A test-side sample of the blocklist (slurs, profanity, real words and places the families could make).
bool hitsBlock(const std::string& s) {
	static const char* const frag[] = { "fuck", "shit", "cunt", "cock", "dick", "piss", "slut", "whore", "bitch", "nigg", "negr",
		"rape", "penis", "vagin", "kike", "chink", "gook", "coon", "kafir", "puta", "kurwa", "pizd", "vittu", "sharmut", "mboro",
		"malaka", "kaka", "hitler", "satan", "mordor", "gondor", "khazad", "arrakis", "skyrim" };
	static const char* const whole[] = { "ass", "tit", "fag", "sex", "homo", "nazi", "suka", "khara", "kuma", "randi", "baka",
		"puki", "titi", "babi", "sambo", "bimbo", "porra", "verga", "pula", "kuk", "thor", "odin", "zeus", "allah", "jesus", "rome",
		"lima", "oslo", "malta", "kenya", "congo", "tonga", "samoa", "fiji", "sparta", "athens", "troy", "sushi", "banana", "piano",
		"pizza", "lava", "mama", "papa", "dear", "door", "dog", "god", "orc", "elf", "endor", "rohan", "tina", "lisa", "anna",
		"maria", "sara", "nina" };
	const std::string f = flatC(s);
	for (const char* x : frag) if (f.find(x) != std::string::npos) return true;
	std::string piece;
	std::vector<std::string> pieces;
	for (char c : s) {
		if (letterC(c)) piece += lowC(c);
		else if (c != '\'') { if (!piece.empty()) pieces.push_back(piece); piece.clear(); }
	}
	if (!piece.empty()) pieces.push_back(piece);
	pieces.push_back(f);
	for (const std::string& p : pieces) for (const char* x : whole) if (p == x) return true;
	return false;
}

int famOf(const std::string& lang) {
	if (lang.rfind("dm2;f=", 0) != 0) return -1;
	return std::atoi(lang.c_str() + 6);
}

int genOf(const std::string& lang) {
	const size_t at = lang.find(";g=");
	return at == std::string::npos ? -1 : std::atoi(lang.c_str() + at + 3);
}

int contactOf(const std::string& lang) {
	const size_t at = lang.find(";c=");
	return at == std::string::npos ? -1 : std::atoi(lang.c_str() + at + 3);
}

int lawCount(const std::string& lang) {
	const size_t at = lang.find(";L=");
	if (at == std::string::npos) return 0;
	const size_t end = lang.find(';', at + 3);
	const std::string l = lang.substr(at + 3, end - at - 3);
	if (l.empty()) return 0;
	return int(std::count(l.begin(), l.end(), ',')) + 1;
}

// The k-th root language of family `fam` (seeds searched in order, so the same call gives the same language).
std::string langOf(int fam, int k) {
	int found = 0;
	for (uint32_t s = 1; s < 100000; s++) {
		Rng r(s * 2654435761u + 17u);
		const std::string lang = names::language(r, std::string());
		if (famOf(lang) != fam) continue;
		if (found++ == k) return lang;
	}
	return std::string();
}

// Letter value sum of a name, modulo 27 (the spelling code of the design).
int residueC(const std::string& s) {
	int t = 0;
	for (char c : s) if (letterC(c)) t += lowC(c) - 'a' + 1;
	return t % 27;
}

int lev(const std::string& a, const std::string& b, int cap) {
	if (std::abs(int(a.size()) - int(b.size())) > cap) return cap + 1;
	std::vector<int> prev(b.size() + 1), cur(b.size() + 1);
	for (size_t j = 0; j <= b.size(); j++) prev[j] = int(j);
	for (size_t i = 1; i <= a.size(); i++) {
		cur[0] = int(i);
		int best = cur[0];
		for (size_t j = 1; j <= b.size(); j++) {
			cur[j] = std::min({ prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1) });
			best = std::min(best, cur[j]);
		}
		if (best > cap) return cap + 1;
		prev.swap(cur);
	}
	return prev[b.size()];
}

// Trigram profile of a list of names (each word framed by ^ and $; the apostrophe is a letter here).
using Profile = std::map<std::string, double>;
Profile profileOf(const std::vector<std::string>& list) {
	Profile p;
	double total = 0;
	for (const std::string& s : list) {
		std::string w = "^";
		auto flush = [&]() {
			w += '$';
			for (size_t i = 0; i + 2 < w.size(); i++) { p[w.substr(i, 3)] += 1; total += 1; }
			w = "^";
		};
		for (char c : s) {
			if (c == ' ' || c == '-') flush();
			else w += lowC(c);
		}
		flush();
	}
	for (auto& kv : p) kv.second /= total;
	return p;
}

double cosine(const Profile& a, const Profile& b) {
	double dot = 0, na = 0, nb = 0;
	for (const auto& kv : a) {
		na += kv.second * kv.second;
		const auto it = b.find(kv.first);
		if (it != b.end()) dot += kv.second * it->second;
	}
	for (const auto& kv : b) nb += kv.second * kv.second;
	return (na > 0 && nb > 0) ? dot / std::sqrt(na * nb) : 0.0;
}

std::vector<std::string> placesOf(const std::string& lang, uint32_t seed, int n) {
	Rng r(seed);
	std::vector<std::string> out;
	for (int k = 0; k < n; k++) out.push_back(names::place(lang, r));
	return out;
}

// Family centroids from four reference languages each (k = 0..3, 200 places each).
std::vector<Profile> centroids() {
	std::vector<Profile> c;
	for (int f = 0; f < kFam; f++) {
		std::vector<std::string> all;
		for (int k = 0; k < 4; k++) {
			const std::vector<std::string> p = placesOf(langOf(f, k), 900u + uint32_t(k), 200);
			all.insert(all.end(), p.begin(), p.end());
		}
		c.push_back(profileOf(all));
	}
	return c;
}

int classify(const std::vector<Profile>& cents, const std::vector<std::string>& sample) {
	const Profile p = profileOf(sample);
	int best = -1;
	double bestS = -1;
	for (int f = 0; f < kFam; f++) {
		const double s = cosine(p, cents[size_t(f)]);
		if (s > bestS) { bestS = s; best = f; }
	}
	return best;
}

} // namespace

DM_TEST("names-cultures: languages spread over all eight families") {
	std::vector<int> count(kFam, 0);
	for (uint32_t s = 1; s <= 400; s++) {
		Rng r(s * 7919u + 5u);
		const int f = famOf(names::language(r, std::string()));
		if (f >= 0 && f < kFam) count[size_t(f)]++;
	}
	std::printf("  400 root languages by family:");
	for (int f = 0; f < kFam; f++) std::printf(" %s %d;", kFamName[f], count[size_t(f)]);
	std::printf("\n");
	for (int f = 0; f < kFam; f++) ok(count[size_t(f)] >= 25, std::string("family ") + kFamName[f] + " appears (" + std::to_string(count[size_t(f)]) + " of 400)");
}

DM_TEST("names-cultures: every family, 3 languages x 300 names: valid, coded, places >= 93 %, people >= 95 % distinct") {
	for (int f = 0; f < kFam; f++) {
		for (int k = 0; k < 3; k++) {
			const std::string lang = langOf(f, k);
			if (!ok(!lang.empty(), std::string("a language of family ") + kFamName[f])) return;
			Rng rp(11u + uint32_t(k)), rq(12u + uint32_t(k)), rc(13u + uint32_t(k));
			std::set<std::string> ps, qs, cs;
			int bad = 0, uncoded = 0, blockedN = 0;
			std::string firstBad;
			auto inspect = [&](const std::string& n, size_t hi, bool coded) {
				const std::string why = soundReason(n);
				if (n.size() < 3 || n.size() > hi || !why.empty()) { bad++; if (firstBad.empty()) firstBad = n + " (" + (why.empty() ? "length" : why) + ")"; }
				if (coded && residueC(n) != 11) uncoded++;
				if (hitsBlock(n)) blockedN++;
			};
			for (int i = 0; i < 300; i++) {
				const std::string p = names::place(lang, rp);
				const std::string q = names::person(lang, rq);
				const names::CultureNames c = names::culture(lang, rc);
				inspect(p, 14, true);
				inspect(q, 14, true);
				inspect(c.name, 14, true);
				inspect(c.adj, 14, false);
				inspect(c.plural.substr(4), 10, false);
				ps.insert(p);
				qs.insert(q);
				cs.insert(c.name);
			}
			const std::string tag = std::string(kFamName[f]) + " " + std::to_string(k) + ": ";
			std::printf("  %-10s %d: distinct of 300: places %zu, persons %zu, peoples %zu\n", kFamName[f], k, ps.size(), qs.size(), cs.size());
			eq(bad, 0, tag + "invalid names, first " + firstBad);
			eq(uncoded, 0, tag + "names outside the spelling code");
			eq(blockedN, 0, tag + "blocklisted names");
			ok(ps.size() >= 279, tag + "distinct places " + std::to_string(ps.size()));   // 93 %: Civ re-rolls an exact repeat
			ok(qs.size() >= 285, tag + "distinct persons " + std::to_string(qs.size()));
			ok(cs.size() >= 285, tag + "distinct peoples " + std::to_string(cs.size()));
		}
	}
}

DM_TEST("names-cultures: a family is recognisable from its place names alone") {
	const std::vector<Profile> cents = centroids();
	int right5 = 0, right1 = 0, all5 = 0, all1 = 0;
	std::vector<int> wrongBy(kFam, 0);
	for (int f = 0; f < kFam; f++) {
		for (int k = 4; k < 14; k++) {                // ten fresh languages per family, not used for the centroids
			const std::string lang = langOf(f, k);
			const std::vector<std::string> p = placesOf(lang, 4242u + uint32_t(k), 60);
			for (int s = 0; s + 5 <= 60; s += 5) {
				const std::vector<std::string> five(p.begin() + s, p.begin() + s + 5);
				const int got = classify(cents, five);
				all5++;
				if (got == f) right5++;
				else wrongBy[size_t(f)]++;
			}
			for (int s = 0; s < 60; s++) {
				all1++;
				if (classify(cents, { p[size_t(s)] }) == f) right1++;
			}
		}
	}
	const double acc5 = double(right5) / double(all5), acc1 = double(right1) / double(all1);
	std::printf("  nearest-family by letter trigrams: from 5 place names %.1f %% right (%d of %d); from a single name %.1f %%\n", 100 * acc5, right5, all5, 100 * acc1);
	std::printf("  wrong 5-name guesses by family:");
	for (int f = 0; f < kFam; f++) std::printf(" %s %d;", kFamName[f], wrongBy[size_t(f)]);
	std::printf("\n");
	ok(acc5 >= 0.97, "five place names name the family (" + std::to_string(acc5) + ")");
	ok(acc1 >= 0.80, "even one place name usually names the family (" + std::to_string(acc1) + ")");
}

DM_TEST("names-cultures: no two names of a world one letter apart (120 simulated worlds)") {
	int worlds = 0, d1 = 0, d2long = 0, exactRetry = 0, towns = 0;
	std::string ex1, ex2;
	for (uint32_t ws = 1; ws <= 120; ws++) {
		Rng wr(ws * 104729u + 7u);
		std::vector<std::string> langs;
		{ Rng r = wr.fork("culture:0"); langs.push_back(names::language(r, std::string())); }
		const int nCult = wr.integer(6, 11);
		for (int c = 1; c < nCult; c++) {
			const int parent = wr.chance(0.75) ? 0 : wr.pickIndex(int(langs.size()));
			Rng r = wr.fork("culture:" + std::to_string(c));
			langs.push_back(names::language(r, langs[size_t(parent)]));
		}
		std::vector<std::string> all;
		std::set<std::string> taken;
		for (int id = 0; id < 140; id++) {
			const std::string& lang = langs[size_t(wr.pickIndex(int(langs.size())))];
			Rng r = wr.fork("settle:" + std::to_string(id));
			std::string name;
			for (int t = 0; t < 6; t++) {
				name = names::place(lang, r);
				if (!taken.count(name)) break;
				exactRetry++;
			}
			if (taken.count(name)) continue;
			taken.insert(name);
			all.push_back(name);
			towns++;
		}
		for (size_t c = 0; c < langs.size(); c++) {
			Rng r = wr.fork("people:" + std::to_string(c));
			all.push_back(names::culture(langs[c], r).name);
			all.push_back(names::person(langs[c], r));
		}
		worlds++;
		for (size_t i = 0; i < all.size(); i++) {
			const std::string a = flatC(all[i]);
			for (size_t j = i + 1; j < all.size(); j++) {
				const std::string b = flatC(all[j]);
				if (a == b) continue;                 // the same name twice (a person and a town): not a near-twin
				const int d = lev(a, b, 2);
				if (d <= 1) { d1++; if (ex1.empty()) ex1 = all[i] + " ~ " + all[j]; }
				else if (d == 2 && a.size() >= 10 && b.size() >= 10) { d2long++; if (ex2.empty()) ex2 = all[i] + " ~ " + all[j]; }
			}
		}
	}
	std::printf("  %d worlds, %d towns: pairs one letter apart %d%s; pairs of 10+ letters two letters apart %d%s; exact repeats re-rolled %d\n",
		worlds, towns, d1, ex1.empty() ? "" : (" (" + ex1 + ")").c_str(), d2long, ex2.empty() ? "" : (" (" + ex2 + ")").c_str(), exactRetry);
	eq(d1, 0, "no near-twins at one letter in any world");
	ok(d2long <= worlds / 4, "long near-twins at two letters stay rare: " + std::to_string(d2long));
}

DM_TEST("names-cultures: daughters drift by regular laws and stay in their family") {
	const std::vector<Profile> cents = centroids();
	int same = 0, all = 0, lawsGrow = 0, godNear = 0;
	for (int f = 0; f < kFam; f++) {
		std::string lang = langOf(f, 0);
		Rng g0(1);
		const std::string god0 = names::god(lang, g0);
		for (int g = 1; g <= 4; g++) {
			Rng r(uint32_t(f * 100 + g) * 7919u);
			const std::string d = names::language(r, lang);
			all++;
			if (famOf(d) == f && genOf(d) == g) same++;
			if (lawCount(d) >= lawCount(lang)) lawsGrow++;
			Rng q(5);
			const std::string gd = names::god(d, q);
			if (lev(flatC(god0), flatC(gd), 8) <= 2 * g + 1) godNear++;
			lang = d;
		}
		const std::vector<std::string> p = placesOf(lang, 77u, 40);
		const int got = classify(cents, p);
		const int contact = contactOf(lang);
		std::printf("  %-10s 4th daughter (laws %d, contact %s): classified %s; places: %s, %s, %s, %s; god %s -> %s\n", kFamName[f], lawCount(lang),
			contact >= 0 ? kFamName[contact] : "none", got >= 0 ? kFamName[got] : "?", p[0].c_str(), p[1].c_str(), p[2].c_str(), p[3].c_str(),
			god0.c_str(), names::god(lang, g0).c_str());
		ok(got == f || got == contact, std::string(kFamName[f]) + ": the 4th daughter still reads as its family (or its contact sister)");
	}
	eq(same, all, "a daughter keeps her mother's family and counts her generation");
	eq(lawsGrow, all, "sound laws accumulate");
	eq(godNear, all, "the god's name drifts regularly (a letter or two per generation)");
}

DM_TEST("names-cultures: rulers, epithets and ships") {
	int bad = 0, total = 0;
	std::string firstBad;
	for (int f = 0; f < kFam; f++) {
		const std::string lang = langOf(f, 1);
		Rng r(31u + uint32_t(f));
		std::printf("  %-10s", kFamName[f]);
		for (int k = 0; k < 60; k++) {
			const std::string ru = names::ruler(lang, r);
			const std::string ep = names::epithet(lang, r);
			const std::string sh = names::ship(lang, r);
			for (const std::string& s : { ru, sh }) {
				total++;
				if (s.size() < 3 || s.size() > 24 || !soundReason(s).empty() || hitsBlock(s)) { bad++; if (firstBad.empty()) firstBad = s; }
			}
			total++;
			if (ep.rfind("the ", 0) != 0 || ep.size() > 24) { bad++; if (firstBad.empty()) firstBad = ep; }
			if (k < 3) std::printf(" %s %s | %s;", ru.c_str(), ep.c_str(), sh.c_str());
		}
		std::printf("\n");
	}
	eq(bad, 0, "every ruler, epithet and ship is valid (first bad: " + firstBad + ")");
	std::printf("  %d checked\n", total);
}

DM_TEST("names-cultures: blocklist scan, 8 families x 3 languages x 2,000 names") {
	int hits = 0, total = 0;
	std::string first;
	for (int f = 0; f < kFam; f++) {
		for (int k = 0; k < 3; k++) {
			const std::string lang = langOf(f, k);
			Rng a(100u + uint32_t(k)), b(200u + uint32_t(k)), c(300u + uint32_t(k));
			for (int i = 0; i < 700; i++) {
				const std::string xs[3] = { names::place(lang, a), names::person(lang, b), names::culture(lang, c).name };
				for (const std::string& x : xs) {
					total++;
					if (hitsBlock(x)) { hits++; if (first.empty()) first = x; }
				}
			}
		}
	}
	std::printf("  %d names scanned against the test-side list: %d hits%s\n", total, hits, first.empty() ? "" : (" (" + first + ")").c_str());
	eq(hits, 0, "no blocklisted word");
}

DM_TEST("names-cultures: speed of a new language and of 10,000 names") {
	Rng lr(4);
	const double t0 = nowMs();
	std::string last;
	for (int k = 0; k < 50; k++) last = names::language(lr, std::string());
	const double perLang = (nowMs() - t0) / 50.0;
	const double t1 = nowMs();
	for (int k = 0; k < 50; k++) (void)names::language(lr, last);
	const double perDaughter = (nowMs() - t1) / 50.0;
	std::printf("  a new language: %.2f ms; a daughter: %.2f ms (each with its variety probe)\n", perLang, perDaughter);
	ok(perLang < 15.0 && perDaughter < 15.0, "a culture's birth stays well inside a sim tick");
}

// DM_NAMES_DUMP=1: 300 places, 60 persons, 30 peoples, 10 rulers and ships per family, and a daughter chain.
DM_TEST("names-cultures: dump 300 names per family (only when DM_NAMES_DUMP=1)") {
	const char* on = std::getenv("DM_NAMES_DUMP");
	if (!on || !*on || *on == '0') return;
	for (int f = 0; f < kFam; f++) {
		std::printf("FAMILY %d %s\n", f, kFamName[f]);
		for (int k = 0; k < 3; k++) {
			const std::string lang = langOf(f, k);
			Rng rp(500u + uint32_t(k)), rq(600u + uint32_t(k)), rc(700u + uint32_t(k)), rr(800u + uint32_t(k));
			std::printf("LANG %d.%d god %s record %zu bytes\nPLACES", f, k, names::god(lang, rp).c_str(), lang.size());
			for (int i = 0; i < 100; i++) std::printf("%s%s", i ? ", " : " ", names::place(lang, rp).c_str());
			std::printf("\nPERSONS");
			for (int i = 0; i < 20; i++) std::printf("%s%s", i ? ", " : " ", names::person(lang, rq).c_str());
			std::printf("\nPEOPLES");
			for (int i = 0; i < 10; i++) {
				const names::CultureNames c = names::culture(lang, rc);
				std::printf("%s%s / %s / %s", i ? "; " : " ", c.name.c_str(), c.adj.c_str(), c.plural.c_str());
			}
			std::printf("\nRULERS");
			for (int i = 0; i < 4; i++) std::printf("%s%s %s", i ? "; " : " ", names::ruler(lang, rr).c_str(), names::epithet(lang, rr).c_str());
			std::printf("\nSHIPS");
			for (int i = 0; i < 4; i++) std::printf("%s%s", i ? "; " : " ", names::ship(lang, rr).c_str());
			std::printf("\n");
		}
		std::string lang = langOf(f, 0);
		for (int g = 1; g <= 3; g++) {
			Rng r(uint32_t(f * 31 + g));
			lang = names::language(r, lang);
			Rng p(9);
			std::printf("DAUGHTER %d (laws %d, contact %d, god %s):", g, lawCount(lang), contactOf(lang), names::god(lang, p).c_str());
			for (int i = 0; i < 12; i++) std::printf(" %s;", names::place(lang, p).c_str());
			std::printf("\n");
		}
	}
	ok(true);
}
