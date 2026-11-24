// DEMIURGE — SimTests/TestWorld.cpp : see TestWorld.h.
#include "TestWorld.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <type_traits>

using namespace dm;

namespace dmt {

std::shared_ptr<const Geo> sharedGeo(int L) {
	static std::map<int, std::shared_ptr<const Geo>> cache;
	auto it = cache.find(L);
	if (it != cache.end()) return it->second;
	std::shared_ptr<const Geo> g = Geo::build(L);
	cache[L] = g;
	return g;
}

std::unique_ptr<World> makeWorld(const MakeOpts& o) {
	state::CreateOpts co;
	co.seed = o.seed;
	co.name = "Testworld";
	co.L = o.L;
	co.geo = o.geo ? o.geo : sharedGeo(o.L);
	std::unique_ptr<World> w = state::create(co);
	const Geo& g = *w->geo;
	const int n = w->n;
	const double off = double(o.seed % 97) * 3.17 + 0.5;
	std::vector<float> raw(static_cast<size_t>(n));
	for (int c = 0; c < n; c++) {
		const double x = g.pos[size_t(3 * c)], y = g.pos[size_t(3 * c + 1)], z = g.pos[size_t(3 * c + 2)];
		raw[size_t(c)] = float(fbm3(x * 1.5 + off, y * 1.5 - off, z * 1.5 + off * 0.5, 5) +
		                       0.3 * ridged3(x * 3.1 + off, y * 3.1, z * 3.1 - off, 3) - 0.15);
	}
	std::vector<float> sorted = raw;
	std::sort(sorted.begin(), sorted.end());
	const size_t si = std::min(size_t(n - 1), size_t(std::floor(n * (1 - o.land))));
	const double shore = sorted[si];
	for (int c = 0; c < n; c++) {
		const double d = double(raw[size_t(c)]) - shore;
		w->elev[size_t(c)] = float(d >= 0 ? std::min(8.0, 0.02 + d * 9) : std::max(-6.5, d * 12));
	}
	w->seaLevel = o.sea;
	w->heat = 0;
	w->phase = o.phase;
	w->vars.seaBase = w->seaLevel;
	if (o.basics) fillBasics(*w);
	if (o.watch) watch(*w);
	return w;
}

World& fillBasics(World& w) {
	const Geo& g = *w.geo;
	const int n = w.n;
	const double sea = w.seaLevel;
	for (int c = 0; c < n; c++) {
		double s = 0;
		for (int k = 0; k < g.nbrN[size_t(c)]; k++) {
			const double d = std::fabs(w.elev[size_t(g.nbr[size_t(6 * c + k)])] - w.elev[size_t(c)]);
			if (d > s) s = d;
		}
		w.slope[size_t(c)] = float(s);
	}
	// coast: multi-source BFS from sea cells
	std::vector<int32_t> q(static_cast<size_t>(n));
	size_t head = 0, tail = 0;
	for (int c = 0; c < n; c++) {
		if (w.elev[size_t(c)] < sea) { w.coast[size_t(c)] = 0; q[tail++] = c; } else w.coast[size_t(c)] = 255;
	}
	while (head < tail) {
		const int c = q[head++];
		const int d = w.coast[size_t(c)];
		if (d >= 254) continue;
		for (int k = 0; k < g.nbrN[size_t(c)]; k++) {
			const int b = g.nbr[size_t(6 * c + k)];
			if (w.coast[size_t(b)] > d + 1) { w.coast[size_t(b)] = uint8_t(d + 1); q[tail++] = b; }
		}
	}
	static const double CAP[16] = { 0, 0, 0.05, 0.05, 0.35, 0.3, 0.9, 1, 0.75, 0.15, 0, 0.03, 0, 0.7, 0, 0 };
	for (int c = 0; c < n; c++) {
		const size_t i = size_t(c);
		const double x = g.pos[3 * i], y = g.pos[3 * i + 1], z = g.pos[3 * i + 2];
		const bool isSea = w.elev[i] < sea;
		const double h = std::max(0.0, w.elev[i] - sea);
		double t = 29 - 53 * std::pow(std::fabs(y), 1.5) - 5.5 * h + w.globalTemp;
		if (isSea && t < -2) t = -2;
		w.temp[i] = float(t);
		const double m = sat(0.55 + 0.45 * fbm3(x * 2.3 + 11, y * 2.3 + 5, z * 2.3 - 7, 3) - 0.004 * std::min(60, int(w.coast[i])) * 3);
		w.moist[i] = float(isSea ? 1.0 : m);
		w.fresh[i] = uint8_t((!isSea && m > 0.62) ? 1 : (isSea ? 255 : 4));
		w.down[i] = -1;
		w.flow[i] = 0;
		uint8_t b;
		if (isSea) b = (sea - w.elev[i] > 0.6) ? Biome::SEA : Biome::SHALLOWS;
		else if (t < -9) b = Biome::ICE;
		else if (h > 4.2 && t < -2) b = Biome::SNOWPEAK;
		else if (w.slope[i] > 1.1 || h > 4.6) b = Biome::ROCK;
		else if (h < 0.12 && w.coast[i] <= 1) b = Biome::BEACH;
		else if (t < -6) b = Biome::TUNDRA;
		else if (t < 3) b = m > 0.3 ? Biome::TAIGA : Biome::TUNDRA;
		else if (m < 0.14) b = Biome::DESERT;
		else if (m < 0.3) b = t > 18 ? Biome::SAVANNA : Biome::GRASSLAND;
		else if (m < 0.6) b = Biome::FOREST;
		else b = t > 20 ? Biome::RAINFOREST : Biome::FOREST;
		w.biome[i] = b;
		w.ice[i] = b == Biome::ICE ? 1.0f : 0.0f;
		const double cap = CAP[b];
		w.veg[i] = float(cap * 0.9);
		w.fauna[i] = float(isSea ? (b == Biome::SHALLOWS ? 0.8 : 0.3) : cap * 0.6);
		w.fert[i] = 0.5f;
		const double tempSuit = sat(1 - std::fabs(t - 17) / 22);
		const bool habitable = !isSea && b != Biome::ICE && b != Biome::ROCK && b != Biome::SNOWPEAK && b != Biome::BARREN;
		w.hab[i] = float(habitable ? sat(tempSuit * (0.25 + 0.75 * sat(m * 1.6)) * (1 - sat(w.slope[i] / 1.5)) * (w.coast[i] <= 2 ? 1.1 : 1.0)) : 0.0);
	}
	// Landmass stand-ins (Terrain.derive / Life.habitability own the real ones): connected land
	// components labelled 0..k-1 by descending size (ties: lowest cell id first), -1 on sea; landHab =
	// cells with hab > 0.05 per landmass.
	{
		std::vector<int32_t> comp(size_t(n), -1), size;
		std::vector<int32_t> first;
		std::vector<int32_t> stack;
		for (int c = 0; c < n; c++) {
			if (w.elev[size_t(c)] < sea || comp[size_t(c)] >= 0) continue;
			const int id = int(size.size());
			size.push_back(0);
			first.push_back(c);
			comp[size_t(c)] = id;
			stack.assign(1, c);
			while (!stack.empty()) {
				const int a = stack.back();
				stack.pop_back();
				size[size_t(id)]++;
				for (int k = 0; k < g.nbrN[size_t(a)]; k++) {
					const int b = g.nbr[size_t(6 * a + k)];
					if (w.elev[size_t(b)] >= sea && comp[size_t(b)] < 0) { comp[size_t(b)] = id; stack.push_back(b); }
				}
			}
		}
		std::vector<int32_t> order(size.size());
		for (size_t i = 0; i < order.size(); i++) order[i] = int32_t(i);
		std::sort(order.begin(), order.end(), [&](int32_t a, int32_t b) {
			return size[size_t(a)] != size[size_t(b)] ? size[size_t(a)] > size[size_t(b)] : first[size_t(a)] < first[size_t(b)];
		});
		std::vector<int32_t> rank(size.size());
		for (size_t r = 0; r < order.size(); r++) rank[size_t(order[r])] = int32_t(r);
		w.cache.landId.assign(size_t(n), -1);
		w.cache.landHab.assign(size.size(), 0);
		for (int c = 0; c < n; c++) {
			if (comp[size_t(c)] < 0) continue;
			const int32_t id = rank[size_t(comp[size_t(c)])];
			w.cache.landId[size_t(c)] = id;
			if (w.hab[size_t(c)] > 0.05f) w.cache.landHab[size_t(id)]++;
		}
		w.cache.landCount = int(size.size());
	}
	w.need.derive = false;
	w.need.climate = false;
	w.need.hydro = false;
	return w;
}

int findLand(const World& w, const std::function<bool(int)>& pred) {
	for (int c = 0; c < w.n; c++) if (w.elev[size_t(c)] >= w.seaLevel && (!pred || pred(c))) return c;
	return -1;
}

int bestHab(const World& w) {
	int best = -1;
	double bv = -1;
	for (int c = 0; c < w.n; c++) if (w.hab[size_t(c)] > bv) { bv = w.hab[size_t(c)]; best = c; }
	return best;
}

std::string checkFinite(const World& w) {
	for (int i = 0; i < state::kCellFieldCount; i++) {
		if (state::kCellFields[i].type != FieldType::F32) continue;
		const CellArray a = state::cellArray(w, i);
		const float* p = static_cast<const float*>(a.data);
		for (size_t c = 0; c < a.size; c++) {
			if (!std::isfinite(p[c])) return std::string(state::kCellFields[i].name) + "[" + std::to_string(c) + "]";
		}
	}
	const struct { const char* nm; double v; } sc[] = {
		{ "year", w.year }, { "seaLevel", w.seaLevel }, { "globalTemp", w.globalTemp }, { "co2", w.co2 },
		{ "dust", w.dust }, { "heat", w.heat }, { "god.awe", w.god.awe }, { "god.aweMax", w.god.aweMax },
		{ "stats.pop", w.stats.pop }, { "god.mercy", w.god.mercy }, { "god.wrath", w.god.wrath },
		{ "stats.peakPop", w.stats.peakPop }, { "stats.believers", w.stats.believers },
		{ "settings.calamity", w.settings.calamity },
	};
	for (const auto& s : sc) if (!std::isfinite(s.v)) return s.nm;
	// (non-finite entity numbers would be saved as 0, so they must be caught here)
	auto badOpt = [](const std::optional<double>& v) { return v.has_value() && !std::isfinite(*v); };
	auto badBag = [](const NumBag& b) { for (const auto& kv : b) if (!std::isfinite(kv.second)) return true; return false; };
	for (const Settlement& s : w.settlements) {
		const double v[] = { s.pop, s.K, s.growth, s.devotion, s.love, s.jaded, s.plague, s.immune, s.inspire, s.blessed, s.ark,
			s.founded, s.lastExpand };
		bool bad = badOpt(s.deadYear) || (s.prayer && !std::isfinite(s.prayer->since));
		for (double x : v) if (!std::isfinite(x)) bad = true;
		if (bad) return "settlement " + std::to_string(s.id);
	}
	for (const Culture& c : w.cultures) {
		const double v[] = { c.science, c.pop, c.exhaustion, c.traits.piety, c.traits.aggression, c.traits.curiosity,
			c.traits.seafaring, c.born, c.edictSince, c.lastSplit, c.color[0], c.color[1], c.color[2] };
		bool bad = badBag(c.seen) || (c.prophet && !std::isfinite(c.prophet->until));
		for (double x : v) if (!std::isfinite(x)) bad = true;
		if (bad) return "culture " + std::to_string(c.id);
	}
	for (const Unit& u : w.units) {
		const double v[] = { u.t, u.speed, u.size, u.born };
		bool bad = badBag(u.data);
		for (double x : v) if (!std::isfinite(x)) bad = true;
		if (bad) return "unit " + std::to_string(u.id);
	}
	for (const Disaster& d : w.disasters) {
		const double v[] = { d.t, d.dur, d.power, d.ang, d.eta, d.flight, d.front };
		bool bad = badBag(d.data);
		for (double x : v) if (!std::isfinite(x)) bad = true;
		if (bad) return "disaster " + std::to_string(d.id);
	}
	for (const auto& kv : w.vars.rel) {
		const Relation& r = kv.second;
		if (!std::isfinite(r.v) || !std::isfinite(r.since) || !std::isfinite(r.truce) || !std::isfinite(r.trade))
			return "vars.rel " + std::to_string(kv.first.first) + "|" + std::to_string(kv.first.second);
	}
	if (badOpt(w.vars.seaBase) || badOpt(w.vars.iceRef) || badOpt(w.vars.lastNuke) || badOpt(w.vars.snapEvery) ||
	    badOpt(w.vars.snapNext) || badBag(w.vars.num) || !std::isfinite(w.vars.lifeBoost))
		return "vars";
	if (w.vars.ark && !std::isfinite(w.vars.ark->year)) return "vars.ark";
	if (w.ending && !std::isfinite(w.ending->year)) return "ending";
	for (const ChronicleEntry& e : w.chronicle) if (!std::isfinite(e.year)) return "chronicle " + std::to_string(e.id);
	// (v4 mythos, M0) every number of the MythosState and the v4 culture fields
	for (const Culture& c : w.cultures)
		if (!std::isfinite(c.traits.compassion) || !std::isfinite(c.traits.ambition) || !std::isfinite(c.traits.courage)) return "culture " + std::to_string(c.id) + " traits";
	{
		const MythosState& m = w.mythos;
		auto all = [](std::initializer_list<double> v) { for (double x : v) if (!std::isfinite(x)) return false; return true; };
		if (!all({ m.presenceSince, m.withdrawnUntil, m.renewedYear }) || badBag(m.num)) return "mythos";
		for (size_t i = 0; i < m.faith.size(); i++) {
			const Faith& f = m.faith[i];
			if (!all({ f.aspectSince, f.image[0], f.image[1], f.image[2], f.image[3], f.agency, f.obedience, f.dependence, f.selfReliance,
				f.clarity, f.visible, f.judgedBy, f.mercy, f.sternness, f.survivorLove, f.survivorFear, f.survivorUntil, f.bearer,
				f.superiority, f.grown }) || badBag(f.data)) return "mythos.faith " + std::to_string(i);
		}
		for (size_t i = 0; i < m.factors.size(); i++) {
			const Factors& f = m.factors[i];
			if (!all({ f.growthR, f.K, f.famineLoss, f.plagueDeath, f.research, f.expandCooldown, f.farmConvert, f.devotionBase,
				f.loveTarget, f.prayerChance, f.answeredGain, f.unansweredPenalty, f.aweS, f.schism, f.warV, f.aggression, f.battle,
				f.defence, f.relDrift, f.plagueHop, f.warStrength, f.tradeEvery, f.naturalRate, f.harmTaken, f.co2Uptake,
				f.expect[0], f.expect[1], f.expect[2], f.expect[3], f.expect[4], f.expect[5] })) return "mythos.factors " + std::to_string(i);
		}
		for (const Decree& d : m.decrees) if (!all({ d.since, d.kept }) || badBag(d.data)) return "mythos.decrees";
		for (const Utterance& u : m.words) if (!all({ u.year, u.fidelity }) || badBag(u.data)) return "mythos.words " + std::to_string(u.id);
		for (const Pact& p : m.pacts) if (!all({ p.sworn, p.until, p.godKept, p.peopleKept }) || badBag(p.data)) return "mythos.pacts " + std::to_string(p.id);
		for (const Figure& f : m.figures) {
			bool fine = all({ f.born, f.died, f.credibility, f.reach, f.fame }) && !badBag(f.data);
			for (float x : f.person) if (!std::isfinite(x)) fine = false;
			for (float x : f.heart) if (!std::isfinite(x)) fine = false;
			if (!fine) return "mythos.figures " + std::to_string(f.id);
		}
		for (const Card& c : m.cards) if (!all({ c.opened, c.deadline, c.severity }) || badBag(c.data)) return "mythos.cards " + std::to_string(c.id);
		for (const Legend& l : m.legends) if (!all({ l.year, l.truth, l.told, l.vitality }) || badBag(l.data)) return "mythos.legends " + std::to_string(l.id);
		for (const House& h : m.houses) if (!all({ h.strength, h.wealth, h.authority, h.corruption, h.since }) || badBag(h.data)) return "mythos.houses";
		for (const RivalVoice& r : m.rivals) if (!all({ r.following, r.since }) || badBag(r.data)) return "mythos.rivals " + std::to_string(r.id);
		for (const Omen& o : m.omens) if (!all({ o.due, o.power, o.ang }) || badBag(o.data)) return "mythos.omens " + std::to_string(o.id);
		for (const Keeper& k : m.keepers) if (!all({ k.appointed, k.loyalty, k.will, k.rogueUntil }) || badBag(k.data)) return "mythos.keepers " + std::to_string(k.id);
		for (const Revelation& r : m.revelations) if (!all({ r.year }) || badBag(r.data)) return "mythos.revelations";
		for (const TownMyth& t : m.towns) if (!all({ t.blessedSince, t.blessedUntil, t.remnantUntil }) || badBag(t.data)) return "mythos.towns " + std::to_string(t.settlement);
		for (const MythAct& a : m.acts) if (!all({ a.year, a.good })) return "mythos.acts";
	}
	for (size_t r = 0; r < w.rivers.size(); r++)
		for (float f : w.rivers[r].flow) if (!std::isfinite(f)) return "rivers[" + std::to_string(r) + "]";
	return std::string();
}

std::vector<Event> capture(World& w, const std::function<void()>& fn) {
	std::vector<Event> events;
	const EventBus::Token tok = w.bus.onAll([&](const Event& e) { events.push_back(e); });
	fn();
	w.bus.off(tok);
	return events;
}

uint32_t hash(const World& w) {
	uint32_t h = 2166136261u;
	auto mix = [&](uint32_t v) { h ^= v & 0xffffu; h *= 16777619u; h ^= v >> 16; h *= 16777619u; };
	for (int i = 0; i < state::kCellFieldCount; i++) {
		if (state::kCellFields[i].save == SaveCodec::None) continue;     // derived fields are not identity
		const CellArray a = state::cellArray(w, i);
		for (size_t c = 0; c < a.size; c++) {
			if (a.type == FieldType::F32) { float f = static_cast<const float*>(a.data)[c]; uint32_t u; std::memcpy(&u, &f, 4); mix(u); }
			else mix(uint32_t(int32_t(a.get(c))));
		}
	}
	std::vector<uint8_t> bytes;
	state::serialize(w, bytes);
	for (uint8_t b : bytes) { h ^= b; h *= 16777619u; }
	return h;
}

std::string checkEvent(const Event& e, int n) {
	const int t = int(e.type);
	if (t < 0 || t >= kEventCount) return "unknown event type " + std::to_string(t);
	if (int(e.data.index()) != t) return std::string("event \"") + e.name() + "\" carries the payload of another event";
	const EventInfo& info = kEventTable[t];
	std::string err;
	int k = 0;
	e.fields([&](const char* key, const auto& v) {
		using V = std::decay_t<decltype(v)>;
		if (k >= info.nKeys || std::string_view(key) != info.keys[k]) {
			if (err.empty()) err = std::string("event \"") + info.name + "\" has unexpected payload key \"" + key + "\"";
		}
		k++;
		if constexpr (std::is_floating_point_v<V>) {
			if (!std::isfinite(v) && err.empty()) err = std::string("event \"") + info.name + "\" payload \"" + key + "\" is not finite";
		}
		if constexpr (std::is_same_v<V, int>) {
			if (std::string_view(key) == "cell" && (v < -1 || v >= n) && err.empty())
				err = std::string("event \"") + info.name + "\" cell " + std::to_string(v) + " is out of range";
		}
	});
	if (err.empty() && k != info.nKeys) err = std::string("event \"") + info.name + "\" is missing payload keys";
	return err;
}

void watch(World& w) {
	World* wp = &w;
	w.bus.onAll([wp](const Event& e) {
		const std::string err = checkEvent(e, wp->n);
		if (!err.empty()) recordFailure(err, std::source_location::current());
	});
}

bool noNaN(const World& w, std::string_view msg, std::source_location loc) {
	const std::string bad = checkFinite(w);
	if (!bad.empty()) recordFailure(std::string(msg) + (msg.empty() ? "" : ": ") + "non-finite value at " + bad, loc);
	return bad.empty();
}

} // namespace dmt
