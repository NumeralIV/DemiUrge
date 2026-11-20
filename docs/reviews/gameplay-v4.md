# DEMIURGE: gameplay v4, agency and strategy in real time

*Author: Claude (one of the lead's build agents, track "gameplay-v4"), 2026-11-20. Design only: no code was
changed. Every rule below names the existing state it reads and the module that would own it.*

**Sources.** `AGENTS.md`, `docs/UNREAL.md`, `docs/SPEC-UNREAL.md`, `docs/CONCEPT.md`, `docs/SPEC-SIM.md` (v3, all
of it), `docs/reviews/claude-design-review.md` (findings 1-13), `docs/handoff/release-notes.md`, the wave-3
gameplay handoff and its tables (`docs/handoff/done-ue/wave3-gameplay*.txt`,
`docs/handoff/wave3-gameplay/before-after-tables.md`), the latest 8-seed simrun output
(`docs/handoff/sim-findings-2/simrun-before-8seeds-all.txt`), the Sim headers (`Sim.h`, `State.h`, `Civ.h`,
`Society.h`, `Powers.h`, `Core/Events.h`, `Chronicle.h`, `Save.h`), `Balance.cpp` (including the v3.1 keys of
the findings 11/13 track, which is still running), `Tools/simrun/Policies.h`, and the client's HUD, flow,
screens and input code (`Game/UI/Hud/DmHudModel.*`, `SDmHudGuide.*`, `Game/Flow/DmFlow.h`,
`Game/UI/Screens/DmScreenTypes.h`, `Game/Input/DmInputRouter.cpp`).

**Time.** At 1x one real second is 2 years and one real minute is 120 years (SPEC-SIM 0.5). A history of
about 3,600 years lasts about 30 minutes. In this document "N s at 1x" always means real seconds at 1x.

---

## 1. Diagnosis in one paragraph

Wave 3 fixed authorship of the ending and the empty opening: the verdict now comes from long memory, all four
moods are reachable, grace and wrath move progress, and the Stone age has camps, a first trial and a first
prayer by year 46. **What the game still lacks is decisions.** The world produces plenty of events but few
choices. The player is never warned before something happens, rarely has two good answers to pick between, and
seldom pays a price for ignoring something. Evidence from the current build:

- **Plenty happens, but none of it waits for the player.** A passive world on seed 2 raised **613 prayers and
  answered 0**. Sim "moments" run at 4-14 a minute in the Stone and Bronze ages and 40-100 a minute in Industry
  (simrun `pacing by age`). An unanswered prayer costs its town only devotion -0.05 and love -0.05, so ignoring
  hundreds of them changes little.
- **The best scripted player is a reflex, not a strategy.** The `kind` policy, which reaches the best ending,
  makes **1,840-2,200 brush strokes per history (1,411-1,602 of them Bloom)** and about 50 clicks. Its optimal
  play is to keep the brush down. Nothing asks it to choose.
- **Little arrives with warning.** War is declared the year relations cross -0.5. The relation moves by 0.05 of
  Gaussian noise a year against a 0.03 pull toward its rest point, so the crossing is mostly luck and can come
  in a couple of years. Famine takes a town from K straight to losses (12 % a year). Plague, eruptions and
  storms all start without an omen.
- **The god has nothing to tend between crises.** There are no persistent choices about a town or a region.
  Every power is a one-off act, and nothing the player sets up keeps working on its own.
- **Awe has no competing uses.** A passive god sits at 95 % of the cap in **88-91 % of years**. An active god
  spends on the same reflex.
- **Every world plays the same way.** The only setup choices are a seed, a calamity level (calm, normal or wild)
  and quick start. There are no goals, starting conditions or culture mixes to pick.

The bones are right: prayers as the core verb, two-handed memory, age-gated powers, a chronicle that remembers.
This document adds three layers on top of those bones, ranked by impact. **Situations** are warnings with a
countdown and several answers. **Tending** is persistent choices about towns, ground and roads. **Setups**
are things that make each world different. It also adds the readouts that make the loops visible and a
scripted first five minutes.

## 2. What "a real strategy game in real time" means here

These are the target qualities, and every proposal is judged against them:

1. **A choice with a cost at least once a minute.** From the first minute of history, at least one open matter
   has at least two good answers. Each answer costs something scarce: Awe, a prophet's single voice, a town's
   one calling, or the favour of the other side.
2. **The world warns before it strikes.** Big things build up in stages that can be read: an omen, then rising
   trouble, then a crisis, then the outcome. Each stage shows a countdown in real seconds at the current speed.
3. **Tend, never command.** The god never moves a unit or places a house. It marks ground, gives towns a
   calling, blesses roads, and acts with miracles. The people decide everything else. This keeps the concept's
   promise: "You never control them."
4. **Every loop can be read.** For each loop (food, faith, knowledge, tension, smoke, memory) the HUD shows the
   current value, the trend, the cause and the god's levers in plain words.
5. **Urgent and important both exist.** Fast situations (5-15 s windows at 1x: a fire, hunger, a plague's
   spread) test reflexes. Slow situations (30-90 s: a war brewing, a heretic, the rising sea, a race to the
   stars) test planning. Both are open at once, so the player is always triaging.
6. **No two worlds alike.** Terrain shapes, people's temperaments, rivals, difficulty, vows and challenges
   change which situations dominate a run and what a good strategy is.
7. **The first five minutes teach by doing**, with one prompt at a time and every step ticked off by an action.

A rule for the whole layer: **a situation must offer at least two meaningful answers, or it is only a
notification** (a toast) and does not take a slot in the situation list.

## 3. Proposals ranked by impact

Each proposal lists: **Why**, **Rule** (numbers are starting values for the balance owner), **UI**, **Headless
test**, **Effort / owner files**. Effort: **S** is under half an agent-day, **M** is about one agent-day,
**L** is two to three agent-days, each built and then verified.

### 3.1 CRITICAL: Situations, so the world warns, then strikes

**Why.** This is the main missing piece (section 1). It turns the stream of moments into a short list of named
matters, each with a deadline, several answers and a consequence the people remember.

**Rule: the module.** Add a new sim module, `Sim/Situations.{h,cpp}` (namespace `dm::situations`).

- **What it owns.** It writes only its own list, `World::situations`, a persisted vector of:

  ```
  Situation { id, kind, stage (Omen | Rising | Crisis | Over), cell, culture, other (culture id or -1),
              towns (settlement ids, at most 8), began, stageSince, deadline (sim year), severity 0..1,
              answered (bool), outcome (None | Answered | Weathered | Lost) }
  ```

- **When it runs.** It runs in the yearly group after `society::yearly` and before `climate::yearly` (a
  one-line change to `Sim.cpp`'s schedule). It subscribes to the bus like Chronicle does (`situations::attach`,
  re-subscribing after `Bus::clear`). It sees `power:used`, `miracle:witnessed`, `prayer:answered`,
  `war:ended` and `settlement:destroyed`, and uses them to mark a situation `answered` when the god acted on
  its towns or its cell during the situation.
- **Determinism.** It draws no rng. It iterates settlements, cultures and relation pairs in ascending id order,
  and ties break on id.
- **Cost.** At most 0.15 ms per yearly group (O(settlements + pairs + disasters)).
- **Events.** It emits a new event, `situation { id, kind, stage, outcome }`, once per stage change.
- **Severity** = (people at stake / world people) × (1.5 if the towns include a capital), clamped to 0..1.
  The HUD ranks by severity. At most **4** situations are shown, and the rest fold into "and N more".
- **What outcomes do.** A situation the god **Answered** adds `0.5 × severity` to the grace of each affected
  culture's memory. This is a Society change: Society reads the `situation` event. "They remember the god who
  stood by them in the flood." A situation that ends **Lost** (towns destroyed, a people split, a war lost)
  while its towns prayed costs love -0.1 in those towns: "Why did you not come?" **Weathered** situations
  change nothing.

**Rule: the catalogue.** Every kind is built from state the sim already has. A **(new)** tag marks the few
sim rules that each kind needs. The MVP is the six kinds marked ★.

| kind | omen (trigger) | crisis | window at 1x | answers (cost) | if ignored | ages |
|---|---|---|---|---|---|---|
| ★ **Hunger** ("dry years at X") | a drought covers a town, or K's 5-year trend reaches pop within 10 years | the town's stores are draining | 5-15 s | Rain (5/s); Bloom (6/s); Harvest calling; a granary through the calling | stores run out, famine (pop to K, 12 %/yr), then exodus as settlers after 8 famine years | all |
| ★ **Plague** | `settlement:plague` start | 3 or more towns ill, or the capital | 5-15 s | Heal the source (40) or the ring of neighbours (40 each, 30 years of immunity); a road cut by shunning | 8 %/yr deaths, spreads within 8 hops and along trade routes | Iron+ (natural), any (god) |
| ★ **War brewing** (A and B) | `vSlow` ≤ -0.25 and falling **(new: `vSlow`, below)** | `vSlow` ≤ -0.4 (the brink), then war | 30-90 s | Omen of Peace (100) calms at the brink (v3.1 `omenCalm`) or ends the war; Prophet + peace edict (120); Inspire the weaker side (60) so the aggressor fails `warStrength` 0.6; strike the aggressor's capital (a setback); shun the frontier strip | war: armies, captured towns, then a "victory" prayer that asks the god to pick a side (3.4) | Bronze+ |
| ★ **A preacher rises** (heretic) | **(new, 3.4)** a low-devotion or estranged large town raises a rival preacher | his following ≥ 30 % of the culture | 30-90 s | your own Prophet in that culture; a good miracle in his sight; smite him (but if the culture's love < 0 he becomes a martyr); a Faith calling there | schism: a new people that keeps devotion × 0.5 for 300 years | Iron+ |
| ★ **The sea comes** | Industry+, sea trend > 0, a town centre within 0.12 of the sea | ≤ 0.04 above the sea | 30-90 s | Raise Land under the town (**new:** good +0.6, cost × 1 while it is warned); Defence calling (a sea wall: +0.08); Bloom forests (CO₂ uptake); shun the low coast | the town dies of `sea`; 60 % leave as settlers (exists) | Industry+ |
| ★ **A golden age** (an opportunity) | town+ with a temple, a market or academy, love ≥ 0.3, growing, no war, plague or famine | a window of 30 years | 15 s | Consecrate at 75 % cost (a Wonder in a capital); Inspire gives +4 progress-years instead of +2; a calling change is free | it passes (no harm) | Bronze+ |
| Wildfire | a fire within 3 hops of a town on dense, dry vegetation | the fire front reaches its territory | 3-8 s | Rain puts it out; or let it burn (scorch, then a Bloom prayer, then rich ash) | cells burn, the town loses farmland | all |
| Strangers meet | two cultures gain `contact` | a window of 30 years | 15 s | Pave a road between them (trade); Omen of Peace (a sworn friendship, v3.1 `omenSworn`); shun the land between (kept apart: fewer wars, no catch-up); a wrath in one's sight (blame) | relations follow the existing drift | Stone+ |
| Far towns drift (schism by distance) | `apart` ≥ half of `apartYears` in 2 or more towns | `apart` ≥ 80 % | 30-60 s | Prophet visits; Faith calling; **(new)** a road (road ≥ 2 to the capital) stretches the estrangement distance from 0.57 to 0.75 rad; or let them go (another people, another Ark candidate) | a new culture is born | Bronze+ |
| The air warms | Industry+, the CO₂ trend projects ≥ +1 °C within 100 years | projected sea rise reaches a town | 60-120 s | Bloom forests (uptake rises with vegetation cover); strike the smokiest culture's factory towns (setback); Inspire it toward Space (CO₂ × 0.5 there, a paradox the player can find) | warming, sea rise, coastal towns lost | Industry+ |
| The fire weapons | two Atomic+ cultures at war | **(new)** nuclear risk ramps from 0 for 10 war-years, to 1.5 %, to 3 %/yr after 30 | 15-40 s | Omen of Peace; Prophet peace edict; strike one side's capital to exhaust it (+0.15 per settlement lost) | nuclear exchange (exists) | Atomic+ |
| The race to the stars | 2 or more cultures have a launch site | the leader's Ark ≥ 0.6 | 60-120 s | Inspire a capital (**new:** Ark +0.02); wrath on a rival's Ark capital (**new:** Ark -setback/250); Omen of Peace if they fight over it | the first to launch decides whose memory ends the story (exists) | Space |
| The mountain smokes | **(new)** a natural eruption gets a 20-40-year precursor at its vent | 5 years before | 30 s | shun the slopes; **(new)** an Earthquake within 0.08 rad vents it (half power); from Industry, open it yourself now (smaller) | full eruption (exists) | all |

**Rule: the sim changes the catalogue needs.** Each is small and owned by one module.

- **Diplomacy: `vSlow`, the slow mood between two peoples.** `vSlow` is an exponential moving average of `v`
  with τ = 20 years, stored in `Relation` (a new field, persisted). War is declared on `vSlow < warV` instead
  of `v < warV`. All the other gates stay. The noise keeps texture in `v`, but a war now takes decades of real
  friction (border, aggression, love gap, envy). That gives the 30-90 s window. **Must be re-verified:** the
  war share of years and the first war (finding 11's targets from the v3.1 track) are re-tuned with
  `borderAge` once `vSlow` exists.
- **Economy: stores.** Each settlement gets `stores` (persisted, in years). It fills +0.5 a year while
  pop ≤ K, up to a cap of 4 (8 with a granary, +4 with the Harvest calling). While pop > K × (1 + famineSlack)
  the town eats its stores first: stores -1 a year, no growth, no losses, no famine years. Famine and losses
  begin only at 0. This creates the Hunger window and gives granaries a reason to exist. It also lowers the
  famine count, which is too high today (68-131 entries for `kind`).
- **Powers: raising a warned town.** In history, Raise Land whose brush centre is within the territory of a
  town that currently has a **The sea comes** situation counts as good +0.6 and costs × 1 instead of × 4. The
  god lifted them. Everywhere else it stays good -0.5 and costs × 4.
- **Diplomacy: the nuclear ramp.** `nukeP` × 0 for the first 10 years of a war, × 1 to 30 years, × 2 after
  that. The existing "at most once per war" rule stays.
- **Society: the Ark responds to the god.** Inspire on a capital that is building its Ark adds +0.02. The god's
  wrath on such a capital subtracts setback / 250, so a meteor (setback 40) subtracts 0.16.
- **Disasters: precursors.** A natural eruption is scheduled 20-40 years before it starts. During that time the
  vent smokes, and the event `volcano:omen { cell }` (new) tells the client to show it. A divine quake within
  0.08 rad halves its power.

**UI.**

- **The Situations panel** replaces the left "Now" strip, keeping its place and icons. Each card shows:
  - the icon and a title in the people's own names ("War brewing: the Vai and the Oru");
  - the stage as 3 pips;
  - **a countdown ring labelled in years and in real seconds at the current speed** ("~40 years, 20 s");
  - one "because" line built from causes the sim knows ("a long frontier; the Oru envy your favour of the
    Vai");
  - up to 3 **answer buttons**, each with its power icon, cost and expected effect ("Omen of Peace on the
    Vai, 100: no war for 80 years"). A click arms the power, flies the camera to the target and outlines the
    target in the brush ring's colour;
  - a quiet "Let it be" that folds the card.

  The answer data comes from a read-only sim helper, `situations::answers(w, id)`, which returns
  `{ power id, target cell, cost, effectKey }`. The HUD formats the words. Folded and finished cards leave
  with their outcome word ("answered", "weathered", "lost").
- **Map markers:** a pulsing ring at the situation's cell at orbit and medium zoom, coloured by stage.
- **Auto-pace v2** (section 3.9) slows time when a situation reaches Crisis.
- **The chronicle** writes one entry per situation at Crisis and one at its end (through its budget), so the
  situations do not add to the firehose.

**Headless test.**

- `SimTests/test_situations.cpp` (synthetic worlds):
  - (1) A drought over a town opens Hunger within 1 year. Stores drain, and there is no famine while
    stores > 0. Rain over the town marks it `answered`, and it ends `Answered` when K ≥ pop.
  - (2) A scripted `v` series pushes `vSlow` through -0.25 and then -0.4. War Tension opens with a deadline
    ≥ 15 years ahead. An Omen at the brink ends it `Answered`.
  - (3) Plague spreads to a third town and the stage goes to Crisis. Healing the source and both neighbours
    ends it `Answered`.
  - (4) Two runs driven by the same calls give identical situation lists, and a save/load round trip
    preserves them.
  - (5) A yearly pass at L = 6 with 120 settlements takes ≤ 0.15 ms.
- **simrun `--situations`** prints per age: situations opened per real minute, crises per minute, outcome
  shares, the longest real-time gap with nothing at Rising or above, and the maximum concurrent crises.
- **New policies** in `Tools/simrun/Policies.cpp`:
  - `responder`: answers every Crisis with the cheapest listed answer it can pay for, otherwise does nothing;
  - `firefighter`: answers only the fast kinds;
  - `diplomat`: answers only war kinds.
- **Targets** (8 seeds):
  - `passive` has ≥ 1 situation at Rising or above in ≥ 80 % of real minutes from minute 1 of history, and
    no gap over 90 s at 1x;
  - ≤ 4 concurrent crises in ≥ 95 % of years;
  - `responder` loses ≥ 40 % fewer towns to famine, plague, war and sea than `passive` on ≥ 6/8;
  - `responder` ends beloved on ≥ 5/8 with ≤ 25 % of the brush-seconds `kind` uses;
  - `diplomat`'s war share of years is ≤ 50 % of `passive`'s on ≥ 6/8;
  - every SPEC-SIM 4 and v3 target still passes.

**Effort / owner files.** **L.**
- The module: `Sim/Situations.{h,cpp}`, `SimTests/test_situations*.cpp`, `SimTests/stubs/Situations_stub.cpp`.
- One schedule line in `Sim/Sim.cpp` (the Sim owner).
- Rule pieces: `vSlow` and the nuclear ramp in `Diplomacy.cpp`; stores in `Economy.cpp`; raising a warned
  town in `Powers.cpp`; the Ark hooks in `Society.cpp`; precursors in `Disasters.cpp`.
- The list, the persisted fields and save v2: `State.h`, `Schema.cpp`, `Save.cpp`.
- simrun's report and the new policies: `Tools/simrun/*`.

### 3.2 CRITICAL: Callings, so the god gives a town a purpose

**Why.** Tending a living world needs choices that persist and keep paying out, the god's version of deciding
what a district is for. Today every act is a one-off. A calling is one choice per town, it reshapes how the
town grows, it can be lost if the god neglects it, and the player can see it from orbit.

**Rule.**

- **The field.** `Settlement::calling` (persisted enum): `None | Harvest | Faith | Defence | Trade | Learning |
  Seafaring | Industry`.
- **The power.** It is set by a new click power, **Calling** (`call`, target settlement, key **C** in history;
  `C` is Comet's key, and Comet exists only in the Water phase). `UseOpts` gains `int choice`.
- **Cost:** 30 + 10 × (the callings the god already holds). The tenth costs 120, so the player cannot call
  everything.
- **Limits:**
  - the town needs a **shrine**, because they listen at the shrine;
  - one change per town per 60 years (free during a golden age);
  - the calling holds while devotion ≥ 0.3. Below that for 20 years the town forgets: the calling becomes
    None, and an event fires.
- **Effects** (small, multiplicative and readable; each offers a trade-off, so no calling is free):

| calling | from | effect | price |
|---|---|---|---|
| Harvest | Stone | K × 1.15; stores cap +4; a granary from village | the town's share of research × 0.9 |
| Faith | Stone | Awe weight S × 1.5; devotion baseline +0.08 | growth r × 0.9; research × 0.85 |
| Defence | Bronze | walls at peace; defence × 1.5; a sea wall from Industry (+0.08) | relations with its neighbours -0.01/yr (a fortress makes them wary) |
| Trade | Iron | a market from village; traders every 3 years instead of 5; +0.01/yr relations with partners | plague hop chance along its routes × 1.5 |
| Learning | Iron | research × 1.3; an academy from town (normally Sail and city+) | devotion baseline -0.05 (questions) |
| Seafaring | Sail | a port from camp; naval settler search maxCost × 1.5 (colonies over the sea) | its colonies count as estranged sooner (0.45 rad) |
| Industry | Industry | a factory from town; research × 1.2; K × 1.1 | CO₂ × 1.5 |

- **How research combines.** Research reads callings through one new factor in Economy's rate:
  `callF = 1 + 0.3 × (people share with Learning) + 0.2 × (Industry share) − 0.15 × (Faith share) −
  0.1 × (Harvest share)`, beside `buildF`.
- **Ages bring decisions, not only powers.** Every age from Bronze to Industry adds a calling, and the
  callings table doubles as the "what this age brings" card.

**UI.**

- **The inspector** gets a "Calling" row:
  - "None. Give them a calling (C), 40 Awe";
  - when opened, 2-7 icons for the callings the age allows, each tooltip stating the effect and the price in
    numbers;
  - the chosen calling as a coloured badge on the town's label;
  - "Forgetting: devotion 0.27, 12 years left" when it is slipping.
- **A Callings overlay** colours each territory by calling.
- **The towns renderer** grows **districts** by calling (this is where "districts that grow" lives, at no cost
  to the sim). The calling picks the district weights of the layout:
  - Harvest: wider field belts and granaries;
  - Faith: a temple quarter and a processional way;
  - Trade: a market square, caravan yards and more roads;
  - Learning: academy domes;
  - Defence: walls and towers;
  - Seafaring: docks;
  - Industry: workshops and chimneys.

  Districts expand with the town's tier.

**Headless test.**

- `SimTests/test_callings.cpp`:
  - Harvest raises K by 1.15 exactly on a synthetic town;
  - Learning raises its culture's research rate by the expected factor;
  - a town below devotion 0.3 for 20 years forgets (one event);
  - a calling cannot change within 60 years;
  - the cost rises with callings held;
  - determinism, and save/load preserves callings.
- **simrun:**
  - a new policy `steward` (answers prayers; gives the capital Learning, temple towns Faith, coastal towns
    Trade, frontier towns Defence; re-calls forgotten towns);
  - target: `steward`'s Ark is 100-500 years before `passive`'s on ≥ 5/6 seeds and still inside 3,000-4,500;
  - target: `steward` sits at the Awe cap in ≤ 30 % of years;
  - target: `passive` summaries match the pre-change build except the new columns (callings default to None,
    so the passive world is unchanged unless stores ships in the same build).

**Effort / owner files.** **M.**
- The field and save: `State.h`, `Schema.cpp`, `Save.cpp`.
- Effects on K, buildings, research, stores and CO₂: `Economy.cpp`.
- Devotion, S and forgetting: `Society.cpp`.
- The `call` power and `UseOpts::choice`: `Powers.{h,cpp}`.
- Relations and trade cadence: `Diplomacy.cpp`.
- Client: the inspector row (HUD), districts (Towns), the overlay (Planet + Control).

### 3.3 HIGH: Hallowed and shunned ground, and blessed roads

**Why.** This is the zoning-like layer: influence over *where* people settle and how the land is connected,
without placing anything by hand. It also gives the god levers for situations, such as keeping two peoples
apart, keeping towns off a coast the sea will take, or linking a far colony to its capital.

**Rule.**

- **The field.** A new per-cell field `zone` (Int8, persisted): +1..+3 hallowed, -1..-3 shunned.
  - Writer: Powers (the brush).
  - Decay: one level toward 0 every 150 years (Civ.yearly, staggered by cell).
  - Dirty flag: `color`.
- **Hallow** (`hallow`, brush, key **G** in history; G is Seed's key, and Seed exists only in the Life phase):
  2 Awe/s at ang 0.06; +1 level per 0.5 s held. It has no witnesses, because marking ground is not a miracle.
  - `civ::siteScore` += 0.35 × level, so settlers choose hallowed sites.
  - The expansion search gets maxCost +20 when a hallowed site lies within that reach (settlers go further
    for it).
  - A town founded on hallowed ground starts with devotion +0.1 and love +0.05 (`FoundOpts`): "They came
    because you called."
  - Inside a territory, farm and urban growth prefer hallowed cells (`farm[]` ranking in Economy; ring growth
    in the towns renderer).
- **Shun** (`shun`, the same brush with Shift+G; the sim power has no letter key and Input maps the chord):
  2 Awe/s.
  - `civ::canSettle` returns a new reason, `Shunned`: no new town there.
  - `path::stepCost` +2 per shunned land cell for armies only, so a shunned strip between two peoples slows
    armies. Past `armyMaxCost` 60 it stops them.
  - Territories stop at shunned cells on re-claim, so the existing `border` term falls by itself.
- **Pave** (`pave`, brush, Shift+C): 4 Awe/s; road +1 per 0.5 s on land cells under the brush (cap 3), through
  a new Society (diplomacy half) API `society::pave(w, cell, ang, dt)`, because Society is the writer of
  `road[]`. Every existing road effect follows: path cost -0.25 a level, faster units, trade routes along
  cheap paths. The **trade-offs are already in the sim**: armies march faster on a paved road, and plague
  hops along trade. A road of level 2 or more from a town to its capital also stretches its estrangement
  distance (3.1, "far towns drift").

**UI.**
- The palette gets a fourth group, **Tend** (Hallow / Shun, Calling, Pave), beside Shape, Sky, Wrath and Grace.
- The brush ring is gold for Hallow, grey for Shun and amber for Pave.
- At medium zoom, hallowed ground has a faint gold shimmer in the planet's ground material and shunned ground
  a cold grey veil, both fading with level.
- A **Zones overlay** shows them.
- The tooltip of a settler unit names its destination and why ("hallowed ground, fresh water").

**Headless test.**
- `SimTests/test_zones.cpp`:
  - siteScore rises by 0.35 a level;
  - canSettle refuses shunned cells;
  - zone decays one level in 150 years;
  - an army's path cost grows across a shunned strip;
  - `pave` raises road[] under the brush up to the cap;
  - determinism.
- **simrun:**
  - `zoner` hallows the best river site on another landmass every 100 years, and from Industry shuns coast
    below +0.3. Target: towns lost to `sea` ≤ 50 % of `passive`'s on ≥ 5/6.
  - `warden` shuns a 3-hop strip between the two largest peoples in contact. Target: war share of years
    ≤ 60 % of `passive`'s on ≥ 4/6.
  - `builder-roads` paves between capitals. Target: trade routes ≥ 1.3 × `passive`'s and relations mean
    higher on ≥ 5/6.

**Effort / owner files.** **M.**
- The field and save: `State.h`, `Schema.cpp`, `Save.cpp`.
- Score, settling and decay: `Civ.cpp` and `Economy.cpp`.
- The army cost: `Path.cpp`.
- The powers: `Powers.{h,cpp}`.
- Pave: `Diplomacy.cpp`.
- Client: chords and ring colours (Input), shimmer and overlay (Planet), the palette group (HUD).

### 3.4 HIGH: Peoples with agendas, new prayers and rival preachers

**Why.** Strategy needs other actors with wants. Today all prayers are for relief (rain, heal, peace, bloom),
so the only good answer is always grace. Wrath has no constituency. Give each people an ambition, give wrath
something to answer, and give the god a rival.

**Rule.**

- **Ambition.** Each culture's ambition (vars.num `society_ambition_<id>`) is re-derived every 25 years:
  - **Conquer**: aggression ≥ 0.6 and a weaker neighbour in contact;
  - **Trade**: seafaring ≥ 0.6, or 2 or more markets;
  - **Know**: curiosity ≥ 0.6;
  - **Worship**: piety ≥ 0.6;
  - **Grow**: otherwise.

  Ties break by trait order and then by id.
- **New prayer kinds** (`PrayerKind` gains `Victory`, `Wisdom`, `Sign`):
  - **Victory** (finding 11's proposal): a Conquer or at-war culture's capital prays for it while at war with
    exhaustion < 0.4. It is answered when **any wrath of the god harms a settlement of the enemy** within 15
    years. The praying people get the answered effects (double faith, love +, grace) while the struck people
    get wrath. This makes taking sides the main decision of a war: help A, help B, or make peace.
  - **Wisdom**: a Know culture's capital prays about every 80 years. It is answered by Inspire. Answered
    prayers already give +1 progress-year; this one gives +3.
  - **Sign**: a Worship culture's temple town prays about every 60 years. It is answered by any miracle
    witnessed there. For a culture whose love < -0.3, **wrath also answers it** ("they asked for a sign and
    you sent fire; they knelt"). That gives the feared path its own answered prayers.
- **Rival preachers.**
  - **Who can raise one:** Iron age or later; a culture with ≥ 4 towns and no preacher in the last 300 years;
    at most one living preacher per two living cultures. The town must hold ≥ 8 % of the culture's people
    with devotion < 0.25, love < -0.25, or `apart` ≥ 60.
  - **How often:** each eligible town rolls 2 % a year from `w.rng`, in Society.yearly, in id order. A named
    preacher appears (Names), with the event `preacher { culture, settlement, name, start }`.
  - **How he grows:** his following `f` starts at 0.05 and grows by 0.02 × (1 − his culture's mean
    devotion) a year. Towns within 0.3 rad lose devotion 0.01 × f a year and gain 1 extra `apart` a year.
  - **The schism:** at f ≥ 0.5 with 2 or more towns, they secede (`BirthReason::Schism`, existing), and the
    daughter's devotion baseline is × 0.5 for 300 years. They follow the preacher's god, and they still
    remember yours.
  - **Answers:**
    - your Prophet in that culture: his growth × 0.4, and each prophet visit to a following town f -0.1;
    - a good miracle witnessed in his town: f -0.05 (at most -0.15 a decade, so spamming does not trivialise
      it);
    - smite his town while he lives: he dies. If the culture's love < 0 he becomes a **martyr** (f +0.15 at
      once, following towns' devotion -0.05); otherwise f -0.3;
    - he dies of age after 80 years.
  - Prophets are one per culture, so using yours against a preacher costs its edicts. That is a real
    opportunity cost.

**UI.**
- The culture panel shows the ambition with an icon and one sentence ("The Oru want land; their neighbours are
  weaker").
- The new prayers get their own icons in the Situations panel and on town labels. Victory shows "who to
  strike".
- A preacher shows at medium zoom as a dark-robed crowd marker in his town, with his following as a ring
  segment.
- The remember card states plainly that "answering a Victory prayer makes the other side remember wrath".

**Headless test.**
- `SimTests/test_society_agendas.cpp`:
  - each ambition from synthetic traits;
  - a Victory prayer is answered by a quake on the enemy, and the two cultures' memories move in opposite
    directions;
  - Sign is answered by wrath only when love < -0.3;
  - a preacher appears under the conditions with a fixed rng state, grows by the formula, a prophet halves his
    growth, smiting him at love < 0 makes a martyr, and a schism happens at f ≥ 0.5;
  - determinism and save.
- **simrun:**
  - passive gets ≥ 1 preacher in ≥ 5/8 histories and ≤ 4 per history;
  - `kingmaker` (always answers the Victory prayer of the culture with the best verdict) ends with that
    culture launching the Ark on ≥ 5/8;
  - every existing ending target still holds.

**Effort / owner files.** **M-L.**
- Ambitions, prayers and preachers: `Society.cpp` and `Society.h` (society half).
- The prayer enum and the new event: `State.h`, `Core/Events.h`, `Schema.cpp`, `Save.cpp`.
- Prose for the new events: `Chronicle.cpp`.
- Icons and labels: HUD.

### 3.5 HIGH: Readable feedback loops

**Why.** Systems only feel strategic when the player can see what moves them. The inspector's plain sentences
("395 people, growing. The land could feed 678.") are the model. This section names every loop and gives
each one a readout.

| loop | what drives it | the readout (existing → new) | the god's levers |
|---|---|---|---|
| Food | land, fertility, moisture, tech → K → people; famine, stores, settlers | inspector K line → **forecast line**: "feeds 360, 412 live here: hungry in ~4 years (stores 4)"; **Hunger overlay** (pop / K per territory) | Rain, Bloom, Harvest, Hallow (where settlers go) |
| Faith | miracles → devotion → Awe income → miracles; jaded dampens; hoarding costs | Awe "+n / yr" → add **"income: 41 % from temples, 22 % from Faith towns"** in its tooltip and a jaded warning on the town label ("numb to miracles") | small acts spread wide, Consecrate, Faith calling, prophets |
| Knowledge | towns, academies, inspire, callings → research → ages; wrath setbacks | age ring → **"Bronze in ~110 years (learning 1.4 / yr; Learning towns +22 %)"** in its tooltip | Inspire, Learning calling, answered Wisdom; wrath on rival capitals |
| Tension | frontier, aggression, love gap, envy of favour → `vSlow` → war → exhaustion | none → **Tension overlay** (border lines from blue to red by `vSlow`; war red and animated) and a "because" line on War cards | Omen, peace edict, Shun strip, Pave road, which side you favour |
| Smoke | factories → CO₂ → warming → sea → lost towns | anomaly and sea readouts → **a projection** in their tooltips ("+3.1 °C and 0.3 sea by ~year 3,400 at this rate; the Vai's engines 54 %") and the warming situation | Bloom forests, strikes on factory towns, Learning instead of Industry, Inspire toward Space |
| Memory | deeds → grace, wrath, neglect → verdict → ending | the remember map → **one dot per culture** (today: one), and the Ark-race card in Space | everything, and whose Ark launches first |

**The act ledger.** About 3 s after any power use, one line under the armed chip says what the act did:
"+0.06 faith in 4 towns · Ta Kripa's prayer answered · +1 year of learning · the Oru saw it (envy)". This is
client only: the HUD sums `miracle:witnessed` ids, `prayer:answered` and its own before/after `regard` reads.

**Cause chips** on toasts and chronicle feed items use payload fields that already exist:
`tsunami:hit.natural`, `war:declared.blamed`, `climate:milestone.culture`, `settlement:destroyed.cause`. For
example: "The sea rose: the Vai's engines", "War, blamed on you".

**Sim helpers.** Read-only, no new state:
- `civ::forecast(w, sid)` returns `{ yearsToHunger, storesYears, nextTierYears, nextBuilding, threats
  (situation ids) }`;
- `climate::project(w, years)` returns `{ anomaly, seaRise, topEmitter }`, a linear projection of the last
  50 years;
- `civ::ageEta(w, culture)` returns years at the current rate.

**Headless test.**
- SimTests check that each helper is read-only (the fingerprint is unchanged after calling it) and accurate on
  synthetic worlds (a town at known K and growth reports hunger within ±1 year; a constant emitter projects
  within 10 %).
- The client scenario `hud-loops` captures each overlay and the forecast tooltips, then checks that the text
  fits its panel and that every number is finite.

**Effort / owner files.** **M.**
- Helpers: `Civ.cpp` / `Economy.cpp` and `Climate.cpp`.
- Overlays: Control (mode list), Planet (render), HUD (legend).
- Ledger, chips, tooltips and the per-culture regard dots: HUD.

### 3.6 HIGH: The first five minutes, an unmissable tutorial

**Why.** Owner playtest 1: 25 clicks on empty ground in 90 s, all `select -1`, and no power armed. Genesis is
the tutorial, but it must teach by doing, one thing at a time, and carry on into the first minutes of history.
The guide thread (7 steps) and the hints (7, now auto-dismissed after 12 s) exist. What is missing is a script
that ties them to the world's beats, auto-arming, and an escalation path for a player who is stuck.

**Rule (client flow, plus two small sim rules).**

- **Each genesis phase arms its own power** (comet, seed, awaken), and the palette shows only that power plus
  Raise and Lower.
- **A click with nothing armed** shows "Choose a power below (or press its key), then click the land" for 4 s.
  After 3 such clicks, the palette pulses and an arrow points at it.
- **The first game gets the tutorial** (a "tutorial" flag stored with the hints). Speed is capped at 2x until
  the Bronze age unless the player picks "skip the lessons" in the setup screen.
- **Sim: a second trial.** `DisastersTune::trial2Year = 150`: a natural lightning fire on forest 3-4 hops from
  the second camp, once per world. It bypasses the protection rules like the first trial, but never within
  0.15 rad of a centre.
- **Sim: a guaranteed early settler.** The first settler party may leave from year 50. This already holds with
  the Stone cooldown of 60; the test pins it.

**The beat sheet** (real time from New World, at the tutorial's capped speed; each beat is done by its action,
or moves on after its fallback time):

| time | world | prompt (one at a time) | done when | fallback |
|---|---|---|---|---|
| 0:00 | Molten, cooling | "Drag to turn your world. Scroll in to come close; scroll out to see it whole." | turned ≥ 30° and zoomed in and out | 25 s |
| 0:25 | Water (Comet armed) | "Click to call a comet. Each one fills the seas." The water meter shows the good band | sea ≥ -3.5, then **Next** | the meter pulses after 30 s |
| 1:20 | (optional) | "The clay is soft: hold U to raise land, J to lower it." | one stroke | 15 s, skippable |
| 1:40 | Life (Seed armed) | "Touch the land. Life spreads from your hand." | veg ≥ 0.25, then **Next** | 40 s |
| 2:30 | People (Awaken armed; habitability overlay forced, as today) | "Green land feeds people. Choose where they wake." | awakened | invalid targets explained in words |
| 2:45 | year 0: camera flies to the camp and selects it (exists) | step 1 "Find your people": the inspector says "30 people. The land could feed 90." | the inspector was read 3 s, then closed | 10 s |
| 3:10 | year 45: the first trial drought; the **Hunger** situation card | "Dry years at Ta Kripa. They will pray. Press **Show me**." (arms Rain, frames the town) | the prayer was answered (year 46+) | the card's answer button pulses |
| 3:20 | the answer: toast, ledger, a chronicle line | ledger: "Prayer answered: faith +, +1 year of learning" | (shown) | — |
| 3:35 | year 60-120: the first settlers leave | "Settlers. Hallow ground (G) where you want the next ones to go." | a hallow stroke | 20 s |
| 4:05 | year 150: the second trial, a forest fire near the second camp; the **Wildfire** card | "A fire. Rain puts it out, or let it burn: ash makes rich soil." (two real answers) | either choice, or it burns out | — |
| 4:30 | after the second act | step 3 "How they will remember you": the remember map opens once | read 4 s | 10 s |
| 4:45 | the first shrine town | "Give Ta Kripa a calling (C): Harvest feeds more; Faith fills your Awe." | a calling chosen | 20 s |
| 5:30 | Bronze (year ~310-330) | the age card: "Bloom, Earthquake, Inspire and Consecrate wake. New calling: Defence. Watch for wars brewing." | (read) | 8 s |

After Bronze, the guide thread continues with its remaining steps (prayer, shrine, chronicle, prophet) on
their own triggers, and the speed cap lifts.

**UI.**
- The guide card shows one step at a time, as today.
- Genesis prompts sit in the existing genesis guide box.
- The **Show me** button sits on situation cards.
- The tutorial and its lessons can be turned off in the setup screen and in settings.

**Headless test.**
- **Client scenario `first-five`** (`Game/Flow/DmFlowScenarios.cpp`): a "newcomer" input bot does exactly what
  each prompt says. The scenario asserts:
  - each beat fires by its time ±15 s;
  - never two prompts at once;
  - each prompt is visible for ≤ 15 s after its condition is met;
  - the prayer is answered and the fire card offered two answers;
  - the world reaches Bronze by 5:45.
- **Scenario `first-five-idle`** (no input): the escalations fire (pulse, arrow), at least 6 beats are shown,
  and nothing blocks the world from reaching Bronze.
- **SimTests:** trial 2 fires once at year 150 ±1, outside 0.15 rad of every centre, and only when the second
  camp exists; the first settler leaves by year 120 on 8/8 seeds.

**Effort / owner files.** **M.**
- The beats, the speed cap and Show-me: `Game/Flow/DmFlow*`.
- Steps, prompts and arrows: `Game/UI/Hud/DmHudModel.cpp`, `SDmHudGuide.cpp`.
- Auto-arm and empty-click tips: `Game/Input/DmControl.cpp`.
- The trial: `Sim/Disasters.cpp`.

### 3.7 MEDIUM-HIGH: Setups for replayability and difficulty

**Why.** Different starting conditions make different strategies best and give the player reasons to play
again. Today `Settings` holds only `calamity`.

**Rule.**

- **`Settings` gains these fields** (persisted; the setup screen sets them; defaults keep today's game):
  - **`shape`**: Continents (default) / Pangaea / Archipelago / Twin lands / Highlands. These are parameters
    of `terrain::generate`: land-fraction target within 30-45 %, landmass-count bias, ridge strength and
    shelf width. Terrain owner. Archipelago means many small peoples, sea trade and Seafaring. Pangaea means
    long frontiers and war. Highlands means rivers, mines and floods.
  - **`temperament`** of the first people: Balanced (default) / Devout / Warlike / Curious / Seafaring /
    Random. Root cultures roll traits with the chosen one's mean +0.25 (`society::newCulture`). Daughters
    drift as today. This steers ambitions (3.4) and so which prayers and situations dominate.
  - **`peoples`**: 1 (default) / 2 rivals. With rivals, the People phase awakens twice, and the second
    awakening must be on another landmass or at least 0.6 rad away. The war, strangers-meet and Ark-race
    situations come early.
  - **`patience`** (difficulty): Gentle / Mortal (default) / Harsh. It sets the prayer timeout (25 / 15 / 10
    years), the unanswered penalty (0.03 / 0.05 / 0.08), jaded gain (× 0.7 / 1 / 1.3) and Awe income (× 1.2 /
    1 / 0.85). Calamity stays a separate choice.
- **Vows (optional goals).** The god may take one vow at New World. It is tracked by a read-only `Sim/Vows.cpp`
  from events and stats and judged at the ending. A kept vow is a × 1.25 score multiplier and a line on the
  planet card. The vows:
  - **Carried to the Stars**: a beloved Ark;
  - **The Hammer**: a feared Ark;
  - **No Hunger**: no town destroyed by famine;
  - **Short Wars**: no war lasts more than 20 years after Iron;
  - **Many Peoples**: 4 or more cultures alive at launch;
  - **Swift Stars**: an Ark before year 3,200;
  - **The Quiet God**: at most 20 acts per century, and still not forgotten;
  - **Gardener**: vegetation cover ≥ 0.6 and anomaly ≤ +2.5 °C at the Ark.
- **Challenges.** Scripted starts built by `sim::challenge(w, id)`: `genesis::skip`, then a fast-forward with
  the passive policy to a target age, then setup through the owners' APIs. Each has a goal and a time limit:
  - **The Drowning Coast**: Industry, the sea rising; save 8 of 12 coastal cities;
  - **Two Brothers**: two peoples on one continent, `vSlow` at -0.35; no war for 300 years;
  - **The Long Winter**: dust 0.6 and an ice age; keep 3 peoples alive for 400 years;
  - **The Silent God**: Iron, devotion 0.15 everywhere; reach beloved;
  - **The Plague Years**: three cities ill; lose fewer than 30 % of the people.
- **The setup code.** The planet card prints the seed and a short setup code (for example `4417-AR-W-H-V2`), so
  a friend can replay the same world.

**UI.**
- The setup screen (Screens) gets these rows: name, seed, shape, temperament, peoples, patience, calamity, vow,
  and "skip the lessons".
- The title gets a **Challenges** page with five cards.
- The ending screen and planet card show vow results and the setup code.
- The HUD shows the vow as a small line under the remember map, with its state (kept / at risk / broken).

**Headless test.**
- simrun gains `--shape`, `--temperament`, `--peoples`, `--patience`, `--vow` and `--challenge`.
- **Variety targets** (5 shapes × 4 seeds, passive):
  - land fraction and landmass counts are inside each shape's band;
  - every shape still meets the SPEC-SIM 4 Ark window;
  - Archipelago has ≥ 1 more culture at year 2,000 than Pangaea on ≥ 3/4;
  - Warlike's war share is ≥ 2 × Devout's on ≥ 3/4;
  - Rivals has its first war ≤ year 900 on ≥ 3/4.
- **Vows:** each vow has a policy that keeps it and one that breaks it.
- **Challenges:** setup fingerprints are reproducible; a scripted policy reaches each goal; passive fails ≥ 3
  of the 5.

**Effort / owner files.** **L** (splits cleanly).
- Shapes: `Terrain.cpp`.
- Temperament, rivals and patience: `Society.cpp`, `Powers.cpp` (two awakenings).
- Settings and save: `State.h`, `Save.cpp`.
- Challenges: `Sim.cpp` or a new `Sim/Challenges.cpp`.
- Vows: a new `Sim/Vows.cpp`.
- Setup, Challenges page and ending: Screens.
- Options: `Tools/simrun`.

### 3.8 MEDIUM-HIGH: The race to the stars and the endgame

**Why.** Today the Ark is a timer at the end. The verdict that counts belongs to the culture that launches
first, so the endgame can become a real strategic choice: which people carries the god's name?

**Rule.**
- The Ark changes of 3.1 (Inspire +0.02; wrath -setback/250).
- The race-to-the-stars situation.
- **(new, Diplomacy)** In the Space age, a war's goal is the rival's Ark capital: armies target it first, and a
  captured Ark capital loses its Ark progress (the existing capital re-election already resets the launch
  site).
- **The Ark bar** (exists) shows every racing culture's progress as stacked lines with culture dots, and the
  mood each would bring ("the Vai would leave you beloved; the Oru, feared").

**Headless test.**
- `kingmaker` (3.4) ends with its chosen culture launching on ≥ 5/8.
- A `spoiler` policy (strikes every leader's Ark capital) delays the Ark by ≥ 150 years and still ends inside
  4,500 years on ≥ 6/8.
- A synthetic test checks that a meteor on an Ark capital subtracts 0.16.

**Effort / owner files.** **S-M.** `Society.cpp` (Ark), `Diplomacy.cpp` (war goal), HUD (Ark bar).

### 3.9 MEDIUM: Pacing and time pressure

**Why.** Real-time pressure only works if the important moments happen at a speed where the player can act.
At 8x, a 40-year window is 2.5 s. Finding 13 asked for auto-pace, and simrun models it (27-32 minutes), but
the client has none yet.

**Rule (client flow, using the sim's `situation` events).**
- **Auto-pace v2** (on by default in the first game, an option afterwards):
  - 2x while no situation is at Rising or above;
  - 1x while one is at Crisis or a new prayer is open;
  - at most 2x while a Crisis touches a capital;
  - a speed key pressed during a Crisis is obeyed for that Crisis (the player's choice wins).
- **"Pause on crisis"**: off by default, on during the tutorial.
- **"Slow down for great events"** (exists: weight-3 entries drop to 1x for 6 s) stays.
- **Decision density by age.** Each age adds at least one situation kind and one calling (the tables in 3.1 and
  3.2), so no age is only "the same, faster".
- **The Awe budget for answers.** Late income is about 2.7 Awe a year, which is 5.4 Awe/s at 1x. An Omen
  (100) takes about 18 s of income and a Calling (30-120) about 6-22 s. With 1-3 open matters a minute, the
  player can answer about 2-3 a minute plus small acts. They cannot save everyone, which is intended.

**Headless test.**
- simrun `--autopace` uses the v2 rule from `situation` events. Targets:
  - a total of 25-35 minutes;
  - no real minute without an open matter or a notable entry;
  - the share of each crisis's window spent at ≤ 1x is ≥ 70 %.
- Client scenario `autopace`: synthetic situations drive the speed changes, and a player key press overrides
  for that crisis only.

**Effort / owner files.** **S.** `Game/Flow/DmFlow.cpp` (auto-pace), Screens settings (options),
`Tools/simrun/simrun.cpp` (model).

### 3.10 MEDIUM: A world that visibly reacts

**Why.** The owner asked for real-time reactive situations, simulations and perceptions: the player must
*see* the people respond, at every zoom. The sim state already exists. This is mostly client work in owned
folders, and it survives the later asset swap because it is driven by state, not by meshes.

**Rule (client, driven by existing sim fields and events).**

| state | close (≤ 2 km) | medium (≤ 50 km) | orbit |
|---|---|---|---|
| hungry (stores draining / famine) | brown fields, thin smoke, fewer lit windows | the town label shows a hunger icon | the Hunger overlay |
| plague | empty streets, white cloth on doors | a pale miasma (fx exists) | — |
| praying | lanterns; a light column at the temple at night | a prayer icon with its kind | the Situations ring |
| answered | a lantern bloom; a short music stinger | a gold pulse | a gold pulse |
| at war | banners; army columns (units exist); burning | red frontier (Tension overlay) | red frontier |
| preacher | a dark-robed crowd in the square | a crowd marker and a following ring | — |
| calling | districts grow by calling (3.2) | a calling badge | the Callings overlay |
| hallowed / shunned | shimmer / veil on the ground | shimmer / veil | the Zones overlay |

Audio: one stinger per situation stage (omen: low, crisis: rising, answered: resolved, lost: falling), within
the existing limiter and mix rules.

**Headless test.** Client scenarios `react-<state>` capture each state at three zooms from a seeded world. The
audio scenario checks that stingers stay below the existing peak and LUFS targets.

**Effort / owner files.** **M.** Towns (`Game/Towns/*`), FX (`Game/FX/*`), Audio (`Game/Audio/*`), HUD labels.

## 4. Decisions by age: what the player weighs

| age (1x minutes) | main dilemmas |
|---|---|
| Stone (0-2.7) | Rain the dry camp, or let the fire burn for ash? Hallow far ground (more land, but towns drift away) or near ground (one tight people)? Harvest or Faith for the first shrine town? |
| Bronze (2.7-7.8) | Strangers meet: pave a road for trade, or shun the land between? Consecrate now or save for the first war? Answer the first Victory prayer and make an enemy? |
| Iron (7.8-13.9) | A preacher: spend your one prophet on him (and lose your edicts) or smite him (and risk a martyr)? Heal the plague's source or the ring? Trade towns spread plague faster. |
| Sail (13.9-20.3) | Seafaring colonies far over the sea will drift away: a road home, or let a new people rise? Learning or Faith in the capital? |
| Industry (20.3-25.6) | Industry towns learn fast and warm the air: which peoples do you let burn coal? Lift the coastal towns, or shun the coast? |
| Atomic (25.6-29.8) | Fire weapons: an Omen now (100) or wait for exhaustion? Strike the smokiest culture back? |
| Space (29.8-31.7) | The race: which people carries your name to the stars? Inspire the ones who love you, or strike their rival's Ark? |

## 5. Balance guardrails (for the balance owner)

- **Every target stays.** SPEC-SIM 4 and the v3 design-review targets must still pass on 8 seeds × every
  policy after each phase. The v3.1 targets of the findings 11/13 track must also pass once that track lands.
- **Passive should not change unless intended.** Callings, zones, pave, vows and the new powers have no effect
  unless the god uses them, so passive summaries must match except for new columns.
- **Rules that do change passive must be re-verified one at a time,** each with a simrun before/after table:
  stores, `vSlow`, preachers, ambitions and the new prayers, the nuclear ramp, precursors and trial 2.
  Expected effects: famine entries fall; war onset shifts later and more readable; one or two schisms more.
- **The policy matrix grows by:** `responder`, `firefighter`, `diplomat`, `steward`, `zoner`, `warden`,
  `builder-roads`, `kingmaker` and `spoiler`, plus one keeper and one breaker per vow.
- **Performance:** mean tick ≤ 2.5 ms (now about 1.9 ms) and any tick ≤ 30 ms. The Situations yearly pass
  must stay ≤ 0.15 ms and Vows ≤ 0.05 ms.
- **Determinism:** `simrun --hash` three times per seed for 8 seeds × 4 policies with the final code (the
  release notes' rule), plus a save/load round trip of every new field.

## 6. Implementation plan: parallel tracks by file ownership

**Gate.** Nothing here starts until the current owners finish: the wave-3 workflow owns everything under
`Unreal/Source` and `Unreal/Config` (towns and planet reviews, then the whole-game final check), and the
simulation track owns `Sim/` (findings 11/13, v3.1). v4 starts from their merged result.

### Phase 0: contracts (1 agent, S; must land before any track)

One owner writes every shared declaration at once, as the foundation-api step did, so no two tracks edit the
same header:

- `Sim/Situations.h`: the `Situation` struct, its enums, the API (`yearly`, `attach`, `answers`, `list`) and
  its tune struct.
- `Sim/Vows.h`.
- Stubs in `SimTests/stubs/`.
- `State.h` / `Schema.cpp`:
  - `Settlement::calling` and `stores`;
  - `Relation::vSlow`;
  - `World::situations`;
  - the per-cell `zone`;
  - the new `Settings` fields;
  - the new `PrayerKind` values;
  - `SettleReason::Shunned`.
- `Core/Events.h`: `situation`, `preacher`, `calling`, `volcano:omen`, and the new prayer kinds in `prayer` and
  `prayer:answered`.
- `Powers.h`: the `call`, `hallow`, `shun` and `pave` definitions (`shun` with no letter key), and
  `UseOpts::choice`.
- `Civ.h`: `forecast`, `ageEta`. `Climate.h`: `project`. `Society.h`: `pave`.
- The `Save.h` format note for **save v2**, readable from v1.
- **SPEC-SIM v4 sections** (rules and numbers from this document, marked "(v4)") and the writers-table rows:
  - `zone`: Powers (decay: Civ);
  - `calling`: Powers through Society (set) and Society (forget);
  - `stores`: Economy;
  - `vSlow`: Diplomacy;
  - `situations`: Situations.

### Phase 1: simulation tracks (parallel lanes; each owns only its files)

| track | owns | builds | effort | needs |
|---|---|---|---|---|
| **S1 situations** | `Sim/Situations.cpp`, `SimTests/test_situations*.cpp`, `stubs/Situations_stub.cpp` | the module and the catalogue's detectors (MVP six first, then the rest) | L | P0 |
| **S2 society** | `Sim/Society.cpp` | ambitions, Victory / Wisdom / Sign prayers, preachers, callings' devotion / S / forgetting, situation outcomes into memory, the Ark hooks, temperament, patience | L | P0 |
| **S3 diplomacy** | `Sim/Diplomacy.cpp` | `vSlow` war gate, the nuclear ramp, `pave`, Trade / Defence callings on relations, the Space-age war goal | M | P0 |
| **S4 civ-economy** | `Sim/Civ.cpp`, `Sim/Economy.cpp`, `Sim/Path.cpp` | stores, callings' K / buildings / research / CO₂, zones in siteScore / canSettle / decay, shunned army cost, `forecast`, `ageEta` | L | P0 |
| **S5 powers-disasters** | `Sim/Powers.cpp`, `Sim/Disasters.cpp` | the `call` / `hallow` / `shun` / `pave` powers, raising a warned town, trial 2, eruption precursors and venting, rivals' second awakening | M | P0 |
| **S6 world** | `Sim/Sim.cpp`, `Sim/Terrain.cpp`, `Sim/Save.cpp`, `Sim/Climate.cpp`, new `Sim/Challenges.cpp`, `Sim/Vows.cpp` | the schedule line for Situations, shapes, save v2 (+ v1 upgrade), `climate::project`, challenges, vows | L | P0 |
| **S7 chronicle** | `Sim/Chronicle.cpp` | prose for situations (Crisis and end only, through the budget), preachers, callings, vows, new prayers; cause words | M | P0 (events) |
| **S8 tools** | `Tools/simrun/*`, `SimTests/test_gameplay_v4*.cpp` | the nine new policies, `--situations`, `--autopace` v2, the setup flags, all targets of this document | M | P0; can start at once with stubs |

Order inside Phase 1, by value:
1. S1 (MVP kinds), S4 (stores, callings, zones) and S8 start together.
2. S2 and S3 follow as soon as their test worlds exist.
3. S5 and S7 next.
4. S6's save v2 must land before any track's save/load test can pass, so S6 does save v2 *first*, then
   shapes, challenges and vows.

### Phase 2: balance (1 agent, M)

The balance owner (`Sim/Balance.cpp`, SPEC-SIM section 4) merges the tracks. For each rule that changes passive
(section 5), the owner runs simrun at 8 seeds × every policy, tunes it, and writes before/after tables to
`docs/handoff/gameplay-v4/`.

### Phase 3: client tracks (parallel lanes; start when Phase 0 headers exist, using stubs; finish against Phase 2)

| track | owns | builds | effort |
|---|---|---|---|
| **C-hud** | `Game/UI/Hud/*` | the Situations panel (cards, countdowns, answer buttons, Show me), the calling row, forecast lines, act ledger, cause chips, vow line, age card, per-culture regard dots, Ark race bar, overlay legends | L |
| **C-flow** | `Game/Flow/*` | the first-five beat script and speed cap, auto-pace v2, pause on crisis, the `first-five` / `first-five-idle` / `autopace` scenarios; the playthrough god uses `responder` + `steward` | M |
| **C-input** | `Game/Input/*` | genesis auto-arm, the empty-click tip, Shift+G / Shift+C chords, the Tend group's brush ring colours, the C key for Calling in history | S |
| **C-screens** | `Game/UI/Screens/*` | setup rows, the Challenges page, settings (auto-pace, pause on crisis, lessons), ending and planet card (vows, setup code) | M |
| **C-planet** | `Game/Planet/*` | Zones / Hunger / Tension / Callings overlays, the hallow shimmer and shun veil in the ground material | M |
| **C-towns** | `Game/Towns/*` | districts by calling, reactive states (3.10), preacher and army markers | M |
| **C-fx-audio** | `Game/FX/*`, `Game/Audio/*` | situation rings and pulses, answered blooms, stage stingers | S |

`Game/Core` (bridge) needs only that `OnSimEvent` routes the new events. That is one edit by its owner, on
request.

### Phase 4: whole-game check (1 agent, M)

- Replay the owner's playtest path (the release notes).
- Run `first-five` and `first-five-idle`.
- Run a full playthrough with the `responder` + `steward` god, with screenshots at close, medium and orbit
  zoom.
- Run simrun with 8 seeds × every policy, `--hash` three times.
- Check perf at 1080p High (60 fps target).
- Write `docs/handoff/done-ue/gameplay-v4.txt`.

### Critical path and total

The critical path is Phase 0 (S), then S1 + S4 (L), then Phase 2 (M), then C-hud (L), then Phase 4 (M):
about 6-8 agent-days of wall time with 8 sim lanes and then 7 client lanes running in parallel (within the
machine's six-agent cap, run S1-S6 first and S7-S8 as lanes free up). Total effort is about 25-30
agent-days.

## 7. Risks and owner decisions

1. **Clutter.** Thirteen situation kinds and four overlays could swamp the screen. Mitigations: at most 4
   cards, folding, the rule that a situation needs two answers, and the MVP of six kinds first. *Owner:
   approve shipping the MVP six before the rest?*
2. **Stores change how famine feels.** There are fewer, later famines with a visible countdown. *Owner: keep
   famine fast and brutal, or windowed as proposed?*
3. **Calling cost.** Proposed as Awe that rises per calling held. The alternative is free callings capped by
   the number of temples. *Owner's pick.*
4. **Keys.** Every letter is taken (powers plus camera W A S D Q E R F, and H for the HUD). The proposal reuses
   the genesis-only keys G (Seed) and C (Comet) in history and adds the chords Shift+G and Shift+C. *Owner:
   accept chords?*
5. **Auto-pace and pause on crisis** are defaults that change how real-time the game feels. Proposed: auto-pace
   on in the first game and an option after; pause on crisis off except in the tutorial.
6. **Save v2** breaks nothing, because v1 saves load with empty new fields, but every save test must be
   re-baselined.
7. **Determinism.** Situations and Vows read the bus. Their handlers must be re-subscribed exactly once (as
   Chronicle does) and must never draw rng.

## 8. The ten things to build first

1. Phase 0 contracts and SPEC-SIM v4 (3.1-3.8 rules as text).
2. Situations MVP: Hunger, Plague, War brewing, Preacher, Sea, Golden age (3.1), with `vSlow` and stores.
3. Callings (3.2).
4. Hallow, Shun and Pave (3.3).
5. The Situations panel with answer buttons and Show me (3.1 UI).
6. The first-five beat script, auto-arm and the empty-click tip (3.6).
7. Victory / Wisdom / Sign prayers and preachers (3.4).
8. Forecast lines, the act ledger and the Hunger / Tension overlays (3.5).
9. Auto-pace v2 (3.9).
10. Setups: shape, temperament, rivals, patience, then vows (3.7).

Each has its headless test above. Together they need about 30 new simrun targets, 9 new scripted policies
(plus vow keepers and breakers), 6 new SimTests files and 5 new client scenarios.
