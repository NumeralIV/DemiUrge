# DEMIURGE: Astra review of the current game rules

*Reviewer: Astra, T-006, 2026-11-20. Design review only; no simulation, client or spec source was
changed.*

## Scope and evidence

This review asks whether the **current v3.1 rules**, after the earlier design-review fixes, make one
30–40 minute history enjoyable to watch and meaningful to influence. It is based on:

* `docs/CONCEPT.md`, all of `docs/SPEC-SIM.md`, and `docs/SPEC-UI.md` sections 4, 4b and 5;
* the current C++ reference behaviour described by those specs;
* the final 8-seed × 17-policy, 4,500-year measurements in
  `docs/handoff/sim-findings-2/simrun-after-8seeds-all.txt` and its before/after tables;
* the previous review, so fixes already present are not reported again as current defects.

The evidence is headless simulation evidence, not a human playtest. It can show pacing, incentives and
outcome collapse; it cannot prove that camera work, spectacle, sound or moment-to-moment feel are fun.
The highest-risk proposals below therefore end in both a headless gate and a short human-playtest
question.

## Verdict

The current game can produce a strong history, and it now remembers the player correctly. Long memory,
all four Ark moods, the first drought and prayer, age-gated powers, alliances, blame, thanks and the
Omen of Peace are meaningful improvements. A passive world reaches its Ark in roughly the promised
length, and different broad policies now reach different endings.

The central remaining problem is **not a lack of events; it is a lack of decisions**. The best kind
policy makes 1,266–1,924 brush strokes plus 57–75 clicks in one history. It is rewarded for acting like a
paint bot. The same runs generate 816–1,098 prayers, while a passive history generates 467–759. By the
Industry age the simulation produces roughly 49–82 measured “moments” per real minute depending on
policy. The player is therefore caught between two bad modes: hold a benevolent brush down and clean up
requests, or watch a torrent of outcomes that were seldom forecast early enough to influence.

The next design pass should turn **many reactions into fewer commitments**:

1. one cast should create a durable weather response instead of demanding repeated brush ticks;
2. many settlement prayers should become one legible cultural plea;
3. wars, hunger, disease and rising seas should become warnings with two or more answers;
4. stored Awe should create a real timing trade-off;
5. the Ark should be a finale the player can influence without buying a last-minute moral verdict.

## What already works and should be protected

* **The ending is historical now.** `Society.memory` prevents the last two minutes from erasing the
  previous half hour. The eight-seed policy matrix cleanly separates passive/forgotten, kind/beloved,
  cruel/feared and chaotic/parting.
* **The opening has an authored first beat.** The year-45 drought and first prayer give the player a
  readable invitation to act, and the second culture usually appears early enough to make the planet
  socially interesting.
* **Powers are physically connected to the world.** Rain, vegetation, fire, terrain, water, fertility,
  population and climate are not isolated meters. Any new strategy layer should use those systems, not
  replace them with abstract cards.
* **The god does not directly command people.** This is the game's identity. Recommendations below mark,
  bless, warn, promise or reshape; they do not turn the game into unit orders.
* **The chronicle and time-lapse make consequences legible after the fact.** The missing layer is
  legibility *before* a consequence, not more retrospective prose.

## Findings, ranked by player impact

Each finding gives the problem, the controlling spec section, a concrete change, and a headless test.

### 1. CRITICAL — Kind play is continuous painting, not judgment

**Problem.** Rain and Bloom are per-real-second brushes with no regional refractory period. A helpful
script can always spend another fraction of a second on another town, gain another witnessed act and
answer another prayer. Across the current eight-seed reference, `kind` uses **1,266–1,924 strokes** and
57–75 clicks; even `prayers` uses 413–586 strokes. Over a 30-minute history that is approximately one
successful brush application every one to one-and-a-half seconds. The input may look pleasant, but the
optimal behaviour has no pause for observation or choice.

**Spec.** SPEC-SIM 3.10 (`Powers.cost`, brush calls, Rain, Bloom and witness faith); 3.8 (prayer
answers and jadedness); section 4 (“a small miracle every 3–8 s later”). SPEC-UI 4 (armed chip and brush
size).

**Proposed change.** Keep the brush as the physical gesture, but make one deliberate stroke create a
**weathering episode**:

* after 0.4 s of continuous painting in one connected region, commit one episode lasting 15 sim years;
* Rain continues to add wetness and suppress fire over that region; Bloom continues to restore
  vegetation/fertility toward one capped target;
* its cost is paid once from area and duration: 25 Awe at the default radius, quadratic with radius;
* repainting the same cells refreshes the visual edge but gives at most 10% extra effect, no additional
  memory and no second prayer answer until the episode ends;
* at most three player-created weathering episodes may be active. Starting a fourth ends the oldest.

This preserves tactile painting and spectacle while making the player decide **where the next fifteen
years of attention belong**.

**Headless test.** Add episode counts and effective brush-seconds to simrun. Across eight seeds, a new
`responder` policy must answer at least 65% of urgent rain/bloom pleas, end beloved on at least 5/8, and
use at most 120 weathering episodes and 180 effective brush-seconds. Replaying the old `kind` input must
not earn more than 15% additional grace or progress than `responder`. All existing famine-recovery and
ending-mix targets must still pass. Human check: after ten minutes, ask whether the player remembers why
each of the last three regions was chosen.

### 2. CRITICAL — Hundreds of settlement prayers become clerical work

**Problem.** At most six prayers are concurrent, but that limit hides the throughput. Passive runs begin
467–759 prayers; kind runs 816–1,098; the prayers policy begins 853–1,279. The Now strip can therefore be
technically tidy while the player experiences an endless series of near-identical “rain here” jobs.
Answering is rewarded individually, so aggregation in the HUD alone would conceal the incentive without
fixing it.

**Spec.** SPEC-SIM 3.8 (Prayers, one per settlement, 15-year timeout, at most six); 3.10 (matching
miracles); SPEC-UI 4b (Now strip) and 4 (first-prayer hint).

**Proposed change.** Turn settlement requests into **cultural choruses**:

* a settlement still records its local need, but a culture exposes at most one active chorus of each
  kind (`rain`, `heal`, `peace`, `bloom`), containing up to eight affected settlement ids;
* severity is affected population share plus 0.25 if a capital is included; the HUD shows “the Velessan
  ask for rain — 4 towns, 31% of their people”;
* one appropriately placed episode/click answers the chorus in proportion to the people actually
  reached; full credit requires reaching at least half the affected population;
* after resolution or timeout, that culture/kind has a 25-year cooldown unless severity increases by
  at least 0.25;
* memory, prayer counts and progress are awarded once per chorus, scaled by affected population—not
  once per town.

**Headless test.** Report local needs separately from player-facing choruses. Across passive seeds,
local needs may remain high, but player-facing choruses must total 40–140 per history, peak at no more
than four concurrent, and repeat the same culture/kind no sooner than 25 years unless severity rises.
The `prayers` policy must answer at least 70% of affected population with fewer than 150 casts, and its
beloved rate must remain at least 6/8. Human check: a player should be able to recount the two most
important unanswered pleas without reopening the panel.

### 3. CRITICAL — The game reports crises after the decision window has closed

**Problem.** The Now strip lists wars, famine, plague, prayers and active disasters. Most entries appear
only when the bad state already exists. War starts at the relation threshold; hunger starts population
loss as soon as `pop > K`; plague and storms begin without a forecast; the sea-rise glyph identifies a
low town but not how quickly it is becoming unsafe. The player can repair damage, but rarely chooses
among preventive answers. This is the largest source of powerlessness.

**Spec.** SPEC-SIM 3.7 (growth/famine and sea hazards), 3.8 (relations and war), 3.10 (natural events),
section 4 (war timing); SPEC-UI 4b (Now strip) and 4 (status glyphs).

**Proposed change.** Add a read-only, deterministic `Society.risks(world)` result and make the Now strip
a **Risks and pleas** list. Return at most four items sorted by population at stake, each with
`kind`, `stage`, `cell`, `culture`, estimated years, plain causes and two or more valid power answers.
The first five risks are:

* **Hunger:** `K` is below the five-year population projection; Crisis when food stores reach zero.
  Add 4 years of stores, 8 with a granary, so there is a real warning window.
* **War:** use a 20-year exponential moving average of relation (`vSlow`) for the declaration gate;
  warn at −0.25 and expose the strongest causes (border, envy, zeal, disease, aggression).
* **Plague:** warn neighbouring/trade-linked towns when their calculated infection exposure passes 20%.
* **Sea:** project the current sea trend to the settlement centre and warn at 100 and 30 years.
* **Fire:** warn when a burning cell can reach owned dense vegetation within three hops.

Every risk must list at least two materially different answers or remain a normal notification. For
example, hunger accepts Rain, Bloom, or a Consecrated harvest patronage; war accepts Omen, a peace
Prophet, or strengthening the weaker culture with Inspire.

**Headless test.** For every famine, declared war, multi-town plague and sea loss in eight passive seeds,
record the first warning. At least 90% must have a warning 10–120 sim years before Crisis; false warnings
that never reach Crisis or resolve naturally must remain below 35%. A `responder` using the cheapest
listed answer must lose at least 40% fewer people to those causes than passive with no more than 120
interventions. Human check: pause immediately after a crisis and ask “what were my options?”

### 4. HIGH — Awe is a cooldown disguised as an economy

**Problem.** A passive god sits at the cap in **88–89% of years** in the current eight-seed run. The spec
already marks the passive ≤50% target as unmet. The current hoarding penalty pushes devotion down, but a
floor preserves enough devotion for the shrine/cap loop, and a full stockpile remains available for a
late burst. Conversely, active kind play is near empty because it can always spend on another brush
tick. Neither state creates a choice between saving and acting.

**Spec.** SPEC-SIM 3.8 (belief, `hoardFloor`, Awe income and cap), 3.10 (cost table), section 4 (Awe
cadence and cap targets); SPEC-UI 4 (Awe orb).

**Proposed change.** Make Awe an **attention reserve** with a soft holding cost:

* after 20 years with no witnessed player deed, Awe above 60% of cap decays by `0.004 × aweMax` per
  year until it reaches 60%; the first witnessed deed resets the idle timer;
* while below 15%, income receives a 25% recovery bonus, preventing an engaged player from being locked
  out after one expensive intervention;
* show the state on the orb: “quiet power is fading” above the soft cap, or “faith is rallying” in
  recovery;
* keep the hard age cap and durable Consecrate/Wonder purchases. Do not convert overflow into free
  permanent value.

This creates three readable choices: spend now, hold a moderate reserve, or deliberately accept that
unused power fades.

**Headless test.** Replace the weak “active policy below 90% in 60% of years” check with reserve-band
checks: passive at ≥95% cap ≤20% of years; `responder` between 15% and 85% in 50–85% of years; no active
policy below 10% for more than 120 consecutive years; great powers remain affordable 25–75 seconds
after their age unlocks. `builder` must buy more durable works and finish with higher devotion than
`hoarder` on at least 6/8 seeds, not merely share the same beloved ending.

### 5. HIGH — The Ark is a two-minute finale with almost no non-destructive lever

**Problem.** Ark construction lasts about 250 sim years—roughly two real minutes at 1×. During it, new
deeds do not enter long memory, and no normal power directly helps or hinders construction. The player
can change short-term love or destroy/capture the capital, but cannot openly choose to bless a departure,
delay a feared culture, or back a rival. The UI announces a climax, then the rules mostly ask the player
to watch it.

**Spec.** SPEC-SIM 3.8 (memory freeze and Ark construction), 3.10 (Inspire/wrath), 3.12 (ending); SPEC-UI
4 (Ark bar) and 5 (ending screen).

**Proposed change.** Make the Ark a contestable project without allowing last-minute moral laundering:

* deeds during Ark progress 0–0.8 enter memory at 25% weight; memory seals only for the final 20%;
* Inspire on an Ark capital adds +0.02 progress, once per 25 years per culture;
* a research setback on an Ark capital also removes `setback / 250` progress (Meteor 0.16,
  Volcano 0.12, Quake 0.20 under the v3.1 tuning), never below zero;
* Consecrate on that capital creates a one-time “departure rite”: +0.05 Ark progress and a visible
  Chronicle commitment, but no direct verdict bonus;
* when multiple Arks exist, the HUD shows the leading two bars and clicking either flies to it.

**Headless test.** Add `ark-race`, `guide-first` and `delay-first` policies. Across constructed two-Ark
fixtures and at least six natural seeds, `guide-first` changes the winner or advances launch by at least
100 years in ≥4/6; `delay-first` changes the winner or delays launch by at least 100 years in ≥4/6;
`late-kind` still fails to buy beloved on ≥4/6 and `late-cruel` still fails to buy feared on ≥4/6.
No policy may hold the game without an ending past year 4,700. Human check: the player should be able to
state which people they helped leave, and why.

### 6. HIGH — “Beloved” can appear before the player has a history

**Problem.** In the current evidence, kind runs report **Beloved before year 20**. The initial tribe
starts at devotion 0.6 and love 0.3, and the first small acts can satisfy the public mood thresholds long
before long memory contains a meaningful sample. Showing an ending word that early is confusing: it
looks like the player has already won the moral arc, then may later fall out of it because of entirely
normal drift or neglect.

**Spec.** SPEC-SIM 3.8 (`Society.memory` and mood thresholds), 3.12 (ending mood); SPEC-UI 4 (top-bar
regard word and Ark caption).

**Proposed change.** Separate the **public standing** from the eventual ending classification. Add a
read-only `mature` flag to `Society.memory`: true only after year 250 and either `grace + wrath ≥ 1.0`
or at least ten resolved choruses. Before then, the HUD says **Unproven** and shows two directional
phrases—“they are growing fond” and “your deeds are becoming a story”—instead of Beloved/Feared/
Forgotten/Regarded. The internal verdict continues accumulating from year zero, and the actual ending
still uses the complete memory.

**Headless test.** No policy may expose a terminal public label before year 250. `kind` must become
publicly Beloved by year 800 on ≥6/8 seeds, `cruel` Feared by year 800 on ≥6/8, and passive Forgotten
only after at least 600 silent years. The final public label and Ark ending must agree in every run once
the Ark begins. Human check: a new player should describe the early label as a forecast, not a score.

### 7. HIGH — Late-game pacing is still measured by ratios when humans feel absolute load

**Problem.** The revised pacing is less back-heavy, but the absolute volume is still too high. The final
matrix reports roughly 49 passive and 72 kind sim moments per real minute in Industry; prayers reaches
about 82. That is close to or above one state-changing event per second before camera movement, power
input, labels and Chronicle banners. The ratio target (“late ≤3.5 × early”) can pass while both ends are
overloaded, and auto-pace saves only about a minute because it reacts to notable entries rather than
the player's unresolved attention.

**Spec.** SPEC-SIM section 4 (pacing target and moment definition); SPEC-UI 4 (toasts/banners), 4b
(Now strip and Chronicle feed), and settings (“slow down for great events”).

**Proposed change.** Distinguish simulation events from **attention events**. An attention event is a
new risk stage, a chorus, a culture birth/death, an age, a Wonder, an Ark step, a war start/end, a city
loss or a great disaster. Aggregate repeated battles, town-level famines, trade routes, fires and
building completions for 10 real seconds into one panel line and one optional Chronicle digest. Auto-
pace slows only when a risk enters Crisis or an epochal event occurs, and remains slow until the player
dismisses/folds it or 20 seconds pass.

**Headless test.** Report both raw moments and attention events. Target 6–18 attention events per real
minute at every age, no more than four in any rolling 10 seconds, at most three unresolved Crisis items,
and no 90-second span after minute one with neither a risk nor an attention event. Raw simulation rates
remain unconstrained. Human check: after an Industry minute, the player can name the most important
event without consulting the Chronicle.

### 8. HIGH — Saving a climate-threatened town is priced and remembered as cruelty

**Problem.** Raise/Lower on owned land costs four times normal and is remembered as harmful. That makes
sense for casual terrain surgery, but not for raising a town that the people's own emissions are about
to submerge. The HUD can show “sea rising”, yet the most intuitive rescue gesture is economically
punished and morally misclassified. The simulation's connectedness becomes a trap rather than an
understandable consequence.

**Spec.** SPEC-SIM 3.7 (sea hazard and evacuation), 3.10 (Raise/Lower cost, `good = −0.5` on owned land),
3.8 (memory); SPEC-UI 4b (sea-risk glyph).

**Proposed change.** When a settlement has a Sea risk at Rising or Crisis stage, Raise Land centred in
its territory costs the normal rate and is `good = +0.6` **only until** its centre reaches 0.15 above the
projected 100-year sea level. Credit the rescue once per settlement per 100 years and only if the stroke
increases that safety margin by at least 0.03. Raising beyond the safety line returns to ×4 cost and
`good = −0.5`. Lower Land retains its current warning and harm rules.

**Headless test.** A `coast-warden` policy must save at least half the towns passive would lose to `sea`
on ≥6/8 seeds, with no more than 12 rescue episodes per run. Every credited rescue must increase the
measured safety margin; repeated strokes above the margin earn no grace. The policy must not lower peak
population or delay the Ark by more than 150 years relative to passive. Human check: tooltip text before
the stroke must predict both the safety gain and how the people will read the act.

### 9. MEDIUM-HIGH — Atomic and Space offer upgrades, but not a new decision

**Problem.** Age gating is fixed, but the last two ages automatically improve an existing tool: Monsoon
for Rain, Star-fall for Meteor. They add scale and spectacle, not a strategic branch. At precisely the
moment climate pressure and the Ark race should sharpen the player's identity, the peaceful player gets
one cheaper brush and the wrathful player gets a larger crater automatically.

**Spec.** SPEC-SIM 3.10 (`kUpgrades`, Atomic Monsoon, Space Star-fall), section 4 (every late age unlocks
or upgrades a power); SPEC-UI 4 (new unlock pulse).

**Proposed change.** Make each late upgrade a one-time, irreversible **revelation choice**, chosen from
two and paid with 300 Awe:

* Atomic: **Monsoon** (current Rain upgrade) or **Mercy** (Heal also grants 30-year immunity to every
  town within 0.08 rad, but costs 80).
* Space: **Star-fall** (current Meteor upgrade) or **Guiding Light** (Inspire may advance an Ark as in
  finding 5, and its research effect doubles, but costs 100).

The unchosen revelation remains unavailable for that history. The Chronicle records the choice, making
it part of the god's authored identity rather than a hidden stat increase.

**Headless test.** Add `monsoon`, `mercy`, `star-fall` and `guiding-light` policies. Each must spend the
same 300-Awe entry cost. Mercy must reduce plague deaths by at least 40% versus Monsoon on ≥5/8 seeds;
Monsoon must end at least 40% more drought choruses; Guiding Light must change/advance an Ark as specified
in finding 5; Star-fall must create larger craters without producing a higher beloved rate. No choice
may dominate the other on ending score across more than 6/8 seeds.

### 10. MEDIUM — Climate has an outcome target but no agency target

**Problem.** The balance contract says passive worlds peak at +2 to +5 °C and gain 0.2–0.5 sea level.
That verifies spectacle, not play. A player can Bloom vegetation or accelerate technology, but the spec
does not require a deliberate steward to measurably change warming, sea loss or emissions. A connected
simulation needs proof that understanding the loop changes its outcome.

**Spec.** SPEC-SIM 3.3 (uptake, factories, anomaly and sea level), 3.5 (vegetation), 3.7 (Industry/Space
and coastal loss), 3.10 (Bloom and Inspire), section 4 (climate target); SPEC-UI 4 (anomaly and sea
numbers).

**Proposed change.** Add cause and leverage before adding another power:

* `Climate.attribution(world)` returns the top emitting culture, current gross emissions, uptake and
  100-year anomaly/sea trend for the HUD risk item;
* Bloom on degraded, unowned vegetation has double fertility recovery for 30 years, so restoring a
  forest is a credible carbon lever rather than cosmetic painting;
* Space-age factories keep the existing half-emission rate, making Inspire a risky but legible route to
  cleaner technology;
* the climate panel explains the tension: “faster learning means more smoke now, less later.”

**Headless test.** Add `steward` (restores the largest degraded carbon sinks, otherwise answers urgent
pleas) and `accelerator` (Inspires the leading industrial culture). On ≥6/8 seeds, at least one must
reduce peak anomaly by 0.75 °C and sea-loss deaths by 40% relative to passive without delaying the Ark by
more than 200 years; which policy wins may vary by seed. The attribution sum must reconcile with yearly
CO₂ change within the documented uptake term.

### 11. MEDIUM — The score risks declaring one valid story morally correct

**Problem.** The concept presents beloved, feared, forgotten, parting and silence as authored endings,
but `Sim.score` applies an ending multiplier. Unless the UI very carefully hides that hierarchy, players
will read Beloved as the correct victory and use the rich simulation to optimise one moral answer. That
undercuts the Chronicle's value and the game's slightly wry, observational tone.

**Spec.** SPEC-SIM 3.12 (`Sim.score`, ending multiplier); SPEC-UI 5 (ending statistics, score, planet
card); CONCEPT pillars 2–3 and Endings.

**Proposed change.** Remove mood from the scalar multiplier. Present five independent legacy lines:
**Life** (peak/living population and biosphere), **Continuity** (years, cultures and surviving places),
**Wonder** (ages and works), **Mercy/Wrath** (descriptive, not positive/negative), and **Memory** (how
strongly and consistently the god was remembered). Give each ending its own non-ranked Chronicle seal.
If a single share-card number must remain, compute it only from the first three lines.

**Headless test.** Reclassifying an identical final world among beloved/feared/forgotten/parting must not
change the numeric legacy total. Each component is monotonic in its named state and finite on extinction
fixtures. Across standard policies, no mood should be the unique top-scoring policy on more than 6/8
seeds. Human check: ask players whether the ending screen describes their history or grades their
morality.

## Balance targets that should change

| Current target | Why it is insufficient or wrong | Replacement/addition |
|---|---|---|
| Small miracle every 5–15 s early and 3–8 s later | This defines button cadence, not decision cadence, and directly rewards brush spam. | One consequential commitment every 20–45 s; ≤120 weathering episodes and ≤180 effective brush-seconds in a successful kind/responder history. |
| Active policy below 90% Awe in at least 60% of years | A player can oscillate between empty and full while making no timing choice; passive currently remains capped 88–89%. | Passive ≥95% cap ≤20%; responder in the 15–85% reserve band for 50–85%; no active policy starved below 10% for >120 years. |
| Late moments per minute ≤3.5 × early | A ratio can pass at 50–80 raw moments/min, which is still perceptual overload. | 6–18 **attention** events/min in every age; ≤4 in a rolling 10 s; ≤3 simultaneous crises. Keep raw events unconstrained. |
| First war by year 1,300 and war share 8–15% median | Frequency does not prove agency; an unwarned war is still a fait accompli. | Keep frequency bounds, plus ≥90% of wars warned 10–120 years in advance and a diplomat policy halves war-years on ≥6/8 seeds. |
| Passive climate peak +2…+5 °C and sea +0.2…+0.5 | This verifies the disaster happens, not that the player can understand or influence it. | Keep passive envelope; add a stewardship/acceleration policy that changes peak anomaly by ≥0.75 °C and sea deaths by ≥40% without delaying the Ark >200 years. |
| Every age unlocks or upgrades a power | An automatic numeric upgrade does not create a choice. | Every age changes a verb; Atomic and Space each offer two mutually exclusive revelations with measurable, non-dominant outcomes. |
| Each Ark mood is most common for some scripted policy | Good outcome coverage, but the HUD can label an ending before a history exists. | Preserve coverage; no terminal public label before year 250/mature memory, and public label must agree with the Ark verdict once construction begins. |
| Population/settlement reference curve | Useful as a safety envelope, but it can encourage identical worlds. | Retain broad min/max safety bounds; report coefficient of variation across seeds and ensure no single seed curve is treated as the ideal trajectory. |

The Ark timing bands, passive survival floor, four-ending policy coverage, early second-culture target,
determinism, performance and no-NaN requirements should remain.

## Where the player is bored, confused or powerless

* **Bored:** while holding Rain/Bloom for the hundredth time; while an Ark bar advances with no
  constructive lever; when late-age upgrades arrive automatically.
* **Confused:** when “Beloved” appears before a history exists; when Raise Land both saves a city and
  counts as cruelty; when a relation becomes war without a visible cause or forecast; when the Awe bar
  is full but hoarding is supposedly bad.
* **Powerless:** before unforecast crises; against the flood of local prayers; during climate change
  without attribution; during the Ark race except through destruction.

## The five changes I would make first

1. **Bundle Rain/Bloom into durable weathering episodes and turn settlement prayers into cultural
   choruses.** These two rules remove most busywork while preserving the tactile god-power fantasy.
2. **Replace the Now strip with a four-item Risks and pleas list with forecast stages, causes, countdowns
   and at least two valid answers.** Start with hunger, war, plague, sea and fire.
3. **Give Awe a soft holding cost and a recovery band.** Make saving, spending and neglect visibly
   different strategies; replace the current permissive cap target.
4. **Make the Ark race influenceable, but seal moral memory only near the end.** Inspire, research
   setbacks and a departure rite should change who leaves without letting a late player buy redemption.
5. **Delay terminal public verdict words until memory is mature, then enforce an absolute attention
   budget for the late game.** The player should read a developing relationship, not an early score amid
   a firehose.

Those five changes directly address the brief's core question: a 30–40 minute history becomes fun to
influence when the player chooses which matters deserve power, can foresee the cost of inaction, and can
recognise their choices in the world that finally leaves them.
