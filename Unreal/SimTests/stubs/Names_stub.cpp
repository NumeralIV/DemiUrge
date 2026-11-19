// DEMIURGE — SimTests/stubs/Names_stub.cpp : neutral stand-in for Sim/Names.cpp (see StubHooks.h).
// Owner: names engineer (keep it matching Names.h). Written by foundation-api.
// Neutral: fixed placeholder names (never empty), no rng draws; cap is honest.
#include "StubHooks.h"

#include <cctype>
#include <string>

DM_STUB_MODULE(names);

namespace dm::names {

std::string language(Rng& rng, const std::string& parent) {
	DM_STUB(names_language, rng, parent);
	return "stub";
}
std::string word(const std::string& lang, Rng& rng, int minSyl, int maxSyl) {
	DM_STUB(names_word, lang, rng, minSyl, maxSyl);
	return "word";
}
std::string place(const std::string& lang, Rng& rng) {
	DM_STUB(names_place, lang, rng);
	return "Place";
}
std::string person(const std::string& lang, Rng& rng) {
	DM_STUB(names_person, lang, rng);
	return "Person";
}
CultureNames culture(const std::string& lang, Rng& rng) {
	DM_STUB(names_culture, lang, rng);
	return CultureNames{ "Folk", "Folkish", "the Folk" };
}
std::string god(const std::string& lang, Rng& rng) {
	DM_STUB(names_god, lang, rng);
	return "Maker";
}
std::string planet(Rng& rng) {
	DM_STUB(names_planet, rng);
	return "World";
}
std::string cap(const std::string& s) {
	DM_STUB(names_cap, s);
	std::string r = s;
	if (!r.empty()) r[0] = char(std::toupper(static_cast<unsigned char>(r[0])));
	return r;
}

} // namespace dm::names
