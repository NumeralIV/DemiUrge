# DEMIURGE: Rules v4.1 spec (decisions, not reactions)

*Lead design (track "rules-v41-spec"), 2026-11-23. Design and build plan only; no code was changed. Binding for the
v4.1 build tracks in section 4 the way `SPEC-SIM.md` binds the simulation and `SPEC-MYTHOS.md` binds mythos:
**module boundaries, state shapes, event names and payloads, the writers rows, the schedule lines, the save block and
the attention contract are binding; numbers are starting values** in the TUNE structs named below. Only the balance
owner retunes them after integration. Independently reviewed 2026-11-23; the review's changes are listed in
`docs/handoff/done-ue/rules-v41-spec.txt`.*

**Names.** **R1-R11 are rules** (section 3). **T0-T7 are build tracks** (section 4), plus B41 (balance) and V41
(verification). A file or function marked "(T4)" belongs to track T4, which is not rule R4.

**Read first:** `AGENTS.md`, `docs/CONCEPT.md`, `docs/SPEC-SIM.md` (all of it), `docs/SPEC-MYTHOS.md` (sections 0, 1, 2,
3.10, 3.16, 3.17, 4, 5, 9), `docs/SPEC-UI.md` 4, 4b and 5, `docs/reviews/astra-design-review.md` (the input),
`docs/reviews/gameplay-v4.md` 3.1 (the umbrella), `docs/handoff/sim-findings-2/` (the evidence) and the Sim headers this
spec changes (`State.h`, `Society.h`, `Powers.h`, `Civ.h`, `Climate.h`, `Life.h`, `Cards.h`, `Sim.h`, `MythosTypes.h`).

**Time.** 1 tick = 0.25 years. At 1x one real second is 2 years and one real minute is 120 years (SPEC-SIM 0.5).
"N s" is always real seconds at 1x.

**Evidence.** "Now" numbers are from `docs/handoff/sim-findings-2/simrun-after-8seeds-all.txt` (8 seeds x 17 policies x
4,500 years, the v3.1 build): min / median / max over seeds unless stated otherwise. A policy "stroke" is one brush call
of `kRealPerTick` = 0.125 s.

---

## 0. Decisions this spec builds on

| # | Decision | By |
|---|---|---|
| D1 | Astra's bundle (T-006, 11 findings) is folded into gameplay-v4's **Situations** as **one sim wave**, "rules v4.1". Situations is the umbrella: Astra's risks and her prayer choruses are both situations. | lead, PR 19 |
| D2 | Astra's numeric targets are **tracking metrics, not pass/fail gates**, until a real 30-minute owner session confirms the cadence. Construction invariants (for example "no verdict word before year 250") are ordinary SimTests and do pass or fail. | lead, PR 19 |
| D3 | **One late-game attention budget.** The card director (`Sim/Cards.*`, SPEC-MYTHOS 2.2) owns it. Situations report into it and never open a card of their own. `CardKind::Situation` stays reserved and unused (its dispatch row in `Cards.cpp` stays `nullptr`). This amends SPEC-MYTHOS 0.3 and 2.2: situations are list rows with a fast cadence, cards are the slow cadence, and both draw on one ledger. | this spec |
| D4 | v4.1 duplicates no Mythos system. Risks forecast *developing states* (hunger, tension, contagion, a rising sea, tinder). Omens (SPEC-MYTHOS 3.17) foresee *scheduled natural events* and own warning the people. Competing prayers (3.10) keep the Victory and Sun prayers. Blessings (3.16) appear only as listed answers. | this spec |
| D5 | Every rule ships behind a TUNE flag that defaults to today's behaviour (the v3.1 convention, SPEC-MYTHOS 0.1 rule 6). **With its flag off, a family writes nothing anywhere**: no `World::rules` part, no `vars.num` or `mythos.num` key, no event, no ledger entry. `Balance.cpp` switches the families on one at a time, with before/after tables. | this spec |
| D6 | Mythology-neutral. The new names are **Gift** (not "revelation", which is Mythos 3.25's), **Ward**, **Guiding Light** and **Leave-taking** (not "rite", which is Mythos 3.13's). There is no mortal avatar. | owner rules |

---

## 1. Scope

### 1.1 Astra's eleven findings

| # | Finding | Verdict | Reason (one line) | Rule |
|---|---|---|---|---|
| 1 | Kind play is continuous painting | **In** | Kind needs 1,266-1,924 strokes and 57-75 clicks today. A durable episode turns that painting into a choice of where to spend 15 years. | R1 |
| 2 | Hundreds of settlement prayers | **In** | 467-1,279 prayers per history. Choruses and once-per-chorus credit fix the incentive, not only the HUD. | R2 |
| 3 | Crises reported after the window closed | **In** (five risks plus the Ark race) | This is the core of the Situations umbrella. Stores and `vSlow` create the warning windows. | R3 |
| 4 | Awe is a cooldown | **In** | Passive sits at 95 % of the cap or more in 91-92 % of years, and kind never does (0 %). A soft holding cost plus a recovery band makes timing a choice. | R4 |
| 5 | The Ark has no constructive lever | **In** | Inspire, wrath and a Leave-taking change who leaves. Memory runs at 25 % weight and seals at 0.8, so late laundering still fails. | R5 |
| 6 | Beloved before the player has a history | **In** | Kind shows Beloved before year 20 on 8/8 seeds today. `mature` gates the public word only, not the verdict. | R6 |
| 7 | Load felt in absolute terms, not as ratios | **In** (sim ledger and headless metrics) | Up to 49-82 raw moments per minute in Industry. The ledger caps loud items. Digests of repeated battles and fires are client work, deferred to the client track. | R7 |
| 8 | Saving a drowning town is priced as cruelty | **In** | Raise Land under a warned town costs x1 and reads as grace until the town is safe. | R8 |
| 9 | Atomic and Space upgrade but never ask | **In**, renamed **Gifts** | Two irreversible pairs paid with 300 Awe. "Revelation" is taken by SPEC-MYTHOS 3.25. Astra's "Mercy" becomes **Ward** (Faith::mercy is a Mythos field). | R9 |
| 10 | Climate has no agency target | **In**, partly | Attribution and Bloom restoration episodes are built. No new power. If the steward target misses, the lever is `ClimateTune::restoreUptake` (balance), not a new rule. | R10 |
| 11 | The score ranks one moral answer | **In** | Today a beloved policy has the top score on 8/8 seeds. The mood multiplier goes; Life, Continuity and Wonder remain. Mythos Legacy stays the non-ranked title. | R11 |

### 1.2 gameplay-v4 pieces under the umbrella

| Piece (gameplay-v4) | Verdict | Reason |
|---|---|---|
| 3.1 Situations module, list, `vSlow`, stores, raising a warned town, Ark hooks | **In** | R3, R8, R5 |
| 3.1 kinds Hunger, Plague, War brewing, The sea comes, Wildfire, The race to the stars | **In** | These are R3's six kinds plus the plea (R2). Wildfire becomes the "Tinder" risk (fires are small: the largest burns 9-12 cells per history). |
| 3.1 A preacher rises; the Victory prayer | **Rejected here** | SPEC-MYTHOS owns them as rival voices (3.21) and competing prayers (3.10). |
| 3.1 The mountain smokes (eruption precursors) | **Rejected here** | Mythos Omens' foresight queue (3.17) owns scheduled eruptions. |
| 3.1 Golden age, Strangers meet, Far towns drift, The air warms, The fire weapons (nuclear ramp) | **Deferred** to v4.2 | Each needs callings or zones, or adds load before the ledger is proven. The air warms is covered by Sea's attribution line. |
| 3.1 outcome effects ("Answered adds 0.5 x severity grace", "Lost costs love -0.1") | **Rejected** | They would count the witnessed deed twice and break the rule that Situations write only their own list. |
| 3.2 Callings, 3.3 zones and roads, 3.4 ambitions, 3.7 setups, vows, challenges | **Deferred** | These are separate waves. The RULE save block (2.3) leaves room for their sub-blocks. |
| 3.5 readable loops (`forecast`, `project`) | **Partly in** | `situations::answers` and `climate::attribution` serve the risk rows. The overlays are client work. |
| 3.6 first five minutes, 3.9 auto-pace v2 | **Client, later** | The sim provides the ledger's `loud` flags (R7). Flow builds the script. |

---

## 2. Architecture

### 2.1 Files

| File | New / changed | What | Track |
|---|---|---|---|
| `Sim/RulesTypes.h` | **new** | every persisted v4.1 struct and enum (2.2), self-contained like `MythosTypes.h` | T0 |
| `Sim/Situations.h` / `.cpp` | **new** | namespace `situations::`: the risk and plea list, detectors, answers, chorus bookkeeping | T0 header and neutral `.cpp`; T1 the real `.cpp` |
| `SimTests/stubs/Situations_stub.cpp`, `stubs/StubHooks.h` rows, `test_link.cpp` count, `run.ps1` key `situations` | **new / changed** | the FOUNDATION-CPP part 2 module kit | T0 |
| `State.h` / `State.cpp` | changed | `World::rules`; `state::rulesDefault`; the `"RULE"` block, written only when `rulesDefault` is false; `extensionsDefault` also requires `rulesDefault` (2.3) | T0 |
| `Core/Events.h` / `.cpp` | changed | four rows (2.6) | T0 |
| `SimTests/test_save.cpp`, `SimTests/test_core_events.cpp` | changed (new cases only) | the T0 acceptance rows | T0 |
| `Society.h`, `Powers.h`, `Civ.h`, `Climate.h`, `Life.h`, `Cards.h`, `Sim.h` | changed (additions only) | the tune fields and declarations named in section 3 (every one is **new** unless the T0 "What is new" list in section 4 names it as existing) | T0 |
| `Society.cpp`, `Diplomacy.cpp` | changed | rules R2, R4, R5, R6; `vSlow` and `tension` (R3) | T2 |
| `Economy.cpp`, `Civ.cpp`, `Climate.cpp`, `Life.cpp` | changed | stores, yearly emissions, the climate ring, attribution, restoration | T3 |
| `Powers.cpp`, `Disasters.cpp` (one call only) | changed | episodes, rescue pricing, gifts, Ark hooks of Inspire and Consecrate, the Ark wrath push in `setBack` and at a god's meteor impact | T4 |
| `Cards.cpp`, `Sim.cpp` | changed | the attention ledger and its gate, schedule lines, attach order, score lines | T5 |
| `Tools/simrun/*` | changed | policies, `--rules`, the tracking table | T6 |
| `Chronicle.cpp` | changed | the v4.1 lines | T7 |
| `Balance.cpp`, `docs/handoff/rules-v41/*` | changed / new | switching the families on, with tables | B41 |
| `docs/SPEC-SIM.md` section 6 "v4.1 rules"; a one-line amendment note under SPEC-MYTHOS 0.3 and 2.2 pointing to D3 | changed | a pointer section: writers rows and events of 2.5 and 2.6 | T0 |

`Disasters.cpp` changes in one place only: where a god's meteor impact already calls `civ::strikeKnowledge` (the
`!nat && wantWitness` branch), it also calls `society::arkPush` for a struck Ark capital (R5). The Ark push is **not**
put inside `civ::strikeKnowledge` (defined in `Economy.cpp`), because Doctrine's "burning" decree outcome
(`Doctrine.cpp`) also calls it, and that is the people's act, not the god's wrath. Tinder reads drought disasters
read-only.

### 2.2 World state (persisted; `Sim/RulesTypes.h`)

`World` gains one member, `RulesState rules`. Each module writes only its own part (2.5). The caps in brackets also
bound the save size.

```cpp
enum class SituationKind : uint8_t { Hunger, War, Plague, Sea, Fire, Plea, Ark, Count };
enum class RiskStage     : uint8_t { Watch, Rising, Crisis, Over };           // Watch: forecast only
enum class RiskOutcome   : uint8_t { None, Answered, Weathered, Lost };
enum class AttnClass     : uint8_t { Epochal, Crisis, Card, Notice };         // R7, highest priority first
enum class ArkBy         : uint8_t { Inspire, Wrath, LeaveTaking };
enum class EpisodePower  : uint8_t { Rain, Bloom };

struct Situation {                     // [open 24, closed last 32]
	int id = 0; SituationKind kind = SituationKind::Hunger;
	uint8_t sub = 0;                     // Plea: the PrayerKind (Rain, Heal, Peace, Bloom)
	RiskStage stage = RiskStage::Watch; RiskOutcome outcome = RiskOutcome::None;
	int cell = -1, culture = -1, other = -1, settlement = -1;   // other: War's second people; settlement: the focus town
	std::array<int, 8> towns{ -1, -1, -1, -1, -1, -1, -1, -1 };  // members (Plea, Plague), ascending id
	std::array<double, 8> peak{};        // Plea: each member's peak population while a member (affected = their sum)
	double began = 0, stageSince = 0, eta = -1;  // eta: sim year of the next stage (Crisis, or the end), -1 unknown
	double stake = 0;                    // people at stake (the sort key)
	double severity = 0;                 // 0..1, the kind's own measure (Plea: Astra's culture share)
	double affected = 0, reached = 0;    // Plea: members' peak people / people reached by answered prayers
	bool answered = false;               // the god acted on its towns or cell while it was open
	bool loud = false;                   // the ledger let its last stage change interrupt (R7)
	int omen = -1;                       // a Mythos Omen of the same danger, -1 none (read only)
	NumBag data;
};
struct Episode { int id = 0; EpisodePower power = EpisodePower::Rain; int cell = -1;   // [3 active]
	double ang = 0.10, began = 0, until = 0, strength = 1; bool restore = false, refreshed = false; NumBag data; };
struct ChorusCool { double until = -1, severity = 0; };
struct ArkLever { double inspired = -1e9, setback = 0; bool leaveTaking = false; };   // per culture
struct ClimateSample { double year = 0, co2 = 0, anomaly = 0, seaLevel = 0, uptake = 0; };   // [11]
struct AttnEntry { double year = 0; AttnClass cls = AttnClass::Notice; uint8_t kind = 0; int ref = -1; bool loud = false; };  // [64]
struct RulesState {
	std::vector<double> stores;                          // index = settlement id, years of food
	std::map<std::pair<int, int>, double> vSlow;         // key = state::relKey(a, b)
	std::vector<Situation> situations;
	std::vector<std::array<ChorusCool, 4>> chorusCool;   // index = culture id; [Rain, Heal, Peace, Bloom]
	std::vector<Episode> episodes;
	std::array<int8_t, 2> gifts{ 0, 0 };                 // [Atomic, Space]: 0 none, 1 first option, 2 second
	std::vector<double> rescued;                         // index = settlement id: year of the last credited rescue, -1e9 never
	std::vector<ArkLever> ark;                           // index = culture id
	std::vector<ClimateSample> climate;                  // every 10 years, oldest first
	std::vector<AttnEntry> attention;                    // the ledger, oldest first
	NumBag num;                                          // module scalars, keys prefixed by owner: "powers_lastSpent",
	                                                     // "civ_co2y_<id>", "climate_uptake", "situations_..."
	int nextId = 1;                                      // Situation and Episode ids
};
```

All v4.1 scalars live in `rules.num`, never in `vars.num` (the version-1 payload), so turning a read-only family on
leaves the v1 view (`core`, 2.3) untouched. The one exception is the chorus count `society_chorus_<id>` (R2), which
sits in `vars.num` beside `society_pray_` and `society_answer_` because daughters inherit all three through the same
Society code path; it is written only with `SocietyTune::chorus` on, and that family changes behaviour anyway.
`AttnEntry::kind` is the event or class key (the Epochal event's `Ev` value, the `SituationKind`, or the `CardKind`);
`ref` is its subject: a settlement id, a culture id, a situation or card id, or for a pair of peoples
`lo * 1024 + hi`.

### 2.3 Save format: the `"RULE"` block

The state stays version 2 (`state::kVersion`). The contracts track adds **one top-level extension block `"RULE"`
version 1** after `"MYTH"`, with its own sub-block list in the MYTH framing (`{ tag, u16 version, u32 length, bytes }`,
then `"END_"`). Older builds skip the unknown tag, so saves stay readable in both directions, and `"BASE"` stays at
version 1. **RULE is written only when `state::rulesDefault(w)` is false** (every RulesState part empty, `gifts`
{0, 0}, `num` empty, `nextId` 1). A world with every v4.1 flag off therefore saves byte-identically to the pre-v4.1
build.

| Sub-block | Contents | Writer | Missing on load (a v2 save without RULE) |
|---|---|---|---|
| `STOR` | `stores` | Economy | with `EconomyTune::stores` on, `civ::rebuild` sizes `stores` to the settlement count and every entry it adds starts at the town's cap (4, or 8 with a granary); with it off, `stores` stays empty |
| `VSLO` | `vSlow` | Diplomacy | with `slowGate` on, the first `society::diplomacy` pass sets `vSlow = v` for every pair it lacks; with it off, nothing |
| `SITU` | `situations`, `chorusCool` | Situations | empty; the next yearly pass rebuilds the open list from state |
| `EPIS` | `episodes`, `gifts`, `rescued` | Powers | none active, no gift, no rescue |
| `ARKL` | `ark` | Society | defaults |
| `CLIM` | `climate` | Climate | empty; the trend reads 0 until two samples exist |
| `ATTN` | `attention` | Cards | empty |
| `RNUM` | `num`, `nextId` | any (prefixed keys) | empty, 1 |

`deserialize` validates every id: settlement, culture, relation key and member towns must exist; situation and
episode ids must be below `nextId`. Index-keyed vectors must have the right length: `stores` and `rescued` either
empty or exactly `settlements.size()`, `chorusCool` and `ark` either empty or exactly `cultures.size()`. A bad id or
length refuses the save, as FOUNDATION-CPP 4.3 already does. The block should stay under 24 KB at year 4,500. This
is an estimate: V41 measures it with `state::sizeOf`.

**Hashes.** `sim::fingerprint` (simrun `hash`) hashes the version-1 layout while `state::extensionsDefault(w)` is
true, and the full version-2 save otherwise (`Sim.cpp`). T0 makes `extensionsDefault` also require
`rulesDefault`. Three consequences:
- With every v4.1 flag off, RULE stays default and is never written. Every recorded hash still holds: phase 0
  (`phase0-guardrail.ps1`) and the phase B1 hashes.
- Once any family writes RULE, `hash` covers it. At phase 0 that also switches `hash` to the version-2 layout, so
  `hash` changes even when nothing else in the world moved.
- A proof that a family is **read-only** therefore uses simrun's `core` hash and `w.rng.s`, not `hash`. `core` is
  the version-1 view (`coreHash` in `simrun.cpp`), which never contains RULE or `rules.num`.

Determinism checks use `hash`. No second hash is needed. (An earlier draft added a `hash2` because it assumed the
fingerprint can never see RULE. That is only true at phase 0 with every extension at its default.)

### 2.4 Schedule (one line each, Sim owner T5)

```
civ::yearly -> society::yearly (diplomacy, updateEpithets) -> [phase >= 1] mythos::yearly -> situations::yearly
-> [phase >= 1] cards::yearly -> powers::weather -> climate::yearly -> powers::natural -> sim::refreshStats
-> chronicle::snapshot -> sim::checkEnding
```

Today `Sim.cpp`'s `yearlyGroup` runs `mythos::yearly` and `cards::yearly` together inside one `if (mythosOn(w))`.
T5 splits that block so `situations::yearly` sits between them and runs at **every** phase. `powers::weather` and
`situations::yearly` return at once when their flags are off.

`sim::use`: `balance::apply -> mythos::attach -> situations::attach -> cards::attach -> chronicle::attach -> rederive ->
mythos::rebuild -> situations::rebuild`. The two new attach calls follow the Chronicle pattern: tokens in scratch,
re-subscribed exactly once, at every phase. They subscribe nothing while their flags are off. Their handlers **only
record** into their own state and never act. `situations::yearly` and `cards::attend` run whatever
`MythosTune::phase` is set to. Card openings still need phase 1 or higher. simrun sets tuning after `sim::create`,
so when it turns a v4.1 flag on it also calls `situations::attach`, `cards::attach` and `situations::rebuild` itself,
just as it already calls `mythos::attach` / `mythos::rebuild` (T6).

### 2.5 Writers (additions to SPEC-SIM 1.1, binding)

| State | Written by |
|---|---|
| `rules.stores` | Economy (`civ::found` sets the start; Economy fills and drains) |
| `rules.vSlow` | Diplomacy |
| `rules.situations`, `rules.chorusCool` | Situations |
| `rules.episodes`, `rules.gifts`, `rules.rescued`; rules.num `powers_lastSpent` | Powers |
| `rules.ark`; `settlement.ark` changes from levers (through `society::arkPush` only; called by Powers, and by Disasters at a god's meteor impact) | Society |
| `rules.climate`; rules.num `climate_uptake` | Climate |
| rules.num `civ_co2y_<id>` (this year's emission per culture; Economy zeroes every live culture's key at the start of `civ::yearly`) | Economy |
| `rules.attention` | Cards |
| prayer counts, science and resolved choruses from choruses (vars.num `society_pray_`, `society_answer_`, `society_chorus_<id>`) | Society, through `society::chorus` |

### 2.6 Events (add-only rows in `Core/Events.h`)

| Event | Payload | Emitted by |
|---|---|---|
| `situation` | `{ id, kind, stage, outcome, culture, loud }` (kind and stage as strings; once per stage change) | Situations |
| `episode` | `{ id, power, cell, ang, start, restore }` (start false at the end, or when replaced) | Powers |
| `ark:push` | `{ culture, settlement, delta, by }` (by: `inspire`, `wrath`, `leave-taking`) | Society |
| `gift` | `{ age, choice }` (choice: `monsoon`, `ward`, `star-fall`, `guiding-light`) | Powers |

### 2.7 The gates (every flag defaults to off, which is v3.1 / Mythos behaviour)

| Family | Flag | Also read by |
|---|---|---|
| Situations list (read-only) | `SituationsTune::on` | Cards (crisis count) |
| Climate attribution (read-only): the ring, `civ_co2y_<id>`, `climate_uptake`, `climate::attribution` | `ClimateTune::attribution` | Situations (Sea kind needs it: with it off, no Sea row opens), Economy |
| Stores | `EconomyTune::stores` | Situations (Hunger) |
| Slow war gate | `DiplomacyTune::slowGate` (and `omenAge = 1` in Balance, if B41 step 3 allows it) | Situations (War) |
| Choruses | `SocietyTune::chorus` (needs `SituationsTune::on`) | Situations |
| Mature words | `SocietyTune::matureWords` | HUD via `memory` |
| Awe reserve | `SocietyTune::reserve` | HUD via `society::reserve` |
| Ark levers | `SocietyTune::arkLevers` | Powers, Disasters (one call) |
| Weathering episodes | `PowersTune::episodes` | Society (no prayer inside a matching episode) |
| Restoration | `PowersTune::restore`; `ClimateTune::restoreUptake` (1 = neutral) | Climate |
| Rescue pricing | `PowersTune::rescue` | Situations (`seaMargin`) |
| Gifts | `PowersTune::gifts` | Powers (`upgrade`) |
| Attention ledger | `CardsTune::attention` | Situations, the director |
| Neutral score | `SimTune::scoreNeutral` | Screens |

**Guardrail.** With every flag off, `simrun --hash` equals the pre-change build on 8 seeds x passive, kind, cruel and
chaotic, at Mythos phase 0 (`phase0-guardrail.ps1`, 32 of 32) and at the phase B1 shipped (its own recorded hashes).
`SituationsTune::on` with `ClimateTune::attribution` (and nothing else) leaves `core` and the rng state unchanged on
every seed and policy: they draw no rng and write only RULE. `hash` does change, because it now covers RULE (2.3).
With `CardsTune::attention` off, `cards::attend` records nothing and returns true. With `SocietyTune::chorus` off,
plea rows still list, but `society::chorus` is never called and the per-town prayer counts stay as v3.1.

### 2.8 Determinism and cost (all rules)

- No v4.1 rule draws from `w.rng` or a fork of it. Every v4.1 rule is a deterministic function of state.
- Iterate settlements, cultures, relation pairs, situations and episodes in ascending id order. Sorts tie on id. Sums
  over towns run in ascending id.
- The EMA uses a constant `alpha = 1 / slowTau` (one multiply-add per pair per year), with no `std::exp` or `std::pow`.
  Trends use ring endpoints (`(x[now] - x[now-100y]) / 100`), never a running regression.
- Real time counts only from the `dt` passed to `powers::use` (episode commit time). It is never read from a clock.
  The commit test is `held + 1e-9 >= episodeCommit`, so four calls of dt 0.1 always commit and three never do.
- No rule keeps a hidden history. A value "N years ago" is either in the persisted ring (`rules.climate`) or derived
  from the current state (the War slope, R3). No rule relies on a scratch cache that a load would drop, except the
  brush press accumulator, which a load drops by design (R1).
- Situations iterates `w.disasters`, `w.mythos.omens` and `w.mythos.cards` in ascending id, and takes the plague
  rings from `Geo` with the same BFS Economy uses (`EconomyTune::plagueHops`).
- Cost at L = 6 with 120 settlements and 6 cultures: `situations::yearly` 0.15 ms mean or less, `powers::weather`
  0.05 ms or less, `cards::attend` O(1). simrun measures these in Release; SimTests only report them. The v4.1 cost
  gate is relative: with every family on, the mean tick rises by at most 0.25 ms, and each run's maximum tick does
  not rise by more than 10 %. sim-findings-2 already has one run of 136 with a thread-CPU maximum of 57.9 ms
  (the other 135 are 6.1-29.0 ms), so an absolute 30 ms gate is noisy before v4.1 changes anything. The 30 ms target
  stays the SPEC-SIM goal.

---

## 3. Rules

Each rule gives: **Problem**, **Rule** (numbers), **Code** (new or changed, by file), **Save**, **Determinism**,
**UI** (the SPEC-UI element; the client is a later separate track) and **Mythos** (cards and the director).

### R1. Weathering episodes (Astra 1)

**Problem.** Rain and Bloom are per-second brushes. Kind paints 1,266-1,924 strokes (158-241 brush-seconds) per history,
so the best policy never has to choose.

**Rule** (`PowersTune`):

| Item | Value |
|---|---|
| Commit | A Rain or Bloom press held for `episodeCommit` 0.4 s of brush `dt`, with its centre staying within 1.0 x ang of where the press began, commits **one episode**. Before the commit nothing is spent and nothing happens; a press released earlier is free and has no effect. Moving further restarts the 0.4 s at the new centre. |
| Duration | `episodeYears` 15 (7.5 s). A restoration Bloom (R10) lasts `restoreYears` 30. |
| Cost, paid once at commit | Rain `episodeCostRain` 25, Bloom `episodeCostBloom` 30, x (ang / def.ang)^2, x `offTerritoryMul` 2 when the brush covers no territory, Rain x `monsoonCostMul` 0.5 with Monsoon (R9). Cannot pay: `awe:short` and no episode. |
| Witness at commit | One `power:used`, one deed and one `society::witness` with faith = def.faith x `episodeFaith` 2 (0.04), the table's good (Rain +0.6 for glad towns, else +0.1; Bloom +0.4) and wAng = 2 x ang. This answers matching prayers: one answer per episode. Today `society::witness` paces brush kinds (it emits `miracle:witnessed` only on every `brushEventEvery` 2nd call, and scales love by elapsed seconds). So Powers passes the new `WitnessOpts::episode = true`, and Society treats the commit as one unpaced act: one `miracle:witnessed`, love at full weight. |
| Each year (`powers::weather`) | Rain applies the existing `rainBrush` effect once with `dt = episodeSecondsPerYear` 0.5 x strength: wet, fertility, need.climate, so wet > 0.3 puts fires out. Bloom calls `life::bloom(cell, ang, bloomRate x 0.5 x strength)`. There is no witness and no memory. |
| Refresh | A press committing inside an active episode of the same power (centre within its ang) adds strength +0.1 once per episode (cap 1.1). It costs 0 and is not witnessed or counted as a deed. |
| Cap | `episodeMax` 3 active (both powers together). A fourth ends the oldest (`episode { start: false }`). |
| Prayers | A town whose centre lies inside an active Rain (Bloom) episode does not *begin* a rain (bloom) prayer: "the god is already at work here". |

Raise and Lower stay per-second brushes.

**Code.** `Powers.h` (T0): the PowersTune fields above, plus `episodes` (false) and `restore`, `restoreYears`.
`void powers::weather(World&)` steps episodes yearly and ends them (`episode { start: false }`).
`int powers::episodeAt(const World&, int cell, EpisodePower)` (read-only, -1 none). Commit runs inside `powers::use`'s
brush branch through a new press accumulator in `PowersScratch`: transient, and a load drops the armed power by
design. `Powers.cpp` (T4) implements all of it. `Society.cpp` (T2): `causeHolds` reads `episodeAt`.

**Save.** `RULE/EPIS`. **Determinism.** Commit time comes only from `dt`. The yearly effects use fixed amounts.
The witness is the existing `society::witness`.
**UI.** SPEC-UI 4 armed chip: a commit ring fills over 0.4 s and the price shows once ("25 Awe for 15 years", not
"/s"). The palette cost for Rain and Bloom becomes a single price. Map: a rain veil or green shimmer over the episode,
with a years-left ring. The client draws its visual edge on refresh.
**Mythos.** An episode is one witnessed deed, so Mythos acts, Legends and aspect each see one deed instead of dozens.
`witnessScale` (Presence) applies at commit. A Rain/Sun competing-prayer card's "middle way" arms Rain at half radius
as today; with episodes on, its answer commits an episode at that radius.

### R2. Prayer choruses (Astra 2)

**Problem.** 467-759 prayers in a passive history, 816-1,098 for kind. Every town's answer adds a count and a
progress-year, which rewards clerical work.

**Rule** (`SituationsTune`, `SocietyTune::chorus`):

| Item | Value |
|---|---|
| Local need | Unchanged: `Settlement::prayer`, `maxPrayers` 6, the 15-year timeout and the per-town penalty (devotion -0.05, love -0.05). |
| Open | Yearly, by culture id, kinds in the order rain, heal, peace, bloom: the culture's live towns praying that kind become one **Plea** situation, `sub` = kind. Excluded: Victory and Sun, and towns named by an open CompetingPrayers card (`Card::settlement`, `ref[0..1]`). |
| Members | Up to `chorusMax` 8 towns by population, descending (ties by id). New praying towns join while the plea has fewer than 8 members; a member is never displaced by a larger newcomer. A member whose prayer ends stays a member (it still counts in `affected`); if it prays the same kind again while the plea is open, that is the same plea. `peak[i]` holds each member's peak population while a member, and `affected` = the sum of `peak`. **CompetingPrayers handoff:** `cards::yearly` pairs prayers after `situations::yearly` (2.4), so a member can land on a new CompetingPrayers card in the same year. On its next pass, Situations removes any member named by an open CompetingPrayers card (`ref[0]`, `ref[1]`) and subtracts its `peak` from `affected`. The card owns that prayer from then on, and an answer to it adds nothing to `reached`. |
| Severity (Astra) | members' people / the culture's people, + `chorusCapital` 0.25 if the capital is a member, clamped 0..1. |
| Reach | Situations' handler on `prayer:answered { id }` of a member adds that town's population to `reached`. |
| Close | When every member's prayer has ended, or `chorusYears` 15 after it opened. credit = min(1, reached / (`chorusFull` 0.5 x affected)). Outcome: Answered (credit > 0), Weathered (causes gone, no answer), Lost (timed out with credit 0). |
| Credit, once per chorus | Situations calls `society::chorus(w, culture, kind, -1)` at open (prayers begun +1) and `society::chorus(w, culture, kind, credit)` at close: answered += credit, science += `prayerScience` x credit x Factors.answeredGain, resolved choruses +1. In chorus mode, a town **beginning** a Rain, Heal, Peace or Bloom prayer adds nothing to `society_pray_<id>`, and Society's per-town `prayerAnswered` adds no count and no science. Without this, the Beloved-by-answered-prayers test (`answeredShare` of at least `answeredMin` prayers) would compare per-town beginnings against per-chorus credit. Victory and Sun keep the v3.1 per-town counting. Double faith, good +1 and `blessed` 20 years stay local. `society::memory` keeps reading `int(...)` of the summed counts, so fractional credit is truncated once per read, a loss below 1. Memory needs no other change: witness impulses are already weighted by each town's share of its people. |
| Cooldown | After closing, the same culture and kind waits `chorusCool` 25 years, unless the new severity is at least the closing severity + `chorusRise` 0.25. During the cooldown, towns still pray (local need) but form no row, and their answers count nothing. |

**Code.** `Situations.cpp` (T1): open, members, reach, close, cooldown. `Society.h` (T0) and `Society.cpp` (T2):
`void society::chorus(World&, int cultureId, PrayerKind, double credit)`, the gate `SocietyTune::chorus`, and
`Memory::choruses` (resolved, vars.num `society_chorus_<id>`, inherited by daughters like `pray` and `answer`).
`int situations::chorusOf(const World&, int sid)` (read-only).
**Save.** `RULE/SITU`. Counts stay in vars.num, saved in the v1 payload.
**Determinism.** No rng. Members in ascending id. The credit is a pure ratio.
**UI.** SPEC-UI 4b: the **Now strip `#pan-now` becomes "Risks and pleas" `#pan-risks`** (R3). A plea row reads "The
Velessan ask for rain: 4 towns, 31 % of their people" with a 15-year countdown in years and seconds. Answering arms the
power and flies to the largest member. Town labels keep their prayer glyph. The SPEC-UI 4 first-steps hint 4 fires on
the first plea.
**Mythos.** CompetingPrayers keeps its towns (excluded above). `prayerChance` and `expect` Factors still gate each
town. `society::answerPrayer` (a Keeper, a middle way) emits `prayer:answered` and counts toward reach. A plea opening
is a Notice in the shared ledger (R7).

### R3. Risks and pleas (Astra 3; the Situations umbrella)

**Problem.** The Now strip lists damage already happening. War is declared the year `v` crosses -0.5, and famine
starts losses the year `pop > K`. No current crisis is forecast, so 0 % are warned.

**Module.** `situations::yearly` (2.4) opens, advances and closes situations from state that the owners already keep.
It writes only `rules.situations` and `rules.chorusCool`, and calls `society::chorus` and `cards::attend` in id order.
Its handlers record `answered` when a `power:used` of one of the row's own answer powers (its catalogue column below)
lands within 2 x ang of the situation's cell, its focus settlement or a member town. For War and Ark it may also
land on the territory of any town of the peoples the row names, so striking an aggressor's capital or inspiring a
racing capital counts. A Rain on a warring people does not. They also record it when `war:ended` names its pair
after an Omen.

**Shared rules.** **stake** is the people at stake. The list sorts by stake (ties: severity, then id) and shows **at
most 4** rows, then "and N more". A risk is **listed only with at least 2 ok answers** (powers unlocked and the
target valid). Otherwise it is a `notice`: one quiet toast and no row. Pleas are exempt: a plea's two options are
answering and letting it lapse, and lapsing has a price. Stage changes into **Rising** are ledger *Notice* entries,
**Crisis** is a ledger *Crisis* entry, and Watch and Over add no attention.

**One matter, one interruption.** Two rules stop the ledger from counting the same danger twice:
- *Same year as an Epochal entry.* A stage change that comes in the same year as an Epochal entry about the same
  matter records no entry of its own and takes that entry's `loud`. The case today is War entering Crisis together
  with its pair's `war:declared`. The Ark row opens at Watch, which adds no attention, so it never clashes with
  `ark:started`.
- *Already foreseen by a Mythos card.* When the row's `omen` is set and a Warning card for that omen is open or was
  answered, the row's Rising entry is recorded **quiet**: the card already took the player's attention. Its Crisis
  entry follows the normal rule.

**Supporting sim rules.**

| Rule | Owner | Numbers (TUNE) |
|---|---|---|
| **Stores** | Economy | `EconomyTune::stores` gate. Fill +`storeFill` 0.5 a year while pop <= Keff, up to `storeCap` 4 (+`storeGranary` 4). While pop > Keff x (1 + famineSlack): stores -1 a year, no growth, no famine loss, famine count stays 0. Famine and losses (unchanged) begin at stores 0. Start: an awakened camp `storeAwaken` 4, a settler camp `storeSettlers` 2. A blessed town eats no stores (the v3 rule in `Economy.cpp`: a blessed town is never `hungry`). `civ::stores(w, sid)` read-only. Side effect: a famine rain prayer (`causeHolds`: famine >= `rainFamineAge`) can therefore begin only once stores are gone; drought prayers are unchanged. B41 re-measures metric 4 with this. |
| **`vSlow`** | Diplomacy | `DiplomacyTune::slowGate` gate, `slowTau` 20. Each year, after `v` moves: vSlow += (v - vSlow) / 20. War is declared on `vSlow < warV` (+ Factors.warV) instead of `v`; every other gate stays (the Omen's calm and `society::tense` keep reading `v`; while vSlow falls, v < vSlow, so v has already crossed the brink when vSlow does). Balance proposes `omenAge = 1` with it, so the Omen wakes in Bronze and the first wars have at least 2 answers. That is conditional on the B41 step-3 evidence (section 4). `society::vSlow(w, a, b)` read-only (v when off). |
| **Tension causes** | Diplomacy | `society::tension(w, a, b) -> Tension { v, vSlow, slope, causes[6] }` (read-only, no rng; the noise term is left out): border, envy, aggression, love gap, plague, zeal, each the size of its last-year term in v3.1 units, ranked. `slope` = (v - vSlow) / `slowTau`, the EMA's own rate per year. It is derived from current state, so no per-pair history is kept or saved. |

**Catalogue** (ages are world age; costs are today's table plus R1, R9):

| Kind | Watch | Rising | Crisis | Over (outcome) | ETA shown | Because line | Answers (cost) |
|---|---|---|---|---|---|---|---|
| **Hunger** (per town) | K < the 5-year projection, p5 = pop + 5 x max(0, growth); or a drought disaster covers the centre | pop > Keff x (1 + slack): eating stores | stores = 0, famine >= 1 | pop <= Keff 3 years running: Answered or Weathered; destroyed by famine or famine >= 8: Lost | Rising: year + stores; Watch: years to cross K + stores | drought / tired land (fert < 0.4) / many mouths (growth > 2 %) / burned fields (scorch > 0.3) | Rain episode (25); Heal (40: blessed 30 years, K x 1.2, no famine years); Bloom episode (30, Bronze+); Bless: Prosperity (80, Mythos phase 2) |
| **War** (per pair at peace, in contact) | vSlow <= -0.25 and falling (v < vSlow) | vSlow <= -0.40 (the brink, `society::tense` reach) | war declared (same id) | peace, or vSlow >= -0.15 at peace: Answered if an answer touched it, else Weathered; a town captured or razed: Lost | year + (vSlow - warV) / max(0.002, -slope), at most 200 | top two `tension` causes ("a long frontier; the Oru envy your favour of the Vai") | Omen of Peace (100, Bronze+); Messenger with the peace edict (120, Iron+); Even the favour: Heal (40) or Inspire (60) on the envious side's capital (only when envy is a top-two cause); Strike the aggressor: Earthquake (60) on its capital (fewer people, so the 0.6 strength gate may fail; it blames you) |
| **Plague** (per outbreak: ill towns joined within `plagueHops` 8) | a live, non-immune town's exposure >= 0.20, exposure = 1 - prod over ill j within 8 hops of (1 - plagueSpreadP x plague_j) | 2 towns ill, or the capital's exposure >= 0.40 | 3 or more ill, or the capital ill | none ill for 5 years: Answered if a Heal touched it, else Weathered; a town dead of plague: Lost | year + 1 / (the highest exposure) | "a fever in X", "the caravans from Y" (a trade route to an ill town) | Heal the source (40: cure + 30 years' immunity); Heal the most exposed neighbour (40: it is spared); Ward Heal (80, R9: every town within 0.08 rad); Bless: A healer's gift (120, phase 2) |
| **Sea** (per town; world age Industry+, 100-year sea trend > 0) | margin(100) < 0, margin(t) = elev[centre] - (seaLevel + trend x t) | margin(30) < 0 | elev - seaLevel < 0.04 | margin(100) >= 0.15: Answered (raised) or Weathered (the trend fell); destroyed by `sea`: Lost | year + (elev - seaLevel) / trend | "the sea rises 0.4 a century; the Vai's engines 54 %" (`climate::attribution`) | Raise Land under the town (R8: 6/s at x1 until safe); Bloom restoration episode (30, R10); Inspire the top emitter's capital (60: Space sooner, half the smoke); Strike the top emitter's largest factory town (Earthquake 60: a research setback) |
| **Fire** ("Tinder", per town) | a drought disaster covers 6 or more territory cells with veg >= 0.5 and dry >= 0.5, where dry = sat(dryA - dryB x (moist + wet)) is Life's ignition term (`LifeTune::dryA`, `dryB`) | a burning cell within 3 hops of those cells | fire on its territory | the drought ends and no fire for 2 years: Answered (a Rain episode over it) or Weathered; 30 % or more of those cells burned: Lost | the drought's end | "the forests of X are tinder" | Rain episode over the forest (25); A firebreak: Lightning (15) on an unowned edge cell (a controlled burn; the forest is lost, ash enriches the soil later) |
| **Ark** (one per world, Space) | an Ark started | two or more Arks, or the leader >= 0.6 | (none) | launch: Answered if the god pushed (R5) | the leader's year + (1 - ark) / rate | `society::arkRace` lanes and their moods | Inspire a racing capital (60: +0.02); Leave-taking: Consecrate (150: +0.05 once); Strike a racing capital (Earthquake 60: -0.20; Meteor 150: -0.16) |
| **Plea** (R2) | (none) | at open | (none) | R2 | 15 years from open | "4 towns, 31 % of their people" | the matching power on the largest member (Rain or Bloom episode; Heal; Omen of Peace); Bloom also answers a prayer for bread (SPEC-SIM 3.8) |

stake: Hunger and Sea, the town's people; War, the smaller side; Plague, the people of ill towns plus towns with
exposure >= 0.2; Fire, the town; Ark, the world; Plea, the members. severity = stake / world people x 1.5 for a
capital, clamped 0..1 (a Plea keeps Astra's value).

**Code.** `Situations.h` (T0): `SituationsTune` (thresholds above; `on`; `listMax` 4; caps 24 / 32).
`attach`, `rebuild`, `yearly`. `std::vector<int> list(const World&)`: listed open ids, sorted.
`const Situation* find(const World&, int id)`. `std::vector<Answer> answers(const World&, int id)`, with
`Answer { const char* power; int cell, settlement; double cost; bool ok; const char* effect; WordOpts word; }`.
`double seaMargin(const World&, int sid, double years = 100)`. `int chorusOf(const World&, int sid)`.
`int crises(const World&)`. All read-only except attach, rebuild and yearly. `Situations.cpp` is T1's. Stores are
T3's; `vSlow` and `tension` are T2's.
**Save.** `RULE/SITU`, `STOR`, `VSLO`.
**Determinism.** No rng and no float accumulation across years except `vSlow`. ETAs are recomputed each year from
state.
**UI.** SPEC-UI 4b `#pan-risks` replaces `#pan-now` in the same place and with the same icons. A row shows the icon,
a title in the people's names, 3 stage pips (Watch, Rising, Crisis), a countdown ring in years and seconds at the
current speed, one because line, up to 3 answer buttons (icon, cost, effect words from `effect`; a click arms the
power and flies there) and a quiet "Let it be" that folds it. Settlement labels gain the Watch / Rising glyph. Sea
keeps the existing "sea rising" glyph. The inspector gets a forecast line ("feeds 360, 412 live here: hungry in about
4 years (stores 4)").
**Mythos.** Risks never open cards. When Omens (phase 2) hold a foreseen event of the same kind on the same town
(drought for Hunger, plague for Plague), the row records `omen` and says "foreseen". The Warning card stays Mythos's.
A Bless answer passes its `WordOpts`. Risk stage changes share the card ledger (R7).

### R4. Awe reserve bands (Astra 4)

**Problem.** Passive is at 95 % of the cap or more in 91-92 % of years and at the cap in 88-89 %. Kind, chaotic,
cruel and builder are under 90 % in 91-100 % of years. Saving is never a choice.

**Rule** (`SocietyTune::reserve`):

| Item | Value |
|---|---|
| Idle | idle = year - rules.num `powers_lastSpent` (missing: the year history began, so a god who never spends is idle from year 0), written by Powers on every successful `use` and every `pay` > 0 while `reserve` is on (R4 replaces "witnessed deed" with "Awe spent", because every deed costs Awe and the Word powers have no witness). |
| Soft holding cost | While idle >= `reserveIdle` 20 and awe > `reserveSoft` 0.60 x aweMax (**Fading**): the year's income is **not added**, and awe -= `reserveDecay` 0.004 x aweMax, never below 0.60 x aweMax. The decay is net. With income still added, it would not bring passive down: late income is about 2.8 a year (Balance: 2.0 + 0.17 ln(1 + S)), while 0.004 x aweMax is 1.2 at the Stone cap of 300 and 2.4 at 600. Awe would then still climb to the cap whenever aweMax is below about 700 (Stone to Iron, which is most of a passive history's first 1,500 years; aweMax at year 1,000 is 450-600 in sim-findings-2), and metric 10 could not pass. A full orb falls to 60 % in 100 idle years and then hovers there. |
| Hoarding | The v3 hoarding drift (`hoardDrift` 0.015 of devotion a year, `hoardFloor` 0.3, both from Balance.cpp) applies while awe >= `hoardShare` 0.95 x aweMax as today, **and also** while Fading (the new flag `hoardIdle`, true by default when `reserve` is on). So passive keeps paying about the devotion cost it pays today, and its verdict does not move. B41 checks this on 8/8 passive seeds (ending mood and end devotion within ±0.02). |
| Recovery band | While awe < `reserveLow` 0.15 x aweMax at the start of the year: income x (1 + `reserveBonus` 0.25). `aweIncome` returns the boosted figure. |
| Unchanged | aweMax, the age caps, Consecrate and Wonder purchases. Overflow buys nothing. |

**Code.** `Society.h` (T0) and `Society.cpp` (T2): the fields above, plus
`ReserveState society::reserve(const World&)` returning `{ band: Low | Mid | High | Fading, idle }` (read-only).
`Powers.cpp` (T4) writes `powers_lastSpent`.
**Save.** `RULE/RNUM` (`powers_lastSpent`). No new sub-block.
**Determinism.** Arithmetic only, once a year.
**UI.** SPEC-UI 4 Awe orb: "quiet power is fading" (Fading) or "faith is rallying" (Low) under the number, and a soft
band mark at 60 %.
**Mythos.** Mythos sinks (Word 30-90, Bless 80-120, Look 20) are spends, so they reset idle. SPEC-MYTHOS 5.1 rule 4
(passive and prayers at the cap fall, never rise) is satisfied by construction. Card answers that cost nothing do not
reset idle.

### R5. Ark race levers (Astra 5)

**Problem.** The Ark takes about 250 years (about 2 minutes). During it no deed enters memory and no normal power
helps or hinders, so the finale is watched rather than played.

**Rule** (`SocietyTune::arkLevers`):

| Item | Value |
|---|---|
| Memory while building | In `commitMemory`, a culture building its Ark remembers deeds at `arkMemoryWeight` 0.25 while ark < `arkSeal` 0.8, and at 0 from 0.8 (sealed). This replaces `arkNoMemory` when on. Favour sums (envy) stay whole. |
| Inspire | Inspire on a capital building its Ark adds `arkInspire` +0.02 (Guiding Light: +0.04), once per `arkInspireGap` 25 years per culture (12.5 with Guiding Light). |
| Wrath | A divine research setback on an Ark capital also removes setback / `arkSetbackDiv` 250: lightning 0.004, plague 0.08, volcano 0.12, meteor 0.16, quake 0.20 (v3.1 `setbackQuake` 50). Never below 0. At most `arkSetbackCap` 0.5 in total per culture per Ark: past that "they build in secret". |
| Leave-taking | Consecrate on an Ark capital performs the Leave-taking instead of a temple or Wonder: +`arkRite` 0.05, once per Ark. Witnessed with good 0 and faith 0.10 (no verdict bonus), with a chronicle commitment. |
| Race | `society::arkRace(w) -> std::vector<ArkLane { culture, settlement, ark, rate, Mood ifLaunched }>`, sorted by ark descending (ties by id), read-only. |

**Code.** `Society.h` (T0) and `Society.cpp` (T2): the fields above, plus
`double society::arkPush(World&, int sid, double delta, ArkBy)`, which returns the change applied (0 when refused or
off) and emits `ark:push`. `Powers.cpp` (T4): Inspire and Consecrate call it, and so does the existing `setBack`
helper (lightning, quake, plague, volcano; a meteor's setback comes only at impact) for a harmed Ark capital, with
`ArkBy::Wrath`. `Disasters.cpp` (T4, one call): a god's meteor impact (`!nat && wantWitness`) calls it beside its
existing `civ::strikeKnowledge`. One act pushes at most once per culture: the act's harmed list is deduplicated by
culture, as `strikeKnowledge` already does. `civ::strikeKnowledge` itself is unchanged, because Doctrine's burning
also calls it (2.1).
**Save.** `RULE/ARKL`. `settlement.ark` is in the v1 payload.
**Determinism.** Pushes happen in the acting call; cooldowns are year comparisons.
**UI.** SPEC-UI 4 Ark bar: the two leading bars with culture dots and "the Vai would leave you Beloved; the Oru,
Feared". Clicking either flies there. When sealed, the caption reads "their memory is sealed". The Ark risk row (R3)
carries the answers.
**Mythos.** The Bearers' Stars charge (Mythos M5c) multiplies `rate`, and pushes are additive. A sealed memory also
seals Mythos `society::remember` calls, through the same `commitMemory`. Legacy is unaffected. A Leave-taking is not
a Rite (3.13) and never touches Doctrine.

### R6. Delayed verdict words (Astra 6)

**Problem.** Today kind reads Beloved before year 20 on 8/8 seeds (curious and blessonly on 7/8, chaotic on 4/8),
before any history exists.

**Rule** (`SocietyTune::matureWords`). `Memory::mature` is true when year >= `matureYear` 250 **and** at least one of
these holds: grace + wrath >= `matureMemory` 1.0; resolved choruses >= `matureChoruses` 10; silent >= `silentYears`
600 (the god's absence is the history); the culture is building its Ark. Before that, the HUD word is **Unproven**,
with two lean phrases from the verdict's sign and the share remembered ("they are waiting to know you" while grace +
wrath < 0.1; then "they are growing fond" or "they are growing wary"; "your deeds are becoming a story" once grace +
wrath >= 0.5). The verdict, the mood and the ending are
unchanged: they still use the full memory from year 0.

**Code.** `Society.h` (T0) and `Society.cpp` (T2): `Memory::mature`, `Memory::choruses`, the tune fields.
**Save.** None (derived).
**Determinism.** Read-only derivation.
**UI.** SPEC-UI 4 top bar regard word, the remember card, and the Ark caption (always mature once building).
**Mythos.** Aspect (Parent, Ruler, Mystery, Threat) is a separate Faith-tab reading and is not gated. Epithets stay
the people's words. The Legacy page appears only at the ending.

### R7. One attention budget (Astra 7; shared with the director)

**Problem.** In Industry there are 49 (passive), 72 (kind) and 82 (prayers) raw moments per real minute: one or more a
second. The v3.1 ratio target passes even while both ends are overloaded.

**Rule** (`CardsTune::attention`). The director keeps one **ledger** of attention entries:

| Class | What | Loud |
|---|---|---|
| **Epochal** | `world:age`, `culture:born`, `culture:died`, `wonder:built`, `ark:started`, `ark:launched`, `war:declared`, `war:ended`, `settlement:destroyed` of a city or larger (tier >= 3), `meteor:impact`, `volcano:start`, `nuke`, `tsunami:start` (one per wave; `tsunami:hit` carries no wave id) | always, and always counted |
| **Crisis** | a situation entering Crisis | if there is room, or always when stake >= `attnBigShare` 0.05 of the world's people |
| **Card** | a budgeted card opening | an opening **needs room** (otherwise the director waits a year; a maxGap-forced opening waits too). After `attnHold` 60 years of waiting for room, the opening goes ahead and is recorded **quiet**, so a long Industry burst can delay a card but never starve the director. |
| **Notice** | a situation entering Rising; a plea opening | only if there is room; otherwise it is recorded **quiet** (the row appears with no toast and no slow-down) |

Room means fewer than `attnMax` 4 loud entries in the last `attnWindow` 20 years (10 s), and fewer than `crisisMax` 3
open Crisis situations for Card and Notice entries. The window is (year - 20, year]. The ledger keeps at most 64
entries; past 64 it drops the oldest **quiet** entry first, so the loud entries in the window always survive and
room is always counted correctly. Card toasts stay at most one per 20 s (SPEC-MYTHOS 2.4). Raw sim events are
unconstrained. Epochal entries and big Crises bypass the cap by design, so "<= 4 loud in any 10 s" (metric 18) can
be broken by a burst of great events. The `--rules` report counts how often that happens, by class.

**Code.** `Cards.h` (T0) and `Cards.cpp` (T5): `Room { int loud, free, crises; }`; `Room cards::room(const World&)`
(read-only); `bool cards::attend(World&, AttnClass, uint8_t kind, int ref)` (records, returns loud);
`void cards::attach(World&)` (records Epochal entries from the bus, only with `attention` on); and the director's
opening gate. `Situations.cpp` (T1) calls `attend` for each Rising, Crisis and plea opening, except the cases that
"one matter, one interruption" (R3) folds into an existing entry. It stores `loud` in the situation and its event.
**Save.** `RULE/ATTN`.
**Determinism.** Year-window counts, no rng. Handler entries are records.
**UI** (client, later): auto-pace slows only on a loud Crisis or an Epochal entry and holds 1x until the row is folded
or 20 s pass. Quiet rows fade in silently. SPEC-UI 4 toasts and banners and 4b `#pan-risks` read `loud`. The settings
row "slow down for great events" maps to Epochal. Repeated battles, town famines, trade routes, fires and building
completions fold into one feed line per 10 s on the client.
**Mythos.** This is the shared budget of SPEC-MYTHOS 0.1 rule 3: card openings count as entries and wait for room, so
the 15-28 cards per history target (5.4) is unchanged in kind and re-measured with the ledger on. Mythos notable
entries (at most 1.2 per minute) remain chronicle lines, not ledger entries.

### R8. Climate-rescue pricing (Astra 8)

**Problem.** Raise Land on owned land costs x4 and reads as harm (good -0.5), so lifting a town the people's own sea
is drowning is punished as cruelty.

**Rule** (`PowersTune::rescue`). In history, a Raise Land press whose brush centre is in the territory of town S with
a Sea situation at Rising or Crisis:
- costs x1 (6/s) instead of x4 while `situations::seaMargin(S, 100)` < `rescueSafe` 0.15;
- the first time in this press that S's centre has gained `rescueGain` 0.03, if S's last credited rescue is at least
  `rescueEvery` 100 years ago, triggers one witness of S with good `rescueGood` +0.6 and sets `rescued[S] = year`;
- every other quantum below the safety line is witnessed with good 0;
- above the line, the v3.1 rule is back: x4 and good -0.5.

Lower Land keeps its v3 drown guard and harm rule.

**Code.** `Powers.h` (T0) and `Powers.cpp` (T4): the fields above, and
`RescueTip powers::rescueTip(const World&, int cell)` returning `{ sid, margin, gainToSafe, credited }` (read-only).
`situations::seaMargin` is T1's.
**Save.** `RULE/EPIS` (`rescued`).
**Determinism.** The margin is read at the call and the press accumulator is transient.
**UI.** SPEC-UI 4 hover tip with Raise armed: "Lift Pell 0.11 to safety; they will see it as rescue (6 Awe/s)". The 4b
sea glyph, and the Sea row's Raise answer.
**Mythos.** A rescue is a witnessed grace deed (aspect Parent leans and Legends may tell it). Nothing else.

### R9. Gifts of the late ages (Astra 9)

**Problem.** Atomic and Space upgrade a power automatically (Monsoon, Star-fall). They add spectacle but no choice.

**Rule** (`PowersTune::gifts`). From the year the world reaches the age (`w.age`, as `powers::upgrade` reads it), the god may choose **one gift per age** for `giftCost` 300 Awe,
irreversibly. The other option is gone for this history:

| Age | Option 1 | Option 2 |
|---|---|---|
| Atomic | **Monsoon** (today's Rain upgrade: brush up to 0.25 rad, x0.5 cost) | **Ward**: Heal costs `wardCost` 80 and also gives `wardYears` 30 years of immunity (and a cure) to every live town within `wardAng` 0.08 rad |
| Space | **Star-fall** (today's Meteor upgrade: crater x1.6, a crater lake) | **Guiding Light**: Inspire costs `guideCost` 100, gives +`guideScience` 4 progress-years and pushes an Ark by +0.04 every 12.5 years (R5) |

With gifts on, `powers::upgrade` returns Monsoon or Star-fall only when they are chosen. An unchosen age has no
upgrade.

**Code.** `Powers.h` (T0) and `Powers.cpp` (T4): `kGifts` (data table),
`bool powers::chooseGift(World&, int slot, int option)` (pays through the normal spend path; refused before the age,
when already chosen, or when the god cannot pay), `int powers::gift(const World&, int slot)`, the cost and effect
reads, and the `gift` event.
**Save.** `RULE/EPIS` (`gifts`).
**Determinism.** A player call only. No rng.
**UI.** SPEC-UI 4: the age card for Atomic and Space shows the two gifts and a "Choose" button. The upgraded button
takes its new name, the new-unlock pulse fires, and the palette tooltip states the price and that the choice cannot
be undone.
**Mythos.** Not a card (player-initiated; the age itself is the Epochal entry). Distinct from Mythos Revelations
(`reveal`, 3.25). The chronicle records the choice (T7).

### R10. Climate agency (Astra 10)

**Problem.** The climate targets show that warming happens (passive peaks at +3.61 to +4.14 °C, sea +0.25 to +0.39),
not that a player can understand it or change it.

**Rule.**

| Item | Value |
|---|---|
| Gate | `ClimateTune::attribution` (new, false). With it off, nothing below is written or sampled (D5). |
| Yearly emission per culture | Economy writes rules.num `civ_co2y_<id>` (this year's factory emission, after zeroing every live culture's key at the start of `civ::yearly`) beside the existing cumulative vars.num `civ_co2_<id>`. `climate::emit` is the only CO2 source today (`Economy.cpp`), so gross = the sum of the `civ_co2y` keys. |
| Uptake | Climate writes rules.num `climate_uptake`: this year's removal in `climate::yearly`, the only place uptake happens. |
| History ring | Climate samples `{ year, co2, anomaly, seaLevel, uptake }` every `histEvery` 10 years, keeping `histLen` 11 (100 years). |
| Attribution | `climate::attribution(w) -> Attribution { topEmitter, std::vector<double> emitYear (per culture id), gross, uptake, anomalyTrend, seaTrend (per year, over the ring), projAnomaly100, projSea100 }`, read-only. It reconciles: co2(now) - co2(last year) = gross - uptake, within 1e-9 unless clamped. |
| Restoration | A Bloom episode (R1) whose brush covers land at least 80 % unowned, with mean veg / capacity < 0.6, is a **restoration**: 30 years, and `life::restore(w, c, ang, amount, fertMul = 2)` doubles fertility recovery (vegetation as Bloom). Climate's uptake becomes co2 x uptake x (0.5 + coverVeg x uptakeOf + (`restoreUptake` - 1) x R), where R = the summed veg of the cells inside active restoration episodes / n, and uptakeOf is the existing `co2Uptake` Factor term. With `restoreUptake` 1 (neutral), Climate reads no episode cell. |
| Kept | Space-age factories at x0.5 emission (SPEC-SIM 3.3), so Inspire is a legible route to cleaner engines. |

**Code.** `Climate.h` (T0) and `Climate.cpp` (T3): the fields above, plus `attribution`. `Economy.cpp` (T3):
`civ_co2y_<id>`. `Life.h` (T0) and `Life.cpp` (T3): `restore`. `Powers.cpp` (T4): the restoration test at commit.
**Save.** `RULE/CLIM`; the two keys are in `RULE/RNUM`.
**Determinism.** Ring endpoints only. Climate draws no rng (as today).
**UI.** SPEC-UI 4 anomaly and sea readouts, tooltip: "+3.1 °C by about year 3,400 at this rate; the Vai's engines
54 %; faster learning means more smoke now, less later". The Sea row's because line.
**Mythos.** The World rite's `co2Uptake` Factor multiplies on the same path. Old growth (`wild`, 3.22) is unaffected;
restored cells grow old growth by the normal rule. The LandClaim card may later ask for a restored forest's land, as
for any forest.

### R11. Score neutrality (Astra 11)

**Problem.** `Sim::score` multiplies by mood (beloved x2, parting x1.6, forgotten x1.4, feared x1.3), so a beloved
policy is the top scorer on 8/8 seeds today (builder on 6 seeds, kind 1, atomic-kind 1).

**Rule** (`SimTune::scoreNeutral`). total = round(**Life** + **Continuity** + **Wonder**), where
- Life = scorePop x log10(1 + peakPop) + scoreLife x cover.veg;
- Continuity = scoreYear x min(years, scoreYearCap) + scoreCulture x cultures alive;
- Wonder = scoreAge x ages + scoreWonder x wonders.

`Score::parts` gains `life`, `continuity`, `wonder`, the descriptive `mercy` (god.mercy), `wrath` (god.wrath) and
`memory` (grace + wrath of `memory(-1)`), plus `mult` = 1. Mood is never read, so the Quiet (x0.6) and Ash (x0.4)
multipliers go too: a silent or burned world scores lower only through its Life and Continuity lines. Each ending
keeps its own non-ranked seal.

**Code.** `Sim.h` (T0) and `Sim.cpp` (T5). **Save.** None.
**Determinism.** Read-only.
**UI.** SPEC-UI 5 ending statistics and the planet card: three numbered lines, a "Hand" line in words ("mostly
mercy") and a "Memory" line in words ("long remembered"). The Mythos Legacy title sits under the mood seal. There is
no single "grade".
**Mythos.** SPEC-MYTHOS 4's Legacy is already non-ranked and stays the title. No vow multipliers exist (gameplay-v4
3.7 is deferred).

---

## 4. Build tracks

**Gate.** T0 starts only when Mythos v1 has merged (M7-M9, B1 and V1; markers in `docs/handoff/done-ue/`) and no other
workflow owns `Unreal/Source/Demiurge/Sim`, `Unreal/SimTests` or `Unreal/Tools`. The house rules apply: at most 6
agents at once; Opus (high) for the complex tracks (T0, T1, T2, T4, B41, V41) and Haiku (xhigh) for the mechanical
ones (T3, T5, T6, T7). Sim tracks test with `SimTests/run.ps1 <key>`; only T0, B41 and V41 run `Unreal/build.ps1`.
Markers are `docs/handoff/done-ue/rules-v41-<track>.built.txt`. **Headers freeze after T0**: a missing declaration is
reported, and the lead adds it between waves.

### T0 `rules-contracts` (1 agent; effort M)

Owns `Sim/RulesTypes.h`, `Sim/Situations.h`, a neutral `Sim/Situations.cpp`, `SimTests/stubs/Situations_stub.cpp`,
the `StubHooks.h` rows, the `test_link.cpp` count, the `run.ps1` key, `State.h` / `State.cpp` (`World::rules`,
`state::rulesDefault`, RULE written only when not default, `extensionsDefault` requiring `rulesDefault`),
`Core/Events.h` / `.cpp` (four rows), the new cases in `SimTests/test_save.cpp` and `test_core_events.cpp`,
additions-only declarations in `Society.h`, `Powers.h`, `Civ.h`, `Climate.h`, `Life.h`, `Cards.h` and `Sim.h`
(every field and function in section 3, every gate false), `docs/SPEC-SIM.md` section 6, and a one-line note under
SPEC-MYTHOS 0.3 and 2.2 ("amended by SPEC-RULES-V41 D3: situations report to the director's ledger;
`CardKind::Situation` stays unused").

**What is new.** Except where section 3 names a symbol as existing (for example `society::witness`, `commitMemory`,
`causeHolds`, `prayerAnswered`, `civ::found`, `civ::rebuild`, `civ::strikeKnowledge`, `powers::use`, `powers::pay`,
`powers::upgrade`, `setBack`, `rainBrush`, `life::bloom`, `Settlement::prayer`, `arkNoMemory`, `hoardShare`,
`hoardDrift`, `hoardFloor`, `omenAge`, `famineSlack`, `plagueSpreadP`, `plagueHops`, `factoryCo2`, `spaceCo2Mul`,
`ownedCostMul`, `sculptHarm`, `monsoonMaxAng`, `monsoonCostMul`, `starfallDepth`, `setback*`, `inspireScience`,
`offTerritoryMul`, `SimTune::score*`), every struct, field, flag and function in section 3 is **new** and T0 declares
it. That includes `Memory::mature`, `Memory::choruses`, `hoardIdle`, `WitnessOpts::episode`, `ReserveState`, `Tension`, `ArkLane`, `Room`,
`Answer`, `RescueTip`, `Attribution`, `EconomyTune::stores` / `storeFill` / `storeCap` / `storeGranary` /
`storeAwaken` / `storeSettlers`, `DiplomacyTune::slowGate` / `slowTau`, `ClimateTune::attribution` / `histEvery` /
`histLen` / `restoreUptake`, `CardsTune::attention` / `attnMax` / `attnWindow` / `crisisMax` / `attnBigShare` /
`attnHold`, and `SimTune::scoreNeutral`.

| SimTest / check | Setup | Expected |
|---|---|---|
| `run.ps1 link`, `run.ps1 all` | Situations neutral | pass; `Unreal/build.ps1` links |
| `phase0-guardrail.ps1` | every v4.1 flag off | 32 of 32 hashes match |
| `save: a default RulesState writes no RULE block` (test_save) | a fresh world, every v4.1 flag off | the bytes equal the pre-v4.1 serializer's for the same world; `extensionsDefault` unchanged |
| `save: a v2 blob without RULE loads` (test_save) | a version-2 blob written by the pre-v4.1 serializer, kept as a new fixture | loads with `rules` default; with `stores` on after `sim::use`, stores = cap; with `slowGate` on, vSlow = v after one year; situations empty |
| `save: a populated RulesState round-trips byte for byte` | 3 situations, 2 episodes, gifts {1, 2}, 11 climate samples, 20 ledger entries | serialize -> deserialize -> serialize is identical |
| `save: RULE naming a missing settlement is refused` | situation town 999 | nullptr, why set |
| `save: a RULE index vector of the wrong length is refused` | `stores` one longer than the settlements | nullptr, why set |
| `save: an unknown RULE sub-block is skipped` | extra `"ZZZZ"` sub-block | loads, others intact |
| `core events: situation, episode, ark:push, gift round-trip` (test_core_events) | one of each | payload fields and string kinds round-trip |

### Phase 1: six parallel tracks (disjoint files), then T7

| Track | Owns | Builds |
|---|---|---|
| **T1 `situations`** | `Sim/Situations.cpp`, `SimTests/test_situations*.cpp` | the R3 catalogue, R2 plea bookkeeping, `answers`, `seaMargin`, ledger calls |
| **T2 `society-diplomacy-v41`** | `Sim/Society.cpp`, `Sim/Diplomacy.cpp`, `SimTests/test_society_v41*.cpp`, `test_diplomacy_v41*.cpp` | chorus credit (and chorus-mode prayer counting), mature, reserve, Ark levers, `vSlow`, `tension`, `causeHolds` x episodes, the unpaced episode witness (`WitnessOpts::episode`) |
| **T3 `economy-climate-v41`** | `Sim/Economy.cpp`, `Sim/Civ.cpp`, `Sim/Climate.cpp`, `Sim/Life.cpp`, `SimTests/test_economy_v41*.cpp`, `test_climate_v41*.cpp`, `test_life_v41*.cpp` | stores, `civ_co2y`, the climate ring, attribution, restoration uptake, `life::restore` (`civ::strikeKnowledge` is **not** changed) |
| **T4 `powers-v41`** | `Sim/Powers.cpp`, one call in `Sim/Disasters.cpp` (2.1), `SimTests/test_powers_v41*.cpp` | episodes and weather, restoration test, rescue pricing, gifts, Ark hooks (Inspire, Consecrate, the wrath push in `setBack` and at a god's meteor impact), `powers_lastSpent` |
| **T5 `director-sim-v41`** | `Sim/Cards.cpp`, `Sim/Sim.cpp`, `SimTests/test_cards_attention*.cpp`, `test_sim_v41*.cpp` | ledger, room, card gate, attach, schedule and attach lines, neutral score |
| **T6 `rules-tools`** | `Tools/simrun/*`, `SimTests/test_rules_integration*.cpp` | policies, `--rules`, the tracking table, attaching the v4.1 modules when it turns flags on (2.4) (can start on stubs at once) |
| **T7 `chronicle-v41`** (after a lane frees) | `Sim/Chronicle.cpp`, `SimTests/test_chronicle_v41*.cpp` | one w2 line per situation at Crisis and at its end (through the budget); chorus answered ("the Velessan asked for rain in four towns, and it came"); gift (w2); Leave-taking (w2); `ark:push` by wrath (w2, blamed); the lint |

**T1 acceptance** (`test_situations.cpp`, `test_situations_chorus.cpp`; synthetic L 4 worlds; T1 sets `rules.stores`
and `rules.vSlow` directly, with collaborators stubbed):

| Test name | Setup | Expected |
|---|---|---|
| `situations: a drought over a village opens Hunger at Watch within 1 year` | village pop 200, K 220, stores 4; a Drought (power 1) on it | 1 Hunger, stage Watch, stake 200, eta > year |
| `hunger: Rising while stores > 0, Crisis at 0` | pop 300 > K 200 x 1.05; stores 4, then 3, 2, 1, 0 (one a year) | Rising years 1-4; Crisis in year 5; eta = year + stores in Rising |
| `war: vSlow -0.30 falling opens Watch, -0.42 Rising, a declaration Crisis, an Omen peace Over Answered` | 2 cultures in contact; v held at -0.6 (so vSlow falls); vSlow series -0.20, -0.30, -0.42; `war:declared`; then `power:used` peace + `war:ended` | stages in that order; one id throughout; one ledger entry for the Crisis year (the Epochal `war:declared`, not a second Crisis entry); outcome Answered |
| `plague: exposure 0.24 opens Watch; a third ill town Crisis; Heals end it Answered` | ill source plague 0.8 at 4 hops (0.3 x 0.8 = 0.24) | Watch for the neighbour; Crisis at 3 ill; Over 5 years after the last cure |
| `sea: margins 0.35 / 0.10 / 0.03 with a +0.004 a year trend` | Industry; climate ring rising 0.4 a century | Watch, Rising, Crisis; `seaMargin(100)` = elev - (sea + 0.4) |
| `fire: tinder under a drought, a fire 3 hops away, over at the drought's end` | 8 owned cells veg 0.6, dry 0.6; a Drought; `fire[c]` set 3 hops off | Watch, Rising, Over Weathered |
| `answers: every listed risk has >= 2 ok answers; fewer is a notice` | Stone-age plague (Heal the source, Heal the neighbour); a Bronze war with `omenAge` -1 and no envy cause (Quake only) | plague listed with 2 answers; the war is not listed and toasts once (ok = unlocked and a valid target; affordability is shown, not required) |
| `chorus: 4 praying towns of one culture make one plea; capital +0.25` | 4 rain prayers of 100, 100, 50, 50 of 600 people, capital included | 1 Plea, 4 towns, severity 0.5 + 0.25 = 0.75 |
| `chorus: answering 60 % of affected gives credit 1, called once` | 2 answered towns with 180 of 300 | `society::chorus` hook: begun once, credit min(1, 180 / 150) = 1 once |
| `chorus: 25-year cooldown unless severity rises by 0.25` | close at 0.4; reopen try at +10 with 0.5, then 0.7 | first refused, second opens |
| `situations: read-only and rng-free` | 200 years of a passive fixture with `on` and `ClimateTune::attribution` on | `state::serialize(w, out, why, 1)` bytes and `w.rng.s` equal an all-off twin's |
| `situations: same calls, same list; save/load mid-Crisis` | 2 runs; save at a Crisis | identical `rules.situations`; the loaded world re-serializes byte for byte; two runs loaded at the same point end on the same `sim::fingerprint` |
| `situations: a CompetingPrayers card takes its town out of a plea` | a rain plea of 3 towns; a card opens naming member 2 | next pass: 2 members; member 2's later answer adds nothing to reach |
| `situations: a foreseen danger interrupts once` | an open Warning card (Omens) for a drought on a town; its Hunger row reaches Rising | `omen` set; the Rising entry is quiet |
| `situations: a War answer may land on either capital` | a War row at Rising; Earthquake on the aggressor's capital | `answered` true |
| `situations: yearly cost` | L 6, 120 settlements, 6 cultures, 200 years | reported (not asserted) in SimTests; simrun `--rules` (Release) shows mean <= 0.15 ms |

**T2 acceptance**:

| Test name | Setup | Expected |
|---|---|---|
| `society v41: chorus mode moves counts and science to the chorus` | chorus on; 3 towns begin rain prayers; witness answers them; then `chorus(c, Rain, -1)` and `chorus(c, Rain, 0.6)` | per-town beginnings add 0 to `pray`, per-town answers add 0 to `answer` and science; the chorus adds 1 to `pray`, 0.6 to `answer` and 0.6 x prayerScience |
| `society v41: mature needs year 250 and a history` | grace 3 at year 249; grace + wrath 0.9 and 9 choruses at 260; 10 choruses; silent 600; Ark begun | false, false, true, true, true; mood unchanged in every case |
| `society v41: idle Awe fades 0.004 x aweMax a year to 60 %` | aweMax 600, awe 540, live towns giving income about 2.8 a year, `powers_lastSpent` 25 years ago | 537.6, 535.2 ... (no income added while fading), never below 360; a spend stops it and income returns |
| `society v41: recovery band x1.25 below 15 %` | awe 80 of 600 | income = 1.25 x the v3.1 income |
| `society v41: hoarding drift applies while fading` | the above, devotion 0.5 | devotion x (1 - 0.015) a year, floor 0.3 |
| `society v41: Ark memory 25 % before 0.8, sealed after` | grace impulse 1 at ark 0.5 and at 0.85 | +0.25, then +0 |
| `society v41: arkPush rules` | ark 0.9; Inspire twice within 25 years; four quakes; Leave-taking twice | +0.02 then 0; -0.20, -0.20, -0.10, then 0 (0.5 total cap); +0.05 once; never below 0; one `ark:push` per applied push |
| `society v41: arkRace lists the leaders in order` | capitals at 0.62 and 0.40 | lanes in that order with moods |
| `society v41: no rain prayer inside a rain episode` | drought over a town inside `episodeAt` | no prayer begins |
| `diplomacy v41: vSlow is an EMA with tau 20` | v held at -0.6 from vSlow 0 for 20 years | vSlow = -0.6 x (1 - 0.95^20) = -0.3849 (+-1e-9) |
| `diplomacy v41: the slow gate declares on vSlow only` | v -0.6 for 5 years, vSlow -0.3 | no war; war on the year vSlow < -0.5 |
| `diplomacy v41: tension is read-only and ranked` | a pair with border 0.8, envy 0.01 | causes border > envy; rng state unchanged |
| `diplomacy v41: the War slope is derived` | vSlow -0.30, v -0.70 | `tension().slope` = (-0.70 + 0.30) / 20 = -0.02 a year; ETA = year + (-0.30 + 0.5) / 0.02 = year + 10 |
| `diplomacy v41: gate off is v3.1` | the recorded trace in `test_diplomacy_v4_trace.cpp` | identical; `rules.vSlow` stays empty |

**T3 acceptance**:

| Test name | Setup | Expected |
|---|---|---|
| `economy v41: stores fill 0.5 a year to 4 (8 with a granary)` | pop <= K for 20 years | 4.0; 8.0 with a granary |
| `economy v41: a hungry town eats its stores before it starves` | pop 1.2 x K, stores 4 | 4 years: stores -1, pop unchanged, famine 0; year 5: famine loss and famine 1 |
| `economy v41: camps start with 4 (awaken) and 2 (settlers)` | found both | 4, 2 |
| `economy v41: yearly emission per culture` | a factory town of 4,000 people, factoryCo2 0.002 | `civ_co2y` = 0.002 x 2 = 0.004; Space x0.5 |
| `climate v41: attribution reconciles` | 100 years, 2 emitting cultures | co2 delta = gross - uptake (1e-9); topEmitter is the larger, ties to the lower id |
| `climate v41: the ring keeps 11 samples every 10 years` | 150 years | 11 samples at years 50 ... 150; seaTrend = (sea150 - sea50) / 100 |
| `climate v41: restoreUptake 1 reads no cell` | restoration episodes active | uptake identical to v3.1 |
| `life v41: restore doubles fertility, not vegetation` | same cell, bloom vs restore(fertMul 2) | equal veg; fert gain x2 |
| `economy v41: a famine rain prayer waits for empty stores` | stores on; a hungry town with stores 2, no drought | no rain prayer while stores > 0; the prayer may begin in the year stores reach 0 |
| `climate v41: attribution off writes nothing` | 200 years, `ClimateTune::attribution` false | `rules.climate` empty; no `civ_co2y_` or `climate_uptake` key anywhere |

**T4 acceptance** (`test_powers_v41.cpp`):

| Test name | Setup | Expected |
|---|---|---|
| `powers v41: Rain held 0.3 s commits nothing` | 3 calls of dt 0.1 | 0 Awe spent, 0 `power:used`, no episode |
| `powers v41: at 0.4 s one episode, 25 Awe, one witness` | 4 calls of 0.1 at the default ang | 1 episode (15 years), awe -25, 1 `power:used`, 1 `miracle:witnessed` (even though `brushEventEvery` is 2); `episode { start: false }` 15 years later |
| `powers v41: episode cost is quadratic and doubled off territory` | ang 0.15 on territory; ang 0.10 on empty land; with Monsoon | 56.25; 50; 12.5 |
| `powers v41: a refresh is free and adds 0.1 once` | a second press inside the episode, twice | 0 Awe; strength 1.1; no witness |
| `powers v41: a fourth episode ends the oldest` | 4 commits | 3 active; the first ended |
| `powers v41: restoration on degraded unowned land lasts 30 years` | 90 % unowned, veg 0.3 of capacity | restore = true, until = began + 30 |
| `powers v41: rescue pricing` | a town 0.10 above the sea with a +0.004 a year trend (margin(30) -0.02: Sea at Rising) | 6 Awe/s until `seaMargin(100)` reaches 0.15 (a gain of 0.45), then 24/s; one good +0.6 witness after the first 0.03 gain; none again within 100 years |
| `powers v41: Ward` | Atomic; chooseGift(0, 2) | -300 Awe; Heal costs 80 and immunises 3 towns within 0.08 rad for 30 years; Monsoon never active; chooseGift(0, 1) refused |
| `powers v41: Guiding Light` | Space; chooseGift(1, 2); Inspire an Ark capital | cost 100; +4 progress-years; ark +0.04; refused again within 12.5 years |
| `powers v41: Leave-taking replaces the Wonder on an Ark capital` | Consecrate on it | ark +0.05; witness good 0; no Wonder |
| `powers v41: a quake on an Ark capital also pulls the Ark` | ark 0.5, arkLevers on, setbackQuake 50 | `setBack` -> ark 0.30, one `ark:push` by wrath |
| `powers v41: a god's meteor on an Ark capital pulls it; a Doctrine burning does not` | ark 0.5; a player meteor strikes the capital; separately a Doctrine burning strikes its academy (in the capital) | -0.16 once; the burning leaves ark unchanged |
| `powers v41: every spend writes powers_lastSpent` | reserve on; use, pay | `rules.num["powers_lastSpent"]` = the year; with reserve off, no key |
| `powers v41: gates off` | every existing powers test | unchanged |

**T5 acceptance**:

| Test name | Setup | Expected |
|---|---|---|
| `cards attention: the fifth Notice in 20 years is quiet` | attend(Notice) at years 100, 102, 104, 106, 108, then 121 (the window is (year - 20, year]) | loud at 100-106; quiet at 108; loud at 121 (102, 104, 106 remain: 3 loud) |
| `cards attention: Epochal is always loud and counted` | ledger full; `war:declared` | loud; the next Notice quiet |
| `cards attention: a big Crisis is loud when full` | stake 6 % of the world | loud |
| `cards attention: the director waits for room` | a strong proposal stream (test_cards' "strong stream"), ledger full in years 290-310 | the 290 opening moves to the first year with room |
| `cards attention: a card never waits more than attnHold` | ledger kept full and 3 Crises open for 100 years | the waiting card opens after 60 years, recorded quiet |
| `cards attention: the ledger drops quiet entries first` | 70 entries within 20 years, 4 of them loud | 64 kept; all 4 loud kept; room counts 4 |
| `cards attention: 3 open Crises hold cards and Notices` | `situations::crises` = 3 | no opening; Notice quiet |
| `cards attention: attach records Epochal only` | founding, tier change, a city razed, a village razed | 1 entry (the city) |
| `sim v41: the yearly order` | stub hooks trace at phase 1, then at phase 0 | civ, society, mythos, situations, cards, weather, climate ...; at phase 0: civ, society, situations, weather, climate ... |
| `sim v41: the neutral score ignores mood` | one world, ending mood set to each of beloved, feared, forgotten, parting | the same total; life + continuity + wonder = total; finite with no people |

**T6 acceptance** (`test_rules_integration.cpp` and simrun):
- 8 seeds x every new policy x 4,500 years with all families on: no NaN, an ending by year 4,700 or earlier, `hash` and
  `core` stable over 3 runs.
- `--saveload` at an episode commit, mid-chorus, at a Crisis and mid-Ark: the loaded world re-saves byte for byte, and
  two runs with the same `--saveload` point end on the same `hash` (as SPEC-MYTHOS 5.1 rule 6 reads it). A loaded run
  is not expected to match the unbroken run, because quantized cell fields differ after a load by design (simrun
  already reports this).
- Read-only proof: `--rules` runs with only Situations and attribution on keep `core` and the final rng state equal to
  the all-off run on 8 seeds x passive, kind, cruel and chaotic.
- **New policies** (`Policies.cpp`; every one answers cards with `--cards lapse` unless stated):

| Policy | Plays | Proves |
|---|---|---|
| `responder` | answers every listed Crisis and every plea of severity >= 0.25 with the cheapest ok answer; otherwise idle | R1-R4, R7 |
| `diplomat` | answers only War rows | R3 |
| `coast-warden` | Raise Land on every Sea row at Rising until safe | R8 |
| `guide-first` / `delay-first` | Space: Inspire and Leave-taking for the best-verdict racer / strike the leader whenever a lever is ready | R5 |
| `monsoon` / `ward` / `star-fall` / `guiding-light` | `responder` plus that gift at its age | R9 |
| `steward` / `accelerator` | from Industry, restoration episodes on the largest degraded unowned land every 30 years / Inspire the top emitter's capital whenever it can; otherwise `responder` | R10 |

  (gameplay-v4 3.2's later callings policy must be named `caller` to avoid the clash with `steward`.)
- **`--rules` report** per run and per age: episodes, effective brush-seconds (the policy's held dt), refreshes;
  local needs, pleas opened, peak concurrent pleas, minimum same-culture/kind gap; per risk kind: opened, Crisis
  reached, warning lead (first Watch to Crisis) histogram, warned 10-120 years share, false share (closed before
  Crisis and not Answered); losses by cause; ledger entries per real minute by class, loud per 10 s maximum, maximum
  open Crises, the longest span after minute 1 with neither an open row nor a loud entry; Awe band shares (>= 95 %,
  15-85 %, < 10 % runs); first public Beloved / Feared / Forgotten year; public label versus Ark mood; the Ark race
  (lanes, winner, launch year, pushes by kind); gifts; peak anomaly and sea deaths; the attribution residual; score
  lines; auto-pace v2 minutes modelled from loud entries.
- **`--summary` "rules" table**: every section 5 metric, printed as `track` (not PASS / FAIL) with its value.

**T7 acceptance**: the voice lint on 8 seeds x 6 policies; situation lines only at Crisis and at the end; v4.1 adds at
most 0.5 notable entries per real minute; the chronicle-budget tests still pass.

### Phase 2: balance (B41, 1 agent, M)

Owns `Sim/Balance.cpp` and `docs/handoff/rules-v41/*`. It switches families on one at a time, each with a simrun
before/after table on 8 seeds x every policy: (1) Situations and attribution together (`core` and the rng state
must be unchanged; `hash` changes because it covers RULE); (2) stores; (3) `vSlow` with `omenAge = 1` (re-tune
`borderAge` if the first war or war share leaves the v3.1 band). Balance.cpp already records that `omenAge = 1` alone
was tried and left off: kind's war share rose from 7.7 % to 11.1 % and "kind at war less than cruel" fell from 7/8
to 6/8. So step 3 must show that this gate holds again with the slow gate. If it does not, keep `omenAge` -1: early
wars then stay notices unless envy is a top-two cause, and owner question 4 is answered "no". (4) choruses and mature
words; (5) episodes and restoration; (6) reserve; (7) Ark levers; (8) rescue; (9) gifts; (10) attention; (11) neutral
score. **Gates during balance are the existing ones**: SPEC-SIM 4, the v3 and v3.1 tables, SPEC-MYTHOS 5.1 and 5.4
(the four failing design-review targets stay as they are or improve). Section 5's metrics are recorded, not gated.

### Phase 3: independent verification (V41, 1 agent, M)

Owns `SimTests/test_rules_v41_adv.cpp` and `docs/handoff/rules-v41/verify/*`. Adversarial checks: bad ids and dead
members in every API; zero cultures; a god spamming 0.39 s presses; episodes at sea and on the poles; gifts before
their age and twice; save at every situation stage and mid-press; 16 seeds; every family on alone and all together.
Marker `rules-v41.txt` with a score and numbers.

### Later: client track (separate wave, against the merged sim)

| Area | SPEC-UI | Work |
|---|---|---|
| `Game/UI/Hud/*` | 4, 4b | `#pan-risks` rows (pips, countdowns, answers, Let it be), plea rows, the commit ring and one-price chip, the Awe orb bands, Unproven and lean phrases, two Ark bars, the gift chooser on the age card, rescue tips, the forecast line, climate tooltips |
| `Game/Flow/*` | 4 settings | auto-pace v2 from `loud`; the first-five beat script (gameplay-v4 3.6) using plea and risk rows |
| `Game/UI/Screens/*` | 5 | the score lines, Hand and Memory words, seals |
| `Game/FX/*`, `Game/Planet/*` | 4 | episode veil and shimmer with a years-left ring, row markers at orbit |

---

## 5. Balance targets (tracking metrics until the owner's playtest)

"Now" is the v3.1 evidence. "Gate after playtest" says which numbers become pass/fail once a real 30-minute owner
session confirms the cadence (D2). Seed shares like "6/8" mean seeds out of 8.

| # | Metric (Astra's target) | Rule | Now (sim-findings-2) | Gate after playtest |
|---|---|---|---|---|
| 1 | <= 120 episodes and <= 180 effective brush-seconds in a successful kind / responder history | R1 | no episodes; kind 1,266 / 1,647 / 1,924 strokes = 158-241 brush-s, 57-75 clicks | yes |
| 2 | responder answers >= 65 % of urgent rain and bloom pleas (urgent: severity >= 0.25, the responder's own threshold); beloved >= 5/8 | R1, R2 | (no responder) kind beloved 8/8 | yes |
| 3 | old `kind` input earns <= +15 % grace or progress over `responder` | R1 | kind Ark 3,395-3,646 vs passive 3,521-4,087 | maybe |
| 4 | pleas 40-140 per history, at most 4 concurrent, same culture/kind >= 25 years apart unless +0.25 | R2 | prayers begun: passive 467 / 559 / 759, kind 816 / 999 / 1,098, prayers 853 / 1,192 / 1,279 | yes |
| 5 | `prayers` reaches >= 70 % of affected people with < 150 casts; beloved >= 6/8 | R2 | answered 665-994 of 853-1,279 prayers; 33-76 clicks + 413-586 strokes; beloved 8/8 | yes |
| 6 | >= 90 % of famines, wars, multi-town plagues and sea losses warned 10-120 years before Crisis | R3 | 0 % (nothing is forecast; first war 335 / 450 / 925 comes the year v crosses -0.5) | yes |
| 7 | false warnings (closed before Crisis, not answered) < 35 % | R3 | n/a | yes |
| 8 | `responder` loses >= 40 % fewer people to those causes than passive, with <= 120 interventions | R3 | passive: 22 / 35 / 46 towns lost to the sea; 57 / 62 / 73 famine entries | yes |
| 9 | `diplomat` war-years <= 50 % of passive on >= 6/8 | R3 | passive at war 6.1 / 9.9 / 23.6 % of years | yes |
| 10 | passive at >= 95 % of the cap in <= 20 % of years | R4 | 91 / 91 / 92 % (at the cap 88-89 %) | yes |
| 11 | `responder` in the 15-85 % band in 50-85 % of years; no active policy under 10 % for more than 120 years running | R4 | kind, chaotic: under 90 % in 100 % of years, at the cap 0 % (band not measured) | yes |
| 12 | great powers affordable 25-75 s after their age unlocks | R4 | 150 Awe every 25.4-27.6 s once meteors unlock (all policies) | maybe |
| 13 | `builder` ends more devout than `hoarder`, with more durable works, on >= 6/8 | R4 | 7/8 (0.33-0.39 vs 0.30) | yes (existing v3 gate stays) |
| 14 | `guide-first` changes the winner or advances the launch >= 100 years on >= 4/6; `delay-first` changes the winner or delays it >= 100 years on >= 4/6 | R5 | n/a (no lever) | yes |
| 15 | `late-kind` not beloved >= 4/6; `late-cruel` not feared >= 4/6; no ending after year 4,700 | R5 | 7/8 not beloved; 8/8 not feared; latest Ark 4,367 (cruel) | **already gates** (v3) |
| 16 | no terminal public word before year 250 | R6 | kind Beloved before year 20 on 8/8 (curious 7/8, blessonly 7/8, chaotic 4/8) | invariant now (SimTest) |
| 17 | kind publicly Beloved by year 800 >= 6/8; cruel Feared by 800 >= 6/8; passive Forgotten only after >= 600 silent years; the word equals the Ark mood once building | R6 | HUD mood = lead mood in 100 % of years (v3 check) | yes |
| 18 | 6-18 attention entries per real minute in every age; <= 4 loud in any 10 s (Epochal and big Crises can exceed this by design: reported separately); <= 3 open Crises; no 90 s span after minute 1 with no row and no loud entry | R7 | raw moments per minute: passive 7.1 (Stone) to 49.3 (Industry); kind 13.3 to 72.0; prayers 16.5 to 82.2 | yes (the main playtest question) |
| 19 | `coast-warden` saves >= 50 % of the towns passive loses to the sea on >= 6/8 with <= 12 rescues; peak population not lower; Ark <= 150 years later | R8 | passive sea losses 22 / 35 / 46 towns | yes |
| 20 | Ward plague deaths <= 60 % of Monsoon's on >= 5/8; Monsoon ends >= 40 % more drought pleas; Star-fall larger craters and no higher beloved rate; no gift dominates the score on > 6/8 | R9 | n/a (upgrades are automatic) | maybe |
| 21 | `steward` or `accelerator` lowers peak anomaly by >= 0.75 °C and sea deaths by >= 40 % on >= 6/8, with the Ark <= 200 years later | R10 | passive peak +3.61 / +3.94 / +4.14 °C, sea +0.253 / 0.279 / 0.391 | maybe (restoreUptake is the lever) |
| 22 | attribution reconciles yearly | R10 | n/a | invariant now (SimTest) |
| 23 | the same world reclassified among moods keeps its total; no mood is the unique top scorer on > 6/8 | R11 | a beloved policy tops 8/8 seeds (builder 6, kind 1, atomic-kind 1) | yes (the first part is an invariant now) |
| 24 | auto-pace history 25-35 minutes | R7 | auto-pace 28.8 / 30.3 / 33.2 min (passive) | **already a gate** (v3.1) |

### 5.1 Where the starting numbers come from

Every number in section 3 is a TUNE starting value. Its source is one of four:

| Source | Numbers |
|---|---|
| **Astra** (`docs/reviews/astra-design-review.md`, findings 1-11) | episode commit 0.4 s, 15 years, 25 Awe at the default radius and quadratic in radius, refresh at most +10 %, 3 active; chorus 8 towns, capital +0.25, full credit at half the affected people, 25-year cooldown unless +0.25; reserve 20 idle years, 60 %, 0.004 x aweMax, 15 %, +25 %; Ark memory 25 % until 0.8, Inspire +0.02 per 25 years, setback / 250, departure +0.05; mature year 250, grace + wrath 1.0 or 10 choruses; attention 4 per 10 s, 3 open Crises, 20 s hold; rescue to 0.15 above the 100-year sea, once per 100 years, gain 0.03, x1, good +0.6; gifts 300 Awe, Ward 80 / 30 years / 0.08 rad, Guiding Light 100 and double research; restoration 30 years, double fertility; every section 5 target |
| **gameplay-v4 3.1** | stores +0.5 a year, cap 4, +4 with a granary, -1 a year while hungry; `vSlow` tau 20, War Watch -0.25 and Rising -0.40; Raise Land x1 and good +0.6 for a warned town; Sea Crisis 0.04 above the sea |
| **The code today** (`Powers.h` table, `Balance.cpp`) | every power cost (Rain 5/s, Bloom 6/s, Raise 6/s, Heal 40, Inspire 60, Omen 100, Quake 60, Meteor 150, Lightning 15, Consecrate 150, Bless 80); setbacks (quake 50 from Balance, the rest are defaults); `famineSlack` 0.05; `plagueSpreadP` 0.3, `plagueHops` 8; `factoryCo2` 0.002; hoarding 0.015 a year with floor 0.3; income 2.0 + 0.17 ln(1 + S); prayers 15 years, at most 6; `offTerritoryMul` 2 |
| **This spec's own choices** (untested; B41 may retune them) | Bloom episode 30 Awe; 0.5 brush-seconds of effect a year; episode faith x2; restoration test (80 % unowned, veg below 0.6 of capacity); Ark setback cap 0.5; Guiding Light +0.04 every 12.5 years and +4 progress-years; camp stores 4 (awaken) and 2 (settlers); matured also by 600 silent years or a building Ark; `attnBigShare` 0.05, `attnHold` 60 (added in review); every Watch, Rising, Over and because-line threshold in the R3 catalogue that the two rows above do not list (Hunger growth 2 %, fertility 0.4, scorch 0.3; War Over -0.15; Plague exposure 0.20 / 0.40 and 5 quiet years; Sea margin over 30 years; Tinder 6 cells, 0.5 / 0.5, 3 hops, 2 years, 30 %; Ark Rising 0.6); ETA cap 200 years and slope floor 0.002; climate ring every 10 years, 11 samples |

---

## 6. How it plays

### First five minutes (years 0-600)

| Time | World | What the player sees and does |
|---|---|---|
| 0:00 | Two bands wake; the Awe orb reads 60; regard word **Unproven: "they are waiting to know you"** | Nothing urges them. The risk list is empty. |
| 0:23 (year 45) | The first trial drought. A **plea row**: "The Umari ask for rain: 2 camps, all of their people", with a 15-year ring (7.5 s) | The player arms Rain and holds it over the camps. A ring fills in 0.4 s; "25 Awe for 15 years" is paid once. A rain veil settles over the camps, and the row closes **Answered**. One deed, one witness, one progress-year. |
| 1:15 (year ~150) | The camps outgrow the dry valley. A **Hunger** row at Watch: "Ta Kripa: hungry in about 9 years (stores 4); many mouths" | Two answers: Rain (25) or Heal (40: they eat well for 30 years). The player can wait, because stores give a real window. If they never act, the row reaches Crisis only when the stores are gone. |
| 2:00 (year 240) | The player has done nothing for 20 years and the orb sits at 70 % | The orb says **"quiet power is fading"**. Saving now has a visible price. |
| 2:05 (year 250) | Memory matures (grace 1.2) | The word becomes **Beloved** or **Regarded** and now means something. |
| 2:40 (year ~320) | Bronze age (an Epochal entry: a banner and a short slow-down) | Bloom, Quake, Inspire, Consecrate and the Omen of Peace wake. |
| 3:30-4:30 (years 420-540) | A second people. **War brewing** at Watch: "the Vai and the Oru, about 60 years (30 s): a long frontier; the Oru envy your favour of the Vai" | Answers: Omen of Peace (100), Even the favour (Heal an Oru town, 40), Strike the Oru capital (Quake, 60, and be blamed). The ledger lets the Rising stage interrupt, because nothing else loud happened in those 10 s. |

### Last ten minutes (about years 2,600-3,800)

| Time | World | What the player sees and does |
|---|---|---|
| 21:00 | Industry. Battles, fires and caravans fold into one feed line per 10 s. Only Crises and great events slow time. | The list holds at most 4 rows. A fifth Notice in the same 10 s fades in quietly. |
| 22:30 | **Sea** at Watch: "Pell: under the sea in about 80 years (40 s); the Vai's engines 54 %" | Raise Land under Pell (6 Awe/s; the tip says "Lift Pell 0.11 to safety; they will see it as rescue"), a restoration Bloom on the burned hills (30 years), or Inspire the Vai toward Space (less smoke later). |
| 24:00 | Atomic. The age card offers the **gift**: Monsoon or Ward, 300 Awe, for ever | The player chooses Ward, because two cities are sick. The other gift is gone. |
| 27:30 | Space: Star-fall or Guiding Light. The **Ark** row lists two lanes: "the Vai 0.62 (would leave you Beloved); the Oru 0.40 (Feared)" | Inspire the Vai capital (+0.02, once per 25 years), the Leave-taking (+0.05 once, no verdict bonus), or Quake the Oru capital (-0.20, and the Oru blame you). Deeds still count at 25 % weight. |
| 29:00 | The Vai pass 0.8 | The caption reads **"their memory is sealed"**. Nothing bought now changes the verdict. |
| 30:00 | Launch. The ending shows the mood seal, the Legacy title and three score lines: Life, Continuity, Wonder | No line grades the player's morality. Mercy and wrath are described, not scored. |

---

## 7. Risks and owner questions (each with the default if unanswered)

| # | Risk, then the question | Default |
|---|---|---|
| 1 | **Famine changes feel.** Stores make famines fewer, later and announced (passive has 57-73 famine entries today). *Keep famine fast and brutal, or windowed?* | Windowed: stores 4 (8 with a granary). |
| 2 | **Gifts take away an automatic upgrade.** A player who never chooses gets neither Monsoon nor Star-fall. *Accept that the late ages ask instead of give?* | Yes. The age card shows the choice until it is made. |
| 3 | **Idle Awe fades.** A watchful player earns no income while above 60 % and idle, and loses up to 40 % of a full orb over about 100 idle years. *Keep "quiet power is fading" at 60 % after 20 idle years?* | Yes, at those numbers. B41 may soften the decay, never remove it. |
| 4 | **The Omen of Peace moves to Bronze**, so the first wars (from year 335) have at least 2 answers. This changes the v3 unlock table. It was tried alone once and left off: kind's war share rose to 11.1 % and "kind at war less than cruel" fell to 6/8 (Balance.cpp note). *Accept, if B41 shows the slow gate restores that gate?* | Yes, conditionally (`omenAge = 1` with the slow gate); otherwise `omenAge` stays -1 and Bronze wars are notices unless envy is a top-two cause. |
| 5 | **The score loses its mood multiplier.** Players who liked "Beloved x2" lose a clear high score. The planet card shows three lines instead. *Accept score neutrality?* | Yes. The total is Life + Continuity + Wonder. |

Build risks the lead holds without an owner question: the Mythos v1 tracks must merge first (the gate in section 4);
`vSlow` shifts war onset later, so the first war target is re-checked in B41 step 3; Ark memory at 25 % could let
`late-kind` buy a little grace, so the v3 gate (row 15) stays pass/fail; and with the ledger on, the 15-28 cards per
history target is re-measured, because openings now wait for room (at most `attnHold` 60 years each). Added in
review: stores delay famine rain prayers until the stores are gone, so plea counts (metric 4) may fall well below
today's prayer counts; a Leave-taking replaces a Wonder on the Ark capital, so a kind god who takes it gives up one
`scoreWonder` line (120), a price B41 should report; and the reserve's withheld income is this spec's reading of
Astra's "decays until it reaches 60 %", chosen because a decay with income still added would not move passive.
