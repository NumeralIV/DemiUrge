// DEMIURGE — SimTests/test_chronicle_mythos_lint.h : the chronicle voice lint of SPEC-MYTHOS 3.31, shared by
// test_chronicle_mythos.cpp (synthetic events, every other module stubbed) and test_chronicle_mythos_histories.cpp
// (real histories, run.ps1 all). Track M8 "chronicle-mythos".
//
// The lint, per entry:
//   - no unfilled template syntax ({ } [ ] |);
//   - none of the game terms of voice rule 6 (Awe, tier, devotion, brush, edict) nor "agency", "obedience",
//     "dependence", "credibility", "fidelity", "aspect", "card" (whole words, any case);
//   - none of the banned words of SPEC-MYTHOS 0.2 (commandment, scripture, covenant, testament, prophet, apostle,
//     disciple, messiah, angel, archangel, saint, demon, devil, chosen people, promised land, heaven, hell, paradise,
//     reincarnation, karma, nirvana, church, mosque, heresy, the Flood, Ark of Noah, apocalypse, sin, sinner,
//     damnation; plurals too);
//   - no gendered pronoun within six words after a figure's name (the god's fixed epithets "She Who ..." /
//     "He Who ..." are titles, voice rule 10, and are taken out first);
//   - mythos entries of weight 1-2 have at most 50 words.
// Per history: no mythos template body written more than 10 times (vars.num "chronicle_mu.*", Chronicle.cpp), and
// mythos adds at most 1.2 notable entries per real minute at 1x (120 years).
#pragma once

#include <algorithm>
#include <cctype>
#include <set>
#include <string>
#include <vector>

#include "State.h"

namespace m8lint {

inline bool isMythType(const std::string& t) {
	static const char* const kTypes[] = { "card", "card-lapsed", "presence", "aspect", "decree", "custom", "breach", "word", "figure",
		"judgement", "survivors" };
	for (const char* k : kTypes) if (t == k) return true;
	return false;
}

inline std::string lower(std::string s) {
	for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
	return s;
}
inline bool letter(char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0; }

// Whole-word (or whole-phrase) occurrence of `w` (lower case) in lower-cased text `t`.
inline bool hasWord(const std::string& t, const std::string& w) {
	for (size_t p = t.find(w); p != std::string::npos; p = t.find(w, p + 1)) {
		const bool lb = p == 0 || !letter(t[p - 1]);
		const bool rb = p + w.size() >= t.size() || !letter(t[p + w.size()]);
		if (lb && rb) return true;
	}
	return false;
}

inline int words(const std::string& t) {
	int n = 0;
	bool in = false;
	for (char c : t) {
		const bool sp = c == ' ';
		if (!sp && !in) n++;
		in = !sp;
	}
	return n;
}

// The faults of one entry ("" = clean). figureNames: the names of every figure / prophet of the history.
inline std::string fault(const dm::ChronicleEntry& e, const std::vector<std::string>& figureNames) {
	const std::string& t = e.text;
	if (t.empty()) return "empty";
	for (char c : t) if (c == '{' || c == '}' || c == '[' || c == ']' || c == '|') return "template syntax";
	const std::string lt = lower(t);
	static const char* const kBanned[] = {
		// voice rule 6 and 3.31's additions
		"awe", "tier", "tiers", "devotion", "brush", "edict", "edicts", "agency", "obedience", "dependence", "credibility", "fidelity",
		"aspect", "aspects", "card", "cards",
		// SPEC-MYTHOS 0.2
		"commandment", "commandments", "law of god", "scripture", "scriptures", "covenant", "covenants", "testament", "testaments",
		"prophet", "prophets", "apostle", "apostles", "disciple", "disciples", "messiah", "messiahs", "angel", "angels", "archangel",
		"archangels", "saint", "saints", "demon", "demons", "devil", "devils", "chosen people", "promised land", "heaven", "heavens",
		"hell", "hells", "paradise", "reincarnation", "karma", "nirvana", "church", "churches", "mosque", "mosques", "heresy",
		"heresies", "heretic", "heretics", "ark of noah", "apocalypse", "sin", "sins", "sinner", "sinners", "damnation",
	};
	for (const char* b : kBanned) if (hasWord(lt, b)) return std::string("banned word '") + b + "'";
	if (t.find("the Flood") != std::string::npos || t.find("The Flood") != std::string::npos) return "banned word 'the Flood'";
	// pronouns after a figure's name (fixed epithets out first)
	std::string pt = t;
	for (const char* ep : { "She Who Burns the Sky", "She Who Opens the Sky", "He Who Moves the Hills", "He Who Keeps the Lamp Lit" })
		for (size_t p; (p = pt.find(ep)) != std::string::npos;) pt.replace(p, std::char_traits<char>::length(ep), "the god");
	const std::string lp = lower(pt);
	for (const std::string& nm : figureNames) {
		if (nm.size() < 2) continue;
		const std::string ln = lower(nm);
		for (size_t p = lp.find(ln); p != std::string::npos; p = lp.find(ln, p + 1)) {
			if (p > 0 && letter(lp[p - 1])) continue;
			size_t q = p + ln.size();
			if (q < lp.size() && letter(lp[q])) continue;
			for (int k = 0; k < 6 && q < lp.size(); k++) {
				while (q < lp.size() && !letter(lp[q])) q++;
				size_t r = q;
				while (r < lp.size() && (letter(lp[r]) || lp[r] == '\'')) r++;
				const std::string wd = lp.substr(q, r - q);
				for (const char* pr : { "he", "she", "him", "his", "her", "hers", "himself", "herself" })
					if (wd == pr) return "gendered pronoun after the name '" + nm + "'";
				q = r;
			}
		}
	}
	if (isMythType(e.type) && e.weight <= 2 && words(t) > 50) return "more than 50 words";
	return "";
}

// Every figure and prophet name of a world (figures, living prophets; plus extra names the caller collected).
inline std::vector<std::string> names(const dm::World& w, const std::set<std::string>& extra = {}) {
	std::set<std::string> s(extra.begin(), extra.end());
	for (const dm::Figure& f : w.mythos.figures) if (!f.name.empty()) s.insert(f.name);
	for (const dm::Culture& c : w.cultures) if (c.prophet && !c.prophet->name.empty()) s.insert(c.prophet->name);
	return std::vector<std::string>(s.begin(), s.end());
}

// The most any mythos template body was written (vars.num "chronicle_mu.*").
inline int maxBodyUse(const dm::World& w) {
	int most = 0;
	for (const auto& kv : w.vars.num)
		if (kv.first.rfind("chronicle_mu.", 0) == 0) most = std::max(most, int(kv.second));
	return most;
}

} // namespace m8lint
