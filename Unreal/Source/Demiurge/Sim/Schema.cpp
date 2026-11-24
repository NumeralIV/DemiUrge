// DEMIURGE — Sim/Schema.cpp : see Schema.h (port of js/sim/schema.js).
#include "Schema.h"

#include <cmath>

#include "State.h"

namespace dm {

const char* const kBiomeNames[Biome::COUNT] = {
	"Sea", "Shallows", "Beach", "Desert", "Savanna", "Grassland", "Forest", "Rainforest",
	"Taiga", "Tundra", "Ice", "Rock", "Snow peak", "Wetland", "Barren", "Sea ice",
};

const char* const kAgeNames[Age::COUNT] = { "Stone", "Bronze", "Iron", "Sail", "Industry", "Atomic", "Space" };

const char* const kBuildingNames[Building::COUNT] = {
	"shrine", "temple", "granary", "walls", "port", "market", "mine", "factory", "academy", "plant", "launch", "wonder",
};

const std::array<std::array<double, 3>, 6> kCultureColors = { {
	{ 0.787, 0.342, 0.0 },     // #e69f00 orange
	{ 0.093, 0.458, 0.807 },   // #56b4e9 sky blue
	{ 0.0, 0.342, 0.171 },     // #009e73 green
	{ 0.871, 0.776, 0.058 },   // #f0e442 yellow
	{ 0.665, 0.112, 0.0 },     // #d55e00 vermilion
	{ 0.604, 0.191, 0.392 },   // #cc79a7 purple
} };

// ------------------------------------------------------------------ enum names

namespace {
template <class E, size_t N>
const char* nameIn(const char* const (&names)[N], E v) {
	const size_t i = size_t(v);
	return i < N ? names[i] : "?";
}
template <class E, size_t N>
bool parseIn(const char* const (&names)[N], std::string_view s, E& out) {
	for (size_t i = 0; i < N; i++) if (s == names[i]) { out = E(i); return true; }
	return false;
}

const char* const PHASE_N[] = { "molten", "water", "life", "people", "history", "ended" };
const char* const CAUSE_N[] = { "none", "drowned", "meteor", "lava", "war", "famine", "plague", "quake", "nuke", "abandoned", "wave", "lightning", "sea", "deluge", "burning" };
const char* const UNIT_N[] = { "settler", "trader", "army", "prophet" };
const char* const NAVAL_N[] = { "land", "coastal", "open" };
const char* const DIS_N[] = { "none", "comet", "meteor", "volcano", "tsunami", "storm", "drought", "deluge", "burning" };
const char* const EDICT_N[] = { "none", "peace", "expand", "build", "crusade" };
const char* const PRAYER_N[] = { "rain", "heal", "peace", "bloom", "victory", "sun" };
const char* const BY_N[] = { "awaken", "settlers" };
const char* const HOW_N[] = { "war", "schism" };
const char* const REASON_N[] = { "awaken", "schism", "distance" };
const char* const IMPACT_N[] = { "comet", "meteor" };
const char* const MILE_N[] = { "warming", "cooling", "sea-rise", "sea-fall", "ice-age", "thaw" };
const char* const ENDING_N[] = { "ark", "silence" };
const char* const MOOD_N[] = { "beloved", "feared", "forgotten", "parting", "ash", "quiet" };
// ---- (v4 mythos, M0) the enums of MythosTypes.h
const char* const PRESENCE_N[] = { "wonders", "messengers", "unseen", "walk" };
const char* const ASPECT_N[] = { "unknown", "parent", "ruler", "mystery", "threat" };
const char* const ARCH_N[] = { "balanced", "generous", "dangerous", "beautiful", "strange" };
const char* const LEGACY_N[] = { "none", "obedient-empire", "compassionate-civilisation", "terrified-world", "thriving-wilderness", "unneeded" };
const char* const AREA_N[] = { "violence", "wealth", "worship", "honesty", "hospitality", "care", "land" };
const char* const CLARITY_N[] = { "exact", "parable", "dream", "sign" };
const char* const CHANNEL_N[] = { "messenger", "dream", "sky" };
const char* const CARD_N[] = { "shape-people", "presence", "first-decree", "decree-conflict", "breach", "messenger-choice", "competing-prayers", "judgement", "pact-offer", "pact-broken", "blessing", "warning", "warning-exploited", "trial", "dispute", "institution", "rival-voice", "land-claim", "revelation", "revelation-misuse", "death-question", "bearers", "keeper-dispute", "coming-of-age", "silence-report", "remnant", "situation" };
const char* const CONTENT_N[] = { "decree", "repeal", "warning", "promise", "answer", "revelation" };
const char* const OUTCOME_N[] = { "pending", "heard", "literal", "wisdom", "sect", "personal", "rival", "house-read", "catastrophe" };
const char* const GODTERM_N[] = { "rain", "shield", "healing", "victory", "no-wrath" };
const char* const PEOPLETERM_N[] = { "keep-decree", "temples", "no-war", "first-fruits", "carry-word" };
const char* const PACTSTATE_N[] = { "sworn", "kept", "god-broke", "people-broke", "forgiven", "lapsed", "ended" };
const char* const FIGKIND_N[] = { "messenger", "candidate", "ruler", "healer", "rival-voice", "life", "founder", "hero", "voyager", "sage", "doubter", "captain" };
const char* const ARCHETYPE_N[] = { "none", "shepherd", "criminal", "child", "queen", "unbelieved" };
const char* const FATE_N[] = { "living", "died", "fallen", "martyred", "tragic" };
const char* const TRIAL_N[] = { "power", "hardship", "temptation", "impossible" };
const char* const CARDSTATE_N[] = { "open", "answered", "lapsed", "withdrawn" };
const char* const MORAL_N[] = { "mercy", "justice", "wrath", "wonder", "warning", "sacrifice" };
const char* const CREDIT_N[] = { "god", "messenger", "rival", "nature", "ancestors" };
const char* const RIVAL_N[] = { "rising", "exposed", "martyr", "faded", "split" };
const char* const OMEN_N[] = { "quake", "eruption", "drought", "plague", "tsunami", "judgement" };
const char* const WARN_N[] = { "none", "dreams", "stars", "harvest", "messenger", "nothing" };
const char* const RESPONSE_N[] = { "none", "repentance", "preparation", "exploitation", "dismissal" };
const char* const DOMAIN_N[] = { "last-door", "deep", "scales", "sleep", "people" };
const char* const TEMPER_N[] = { "gentle", "stern" };
const char* const REVEAL_N[] = { "agriculture", "writing", "medicine", "world-shape", "death-truth" };
const char* const REVOUT_N[] = { "pending", "shared", "hoarded", "weaponised" };
const char* const DEATH_N[] = { "none", "further-shore", "turning", "return", "long-sleep" };
const char* const SURE_N[] = { "none", "uncertain", "glimpsed", "known" };
const char* const CHARGE_N[] = { "none", "carry-word", "garden", "peace", "stars" };
const char* const BLESS_N[] = { "none", "prosperity", "healer", "endurance" };
const char* const CONSEQ_N[] = { "none", "gratitude", "dependence", "envy", "conquest" };
const char* const JUDGE_N[] = { "deed", "heart" };
const char* const SENTENCE_N[] = { "forgive", "warn", "remove", "disaster" };
const char* const CIRC_N[] = { "none", "famine", "war", "plague", "fear", "order" };
const char* const HEART_N[] = { "unknown", "selfless", "fearful", "greedy", "zealous" };
const char* const DISPUTE_N[] = { "why-died", "why-struck", "too-heavy", "hidden-face", "why-them", "no-return" };
const char* const DANSWER_N[] = { "explain", "silence", "change-mind", "demand" };
const char* const RENEW_N[] = { "none", "water", "burning" };
const char* const LEVEL_N[] = { "low", "middling", "high" };
const char* const SOURCE_N[] = { "mythos", "cards", "doctrine", "characters", "faiths", "omens", "keepers", "renewal", "situation" };
const char* const WHY_N[] = { "ok", "awe", "unavailable", "needs-messenger", "needs-ruler", "locked" };

// Decree stances per area (MythosTypes.h stanceName): index = the area's stance enum value.
const char* const STANCE_VIOLENCE[] = { "peace", "defence", "valour" };
const char* const STANCE_WEALTH[] = { "share", "tithe", "gain" };
const char* const STANCE_WORSHIP[] = { "prayer", "temples", "kindness", "art", "world" };
const char* const STANCE_HONESTY[] = { "truth", "discretion", "cunning" };
const char* const STANCE_HOSPITALITY[] = { "open", "guarded", "closed" };
const char* const STANCE_CARE[] = { "shelter", "kin", "strength" };
const char* const STANCE_LAND[] = { "dominion", "stewardship", "equal" };
const char* const* stanceTable(DecreeArea a) {
	switch (a) {
	case DecreeArea::Violence: return STANCE_VIOLENCE;
	case DecreeArea::Wealth: return STANCE_WEALTH;
	case DecreeArea::Worship: return STANCE_WORSHIP;
	case DecreeArea::Honesty: return STANCE_HONESTY;
	case DecreeArea::Hospitality: return STANCE_HOSPITALITY;
	case DecreeArea::Care: return STANCE_CARE;
	case DecreeArea::Land: return STANCE_LAND;
	default: return nullptr;
	}
}
} // namespace

const char* toStr(Phase v) { return nameIn(PHASE_N, v); }
const char* toStr(Cause v) { return nameIn(CAUSE_N, v); }
const char* toStr(UnitType v) { return nameIn(UNIT_N, v); }
const char* toStr(NavalMode v) { return nameIn(NAVAL_N, v); }
const char* toStr(DisasterType v) { return nameIn(DIS_N, v); }
const char* toStr(Edict v) { return nameIn(EDICT_N, v); }
const char* toStr(PrayerKind v) { return nameIn(PRAYER_N, v); }
const char* toStr(FoundedBy v) { return nameIn(BY_N, v); }
const char* toStr(CaptureHow v) { return nameIn(HOW_N, v); }
const char* toStr(BirthReason v) { return nameIn(REASON_N, v); }
const char* toStr(ImpactKind v) { return nameIn(IMPACT_N, v); }
const char* toStr(MilestoneKind v) { return nameIn(MILE_N, v); }
const char* toStr(EndingType v) { return nameIn(ENDING_N, v); }
const char* toStr(Mood v) { return nameIn(MOOD_N, v); }
bool fromStr(std::string_view s, Phase& out) { return parseIn(PHASE_N, s, out); }
bool fromStr(std::string_view s, Cause& out) { return parseIn(CAUSE_N, s, out); }
bool fromStr(std::string_view s, UnitType& out) { return parseIn(UNIT_N, s, out); }
bool fromStr(std::string_view s, NavalMode& out) { return parseIn(NAVAL_N, s, out); }
bool fromStr(std::string_view s, DisasterType& out) { return parseIn(DIS_N, s, out); }
bool fromStr(std::string_view s, Edict& out) { return parseIn(EDICT_N, s, out); }
bool fromStr(std::string_view s, PrayerKind& out) { return parseIn(PRAYER_N, s, out); }
bool fromStr(std::string_view s, FoundedBy& out) { return parseIn(BY_N, s, out); }
bool fromStr(std::string_view s, CaptureHow& out) { return parseIn(HOW_N, s, out); }
bool fromStr(std::string_view s, BirthReason& out) { return parseIn(REASON_N, s, out); }
bool fromStr(std::string_view s, ImpactKind& out) { return parseIn(IMPACT_N, s, out); }
bool fromStr(std::string_view s, MilestoneKind& out) { return parseIn(MILE_N, s, out); }
bool fromStr(std::string_view s, EndingType& out) { return parseIn(ENDING_N, s, out); }
bool fromStr(std::string_view s, Mood& out) { return parseIn(MOOD_N, s, out); }

static_assert(sizeof(PHASE_N) / sizeof(PHASE_N[0]) == size_t(enumCount<Phase>()));
static_assert(sizeof(CAUSE_N) / sizeof(CAUSE_N[0]) == size_t(enumCount<Cause>()));
static_assert(sizeof(UNIT_N) / sizeof(UNIT_N[0]) == size_t(enumCount<UnitType>()));
static_assert(sizeof(NAVAL_N) / sizeof(NAVAL_N[0]) == size_t(enumCount<NavalMode>()));
static_assert(sizeof(DIS_N) / sizeof(DIS_N[0]) == size_t(enumCount<DisasterType>()));
static_assert(sizeof(EDICT_N) / sizeof(EDICT_N[0]) == size_t(enumCount<Edict>()));
static_assert(sizeof(PRAYER_N) / sizeof(PRAYER_N[0]) == size_t(enumCount<PrayerKind>()));
static_assert(sizeof(BY_N) / sizeof(BY_N[0]) == size_t(enumCount<FoundedBy>()));
static_assert(sizeof(HOW_N) / sizeof(HOW_N[0]) == size_t(enumCount<CaptureHow>()));
static_assert(sizeof(REASON_N) / sizeof(REASON_N[0]) == size_t(enumCount<BirthReason>()));
static_assert(sizeof(IMPACT_N) / sizeof(IMPACT_N[0]) == size_t(enumCount<ImpactKind>()));
static_assert(sizeof(MILE_N) / sizeof(MILE_N[0]) == size_t(enumCount<MilestoneKind>()));
static_assert(sizeof(ENDING_N) / sizeof(ENDING_N[0]) == size_t(enumCount<EndingType>()));
static_assert(sizeof(MOOD_N) / sizeof(MOOD_N[0]) == size_t(enumCount<Mood>()));

// ---- (v4 mythos, M0)
const char* toStr(Presence v) { return nameIn(PRESENCE_N, v); }
const char* toStr(Aspect v) { return nameIn(ASPECT_N, v); }
const char* toStr(Archetype v) { return nameIn(ARCH_N, v); }
const char* toStr(Legacy v) { return nameIn(LEGACY_N, v); }
const char* toStr(DecreeArea v) { return nameIn(AREA_N, v); }
const char* toStr(Clarity v) { return nameIn(CLARITY_N, v); }
const char* toStr(Channel v) { return nameIn(CHANNEL_N, v); }
const char* toStr(CardKind v) { return nameIn(CARD_N, v); }
const char* toStr(WordContent v) { return nameIn(CONTENT_N, v); }
const char* toStr(WordOutcome v) { return nameIn(OUTCOME_N, v); }
const char* toStr(GodTerm v) { return nameIn(GODTERM_N, v); }
const char* toStr(PeopleTerm v) { return nameIn(PEOPLETERM_N, v); }
const char* toStr(PactState v) { return nameIn(PACTSTATE_N, v); }
const char* toStr(FigureKind v) { return nameIn(FIGKIND_N, v); }
const char* toStr(MessengerArchetype v) { return nameIn(ARCHETYPE_N, v); }
const char* toStr(FigureFate v) { return nameIn(FATE_N, v); }
const char* toStr(TrialKind v) { return nameIn(TRIAL_N, v); }
const char* toStr(CardState v) { return nameIn(CARDSTATE_N, v); }
const char* toStr(LegendMoral v) { return nameIn(MORAL_N, v); }
const char* toStr(LegendCredit v) { return nameIn(CREDIT_N, v); }
const char* toStr(RivalState v) { return nameIn(RIVAL_N, v); }
const char* toStr(OmenKind v) { return nameIn(OMEN_N, v); }
const char* toStr(WarningWay v) { return nameIn(WARN_N, v); }
const char* toStr(OmenResponse v) { return nameIn(RESPONSE_N, v); }
const char* toStr(KeeperDomain v) { return nameIn(DOMAIN_N, v); }
const char* toStr(Temper v) { return nameIn(TEMPER_N, v); }
const char* toStr(RevelationKind v) { return nameIn(REVEAL_N, v); }
const char* toStr(RevelationOutcome v) { return nameIn(REVOUT_N, v); }
const char* toStr(DeathBelief v) { return nameIn(DEATH_N, v); }
const char* toStr(Certainty v) { return nameIn(SURE_N, v); }
const char* toStr(Charge v) { return nameIn(CHARGE_N, v); }
const char* toStr(BlessingKind v) { return nameIn(BLESS_N, v); }
const char* toStr(Consequence v) { return nameIn(CONSEQ_N, v); }
const char* toStr(JudgeBy v) { return nameIn(JUDGE_N, v); }
const char* toStr(Sentence v) { return nameIn(SENTENCE_N, v); }
const char* toStr(Circumstance v) { return nameIn(CIRC_N, v); }
const char* toStr(HeartKind v) { return nameIn(HEART_N, v); }
const char* toStr(DisputeKind v) { return nameIn(DISPUTE_N, v); }
const char* toStr(DisputeAnswer v) { return nameIn(DANSWER_N, v); }
const char* toStr(RenewalKind v) { return nameIn(RENEW_N, v); }
const char* toStr(TraitLevel v) { return nameIn(LEVEL_N, v); }
const char* toStr(CardSource v) { return nameIn(SOURCE_N, v); }
const char* toStr(OptionWhy v) { return nameIn(WHY_N, v); }
bool fromStr(std::string_view s, Presence& out) { return parseIn(PRESENCE_N, s, out); }
bool fromStr(std::string_view s, Aspect& out) { return parseIn(ASPECT_N, s, out); }
bool fromStr(std::string_view s, Archetype& out) { return parseIn(ARCH_N, s, out); }
bool fromStr(std::string_view s, Legacy& out) { return parseIn(LEGACY_N, s, out); }
bool fromStr(std::string_view s, DecreeArea& out) { return parseIn(AREA_N, s, out); }
bool fromStr(std::string_view s, Clarity& out) { return parseIn(CLARITY_N, s, out); }
bool fromStr(std::string_view s, Channel& out) { return parseIn(CHANNEL_N, s, out); }
bool fromStr(std::string_view s, CardKind& out) { return parseIn(CARD_N, s, out); }
bool fromStr(std::string_view s, WordContent& out) { return parseIn(CONTENT_N, s, out); }
bool fromStr(std::string_view s, WordOutcome& out) { return parseIn(OUTCOME_N, s, out); }
bool fromStr(std::string_view s, GodTerm& out) { return parseIn(GODTERM_N, s, out); }
bool fromStr(std::string_view s, PeopleTerm& out) { return parseIn(PEOPLETERM_N, s, out); }
bool fromStr(std::string_view s, PactState& out) { return parseIn(PACTSTATE_N, s, out); }
bool fromStr(std::string_view s, FigureKind& out) { return parseIn(FIGKIND_N, s, out); }
bool fromStr(std::string_view s, MessengerArchetype& out) { return parseIn(ARCHETYPE_N, s, out); }
bool fromStr(std::string_view s, FigureFate& out) { return parseIn(FATE_N, s, out); }
bool fromStr(std::string_view s, TrialKind& out) { return parseIn(TRIAL_N, s, out); }
bool fromStr(std::string_view s, CardState& out) { return parseIn(CARDSTATE_N, s, out); }
bool fromStr(std::string_view s, LegendMoral& out) { return parseIn(MORAL_N, s, out); }
bool fromStr(std::string_view s, LegendCredit& out) { return parseIn(CREDIT_N, s, out); }
bool fromStr(std::string_view s, RivalState& out) { return parseIn(RIVAL_N, s, out); }
bool fromStr(std::string_view s, OmenKind& out) { return parseIn(OMEN_N, s, out); }
bool fromStr(std::string_view s, WarningWay& out) { return parseIn(WARN_N, s, out); }
bool fromStr(std::string_view s, OmenResponse& out) { return parseIn(RESPONSE_N, s, out); }
bool fromStr(std::string_view s, KeeperDomain& out) { return parseIn(DOMAIN_N, s, out); }
bool fromStr(std::string_view s, Temper& out) { return parseIn(TEMPER_N, s, out); }
bool fromStr(std::string_view s, RevelationKind& out) { return parseIn(REVEAL_N, s, out); }
bool fromStr(std::string_view s, RevelationOutcome& out) { return parseIn(REVOUT_N, s, out); }
bool fromStr(std::string_view s, DeathBelief& out) { return parseIn(DEATH_N, s, out); }
bool fromStr(std::string_view s, Certainty& out) { return parseIn(SURE_N, s, out); }
bool fromStr(std::string_view s, Charge& out) { return parseIn(CHARGE_N, s, out); }
bool fromStr(std::string_view s, BlessingKind& out) { return parseIn(BLESS_N, s, out); }
bool fromStr(std::string_view s, Consequence& out) { return parseIn(CONSEQ_N, s, out); }
bool fromStr(std::string_view s, JudgeBy& out) { return parseIn(JUDGE_N, s, out); }
bool fromStr(std::string_view s, Sentence& out) { return parseIn(SENTENCE_N, s, out); }
bool fromStr(std::string_view s, Circumstance& out) { return parseIn(CIRC_N, s, out); }
bool fromStr(std::string_view s, HeartKind& out) { return parseIn(HEART_N, s, out); }
bool fromStr(std::string_view s, DisputeKind& out) { return parseIn(DISPUTE_N, s, out); }
bool fromStr(std::string_view s, DisputeAnswer& out) { return parseIn(DANSWER_N, s, out); }
bool fromStr(std::string_view s, RenewalKind& out) { return parseIn(RENEW_N, s, out); }
bool fromStr(std::string_view s, TraitLevel& out) { return parseIn(LEVEL_N, s, out); }
bool fromStr(std::string_view s, CardSource& out) { return parseIn(SOURCE_N, s, out); }
bool fromStr(std::string_view s, OptionWhy& out) { return parseIn(WHY_N, s, out); }
static_assert(sizeof(PRESENCE_N) / sizeof(PRESENCE_N[0]) == size_t(enumCount<Presence>()));
static_assert(sizeof(ASPECT_N) / sizeof(ASPECT_N[0]) == size_t(enumCount<Aspect>()));
static_assert(sizeof(ARCH_N) / sizeof(ARCH_N[0]) == size_t(enumCount<Archetype>()));
static_assert(sizeof(LEGACY_N) / sizeof(LEGACY_N[0]) == size_t(enumCount<Legacy>()));
static_assert(sizeof(AREA_N) / sizeof(AREA_N[0]) == size_t(enumCount<DecreeArea>()));
static_assert(sizeof(CLARITY_N) / sizeof(CLARITY_N[0]) == size_t(enumCount<Clarity>()));
static_assert(sizeof(CHANNEL_N) / sizeof(CHANNEL_N[0]) == size_t(enumCount<Channel>()));
static_assert(sizeof(CARD_N) / sizeof(CARD_N[0]) == size_t(enumCount<CardKind>()));
static_assert(sizeof(CONTENT_N) / sizeof(CONTENT_N[0]) == size_t(enumCount<WordContent>()));
static_assert(sizeof(OUTCOME_N) / sizeof(OUTCOME_N[0]) == size_t(enumCount<WordOutcome>()));
static_assert(sizeof(GODTERM_N) / sizeof(GODTERM_N[0]) == size_t(enumCount<GodTerm>()));
static_assert(sizeof(PEOPLETERM_N) / sizeof(PEOPLETERM_N[0]) == size_t(enumCount<PeopleTerm>()));
static_assert(sizeof(PACTSTATE_N) / sizeof(PACTSTATE_N[0]) == size_t(enumCount<PactState>()));
static_assert(sizeof(FIGKIND_N) / sizeof(FIGKIND_N[0]) == size_t(enumCount<FigureKind>()));
static_assert(sizeof(ARCHETYPE_N) / sizeof(ARCHETYPE_N[0]) == size_t(enumCount<MessengerArchetype>()));
static_assert(sizeof(FATE_N) / sizeof(FATE_N[0]) == size_t(enumCount<FigureFate>()));
static_assert(sizeof(TRIAL_N) / sizeof(TRIAL_N[0]) == size_t(enumCount<TrialKind>()));
static_assert(sizeof(CARDSTATE_N) / sizeof(CARDSTATE_N[0]) == size_t(enumCount<CardState>()));
static_assert(sizeof(MORAL_N) / sizeof(MORAL_N[0]) == size_t(enumCount<LegendMoral>()));
static_assert(sizeof(CREDIT_N) / sizeof(CREDIT_N[0]) == size_t(enumCount<LegendCredit>()));
static_assert(sizeof(RIVAL_N) / sizeof(RIVAL_N[0]) == size_t(enumCount<RivalState>()));
static_assert(sizeof(OMEN_N) / sizeof(OMEN_N[0]) == size_t(enumCount<OmenKind>()));
static_assert(sizeof(WARN_N) / sizeof(WARN_N[0]) == size_t(enumCount<WarningWay>()));
static_assert(sizeof(RESPONSE_N) / sizeof(RESPONSE_N[0]) == size_t(enumCount<OmenResponse>()));
static_assert(sizeof(DOMAIN_N) / sizeof(DOMAIN_N[0]) == size_t(enumCount<KeeperDomain>()));
static_assert(sizeof(TEMPER_N) / sizeof(TEMPER_N[0]) == size_t(enumCount<Temper>()));
static_assert(sizeof(REVEAL_N) / sizeof(REVEAL_N[0]) == size_t(enumCount<RevelationKind>()));
static_assert(sizeof(REVOUT_N) / sizeof(REVOUT_N[0]) == size_t(enumCount<RevelationOutcome>()));
static_assert(sizeof(DEATH_N) / sizeof(DEATH_N[0]) == size_t(enumCount<DeathBelief>()));
static_assert(sizeof(SURE_N) / sizeof(SURE_N[0]) == size_t(enumCount<Certainty>()));
static_assert(sizeof(CHARGE_N) / sizeof(CHARGE_N[0]) == size_t(enumCount<Charge>()));
static_assert(sizeof(BLESS_N) / sizeof(BLESS_N[0]) == size_t(enumCount<BlessingKind>()));
static_assert(sizeof(CONSEQ_N) / sizeof(CONSEQ_N[0]) == size_t(enumCount<Consequence>()));
static_assert(sizeof(JUDGE_N) / sizeof(JUDGE_N[0]) == size_t(enumCount<JudgeBy>()));
static_assert(sizeof(SENTENCE_N) / sizeof(SENTENCE_N[0]) == size_t(enumCount<Sentence>()));
static_assert(sizeof(CIRC_N) / sizeof(CIRC_N[0]) == size_t(enumCount<Circumstance>()));
static_assert(sizeof(HEART_N) / sizeof(HEART_N[0]) == size_t(enumCount<HeartKind>()));
static_assert(sizeof(DISPUTE_N) / sizeof(DISPUTE_N[0]) == size_t(enumCount<DisputeKind>()));
static_assert(sizeof(DANSWER_N) / sizeof(DANSWER_N[0]) == size_t(enumCount<DisputeAnswer>()));
static_assert(sizeof(RENEW_N) / sizeof(RENEW_N[0]) == size_t(enumCount<RenewalKind>()));
static_assert(sizeof(LEVEL_N) / sizeof(LEVEL_N[0]) == size_t(enumCount<TraitLevel>()));
static_assert(sizeof(SOURCE_N) / sizeof(SOURCE_N[0]) == size_t(enumCount<CardSource>()));
static_assert(sizeof(WHY_N) / sizeof(WHY_N[0]) == size_t(enumCount<OptionWhy>()));

const char* stanceName(DecreeArea a, int stance) {
	const char* const* t = stanceTable(a);
	if (!t || stance < 0 || stance >= stanceCount(a)) return "none";
	return t[stance];
}

bool stanceFromName(DecreeArea a, std::string_view s, int& out) {
	if (s == "none") { out = -1; return true; }
	const char* const* t = stanceTable(a);
	if (!t) return false;
	for (int i = 0; i < stanceCount(a); i++) if (s == t[i]) { out = i; return true; }
	return false;
}

namespace state {

// ------------------------------------------------------------------ factories

Settlement& newSettlement(World& w, int cell, int cultureId) {
	Settlement s;
	s.id = int(w.settlements.size());
	s.cell = cell;
	s.culture = cultureId;
	s.founded = w.year;
	s.lastExpand = w.year;
	w.settlements.push_back(std::move(s));
	return w.settlements.back();
}

int freeColorSlot(const World& w) {
	const size_t P = kCultureColors.size();
	bool used[6] = { false, false, false, false, false, false };
	for (const Culture& c : w.cultures) {
		if (c.dead) continue;
		for (size_t i = 0; i < P; i++) {
			const auto& k = kCultureColors[i];
			if (std::fabs(k[0] - c.color[0]) + std::fabs(k[1] - c.color[1]) + std::fabs(k[2] - c.color[2]) < 1e-3) used[i] = true;
		}
	}
	for (size_t i = 0; i < P; i++) if (!used[i]) return int(i);
	return int(w.cultures.size() % P);
}

Culture& newCulture(World& w, int parent) {
	Culture c;
	c.id = int(w.cultures.size());
	c.color = kCultureColors[size_t(freeColorSlot(w))];
	c.parent = parent;
	c.born = w.year;
	w.cultures.push_back(std::move(c));
	return w.cultures.back();
}

Unit& newUnit(World& w, UnitType type) {
	Unit u;
	u.id = w.nextId.unit++;
	u.type = type;
	u.born = w.year;
	w.units.push_back(std::move(u));
	return w.units.back();
}

Disaster& newDisaster(World& w, DisasterType type, int cell) {
	Disaster d;
	d.id = w.nextId.disaster++;
	d.type = type;
	d.cell = cell;
	w.disasters.push_back(std::move(d));
	return w.disasters.back();
}

// ------------------------------------------------------------------ lookups

Settlement* settlement(World& w, int id) { return (id >= 0 && size_t(id) < w.settlements.size()) ? &w.settlements[size_t(id)] : nullptr; }
const Settlement* settlement(const World& w, int id) { return (id >= 0 && size_t(id) < w.settlements.size()) ? &w.settlements[size_t(id)] : nullptr; }
Culture* culture(World& w, int id) { return (id >= 0 && size_t(id) < w.cultures.size()) ? &w.cultures[size_t(id)] : nullptr; }
const Culture* culture(const World& w, int id) { return (id >= 0 && size_t(id) < w.cultures.size()) ? &w.cultures[size_t(id)] : nullptr; }

Unit* findUnit(World& w, int id) {
	for (Unit& u : w.units) if (u.id == id) return &u;
	return nullptr;
}
const Unit* findUnit(const World& w, int id) {
	for (const Unit& u : w.units) if (u.id == id) return &u;
	return nullptr;
}
Disaster* findDisaster(World& w, int id) {
	for (Disaster& d : w.disasters) if (d.id == id) return &d;
	return nullptr;
}
const Disaster* findDisaster(const World& w, int id) {
	for (const Disaster& d : w.disasters) if (d.id == id) return &d;
	return nullptr;
}

bool eraseUnit(World& w, int id) {
	for (size_t i = 0; i < w.units.size(); i++) {
		if (w.units[i].id == id) { w.units.erase(w.units.begin() + std::ptrdiff_t(i)); return true; }
	}
	return false;
}
bool eraseDisaster(World& w, int id) {
	for (size_t i = 0; i < w.disasters.size(); i++) {
		if (w.disasters[i].id == id) { w.disasters.erase(w.disasters.begin() + std::ptrdiff_t(i)); return true; }
	}
	return false;
}

} // namespace state
} // namespace dm
