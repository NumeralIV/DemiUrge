# DEMIURGE — Mythos spec (v1): how the god relates to its people

*Lead design (track "mythos-spec"), 2026-11-20. Design and build plan only; no code was changed. Binding for the
mythos build tracks in section 9 the way `SPEC-SIM.md` binds the simulation: **module boundaries, state shapes,
event names and payloads, the writers rows, the schedule lines and the card contract are binding; numbers are
starting values** in TUNE structs (section 1.6) that only the balance owner retunes after integration.*

**Read first:** `AGENTS.md`, `docs/CONCEPT.md`, `docs/SPEC-SIM.md` (all of it, v3 and v3.1 notes),
`docs/FOUNDATION-CPP.md`, `docs/SPEC-UNREAL.md`, `docs/SPEC-UI.md`, `docs/reviews/claude-design-review.md`,
`docs/reviews/gameplay-v4.md`, `docs/reviews/story-v4.md`, `docs/content/chronicle-voice.md`, and the Sim headers
`Society.h`, `Powers.h`, `Chronicle.h`, `Civ.h`, `Schema.h`, `State.h`.

**Time.** 1 tick = 0.25 years; at 1x one real second is 2 years and one real minute is 120 years (SPEC-SIM 0.5). A
history of about 3,600 years is about 30 minutes at 1x. "N s at 1x" always means real seconds at 1x.

---

## 0. The owner's decision and what this spec covers

**Decision (owner, 2026-11-20):** build every god interaction and mythology system on the owner's list **except
entering the world as a mortal**, which is deferred. Keep the game mythology-neutral: its own invented faiths, with
echoes of old stories only as echoes.

Covered here, each as a system with rules, TUNE, hooks, player interaction, chronicle output, visuals and audio,
tests and simrun targets (section numbers in brackets):

| # | Owner's item | System | Phase |
|---|---|---|---|
| 1 | A world with consequences: generous, dangerous, beautiful, strange | World archetypes [3.1] | v1 |
| 2 | How present you are (messengers, miracles only, unseen; walking among them deferred) and how they see you (parent, ruler, mystery, threat) | Presence [3.2], Aspect [3.3] | v1 |
| 3 | Free will that emerges from presence and clarity, not a slider | Agency, obedience, dependence [3.4] | v1 |
| 4 | Shape the first people: curiosity, compassion, ambition, courage, aggression | First people [3.5] | v1 |
| 5 | Laws on violence, wealth, worship, honesty, hospitality, care for the weak, with conflicts | Decrees [3.6], conflicts [3.7] | v1 |
| 6 | Pacts kept or broken across generations | Pacts [3.14] | v2 |
| 7 | Messengers: shepherd, criminal, child, queen, the unbelieved | Messengers [3.8] | v1 |
| 8 | How clearly you speak: exact orders, dreams, parables, signs | The Word and its clarity [3.9] | v1 |
| 9 | Competing prayers | Competing prayers [3.10] | v1 |
| 10 | Witnessed miracles become stories that change over centuries | Legends [3.15] | v2 |
| 11 | Kinds of blessing that attract gratitude, dependence, envy or conquest | Blessings [3.16] | v2 |
| 12 | Judging deeds versus intentions, on cards with context | Judgement [3.11] | v1 |
| 13 | Mercy or punishment, remembered by survivors | Sentences and survivor memory [3.12] | v1 |
| 14 | Warnings before disaster, and how people respond | Omens and warnings [3.17] | v2 |
| 15 | Testing individuals | Trials [3.18] | v2 |
| 16 | Letting people argue with you | Disputes [3.19] | v2 |
| 17 | A chosen people with a mission that may become superiority | The Bearers [3.24] | v3 |
| 18 | Religion as an institution: temples, priests, offerings, authority, corruption | The House of the God [3.20] | v2 |
| 19 | False messengers | Rival voices [3.21] | v2 |
| 20 | What worship means, shaping what peoples build | The Rite [3.13] | v1 (decree), v2 (districts) |
| 21 | Revelations given when ready or too early | Revelations [3.25] | v3 |
| 22 | Guardians with domains who can disagree | Keepers [3.26] | v3 |
| 23 | What death means, and how sure they are | Beliefs about death [3.27] | v3 |
| 24 | Nature's claim: farmland versus ancient forest | The Decree of the Land and old growth [3.22] | v2 |
| 25 | People outgrowing their need of you | Coming of age [3.28] | v3 |
| 26 | Withdrawing to watch: a century of silence | Withdrawal [3.29] | v3 |
| 27 | Renewal instead of a reset, and a new game from this world | Renewal [3.30] | v3 |
| 28 | The legacy ending: what it felt like to live under your care | Legacy [4] | v1 (verdict), v3 (lives) |

**Not covered (deferred by the owner):** the god walking among the people as a mortal. The `Presence` enum keeps a
reserved value `Walk` that `mythos::setPresence` refuses, and the presence chooser shows it greyed with "Not yet".

### 0.1 Principles (binding)

1. **Tend, never command** (gameplay-v4 2.3). The god speaks, blesses, judges, warns, tests and withholds. People
   decide. Even an exact order is obeyed only as far as the people's obedience reaches.
2. **A card is a choice, or it is a toast.** Every decision card offers at least two meaningful answers, each with a
   price, plus a defined default when the player lets it lapse. Anything with one answer is a chronicle line or a
   toast, never a card.
3. **One meaningful choice every 1-3 real minutes at 1x, never spam** (section 2.2): a director paces every card in
   the game against one attention budget, shared with gameplay-v4's Situations if they are built.
4. **Free will emerges.** No trait, slider or setting sets agency directly. It follows from how present the god is,
   how clearly it speaks, what it reveals, and what the people endure.
5. **Mythology-neutral words** (section 0.2). The game's faiths are its own. Old stories may echo (a flood with a
   remnant, a people with a charge, a voice nobody believed) but are never named or quoted.
6. **Today's game is the default.** Every rule defaults (TUNE) to exactly the v3.1 behaviour; the god's own choices
   change the world, and emergent rules that would change a passive world sit behind TUNE flags that the balance
   owner switches on with before/after tables (the v3.1 convention). SPEC-SIM section 4 and every v3 / v3.1 target
   must still pass.
7. **Honest rules, unreliable people.** The sim always knows the truth (who did what, and why); belief, legend and
   interpretation may be wrong. Endings judge what really happened (Society memory) plus how the people lived
   (Legacy); legends colour the words.

### 0.2 Neutral names (binding for UI copy, chronicle templates and data)

| Concept | In-game name | Never write |
|---|---|---|
| Commandments, laws | **Decrees** ("the Decree of the Gate") | commandment, law of god, scripture |
| Covenant | **Pact** ("the Pact of Tel Asha") | covenant, testament |
| Prophet | **Messenger** (chronicle may also say *seer*, *oracle*, *voice*) | prophet (new lines; old lines migrate, 3.8), apostle, disciple, messiah |
| Angels, gods under the god | **Keepers** ("the Keeper of the Deep") | angel, archangel, saint, demon, devil |
| Chosen people | **the Bearers** and their **Charge** | chosen people, promised land |
| Afterlife beliefs | **the Further Shore**, **the Turning**, **the Return**, **the Long Sleep** (tooltips: an afterlife, rebirth, return from death, final rest) | heaven, hell, paradise, reincarnation, karma, nirvana |
| Priesthood | **the House of the God**, priests, keepers of the shrine | church, mosque, temple names from real faiths |
| Rival faiths | **the Way of {name}** | heresy (as a label; the chronicle may say "a new way") |
| Great flood / fire | **the Great Water**, **the Great Burning**; survivors: **the Remnant** | the Flood, Ark of Noah, apocalypse |
| Sin, judgement | wrong, breach, deed, heart | sin, sinner, damnation |

The chronicle voice lint (3.31) rejects every word in the right-hand column in generated text.

### 0.3 Relation to the other v4 designs

- **gameplay-v4 (`docs/reviews/gameplay-v4.md`).** Situations, callings, zones and setups are a separate plan and
  are **not required** by this spec. Mythos owns the **card director** (`Sim/Cards.*`). If Situations are built, they
  register with the director as card kinds of their own (`CardKind::Situation`) so one budget, one panel and one
  auto-pace rule cover both. gameplay-v4's Victory prayers and rival preachers are absorbed here: Victory is a
  competing prayer (3.10), preachers are rival voices (3.21). Its setups (`shape`, `temperament`) overlap world
  archetypes and the first people; this spec's versions are the ones to build.
- **story-v4 (`docs/reviews/story-v4.md`).** Story is narration with no teeth in its phase 1. This spec gives the
  overlapping parts teeth and owns them: **figures** (story-v4 section 3) live in `Sim/Characters.*` (Story reads
  them), **prophecy** becomes warnings and promises spoken through the Word (3.9, 3.14, 3.17), and **tenets and
  sayings** become doctrine and legends (`Sim/Doctrine.*`, `Sim/Legends.*`). Houses, threads, named wars, acts and
  the epilogue stay in Story, to be built later on top of these modules. Section 1.9 maps every story-v4 piece.

---

## 1. Architecture

### 1.1 New sim modules (namespace `dm::<ns>`; plain C++20 under FOUNDATION-CPP rules)

| Module | Files | Namespace | Owns (state) | Phase |
|---|---|---|---|---|
| **Mythos core** | `Sim/Mythos.h/.cpp` | `mythos::` | per-culture `Faith` (aspect, agency, obedience, dependence, self-reliance, survivor memory), presence stance, the yearly **Factors** every other module reads, blessings' consequences, disputes, coming of age, withdrawal, the legacy verdict | v1 (+v2, v3) |
| **Cards (director)** | `Sim/Cards.h/.cpp` | `cards::` | the card list, the attention budget, card lifecycle, `answer` dispatch, defaults on lapse | v1 |
| **Doctrine** | `Sim/Doctrine.h/.cpp` | `doctrine::` | decrees, breaches and conflicts, utterances (the Word) and interpretation, judgement doctrine, pacts, the rite, the Land decree, revelations, beliefs about death | v1 (+v2, v3) |
| **Characters** | `Sim/Characters.h/.cpp` | `characters::` | the figure registry (messengers and candidates, rulers, healers, the tested, rival voices' figures), personality, hidden heart, credibility, trials | v1 (+v2) |
| **Legends** | `Sim/Legends.h/.cpp` | `legends::` | stories of witnessed deeds, their drift, retelling and fading, the "lives" album | v2 (+v3) |
| **Faiths** | `Sim/Faiths.h/.cpp` | `faiths::` | the House of the God per culture (strength, wealth, authority, corruption), rival voices and the Ways they found, the Bearers | v2 (+v3) |
| **Omens** | `Sim/Omens.h/.cpp` | `omens::` | the foresight queue of coming disasters, warnings and the people's responses | v2 |
| **Keepers** | `Sim/Keepers.h/.cpp` | `keepers::` | guardians, their domains, dispositions, loyalty and acts | v3 |
| **Renewal** | `Sim/Renewal.h/.cpp` | `renewal::` | the Great Water / Great Burning orchestration, the Remnant, and `renewal::reseed` (new game from this world) | v3 |
| (shared types) | `Sim/MythosTypes.h` | `dm::` | every persisted struct and enum of the modules above, so the contract is frozen in one file by Phase 0 | v1 |

Each module follows FOUNDATION-CPP part 2: a header with its tune struct, a stub in `SimTests/stubs/<Module>_stub.cpp`
with neutral behaviour, rows in `StubHooks.h`, its tests `SimTests/test_<key>*.cpp`, and a `run.ps1 <key>` row. Keys:
`mythos`, `cards`, `doctrine`, `characters`, `legends`, `faiths`, `omens`, `keepers`, `renewal`.

### 1.2 World state (persisted)

`World` gains one member, `MythosState mythos` (in `State.h`, defined in `MythosTypes.h`). Each module writes only
its own part (writers, 1.7). Shapes (binding; caps in brackets are also the save-size bound):

```cpp
enum class Presence : uint8_t { Wonders, Messengers, Unseen, Walk /* reserved, refused */ };
enum class Aspect   : uint8_t { Unknown, Parent, Ruler, Mystery, Threat };
enum class Archetype: uint8_t { Balanced, Generous, Dangerous, Beautiful, Strange };
enum class Legacy   : uint8_t { None, ObedientEmpire, CompassionateCivilisation, TerrifiedWorld, ThrivingWilderness, Unneeded };
enum class DecreeArea : uint8_t { Violence, Wealth, Worship, Honesty, Hospitality, Care, Land, Count };
enum class Clarity  : uint8_t { Exact, Parable, Dream, Sign };
enum class Channel  : uint8_t { Messenger, Dream, Sky };
enum class CardKind : uint8_t { ShapePeople, Presence, FirstDecree, DecreeConflict, Breach, MessengerChoice,
	CompetingPrayers, Judgement, PactOffer, PactBroken, Blessing, Warning, WarningExploited, Trial, Dispute,
	Institution, RivalVoice, LandClaim, Revelation, RevelationMisuse, DeathQuestion, Bearers, KeeperDispute,
	ComingOfAge, SilenceReport, Remnant, Situation /* gameplay-v4, if built */, Count };

struct Faith {                       // one per culture ever born (index = culture id)
	Aspect aspect = Aspect::Unknown; double aspectSince = 0;
	std::array<double, 4> image{};    // fading Parent, Ruler, Mystery, Threat impressions (3.3)
	double agency = 0.3, obedience = 0.5, dependence = 0.3, selfReliance = 0;
	double clarity = 0.5;             // fading mean clarity of what the god has said (Exact 1 .. Sign 0)
	double visible = 0;               // fading count of witnessed deeds (presence felt)
	double judgedBy = 0;              // -1 deeds .. +1 hearts (3.11)
	double mercy = 0, sternness = 0;  // fading sentences: forgiven / punished (3.12)
	double survivorLove = 0, survivorFear = 0, survivorUntil = -1;   // the living generation's memory (3.12)
	int8_t death = 0, deathSure = 0;  // DeathBelief, Certainty (3.27)
	double bearer = -1, superiority = 0; int8_t charge = 0;   // the Bearers (3.24); bearer = year chosen, -1 none
	double grown = -1;                // year they came of age (3.28), -1 not yet
};
struct Factors { /* section 1.4 */ };
struct Decree { int culture; DecreeArea area; int8_t stance; Clarity clarity; double since, kept; int by /* figure */; };
struct Utterance { int id, culture, figure; uint8_t content; Clarity clarity; Channel channel; double year, fidelity;
	int8_t meant, heard; uint8_t outcome; };                                      // [last 64]
struct Pact { int id, culture; uint8_t godTerm, peopleTerm; double sworn, until; double godKept, peopleKept;
	uint8_t state; int figure; };                                                 // [24]
struct Figure { int id; uint8_t kind, archetype; int culture, home; std::string name, byname;
	double born, died = -1, credibility, reach, fame; std::array<float, 4> person{};  // zeal, gentleness, cunning, pride
	std::array<float, 3> heart{};      // integrity, humility, faith: hidden until looked into
	uint8_t fate = 0; uint16_t flags = 0; int trial = -1; };                       // [160]
struct Card { int id; CardKind kind; uint8_t state; int culture, other, settlement, figure, cell;
	double opened, deadline, severity; int8_t choice = -1, lapse; std::array<int, 4> ref{}; NumBag data; };   // [open 2, closed last 48]
struct Legend { int id, culture, settlement, cell; std::string deed; double year, truth, told, vitality;
	uint8_t moral, credit, versions; int source /* entry id */; };                // [12 per culture, 72]
struct House { int culture; double strength, wealth, authority, corruption; double since; };   // per culture
struct RivalVoice { int id, culture, figure; double following, since; int way /* culture id after a split */; uint8_t state; };  // [12]
struct Omen { int id; uint8_t kind; int cell, culture; double due, power, ang; uint8_t warned, response; int figure; };     // [16]
struct Keeper { int id; uint8_t domain, disposition; int culture; std::string name; double appointed, loyalty, will; double rogueUntil = -1; };  // [5]
struct Revelation { int culture; uint8_t kind, outcome; double year; bool early; };   // [7 x cultures]
struct MythosState {
	Presence presence = Presence::Wonders; double presenceSince = 0; Archetype archetype = Archetype::Balanced;
	std::vector<Faith> faith; std::vector<Factors> factors;   // index = culture id
	std::vector<Decree> decrees; std::vector<Utterance> words; std::vector<Pact> pacts;
	std::vector<Figure> figures; std::vector<Card> cards; std::vector<Legend> legends;
	std::vector<House> houses; std::vector<RivalVoice> rivals; std::vector<Omen> omens;
	std::vector<Keeper> keepers; std::vector<Revelation> revelations;
	double withdrawnUntil = -1; bool withdrawAnnounced = false;
	int renewal = 0; double renewedYear = -1;                 // 0 none, 1 water, 2 burning
	NumBag num;                                               // module counters, keys prefixed "mythos_", "cards_", ...
	int nextId = 1;
};
```

`Ending` gains `Legacy legacy = Legacy::None` (section 4). `Settings` gains `Archetype archetype` (3.1).
`Culture::traits` gains `compassion`, `ambition`, `courage` (3.5). `Prophet` gains `int figure = -1` (3.8).
`PrayerKind` gains `Victory`, `Sun` (3.10). `Cause` gains `Deluge`, `Burning` (3.30). `powers::Group` gains `Word`.

### 1.3 Save format

Saves stay readable both ways. Phase 0 adds **extension blocks** to `state::serialize`: after the version-1 payload, a
list of `{ tag (4 chars), version (u16), length (u32), bytes }`, ending with tag `"END_"`. A loader skips unknown
tags and leaves missing ones at their defaults; `kVersion` becomes 2, and a version-1 file loads with an empty
`MythosState` (`mythos::rebuild` then creates a `Faith` per culture at its defaults). Mythos is one block, `"MYTH"`,
with one sub-block per module (so gameplay-v4's and story-v4's future state can add their own tags without a
version fight). `deserialize` validates every id inside the block (figures, cards, legends naming dead or missing
entities are refused, as FOUNDATION-CPP 4.3 does for the rest). Bound: the block stays under 80 KB at year 4,500
(caps in 1.2).

### 1.4 Factors: how mythos reaches the rest of the sim

Mythos does not write other modules' state. Once a year it computes, per culture, a **Factors** record of
multipliers and offsets from every mythos system (decrees, rite, aspect, agency, the House, blessings, death,
Bearers, archetype). Each owner module reads `mythos::factors(w, cultureId)` (read-only, returns the neutral record
for a bad id) at the one place its rule needs it. **Neutral values reproduce v3.1 exactly**, so the stubbed and the
untouched game are identical.

```cpp
struct Factors {                     // neutral = no effect
	// Economy (civ::yearly)
	double growthR = 1, K = 1, famineLoss = 1, plagueDeath = 1, research = 1, expandCooldown = 1;
	double farmConvert = 1;           // farm[] growth rate on new cells
	bool   farmAncient = true;        // may old-growth cells be farmed (3.22)
	bool   claimAncient = true;       // do territories include old-growth cells (3.22)
	// Society (society::yearly / witness)
	double devotionBase = 0, loveTarget = 0, prayerChance = 1, answeredGain = 1, unansweredPenalty = 1;
	double aweS = 1;                  // the culture's weight in Awe income S
	double schism = 1;                // estrangement and secession pace
	// Diplomacy (society::diplomacy / onArrive)
	double warV = 0;                  // added to DiplomacyTune::warV (negative = harder to start a war)
	double aggression = 0, battle = 1, defence = 1, relDrift = 0, plagueHop = 1, warStrength = 1, tradeEvery = 1;
	// Disasters (powers::natural) and Climate
	double naturalRate = 1, harmTaken = 1;   // harmTaken: damage from war, disaster and famine (Endurance blessings use the town's own)
	double co2Uptake = 1;             // Climate: vegetation's CO2 uptake on the culture's land (the World rite, 3.13)
	std::array<double, 6> expect{ 1, 1, 1, 1, 1, 1 };   // prayer chance by PrayerKind (living legends, 3.15)
};
```

**Call graph additions** (FOUNDATION-CPP 14): Society, Diplomacy, Civ, Economy, Climate, Disasters and Powers →
Mythos (`factors`, `townFactors`, `witnessScale`, `aspect`, `warGoal`, `neglectScale`: read-only); Mythos → Doctrine,
Characters, Faiths, Omens, Legends, Keepers (yearly), Cards (`propose`), Society (`nudge`, `remember`, `secede`,
`answerPrayer`, `raiseProphet`, `shapeTraits`), Civ (`cure`, `infect`, `damage`, `exodus`, `demoteTemple`,
`addScience`, `strikeKnowledge`), Powers (`pay`), Diplomacy (`declareWar`, `makePeace`, `swearPeace`); Cards → every
source module's answer handler; Disasters → Omens (`foresee`); Sim → Mythos (`attach`, `rebuild`, `yearly`,
`legacy`), Cards (`yearly`), Renewal (`reseed`). No module calls Cards except to propose or answer.

Settlement-level effects (blessings, the Remnant) are kept per settlement in `MythosState` and read through
`mythos::townFactors(w, sid)` (same neutral rule: `K`, `growthR`, `harmTaken`, `plagueDeath`).

### 1.5 Schedule (one line each for the Sim owner)

The yearly group (SPEC-SIM 3.12 / FOUNDATION-CPP 15) becomes:

```
civ::yearly -> society::yearly (ends with diplomacy and updateEpithets) -> mythos::yearly -> cards::yearly
-> climate::yearly -> powers::natural -> sim::refreshStats -> chronicle::snapshot -> sim::checkEnding
```

`mythos::yearly` calls, in this order and only for modules whose phase flag is on: `doctrine::yearly`,
`characters::yearly`, `faiths::yearly`, `omens::yearly`, `legends::yearly`, `keepers::yearly`, then its own pass
(faith, factors, consequences). Each submodule may **propose** cards (`cards::propose`); `cards::yearly` then opens
at most one (section 2). `sim::use` calls `mythos::attach(w)` **before** `chronicle::attach(w)` (Mythos subscribes to
the bus like Chronicle: tokens in scratch, re-subscribed exactly once; it only records, never acts, inside a handler)
and `mythos::rebuild(w)` after `sim::rederive`. `sim::checkEnding` calls `mythos::legacy(w)` to fill `ending.legacy`.

### 1.6 Tuning

One tune struct per module (`MythosTune`, `CardsTune`, `DoctrineTune`, `CharactersTune`, `LegendsTune`,
`FaithsTune`, `OmensTune`, `KeepersTune`, `RenewalTune`), defaults as in this document, read with `w.tune<T>()`.
`MythosTune::phase` (0 = off, 1 = v1 systems, 2 = v2, 3 = v3) gates everything: at 0 the mythos modules do nothing,
the new powers are locked and every Factors record is neutral. `MythosTune::emergent` (default false) gates the rules
that move a world whose god never touched the mythos systems (agency on research, aspect on prayers, the House's
corruption, rival voices); the balance owner turns it on in `Balance.cpp` with before/after tables. The TUNE blocks
below give each system's fields.

### 1.7 Writers (additions to SPEC-SIM 1.1 / FOUNDATION-CPP 13; binding)

| state | written by |
|---|---|
| `mythos.presence`, `faith[*]` (aspect, image, agency, obedience, dependence, selfReliance, clarity, visible, judgedBy, mercy, sternness, survivor*, grown), `factors`, `withdrawnUntil`, blessings | Mythos |
| `faith[*].death / deathSure`, `decrees`, `words`, `pacts`, `revelations` | Doctrine |
| `faith[*].bearer / superiority / charge`, `houses`, `rivals` | Faiths |
| `figures` | Characters (Faiths creates rival voices' figures through `characters::spawn`) |
| `cards` | Cards (submodules only propose; answers go through `cards::answer`) |
| `legends` | Legends |
| `omens` | Omens (Disasters reports scheduled natural events through `omens::foresee`) |
| `keepers` | Keepers |
| `renewal`, `renewedYear`; the Remnant | Renewal |
| `mythos.archetype` (copy of `settings.archetype`) | Sim (at create) |
| `culture.traits.compassion / ambition / courage` | Society (`newCulture`; the first people's shaping through `society::shapeTraits`) |
| `culture.prophet.figure` | Society (`raiseProphet` with `RaiseOpts::figure`) |
| `settlement.devotion / love` changes caused by mythos | Society only: Mythos asks through `society::nudge(w, sid, dDevotion, dLove)` (new [society] API, clamped, never NaN) |
| `god.awe` spending for mythos acts | Powers only: `powers::pay(w, what, amount) -> bool` (new [powers] API; emits `awe:short` like `use`) |
| `ending.legacy` | Sim (`checkEnding`, from `mythos::legacy`) |

Mythos never writes a settlement, culture, relation or cell field directly. Every effect on them goes through Factors
(read by the owner) or one of the owner APIs above or already public (`civ::damage`, `civ::infect`,
`civ::addScience`, `society::makePeace`, `society::declareWar`, `powers::start`, `life::ignite`).

### 1.8 Events (add-only rows in `Core/Events.h`; Phase 0)

| event | payload | emitted by |
|---|---|---|
| `card` | `{ id, kind, stage, culture, choice }` stage: `open`, `answered`, `lapsed`, `withdrawn` | Cards |
| `presence` | `{ presence, prev }` | Mythos |
| `aspect` | `{ culture, aspect, prev }` | Mythos |
| `decree` | `{ culture, area, stance, prev, clarity }` (stance −1 = repealed) | Doctrine |
| `breach` | `{ culture, area, settlement, figure, severity }` | Doctrine |
| `word` | `{ id, culture, content, clarity, channel, outcome }` outcome: `heard`, `wisdom`, `sect`, `misread`, `catastrophe` | Doctrine |
| `pact` | `{ id, culture, what }` what: `sworn`, `kept`, `god-broke`, `people-broke`, `forgiven`, `lapsed` | Doctrine |
| `figure` | `{ id, kind, culture, settlement, what }` what: `chosen`, `rise`, `vindicated`, `discredited`, `redeemed`, `fallen`, `exalted`, `died` | Characters |
| `trial` | `{ figure, kind, what }` what: `begun`, `passed`, `failed` | Characters |
| `judgement` | `{ card, culture, by, sentence }` by: `deed`, `heart`; sentence: `forgive`, `warn`, `remove`, `disaster` | Doctrine |
| `legend` | `{ id, culture, what }` what: `born`, `grew`, `credited`, `forgotten`, `inherited` | Legends |
| `blessing` | `{ settlement, kind, start }` | Mythos |
| `omen` | `{ id, kind, cell, culture, what }` what: `foreseen`, `warned`, `repented`, `prepared`, `exploited`, `dismissed`, `struck`, `averted` | Omens |
| `dispute` | `{ culture, kind, answer }` | Mythos |
| `house` | `{ culture, what }` what: `rose`, `corrupt`, `cleansed`, `reformed`, `endorsed`, `fell` | Faiths |
| `rival` | `{ id, culture, figure, what }` what: `rise`, `exposed`, `martyr`, `faded`, `split` | Faiths |
| `bearers` | `{ culture, charge, what }` what: `chosen`, `proud`, `rebuked`, `released`, `replaced` | Faiths |
| `revelation` | `{ culture, kind, what, early }` what: `given`, `shared`, `hoarded`, `weaponised` | Doctrine |
| `keeper` | `{ id, domain, culture, what }` what: `appointed`, `acted`, `disagreed`, `rogue`, `dismissed` | Keepers |
| `grown` | `{ culture, answer }` | Mythos |
| `withdraw` | `{ start, announced }` | Mythos |
| `renewal` | `{ kind, what }` what: `begun`, `remnant`, `ended` | Renewal |

Payload strings (`what`, `kind`, `stance` names) are `std::string` (open sets), ids `int`, numbers `double`. The
`ending` payload gains `legacy`.

### 1.9 story-v4 mapping (what this spec takes over)

| story-v4 | Here |
|---|---|
| 3.1 figures (founder, ruler, prophet, hero, voyager, sage, doubter, captain) | `characters::` registry: messenger, candidate, ruler, healer, tested, rival voice are built in v1-v2; founder, hero, voyager, sage, doubter, captain are added by Story later as `Figure` kinds (the struct and the API already carry them) |
| 3.2 rulers and houses | v1 keeps one ruler figure per capital (needed for "remove the ruler" and trials); dynasties stay Story's |
| 5 prophecy | warnings (3.17) and promises (pacts 3.14) spoken through the Word; fulfilment and failure move credibility |
| 7 tenets, sayings, perception vs truth | doctrine (decrees, judgement doctrine, rite, death belief) + legends (3.15, misread credit included) |
| 4 threads, 6 acts, 8 UI moments, 9 templates | Story (not built here); the mythos chronicle lines (3.31) use the same placeholders |

### 1.10 Determinism and cost

- All mythos randomness comes from forks: `w.rng.fork("myth:<module>:<what>:<id>:<year>")`. No mythos module
  advances `w.rng` itself, so a world whose god ignores mythos follows the same stream as before except through state
  that the rules change (and with `phase` 0, bit for bit).
- Iterate cultures, settlements, figures, cards and legends by ascending id; every sort ties on id.
- Cost: `mythos::yearly` with every submodule ≤ 0.35 ms mean at L = 6 with 120 settlements and 6 cultures, any single
  yearly ≤ 3 ms; handlers O(1). Mean tick stays ≤ 2.5 ms.

---

## 2. The decision loop

### 2.1 How decisions reach the player

Three entry points, all non-modal:

1. **Cards** (the Mythos panel, left edge, below the existing "Now" strip; on narrow layouts a bottom sheet). The
   world asks; the player answers. A card shows: an icon and a title in the people's own names; a context block
   (who, what happened, why; for judgement cards the deed and, once looked into, the heart); a countdown ring in years
   and in real seconds at the current speed; 2-4 answer buttons, each with its cost and its expected effect in plain
   words; and "Let it be" (the lapse default, named in words: "They will decide for themselves"). Answering may arm a
   power and fly the camera (like gameplay-v4's Show me).
2. **The Word** (palette group **Word**, between Grace and the genesis powers): the god speaks when it chooses.
   Powers `speak` (5), `prophet` shown as **Messenger** (P, unchanged key), `bless` (6), `test` (8), `reveal` (7),
   `renew` (no key). Each opens a small chooser beside the armed chip (what to say, how clearly, through whom).
3. **The Faith panel** (a new tab of the culture panel, and the Mythos book, key `` ` ``): standing choices that are
   not urgent: the presence stance, each people's decrees and pacts, its messenger, its House, its beliefs, its
   legends, the Keepers (Pantheon tab), the Bearers. The **Withdraw** button sits in the time controls (key 9).

### 2.2 The director (`Sim/Cards.*`): pacing and budget

Every source proposes candidate matters; the director decides what becomes a card. Rules:

```cpp
struct CardsTune {
	double firstYear = 90;           // no card (but genesis' ShapePeople / Presence) before this year of history
	double minGap = 70;              // years between two card openings (35 s at 1x)
	double targetGap = 200;          // the budget refills one card per this many years (100 s at 1x)
	double maxGap = 420;             // past this many years without a card, open the best proposal even if weak
	int    burst = 2;                // budget bucket size
	int    maxOpen = 2;              // open cards at once
	double cultureGap = 150;         // one culture gets a new card at most this often (unless severity >= 0.8)
	double kindGap = 400;            // the same kind again within this many years: priority x 0.5
	double novelty = 1.5;            // a kind never seen in this world: priority x this
	double minPriority = 0.25;       // below this a proposal waits (unless maxGap forces it)
	double withdrawnHold = 1;        // while the god has withdrawn: cards open as SilenceReport digests only
	std::array<double, int(CardKind::Count)> deadline{};   // years open before lapse, per kind (default 60; table 2.3)
	std::array<double, int(CardKind::Count)> weight{};     // priority weight per kind (default 1)
};
```

- **Proposal:** `cards::propose(w, Proposal{ kind, culture, other, settlement, figure, cell, severity, ref, data })`
  during the submodules' yearly passes. Proposals are kept for one year only (sources re-propose while the matter
  stands); identical `(kind, culture, ref[0])` proposals merge.
- **Priority** = `severity × weight[kind] × novelty factor × (kindGap factor) × ageFit`, where severity 0..1 is the
  source's measure (people at stake / world people, × 1.5 for a capital, or the source's own scale), ageFit 1 inside
  the kind's age band (2.3) else 0.
- **Opening:** at most one card per year, only if a budget token is available, `year − lastOpen ≥ minGap`, fewer than
  `maxOpen` are open, and priority ≥ `minPriority` (or `year − lastOpen ≥ maxGap`). Ties break by kind order then
  culture id. Opening emits `card { stage: open }`.
- **Answer:** `cards::answer(w, id, choice) → bool` validates the choice, pays its cost through `powers::pay` (refused
  if the god cannot pay: `awe:short`, the card stays open), dispatches to the source module's handler (a `static
  const` table of function pointers indexed by kind: no globals), marks it answered and emits `card { answered }`.
  Answering is a deed (`god.deeds["card:<kind>"]++`) but is witnessed only when the handler acts visibly.
- **Lapse:** at the deadline the source's `lapse` choice is applied (always defined; usually "the people decide": an
  agency-weighted forked roll between their own options), `card { lapsed }`.
- **Situations:** if gameplay-v4's module exists it proposes `CardKind::Situation` with its own severity; the card
  shows the situation UI. One budget covers both.

**Cadence targets** (simrun, 8 seeds; section 5): 15-28 cards per 3,600-year history; the median gap between
openings 150-260 years (75-130 s at 1x); ≥ 90 % of gaps ≤ 420 years; never two openings within 70 years; never
more than 2 open; every age that lasts ≥ 200 years opens at least one card. With auto-pace (2.4) this is about one
meaningful choice every 1-3 real minutes, on top of the powers and prayers the game already has.

### 2.3 Card catalogue (kinds, sources, windows, answers)

Deadlines are in years (real seconds at 1x in brackets). "Lapse" is the default.

| kind | source | ages | opens when | window | answers (cost) | lapse |
|---|---|---|---|---|---|---|
| ShapePeople | Mythos | awaken | the first people wake (not budgeted) | until answered (genesis is paused for it) | 3.5 | random rolls |
| Presence | Mythos | awaken, then once per age on request | first awaken (not budgeted) | until answered | Wonders / Messengers / Unseen (Walk greyed) | Wonders |
| FirstDecree | Doctrine | Stone-Bronze | a culture's first shrine, or year 200, no decree yet | 80 (40 s) | give one decree (3.6, cost by clarity) / "they will find their own way" | none: the people take a custom (3.6) |
| DecreeConflict | Doctrine | any | two of a culture's decrees collide in a case (3.7) | 50 (25 s) | 3 case-specific answers | the stricter decree wins, obedience −0.05 |
| Breach | Doctrine | any | a severe breach (severity ≥ 0.5) | 50 | becomes a Judgement card (3.11) | — |
| MessengerChoice | Characters | Bronze+ | the god arms Messenger on a town (not budgeted: player-initiated), or a devout culture "seeks a voice" (no messenger 250 y, devotion ≥ 0.45) | 60 | choose 1 of 3 candidates / none | none |
| CompetingPrayers | Society + Cards | any | two live prayers that cannot both be granted (3.10) | the prayers' own (≤ 15 y; 8 s) | answer A / B / a middle way / neither | neither |
| Judgement | Doctrine + Characters | Bronze+ | a breach or a figure's deed with context (3.11) | 60 | by deed or by heart × forgive / warn / remove the ruler / disaster | the people judge (custom) |
| PactOffer | Doctrine | Bronze+ | a culture asks for a pact (after a great answered prayer or a great fear) | 80 | accept terms / change terms / refuse | refused |
| PactBroken | Doctrine | any | the people broke a pact term | 60 | forgive / warn / hold them to it (punish) / end the pact | pact ends, sternness 0 |
| Blessing | Mythos | Bronze+ | a blessing's consequence escalates: envy, dependence or conquest (3.16) | 60 | shield / share / withdraw / let it be | let it be |
| Warning | Omens | any | a foreseen disaster ≥ 25 years ahead (3.17) | until 10 years before | dream / strange stars / failed harvest / messenger / say nothing | say nothing |
| WarningExploited | Omens | Bronze+ | a warning was exploited (a ruler or priest seized on it) | 40 | rebuke / allow / strike | allow |
| Trial | Characters | Iron+ | a worthy figure (3.18) | 80 | power / hardship / temptation / impossible charge / leave them be | leave them be |
| Dispute | Mythos | Bronze+ | the people argue (3.19) | 50 | explain / silence / change your mind / demand obedience | silence |
| Institution | Faiths | Iron+ | the House's corruption ≥ 0.5 (3.20) | 80 | cleanse / reform / endorse / abandon | endorse (it goes on) |
| RivalVoice | Faiths | Iron+ | a rival voice's following ≥ 0.15 (3.21) | 80 | expose / send your messenger / trust the people | trust the people |
| LandClaim | Doctrine | Bronze+ | a hungry town's best land is old growth (3.22) | 50 | allow / refuse with a sign / a grove | by the Land decree |
| Revelation | Doctrine | v3 | a culture is ready, or asks (3.25) | 80 | give now (choose clarity) / wait / give to another | wait |
| RevelationMisuse | Doctrine | v3 | a revelation was hoarded or weaponised | 60 | shame them / give it to all / let it be | let it be |
| DeathQuestion | Doctrine | v3 | great deaths and no sure belief (3.27) | 60 | four beliefs (× clarity) / silence | silence |
| Bearers | Faiths | v3 | superiority ≥ 0.5 (3.24) | 80 | rebuke / renew the charge / release / choose another | renew (it grows) |
| KeeperDispute | Keepers | v3 | a Keeper disagrees (3.26) | 60 | grant / refuse / change its temper / dismiss | refuse |
| ComingOfAge | Mythos | v3 | self-reliance ≥ 0.5 (3.28) | 80 | bless their going / remind them / hold them close | bless their going |
| SilenceReport | Mythos | v3 | a withdrawal ends (3.29; not budgeted) | until answered | return gently / in glory / stay silent a century more | gently |
| Remnant | Renewal | v3 | the Great Water or Burning ends (not budgeted) | until answered | what the Remnant is told: explain / silence / a pact | silence |

### 2.4 Pacing on the client

- **Auto-pace** (Flow): while a card is open the game runs at most at 1x (the player's own speed key during that
  card wins, as in gameplay-v4 3.9). Setting "Pause when the world asks" (off by default; on in the first game's
  tutorial) pauses on every card opening.
- **Never spam:** at most one card toast per 20 s; a card arriving while another is open slides in below it with a
  quiet chime only. When the HUD is hidden, the card counter on the eye button shows the open count.

---

## 3. Systems

Each system: **Rules** (state, numbers in its TUNE struct), **Hooks** (which existing module reads what), **Player**
(panel, card or power; how often), **Chronicle and myth**, **Look and sound**, **Tests** (SimTests and simrun).
Common simrun flags are in section 5.

### 3.1 World archetypes (v1)

**Rules.** `Settings::archetype` (persisted; the setup screen sets it; default Balanced = today's world). It is a
bundle of parameters for the owners' existing generators plus a mythic seed:

| archetype | terrain / climate / life (owner params) | disasters | mythic seed | the price |
|---|---|---|---|---|
| **Balanced** | today | today | none | none |
| **Generous** | `fert` +0.15 at generation; Climate `moistBias` +0.05; Life `capacityK` × 1.1 | natural rates × 0.6 | legends of plenty are born at half the threshold | dependence target +0.05; agency target −0.03 ("the world gives; why ask why") |
| **Dangerous** | Terrain `ridgeK` × 1.3 (more mountains and volcanic belts); `fert` −0.05 | natural rates × 1.6, quakes and eruptions × 2, droughts × 1.3; foresight lead +10 years | Threat impressions +0.1 at the waking | Awe income × 1.1 (they pray harder); devotion baseline +0.03 |
| **Beautiful** | Terrain `shelfK` × 1.3 and `islandK` × 1.3 (more coast and islands); Climate `noiseK` × 1.3 (more varied biomes) | today | the Art rite is the first custom; legends' vitality +0.1 | none in numbers; the world itself is the reward |
| **Strange** | 3-6 **marvels** placed by Terrain at generation (cell ids in `vars.num`: singing stones, warm springs in the ice, a lake that glows, a wood that whispers); marvel cells never burn and keep `fert` ≥ 0.8 | a permanent storm ring over one ocean (Disasters: a long storm that re-forms) | Mystery impressions +0.1; Sign and Dream fidelity × 1.2 within 0.2 rad of a marvel; legends are born at marvels | rival voices × 1.5 ("other powers walk here") |

```cpp
// MythosTune: archetype parameters, applied by the owners at generation through mythos::archetypeParams(w)
struct ArchetypeParams { double fert = 0, moistBias = 0, capacityK = 1, ridgeK = 1, shelfK = 1, islandK = 1,
	noiseK = 1, natural = 1, quakeVolcano = 1, drought = 1, foresightLead = 0, aweIncome = 1; int marvels = 0; };
```

**Hooks.** Terrain (`generate` reads `archetypeParams`), Climate (`moistBias`, `noiseK`), Life (`capacityK`),
Disasters (`natural` via `Factors::naturalRate` = archetype × calamity), Society (Awe income), Mythos (seeds).

**Player.** Setup screen row "What kind of world?" with four cards and a one-line promise each ("Generous: rain and
plenty; they may never learn to ask why"). Shown again on the planet card. No cards in play.

**Chronicle and myth.** Genesis line per archetype (w3, told in the tale). Generous: "The world was made soft and wet
and full of fruit, and the first people never once went hungry, and never once asked why." Strange: "There were
stones on that world that sang at dusk, and nobody ever learned who had taught them."

**Look and sound.** Generous: lusher palette from orbit (Planet reads the fields; no special code). Dangerous: more
smoke on idle volcanoes (FX ambient). Beautiful: more islands and reefs. Strange: marvel cells get a faint glow decal
and an ambient tone (Audio cue `marvel`); the storm ring is visible from orbit.

**Tests.** `test_mythos_archetype`: each archetype's parameters reach the generators (land fraction within 36-42 %,
Beautiful 34-42 %); marvels on land, never within 7 hops of each other; Balanced bit-identical to the v3.1 world
(`dmt::hash`). simrun `--archetype <name>`: every archetype meets the SPEC-SIM 4 Ark window on ≥ 5/6 passive seeds;
Dangerous loses ≥ 1.3 × Balanced's people to natural events; Generous has ≤ 0.7 × Balanced's famine entries.

### 3.2 Presence: how near the god comes (v1)

**Rules.** `mythos.presence` (world-wide):

| stance | the god's acts | the Word | effect on the people |
|---|---|---|---|
| **Wonders** (default; today's game) | witnessed as today | every channel; an Exact word from the sky costs × 2 | none (today) |
| **Messengers** | a miracle within 0.15 rad of a living messenger: faith × 1.5 and the messenger's credibility +0.05; elsewhere faith × 0.6 | Word costs × 0.6; a culture without a messenger hears only Dreams and Signs | agency target +0.05; Parent and Ruler impressions grow through the messenger |
| **Unseen** | acts read as fortune: witness faith × 0.25, no jaded, no blame or envy (favour sums × 0), memory grace and wrath × 0.4 ("a hidden hand") | Dreams and Signs only | agency target +0.15; every act adds Mystery; epithets from the absent family |
| **Walk** | reserved (deferred by the owner) | — | `setPresence` returns false |

A stance changes at most once per world age, costs 100 Awe (`powers::pay`), and the people notice: one Dispute
proposal per culture with agency ≥ 0.4 ("Why have you hidden your face?"). The first stance is chosen at the first
awakening (Presence card, free). `mythos::setPresence(w, p) → bool`.

```cpp
// MythosTune (presence)
double msgFaithNear = 1.5, msgFaithFar = 0.6, msgNearAng = 0.15, msgCred = 0.05, msgWordCost = 0.6;
double unseenFaith = 0.25, unseenMemory = 0.4, unseenAgency = 0.15, msgAgency = 0.05;
double presenceCost = 100, exactSkyCost = 2;
```

**Hooks.** Society `witness` reads `mythos::witnessScale(w, cell, kind)` → `{ faith, memory, favour, jaded }`
multipliers (all 1 under Wonders). Chronicle reads the stance for epithets and act lines.

**Player.** The Presence card at the waking (three portraits of the god's stance, one sentence each; Walk greyed:
"Not yet"); later the Faith panel's stance row ("Change how near you come: 100 Awe, once an age").

**Chronicle and myth.** `presence` (w2): "From that year the Umari no longer saw the hand that fed them, and began to
thank the weather instead." Messengers: "After that the god spoke only through the mouths of the people's own."

**Look and sound.** Unseen: the Word group shows a veiled icon; acts lose their divine glow (FX draws them as
weather: rain without the gold motes). Messengers: miracles near a messenger rise from the messenger's marker.

**Tests.** `test_mythos_presence`: the witness multipliers per stance; one change per age; Walk refused; Unseen acts
add no favour and × 0.4 memory. simrun `unseen-kind` (kind, Unseen from the start) ends Forgotten or Parting ≥ 5/8
with mean agency at year 2,000 ≥ kind's + 0.2; `msg-kind` (kind, Messengers, keeps a messenger in the capital) ends
Beloved ≥ 5/8.

### 3.3 Aspect: how they see the god (v1)

**Rules.** Each culture keeps four fading impressions `image[Parent, Ruler, Mystery, Threat]` (half-life 300 years).
Every deed, sentence and word adds to them (× the witness's share of its culture, like memory):

| impression | added by (amount per act) |
|---|---|
| **Parent** | an answered prayer 0.3; Heal, Bless, Rain or Bloom on their towns 0.1 (brush: 0.05 per second); a Parable or Dream 0.2; Forgive 0.3; Explain 0.2 |
| **Ruler** | a decree 0.3 (Exact 0.5); Demand obedience 0.3; a sentence by the deed 0.2; just punishment of a breach 0.3; a pact sworn 0.2 |
| **Mystery** | a Sign 0.3; answering with Silence 0.2; any act under Unseen 0.1; a withdrawal 0.5; each 100 years without a deed 0.2 |
| **Threat** | wrath on a town that broke nothing 0.4 (× the harm); a natural disaster credited to the god (legend) 0.2; a pact broken by the god 0.6; the Great Water or Burning 1.0 |

`aspect` = the largest impression when it is ≥ 0.15 and ≥ 1.2 × the second; it changes at most once per 40 years
(`Unknown` until one qualifies). Emits `aspect`.

| aspect | effect (when `emergent`, or once the god has used a mythos act on them) |
|---|---|
| Parent | prayer chance × 1.25; love drift target +0.08 (instead of 0); dependence target +0.1 |
| Ruler | obedience target +0.1; House authority growth × 1.5; agency target −0.05 |
| Mystery | agency target +0.08; jaded decay × 1.5 (wonder renews); devotion baseline −0.03 |
| Threat | the v3 fear rule applies whatever their love (pray harder before Atomic, defy after); obedience target +0.08; love drift target −0.05 |

```cpp
// MythosTune (aspect)
double imageHalfLife = 300, aspectMin = 0.15, aspectLead = 1.2, aspectHold = 40;
double parentPray = 1.25, parentLove = 0.08, parentDepend = 0.1, rulerObey = 0.1, rulerHouse = 1.5,
	rulerAgency = -0.05, mysteryAgency = 0.08, mysteryJaded = 1.5, mysteryDevotion = -0.03,
	threatObey = 0.08, threatLove = -0.05;
```

**Hooks.** Factors (`prayerChance`, `loveTarget`, `devotionBase`); Society's fear rule reads `mythos::aspect`;
Chronicle epithets gain one ungendered family per aspect for when no deed dominates: "the Keeper of the Hearth"
(Parent), "the Lawgiver" (Ruler), "the Unseen" (Mystery), "the Storm Over Us" (Threat).

**Player.** The culture panel: "They see you as a parent: they ask, and expect", with the four impressions as a small
four-petal glyph. An **aspect overlay** (Planet, colour by aspect) joins the overlay switch.

**Chronicle and myth.** `aspect` (w2, story budget): "By then the Vesh no longer called the god a storm. They called it
a judge, and kept their accounts in order."

**Look and sound.** The aspect glyph. Music (Audio) leans by the leading culture's aspect: Parent warmer, Ruler a
steady low drone, Mystery modal and sparse, Threat minor with low percussion (within the existing layer rules).

**Tests.** `test_mythos_aspect`: impressions per act, fading, hysteresis, one `aspect` event per change. simrun final
aspect of the Ark culture: `kind` Parent ≥ 6/8; `cruel` Threat ≥ 6/8; `lawgiver` (3.6) Ruler ≥ 5/8; `passive` Mystery
or Unknown ≥ 6/8.

### 3.4 Agency, obedience, dependence: free will that emerges (v1)

**Rules.** Per culture, yearly, each value moves part of the way toward a target built from what happened. None of
them is ever set by the player.

```
agencyT    = clamp(0.25 + 0.35 × (1 − clarity) + 0.25 × (1 − sat(visible / 20)) + 0.10 × curiosity + 0.15 × literacy
                   − 0.15 × dependence + presence term (3.2) + aspect term (3.3), 0.05, 0.95)
obedienceT = clamp(0.30 + 0.35 × devotion + 0.10 [Ruler] + 0.08 [Threat] + 0.15 × House authority
                   − 0.45 × agency × burden − 0.04 × max(0, decrees − 3) + survivor term (3.12), 0.05, 0.95)
dependenceT= clamp(0.15 + 0.35 × sat(answered prayers in the last century / 8) + 0.15 [a blessing active]
                   + 0.10 [Parent] − 0.30 × agency − 0.30 × selfReliance, 0, 1)
```

`clarity` = fading mean of the culture's heard words (Exact 1, Parable 0.65, Dream 0.4, Sign 0.15; half-life 200
years; 0.5 before any word); `visible` = fading count of witnessed deeds (half-life 100 years); `literacy` = 0.5 ×
share of towns with an academy + 0.5 × [the Writing revelation shared] (3.25); `burden` = mean strictness of the
culture's decrees (3.6). Withdrawal adds 0.004 a year to agencyT while it lasts (3.29).

| value | effect (research and schism terms are `emergent`-gated) |
|---|---|
| agency | research × (1 + 0.12 × (agency − 0.4)); schism pace × (0.7 + 0.6 × agency); misreads of words become wisdom or sect more than literal (3.9); disputes ∝ agency (3.19); rival voices are seen through more often (3.21) |
| obedience | breach chance × (1 − obedience) (3.6); each decree's effects scale by `kept` (≈ obedience × fidelity) |
| dependence | prayer chance × (1 + 0.5 × dependence); the unanswered penalty × (0.6 + 0.8 × dependence) (silence hurts a dependent people more); research × (1 − 0.1 × dependence) |

```cpp
// MythosTune (will)
double agencyDrift = 0.01, obedienceDrift = 0.02, dependenceDrift = 0.015;   // share of the gap per year
double visibleHalfLife = 100, visibleRef = 20, clarityHalfLife = 200;
std::array<double, 4> clarityValue{ 1.0, 0.65, 0.4, 0.15 };                  // Exact, Parable, Dream, Sign
double researchAgency = 0.12, schismAgency = 0.6, dependPray = 0.5, dependPenalty = 0.8, dependResearch = 0.1;
```

**Hooks.** Factors (`research`, `schism`, `prayerChance`, `unansweredPenalty`); Doctrine (breaches, interpretation);
Faiths (rival voices); Mythos (disputes, coming of age, legacy).

**Player.** Never a slider. The culture panel's "Will" line in words: "They think for themselves: they will argue,
and invent" / "They wait for your word: they obey, and depend", with the three values as small bars and a "why"
tooltip naming the two largest causes ("you speak plainly and often; they have few academies").

**Chronicle and myth.** No entry for drift; its consequences (disputes, ways, discoveries) tell it. The tale's god
paragraph names it once: "They had learned to think without being told, and they never quite forgave it."

**Tests.** `test_mythos_will`: each target term on synthetic cultures; drift; bounds; no NaN with zero cultures.
simrun: `exact-only` (decrees and words only Exact, often) agency ≤ 0.25 and obedience ≥ 0.65 at year 2,000 ≥ 6/8;
`parable-only` agency ≥ 0.5 ≥ 6/8; `passive` agency ≥ 0.6 ≥ 6/8; `prayers` dependence ≥ 0.55 ≥ 6/8.

### 3.5 Shaping the first people (v1)

**Rules.** `Traits` gains `compassion`, `ambition`, `courage` (0..1; `piety`, `aggression`, `curiosity`, `seafaring`
stay). Root cultures roll them like the others (0.5 ± spread); daughters drift. At the first awakening the
**ShapePeople** card sets the five named traits: each **Low** (0.25), **Middling** (0.5) or **High** (0.8); at most
two High, and each High needs one Low (a people cannot be good at everything). "Leave it to chance" rolls them. The
second band and later awakenings inherit the choice (± drift). `society::shapeTraits(w, cultureId, Traits)`.

| trait | strength | problem |
|---|---|---|
| **Curiosity** (existing) | research × (0.85 + 0.3 c) (existing `traitF`) | agency target +0.1 c; sects from misread words × (1 + 0.5 c); early revelations weaponised × (1 + c) |
| **Compassion** | famine losses × (1 − 0.25 c); plague deaths × (1 − 0.15 c); love drift +0.03 c; Kindness and Shelter customs | battle strength × (1 − 0.1 c); dependence target +0.1 c; settler cooldown × (1 + 0.2 c) |
| **Ambition** | settler cooldown × (1 − 0.2 a); build-edict Wonder in 20 years when a ≥ 0.7; growth r × (1 + 0.05 a) | Bearers' superiority × (1 + a); House corruption × (1 + 0.5 a); warnings exploited × (1 + a) |
| **Courage** | defence × (1 + 0.15 k); naval search maxCost × (1 + 0.2 k); fear's devotion bonus × (1 − 0.5 k) | `warStrength` gate × (1 − 0.2 k) (they attack stronger peoples); Demand obedience × (1 − 0.4 k) |
| **Aggression** (existing) | battle strength × (1 + 0.1 g) | the existing relations term (they war more) |

**Hooks.** Society (`newCulture`, `shapeTraits`); Economy and Diplomacy read the effects through Factors (Mythos folds
the traits in, so each owner reads one place); Faiths, Doctrine and Omens read the problems.

**Player.** The ShapePeople card: five rows of three pips, each with its strength and problem in one line, and a live
sentence ("they will be curious and brave, and not gentle"). The culture panel says the traits in words.

**Chronicle and myth.** The waking entry gains a clause from the two strongest traits: "... and they woke asking
questions, and would not stop" (curiosity); "... and the first thing they did was carry the lame one to the fire"
(compassion).

**Tests.** `test_society_traits`: the budget rule; inheritance and drift; validation. simrun `--shape <preset>`:
High-Compassion ≤ 0.8 × Low's famine deaths ≥ 6/8; High-Ambition ≥ 1.2 × the settlements at year 1,000 ≥ 6/8;
High-Courage ≥ 1.3 × the wars ≥ 5/8; every preset meets the SPEC-SIM 4 Ark window ≥ 5/6.

### 3.6 Decrees (v1)

**Rules.** A culture holds at most one decree per area. A decree is given by the Word (3.9) with a stance, a clarity
and, for Exact and Parable, a messenger. The people **keep** it at `kept` = obedience × fidelity (re-evaluated
yearly). With no decree in an area the people may form a **custom** (a stance chosen from their traits, forked, at
the culture's year 200 and at its first shrine) that works at half strength and is never breached (it is theirs).
Repealing is a word (`speak`, content Repeal).

| area | stance (strictness) | effects at `kept` = 1 (scaled by kept) | pressure to breach |
|---|---|---|---|
| **Violence** (the Decree of Blood) | **Peace** "shed no blood" (0.9) | `warV` −0.15 (harder to start a war); aggression −0.1; battle × 0.9 | they are attacked, or a neighbour on the brink (v ≤ −0.4) |
| | **Defence** "blood only for the gate" (0.5) | defence × 1.15; walls at peace in towns+ | a crusade edict |
| | **Valour** "strength is holy" (0.2) | battle × 1.1; aggression +0.1; relations −0.005 / yr with neighbours | never |
| **Wealth** (the Decree of Bread) | **Share** "leave the corners of the field" (0.7) | famine losses × 0.8; growth × 0.98; market research × 0.95 | ambition ≥ 0.6 and 2+ markets |
| | **Tithe** "a tenth to the House" (0.5) | aweS × 1.15; House wealth × 1.5 | famine |
| | **Gain** "increase is a blessing" (0.2) | trade every × 0.8; research × 1.04; House corruption × 1.3 | never |
| **Worship** (the Decree of the Altar) | the **Rite** (3.13) | 3.13 | 3.13 |
| **Honesty** (the Decree of the Tongue) | **Truth** "speak no false word" (0.8) | trade relations +0.002 / yr; word fidelity +0.1; rival voices' growth × 0.7 | war, or a Shelter case (3.7) |
| | **Discretion** "truth to whom it is owed" (0.4) | word fidelity +0.05 | low |
| | **Cunning** "the clever are blessed" (0.1) | research × 1.03; `warStrength` × 0.95; rival voices × 1.3 | never |
| **Hospitality** (the Decree of the Gate) | **Open** "the stranger eats first" (0.8) | relations +0.01 / yr with cultures in contact; plague hop × 1.3; foreign settlers may join their towns | war with a neighbour in contact |
| | **Guarded** "feed the stranger at the gate" (0.5) | relations +0.004 / yr | low |
| | **Closed** "our bread for our own" (0.2) | relations −0.008 / yr; plague hop × 0.7; Bearers' superiority × 1.5 | never |
| **Care** (the Decree of the Weak) | **Shelter** "carry the ones who cannot walk" (0.8) | famine losses × 0.75; plague deaths × 0.85; growth × 0.97 | a town 3+ famine years, or plague |
| | **Kin** "care begins at the hearth" (0.4) | famine losses × 0.9 | low |
| | **Strength** "the strong endure" (0.2) | growth × 1.03; famine losses × 1.2; battle × 1.05 | never |
| **Land** (the Decree of the Land) | Dominion / Stewardship / Equal standing (3.22) | 3.22 | 3.22 |

**Breaches.** Yearly per decree: `P = 0.02 × strictness × pressure × (1 − obedience)`, pressure 0..1 from the last
column (0.1 when the condition is absent). A breach picks the town with the most pressure, emits `breach` with a
severity (its share of the culture's people × pressure) and lowers `kept` by 0.1 for 20 years. Severity ≥ 0.5
proposes a Judgement card (3.11); smaller breaches are told (w1) and, with a Keeper of Scales, judged by it (3.26).

```cpp
// DoctrineTune (decrees)
double breachBase = 0.02, breachKept = -0.1, breachYears = 20, breachCard = 0.5, pressureIdle = 0.1;
double customYear = 200, customStrength = 0.5, keptDrift = 0.05;
```

**Hooks.** Every effect goes into Factors (Economy, Society and Diplomacy read them). Pressure is read from the owners'
read-only state (relations, famine, plague, markets).

**Player.** The **Decree tablet** (Faith panel, and the `speak` chooser's "Give a decree"): seven stone slots; each
shows the stance, how well it is kept ("kept by most"), how it was given (exactly, in a parable ...) and its effect
line. Giving one: area → stance (three cards with effect and strictness) → clarity → through whom → confirm (cost by
clarity, 3.9). The FirstDecree card prompts the first one early.

**Chronicle and myth.** `decree` (w2). Exact: "In the 212th year the Umari were told, in words nobody could mistake,
to shed no blood. They cut it over the door of every house." Parable: "The god told the Umari a story about a farmer
who left the corners of the field for strangers. They argued about it for a generation, and left the corners."
Custom (w1): "The Vesh had no law about strangers, so they made one: feed them at the gate, and count the spoons."
`breach` (w1 / w2): "At Qe-Luun the Decree of Blood was broken over a well, and the town was ashamed all winter."

**Look and sound.** A carved sigil per area; a new decree shows a short gold glyph rising over the capital (FX
`decree`); a breach a dark ripple over the town (FX `breach`, Audio a soft `toll`).

**Tests.** `test_doctrine_decrees`: one per area; `kept`; each effect reaches Factors exactly; breach probability and
pressure; customs; save round trip. simrun `lawgiver` (Peace, Share, Temples, Truth, Guarded, Shelter, Stewardship,
Exact words through a messenger, punishes breaches): war share ≤ 0.6 × passive's ≥ 5/8; famine deaths ≤ 0.8 ×
passive's ≥ 6/8; Ark inside the window ≥ 6/8. `valour` (Valour, Gain, Closed, Strength): war share ≥ 1.3 × passive's
≥ 5/8 and never above a third of the years (v3.1 cap).

### 3.7 When decrees collide (v1)

**Rules.** Doctrine checks yearly, per culture, a fixed table of **cases** where two held decrees (or a decree and
hunger) demand opposite things. A case needs both decrees with `kept` ≥ 0.4 and its trigger; it proposes a
DecreeConflict card (severity = people at stake / the culture's people). At most one open case per culture; the same
case again only after 150 years. Lapse: the stricter decree wins; the other's `kept` −0.1 and obedience −0.05
("they could not keep both").

| case | decrees | trigger | answers (cost) |
|---|---|---|---|
| **The raided weak** | Peace × Shelter | the culture is attacked | "The gate may be held" (Peace acts as Defence for 50 years; Ruler +0.2) / "No blood, even now" (defence × 0.6 in this war; a martyr legend; love +0.05; Parent +0.2) / "A sign between them" (80: the Omen's calm) |
| **The hidden stranger** | Truth × Shelter or Open | a war in which they shelter the enemy's refugees | "The lie that saves is no lie" (Truth's strictness −0.3 for good; agency +0.03; judgedBy → hearts) / "Truth, whatever it costs" (refugees given up: love −0.04, obedience +0.05; judgedBy → deeds) / "Tell them in a dream" (30: the people choose, forked by agency) |
| **The temple granary** | Tithe × Shelter | famine while the House is wealthy (≥ 0.5) | "Open the House's stores" (the famine ends in the town; House wealth −0.3, corruption −0.1) / "The tenth is the god's" (House authority +0.1; love −0.05) / "Feed them yourself" (arms Bloom on the town) |
| **The enemy at the gate** | Open × Peace or Defence | an army of a culture in contact nears a town | "Feed them and send them home" (relations +0.1 with them; defence × 0.8 at that town) / "The gate shuts" (Hospitality acts as Guarded for 40 years) |
| **The wood or the children** | Equal standing × any Care | famine and the best land is old growth | as LandClaim (3.22) |
| **The altar or the bread** | Temples rite × Share | a Wonder is due while a town hungers | "Build" (the Wonder on time; the famine goes on) / "Bread first" (Wonder delayed 30 years; K × 1.1 for 10 years) |
| **The clever and the true** | Cunning × Truth (a daughter's custom against its parent's decree) | a schism | "Both are mine" (Mystery +0.2) / "One of them is wrong" (choose one; the other's love −0.1) |
| **The strong and the weak** | Strength × Kindness rite | plague | "Let the sickness choose" (plague deaths × 1.2; growth × 1.03 for 30 years) / "Carry them" (Strength acts as Kin for 50 years) |

**Player.** The card shows both tablets side by side, the case in one line, and the answers.

**Chronicle and myth.** One w2 entry at resolution: "The Umari had been told to shed no blood and to carry the weak,
and when the Vesh came for the weak they had to choose. They held the gate, and wept about it after, and the god said
nothing against it."

**Tests.** `test_doctrine_conflicts`: each case's trigger and answers; one open per culture; the lapse rule. simrun:
`lawgiver` sees ≥ 2 conflict cards per history ≥ 6/8; `passive` (customs only) ≤ 1.

### 3.8 Messengers (v1)

**Rules.** The Prophet power (id `prophet`, key P, cost 120, Iron) is shown as **Messenger** and becomes a choice:
arming it on a town opens the MessengerChoice card with **three candidates** from that town (forked per town and
decade, so re-arming shows the same three). `characters::candidates(w, sid)` is a pure read returning would-be
figures; the answer creates the chosen `Figure` (kind Messenger) and calls `society::raiseProphet(w, sid,
RaiseOpts{ figure })`. From the Bronze age a devout culture may ask ("they seek a voice": no messenger for 250 years,
devotion ≥ 0.45), proposing the card at a lower cost (60). The messenger lives `prophetYears` (60; the Child 80),
travels as today, and **speaks**: every Exact or Parable word to that culture goes through them.

| archetype | offered | credibility | reach (rural / town / court / outcast) | gift | risk |
|---|---|---|---|---|---|
| **Shepherd** | any town | 0.5 | 0.9 / 0.6 / 0.3 / 0.5 | travels every 7 years (not 10); Parable fidelity +0.05 | slow to reach the court: decrees take 20 years to be kept in full |
| **Criminal** | town+ | 0.2 | 0.4 / 0.5 / 0.1 / 1.0 | **redeemed** if a miracle is witnessed in their town within 20 years: credibility +0.4 at once, and then the House's corruption −0.01 a year while they live | 20 % relapse in their first 30 years if not redeemed: discredited, a Breach of Honesty, and words through them fail |
| **Child** | any town | 0.35 | 0.5 / 0.5 / 0.2 / 0.5 | fidelity +0.15 (repeats exactly); credibility +0.01 a year; a Sign in their presence counts double | if their town is taken or struck: a martyr legend, love −0.1 toward whoever did it |
| **Queen** | the capital, or a ruled town+ | 0.7 | 0.4 / 0.7 / 1.0 / 0.1 | decrees kept in full at once in the capital (obedience +0.1 there); Exact fidelity +0.05; House authority +0.1 | pride: 25 % chance she uses the word for power (a WarningExploited-like card, or a decree of her own: Closed or Valour) |
| **The Unbelieved** | any town | 0.1 | 0.6 / 0.6 / 0.6 / 0.6 | each fulfilled warning or promise through them: credibility +0.3; at ≥ 0.8 **vindicated**: quoted for 1,000 years (a legend), and every later word to this culture +0.05 fidelity | nobody listens until then: words through them start at fidelity × 0.4 |

Credibility moves +0.05 per miracle in their town while they live (× 1.5 under Messengers), +0.3 per fulfilled
warning or promise, −0.2 per promise the god did not keep, −0.1 per wrath of the god on their town that they did not
foretell, and drifts 1 % a year toward the archetype's start. Their visits raise devotion by `prophetDevotion × (0.5 +
credibility)` (Society reads `characters::credibility`). Reach weights a word's `kept` by the culture's town tiers
(rural = camps and villages, town = towns, court = the capital, outcast = towns with famine or plague).

```cpp
// CharactersTune (messengers)
int candidates = 3; double askYears = 250, askDevotion = 0.45, askCost = 60;
double credMiracle = 0.05, credFulfilled = 0.3, credBroken = -0.2, credStruck = -0.1, credDrift = 0.01;
double criminalRedeem = 0.4, criminalRelapse = 0.2, criminalWindow = 30, childCredYear = 0.01, childYears = 80;
double queenObey = 0.1, queenPride = 0.25, unbelievedStart = 0.4, vindicated = 0.8, shepherdTravel = 7;
```

**Hooks.** Society (`raiseProphet` with `RaiseOpts::figure`; visits × credibility), Doctrine (fidelity), Faiths (the
Criminal against corruption, the Queen's authority), Legends (martyrs, vindication). **Name migration:** the HUD
calls the power Messenger; Chronicle templates replace "prophet" with "messenger", "seer" or "voice" (by culture
family), and the voice lint bans "prophet" in new lines (3.31).

**Player.** The MessengerChoice card: three portraits (concept job G2) with name, archetype, one line of life ("Ossa,
who keeps goats above Tel Asha and talks to them"), credibility in words ("few will believe her yet"), reach, the
gift and the risk; choose one, or cancel (refund). The culture panel shows the living messenger and their last word.

**Chronicle and myth.** `figure chosen` (w2): "The god chose Ossa, who kept goats above Tel Asha. The elders laughed
for a week, and then they listened." Redeemed: "Darro had stolen from half of Qe-Luun. When the magistrate's sick
child rose from bed at Darro's word, the other half began to listen too." Vindicated: "For forty years nobody
believed Ithel. Then the mountain woke exactly as Ithel had said, and they built Ithel a house of stone, too late to
live in."

**Look and sound.** A messenger marker at medium zoom: a small robed figure with a staff-light (Towns stand-in; an
accent per archetype: crook, child height, circlet, wrist cloth, grey hood); the existing prophet glow keyed to
credibility; the choir cue thinned by archetype (the Child: one voice).

**Tests.** `test_characters_messengers`: candidates deterministic per town and decade; the Queen only where allowed;
each gift and risk on synthetic worlds; credibility moves; save. simrun: the Unbelieved vindicated in ≥ 5/8 runs of
`warner` (3.17) speaking through them; the Criminal redeemed in ≥ 60 % of cases followed by a miracle within 20 years;
the Queen's pride in 15-35 % of Queens.

### 3.9 The Word and how clearly it is spoken (v1)

**Rules.** Power `speak` (group Word, key 5, click on a town = its culture; History, from the Stone age). `UseOpts`
gains `word { content (Decree, Repeal, Warning, Promise, Answer, Revelation), area, stance, clarity, channel, figure,
ref }`; the chooser fills it. One word per culture per 10 years (a god who talks all the time is not heard; `can`
says so).

| clarity | channel | cost | base fidelity | impression | clarity value |
|---|---|---|---|---|---|
| **Exact order** | a messenger (or the sky under Wonders, cost × 2) | 60 | 0.95 | Ruler +0.5 | 1 |
| **Parable** | a messenger | 50 | 0.70 | Parent +0.2 | 0.65 |
| **Dream** | one town's sleepers (spreads by reach) | 30 | 0.55 | Parent +0.2 | 0.4 |
| **Sign** | the sky over the whole culture | 90 | 0.35 | Mystery +0.3 | 0.15 |

**Fidelity** F = base × (0.6 + 0.4 × the messenger's credibility, or 0.5 without one) × (1 + 0.1 [Truth] + 0.05
[Discretion] + 0.1 [Writing shared] + 0.15 [Child]) × (1.2 for Sign and Dream near a marvel), at most 0.98. A forked
roll decides: with probability F the people hear what was meant. Otherwise the misread depends on the clarity:

| clarity | given a misread (forked) |
|---|---|
| Exact | **literal** (0.7): kept by the letter: strictness +0.2 and no bending (its conflict cases lose their "adapt" answer until repealed); **catastrophe** (0.3 × (1 − agency) × (0.5 + House authority)): the letter turned on someone (a purge: −10 % people in the town with most strangers, or a war of zeal on the least devout neighbour if v < 0); else literal |
| Parable | **wisdom** (0.6): heard differently but well (the neighbouring milder stance; agency +0.03, compassion +0.02); **sect** (0.4): two readings, a rival voice with following 0.1 (3.21) |
| Dream | **personal** (0.5): only the dreamer's town keeps it (kept × 0.3 elsewhere); **rival** (0.4): another dreamer claims a different meaning (rival voice, following 0.15); **catastrophe** (0.1 × (1 − agency)): an exodus (a share of the town leaves as settlers) |
| Sign | **read by the House** (0.5): the House picks the meaning (authority +0.1, the stricter stance); **rival** (0.3): a rival voice, following 0.2; **catastrophe** (0.2 × (1 − agency) × (1 + House corruption)): a burning (an academy town loses 30 progress-years via `civ::strikeKnowledge`, or the House's temple burns: strength −0.3) |

Each outcome emits `word { outcome }`. A catastrophe is told (w2) and remembered as Threat +0.2 (the people blame the
god's unclear voice). `heard` and `wisdom` raise the messenger's credibility by 0.05.

```cpp
// DoctrineTune (the word)
std::array<double, 4> wordCost{ 60, 50, 30, 90 }, fidelityBase{ 0.95, 0.70, 0.55, 0.35 };
double wordGap = 10, fidelityCap = 0.98, credFloor = 0.6, credK = 0.4, noMessengerCred = 0.5;
double literalShare = 0.7, parableWisdom = 0.6, dreamPersonal = 0.5, dreamRival = 0.4, signHouse = 0.5, signRival = 0.3;
double exactCat = 0.3, dreamCat = 0.1, signCat = 0.2, sectFollowing = 0.1, dreamFollowing = 0.15, signFollowing = 0.2;
double exodusShare = 0.3;
```

**Hooks.** Doctrine owns words; Faiths reads sect and rival outcomes; Mythos updates clarity and impressions; Civ
(`strikeKnowledge`; a new [civ] API `civ::exodus(w, sid, share)` that sends a settler party), Diplomacy
(`declareWar` for zeal).

**Player.** The `speak` chooser: "What will you say?" (Decree, Repeal, Warning, Promise, Answer) → "How clearly?"
(four cards with cost, fidelity in words: "almost all will hear it as you mean it" ... "few will hear what you
meant", and the risk line) → "Through whom?" (the messenger, a dreamer, the sky). Within 3 s the act ledger says
"Heard as meant", or "They heard something else: a new way may rise".

**Chronicle and myth.** `word` (w2). Sign read by the House: "Three red stars stood over Tel Asha all winter. The
priests said they meant that strangers must be driven out, and the strangers were driven out, and nobody asked the
stars." Parable wisdom: "The Umari never agreed what the story of the two wells meant, but they all began to share
their water, which may have been the point."

**Look and sound.** Exact: a still pillar of pale light over the messenger's town for 3 s and a held choir chord.
Parable: lanterns kindling in a ring through the town (night) or a warm wash (day), a lyre phrase. Dream: blue motes
drifting over the sleeping town's roofs, a soft high pad. Sign: **strange stars** (Sky: one to three coloured stars or
a halo that stay in that culture's sky for 20-40 years, visible from orbit above the limb), a low bell. A misread
plays the FX of its outcome (an exodus is a settler column; a burning is fire at the academy).

**Tests.** `test_doctrine_word`: costs, the 10-year gate, the fidelity formula, outcome shares over 10,000 forked rolls
within ±2 % of the table, catastrophes need low agency, none from Parable. simrun: `sign-only` founds ≥ 2 rival Ways
per history ≥ 5/8 and has ≥ 1 catastrophe ≥ 4/8; `parable-only` has 0 catastrophes ≥ 7/8 and ≥ 1 sect ≥ 5/8;
`exact-only` founds ≤ 1 rival Way ≥ 6/8.

### 3.10 Competing prayers (v1)

**Rules.** `PrayerKind` gains **Victory** (gameplay-v4 3.4: a capital at war with exhaustion < 0.4 prays to win;
answered by any wrath of the god on the enemy within 15 years) and **Sun** (a town with `wet` > 0.7 for 3 years, a
storm over it, or flooded territory prays for the rain to stop; answered by the god not raining there for 10 years
after the card, or by Raise Land under a flooded centre). The director pairs prayers that cannot both be granted:

| pair | condition | answers (cost) |
|---|---|---|
| **Rain / Sun** | a Rain and a Sun prayer within 0.2 rad | rain on the dry (the wet town's prayer fails) / hold the rain (the dry one fails) / a middle way: light rain on the dry fields only (Rain armed at half radius, cost × 1.5, both answered at half effect) / neither |
| **Victory / Victory** | two cultures at war, both capitals praying | stand with A (arms a wrath power on B's nearest town) / stand with B / the Omen of Peace (both prayers fail; both remember that the god held them apart) / neither |
| **Victory / Peace** | the aggressor prays Victory, the defender Peace | stand with the aggressor / the Omen (the defender is answered) / neither |
| **Heal / Victory** | plague in A's city while B prays Victory over A | heal A (B's prayer fails; B envies) / let the plague run (B is "answered") / neither |

Answering one side counts as `prayer:answered` for it (Society's answered effects) and as an unanswered prayer with
× 1.5 penalty and envy for the other ("the god chose them over us"). "Neither" lets both lapse at half the penalty,
Mystery +0.2 for both.

```cpp
// SocietyTune (new prayers) and CardsTune (pairs)
double sunWet = 0.7; int sunYears = 3; double victoryExhaustion = 0.4, victoryWindow = 15;
double competeAng = 0.2, competePenalty = 1.5, competeMiddleCost = 1.5, competeMiddleEffect = 0.5;
```

**Hooks.** Society (the prayer kinds; Victory answered in `witness` when a harmed settlement belongs to the enemy),
Cards (pairing from Society's read-only prayer list), Diplomacy (envy through the existing favour sums).

**Player.** The card shows two lanterns, their towns, peoples and stakes ("Ondo's wheat / Pell's flooded houses"), and
the answers; town labels gain the Victory (crossed spears) and Sun (a sun disc) prayer icons.

**Chronicle and myth.** One w2 line at the end: "Ondo prayed for rain and Pell for an end to it, in the same week, and
the god gave Ondo a little and Pell a little, and both towns complained, which was how they knew it was fair."
Victory: "The Thrask asked the god for victory over the Vai, and the sky fell on the Vai at Ruun. The Vai have not
forgotten whose sky it was."

**Look and sound.** Two prayer columns of light at night linked by a faint arc while the card is open; the answered
one blooms gold, the other dims; two reed tones that resolve or clash.

**Tests.** `test_society_prayers_v4`: Victory and Sun start and end; Victory answered by wrath on the enemy only;
`test_cards_compete`: pairing radius; the answers' answered / unanswered effects. simrun: `passive` opens ≥ 2
competing-prayer cards per history ≥ 6/8; `kingmaker` (always stands with the culture of the best verdict) ends with
that culture launching the Ark ≥ 5/8.

### 3.11 Judgement: deeds and hearts (v1)

**Rules.** A Judgement card opens for a severe breach (3.6), a figure's deed (a ruler razing a town, a Queen's pride,
a Criminal's relapse, a trial's fall: 3.18), or a pact the people broke (3.14). Each carries a **context** `{ who
(figure or town), deed, circumstance (famine, war, plague, fear, an order), heart (selfless, fearful, greedy,
zealous) }`. The deed and circumstance show at once; the heart stays hidden until the player pays **Look into the
heart** (20 Awe, one click, nothing witnessed). The heart is real (forked from the figure's hidden `heart` values, or
from the town's culture traits for a town), so judging blind is a gamble.

An answer has two parts: **by the deed** or **by the heart**, then a **sentence** (3.12). Doctrine records the first
in `judgedBy` (−0.25 for deed, +0.25 for heart, toward ±1, fading with half-life 300 years):

| doctrine | effect |
|---|---|
| judged by deeds (judgedBy ≤ −0.3) | obedience target +0.05; House authority growth × 1.2 (law needs judges); agency target −0.03; breaches are rarer (× 0.9) but each is told as graver (severity × 1.2) |
| judged by hearts (judgedBy ≥ +0.3) | agency target +0.04; compassion drift +0.01 a century; breach chance × 1.1 ("they meant well"); Forgive's love effect × 1.3 |

```cpp
// DoctrineTune (judgement)
double lookCost = 20, judgedStep = 0.25, judgedHalfLife = 300, judgedLine = 0.3;
double deedObey = 0.05, deedHouse = 1.2, deedAgency = -0.03, deedBreach = 0.9, heartAgency = 0.04, heartBreach = 1.1;
```

**Player.** The Judgement card: portrait or town, the deed in one line, the circumstance, the heart (or a veiled
"Look into the heart, 20"), the two toggles and the sentence buttons with costs. The budget's `cultureGap` keeps it
to about one per culture every 150-300 years.

**Chronicle and myth.** `judgement` (w2): "When the god judged Arvo, who had burned the granary of Pell to stop the
sickness, it looked past the fire into the man, and forgave him. After that the Vesh judged their own by what they
had meant, and argued much more about it." By deed: "... the god judged the burning and not the reason, and the Vesh
learned to judge that way too, and their judges grew grim and fair."

**Look and sound.** The heart reveal is a soft light passing through the portrait; the sentence plays its FX (3.12).

**Tests.** `test_doctrine_judgement`: the heart is hidden until paid and stable per card; `judgedBy` moves and fades;
doctrine effects reach Factors. simrun `judge-deeds` vs `judge-hearts`: deeds' obedience at year 2,500 ≥ hearts' +
0.1 ≥ 6/8; hearts' agency ≥ deeds' + 0.1 ≥ 6/8.

### 3.12 Sentences, mercy and the memory of survivors (v1)

**Rules.** The sentences of Judgement and PactBroken cards (and of other cards where named):

| sentence | cost | effect | survivors (70 years) |
|---|---|---|---|
| **Forgive** | 0 | the figure or town is spared; love +0.08 there; obedience −0.03 if it was a repeat; Parent +0.3; mercy +1 | love drift target +0.05 |
| **Warn** | 20 | a Dream or Sign over the town; obedience +0.05; nobody harmed; Ruler or Mystery +0.2 | obedience target +0.04 |
| **Remove the ruler** | 60 | only when a ruler did the deed: the ruler dies (`characters::fell`), obedience +0.1 in the culture, fear in the capital (love −0.04); Ruler +0.3 | obedience +0.06, love −0.02 |
| **Disaster** | the power's own | arms the chosen wrath power (lightning, quake, plague; meteor from Sail) on the town and flies there; when it lands it is **just** if the thing judged (the deed, or the heart) was wrong: Ruler +0.3; otherwise **unjust**: Threat +0.4 | just: obedience +0.08, love −0.04; unjust: love −0.1 and a Dispute proposal (3.19) |

`mercy` and `sternness` (fading sums, half-life 500 years) record the god's temper for the legacy (4) and the Keeper
of Scales (3.26). **Survivors:** a sentence sets the culture's survivor memory until `year + 70`; while it lasts the
targets in the last column apply and chronicle lines may say "the old ones who remembered ..."; when it ends the
memory becomes a legend (3.15) or, in v1, fades with one line.

```cpp
// DoctrineTune (sentences)
double warnCost = 20, removeCost = 60, survivorYears = 70, mercyHalfLife = 500;
double forgiveLove = 0.08, forgiveRepeat = -0.03, warnObey = 0.05, removeObey = 0.1, removeFear = -0.04;
double survForgive = 0.05, survWarn = 0.04, survRemove = 0.06, survJust = 0.08, survJustLove = -0.04, survUnjust = -0.1;
```

**Hooks.** Society (`nudge` for love, Factors for targets), Characters (`fell`), Powers (arming, the HUD's fly-to),
Mythos (impressions, survivor memory).

**Chronicle and myth.** At the sentence (w2), and once when the survivors are gone (w1): "The last of those who had
seen the god forgive Qe-Luun died in the 1,480th year, and after that it was only a story, and grew."

**Look and sound.** Forgive: warm light settling on the town. Warn: one dream or star. Remove: lightning framed on the
ruler's hall. Disaster: the power's own FX.

**Tests.** `test_doctrine_sentences`: costs, effects, just and unjust by the hidden heart, survivor memory begins and
ends at 70 years. simrun `merciful` (forgives all) ends Beloved ≥ 5/8; `stern` (punishes all, justly) ends Feared or
Parting ≥ 5/8 and its obedience ≥ merciful's + 0.1 ≥ 6/8.

### 3.13 The Rite: what worship means (v1 decree; v2 districts)

**Rules.** The Worship decree's stance is the **Rite** (or the people's custom: high piety → Prayer or Temples, high
compassion → Kindness, high curiosity or the Beautiful archetype → Art, the Generous archetype → World):

| rite | what they do | effects at kept 1 | what they build (v2 districts) | pressure |
|---|---|---|---|---|
| **Prayer** | they ask | prayer chance × 1.3; answered effects × 1.25; dependence target +0.1 | shrines in every village, prayer poles, lantern walks | never |
| **Temples** | they build for the god | temple devotion gate −0.05; Consecrate × 0.8 cost; aweS × 1.2; House strength growth × 1.5 | a temple quarter, processional ways, earlier Wonders | famine |
| **Kindness** | they serve each other | plague deaths × 0.8; love drift +0.03; relations +0.003 a year with contacts | almshouses, wells, guest-halls; hospitals from Industry | war |
| **Art** | they make beauty | build-edict Wonder in 20 years; research × 1.03; legends' vitality loss × 0.7 | theatres, carved stones, painted walls; later museums | ambition |
| **World** | they keep the land | farm conversion × 0.8; old growth kept as under Stewardship at half strength; CO₂ uptake × 1.1 on their land | sacred groves, terraces, river shrines | hunger |

`Factors` gains `co2Uptake` (Climate reads the land-weighted mean in `climate::yearly`; neutral 1).

**Hooks.** Factors (Society, Economy, Diplomacy, Climate), Legends (vitality), Towns (v2: district weights from
`mythos::rite(w, culture)`).

**Player.** The Altar slot of the Decree tablet, one line per rite ("Kindness: they will heal one another; they will
not fight as hard"); the culture panel says "They worship by kindness".

**Chronicle and myth.** `decree` for a rite (w2): "From then on the Vai worshipped the god by keeping guest-halls, and a
stranger could walk from the mountains to the sea and never once pay for supper."

**Look and sound.** v2 districts with a distinct silhouette per rite at 300 m; an ambient layer per rite near its
towns (bells, lyres, chisels, water and birds).

**Tests.** `test_doctrine_rite`: each rite's Factors; customs from traits. simrun: `kind` with Kindness ≤ 0.85 × the
plague deaths of `kind` with Prayer ≥ 6/8; Temples builds ≥ 1.2 × Prayer's Wonders ≥ 5/8; World keeps forest cover at
the Ark ≥ 1.15 × Prayer's ≥ 6/8.

### 3.14 Pacts across generations (v2)

**Rules.** A pact binds the god and one culture with one term each, sworn for a span (100-400 years) or "while the
people last". It is offered by the people (PactOffer card: after a great answered prayer, a great fear, or a
renewal) or by the god (`speak`, content Promise, with the terms). Terms:

| the god promises (godTerm) | kept when | broken when |
|---|---|---|
| **Rain in drought** | every rain prayer of theirs is answered within 10 years | a rain prayer lapses unanswered |
| **Shield from the sea** | no town of theirs is lost to the god's or the sea's water | a town drowned, waved or sea-taken |
| **Healing** | every plague in their cities is healed within 15 years | a city loses ≥ 30 % to plague |
| **Victory** | they win their wars (or the god stands with them, 3.10) | they lose a war |
| **No wrath** | the god never harms them | any wrath of the god on their towns |

| the people promise (peopleTerm) | kept when | broken when |
|---|---|---|
| **Keep a decree** (one named) | its `kept` ≥ 0.5 | a severe breach of it |
| **A temple in every town** | every town+ has a temple within 50 years | — (lapses) |
| **No war of their making** | they declare no war | they declare one |
| **The first fruits** (offering) | tithe: House wealth +, aweS × 1.2 | a famine-year refusal (forked by ambition) |
| **Carry the word** (Bearers v3) | their messengers travel to other peoples | — |

Each side's record `godKept` / `peopleKept` (fading, half-life 300) is checked yearly. **God-broke:** love −0.15 in
every town, Threat +0.6, credibility −0.2 of the messenger who carried it, a grievance legend that outlives the pact
by 600 years, and memory wrath +0.5 (so a promise broken costs at the ending). **People-broke:** a PactBroken card
(sentences 3.12, plus "end the pact"). A pact kept to its term gives love +0.1, devotion +0.05, Parent or Ruler
+0.3, and a legend. Pacts are inherited by daughter peoples at a schism ("their grandmothers swore it").

```cpp
// DoctrineTune (pacts)
double pactMinYears = 100, pactMaxYears = 400, pactHalfLife = 300, godBrokeLove = -0.15, godBrokeThreat = 0.6;
double godBrokeWrath = 0.5, grievanceYears = 600, keptLove = 0.1, keptDevotion = 0.05; int maxPacts = 24;
```

**Hooks.** Society (prayers checked against the godTerm; memory wrath through `society::remember(w, culture, grace,
wrath)`, a new [society] API that adds to the long memory like a witnessed deed), Diplomacy (no-war term through
Factors `warV` −0.3 while kept), Characters (credibility), Legends (pact legends), Faiths (offerings).

**Player.** PactOffer card: the people's request in their words and both terms; answers: accept / change terms (a
second chooser) / refuse. The Faith panel's **Pacts** list: each pact as a seal with both terms, years left, and a
two-sided kept bar. A warning chip "Your promise is at risk: the Vai pray for rain at Ondo" appears on the Now strip.

**Chronicle and myth.** `pact` sworn (w2): "At Tel Asha the Vai and their god made a pact: rain in every drought, and
no blood but at the gate. They cut it into a standing stone, so that nobody could change their mind quietly."
God-broke (w3): "The rain did not come to Ondo, though the pact was eighty years old and the stone still stood. The
Vai turned the stone face down, and their grandchildren were told why." Recalled (w1, centuries later): "... at the
foot of the stone the Vai had turned over in the year 912".

**Look and sound.** A standing-stone ring prop at the capital (Towns stand-in) with a gold band while kept, cracked
when broken; a seal glyph on the culture label; the god-broke stinger (a low falling interval).

**Tests.** `test_doctrine_pacts`: every term's kept and broken detection; inheritance at schism; god-broke penalties
reach memory. simrun `pactkeeper` (swears rain and healing pacts and keeps them) ends Beloved ≥ 6/8; `pactbreaker`
(swears and ignores) ends Feared or Parting ≥ 6/8, and its chronicle names a broken pact ≥ 300 years later ≥ 5/8.

### 3.15 Legends: stories that change (v2)

**Rules.** A **legend** is born when a culture witnesses a great deed (good magnitude × its share of the culture's
people ≥ 0.15), when a card's answer is dramatic (forgive, a just or unjust disaster, a martyr, a vindication, a pact
kept or broken), at a marvel, or at a renewal: at most 3 per culture per century, 12 living per culture. It records
the **truth** (what, where, when, how many harmed or saved: `truth` = a magnitude) and starts with `told = truth`,
`credit = God` (or Messenger / Nature for natural events misread), a **moral** (Mercy, Justice, Wrath, Wonder,
Warning, Sacrifice) and `vitality` 1.

Every 50 years (staggered by id) each legend is retold:

- **Fading:** vitality −0.08 (× 0.7 with the Art rite, × 0.8 with a strong House, × 0.5 once Writing is shared);
  +0.25 when a similar deed recurs; below 0.1 it is forgotten (`legend { forgotten }`, w1).
- **Growing:** `told ×= 1 + 0.12 × (1 − agency) × (1 + zeal of their messenger)`, capped at 10 × truth.
- **Credit shifts** (forked, at most once a century): to their Messenger (0.2 if one was present: a hero-cult), to a
  rival Way (rival following × 0.3), to Nature (0.15 × agency from the Industry age: "it was only weather"), to the
  Ancestors (0.1 after 1,000 years).
- **Moral drift:** toward the culture's doctrine (judgedBy, aspect, rite): a Wrath legend among a Parent-aspect people
  becomes Warning ("it was for our good").

**Effects:** devotion baseline +0.02 × Σ vitality of their God-credited legends (at most +0.08); a living legend of a
kind makes prayers of that kind 20 % likelier (they expect it again); Wrath and Warning legends add obedience +0.03
each (at most +0.09); Mercy legends love target +0.02 each (at most +0.06). Daughters inherit copies at a schism (with
one immediate drift). The **ending** shows the three strongest legends as told beside the truth.

```cpp
// LegendsTune
double bornMin = 0.15; int perCentury = 3, perCulture = 12, maxLegends = 72;
double retellYears = 50, fade = 0.08, fadeArt = 0.7, fadeHouse = 0.8, fadeWriting = 0.5, renew = 0.25, forget = 0.1;
double grow = 0.12, growCap = 10, creditMessenger = 0.2, creditRival = 0.3, creditNature = 0.15, creditAncestor = 0.1;
double devotionPer = 0.02, devotionCap = 0.08, expect = 1.2, obeyPer = 0.03, obeyCap = 0.09, lovePer = 0.02, loveCap = 0.06;
```

**Hooks.** Mythos folds legend effects into Factors; Society (prayer expectation through `prayerChance` per kind: a
small `std::array` in Factors); Chronicle (legend lines and the ending); Story later reads legends as tenets.

**Player.** The **Legends** tab of the Mythos book: each legend as an illuminated card (concept job G5): the told
version as the people tell it, and under it, small and grey, "What happened" with the truth; the credit and moral as
words; vitality as how widely it is told ("every child knows it" ... "a few old people remember"). No card.

**Chronicle and myth.** `legend born` (w1), `grew` (w1 at most once per legend per 300 years), `credited` (w2):
"By the 1,900th year the Vesh said that Ithel, not the god, had split the mountain to save them. Ithel had only
warned them, and the mountain had split by itself." The tale v4's "remembered above all" uses the strongest legends.

**Look and sound.** Legend sites get a small marker at medium zoom (a carved stone, Towns stand-in); the Legends tab
opens with a page-turn sound.

**Tests.** `test_legends`: birth threshold and caps; fade and renew; growth bounded; credit shifts only under their
conditions; inheritance; save. simrun: 15-40 legends per history ≥ 6/8; ≥ 60 % of the legends alive at the end tell
≥ 1.5 × the truth ≥ 6/8; ≥ 1 credit change per history ≥ 6/8; Art-rite worlds keep ≥ 1.3 × the legends alive at the
end ≥ 5/8.

### 3.16 Kinds of blessing (v2)

**Rules.** Power `bless` (group Word, key 6, click on a town; Bronze) with a choice:

| blessing | cost | effect (in the town, `townFactors`) | lasts |
|---|---|---|---|
| **Prosperity** | 80 | K × 1.25, growth r × 1.2, a market from village | 40 years |
| **A healer's gift** | 120 | a **Healer** figure is born there: every plague in the town and within 8 hops is cured at once (no cost, witnessed as the god's), plague deaths × 0.5 there | the healer's life (40-60 years) |
| **Endurance** | 100 | `harmTaken` × 0.5 (war capture needs × 2 strength, disaster and famine losses halved) | 60 years |

Consequences roll every 10 years while a blessing lasts (forked; weights by state):

| consequence | weight | effect | escalates to a card when |
|---|---|---|---|
| **Gratitude** | 1 + Parent | love +0.05, devotion +0.03 in the town | never |
| **Dependence** | 0.5 + dependence + Prayer rite | dependence +0.1 in the culture; prayers × 1.5 in the town | dependence ≥ 0.7 |
| **Envy** | relations pressure: towns of other cultures within contact | v −0.01 a year with those cultures (on top of the favour envy) | v ≤ −0.4 with one |
| **Conquest** | a neighbour with aggression ≥ 0.5 or ambition ≥ 0.6 | the blessed town becomes that neighbour's war goal: its war chance × 1.5 and its armies target the town first | war is declared |

The Blessing card: "The Oru covet blessed Tel Asha" → **Shield** (Endurance on the town at half cost) / **Share**
(the same blessing on a town of the envious people, full cost; envy ends) / **Withdraw** (the blessing ends now;
dependence −0.2, love −0.05: "the god took it back") / **Let it be**.

```cpp
// MythosTune (blessings)
double prosperityCost = 80, healerCost = 120, enduranceCost = 100, prosperityK = 1.25, prosperityR = 1.2;
double prosperityYears = 40, enduranceYears = 60, enduranceHarm = 0.5, healerRange = 8, healerDeath = 0.5;
double consequenceEvery = 10, envyV = -0.01, conquestWar = 1.5, dependCard = 0.7, envyCard = -0.4;
```

**Hooks.** Economy (`townFactors`: K, growth, harm), Civ (`civ::cure` by the healer: Mythos calls it), Diplomacy
(`warV` and the army target through a new [diplomacy] read `mythos::warGoal(w, culture)`), Characters (Healer).

**Player.** The `bless` chooser (three cards); blessed towns show a badge (sheaf / hand / stone) and its years left;
the Blessing card when a consequence escalates.

**Chronicle and myth.** `blessing` (w2): "The god made Tel Asha rich, and the Oru, who could see its granaries from
their hills, began to count them." Healer: "In the 640th year a child was born in Pell who could close a fever with
a hand on the brow. Sick people walked to Pell from three countries, and some of them stayed."

**Look and sound.** Prosperity: golden shimmer on the town's fields (Planet farm tint + FX motes); Healer: a soft
white-green light at the healer's house at night; Endurance: a faint stone-grey ring around the territory at medium
zoom. Each has a short stinger (chime, harp, low horn).

**Tests.** `test_mythos_blessings`: each effect and duration; consequence weights; the card's answers; save. simrun
`blessed-one` (blesses the same capital again and again): that town is attacked ≥ 2 × as often as others ≥ 5/8 and its
culture's dependence ≥ 0.6 ≥ 6/8; `healer` (one healer per plague-prone city) plague deaths ≤ 0.6 × passive's ≥ 6/8.

### 3.17 Omens and warnings (v2)

**Rules.** **Foresight:** `powers::natural` schedules its large events ahead (quakes, eruptions, droughts of 8+
years, city plagues, and tsunamis from natural quakes) 20-40 years before they strike (Dangerous +10), and reports
each through `omens::foresee(w, kind, cell, due, power, ang)` (the Disasters owner moves the roll earlier and keeps
the same rates; the event fires at `due`). The god's own planned judgement can be foretold too (`speak`, content
Warning, on a town: "I will strike Tel Asha in 30 years unless ..."; Omens records it with the god's term).

A Warning card opens for a foreseen event ≥ 25 years ahead that will hit a town (budgeted; at most one per culture per
300 years; the Keeper of Sleep sends small ones itself, 3.26):

| warning | cost | reach and belief |
|---|---|---|
| **Dreams** | 20 | the threatened town only; believed with devotion × (0.6 + 0.4 × messenger credibility) |
| **Strange stars** | 60 | the whole culture sees them; read by the House (its authority decides the meaning: 3.9's Sign rules) |
| **Failed harvests** | 40 | the town and its neighbours: K × 0.9 for 10 years (a harsh, unmistakable sign); belief +0.2 |
| **A messenger** | 0 extra (needs one) | the whole culture; belief = credibility |
| **Say nothing** | 0 | nobody knows |

The people's **response** (forked, weighted, once, 10 years after the warning):

| response | weight | effect |
|---|---|---|
| **Repentance** | belief × devotion × obedience | obedience +0.1; breaches cleared; devotion +0.05; for the god's own judgement, a card "They repented: spare them?" (spare: Parent +0.3; strike anyway: Threat +0.5) |
| **Preparation** | belief × (agency + curiosity) × 0.8 | when it strikes, losses × 0.4 and no destruction above 30 people (they left, built, stored) |
| **Exploitation** | belief × (corruption + ambition) × 0.6 | a ruler or priest seizes on it: House authority +0.15, corruption +0.1, or a neighbour attacks the frightened town (war chance × 2 for 20 years); a WarningExploited card (rebuke: authority −0.2, love −0.03 / allow / strike the exploiter: Disaster on the capital) |
| **Dismissal** | (1 − belief) + doubt | nothing; when it strikes, the warner's credibility +0.3 (the Unbelieved's vindication), and a "they did not listen" legend |

Every struck event resolves the omen (`omen { struck }`); a god-judgement the god spares is `averted`.

```cpp
// OmensTune
double leadMin = 20, leadMax = 40, cardMinLead = 25, cultureGap = 300; int maxOmens = 16;
double dreamCost = 20, starsCost = 60, harvestCost = 40, harvestK = 0.9, harvestYears = 10, harvestBelief = 0.2;
double respondAfter = 10, prepareLoss = 0.4, prepareKeep = 30, exploitAuthority = 0.15, exploitCorrupt = 0.1;
double exploitWar = 2, dismissCred = 0.3;
```

**Hooks.** Disasters (the foresight queue: `powers::natural` schedules and fires at `due`; with `phase` < 2 it fires
at once as today), Economy (`townFactors.harmTaken` while prepared; K for failed harvests), Characters (credibility),
Faiths (exploitation), Legends.

**Player.** The Warning card: the threat in the people's words ("The mountain above Tel Asha will wake in about 30
years") with a countdown, the four ways to warn and "say nothing"; later a toast with the response, and the omen's
result on the day.

**Chronicle and myth.** `omen warned` (w2): "For a winter the people of Tel Asha dreamed of the mountain opening.
Some moved their goats. Most moved nothing." Prepared (w2, when it strikes): "When the mountain opened, Tel Asha was
empty but for its dogs, which had been told nothing and ran anyway." Dismissed: "Ithel had said the sea would come,
and the sea came, and that is how the Vai learned to listen to Ithel."

**Look and sound.** Dreams: blue motes over the town at night (as 3.9); strange stars: Sky's coloured stars for the
warning's span; failed harvests: brown, thin fields (Planet farm tint) and fewer lit windows; a messenger: the marker
walks a circuit of the towns. The foreseen danger itself shows its precursor (gameplay-v4: a smoking vent, small
tremors, dry rivers).

**Tests.** `test_omens`: foresight keeps the natural rates (5,000-year counts within ±10 % of the table); every
response's weight and effect; repentance can avert a god-judgement; preparation losses. simrun `warner` (warns every
foreseen danger, cheapest way that fits) loses ≥ 40 % fewer people to foreseen disasters than `passive` ≥ 6/8; across 8
seeds all four responses occur; `passive` behaves as v3.1 with `phase` 1 (foresight off) bit for bit.

### 3.18 Trials of individuals (v2)

**Rules.** Power `test` (group Word, key 8, Iron) on a town with a notable figure (a ruler, a messenger, a healer, a
hero), or a Trial card when a figure stands out (a ruler with pride ≥ 0.6, a messenger with credibility ≥ 0.6). The
trial:

| test | cost | what the god does | passes when (forked, by the hidden heart) | if passed | if failed |
|---|---|---|---|---|---|
| **Power** | 80 | gives them victory or riches (a Prosperity blessing on their town, +0.2 battle for their wars) | integrity × 0.6 + humility × 0.4 | **exalted**: obedience +0.1, relations +0.05 with neighbours; a just reign (byname "the Just") | **tyrant**: corruption +0.2, aggression +0.2 for the reign, love −0.05 |
| **Hardship** | 60 | afflicts them (famine or plague in their town) | faith × 0.7 + integrity × 0.3 | **steadfast**: devotion +0.1 in the culture; a legend | **broken**: they curse the god (devotion −0.1 in the capital; a doubt legend) |
| **Temptation** | 40 | offers what a decree forbids | integrity × 0.8 + faith × 0.2 | **resisted**: obedience +0.08 (an example); a legend | **fallen**: a breach of that decree and a Judgement card about them |
| **An impossible charge** | 100 | charges them with what cannot be done (end a feud, carry a decree to another people, hold a doomed city) | (integrity + faith + humility) / 3 × 0.5 | **the charge done**: the thing happens (peace sworn, decree adopted elsewhere, the city holds); a great legend | **a tragic end**: they die trying; agency +0.05; a great legend all the same |

The result comes 20-40 years later (`trial { passed | failed }`) as a toast and a chronicle line, and with Look into
the heart (3.11) beforehand the player can see the odds.

```cpp
// CharactersTune (trials)
double trialPride = 0.6, trialCred = 0.6, trialMin = 20, trialMax = 40;
std::array<double, 4> trialCost{ 80, 60, 40, 100 };
double exaltObey = 0.1, tyrantCorrupt = 0.2, tyrantAggr = 0.2, steadfastDev = 0.1, brokenDev = -0.1, resistObey = 0.08;
double impossibleK = 0.5, tragicAgency = 0.05;
```

**Hooks.** Characters (heart, outcome), Mythos (`townFactors` for the gifts; Factors for the reign), Doctrine
(breach, Judgement), Diplomacy (`swearPeace` for a feud ended), Legends.

**Player.** The `test` chooser on a figure (four cards with the odds if the heart was looked into, else "you cannot
tell"); the Trial card when the world offers one; the figure's line in the culture panel ("Teodo, being tested with
power, 12 years").

**Chronicle and myth.** `trial` (w2): "The god gave Teodo every victory Teodo asked for, to see what would be done with
them. Teodo did what most do. The House of Teodo is remembered at Qe-Luun, but not fondly." Tragic: "Ilun was told to make the Vai and
the Thrask friends, and spent forty years at it, and died at a border feast with both peoples' cups in hand."

**Look and sound.** A thin ring of light around the tested figure's marker while the trial lasts; passed: a gold
flare; failed: the ring breaks into ash.

**Tests.** `test_characters_trials`: odds by heart; outcomes and timing; impossible charges succeed at about 0.5 ×
mean heart. simrun `tester` (tests every offered figure with power): tyrants in 30-60 % of power trials across 8
seeds; `tester-hardship` ends with lower devotion than `kind` ≥ 6/8.

### 3.19 When the people argue (v2)

**Rules.** A Dispute card opens when a culture with agency ≥ 0.4 has a grievance with the god:

| dispute | trigger |
|---|---|
| **Why did they die?** | 3+ unanswered prayers in a decade, or a plague or famine killing ≥ 15 % of a city |
| **Why were we struck?** | an unjust sentence (3.12) or wrath on a town that broke nothing |
| **This decree is too heavy** | a decree with strictness ≥ 0.7, burden × agency ≥ 0.35 |
| **Why have you hidden your face?** | a presence change to Unseen, or a withdrawal |
| **Why them and not us?** | favour envy (Diplomacy's favour gap ≥ 0.01 a year) |

Answers:

| answer | cost | effect |
|---|---|---|
| **Explain** | 30 (a word, clarity of the player's choice) | agency +0.05; love +0.05; Parent +0.2; Mystery −0.1; the grievance closes |
| **Silence** | 0 | Mystery +0.2; agency +0.03; love −0.03; at agency ≥ 0.6 devotion −0.02 (they stop asking) |
| **Change your mind** | 0 | the decree is softened or repealed, or the god promises (a pact proposal): love +0.08; Ruler −0.2; doctrine "the god can be moved": later disputes × 1.5 |
| **Demand obedience** | 20 | obedience +0.12 (× (1 − 0.4 courage)); love −0.06; agency −0.05; Ruler +0.3, Threat +0.1 |

```cpp
// MythosTune (disputes)
double disputeAgency = 0.4, disputeDeaths = 0.15, heavyStrict = 0.7, heavyBurden = 0.35;
double explainCost = 30, demandCost = 20, explainAgency = 0.05, explainLove = 0.05, silenceLove = -0.03;
double changeLove = 0.08, movedK = 1.5, demandObey = 0.12, demandLove = -0.06, demandAgency = -0.05;
```

**Player.** The card speaks in the people's voice ("The elders of Tel Asha ask why the children died in the plague
year") with the four answers; "Explain" opens the Word chooser's clarity row.

**Chronicle and myth.** `dispute` (w2): "The elders of Tel Asha asked the god why the children had died, and the god
answered them in a dream that none of them could repeat in the morning, but all of them woke less angry." Silence:
"... and the god said nothing, and the elders went home and began, for the first time, to argue with each other
instead."

**Look and sound.** An assembly of small figures at the town's square (Towns stand-in crowd) while the card is open;
answered by the chosen clarity's FX (3.9).

**Tests.** `test_mythos_disputes`: triggers; answers' effects; "moved" doctrine. simrun `explainer` (always explains)
vs `silent` (always silence) vs `tyrant` (always demands): explainer's love ≥ tyrant's + 0.15 ≥ 6/8; tyrant's
obedience ≥ explainer's + 0.1 ≥ 6/8; silent's final aspect Mystery ≥ 5/8.

### 3.20 The House of the God: temples, priests, offerings (v2)

**Rules.** Each culture's **House** (`houses[culture]`):

```
strength  → toward sat(0.6 × temple share + 0.3 × shrine share + 0.2 [Temples rite] + 0.1 [Tithe]) at 2 %/yr
authority += 0.01 × ([Ruler] + clarity + [Queen messenger] + [judged by deeds]) − 0.01 × agency      (0..1, yearly)
wealth    += 0.02 × strength × (1 + [Tithe] + [first-fruits pact]) − 0.01                            (0..1)
corruption+= 0.006 × wealth × authority × (1 + 0.5 ambition) × (1 − 0.3 [Truth]) − 0.004 × [reform]
             − 0.01 × [a redeemed Criminal messenger lives]; −0.002 when wealth < 0.2                 (0..1)
```

| part | effect |
|---|---|
| strength | aweS × (1 + 0.3 × strength); devotion drift toward baseline × (1 − 0.3 × strength) (it holds belief steady) |
| authority | obedience target +0.15 × authority; Signs are read by the House (3.9) |
| corruption | love drift target −0.04 × corruption; K × (1 − 0.05 × corruption) where Tithe holds (offerings press the poor); rival voices' chance × (1 + 2 × corruption); priests' breaches of Honesty and Wealth decrees (pressure + corruption) |

At corruption ≥ 0.5 the Institution card opens: "The priests of Tel Asha grow fat on offerings":

| answer | cost | effect |
|---|---|---|
| **Cleanse** | lightning (15) on the temple | the temple is lost (`civ::demoteTemple`, a new [civ] API); corruption −0.4, authority −0.3, wealth −0.5; fear (love −0.04); Ruler +0.2, Threat +0.1 |
| **Reform** | a Parable through a messenger (50) | corruption −0.02 a year for 20 years; needs credibility ≥ 0.3 |
| **Endorse** | 0 | authority +0.2, corruption +0.1, obedience +0.05; the House thanks the god (devotion +0.03) |
| **Abandon** | 0 | the House falls: strength, authority and wealth to 0, temples stay as buildings but count as shrines for Awe for 100 years; devotion −0.1; agency +0.15 |

```cpp
// FaithsTune (the House)
double strengthDrift = 0.02, authorityStep = 0.01, wealthStep = 0.02, wealthCost = 0.01, corruptK = 0.006;
double corruptReform = 0.004, corruptCriminal = 0.01, corruptLowWealth = 0.002, corruptCard = 0.5;
double houseAwe = 0.3, houseSteady = 0.3, houseObey = 0.15, corruptLove = -0.04, corruptK2 = 0.05, corruptRival = 2;
double cleanseCorrupt = -0.4, cleanseAuthority = -0.3, reformYears = 20, endorseAuthority = 0.2, abandonDevotion = -0.1;
```

**Hooks.** Society (Awe weights, drift steadiness, love target via Factors), Economy (K under Tithe), Civ
(`demoteTemple`), Doctrine (Signs read by the House, priests' breaches), Faiths (rival voices).

**Player.** The culture panel's **House** row: "A strong House: the priests are respected, and rich" with four small
bars; the Institution card; Towns show the House's state (below).

**Chronicle and myth.** `house` (w2): "The House of the god at Tel Asha had a gold roof by then, and a tax on salt, and
an opinion about everything." Cleansed (w3 when the temple burns): "The god burned the House at Tel Asha with the
priests' account books inside. The Vai rebuilt it smaller, out of wood, and kept the books in the open."

**Look and sound.** Towns: the temple quarter grows with strength; corruption gilds roofs (Flow texture F2) and adds
walls around the temple; Abandon leaves the temple dark. Audio: temple bells richer with strength, slightly detuned
with corruption.

**Tests.** `test_faiths_house`: each formula on synthetic cultures; card answers; save. simrun: `passive` reaches
corruption ≥ 0.5 in ≥ 1 culture by year 3,000 ≥ 4/8 (`emergent` on); `reformer` (answers every Institution card with
Reform) keeps mean corruption ≤ 0.5 × passive's ≥ 6/8; `endorser` has ≥ 1.5 × passive's rival Ways ≥ 5/8.

### 3.21 Rival voices and false messengers (v2)

**Rules.** A **rival voice** (a figure of kind RivalVoice) rises in a culture (Iron age+, at most one per culture, at
most one per two living cultures) from: a sect or rival outcome of the Word (3.9; starts at that following), the
House's corruption, or (as gameplay-v4 3.4) a large low-devotion or estranged town rolling 2 % a year. Its following
`f` grows by `0.02 × (1 − devotion) × (1 + 2 × corruption) × (Cunning 1.3 / Truth 0.7)` a year; towns within 0.3 rad
lose devotion 0.01 × f a year and gain `apart`. At f ≥ 0.5 with 2+ towns it **splits**: a daughter culture (existing
schism, `BirthReason::Schism`) following **the Way of {name}**: devotion baseline × 0.5 for 300 years, its image of
the god Threat +0.3 (they think the god is someone else's), and it remembers the god's deeds as before. The RivalVoice
card at f ≥ 0.15:

| answer | cost | effect |
|---|---|---|
| **Expose** | 60 (a Sign that contradicts them) | succeeds with P = 0.5 + 0.4 × (1 − f); success: f −0.3 and the voice discredited; failure, or any culture love < 0: a **martyr**: f +0.15, devotion −0.05 in the following towns |
| **Send your messenger** | the Messenger power (choose a candidate) | a contest every 10 years: f −= 0.1 × (your messenger's credibility − the voice's credibility); a messenger in the same town × 2 |
| **Trust the people** | 0 | each decade P(seen through) = 0.08 + 0.12 × agency + 0.05 [Truth]; seen through: f → 0 and agency +0.03 ("they worked it out"); otherwise it grows on |

The voice dies of age after 80 years (f halves unless a successor rises: 30 %).

```cpp
// FaithsTune (rival voices)
double rivalRoll = 0.02, rivalTownShare = 0.08, rivalGrow = 0.02, rivalNear = 0.3, rivalDevotion = 0.01;
double rivalSplit = 0.5, rivalCard = 0.15, exposeCost = 60, exposeBase = 0.5, exposeK = 0.4, martyrF = 0.15;
double contestEvery = 10, contestK = 0.1, trustBase = 0.08, trustAgency = 0.12, trustTruth = 0.05, rivalLife = 80;
double successor = 0.3, wayDevotion = 0.5, wayYears = 300; int maxRivals = 12;
```

**Hooks.** Society (devotion and apart through `nudge` and Factors `schism`; the split through Society's schism rule
with a reason flag `society::secede(w, towns, reason)`, a new [society] API), Characters (the figure), Names (the
Way's name from the voice's name).

**Player.** The RivalVoice card ("Ves of Pell says the god wants the Vai to stop paying the House"), a dark-robed crowd
marker in the voice's town with the following as a ring segment, the culture panel's "A rival voice: Ves, a third of
Pell listens".

**Chronicle and myth.** `rival rise` (w2): "Ves of Pell said that the god had spoken to Ves in a dream, and that the
god was tired of the House. Half of Pell thought this sounded likely." Split (w3): "The followers of Ves left the
Vai and called themselves the people of the Way of Ves. They kept the Vai's songs and changed the words." Seen through
(w2): "The Vai listened to Ves for twenty years and then, without anyone telling them to, stopped."

**Look and sound.** The crowd marker; a split shows the new culture's colour spreading from the voice's town; a
discordant low chant near the voice's town.

**Tests.** `test_faiths_rivals`: rise conditions, growth formula, the three answers' odds, martyrdom, split. simrun:
`passive` 1-4 rival voices per history ≥ 6/8; trust-the-people succeeds ≥ 70 % in cultures with agency ≥ 0.6 and
≤ 35 % below 0.3 (across 8 seeds); `exposer` (always expose) has ≥ 1 martyr ≥ 4/8.

### 3.22 Nature's claim: the Decree of the Land and old growth (v2)

**Rules.** Life adds one per-cell field, **`wild`** (u8, persisted, Raw): decades of unbroken forest cover (veg ≥ 0.6,
farm < 0.1, urban < 0.05), +1 per decade up to 255, reset to 0 when cleared, burned below 0.3 or farmed. A cell is
**old growth** at `wild` ≥ 30 (300 years). The Land decree:

| stance (strictness) | effect |
|---|---|
| **Dominion** "the land is given to us" (0.1) | farm conversion × 1.2; K × 1.05; old growth farmed like any land |
| **Stewardship** "take, and tend" (0.5) | old growth is not farmed unless pop > K × 1.05 (hunger); K × 0.97; farmland rotates (fert relaxes up 2 % faster) |
| **Equal standing** "the forest is not ours" (0.9) | old growth is never farmed and territories do not claim it (`claimAncient` false); K × 0.9; siteScore prefers open land; fauna and veg kept |

A LandClaim card opens when a town hungers (famine ≥ 1 or pop > K) and its best unfarmed cells are old growth while
the decree is Stewardship or Equal standing, or under Dominion when the forest is the last old growth of its biome on
that landmass:

| answer | cost | effect |
|---|---|---|
| **Allow** | 0 | the forest may be farmed (its cells `farmAncient` for this town); a legend of the Elder Wood dies; Land drifts toward Dominion (strictness −0.1) |
| **Refuse with a sign** | 30 (wolves, a storm) | the forest stays; the town's famine runs; love −0.04; wilderness +; Mystery or Threat +0.2 |
| **A grove** | 10 | half the old growth is cleared, a sacred grove (the oldest cells) kept for good (`wild` cells marked with flag 255 = never farmed); Stewardship +0.1 |

```cpp
// LifeTune (old growth) and DoctrineTune (land)
double wildVeg = 0.6, wildBurn = 0.3; int wildAncient = 30;
double dominionFarm = 1.2, dominionK = 1.05, stewardK = 0.97, stewardHunger = 1.05, equalK = 0.9;
double refuseCost = 30, groveCost = 10, refuseLove = -0.04;
```

**Hooks.** Life (`wild`), Economy (farm ranking skips cells by Factors; K), Civ (`claim` skips old growth under Equal
standing), Climate (more forest, more uptake: already in `climate::yearly` via cover), Legends (forests named).

**Player.** The Land slot on the Decree tablet; an **Old growth** overlay (Planet: deep green hatch over ancient
cells); the LandClaim card shows the forest and the hungry town side by side.

**Chronicle and myth.** `decree` Land (w2): "The god told the Vesh that the forest was not theirs, only next to them.
The Vesh built their fences on the outside of it, which surprised the forest." LandClaim allowed: "The Vai cut the
Elder Wood above Ondo for wheat, and ate, and their children never saw a tree that old again."

**Look and sound.** Old growth at medium zoom: taller, darker canopy (Nature reads `wild` for tree scale and age
variants); groves get a ring of standing stones; deep-forest ambience near old growth.

**Tests.** `test_life_wild`: counts, reset, persistence (save round trip); `test_doctrine_land`: each stance; cards.
simrun `gardener-myth` (Equal standing from the first shrine, refuses every claim, World rite): forest cover at the
Ark ≥ 1.3 × passive's ≥ 6/8 and Ark inside the window ≥ 6/8; `dominion` has the lowest old-growth count at the end
≥ 7/8.

### 3.23 Rulers (the supporting figure, v1)

Judgement ("remove the ruler"), trials and the Queen need a ruler. Characters keeps **one ruler figure per living
culture with a capital from the Bronze age** (story-v4 3.2 builds dynasties on top). A reign lasts 15-45 years
(forked); a successor is a new figure (heir 75 %, a new house when the capital fell, was taken or a ruler was removed).
Rulers carry `person` (pride, cunning, zeal, gentleness) and a hidden `heart`; pride ≥ 0.6 makes a Trial worth
offering; a cruel ruler's deeds (razing, a purge) feed Judgement cards. Effects in v1: none on the sim except through
the cards above (and story-v4 S10's optional teeth later). `characters::ruler(w, culture) → figure id`.

```cpp
// CharactersTune (rulers)
double reignMin = 15, reignMax = 45, heirShare = 0.75;
```

**Tests.** `test_characters_rulers`: exactly one living ruler per Bronze+ culture with a capital every year; reigns in
range; a removed ruler is succeeded the same year.

### 3.24 The Bearers: a people with a charge (v3)

**Rules.** Once per world age the god may choose one living culture as **the Bearers** (Faith panel; 300 Awe) and give
them a **charge**:

| charge | the gift | the work |
|---|---|---|
| **Carry the word** | devotion baseline +0.1; aweS × 1.3 | their messenger visits other peoples (a prophet unit to a foreign capital every 20 years): that people adopts one of the Bearers' decrees as a custom |
| **Keep the garden** | love target +0.05; the World rite at full strength | their land's old growth is never cleared; CO₂ uptake × 1.2 on it |
| **Keep the peace** | relations +0.005 a year with everyone in contact | an Omen of Peace cast on any war of their neighbours costs × 0.5; they may calm a war themselves once a century (as the Omen) |
| **Reach the stars** | research × 1.1 | the Ark builds × 1.15 faster in their capital |

**Superiority** `s` grows `0.004 × (1 + successes of the charge per century) × (1 − 0.6 × compassion) × (1.5 if
Closed hospitality)` a year (`successes`: decrees carried, peaces kept, wonders, an age reached first). Effects:
relations −0.02 × s a year with every culture in contact; at s ≥ 0.6 a crusade pull (`aggression` +0.2 × s toward the
least devout neighbour; the existing crusade rule); envy from others (the favour sums treat them as favoured: +0.5 × s
grace rate). The Bearers card at s ≥ 0.5 ("The Vai say they alone are the god's"):

| answer | effect |
|---|---|
| **Rebuke** | s −0.3; love −0.05; Ruler +0.2 |
| **Renew the charge** | s +0.05; the charge's gift × 1.2 for 50 years |
| **Release them** | they are no longer the Bearers: devotion −0.05, s decays 2 % a year |
| **Choose another** | a new people becomes the Bearers (300 Awe); the old ones' relations −0.2 with them (jealousy); a legend of the rejection |

```cpp
// FaithsTune (Bearers)
double bearersCost = 300, bearerDevotion = 0.1, bearerAwe = 1.3, carryEvery = 20, gardenUptake = 1.2;
double peaceOmen = 0.5, starsResearch = 1.1, starsArk = 1.15, supGrow = 0.004, supCompassion = 0.6, supClosed = 1.5;
double supRel = -0.02, supCrusade = 0.6, supCard = 0.5, rebuke = -0.3, renew = 0.05, jealousy = -0.2;
```

**Hooks.** Society (prophet units abroad through `raiseProphet`'s travel rule extended to foreign capitals; devotion,
Ark speed), Diplomacy (relations, crusade, Omen cost through Powers), Doctrine (customs adopted), Economy (research).

**Player.** Faith panel **Bearers** row: choose a people and a charge (four cards); their territory border gets a gold
sigil line; the Bearers card when pride grows.

**Chronicle and myth.** `bearers chosen` (w3): "The god set a charge on the Vai: to carry its word to every people.
The Vai were honoured, and then they were busy, and then they were insufferable." Rebuked: "The god reminded the Vai,
in front of everyone, that they had been chosen to carry the word, not to be it."

**Look and sound.** The gold border sigil; at s ≥ 0.6 the Bearers' banners in their towns grow larger (Towns); a
brass motif in the music while their people lead.

**Tests.** `test_faiths_bearers`: each charge's gift and work; superiority growth and effects; the card. simrun
`zealot` (chooses the leading people, renews every card): superiority ≥ 0.6 within 1,000 years of the choice ≥ 6/8 and
war share ≥ 1.2 × passive's ≥ 5/8; `humble-bearers` (rebukes every card) keeps s < 0.4 ≥ 6/8.

### 3.25 Revelations: knowledge given too early or in time (v3)

**Rules.** Power `reveal` (group Word, key 7; Bronze) on a culture, with a choice of knowledge and a clarity (3.9). One
of each kind per culture. Each kind has a **natural age**; given at or after it the people are **ready**, given before
it **too early** (+progress-years at once, but misuse is likelier).

| knowledge | natural age | cost | if shared (the good) | if hoarded | if weaponised |
|---|---|---|---|---|---|
| **Agriculture** (the seasons, the plough) | Bronze | 150 | K × 1.12 for every town; famines fewer; +30 progress-years if early | only the capital and its 3 nearest towns gain; House wealth +0.2 | famine as a weapon: in their wars the defender's K × 0.8 |
| **Writing** | Bronze | 150 | research × 1.08; decree fidelity +0.1 and decrees' `kept` decay halved; legends drift × 0.5 | priests' letters: House authority +0.2, corruption +0.1; agency −0.05 | propaganda: aggression +0.1, rival voices × 1.2 |
| **Medicine** | Iron | 200 | plague deaths × 0.6; heal prayers fewer | the rich are healed: plague deaths × 0.8 in the capital only; love −0.03 | poisons: battle × 1.1; once, a plague started in an enemy capital |
| **The shape of the world** (it is round; the stars are far suns) | Sail | 200 | agency +0.15; dependence −0.1; research × 1.05; naval reach × 1.2 | a secret school: a rival voice seed (following 0.1) | proof against the god: devotion −0.1 in the culture; Doubt (story) |
| **The truth of death** | any | 250 | sets the death belief to Known (3.27) | the House keeps it: authority +0.2 | used to command: obedience +0.15, love −0.08 |

The outcome is a forked roll 10 years after the gift, weighted: **shared** `(1 − corruption) × (0.5 + compassion) ×
(1 + 0.3 [Truth]) × (1 + 0.5 agency)`; **hoarded** `(corruption + authority) × (0.5 + ambition)`; **weaponised**
`(0.5 + aggression + 0.5 curiosity) × [at war or a rival within contact]`; × 2 on hoarded and weaponised when early.
A hoarded or weaponised outcome proposes the RevelationMisuse card: **Shame them** (lightning on the capital's House or
academy: the outcome flips to shared with P 0.5; Threat +0.2) / **Give it to all** (the same revelation to every other
culture at half cost) / **Let it be**. The Revelation card also arrives by itself when a culture with curiosity ≥ 0.6
"asks to understand" (budgeted).

```cpp
// DoctrineTune (revelations)
std::array<double, 5> revealCost{ 150, 150, 200, 200, 250 };
std::array<int, 5> naturalAge{ 1, 1, 2, 3, 0 };   // Age::BRONZE, BRONZE, IRON, SAIL, any
double earlyScience = 30, earlyMisuse = 2, outcomeAfter = 10, agriK = 1.12, writeResearch = 1.08, medicineDeath = 0.6;
double worldAgency = 0.15, worldDepend = -0.1, worldResearch = 1.05, shameFlip = 0.5;
```

**Hooks.** Economy (K, research, plague deaths through Factors), Doctrine (fidelity), Legends (drift), Faiths (House,
rival seed), Diplomacy (battle, defender K), Disasters (the one weaponised plague through `powers::start`-like
`civ::infect`), Mythos (agency, dependence).

**Player.** The `reveal` chooser: five cards, each marked "they are ready" or "too early: they may misuse it" with the
three outcomes in words; the culture panel's **Knowledge** row lists what was given and what they did with it.

**Chronicle and myth.** `revelation given` (w2): "The god showed the Umari, in a dream, how marks on clay could hold a
word after the speaker was gone. Within a century the priests had a word for everything and the farmers had a tax."
Weaponised: "The Thrask had been given the secret of fevers so that they might cure them. In the war with the Vai they
found the other use."

**Look and sound.** A slow light descending on the capital's academy or temple (FX `revelation`); hoarded: the light
stays in one building; weaponised: it turns red. A rising harp figure.

**Tests.** `test_doctrine_revelations`: costs, readiness, effects per outcome, outcome weights over many forks,
misuse card answers. simrun `revealer-early` (gives each knowledge one age early): Space ≥ 150 years before passive
≥ 5/8 and ≥ 2 hoarded or weaponised outcomes ≥ 5/8; `revealer-ready` (gives each at its natural age): ≤ 1 misuse ≥ 6/8.

### 3.26 The Keepers: guardians with domains (v3)

**Rules.** The god may appoint **Keepers** (Pantheon tab; 200 Awe each; upkeep 2 Awe a year each, through
`powers::pay` yearly). Slots: 1 from the Iron age, 3 from Sail, 5 from Industry. Each has a **domain**, a
**temper** set at appointment (Gentle or Stern), a **loyalty** 0..1 (starts 0.8) and a **will** 0..1 (starts 0.3,
grows 0.002 a year: Keepers grow more themselves with time). Names come from a divine root of the oldest culture's
language ("the Keeper of the Deep, whom the Vai call Asheru").

| Keeper | acts by itself each year (budgeted, witnessed as the god's at half faith) | Gentle | Stern |
|---|---|---|---|
| **of the Last Door** (death) | plague and famine deaths; the certainty of death beliefs | plague deaths × 0.85 everywhere; certainty grows toward Glimpsed | plague severity +0.1 in towns that broke a decree; certainty toward Known (fear) |
| **of the Deep** (oceans) | storms, waves, the fish | tsunami and storm losses × 0.6 near towns | strikes fleets of peoples at war (armies at sea −30 %); over-fished ports' fish × 0.8 |
| **of Scales** (justice) | small judgements (breaches with severity < 0.5) by the god's own doctrine (judgedBy, mercy, sternness) | forgives (as 3.12) | warns or strikes small (lightning near the town) |
| **of Sleep** (dreams) | warnings for foreseen dangers (3.17) by Dream, free | warns everyone | warns only obedient peoples (obedience ≥ 0.5) |
| **of a People** (one chosen culture) | answers that people's small prayers (rain, heal, sun) at half effect | answers all | answers only if they keep their decrees |

**Disagreement.** Each Keeper compares the god's recent acts with its temper (a Gentle Keeper of the Deep against a
meteor wave on towns; a Stern Keeper of Scales against forgiveness of a repeat breach; a Keeper of a People against
wrath on its people or a Victory card given to their enemy): loyalty −0.15 per clash, +0.01 a year otherwise
(toward 1). On a clash with loyalty < 0.7, a KeeperDispute card in the Keeper's voice ("The Keeper of the Deep asks
you to spare the coasts of the Vai"):

| answer | effect |
|---|---|
| **Grant** | the Keeper's way for 100 years in that domain (e.g. no wave of the god's own reaches towns: the god's sea impacts are softened × 0.6); loyalty +0.2 |
| **Refuse** | loyalty −0.1; Ruler +0.1 for the people who learn of it (a w1 line) |
| **Change its temper** | 100 Awe: Gentle ↔ Stern; loyalty −0.2; will −0.1 |
| **Dismiss** | the Keeper leaves; its people's legends of it remain; the slot is free after 100 years; Mystery +0.2 |

At loyalty < 0.2 a Keeper goes **its own way** for 50 years (`rogueUntil`): it acts by its temper ignoring the god's
doctrine (a Keeper of a People takes its people's side in every war: their Victory prayers answered by it with a small
lightning on the enemy, which their enemies blame on the god: Threat +0.1 for them). Chronicle: "the Keeper of the
Deep would not hear".

```cpp
// KeepersTune
double appointCost = 200, upkeep = 2, loyaltyStart = 0.8, willStart = 0.3, willGrow = 0.002;
std::array<int, 7> slots{ 0, 0, 1, 3, 5, 5, 5 };
double clash = -0.15, mend = 0.01, disputeBelow = 0.7, rogueBelow = 0.2, rogueYears = 50, grantYears = 100;
double grantLoyalty = 0.2, refuseLoyalty = -0.1, temperCost = 100, temperLoyalty = -0.2, dismissGap = 100;
double gentleDeath = 0.85, sternPlague = 0.1, gentleSea = 0.6, sternFleet = 0.3, actFaith = 0.5;
```

**Hooks.** Civ (plague via Factors `plagueDeath`; `civ::infect` for Stern), Disasters (wave and storm losses through
Factors `harmTaken` for coastal towns; fleets via `units::kill`-like losses through a [diplomacy] hook), Doctrine
(small judgements), Omens (dreams), Society (prayers answered by a Keeper: `society::answerPrayer(w, sid, scale)`, a new
[society] API), Cards (fewer judgement and warning cards: the Keepers absorb them).

**Player.** The **Pantheon** tab: up to five slots as abstract sigils (concept job G3), each with domain, temper,
loyalty ("loyal" ... "restless") and its last act; appoint, change temper, dismiss. The KeeperDispute card in the
Keeper's voice.

**Chronicle and myth.** `keeper appointed` (w2): "The Vai began to say that the god had set a keeper over the sea, a
patient one, who counted the boats out and counted them in." Rogue (w2): "That year the Keeper of the Thrask would not
be told. When the Vai marched north the sky over their army opened, and the Vai said the god had done it, and the god
had not."

**Look and sound.** No figures: each Keeper is a phenomenon. The Last Door: pale moth-like motes at dusk over dying
towns. The Deep: phosphorescent tides along protected coasts at night. Scales: a still white column over a judged
town for a second. Sleep: aurora-like veils over sleeping towns. A People: a banner-light of their colour over the
capital. A quiet theme per Keeper (one instrument each) in the music.

**Tests.** `test_keepers`: slots by age; upkeep; each domain's act by temper; clashes, cards and rogue periods;
save. simrun `pantheon` (appoints all five Gentle): cards opened ≤ 0.75 × `responder-myth`'s ≥ 6/8 (Scales and Sleep
absorb them) and losses to foreseen disasters ≤ 0.7 × passive's ≥ 6/8; `contrarian` (appoints Gentle and acts
cruelly): ≥ 1 rogue Keeper per history ≥ 6/8.

### 3.27 What death means (v3)

**Rules.** Each culture holds one **belief** (None, the Further Shore = an afterlife, the Turning = rebirth, the
Return = return from death, the Long Sleep = final rest) and a **certainty** (Uncertain 0.3, Glimpsed 0.6, Known 1.0).
It forms by itself after the first great dying (a plague or war killing ≥ 20 % of a city) from the aspect and traits
(Parent → Further Shore, Mystery → Turning, Threat → Long Sleep, Ruler → Further Shore with judgement; compassion
toward Turning, curiosity toward Long Sleep; forked), Uncertain; it may change at a renewal, a revelation (3.25: the
truth of death sets Known), a DeathQuestion card, or a Keeper of the Last Door. The DeathQuestion card ("The Vai ask
what lies past death", after a great dying, when the belief is None or Uncertain): choose a belief and the clarity
(Exact → Known, Parable or Dream → Glimpsed, Sign → Uncertain) or Silence (they decide themselves, Uncertain).

| belief (× certainty k) | strengths | problems |
|---|---|---|
| **the Further Shore** | defence × (1 + 0.1 k); obedience target +0.08 k (judgement waits) | aggression +0.05 k (they do not fear dying in war); martyrs (rival voices' martyr chance × (1 + k)) |
| **the Turning** | farm conversion × (1 − 0.15 k); fauna kept; love target +0.03 k | research × (1 − 0.03 k) (no hurry); war weariness gain × (1 − 0.2 k) (they fight on) |
| **the Return** | devotion baseline +0.05 k; legends' vitality +0.1 k | dependence target +0.1 k; after each great dying a Dispute ("why did they not come back?") with chance 0.5 k |
| **the Long Sleep** | compassion drift +0.02 k a century; research × (1 + 0.05 k) | devotion baseline −0.05 k; plague flight: settlers leave plague towns (exodus 10 % at plague > 0.4) |

```cpp
// DoctrineTune (death)
double dyingShare = 0.2; std::array<double, 3> certainty{ 0.3, 0.6, 1.0 };
double shoreDefence = 0.1, shoreObey = 0.08, shoreAggr = 0.05, turnFarm = 0.15, turnLove = 0.03, turnResearch = 0.03;
double turnWeary = 0.2, returnDevotion = 0.05, returnDepend = 0.1, returnDispute = 0.5, sleepCompassion = 0.02;
double sleepResearch = 0.05, sleepDevotion = -0.05, sleepFlight = 0.1, sleepFlightPlague = 0.4;
```

**Hooks.** Factors (Diplomacy defence, aggression, weariness; Economy farm, research; Society devotion), Civ
(`exodus` for flight), Faiths (martyrs), Mythos (disputes, dependence).

**Player.** The culture panel's **Death** line: "They believe the dead cross to the Further Shore (they are sure of
it)"; the DeathQuestion card.

**Chronicle and myth.** `belief` (w2): "After the fever years the Vesh began to say that the dead go home across a
water nobody living has seen, and that the god keeps a lamp on the far side. The ferrymen did well out of it."

**Look and sound.** Funerary customs at towns (Towns, v3: pyres and boats for the Further Shore, tree burials for the
Turning, tombs for the Return, plain fields of stones for the Long Sleep) at close zoom.

**Tests.** `test_doctrine_death`: formation, card, effects by certainty. simrun: across 8 passive seeds each belief
forms at least once; a Further Shore Known culture's defence advantage shows in battles won (≥ 1.1 × at equal odds,
synthetic test).

### 3.28 Coming of age: outgrowing the god (v3)

**Rules.** `selfReliance` grows `0.004 × agency × sat((age − Sail) / 3) × (1 − dependence) × (1 + 0.5 [the shape of
the world shared]) − 0.002 × dependence` a year (0..1). Effects: prayer chance × (1 − 0.6 × s); research × (1 + 0.1 ×
s); their Awe weight × (1 − 0.3 × s); neglect accrues × (1 − s) for them (a grown people does not feel abandoned). At
s ≥ 0.5 the ComingOfAge card ("The Vai have stopped asking"):

| answer | effect |
|---|---|
| **Bless their going** | s +0.2, dependence −0.2, love +0.05; Unneeded legacy weight +; `grown = year` |
| **Remind them** | a Sign: dependence +0.1, s −0.1, Mystery +0.2 |
| **Hold them close** | an Exact word and a miracle: dependence +0.2, agency −0.1, Ruler +0.2; Obedient legacy weight + |

```cpp
// MythosTune (coming of age)
double growK = 0.004, growDepend = 0.002, growPray = 0.6, growResearch = 0.1, growAwe = 0.3, growCard = 0.5;
double blessGoing = 0.2, remindDepend = 0.1, holdDepend = 0.2, holdAgency = -0.1;
```

**Player.** The card; the culture panel's Will line gains "They are coming of age".

**Chronicle and myth.** `grown` (w2): "In the 3,104th year the Vai held their harvest feast and nobody prayed for the
harvest. The old priest noticed, and said nothing, and then said a short, private thank-you of a different kind."

**Tests.** `test_mythos_grown`: growth formula, effects, the card. simrun `parent` (kind, parables, explains, shares
revelations in time, blesses their going): self-reliance ≥ 0.5 in the Ark culture ≥ 5/8 and legacy Unneeded ≥ 5/8.

### 3.29 Withdrawal: a century of silence (v3)

**Rules.** `mythos::withdraw(w, announced)` (the **Withdraw** button in the time controls, key 9; not in the Stone age,
not while an Ark is being built, at most once per world age): for 100 years the god does nothing.

- Powers refuse with reason `withdrawn` (a new `Reason`); cards open only as a digest; open cards lapse at once.
- Keepers act at half strength; Awe income accrues with no hoarding penalty (the god is away, not hoarding).
- Agency target +0.004 a year (3.4); neglect accrues × 0.5 if **announced** (a Sign or Dream before leaving: "I will be
  silent a hundred years"; 30 Awe), × 1 if not; Mystery +0.5.
- The client runs at 4x or faster (the player's speed if higher) with the palette sealed by a gold line.

At the end the **SilenceReport** card (not budgeted): per culture, what survived: each decree kept or lost (kept ≥
0.5), legends changed, the House's corruption then and now, new Ways, agency and dependence then and now. Answers:
**Return gently** (a Dream: love +0.03) / **Return in glory** (a great miracle: devotion +0.1, jaded +0.3, dependence
+0.1) / **Stay silent another century** (repeat; announced status kept).

```cpp
// MythosTune (withdrawal)
double withdrawYears = 100, announceCost = 30, withdrawAgency = 0.004, neglectAnnounced = 0.5, withdrawMystery = 0.5;
double keeperHalf = 0.5, gentleLove = 0.03, gloryDevotion = 0.1, gloryJaded = 0.3, gloryDepend = 0.1;
```

**Hooks.** Powers (`can` reason), Society (neglect accrual reads `mythos::neglectScale`), Cards (digest mode), Keepers,
Flow (speed, seal).

**Player.** The Withdraw button (confirm sheet: announced or silent, what will happen); the report card on return.

**Chronicle and myth.** `withdraw` (w2): "The god told the Vai it would be silent a hundred years, and was. The Vai
spent the first ten waiting, the next forty arguing, and the rest getting on with it." The report's lines are
chronicled once (w2): "When the god spoke again, the Vai still kept the Decree of Bread, had forgotten the Decree of
the Gate, and had a new way of praying that the god had never taught them."

**Look and sound.** The HUD dims to ink; the palette shows a gold seal; music thins to one held drone and the world's
own ambience; the year counter runs faster in gold.

**Tests.** `test_mythos_withdraw`: refusals, durations, announced neglect, the report's contents. simrun `withdrawer`
(kind, announces one century of silence per age from Bronze): not Forgotten ≥ 5/8 and agency at the Ark ≥ kind's +
0.15 ≥ 6/8; `withdrawer-silent` (no announcement): Forgotten or Parting ≥ 5/8.

### 3.30 Renewal: the Great Water and the Great Burning (v3)

**Rules.** Power `renew` (group Word, no key; Iron age+; once per world; costs all Awe and needs Awe ≥ 600). Confirm
sheet: the kind and the **Remnant** (up to 3 settlements; any settlement whose people *prepared* after a warning of
it is spared too).

- **The Great Water** (`DisasterType::Deluge`, Disasters): for 6 years the sea stands `deluge` 1.2 world units above
  the shore on every lowland (land with elev < sea + 1.2, not in a Remnant territory): settlements there are destroyed
  (cause `Deluge`), others lose 60 %; units at sea die; `wet` = 1 and fert +0.3 (silt) on flooded land. Climate's
  `seaLevel` is untouched (the Deluge is a disaster field drawn by Planet as raised water: `lava`-like per-cell depth in
  the disaster's `cells`, drawn by Planet's water).
- **The Great Burning** (`DisasterType::Burning`, Disasters + Life): a world drought (moist × 0.3 for 4 years) and
  ignitions on every continent: vegetation burns on ≥ 60 % of land (the 1,500-cell fire cap is lifted for it, in
  waves of 1,500), settlements outside the Remnant lose 70 % (destroyed below 3 people: cause `Burning`); fert +0.3
  (ash) afterwards.

Afterwards: every culture's science falls to the start of its current age (no age is lost); the Remnant towns get
pop ≥ 50 (Renewal guarantees it: the god kept them), their culture's survivor memory lasts **150 years** (love target
+0.1 if they were warned, −0.1 if not; obedience +0.2; Threat +1.0 for everyone, Ruler +0.5 for the Remnant), the
legend of the Water or the Burning is born with vitality 2 (it never fades below 0.5), and the Remnant card asks what
they are told (Explain / Silence / a Pact). Memory: the act is wrath (good −1) for every harmed culture.

**New game from this world.** `renewal::reseed(w, opts) → unique_ptr<World>` builds a fresh world for "Begin again
from this world" (ending screen and pause menu): the same seed, terrain (`elev`), and the current sea level as the new
datum; `veg` filled (Life.fill), the old `urban` kept as **ruins** (Life fades them as usual), the old world's name
kept as "the Old World" in `vars.str`, and up to 6 of the strongest old legends imported as **Old Stories** (credit
Ancestors, vitality 1, a new culture inherits them at waking); phase `People`, year 0. Save metadata records the
parent world's name, seed and ending. The old world is not changed.

```cpp
// RenewalTune
double minAwe = 600, delugeHeight = 1.2, delugeYears = 6, delugeLoss = 0.6, burnDryYears = 4, burnMoist = 0.3;
double burnShare = 0.6, burnLoss = 0.7, ashFert = 0.3, remnantPop = 50, remnantYears = 150, remnantObey = 0.2;
int maxRemnant = 3, oldStories = 6;
```

**Hooks.** Disasters (the two disaster types; the Deluge field), Life (the Burning's fire waves, `fill` at reseed),
Civ (`damage`, `destroy`, science reset via `civ::addScience` negative to the age floor), Planet (draw the Deluge's
water), Legends, Sim (`reseed` uses `sim::create` and `genesis::skip`-like steps; Save meta).

**Player.** The `renew` power with a two-step confirm (kind, Remnant picks on the map); the Remnant card; the ending
screen's "Begin again from this world" button and the pause menu's.

**Chronicle and myth.** `renewal` (w3): "Then the god let the sea walk over the lowlands, all of them, for six years.
Of the Vai only those at Tel Asha lived, because they had been told, and had built on the hill. Afterwards they did
not tell the story often, but they told it exactly." New world opening (w3): "Before the counting there had been
another world here, with other peoples on it. Their towns were hills now, and their stories had become the first
stories."

**Look and sound.** The Great Water: a slow-rising, storm-dark sea over the lowlands from orbit, rain bands, the
Remnant's towns lit on their hills; the Great Burning: fire fronts crossing continents, a brown sky, then ash plains
greening. A unique low score cue for each; silence afterwards.

**Tests.** `test_renewal`: the Remnant survives with ≥ 50 people (100 %), no age lost, the two disasters' bounds
(cells, cost per tick ≤ 4 ms), the reseeded world's terrain equals the old one (`elev` hash), Old Stories imported;
save mid-Deluge. simrun `renewer` (renews at the Iron age with one Remnant): never loses all its people 8/8; the
renewal legend is alive at the end ≥ 7/8; an Ark inside 4,500 years ≥ 5/8 (or the miss is reported); `reseed` +
passive meets the SPEC-SIM 4 Ark window ≥ 5/6.

### 3.31 Chronicle, myth output and the voice lint (all phases)

- **Entry types** (Chronicle's vocabulary, new): `card` (resolutions), `presence`, `aspect`, `decree`, `custom`,
  `breach`, `word`, `pact`, `figure`, `trial`, `judgement`, `sentence`, `survivors`, `legend`, `blessing`, `omen`,
  `dispute`, `house`, `rival`, `way`, `bearers`, `revelation`, `keeper`, `belief`, `grown`, `withdraw`, `renewal`.
  Weights as given per system; card resolutions are w2 through the **story** bucket; most v2-v3 drift lines are w1
  through the digest. Budget target: mythos adds at most **1.2 notable entries per real minute at 1x** and never more
  than one card-resolution entry per card.
- **Templates:** at least 3 variants per entry type and per stance or kind (writing job W3), in the approved voice
  (11 rules). Placeholders add `{messenger}`, `{decree}` ("the Decree of the Gate"), `{stance_words}` ("feed the
  stranger at the gate"), `{pact}`, `{legend_told}`, `{legend_truth}`, `{keeper}`, `{way}`, `{belief}`, `{charge}`.
- **Lint** (`test_chronicle_mythos`, 8 seeds × 6 policies): no unfilled `{...}`; none of the game terms of voice rule 6
  plus "agency", "obedience", "dependence", "credibility", "fidelity", "aspect", "card"; none of the banned words of
  0.2; no gendered pronoun within six words after a figure placeholder; weight 1-2 entries ≤ 50 words; no template body
  used > 10 times per history.
- **The tale** gains a "What they believed" paragraph (decrees kept at the end, the death belief, the strongest legend
  told beside the truth) and the Legacy paragraph (section 4).

---

## 4. The legacy ending: what it felt like to live under your care

The ending keeps its type and **mood** (SPEC-SIM 3.12: beloved, feared, forgotten, parting; silence quiet or ash): the
v3 targets stand. Mythos adds a second verdict, the **Legacy**, about how the people lived, computed in
`mythos::legacy(w)` for the Ark culture (or everyone for a silence) and stored in `ending.legacy`:

| legacy | score (0..1) |
|---|---|
| **An obedient empire** | 0.45 × obedience + 0.25 × House authority + 0.2 × Ruler share of the image + 0.1 × sat(decrees / 4); × 0.5 with fewer than 2 decrees |
| **A compassionate civilisation** | 0.35 × compassion + 0.25 × care (Shelter 1, Kin 0.5; Kindness rite +0.5; healers) + 0.25 × (1 − famine and plague deaths / peak people, ×4) + 0.15 × sat(love + 0.5) |
| **A terrified world** | 0.6 × sat(−verdict / 0.5) + 0.4 × Threat share of the image |
| **A thriving wilderness** | 0.45 × sat(forest cover now / at year 0) + 0.3 × Land (Equal 1, Stewardship 0.6, none 0.3, Dominion 0) + 0.25 × veg cover; × 1.3 for a silence ending (the world without them thrives) |
| **A humanity that no longer needs you** | 0.5 × selfReliance + 0.3 × agency + 0.2 × (1 − dependence); × 0.5 unless the god is remembered (memory `remembered`): a forgotten god was never outgrown |

The highest score wins (ties: Unneeded, Compassionate, Wilderness, Obedient, Terrified). v1 builds the verdict and one
paragraph per legacy × mood (30, writing job W6); v3 adds **Lives**: at each act close (world age change) Characters
samples one ordinary life of the leading culture (a farmer, a ferryman, a priest's daughter), built from the state of
that age (the decrees kept, famine or plenty, the rite, the House, the death belief, a legend everyone told), stored
as a small record (`Figure` kind Life, 7 at most), and the ending shows them as an album: one short paragraph each
("In the Age of Iron, Hesse, who mended nets at Pell, kept the Decree of the Gate for sixty years without once
meeting a stranger, and was buried facing the Further Shore."; generated names have no known gender, so the templates
use no pronouns). The planet card prints the legacy title under the mood.

**Tests.** `test_mythos_legacy`: each score on synthetic worlds; ties; silence boost; remembered gate. simrun targets
(8 seeds): `lawgiver` → Obedient ≥ 5/8; `gentle` (Kindness, Shelter, parables, forgives) → Compassionate ≥ 5/8;
`cruel` → Terrified ≥ 6/8; `gardener-myth` → Wilderness ≥ 5/8; `parent` → Unneeded ≥ 5/8; `passive` → never
Terrified (8/8) and never Unneeded ≥ 7/8; every legacy is the most common legacy of some policy. The mood targets of
SPEC-SIM 4 and the v3 / v3.1 tables still pass.

---

## 5. Balance, simrun and the scripted gods

### 5.1 Guardrails (binding for every phase)

1. **`phase` 0 is v3.1 bit for bit** (`simrun --hash` equal to the pre-mythos build on 8 seeds × passive, kind,
   cruel, chaotic). Every mythos module is a no-op and every Factors record neutral.
2. **`phase` ≥ 1 with `emergent` off:** a god that never answers a card and never uses a Word power leaves the sim
   fingerprint (year, tick, rng state, live towns, people, sea, co2, cultures, units, relations) identical to v3.1.
   Cards still open and lapse (they are records), but lapse effects, customs, aspect effects, agency on research and
   schisms, House corruption and rival voices are `emergent`-gated. Every existing policy (which ignores cards) keeps
   its v3.1 tables.
3. **Turning on `emergent`** (balance phase B of each wave) is done one rule family at a time with before/after tables
   on 8 seeds × every policy (`docs/handoff/mythos-v<N>/`): customs; aspect; agency (research, schism); dependence;
   House; rival voices; legends; foresight. Expected movements: Ark years shift by agency's research term (retune
   `EconomyTune::ageNeed` if the passive Ark window moves), one more schism per history, slightly fewer famines with
   customs. **Every SPEC-SIM section 4 target, every v3 design-review target and every v3.1 target must pass** after
   each family, on 8 seeds (the four already-failing design-review targets stay as they are or improve).
4. **Awe.** Mythos acts are a new sink (Word 30-90, Bless 80-120, Look 20, Keepers' upkeep): `prayers` and `passive`
   at the cap should fall, never rise; an active mythos god (`responder-myth`) sits below 90 % of aweMax in ≥ 70 % of
   years. aweMax rules are unchanged.
5. **Cost:** mean tick ≤ 2.5 ms, any tick ≤ 30 ms (the Burning ≤ 4 ms per tick); `mythos::yearly` ≤ 0.35 ms mean.
6. **Determinism:** `--hash` three times per seed (8 seeds × passive, kind, responder-myth, lawgiver, sign-only,
   renewer); `--saveload` at a card opening, mid-trial, mid-withdrawal and mid-Deluge replays to the same hash.

### 5.2 simrun additions (`Tools/simrun/*`)

- `--mythos-phase 0|1|2|3` (default: the build's Balance value), `--emergent on|off`, `--archetype <name>`,
  `--shape <preset>` (e.g. `curious-brave`, `kind-meek`), `--presence wonders|messengers|unseen`.
- `--cards lapse|first|random|policy` (how cards are answered when the policy has no rule for a kind; default lapse).
- `--mythos` prints, per run and per age: cards opened per real minute at 1x and with auto-pace v2, the gap
  distribution (median, 90th percentile, max), open-at-once maximum, answers by kind; per culture over time: aspect,
  agency, obedience, dependence, self-reliance; decrees held and kept; breaches; words by clarity and outcomes; pacts
  kept and broken; legends alive, told / truth, credit changes; blessings and consequences; omens and responses; trials;
  disputes; the House (corruption max); rival voices and Ways; Bearers' superiority; revelations by outcome; Keepers'
  acts and rogue years; death beliefs; legacy scores and the legacy; the mythos share of notable entries per minute.
- `--summary` gains a **mythos** table with every target of sections 2.2, 3 and 4, pass or fail per policy.

### 5.3 Scripted gods (`Tools/simrun/Policies.cpp`)

| policy | plays | proves |
|---|---|---|
| `responder-myth` | kind, and answers every card with its first answer | cadence; Awe sink; nothing breaks when everything is answered |
| `random-myth` | chaotic, and answers every card at random | robustness |
| `lawgiver` / `valour` | the decree sets of 3.6, Exact through a messenger | decrees, Obedient legacy |
| `exact-only` / `parable-only` / `sign-only` | words only at one clarity | 3.4, 3.9 |
| `unseen-kind` / `msg-kind` | kind under one presence | 3.2 |
| `judge-deeds` / `judge-hearts` / `merciful` / `stern` | one judging habit | 3.11, 3.12 |
| `kingmaker` | stands with the best-verdict culture in every competing prayer | 3.10 |
| `pactkeeper` / `pactbreaker` | swears pacts and keeps / ignores them | 3.14 |
| `blessed-one` / `healer` | blessing habits | 3.16 |
| `warner` | warns every foreseen danger | 3.17 |
| `tester` / `tester-hardship` | trials | 3.18 |
| `explainer` / `silent` / `tyrant` | one dispute habit | 3.19 |
| `reformer` / `endorser` / `exposer` | House and rival habits | 3.20, 3.21 |
| `gardener-myth` / `dominion` | the Land decree | 3.22, legacy |
| `zealot` / `humble-bearers` | the Bearers | 3.24 |
| `revealer-early` / `revealer-ready` | revelations | 3.25 |
| `pantheon` / `contrarian` | Keepers | 3.26 |
| `parent` / `gentle` | the kind god of this spec (parables, explain, forgive, reveal in time, bless their going) | legacy Unneeded / Compassionate |
| `withdrawer` / `withdrawer-silent` | a century of silence per age | 3.29 |
| `renewer` | the Great Water at the Iron age | 3.30 |

Existing policies (passive, kind, cruel, chaotic, doom, curious, blessonly, prayers, repent, sculptor, late-kind,
late-cruel, inspire-spam, hoarder, builder, atomic-kind, atomic-cruel, peacemaker, favourite, scourge) are unchanged
and answer no card (`--cards lapse`).

### 5.4 Cadence targets (section 2.2), consolidated

On 8 seeds with `responder-myth` and with `passive` (cards open and lapse): 15-28 cards per history; median gap
150-260 years; ≥ 90 % of gaps ≤ 420 years; no two openings within 70 years; never more than 2 open; every age of
≥ 200 years opens ≥ 1; with auto-pace v2 modelled (1x while a card is open, else 2x as gameplay-v4 3.9), at least one
card opening in ≥ 80 % of 3-minute windows and none in < 1 minute of another; mythos adds ≤ 1.2 notable entries per
real minute at 1x.

---

## 6. The client

### 6.1 `Game/UI/Mythos/*` (new client module, owner track CM1)

| file | what |
|---|---|
| `DmMythosModel.h/.cpp` | `FDmMythosContext`: built from the world on mythos events and at 2 Hz: open cards (`FDmCardView`: title, context lines, the heart if revealed, answers with cost and effect words, countdown in years and real seconds at the current speed, lapse words), per-culture faith views (aspect glyph, will words and bars with causes, decrees, pacts, House, rival voice, Bearers, knowledge, death belief, legends), the Pantheon, presence. Actions: `AnswerCard`, `LookIntoHeart`, `Speak`, `SetPresence`, `Withdraw`, `AppointKeeper`, `ChooseBearers` (all through `UDmSimSubsystem`). Read-only sim calls only. |
| `DmMythosCopy.h/.cpp` | every player-facing string of cards, choosers and panels as data tables (from writing job W2), plain language, no internal ids |
| `SDmMythosPanel.h/.cpp` | the card stack (at most 2 cards + "N waiting" when Situations share it), slide-in, chime once |
| `SDmMythosCard.h/.cpp` | one card: icon, title, context, heart veil, answers, countdown ring, lapse link; the MessengerChoice and ShapePeople layouts |
| `SDmWordChooser.h/.cpp` | the chooser beside the armed chip for `speak`, `bless`, `test`, `reveal`, `renew` (steps: what → how clearly → through whom → confirm), with costs and risk lines |
| `SDmDecreeTablet.h/.cpp` | the seven stone slots |
| `SDmFaithTab.h/.cpp` | the culture panel's Faith tab content |
| `SDmMythosBook.h/.cpp` | the Mythos book (key `` ` ``): Legends, Pacts, Pantheon, Beliefs tabs; legends as illuminated cards (told above, truth below) |
| `SDmLegacyAlbum.h/.cpp` | the ending's Legacy page and the Lives album (mounted by Screens) |
| `DmMythosScenarios.cpp` | scenarios `mythos-card-<kind>` (one per card kind on a seeded world), `mythos-word`, `mythos-tablet`, `mythos-faith`, `mythos-book`, `mythos-legacy`, `mythos-narrow` (390 × 844), `mythos-ui-scale` (1280 × 720 at uiScale 1.5) |

Style: gold on ink, Cinzel titles, Cormorant Garamond body, Inter numbers (`docs/concept/ui/`); no colour-only signals;
touch targets ≥ 40 px; rebuild only on events or at 2 Hz (no per-frame allocation); `HUD.update` budget unchanged.

### 6.2 Changes in other client modules (one owner each)

| module (owner) | change |
|---|---|
| Core bridge (C0) | `OnSimEvent` routes the 22 new events; `UDmSimSubsystem` gains `AnswerCard`, `LookIntoHeart`, `Speak(FDmWordOpts)`, `SetPresence`, `Withdraw`, `AppointKeeper`, `ChooseBearers`, `NewFromThisWorld`; read accessors for `MythosState` |
| HUD (C7) | palette group **Word**; the Mythos panel slot under the Now strip; the culture panel's Faith tab host; the armed chip hosts `SDmWordChooser`; toasts for `card` (one per 20 s); the eye button's open-card count; the Messenger rename; Withdraw button and key 9 in the time controls; new prayer icons |
| Input (C3) | keys 5-8 arm the Word powers (they read `def.key`), 9 withdraws, `` ` `` opens the book; settlement-targeted Word powers highlight the town under the pointer |
| Screens (C8) | setup row "What kind of world?"; the ending's Legacy page (hosts the album); "Begin again from this world" (v3) on the ending and pause screens; settings "Pause when the world asks" |
| Flow | auto-pace: ≤ 1x while a card is open; pause-on-card setting; the playthrough god (`DmGodPolicy`) answers cards like `responder-myth`; tutorial beat: the Presence and ShapePeople cards at the waking; scenario `mythos-first-ten` (the first 10 minutes with the newcomer bot) |
| FX (C6) | `decree` glyph, `breach` ripple, the four word effects (pillar, lanterns, dream motes, house-read sign), blessings (shimmer, healer light, endurance ring), the trial ring, the omen visuals that are not Sky's, the Keepers' phenomena, the Deluge and Burning, the withdrawal dim |
| Sky (C2) | **strange stars** (1-3 coloured stars or a halo pinned to a culture's sky for 20-40 years), the storm ring of the Strange archetype, the Deluge's storm sky, the Burning's brown sky |
| Planet (C1) | overlays: aspect, old growth; the Deluge water (per-cell depth from the disaster); failed-harvest and prosperity field tints; marvel glow decals |
| Towns (C5) | markers: messenger (by archetype), rival voice crowd, assembly crowd, legend stones, standing-stone pact ring; rite districts (v2); the House's temple quarter and gilding by strength and corruption; the Bearers' banners; funerary customs (v3); all through the mesh-factory interface (stand-ins now, assets later) |
| Nature (C4) | old-growth trees: taller, darker, older variants where `wild` ≥ 30 |
| Audio (C9) | one-shots `card`, `card-judge`, `card-prayer`, `decree`, `breach`, `word-exact`, `word-parable`, `word-dream`, `word-sign`, `pact`, `pact-broken`, `blessing`, `omen`, `trial`, `keeper`, `renewal`, `withdraw`, `marvel`; music: aspect colour, the withdrawal drone, the Keepers' single-instrument themes, the renewal cues; all inside the existing limiter, LUFS and sub-bass rules (`docs/content/sound-direction.md`) |

### 6.3 Client acceptance (every phase)

- Headless model tests (`FDmMythosContext` from a seeded world, no Slate): every card kind produces a view with ≥ 2
  answers, finite numbers and no internal ids; countdown seconds = years / (2 × speed); one toast per 20 s.
- Scenario shots for every scenario in 6.1 and every visual of 6.2, judged by eye against the concept boards (section
  8); text fits at 1280 × 720, 1920 × 1080 and 390 × 844.
- 60 fps at 1080p High on the RTX 3060 with two cards open, the book open and a Sign in the sky.
- The `mythos-first-ten` scenario: the Presence and ShapePeople cards appear at the waking, the first budgeted card
  by minute 3 of history, never two prompts at once, and the world reaches Bronze.

---

## 7. Interaction map (where the player meets each system, and how often)

| system | entry | panel / card / power | how often at 1x |
|---|---|---|---|
| Archetype | setup | Setup screen row | once |
| Presence | waking; Faith panel | Presence card; stance row | once, then ≤ once an age |
| First people | waking | ShapePeople card | once |
| Decrees | Word power `speak`; FirstDecree card | Decree tablet | first within ~2 minutes; then at will (one word per culture per 10 years) |
| Decree conflicts | the world | DecreeConflict card | ~1 per 10-15 minutes per decree-rich culture |
| Messengers | `prophet` shown as Messenger; "seek a voice" card | MessengerChoice card | ~1 per 8-12 minutes from Iron |
| The Word | `speak` (5) | Word chooser | at will |
| Competing prayers | the world | CompetingPrayers card | ~2-4 per history |
| Judgement and sentences | the world | Judgement card | ~2-5 per history |
| Rite | `speak` (Altar slot) | Decree tablet | once or twice per culture |
| Pacts | the world asks; `speak` Promise | PactOffer / PactBroken cards; Pacts tab | ~1-3 per history |
| Legends | — | Mythos book, Legends tab | read at will; no card |
| Blessings | `bless` (6) | chooser; Blessing card | at will; consequence card ~1-2 per history |
| Warnings | the world (foresight) | Warning card | ~3-6 per history |
| Trials | `test` (8); Trial card | chooser; Trial card | ~1-3 per history |
| Disputes | the world | Dispute card | ~2-4 per history (more with high agency) |
| The House | the world | Institution card; Faith tab | ~1-2 per history |
| Rival voices | the world | RivalVoice card | ~1-3 per history |
| Land | `speak` (Land slot); the world | LandClaim card; old-growth overlay | ~1-2 per history |
| Bearers | Faith panel | Bearers row and card | ≤ once an age |
| Revelations | `reveal` (7); the world asks | chooser; Revelation / Misuse cards | ~2-5 per history |
| Keepers | Pantheon tab | KeeperDispute card | ~1-3 per history from Iron |
| Death | the world; revelation | DeathQuestion card | ~1 per culture |
| Coming of age | the world | ComingOfAge card | 0-2 per history (late) |
| Withdrawal | time controls (9) | confirm sheet; SilenceReport card | ≤ once an age |
| Renewal | `renew`; ending / pause menu | confirm sheet; Remnant card; Begin again | ≤ once a world |
| Legacy | ending | Legacy page and album | once |

Summed with the budget: 15-28 budgeted cards plus the player's own Word acts and the existing powers and prayers.

---

## 8. Art, writing and Flow jobs

The helpers are on hold (`docs/tasks/README.md`); these are drafted as tasks **T-021 to T-033** for the board when the
owner lifts the hold (the lead adds them; until then the lead's own agents may do the writing jobs). Every job
follows AGENTS.md 4: original work, no real-world religious iconography, symbols or scripts (no crosses, crescents,
stars of real faiths, wheels, ankhs, halos copied from any tradition), no living artist's style, the near-photoreal
cinematic look of `shots/ue-cloudlab-terminator-final.png` for world images and the gold-on-ink UI of
`docs/concept/ui/` for interface images.

### 8.1 Concept art for Grok

| id | task | delivers to | content and acceptance |
|---|---|---|---|
| G1 / T-021 | **The Word, four clarities** | `docs/concept/mythos/word/` | one town (Iron age, 300 m and 2 km, night and day) receiving an Exact order (a still pale pillar), a Parable (lanterns kindling in a ring), a Dream (blue motes over roofs), a Sign (strange coloured stars from orbit above the limb, and from the town); 8 images + notes on colour, timing and readability at orbit |
| G2 / T-022 | **Messengers** | `docs/concept/mythos/messengers/` | the five archetypes (shepherd, criminal, child, queen, the unbelieved) as painted portraits for the card (3:4) in three culture families each (15), plus a 1:1 silhouette sheet of the medium-zoom marker with each archetype's accent; no gendered caricature, no real-faith dress |
| G3 / T-023 | **The Keepers** | `docs/concept/mythos/keepers/` | five domains as phenomena, not figures (the Last Door, the Deep, Scales, Sleep, a People), each from orbit and at 2 km, plus a Pantheon sigil per Keeper for the UI (vector-friendly, 5) |
| G4 / T-024 | **Mythos UI** | `docs/concept/ui/mythos/` | the card (all layouts: standard, judgement with heart veil, messenger choice, competing prayers, shape people), the Word chooser steps, the Decree tablet, the Faith tab, the Mythos book tabs, the Legacy page and album, at 1920 × 1080 and 390 × 844, with px, hex and font notes like `docs/concept/ui/` |
| G5 / T-025 | **Legend illuminations** | `docs/concept/mythos/legends/` | 12 plates in an original illuminated-manuscript language invented for DEMIURGE (not imitating any real tradition): one per moral × two deed kinds, each with a "told" and a "truth" version side by side |
| G6 / T-026 | **Blessings and warnings in the world** | `docs/concept/mythos/world/` | prosperity fields, a healer's house at night, an enduring village; failed harvests, a smoking omen, a messenger's circuit; at 300 m and 2 km |
| G7 / T-027 | **The House of the God by age** | `docs/concept/mythos/house/` | a temple quarter at Bronze, Iron, Sail, Industry in three families: humble, strong, corrupt (gilded, walled), abandoned; 300 m |
| G8 / T-028 | **Rites in town** | `docs/concept/mythos/rites/` | the five rites' districts (Prayer, Temples, Kindness, Art, World) in one Iron-age town layout at 300 m, silhouettes that read apart |
| G9 / T-029 | **Renewal** | `docs/concept/mythos/renewal/` | the Great Water and the Great Burning from orbit (during and after), the Remnant on its hill at 2 km, ruins of the old world in a new history at 300 m |
| G10 / T-030 | **Archetype worlds** | `docs/concept/genesis/archetypes/` | Generous, Dangerous, Beautiful, Strange from orbit at the Life phase, plus two Strange marvels close up |
| G11 / T-031 | **Legacy key images** | `docs/concept/mythos/legacy/` | one image per legacy (obedient empire, compassionate civilisation, terrified world, thriving wilderness, a humanity that no longer needs you) for the ending page, 16:9 |

### 8.2 Writing jobs (Grok, or the lead's agents while the hold lasts)

| id | task | delivers to | content and acceptance |
|---|---|---|---|
| W1 / T-032a | **Decree sayings** | `docs/content/mythos/decrees.md` | for each area × stance (7 × 3, plus 5 rites), one plain line for the UI and one saying per culture family (8) in the chronicle voice: 26 × 9 lines |
| W2 / T-032b | **Card copy** | `docs/content/mythos/cards.md` | every card kind: 3 title variants, context lines with placeholders, every answer label (≤ 4 words) and effect line (plain words, no numbers but costs), lapse words; the MessengerChoice life lines (5 archetypes × 8 families × 3) |
| W3 / T-032c | **Chronicle templates** | `docs/content/mythos/chronicle.md` | ≥ 3 variants per entry type and kind of 3.31 in the approved voice (11 rules), each with weight and placeholders; passes the lint of 3.31 |
| W4 / T-032d | **Legends** | `docs/content/mythos/legends.md` | for each moral × deed kind: a "told" template at three growth stages and a "truth" line; credit-shift lines (messenger, rival, nature, ancestors) |
| W5 / T-032e | **Keepers and beliefs** | `docs/content/mythos/keepers.md` | Keeper petitions (5 domains × 2 tempers × 3 situations), rogue lines; death-belief sayings (4 beliefs × 3 certainties × 8 families) |
| W6 / T-033 | **Legacy and Lives** | `docs/content/mythos/legacy.md` | one ending paragraph per legacy × mood (5 × 6), and Life templates per age (7 × 4 variants), pronoun-free |
| W7 / T-033b | **Neutrality review** | `docs/reviews/mythos-neutrality.md` | a review of every mythos name and template for real-religion echoes beyond "echo", with a proposed replacement list and additions to the lint |

### 8.3 Flow texture jobs (added to `docs/flow-queue.json` by the lead; same fields, S2 template unless noted)

| rank | id | category | subject (goes into the template) | dest | notes |
|---|---|---|---|---|---|
| 197 | `look-mythos-sign-orbit` | concept | the Word as Sign, from orbit | `docs/concept/flow-look/look-mythos-sign-orbit-<a\|b\|c\|d>.jpg` | 16:9 look anchor: three violet-red stars above the limb over a night continent with city lights; tier 5 (extra look concepts) |
| 198 | `look-mythos-remnant-300m` | concept | the Remnant after the Great Water | `docs/concept/flow-look/look-mythos-remnant-300m-<a\|b\|c\|d>.jpg` | 16:9 look anchor: a hill town above silt plains and drowned fields, morning; tier 5 |
| 199 | `mythos-old-growth-floor` | terrain | ancient forest floor: deep moss, roots, ferns, fallen trunks, seen straight from above, about 4 m | `art/textures-flow/s2/mythos/mythos-old-growth-floor/mythos-old-growth-floor-<a\|b\|c\|d>.jpg` | tier 2 (old growth at close zoom) |
| 200 | `mythos-silt-plain` | terrain | fresh grey-brown flood silt with drying cracks and drift lines, from above, about 8 m | `art/textures-flow/s2/mythos/mythos-silt-plain/mythos-silt-plain-<a\|b\|c\|d>.jpg` | tier 2 (after the Great Water) |
| 201 | `mythos-ash-plain` | terrain | ash plain after a great fire: charred stubble, grey ash, first green shoots, from above, about 8 m | `art/textures-flow/s2/mythos/mythos-ash-plain/mythos-ash-plain-<a\|b\|c\|d>.jpg` | tier 2 (after the Great Burning) |
| 202 | `mythos-gilded-roof` | buildings | over-gilded temple roof tiles, worn gold leaf on dark ceramic, straight on, about 3 m | `art/textures-flow/s2/mythos/mythos-gilded-roof/mythos-gilded-roof-<a\|b\|c\|d>.jpg` | tier 3 (the corrupt House) |
| 203 | `mythos-temple-stone-plain` | buildings | plain pale temple ashlar with mason marks, humble, straight on, about 3 m | `art/textures-flow/s2/mythos/mythos-temple-stone-plain/mythos-temple-stone-plain-<a\|b\|c\|d>.jpg` | tier 3 (the humble House) |
| 204 | `mythos-standing-stone` | props | weathered standing-stone surface with shallow carved bands and no letters, straight on, about 1 m | `art/textures-flow/s2/mythos/mythos-standing-stone/mythos-standing-stone-<a\|b\|c\|d>.jpg` | tier 3 (pact rings, legend stones) |
| 205 | `mythos-grove-bark` | props | very old broadleaf bark with lichen and moss, straight on, about 2 m | `art/textures-flow/s2/mythos/mythos-grove-bark/mythos-grove-bark-<a\|b\|c\|d>.jpg` | tier 4 (sacred groves, old growth) |
| 206 | `mythos-votive-cloth` | props | faded votive ribbons and cloth strips of many colours, knotted on cord, straight on, about 1 m | `art/textures-flow/s1/mythos/mythos-votive-cloth/mythos-votive-cloth-<a\|b\|c\|d>.jpg` | tier 4, S1 (the Prayer rite) |
| 207 | `mythos-lantern-paper` | props | oiled lantern paper lit warmly from behind, straight on, about 0.5 m | `art/textures-flow/s1/mythos/mythos-lantern-paper/mythos-lantern-paper-<a\|b\|c\|d>.jpg` | tier 4, S1 (Parable effect, rites) |
| 208 | `mythos-herb-garden` | crops | a healer's herb garden: low aromatic herbs in beds with narrow paths, from above, about 4 m | `art/textures-flow/s2/mythos/mythos-herb-garden/mythos-herb-garden-<a\|b\|c\|d>.jpg` | tier 4 (the healer's gift) |
| 209 | `mythos-ui-card-vellum` | ui | dark ink-stained vellum with a subtle grain, for card backgrounds | `art/textures-flow/s2/mythos/mythos-ui-card-vellum/mythos-ui-card-vellum-<a\|b\|c\|d>.jpg` | tier 5 |
| 210 | `mythos-ui-tablet-stone` | ui | dark carved stone with gold-filled grooves and no letters, for the Decree tablet slots | `art/textures-flow/s2/mythos/mythos-ui-tablet-stone/mythos-ui-tablet-stone-<a\|b\|c\|d>.jpg` | tier 5 |
| 211 | `mythos-ui-book-parchment` | ui | warm parchment page with faint ruling and no text, for the Mythos book | `art/textures-flow/s2/mythos/mythos-ui-book-parchment/mythos-ui-book-parchment-<a\|b\|c\|d>.jpg` | tier 5 |
| 212 | `mythos-fx-dream-motes` | ui | soft blue glowing motes on black as a 4 x 4 sprite sheet (not tileable) | `art/textures-flow/s1/mythos/mythos-fx-dream-motes/mythos-fx-dream-motes-<a\|b\|c\|d>.jpg` | tier 5, S1; ingest with the square check only |

Each texture job's prompt is the S2 (or S1) template of `docs/flow-queue.json` with the subject above between the
head and ", seen"; the two look anchors use the queue's concept-art head (stylized realism, physically lit, no text).
The lead writes the full JSON rows (prompt, count 4, style, aspect 1:1 or 16:9, tier, dest, notes) into the queue;
its how-to-run and ingest notes apply unchanged (four images per job, 2K upscaled, 0 credits, `tools/flow_provenance.py
ingest`).

---

## 9. Build plan: phases and parallel tracks with disjoint ownership

**Gate.** Nothing starts while another workflow owns `Unreal/Source`, `Unreal/SimTests` or `Unreal/Tools` (check
`docs/handoff/done-ue/` and the running workflows). Each track is built, then independently verified (the house
rule); at most 6 agents at once; Opus (high) for complex tracks, Haiku (xhigh) for mechanical ones (memory note
2026-11-19). Sim tracks build and test with `SimTests/run.ps1 <key>`; only the contracts, balance and verify tracks
run `Unreal/build.ps1`. Client tracks work in lanes (`Unreal/lane.ps1 new <key>`) and sync only their owned files.
Markers: `docs/handoff/done-ue/mythos-<track>.built.txt` and `mythos-<track>.txt`.

**Ownership rule.** A file belongs to exactly one track per phase. Existing module files are edited only by the track
that lists them, and only in the places this spec names. Headers are frozen after the contracts track: later tracks
add nothing to a header except their own half's private helpers in their `.cpp` (if a declaration is missing, the
track reports it and the lead adds it between waves).

### Phase 0: contracts (track **M0 `mythos-contracts`**, 1 agent, Opus; effort M)

Owns: `Sim/MythosTypes.h`; the headers `Sim/{Mythos,Cards,Doctrine,Characters,Legends,Faiths,Omens,Keepers,
Renewal}.h` with every v1-v3 declaration and tune struct of this spec; neutral initial `.cpp` files for those nine
modules; `SimTests/stubs/<Module>_stub.cpp` × 9, `stubs/StubHooks.h` rows, `test_link.cpp` count; `SimTests/run.ps1`
keys; `State.h` (World::mythos, `Settings::archetype`, the per-cell field `wild` u8 Raw), `State.cpp` (extension
blocks, `kVersion` 2, MYTH validation), `Schema.h/.cpp` (Traits fields, `PrayerKind` Victory and Sun, `Cause` Deluge
and Burning, `Ending::legacy`, `Prophet::figure`, the new enums' `toStr` / `fromStr`), `Save.h/.cpp` (version note,
v1 → v2 load), `Core/Events.h/.cpp` (the 22 rows, `ending.legacy`); additions-only declarations in `Powers.h`
(`Group::Word`; the `speak`, `bless`, `test`, `reveal`, `renew` rows; `UseOpts::word`; `Reason::Withdrawn`;
`powers::pay`), `Society.h` (`shapeTraits`, `nudge`, `remember`, `secede`, `answerPrayer`, `RaiseOpts`), `Civ.h`
(`exodus`, `demoteTemple`); the SPEC-SIM.md pointer section "v4 mythos" (writers rows and events of 1.7 and 1.8).

Acceptance:
- `run.ps1 link` and `run.ps1 all` pass with the nine modules neutral; `Unreal/build.ps1` compiles and links.
- `phase` 0 (the default): `simrun --hash` equal to the pre-change build on 8 seeds × passive, kind, cruel, chaotic.
- A version-1 save fixture loads (empty mythos, `wild` 0); a populated synthetic MythosState round-trips and re-saves
  byte-identically; deserialize refuses a MYTH block naming a missing culture, figure or card.
- `test_core_events` passes with the new rows; every new enum round-trips through `toStr` / `fromStr`.

### Phase 1 (v1): the core loop

Systems: 3.1-3.13, 3.23, 3.31 (v1 lines), 4 (verdict). Sim tracks start together after M0 (six lanes), then M7-M9 as
lanes free.

| track | owns | builds | effort | acceptance (beyond its unit tests passing in `run.ps1 <key>` and `all`) |
|---|---|---|---|---|
| **M1 `mythos-core`** | `Sim/Mythos.cpp`, `SimTests/test_mythos*.cpp` | presence (3.2), aspect (3.3), will (3.4), Factors assembly from traits, decrees, rite and survivor memory, archetype params (3.1), ShapePeople / Presence handlers, the legacy verdict (4) | L | tests of 3.1-3.5 and 4; Factors neutral at phase 0 and with no mythos act; `mythos::yearly` ≤ 0.1 ms own share |
| **M2 `cards`** | `Sim/Cards.cpp`, `SimTests/test_cards*.cpp` | the director (2.2), proposals, budget, answer dispatch table, lapse, competing-prayer pairing (3.10) | M | synthetic proposal streams meet 2.2's cadence rules exactly; open cards survive save / load; two runs give identical card lists |
| **M3 `doctrine`** | `Sim/Doctrine.cpp`, `SimTests/test_doctrine*.cpp` | decrees, customs, breaches (3.6), conflict cases (3.7), the Word and interpretation (3.9), judgement (3.11), sentences and survivors (3.12), the rite (3.13) | L | tests of 3.6, 3.7, 3.9, 3.11-3.13; outcome shares within ±2 % over 10,000 forks |
| **M4 `characters`** | `Sim/Characters.cpp`, `SimTests/test_characters*.cpp` | the registry, candidates, the five messenger archetypes and credibility (3.8), rulers (3.23) | M | tests of 3.8 and 3.23; ≤ 160 figures; names never empty |
| **M5 `society-v4`** | `Sim/Society.cpp`, `Sim/Diplomacy.cpp`, `SimTests/test_society_v4*.cpp`, `test_diplomacy_v4*.cpp` | new traits and `shapeTraits` (3.5), Victory and Sun prayers (3.10), `witnessScale` reads (3.2), Factors reads (devotion, love target, prayers, Awe S, schism, warV, aggression, battle, defence, relations, plague hop, warStrength, trade cadence), `RaiseOpts::figure` and visits × credibility (3.8), `nudge`, the fear rule by aspect | L | every v3 / v3.1 society and diplomacy test still passes; neutral Factors = identical yearly results (a recorded 300-year trace) |
| **M6 `economy-world-v4`** | `Sim/Civ.cpp`, `Sim/Economy.cpp`, `Sim/Terrain.cpp`, `Sim/Climate.cpp`, `Sim/Life.cpp`, their `test_*_v4*.cpp` | Factors reads (growth, K, famine, plague, research, cooldowns, farm conversion; `townFactors`), `civ::exodus`, archetype parameters in generation and climate (3.1), marvels | M | land fractions per archetype (3.1); Balanced bit-identical; existing terrain / climate / life / civ / economy tests pass |
| **M7 `powers-v4`** | `Sim/Powers.cpp`, `Sim/Disasters.cpp`, `SimTests/test_powers_v4*.cpp` | `speak` (UseOpts.word → `doctrine::speak`), the Messenger choice flow, `powers::pay`, phase gating of the Word rows in `unlocked`, archetype natural rates and the Strange storm ring | M | each Word power's `can` reasons and costs; nothing spent on a refused word; existing powers tests pass |
| **M8 `chronicle-mythos`** | `Sim/Chronicle.cpp`, `SimTests/test_chronicle_mythos*.cpp` | v1 entry types and templates (3.31; W3's lines if delivered, else the track writes ≥ 3 per type), the prophet → messenger migration, aspect epithet families, the tale's belief and legacy paragraphs, the lint | M | the lint on 8 seeds × 6 policies; mythos ≤ 1.2 notable entries per real minute; the chronicle-budget tests still pass |
| **M9 `mythos-tools`** | `Sim/Sim.cpp`, `Tools/simrun/*`, `SimTests/test_mythos_integration*.cpp` | the schedule lines and attach / rebuild order (1.5), `ending.legacy`, simrun flags and report (5.2), the v1 policies (responder-myth, random-myth, lawgiver, valour, exact-only, parable-only, sign-only, unseen-kind, msg-kind, judge-deeds, judge-hearts, merciful, stern, kingmaker, gentle) and the mythos summary table | M | integration: 8 seeds × all v1 policies × 4,500 years without NaN; `--hash` stable 3×; save / load at a card opening replays identically |
| **B1 `mythos-balance-v1`** | `Sim/Balance.cpp`, `docs/handoff/mythos-v1/*` | `phase` 1 on; `emergent` families on one at a time (customs, aspect, agency, dependence) with before/after tables; retunes | M | every target of 5.1 and of 3.1-3.13 / 4 marked v1; SPEC-SIM 4, v3, v3.1 tables |
| **V1 `mythos-verify-v1`** | `SimTests/test_mythos_v1_adv.cpp`, `docs/handoff/mythos-v1/verify/*` | independent adversarial check (bad ids, zero cultures, all cards lapsing, a god spamming words, save at every card stage, 16 seeds) | M | marker `mythos-v1.txt` with a score and numbers |

Client v1 (lanes; start once M0's headers exist, against stubs and the fixture world; finish against B1):

| track | owns | builds | acceptance |
|---|---|---|---|
| **CM1 `mythos-ui`** | `Game/UI/Mythos/*` (new) | 6.1 for the v1 kinds: panel, card layouts (standard, ShapePeople, Presence, MessengerChoice, Judgement with heart, CompetingPrayers, DecreeConflict, FirstDecree, Dispute-ready), Word chooser (`speak`), Decree tablet, Faith tab (presence, aspect, will, decrees, messenger), Legacy page (verdict + paragraph) | 6.3 model tests; scenarios `mythos-card-*`, `mythos-word`, `mythos-tablet`, `mythos-faith`, `mythos-legacy`, `mythos-narrow` shot and judged |
| **CM2 `hud-input-v4`** | `Game/UI/Hud/*`, `Game/Input/*` | Word palette group, mount points, the Faith tab host, chooser in the armed chip, card toasts, eye count, the Messenger rename, keys 5-8 and `` ` ``, new prayer icons | existing HUD scenarios unchanged; HUD.update budget held; keys documented in Help copy |
| **CM3 `screens-flow-bridge-v4`** | `Game/UI/Screens/*`, `Game/Flow/*`, `Game/Core/*` | bridge events and subsystem calls (6.2), setup row "What kind of world?", the ending's Legacy page host, "Pause when the world asks", auto-pace on cards, the playthrough god answering cards, `mythos-first-ten` | `mythos-first-ten` passes; save / load from the pause menu with an open card |
| **CM4 `fx-sky-v4`** | `Game/FX/*`, `Game/Sky/*` | decree glyph, breach ripple, the four word effects, strange stars, the Strange storm ring, sentence effects | scenario shots at orbit, 2 km and 300 m; 60 fps with a Sign up |
| **CM5 `planet-audio-v4`** | `Game/Planet/*`, `Game/Audio/*` | aspect overlay, marvel decals; v1 audio one-shots and the aspect music colour | audio scenarios: peak, LUFS, sub-bass within `sound-direction.md`; overlay legend |
| **CM6 `towns-v4`** | `Game/Towns/*` | messenger markers by archetype, assembly crowds, the sentence framing at the ruler's hall | `react-messenger`, `react-assembly` shots at three zooms |
| **CV1 `mythos-game-v1`** | `docs/handoff/done-ue/mythos-game-v1.txt`, shots | whole-game check: the owner's playtest path plus a 30-minute playthrough with the playthrough god; close / medium / orbit shots of each v1 visual | marker with screenshots and fps |

### Phase 2 (v2): the systems

Systems: 3.13 districts, 3.14-3.22. Same pattern; no header changes (M0 declared them).

| track | owns | builds | acceptance |
|---|---|---|---|
| **M10 `legends`** | `Sim/Legends.cpp`, `test_legends*.cpp` | 3.15 | tests and simrun targets of 3.15 |
| **M11 `omens`** | `Sim/Omens.cpp`, `test_omens*.cpp` | 3.17 (with the Disasters foresight hook in M7b) | tests of 3.17; natural rates within ±10 % over 5,000 years |
| **M12 `faiths`** | `Sim/Faiths.cpp`, `test_faiths*.cpp` | the House (3.20), rival voices and Ways (3.21) | tests of 3.20, 3.21 |
| **M3b `doctrine-v2`** | `Sim/Doctrine.cpp`, `test_doctrine_v2*.cpp` | pacts (3.14), the Land decree and LandClaim (3.22), rite pressures, warnings spoken as words | tests of 3.14, 3.22 |
| **M4b `characters-v2`** | `Sim/Characters.cpp`, `test_characters_v2*.cpp` | trials (3.18), healers (3.16), rival voices' figures | tests of 3.18 |
| **M1b `mythos-core-v2`** | `Sim/Mythos.cpp`, `test_mythos_v2*.cpp` | blessings and consequences (3.16), disputes (3.19), folding legends, House, pacts and blessings into Factors | tests of 3.16, 3.19 |
| **M5b `society-v4b`** | `Sim/Society.cpp`, `Sim/Diplomacy.cpp` | `remember`, `secede`, the blessed war goal, pacts' no-war term, Way devotion | all society / diplomacy tests |
| **M6b `economy-life-v4b`** | `Sim/Civ.cpp`, `Sim/Economy.cpp`, `Sim/Life.cpp` | `wild` (Life), old growth in farming and claims, `demoteTemple`, failed-harvest K, prepared harm, Tithe K | `test_life_wild`; land tests |
| **M7b `powers-v4b`** | `Sim/Powers.cpp`, `Sim/Disasters.cpp` | `bless`, `test`, the foresight scheduling of natural events | `test_powers_v4b`, `test_disasters` unchanged at phase 1 |
| **M8b `chronicle-mythos-v2`** | `Sim/Chronicle.cpp` | v2 lines, legends in the tale | lint; budget |
| **M9b `mythos-tools-v2`** | `Tools/simrun/*`, `test_mythos_integration_v2*.cpp` | v2 policies (pactkeeper, pactbreaker, blessed-one, healer, warner, tester, tester-hardship, explainer, silent, tyrant, reformer, endorser, exposer, gardener-myth, dominion) and report | integration and determinism as M9 |
| **B2**, **V2** | as B1 / V1 | `phase` 2; `emergent`: House, rival voices, legends, foresight | all targets marked v2 |
| **CM1b** | `Game/UI/Mythos/*` | Legends and Pacts tabs, v2 card layouts, `bless` / `test` choosers | 6.3 for v2 kinds |
| **CM2b** | `Game/UI/Hud/*`, `Game/Input/*` | blessing badges, the pact-at-risk chip, keys 6 and 8 | HUD scenarios |
| **CM4b** | `Game/FX/*`, `Game/Sky/*` | blessings, warnings (dream, harvest, messenger circuit), the trial ring | shots |
| **CM5b** | `Game/Planet/*`, `Game/Audio/*` | old-growth overlay, harvest and prosperity tints, v2 cues | shots, audio checks |
| **CM6b** | `Game/Towns/*`, `Game/Nature/*` | rite districts, the House's quarter and gilding, rival crowds, pact and legend stones, old-growth trees | `react-*` shots at three zooms |
| **CV2** | marker and shots | whole-game check v2 | as CV1 |

### Phase 3 (v3): the deep systems

Systems: 3.24-3.30, 4 (Lives).

| track | owns | builds | acceptance |
|---|---|---|---|
| **M13 `keepers`** | `Sim/Keepers.cpp`, `test_keepers*.cpp` | 3.26 | tests and targets of 3.26 |
| **M14 `renewal`** | `Sim/Renewal.cpp`, `test_renewal*.cpp` | 3.30 orchestration and `renewal::reseed` | tests of 3.30 |
| **M3c `doctrine-v3`** | `Sim/Doctrine.cpp` | revelations (3.25), beliefs about death (3.27) | tests of 3.25, 3.27 |
| **M12b `faiths-v3`** | `Sim/Faiths.cpp` | the Bearers (3.24) | tests of 3.24 |
| **M1c `mythos-core-v3`** | `Sim/Mythos.cpp` | coming of age (3.28), withdrawal (3.29), Lives (4) | tests of 3.28, 3.29, 4 |
| **M4c `characters-v3`** | `Sim/Characters.cpp` | Life figures sampled at act close | ≤ 7 lives; pronoun-free templates' data |
| **M5c `society-v4c`** | `Sim/Society.cpp`, `Sim/Diplomacy.cpp` | `answerPrayer`, messengers abroad, `neglectScale`, the Bearers' Ark speed, Keepers' fleet losses | all society / diplomacy tests |
| **M6c `economy-life-v4c`** | `Sim/Civ.cpp`, `Sim/Economy.cpp`, `Sim/Life.cpp` | plague flight, the age-floor science reset, the Burning's fire waves, `fill` for reseed | land tests |
| **M7c `powers-v4c`** | `Sim/Powers.cpp`, `Sim/Disasters.cpp` | `reveal`, `renew`, the Deluge and Burning disaster types, `Reason::Withdrawn` | disasters' bounds (≤ 4 ms a tick) |
| **M8c `chronicle-mythos-v3`** | `Sim/Chronicle.cpp` | v3 lines, Lives, the reseed opening | lint |
| **M9c `mythos-tools-v3`** | `Sim/Sim.cpp`, `Sim/Save.cpp` (parent-world meta), `Tools/simrun/*` | reseed support, v3 policies (zealot, humble-bearers, revealer-early, revealer-ready, pantheon, contrarian, parent, withdrawer, withdrawer-silent, renewer) | integration; `--saveload` mid-withdrawal and mid-Deluge |
| **B3**, **V3** | as B1 / V1 | `phase` 3 | every target of this spec |
| **CM1c** | `Game/UI/Mythos/*` | Pantheon and Beliefs tabs, v3 cards, `reveal` / `renew` choosers, the Lives album | 6.3 |
| **CM2c** | `Game/UI/Hud/*`, `Game/Input/*` | the Withdraw button and key 9, the seal | HUD scenarios |
| **CM3c** | `Game/UI/Screens/*`, `Game/Flow/*`, `Game/Core/*` | "Begin again from this world", withdrawal speed, the bridge's v3 calls | a reseeded world opens at the People phase with ruins |
| **CM4c** | `Game/FX/*`, `Game/Sky/*` | the Keepers' phenomena, the Deluge and the Burning, revelation light, withdrawal dim | shots from orbit and 2 km |
| **CM5c** | `Game/Planet/*`, `Game/Audio/*` | the Deluge water, Keepers' themes, renewal cues | audio checks |
| **CM6c** | `Game/Towns/*` | funerary customs, the Bearers' banners | shots |
| **CV3** | `docs/handoff/done-ue/mythos-v3.txt` | final whole-game check: a full playthrough per legacy policy (five), shots, fps, simrun 8 seeds × every policy × 3 hashes | marker |

### 9.1 Order, critical path and effort

1. M0 (≈ 1 agent-day). 2. M1, M2, M3, M4, M5, M6 together (6 lanes, 1-3 days); M7, M8, M9 as lanes free; client
CM1-CM6 start on stubs as sim lanes free. 3. B1, then V1 and CV1. 4. Phase 2 the same way (M10-M12 first: they are new
files), then Phase 3. The critical path is M0 → M3 (L) → M9 → B1 → V1 → (v2) M12 → B2 → (v3) M13 / M14 → B3 → CV3:
about 12-16 agent-days of wall time; total effort about 55-70 agent-days across the three phases. Every phase ships
on its own (the game is complete and balanced at `phase` 1 and at 2).

---

## 10. Risks and decisions for the owner

1. **Clutter.** Mythos cards, gameplay-v4 situations (if built) and the Now strip compete for the left edge. One
   director, at most 2 open cards, one toast per 20 s, and the rule "two answers or it is a toast". *Owner: approve the
   pace of one choice every 1-3 minutes, shared with situations.*
2. **The Messenger rename.** The neutral-names rule implies renaming the Prophet power and chronicle lines to
   Messenger / seer / voice. *Owner: confirm (recommended).*
3. **Keys.** 5-8 arm Word powers, 9 withdraws, `` ` `` opens the book (every letter is taken). *Owner: accept.*
4. **Legacy beside mood.** The ending keeps its mood (the v3 targets) and adds the Legacy as a second line. *Owner:
   confirm both stay (recommended) rather than Legacy replacing mood.*
5. **Looking into the heart costs 20 Awe.** Judging blind is the default gamble; free sight would make every
   judgement easy. *Owner's pick.*
6. **Emergent rules move a passive world** (agency on research, customs, the House, rival voices). They ship behind
   `emergent` and are switched on with tables; the Ark window may need a retune. *Accepted in this plan.*
7. **Renewal and ages.** The Great Water / Burning sets science back to the start of the current age (no age is lost,
   as SPEC-SIM promises) and "Begin again from this world" makes a new save. *Owner: confirm.*
8. **Content volume.** About 1,500 new lines of copy and templates. The helpers are on hold; until it lifts, the lead's
   agents do the writing jobs (W1-W7) and the art jobs wait (the client uses simple stand-ins and the existing palette).
9. **Save version 2.** Extension blocks let gameplay-v4's and story-v4's future state add tags without a second version
   fight; every save test is re-baselined once, in M0.
10. **Walking among them** stays deferred; `Presence::Walk` is reserved so a later spec can add it without a save
    change.
