# Chronicle voice — sample entries, god epithets and endings (proposal, T-005, Grok)

A proposal for the voice of `DM.Chronicle` (SPEC-SIM §3.11): template text the chronicle can fill
from bus events (SPEC-SIM §2), god epithets for `Chronicle.epithet` / `updateEpithets`, and the
ending paragraphs that `Screens.ending` shows (SPEC-UI: ark-beloved, ark-feared, ark-forgotten,
ark-parting, silence-quiet, silence-ash). All prose is original, written for DEMIURGE.

Tone (CONCEPT.md): reads like a translated ancient text; mythic, warm, a little wry.

## 1. Placeholders

Every placeholder used in this file. Names come from the entities in SPEC-SIM §1 and the names
module (§3.1); the example values use the spec's own examples (`'Umar'`, `'Umaran'`,
`'the Umari'`, `'Tel Asha'`, `'Year 312'`, `'the Rain-Giver'`).

"Subject culture" means the culture the entry is about: the founder, the one that rose an age, the
aggressor in `war:declared`, the attacker in `battle`, the culture that owns the settlement, etc.
"Other culture" is the second culture of a two-culture event.

| Placeholder | Meaning | Filled from | Example |
|---|---|---|---|
| `{year}` | Current year as a number | `world.year` (floor) | `312` |
| `{year_ord}` | Year as an ordinal, for "In the {year_ord} year" | `world.year` + ordinal suffix (1st, 2nd, 3rd, 4th, 11th–13th) | `312th` |
| `{year_str}` | The game's year label | `Chronicle.yearStr(world)` | `Year 312` |
| `{culture}` | Subject culture, proper name | `culture.name` | `Umar` |
| `{culture_adj}` | Subject culture, adjective | `culture.adj` | `Umaran` |
| `{culture_plural}` | Subject culture, the people (includes "the") | `culture.plural` | `the Umari` |
| `{other_plural}` | Other culture, the people | other `culture.plural` (`war:*` b, `battle` defender, `settlement:captured` from/to, `nuke` to) | `the Vesh` |
| `{parent_plural}` | Parent culture of a new culture | `culture.plural` of `culture.parent` (`culture:born`) | `the Umari` |
| `{victor_plural}` / `{loser_plural}` | Winner and loser of a battle or war | `battle.won` / `war:ended.victor` | `the Vesh` |
| `{god_name}` | The subject culture's own name for the god | `culture.godName` | `Ashtavai` |
| `{god_epithet}` | The subject culture's current epithet for the god (includes "the" where the epithet has one) | `culture.epithet` (fall back to `god.epithet`) | `the Rain-Giver` |
| `{new_epithet}` / `{old_epithet}` | Epithet after and before a change | `updateEpithets` | `the Hammer` |
| `{settlement}` | The settlement the event is about | `settlement.name` (event `id`, `settlement`, or `Civ.at(cell)`) | `Tel Asha` |
| `{origin}` | Where settlers came from | `unit.from` → `settlement.name` (only for `by: 'settlers'`; use a variant without it if unknown) | `Vornhal` |
| `{other_settlement}` | Second settlement (trade partner) | `trade:route` b | `Qe-Luun` |
| `{tier}` / `{prev_tier}` | Settlement tier words | `Civ.TIERS[tier]` / `[prev]` | `town` / `village` |
| `{building}` | Building, as a common noun | `settlement:building.building` → display word (shrine, temple, granary, walls, port, market, mine, factory, academy, power plant, launch site) | `granary` |
| `{wonder}` | Name of a wonder | `wonder:built.name` | `the Sunwell` |
| `{age}` | Age name | `DM.AGES[age]` (Stone, Bronze, Iron, Sail, Industry, Atomic, Space) | `Iron` |
| `{prophet}` | Prophet's name | `prophet:risen.name` / `prophet:died.name` | `Dovak` |
| `{prayer_kind}` | What was prayed for, as a phrase | `prayer.kind` → `rain` → "rain", `heal` → "healing", `peace` → "peace", `bloom` → "green fields" | `rain` |
| `{war_years}` | Length of a war in years, written as words when below 20 | `year − relation.since` | `eleven` |
| `{planet}` | The world's name | `world.name` | `Ithe` |

Rules for filling:

- Plural culture names already include "the" (`'the Umari'`); never write "the {culture_plural}".
- Epithets include their article (`'the Rain-Giver'`); a few epithets have none ("She Who Burns the
  Sky"). Capitalise the first letter when the placeholder starts a sentence.
- Missing data: choose another variant rather than printing an empty placeholder.

## 2. Sample entries (60, plus variants)

Each entry gives the event, the suggested weight (1 minor, 2 notable, 3 epochal; ChronicleEntry
`weight`) and the template. The most common events have three variants (marked a, b, c) so the
chronicle can rotate them and never repeat itself in a short span. There are 60 numbered
entries; with the variants that makes 76 templates.

### Beginnings

- **1** `settlement:founded` (`by: 'awaken'`) · w3 — In the first year, before there was counting,
   {god_epithet} touched the ground at {settlement}, and {culture_plural} woke. They looked up at
   the sky and did not yet know whom to thank, so they thanked everything.
- **2** `power:used` `comet` (genesis, told in the tale) · w3 — In the beginning the world was fire and
   then stone, and {god_epithet} called ice down out of the dark. Where it fell, the hollows filled,
   and that was the first sea.
- **3** `power:used` `seed` (genesis, told in the tale) · w3 — Then green came out of the wet places,
   slowly at first, as if it were not sure it was allowed.

### Foundings (most common: three variants)

- **4a** `settlement:founded` (`by: 'settlers'`) · w1 — In the {year_ord} year, a band went out from
   {origin} with seed corn and quarrels enough for a new town, and founded {settlement} by the
   water.
- **4b** `settlement:founded` · w1 — Some of the people of {origin} grew tired of their neighbours'
   faces. They walked until the faces around them were their own again, and there they built
   {settlement}.
- **4c** `settlement:founded` · w1 — {settlement} was founded in the {year_ord} year. Its founders
   swore it would one day be greater than {origin}. The chronicle notes that all founders swear
   this.

### Growth (most common: three variants)

- **5a** `settlement:tier` (up) · w1 — In the {year_ord} year {settlement} could no longer call itself
   a {prev_tier}. It was a {tier} now, and its people began to walk a little differently.
- **5b** `settlement:tier` (up) · w1 — The houses of {settlement} reached the old boundary stones and
   did not stop. From this year {culture_plural} call it a {tier}.
- **5c** `settlement:tier` (up) · w1 — So many children were born in {settlement} that the elders
   gave up learning their names. Thus it became a {tier}.
- **6** `settlement:tier` (down) · w1 — {settlement} dwindled to a {tier}. Its streets were too wide
   for the people left in them, and grass came up between the stones.

### Buildings (most common: three generic variants, plus four special ones)

- **7a** `settlement:building` · w1 — In the {year_ord} year {culture_plural} raised a {building} at
   {settlement}, and were proud of it for one generation, which is the usual length of pride.
- **7b** `settlement:building` · w1 — At {settlement} they built a {building}. It took longer than
   promised and cost more than was said, as is the way of all building.
- **7c** `settlement:building` · w1 — A {building} now stands at {settlement}, where before there was
   only mud and opinion.
- **8** `settlement:building` `shrine` · w1 — At {settlement} they set stones in a ring and laid the
   first fruits on them for {god_name}. It was a small shrine. {god_epithet} did not complain.
- **9** `settlement:building` `temple` · w2 — {culture_plural} of {settlement} built a temple to
   {god_name} so tall that the priests must rest twice on the stair, and they said that this was
   the point.
- **10** `settlement:building` `walls` · w1 — {settlement} built walls. No one said against whom.
   Everyone knew.
- **11** `settlement:building` `launch` · w3 — Outside {settlement}, on flat ground swept clean, the
   {culture_adj} builders raised a tower of iron and a tower of fire beside it, and pointed them
   both at the sky.

### Famine and plague (famine: three variants)

- **12a** `settlement:famine` · w2 — In the {year_ord} year the granaries of {settlement} rang hollow
   when struck. The people ate the seed corn, and then the stories of better years.
- **12b** `settlement:famine` · w2 — Hunger came to {settlement} and sat down at every table. Many
   were carried out to the fields that could no longer feed them.
- **12c** `settlement:famine` · w2 — There was famine in {settlement}. The people looked up and asked
   {god_epithet} for rain, and counted the days, and the days were long.
- **13** `settlement:plague` (`start: true`) · w2 — A sickness came to {settlement} by the river road.
   It went from house to house like a guest no one had invited and no one could turn away.
- **14** `settlement:plague` (`start: false`) · w1 — The plague left {settlement} as it had come,
   without asking. The living swept their doorsteps and were careful, for a while, to be kind.

### Ruin

- **15** `settlement:destroyed` `drowned` · w2 — The sea rose at {settlement} by a hand's width a year,
   and then by another. In the {year_ord} year the last of its people rowed away, and the fish
   moved into the houses.
- **16** `settlement:destroyed` `meteor` · w3 — Fire fell on {settlement}. Where it stood there is now a
   round hollow, and {culture_plural} do not go near it, and they do not say its name.
- **17** `settlement:destroyed` `lava` · w3 — The mountain opened above {settlement} and its stone ran
   like honey, though not like honey. The town is beneath it. The new ground is very fertile, the
   chronicle observes, as though that were a comfort.
- **18** `settlement:destroyed` `war` · w2 — {settlement} was taken by {other_plural} and burned, and
   its name was struck from the lists of {culture_plural}. The crows kept no lists.
- **19** `settlement:destroyed` `quake` or `wave` · w2 — The ground shook at {settlement} and the walls
   lay down. What the shaking spared, the sea came in to take.
- **20** `settlement:destroyed` `abandoned` · w1 — No one destroyed {settlement}. One by one its people
   left for somewhere better, until the last of them shut the gate behind out of habit.
- **21** `settlement:captured` (`how: 'war'`) · w2 — In the {year_ord} year {settlement} opened its
   gates to {other_plural}, having run out of other ideas. It is theirs now, and pays its taxes in
   their coin.

### Peoples (born, schism, died)

- **22** `culture:born` (`reason: 'schism'`) + `settlement:captured` (`how: 'schism'`) · w3 — The
   people of the far valleys spoke the old tongue so strangely that the capital laughed at them.
   In the {year_ord} year they stopped coming to be laughed at, and called themselves
   {culture_plural}.
- **23** `culture:born` (`reason: 'distance'`) · w3 — From {parent_plural} came {culture_plural}, as a
   branch comes from a tree: the same wood, pointing elsewhere.
- **24** `culture:died` · w3 — {culture_plural} are no more. Their words live on in the mouths of
   their neighbours, who use them for cursing and for the names of rivers.

### Ages (most common: three variants, plus the world's first)

- **25a** `culture:age` · w2 — In the {year_ord} year {culture_plural} entered the Age of {age}. The
   grandfathers said it was a mistake. The grandchildren did not hear them over the noise.
- **25b** `culture:age` · w2 — A new thing was learned at {settlement}, and then everywhere. So the
   Age of {age} began among {culture_plural}.
- **25c** `culture:age` · w2 — {culture_plural} put down the tools of their mothers and took up the
   tools of the Age of {age}. {god_epithet} watched, and did not say whether this was wise.
- **26** `world:age` · w3 — In the {year_ord} year the world turned a page: {culture_plural}, first of
   all peoples, came into the Age of {age}, and the others began to hurry.

### War (declarations and battles: three variants each)

- **27a** `war:declared` · w2 — In the {year_ord} year {culture_plural} remembered an old insult from
   {other_plural}, and went to war over it.
- **27b** `war:declared` · w2 — {culture_plural} and {other_plural} could no longer share the river.
   They tried to share it with spears instead.
- **27c** `war:declared` · w2 — War between {culture_plural} and {other_plural}. Both sides prayed to
   the same sky for victory; the chronicle does not record which prayer was heard.
- **28a** `battle` · w1 — Before {settlement}, {culture_plural} met {other_plural}. By evening the
   field belonged to {victor_plural}, and to the birds.
- **28b** `battle` · w1 — At {settlement} {victor_plural} won the day. {loser_plural} tell it
   differently, in their own chronicle, which no one has asked to read.
- **28c** `battle` (defender won) · w1 — The host of {culture_plural} came against {settlement} and
   broke on its walls like a wave on rock, and went home fewer.
- **29** `war:ended` (with a victor) · w2 — After {war_years} years {loser_plural} asked for peace, and
   {victor_plural} granted it, and made them pay for the asking.
- **30** `war:ended` (`victor: -1`) · w2 — The war of {culture_plural} and {other_plural} ended because
   both had run out of sons. Each side wrote down that it had won.

### Trade (most common: three variants)

- **31a** `trade:route` · w1 — In the {year_ord} year the first caravan came from {settlement} to
   {other_settlement}, carrying salt, dyed cloth and gossip. Of the three, the gossip sold best.
- **31b** `trade:route` · w1 — The feet of traders wore a road between {settlement} and
   {other_settlement}. Along it went pots and coins and, later, fevers.
- **31c** `trade:route` · w1 — Merchants of {settlement} reached {other_settlement} and found that
   the people there were also people. This was thought remarkable, and then useful.

### Prophets and edicts

- **32** `prophet:risen` · w2 — In the {year_ord} year a prophet named {prophet} stood up in the
   market of {settlement} and said that {god_name} had spoken. Some believed. The rest moved their
   stalls.
- **33** `prophet:died` · w2 — {prophet} the prophet died, and was buried facing the sunrise.
   {culture_plural} argued over every word {prophet} had said, which is how they knew the words
   were holy.
- **34** `edict` `peace` · w2 — {prophet} spoke for {god_epithet}: "Lay down the spear." And
   {culture_plural} laid it down, though somewhere they could reach it.
- **35** `edict` `expand` · w2 — {prophet} spoke for {god_epithet}: "Go out and fill the land." The
   young of {culture_plural} took this as permission, which it was.
- **36** `edict` `build` · w2 — {prophet} spoke for {god_epithet}: "Build a thing that can be seen
   from the sky." And {culture_plural} began to quarry the hills.
- **37** `edict` `crusade` · w2 — {prophet} spoke for {god_epithet}: "{other_plural} pray badly."
   {culture_plural} sharpened their swords to correct them.
- **38** `wonder:built` · w3 — In the {year_ord} year {culture_plural} finished {wonder} at
   {settlement}. It took three lifetimes. The first did not live to see it; the last complained of
   the dust.

### Prayers

- **39** `prayer` (`start: true`) · w1 — The people of {settlement} climbed to the shrine and prayed
   to {god_name} for {prayer_kind}. They prayed loudly, in case {god_epithet} was far away.
- **40** `prayer:answered` · w2 — {settlement} prayed for {prayer_kind}, and it was given. The priests
   were as surprised as anyone, but hid it well.
- **41** `prayer` (`start: false`, unanswered after 15 years) · w1 — For fifteen years {settlement}
   prayed for {prayer_kind}, and the sky said nothing. At last they stopped asking. Some stopped
   believing. The shrine grew moss.

### The god's acts (`power:used` / `miracle:witnessed`)

- **42** `rain` · w1 — {god_epithet} drew a hand across the sky above {settlement}, and the dry fields
   drank.
- **43** `bloom` · w1 — Where {god_epithet} passed, the land went green out of season, and the old
   women said the trees had heard a rumour.
- **44** `raise` / `lower` (in history) · w2 — {god_epithet} took hold of the land near {settlement}
   and moved it, the way a sleeper moves a blanket. The farmers measured their fields again and
   did not like the answer.
- **45** `lightning` (`natural: false`) · w1 — {god_epithet} threw fire from a clear sky at
   {settlement}. No one knew why. {culture_plural} made more offerings, to be safe.
- **46** `meteor:impact` (`kind: 'meteor'`) · w3 — In the {year_ord} year a star came down among
   {culture_plural}, and the sky was dark with dust until the harvest after next. They gave
   {god_name} a new name that year, and it was not a kind one.
- **47** `volcano:start` (`natural: false`) · w3 — {god_epithet} split the earth near {settlement},
   and a mountain was born in one summer, roaring like a calf. Its smoke stood over the land for
   twelve years.
- **48** `quake` (`natural: false`) · w2 — The earth shrugged under {settlement}. Pots fell, then roofs,
   then the walls. {culture_plural} asked what they had done. No answer came, which was itself an
   answer.
- **49** `plague` (the power) · w2 — {god_epithet} breathed on {settlement}, and the breath was sick.
   The priests said it was a punishment. They were not sure for what, so they punished everything.
- **50** `heal` · w2 — {god_epithet} laid a hand on {settlement}, and the sick rose from their beds,
   puzzled, and asked what there was to eat.
- **51** `inspire` · w1 — At {settlement} the craftsmen woke with ideas they could not explain, and
   claimed them as their own.
- **52** `peace` (Omen of Peace) · w2 — A sign appeared over the armies of {culture_plural} and
   {other_plural}, and both sides lowered their spears to look at it. When they looked down again,
   neither remembered why they had come.

### Nature and the world

- **53** `storm` (`start: true`) · w1 — A great storm walked in from the sea and stood over {settlement}
   for three days, and the roofs learned to fly.
- **54** `drought` (`start: true`) · w2 — No rain fell on the lands of {settlement}. The river showed
   its stones, and the people counted them.
- **55** `fire:start` (natural) · w1 — Lightning found the forest near {settlement}, and the forest
   burned for a season. In the spring, from the ash, it began again.
- **56** `tsunami:hit` · w2 — The sea drew back from the shore at {settlement}, as if to look at
   something, and then came running. Those who went down to gather the stranded fish did not come
   back up.
- **57** `climate:milestone` `warming` / `sea-rise` · w3 — In the {year_ord} year the old men said the
   winters had been colder in their youth, as old men always say; but this time the ice agreed with
   them, and the sea came up to listen.
- **58** `climate:milestone` `ice-age` / `cooling` · w3 — The winters grew long and the summers forgot
   to come. Snow lay on the high passes all year, and the trade roads closed one by one.

### The last ages

- **59** `nuke` · w3 — In the {year_ord} year {culture_plural} lit a second sun over {settlement}.
   It rose at noon and did not set gently. Afterwards both peoples were very quiet for a long time.
- **60** `ark:launched` · w3 — In the {year_ord} year, from {settlement}, {culture_plural} went up into
   the sky on a pillar of fire, carrying seed and songs and the name of {god_name}. Those who
   stayed watched until the light was a star among stars.

**Extra (not counted in the 60), for two events the chronicle also needs:**

- `ark:started` · w2 — At {settlement} {culture_plural} began to build a ship that would not
  sail on water. The children were told it was for them. It was.
- Epithet change (from `updateEpithets`) · w2 — In the {year_ord} year {culture_plural} stopped
  calling their god {old_epithet}. From now on, in the market and at the hearth, it was
  {new_epithet}.

### Coverage of SPEC-SIM §2

| Event | Entries |
|---|---|
| `settlement:founded` | 1, 4a–c |
| `settlement:tier` | 5a–c, 6 |
| `settlement:building` | 7a–c, 8–11 |
| `settlement:famine` | 12a–c |
| `settlement:plague` | 13, 14 |
| `settlement:destroyed` (all causes; `famine`/`plague` causes reuse 12/13 wording) | 15–20, 59 |
| `settlement:captured` | 21, 22 |
| `culture:born` / `culture:died` | 22, 23 / 24 |
| `culture:age` / `world:age` | 25a–c / 26 |
| `war:declared` / `war:ended` / `battle` | 27a–c / 29, 30 / 28a–c |
| `trade:route` | 31a–c |
| `prophet:risen` / `prophet:died` / `edict` | 32 / 33 / 34–37 |
| `wonder:built` | 38 |
| `prayer` / `prayer:answered` | 39, 41 / 40 |
| `power:used` and `miracle:witnessed` | 2, 3, 42–52 |
| `lightning`, `meteor:impact`, `volcano:start`, `quake`, `tsunami:hit` | 45, 46, 47, 48, 56 (natural twins can use 53–56 wording with "the sky" instead of {god_epithet}) |
| `storm`, `drought`, `fire:start` | 53, 54, 55 |
| `climate:milestone` | 57, 58 |
| `nuke` | 59 |
| `ark:started` / `ark:launched` | extra / 60 |
| `people:lost` | see ending "silence" style; suggested line: *The last fire of {culture_plural} went out at {settlement}. The land kept their footprints for a season, then the grass forgot them too.* |

Not chronicled (technical or too frequent): `phase`, `meteor:launched`, `tsunami:start`,
`volcano:end`, `unit:*`, `chronicle:entry`, `awe:short`, `ending` (the ending has its own text).

## 3. God epithets (40)

Grouped by the behaviour that earns them. "Earned by" names the inputs the spec already tracks
(`culture.seen[power]`, `god.deeds`, `god.mercy` / `god.wrath`, `Society.regard`). Each epithet
is written with its article, ready to drop into `{god_epithet}`.

### Kind (high `god.mercy`, love ≥ 0.2)
1. the Gentle Hand
2. the Kind Sky
3. Mother of Harvests
4. the Patient One
5. He Who Keeps the Lamp Lit

### Rain-giving (`seen.rain` dominant)
6. the Rain-Giver
7. the Cloud-Shepherd
8. She Who Opens the Sky
9. the Well-Filler

### Green and fruitful (`seen.bloom` dominant)
10. the Green Hand
11. the Sower
12. Lord of the Second Spring

### Healing and grace (`seen.heal`, `seen.inspire`, `seen.prophet`)
13. the Physician of Towns
14. the Breath That Mends
15. the Whisperer of Crafts
16. the Sender of Prophets

### Peace (`seen.peace`)
17. the Breaker of Spears
18. the Quiet Between Armies

### Fire-throwing (`seen.lightning`, `seen.meteor` dominant)
19. She Who Burns the Sky
20. the Hammer
21. the Thrower of Stars
22. the Lightning-Handed

### Earth-shaking (`seen.quake`, `seen.volcano`, sculpting under towns)
23. the Shaker of Floors
24. the Mountain-Maker
25. He Who Moves the Hills

### Drowning (settlements lost to `drowned` / `wave`)
26. the Drowner
27. the Sea's Elbow

### Plague-sending (`seen.plague`)
28. the Sick Wind
29. the Grey Breath

### Cruel (high `god.wrath`, love ≤ −0.2)
30. the Unkind Sky
31. the Hungry One
32. the Tyrant Above
33. the One We Do Not Name

### Chaotic (both mercy and wrath high, mixed acts close together)
34. the Two-Handed
35. the Laughing Storm
36. the Coin-Tosser

### Absent (no `power:used` for centuries, low devotion)
37. the Silent One
38. the Sleeper
39. the Empty Chair
40. the Rumour

Fallbacks: a culture that has seen nothing yet uses its plain `{god_name}` or "the Maker". When two
groups tie, prefer the most recent deeds (people remember the last thing a god did).

## 4. Endings

One short paragraph each, in the chronicle's voice, for the ending keys in SPEC-UI
(`Screens.ending`). Placeholders: `{culture_plural}`, `{god_name}`, `{god_epithet}`,
`{settlement}`, `{planet}`, `{year_ord}`.

**ark-beloved — Beloved.** In the {year_ord} year {culture_plural} rose from {settlement} on a
pillar of light. They carried seed, and songs, and a carved stone bearing the name of {god_name},
whom they called {god_epithet}. Before the clouds closed below them, they looked back at {planet}
and wept, not from grief, but because they had been loved, and knew it, and were taking that with
them too. Wherever they land, there will be a shrine before there is a house.

**ark-feared — Feared.** In the {year_ord} year {culture_plural} fled {planet}. They built the
ship in secret, as far as anything can be secret from a god, and they launched it at night. They
did not pray as they rose. On the new world, the mothers will tell the children of {god_epithet},
who lived in the sky of the old one, and the children will sleep with the lamp lit, and grow up,
and build their houses low.

**ark-forgotten — Forgotten.** In the {year_ord} year {culture_plural} left {planet} for the
stars. They took their machines, their music and their quarrels. They did not take {god_name}; no
one remembered to. In an old temple at {settlement} the wind still turns the pages of a book no
one reads, and in it, in a hand no one living could read, is a name that once meant everything.

**silence-quiet — Silence.** There was no last battle and no last prayer. The towns emptied, one
and then another, and the roads went back to grass, and the grass did not ask where everyone had
gone. {planet} turns in the light as it did before the first fire. The rivers still find the sea.
If {god_epithet} is listening, there is nothing now to hear but the wind.

**ark-parting — The Parting (my design for the spec's fourth Ark mood).** In the {year_ord} year
{culture_plural} went up from {settlement} to the stars. They were not sure, at the end, what
{god_name} had been to them: a parent, a storm, a story. So they did what grown children do: they
said a careful goodbye, and promised to write, and did not say when. Somewhere above the clouds
one of them looked back and raised a hand, just in case.

**Optional sixth, silence-ash — Ash** (the spec has this key for silence after a nuclear war):
They had learned at last to make suns, and they made them. Afterwards the sky over {planet} was
grey for a hundred years, and then blue again, as if nothing had happened. Nothing had: not any
more. In the ruins of {settlement} a green shoot came up through the floor of the great hall, and
there was no one to call it a sign.

## 5. Style guide (11 rules)

1. **Translated, not archaic.** Write plain modern English with an old rhythm. No "thee", "thou",
   "hath" or "-eth". The feel comes from structure ("and … and …", "In the {year_ord} year …"),
   not from fake old words.
2. **One or two sentences, under about 45 words** for minor and notable entries. Weight-3 entries
   may run to three sentences. Endings are one paragraph.
3. **Open with time or place, close with an image.** Most entries start "In the {year_ord} year"
   or with the settlement, and end on something seen: fish in the houses, moss on the shrine.
   Vary the opening on at least a third of entries so the feed does not drum.
4. **Vary the rhythm.** Mix one long sentence of "and … and …" with a short, flat one after it.
   The short sentence is usually where the wit lands ("Everyone knew.").
5. **Wry, never mocking.** The humour is the chronicler's dry eye on human nature (pride, gossip,
   bargaining), never a joke at the players' expense, never sarcasm about grief. Disasters that
   kill are told straight, with at most one dry line.
6. **The chronicler does not know the rules.** No numbers except years and short counts, no game
   terms ("Awe", "tier", "devotion", "brush", "edict"), no explaining mechanics. People see signs,
   not systems. (`{tier}` words like "town" and "city" are fine; they are ordinary words.)
7. **The god is seen from below.** Never say what the god intended. Report what happened and what
   the people believed. The god is "{god_epithet}" or "{god_name}", and is never addressed as "you".
8. **The people speak through their own names.** Use the culture's plural, its own name for the
   god and its epithet. Never use real-world peoples, places, religions or holidays.
9. **No modern slang or modern idiom.** No "okay", "guys", "deal with it", no technology metaphors
   before the age that invents them. In the Atomic and Space ages the vocabulary may modernise
   slightly (machines, ships of the sky), but the voice stays the same old chronicler.
10. **No gendered pronouns for placeholders.** Prophets, gods and leaders come from generated
    names without a known gender; repeat the name or rephrase instead of "he" or "she". (Fixed
    epithets such as "She Who Burns the Sky" are exceptions because the gender is part of the
    title.)
11. **Repeat nothing within a lifetime.** Rotate variants and never show the same template for the
    same culture twice within about 60 game years; rate-limit weight-1 entries as SPEC-SIM §3.11
    already requires.

## Open questions for the lead

- `{origin}` for settler foundings is not in the `settlement:founded` payload; the chronicle would
  need to read it from the arriving settler unit, or drop variants 4a–4c's origin clause.
- `{year_ord}` (ordinals) is proposed as a new helper beside `Chronicle.yearStr`; English ordinals
  need the 11th–13th exception.
- Natural events (`natural: true`) could reuse the god's-act wording with "the sky" or "the earth"
  as the subject; or the chronicle could deliberately credit the god anyway, since the people
  cannot tell the difference. A deliberate choice here would add a lot of flavour.
