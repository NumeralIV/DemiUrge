# DEMIURGE: story and plot design, v4 ("story-v4")

*Author: Claude (one of the lead's build agents, night track "story-v4"), 2026-11-20. Design only: no code under
`Unreal/Source` or `Unreal/Config` was touched. The wave-3 workflow owns the client and the simulation track owns
`Sim/`; every change below is a proposal for those owners, sequenced in section 10.*

**Read first:** `docs/content/chronicle-voice.md` (the approved voice: its 11 style rules bind every line in this
document), `Sim/Chronicle.h` (the budget, the epithets), `Sim/Society.h` (memory, verdict, moods, prophets, edicts,
alliances), `Sim/Names.h` and `docs/content/cultures.md` (eight culture families, `names::ruler`, `names::epithet`,
`names::ship`), `docs/reviews/claude-design-review.md` (findings 1, 2, 4 and 8 shaped this design).

---

## 0. Evidence: what the story is today

I ran the current headless build (`Unreal/Tools/simrun/build/simrun.exe`, built 2026-11-20 03:04) on one full
history: `--seed 2 --years 4500 --policy kind --chronicle 5000 --tale` (world "Mereshore", Ark in year 3,330,
ending *beloved*, 1,244 entries: 1,083 minor, 136 notable, 25 epochal). I read all of it. Numbers in this section
come from that run.

**What works.** The voice is excellent and consistent; the peoples have their own names, tongues and names for the
god; epithets track the god's deeds; the budget keeps the late game readable; the ending paragraph lands.

**What is missing is continuity.** Every entry is a separate anecdote. Nothing that happens in one century is
remembered in the next, so the history reads like weather, not like a story.

| Observation (seed 2, kind) | Why it hurts the story |
|---|---|
| 18 prophets rose; every one in the capital Beiluskar; 15 of 18 are seen once at rising and once at death | Prophets are the only named people, and they are interchangeable. No one is remembered. |
| 7 "build" edicts ("began to quarry the hills") and **1 wonder** in 3,330 years | A promise made 7 times and kept once is told as if it were kept. Cause: all prophets live in Beiluskar, which already has its wonder, and `society::raiseWonder` refuses a second (finding B4). |
| 16 wars, 64 battles; the tale says "They fought 16 wars"; no war has a name, no war remembers the last | The strongest dramatic material in the sim is reduced to a count. |
| The Vakkadnir and the Boslirinir go to war in 1871; the Boslirinir fight the Fubuggir in 2163; the Vakkadnir fight the Orbelurgir in 2213 | Rivalries exist in the sim (`Relation.v`, `aggressor`, captures) but no entry ever says "again" or "for the third time". |
| 0 alliances | `DiplomacyTune::allyForm = 2` (relations never exceed 1), so the alliance system is off by default. |
| 8 epithet changes; the god's name for a people is its only theology | The people never say what they *believe*, only what they call the god this decade. |
| The Ark first appears in year 3,131 ("began to build a ship that would not sail on water") | Nothing earlier points at it. The finale has no set-up. |
| Of the four "remembered above all" moments in the tale, two are "the last families left Fankilosk / Iridsund" | The tale picks weight-3 entries evenly in time, not by significance. |
| `names::ruler`, `names::epithet`, `names::ship` exist and are never called (cultures.md section 8) | The data for rulers, bynames and fleets is ready and unused. |

**Bugs found on the way** (for the chronicle and society owners; each is quoted from the run):

- **B1** The Ark launch and the ending are two weight-3 entries in the same year with overlapping text
  ("From Beiluskar, the Sveisilkir went up into the sky on a pillar of fire ..." then "In the 3330th year the
  Sveisilkir rose from Beiluskar on a pillar of light ..."). The ending should replace the launch line, or the
  launch line should be held back when the ending fires on the same tick.
- **B2** One Omen of Peace wrote **9 entries in year 2238** ("An omen of peace was seen over Stugaleid ...",
  "Skeldund spoke peace over Brokthurn ...", one per witnessing town).
- **B3** "The prophet Homadis was gathered to Skeldund." is written **twice** in year 3036.
- **B4** (Society) the build edict silently fails when the prophet's town already has a wonder; it should choose the
  culture's greatest town without one, and the chronicle should tell an edict that came to nothing.
- **B5** `tale()` section 7 picks the first, the last and two evenly spaced weight-3 entries, so abandonments
  outrank wonders and wars. It should rank by story significance (section 9.4).
- **B6** The god's own small acts are outside `templateCap`: "A warm rain fell on X ... the roofs leaked, and
  nobody minded" ×10, "Rain answered the prayers of X, warm and heavy and long" ×6.
- **B7** Year 0 has "Again the god woke a people: the Sveisilkir, at Hirufong." directly after the first
  awakening: the genesis skip path wakes twice or tells a second camp as a re-awakening.
- **B8** 6 of the 25 epochal entries are abandoned cities ("Figbrekk was abandoned."): the run's 43 lost towns are
  all cause *sea*, but several are told as a quiet abandonment with no link to the rising sea or its engines.

---

## 1. What the story must do (target qualities)

In my own words, for this game:

1. **A history you can follow.** At any moment the player can say who the main peoples are, who rules them, who
   hates whom and why, and what the people believe about their god. The chronicle answers "what happened"; the
   story layer answers "and what did it lead to".
2. **People who outlive their entries.** A handful of rulers, prophets, heroes and thinkers per history become
   famous. Their names come back centuries later, in prayers, in war names, in proverbs, on the Ark.
3. **Memory in every relationship.** Two peoples who fought once remember it; a town taken is a town owed; a peace
   has a name and can be broken; old friends are betrayed or kept.
4. **Belief that changes and can be wrong.** The people build myths from what they saw, credit the god for the
   mountain's own fire, doubt when the sky is silent, and argue with their daughters about what the god wants.
5. **Set-ups that pay off.** Prophecies, a star-reader's dream of the sky, the first wonder, the founder's name and
   the oldest feud are planted early and resolved at the Ark, so the ending feels earned, not announced.
6. **The player's hand inside the plot.** Every story thread has a point where a power changes its course: answer
   the prophet, prove the doubter wrong, break the feud with an omen, let the prophecy of the sea come true. The
   player can read the ending coming and still change it.
7. **Different every time.** Culture families, the god's style and the sim's own chaos give each world its own cast,
   its own feud, its own creed and its own last words. Two histories should never tell the same plot.
8. **Quiet enough to read.** The story adds about one notable beat per real minute and never more than one story
   toast per 20 seconds; most of its value is in better words for entries that already exist.

---

## 2. Architecture: a story memory beside the chronicle

### 2.1 Split of responsibilities

| Part | Owns | Never does |
|---|---|---|
| **`Sim/Story.h/.cpp` (new, story track)** | The narrative state: figures, houses, threads between peoples, named wars and treaties, prophecies, myths (tenets), acts. Decides *what* happens to the story. | Writes prose; draws from `w.rng`; changes anything another module reads (phase 1). |
| **`Sim/Chronicle.cpp` (chronicle track)** | All words: the new entry types and placeholders, the sayings, the act epigraphs, the epilogue, the tale v4. Reads Story through read-only accessors. | Keeps narrative state of its own beyond its scratch. |
| **Client (C7 HUD, C8 screens)** | Story toasts, the act title card, the culture panel's people, feuds and beliefs, the book's chapters, the ending epilogue, the planet card's lines, the timelapse captions. | Computes story; it reads `story::` accessors and chronicle text. |

Rationale: the chronicle already owns the voice, the template machinery (`pickTemplate`, shuffle bags, the budget)
and the epithets. Putting the memory in its own module keeps `Chronicle.cpp` (4,046 lines) from growing a second
brain, lets the HUD read the story without parsing prose, and lets the story be tested without a single template.

### 2.2 Determinism and isolation (binding)

- Story draws only from forks: `w.rng.fork("story:<kind>:<id>")` (names, reign lengths, prophecy choice). It never
  advances `w.rng`.
- **Phase 1 is pure narration.** With Story on or off, the *sim fingerprint* (year, tick, rng state, live towns,
  pop, sea level, co2, cultures, units: the one the chronicle-budget verifier used) is identical. Section 10, S10,
  lists the optional "teeth" that let figures affect the sim, each behind a tune flag that is off by default.
- Story's own state is persisted and goes through save/load exactly (a save made mid-reign continues the same reign,
  prophecy and feud).
- Cost: every handler O(1) or O(cultures); yearly work O(cultures + threads); a 4,500-year history must run within
  3 % of its time without Story.

### 2.3 Wiring (one line each for other owners)

- `sim::use` calls `story::attach(w)` **before** `chronicle::attach(w)` (Sim owner), so for any event Story
  updates first (the war gets its name before the chronicle writes the declaration). A test pins the order.
- The yearly group calls `story::yearly(w)` after `society::yearly` (Sim owner). Story's own events are emitted
  only from `story::yearly` (a queue filled by its handlers), never from inside another module's emit, so a story
  entry never appears *before* the entry that caused it.
- New bus events (add-only rows in `Core/Events.h`, foundation owner; `test_core_events` checks them):

| Event | Payload | Meaning |
|---|---|---|
| `story:figure` | `id, kind, culture, settlement, what` (`rise`, `deed`, `fame`, `death`, `legend`) | A figure appeared, did something famous, crossed the fame bar, died, or became legend (a saint, a hero of song). |
| `story:reign` | `culture, ruler, prev, house, newHouse, why` | A succession (`heir`, `usurper`, `conqueror`, `schism`, `ruin`). |
| `story:thread` | `a, b, what, war` (`contact`, `war-named`, `feud`, `treaty`, `compact`, `betrayal`, `reconquest`, `reconciled`) | A change in a relationship between two peoples. |
| `story:prophecy` | `id, culture, prophet, kind, what` (`uttered`, `fulfilled`, `failed`) | A prophecy was made or resolved. |
| `story:myth` | `culture, tenet, variant, what` (`formed`, `overturned`, `misread`) | A belief formed, changed, or misattributed an event to the god. |
| `story:act` | `age, culture, what` (`open`, `close`) | The world entered a new age (an act of the story opens; the last one closes). |
| `story:fate` | `culture, mood, prev` | The people's foreseen parting from the god changed (section 6). |

### 2.4 Storage

**Recommended:** a typed `Saga` struct in `World` (`world.saga`), saved by `state::serialize` with
`kVersion = 2`; a version-1 save loads with an empty saga (Story rebuilds what it can: a ruler for every capital, an
origin tenet for every people, no history). This is a foundation change (State/Schema/Save owners).

**Fallback** if the foundation owner declines a version bump in this release: Story keeps the same structs in
`w.scratch<StoryScratch>()` and persists them as compact records in `w.vars.str["story_*"]` (already saved,
currently unused). The `story::` API hides which one is used; switching later is one file.

```
enum class FigureKind : uint8_t { Founder, Ruler, Prophet, Hero, Voyager, Sage, Doubter, Captain };
enum class Fate : uint8_t { Living, Died, FellInBattle, LostAtSea, TakenByTheGod, Vanished, Deposed, WentWithTheArk };
struct Figure {
	int id; FigureKind kind; int culture, home;          // home = settlement id
	std::string name, style, byname;                     // 'Teodo', 'Ossaro Teodo' (names::ruler), 'the Tide-Born'
	double born, died = -1, fame = 0;                    // fame 0..1 (section 3.3)
	int house = -1, ordinal = 1;                         // rulers: house id, regnal number ('the Second')
	std::array<int, 6> deeds{-1,...};                    // chronicle entry ids of its famous moments
	Fate fate = Fate::Living; uint16_t flags = 0;        // Saint, Relic (a tomb in `home`), Song, Statue, Carried
};
struct House { int id, culture, founder; std::string name; double founded, ended = -1; int rulers = 0; };
struct Grievance { uint8_t kind; double year; int settlement, by; double weight; };   // section 4.2
struct Thread {                                         // one per pair of peoples that ever met (lo id first)
	int a, b; uint8_t origin;                            // Kin (parent/daughter), Trade, Border, Sea, Faith
	double met; int wars = 0, warsWonA = 0, warsWonB = 0;
	std::string feud, compact;                           // names once earned ('the Salt Feud', 'the Compact of Qe-Luun')
	std::vector<Grievance> grudges;                      // at most 8, the lightest dropped first
	double warmth = 0;                                   // remembered good (trade years, help, shared wrath), fades
	int lastWar = -1;                                    // NamedWar id
};
struct NamedWar { int id, a, b, ordinal; std::string name; double start, end = -1; int victor = -2, hero = -1, decisive = -1; std::string treaty; };
struct Prophecy { int id, culture, prophet; uint8_t kind; int subject; double uttered, deadline; uint8_t state; int payoffEntry = -1; };
struct Tenet { int culture; uint8_t kind, variant; double year; int source; bool overturned = false; bool misread = false; };
struct Act { int age; double opened; int leader; int closeEntry = -1; };
struct Saga { std::vector<Figure> figures; std::vector<House> houses; std::vector<Thread> threads;
	std::vector<NamedWar> wars; std::vector<Prophecy> prophecies; std::vector<Tenet> tenets; std::vector<Act> acts; int nextId = 1; };
```

Caps (also the save-size bound, < 40 KB): 160 figures (the least famous dead go first; saints, the founder, every
house founder and anyone named on the Ark are never pruned), 8 grudges per thread, 40 prophecies, 12 tenets per
people, 6 deeds per figure.

---

## 3. Recurring characters

### 3.1 The cast

| Kind | When one appears | Name | What makes them famous | How they end |
|---|---|---|---|---|
| **Founder** | Every people at its birth: the awakened first people (the first to speak at the waking) and every daughter people (the one who led the schism or the far valleys) | `names::person` in the people's own tongue | Leading the people out; the origin myth names them | Dies within ~60 years; never pruned; the Ark carries the name |
| **Ruler** | Every people with a capital from the Bronze age (Stone-age peoples have elders, not rulers) | `names::ruler` (title + name) and, at death, a byname | A wonder in the reign, a war won, towns taken, a long reign, a miracle in the capital during the reign | Reign of 15-45 years (fork); succession (3.2) |
| **Prophet** | As today (`prophet:risen`) | As today | Each prophet now utters one **prophecy** (section 5) and teaches one doctrine; an edict carried out, a miracle by the god in the prophet's town while the prophet lives ("the god answered the prophet"), a fulfilled prophecy | As today; famous prophets become **saints** (Relic: a tomb in their town) |
| **Hero** | A battle that turned a war: a defender winning against the odds (`battle.odds`, data need D3), the taking of a capital, the deciding battle of a named war; or a town holding through plague, famine and siege in one decade | `names::person` + `names::epithet` ("Dovak the Star-Reader") | The battle itself; a hero who wins twice becomes legend | Falls in a later battle (`FellInBattle`) or dies old; heroes of the losing side are remembered too, by their own people |
| **Voyager** | Sail age: a people's first town across open sea, its first sea trade route, first contact by sea | captain `names::person`, ship `names::ship` | The crossing | `LostAtSea` (a settler ship that never arrived, `unit:died` cause `path`) or returns |
| **Sage** | Every `culture:age` of a people: the one who first worked bronze, iron, sails, engines, the atom, the rockets, in the town where it happened (data need D2) | `names::person` | The discovery; inspired (the god's Inspire on that town within 20 years) sages say the idea came in a dream | Dies; a sage of an inspired discovery feeds the "god-given craft" tenet |
| **Doubter** | Industry age onward: a people whose devotion falls below 0.25, or after a failed prophecy | `names::person` | A book that says the god is a story | The god acts visibly in that people within 30 years (the doubter is answered, "burned the book" or "said it was weather"), or the doubt spreads into a tenet |
| **Captain** | `ark:started` | `names::person`; the Ark's name from the saga (section 6.3) | Building and leading the Ark | `WentWithTheArk` |

Typical cast of one history (estimate to verify with S4's counters): 6 founders, 40-70 rulers in 8-14 houses,
12-20 prophets, 6-15 heroes, 2-6 voyagers, 20-35 sages, 0-4 doubters, 1 captain; of them **8-15 famous**
(fame >= 0.5) and **3-6 legends** (fame >= 0.75).

### 3.2 Rulers and houses (dynasties)

- A new house begins with its founder ruler; its name is "the House of {founder name}".
- **Succession** when the reign ends: an heir of the same house (default about 75 %), or a new house when one of
  these happened during the reign, in this order: the capital was taken (`conqueror`: the victor's house rules it,
  or a new local one), the people was born by schism (`schism`), the capital was destroyed (`ruin`: "the house fell
  with the city"; any cause, including the god's), a lost named war (40 %), famine in the capital for 3+ years
  (25 %).
- **Regnal names.** An heir takes an ancestor's name with 30 % (fork), which gives "Teodo the Second". Ordinals
  are written as words up to the tenth ("the Third"), then "the 11th".
- **Bynames** at death, from the reign's record (first match wins): a wonder → "the Builder"; captured a capital →
  "the Conqueror"; won a named war → "the Victorious"; a miracle of the god in the capital → "the Blessed"; an
  Omen of Peace ended the reign's war → "the Peacemaker"; towns lost → "the Unlucky"; plague in the capital → "the
  Pale"; famine → "the Hungry"; reign >= 40 years → "the Old"; otherwise `names::epithet` (the family's flavour).
- **Reign openers.** While a ruler reigns, about one in five of the people's notable entries may open with the reign
  instead of the year: "In the ninth year of Teodo the Second, ..." (voice rule 3 asks to vary openings).

### 3.3 Fame

Fame is 0..1 and decides who the chronicle mentions again.

| Gain | Amount |
|---|---|
| A deed entry of weight 2 (weight 3) | +0.10 (+0.25) |
| A wonder raised in the figure's town in their lifetime (rulers and prophets) | +0.30 |
| A named war won (ruler of the victor, the war's hero) | +0.25 |
| The god's grace in their town while they live (prophets, rulers): heal, rain, bloom, inspire, consecrate | +0.10 per decade with one, at most +0.30 |
| A prophecy fulfilled (prophet) | +0.40 |
| Remembered in a later named thing (a war, a treaty, the Ark) | +0.15 |

After death fame halves every 600 years; a saint or legend never falls below 0.4. **Famous** (>= 0.5): may be named
again in later entries, prayers and sayings, appears in the tale. **Legend** (>= 0.75): may give its name to a war,
a treaty or the Ark, and is carried in the epilogue.

### 3.4 How characters come back across centuries

1. **By the line:** a house's rulers recall the founder ("of the line of Teodo"), regnal ordinals.
2. **By the place:** a saint's tomb or a hero's battlefield is a Relic flag on the settlement; when the town is
   struck or taken, the entry says so ("where the bones of Bralrosid lay"); the inspector shows it.
3. **By the word:** a saint's doctrine becomes a tenet and a saying ("As Bralrosid taught: ask once, and wait");
   later prayers in that people may be "in the words of Bralrosid".
4. **By the name:** a legend's name is given to a war, a treaty, a ship or the Ark.
5. **By the grudge:** a grievance remembers who did it ("since the days of Ossaro Teodo, who burned Qe-Luun").

---

## 4. Rivalries and alliances with memory

### 4.1 Threads

A thread opens for a pair of peoples the first time they meet: a daughter is born (`Kin`), the first caravan
(`Trade`), territories within the contact range (`Border`), the first ship (`Sea`). Its origin colours every later
line ("the Vai and the Thrask, who had once traded salt and songs").

### 4.2 Grievances and warmth

| Grievance (by = the culture that did it) | weight | fades |
|---|---|---|
| A town taken (`settlement:captured`, war) | 1.0, the capital 2.0 | half-life 400 years, **but a town owed is never forgotten while it stands and is held by `by`** |
| A town razed (`settlement:destroyed`, war) | 1.5 | 400 years |
| A war begun by `by` | 0.5 | 300 years |
| A crusade edict against the victim | 1.0 | 300 years |
| A nuclear strike | 3.0 | 1,000 years |
| An alliance broken by war | 1.5 | 400 years |
| The god struck one and spared the other (`war:declared.blamed`) | 0.5 | 150 years |

**Warmth** rises with trade years, an ally's help, shared wrath from the god, a sworn peace kept for its term, and
fades with a half-life of 200 years. Story never changes `Relation.v` in phase 1: grievances and warmth decide words,
war names and reconciliations, not whether war happens.

### 4.3 Naming wars, feuds and peaces

**A war is named at its declaration** (first match), may be renamed by its decisive battle, and is numbered per pair:

| Rule | Name |
|---|---|
| Parent and daughter people | "the Kin-War" (then "the Second Kin-War") |
| The aggressor's prophet has the crusade edict | "the Holy War of {prophet}" |
| The aggressor holds a grievance town taken by the defender | "the War for {town}" |
| `war:declared.blamed` (the god moved it) | "the War of the Angry Sky" |
| `war:declared.ally` (a call to arms) | "the War of the {compact}" |
| A nuclear strike during the war | renamed "the War of Fire" |
| Otherwise | "the {ordinal} {defender adjective} War" ("the Second Vaic War") |

At peace (`war:ended`): the **decisive battle's town** (most battles, else the last) names the treaty: "the Peace of
{town}". An Omen of Peace that ended it makes it "the God's Peace of {town}". A draw is "the Peace of Empty Hands".

**A feud is named** when a pair has fought two named wars, or one war took a capital, or a nuclear strike fell: "the
{word} Feud", where the word is the first disputed town ("the Tel Asha Feud"), else the land between the capitals
(river, salt (desert), pine, ice, reef, steppe), else "the Long Feud" once it is 500 years old.

**A compact is named** when two peoples become allies (`alliance`, formed): "the Compact of {town}" (the larger
capital of the two). Breaking it by war is a **betrayal** grievance on both sides' memory, told once.

**Payoffs the chronicle must tell** (each at most once per pair per 100 years, story bucket):
- *Reconquest:* a grievance town is taken back: "After {years} years Tel Asha was the Vai's again."
- *Third time:* the third named war between a pair, told as such.
- *Reconciliation:* a feud ends when its grudges have faded below 0.5 total and warmth is above 0.5, or by an
  Omen of Peace sworn for its full term: the feud's name is retired in a line.
- *Old friends:* a war between a pair whose thread warmth was above 0.5 ("who had once traded salt and songs").

**Data dependency:** alliances need `DiplomacyTune::allyForm <= 1` (D5). Until the sim track turns them on, Story
narrates *friendships* (pairs with warmth >= 0.6 for 100 years) instead of compacts, with no sim effect.

---

## 5. Prophecy: the foreshadowing engine

Every prophet utters **one** prophecy at rising (fork `story:prophecy:<figure id>`), chosen among the kinds whose
condition holds, weighted by the culture family (section 7.2). It is a real, tracked promise with a deadline. It is
told when uttered (notable), shown in the culture panel while open, and told again when it is fulfilled or fails.

| Kind | Condition to utter | Fulfilled by | Deadline | The player's lever |
|---|---|---|---|---|
| **Rain** ("the god will end a thirst") | the people had a famine or drought in the last 50 years | `prayer:answered` (rain or bloom) for that people | 80 y | Answer it: an easy, early promise kept |
| **Healing** | a plague in the people in the last 50 years | `prayer:answered` heal, or Heal on a sick town of theirs | 80 y | Heal |
| **Great work** ("a thing seen from the sky") | no wonder in the people | `wonder:built` for the people (edict, Consecrate) | 150 y | Consecrate a capital |
| **Peace** ("the spears will be laid down") | the people is at war | `war:ended` for the people | 60 y | Omen of Peace |
| **Fall** ("{rival capital} will be humbled") | a feud or a war with a rival | the rival's capital captured or destroyed | 300 y | Wrath, or let the war run |
| **Sea** ("the sea will walk into {town}") | a coastal town of the people; Sail age or later, or co2 rising | that town destroyed by `drowned`, `wave` or `sea` | 400 y | Lower Land fulfils it cruelly; a warm world fulfils it slowly; Raise Land may save it |
| **Star** ("a star will fall and the sky go dark") | any people, Iron age or later | a meteor impact anywhere | 300 y | Meteor (or Star-fall) |
| **Kin** ("brothers will stand against brothers") | the people has a daughter people | a Kin-War | 400 y | None (the sim's own drift): foreshadows a schism war |
| **Return** ("the god will speak again") | the people has not witnessed a deed for 200+ years | any deed of the god witnessed by the people | 100 y | Act, anywhere they can see |
| **Ladder** ("our children will walk among the lights") | Sail age or later and curiosity >= 0.5, or a Qa'tiri-family people | `ark:launched` by this people or its daughters | none | Keep them alive and believing: the Ark |

- **Fulfilled:** a payoff entry quoting the prophecy and its age ("As Fallolfe had foretold six hundred years
  before, ..."); the prophet's fame +0.4; the people forms or strengthens a tenet (section 7). A fulfilled Ladder
  prophecy is quoted on the Ark (section 6).
- **Failed:** an entry that the words were not kept; a "false prophet" mark; with a Doubter in the people, it is
  the doubter's proof. Three failed prophecies in one people form the Doubt tenet early.
- At most **one open prophecy per people** of each kind; at most 40 kept; a prophecy whose subject town is gone is
  failed at once with a different line ("Tel Asha was gone before the sea could come for it").

This gives the player readable, optional goals that are in-world (no quest log, no game terms): the people say what
they expect of the god, and the player chooses to fulfil, thwart or ignore it.

---

## 6. Acts, the ending set-up and the Ark

### 6.1 The arc of a history: seven acts and an epilogue

The world age (`world:age`) opens an **act**. Each act has a title (the chapter header in the book), a dramatic
question, the beats the story layer looks for, the myth stage it tends toward, and the threads it plants or pays.
At 1x one act lasts about 2-6 real minutes (seed 2: Stone 292 years, about 2.4 min; Iron 630 years, 5.2 min;
Space 219 years, 1.8 min; 120 years a minute at 1x).

| Act (age) | Title | The question | Beats the story layer creates or highlights | Myth stage | Planted (P) / paid (★) |
|---|---|---|---|---|---|
| **Prologue** (genesis) | *The Making* | What kind of world? | first sea, first green, the waking; the founder speaks | — | P: the first comet ("the star that brought the sea"), the waking place |
| **I** Stone | *The Waking* | Who woke us, and what does it want? | the founder; the first shrine; the first prayer and whether it was heard; the first miracle seen; the origin tenet | Origin | P: the founder's name; the first answered or unanswered prayer |
| **II** Bronze | *The Sundering* | Kin or strangers? | the first rulers and houses; the first daughter people and its founder (design review finding 4 brings it into Bronze); first contact and the thread's origin; the first prophet and prophecy; the first war, named | Covenant | P: the Kin thread; the first prophecy; ★ the first prayer remembered in the covenant |
| **III** Iron | *The Kingdoms* | Who will rule the land? | dynasties and bynames; heroes of the first great wars; the first feud named; the first wonder (or the edict that failed); saints | Doctrine, Heresy | P: the first wonder; the feud; ★ Kin and Fall prophecies |
| **IV** Sail | *The Wide Sea* | What lies beyond? | voyagers and named ships; contact by sea; compacts; the star-readers (Ladder prophecy) | Wider Heaven | P: the Ladder; ★ Great work prophecies |
| **V** Industry | *The Engines* | Do we still need the god? | sages of engines; smoke and the rising sea, blamed on the engines (finding 7); doubters; reconquests in steel | Doubt or Revival | P: the Sea prophecies come due; ★ feuds reconciled or hardened |
| **VI** Atomic | *The Second Sun* | What will we do with the god's fire? | the War of Fire or the long standoff; a peacemaker hero; the fate omen first heard | Reckoning | ★ Star and Sea prophecies; the oldest feud's climax |
| **VII** Space | *The Leaving* | Will they take the god with them? | the captain; the Ark named; who goes, who stays; what is carried | Ark faith | ★ Ladder, the founder's name, the first wonder, the feud, the saints |
| **Epilogue** (ending) | *The Last Page* | What did it all mean? | the ending paragraph, then what was carried, who stayed, what the god was | — | ★ everything still open (section 6.4) |

**Act open** (weight 2, never dropped, once per world age): an epigraph built from the world's own state, e.g. "Here
begins the Age of Iron. The Vai were two peoples now, and the House of Teodo ruled at Tel Asha." **Act close**
(weight 2, written just before the next act opens): a summary of the act from the saga, at most three clauses (the
act's famous figure, its main thread change, the people's tenet), e.g. "So ended the Age of Bronze. In it Teodo
the Builder raised the Sunwell, the Vai and the Thrask learned to hate each other over Qe-Luun, and the Vai came
to believe that the god answers those who ask once and wait."

### 6.2 The fate omen: the ending you can see coming

From the Industry age, the story reads `society::memory(w, c)` for the leading people (and the Ark people once
there is one) every year, and when the mood it would produce changes and stays changed for 30 years, it tells a
**fate omen** (weight 2, at most once per people per 80 years, `story:fate`). This turns the verdict of design
review finding 1 into something the player can read in the world and still change:

| Mood | Fate omen (chronicle voice; one of these is drawn) |
|---|---|
| Beloved | "The children of {culture_plural} asked whether {god_name} would come with them when they went up to the stars. The priests said yes, and believed it." |
| Feared | "Among {culture_plural} the old women said that when the people went up to the stars, they would at last be beyond the reach of {god_epithet}. No one laughed." |
| Forgotten | "A child of {culture_plural} asked what {god_name} was. No one in the house could say, and after a while no one tried." |
| Parting | "{culture_plural} could not agree whether {god_epithet} had been a parent or a storm, and began to say both, in different rooms." |

The HUD shows the current fate as one quiet line (section 8.3).

### 6.3 The Ark as the payoff

At `ark:started` the story names the captain (a Captain figure) and **the Ark**, first match: a legend's name
("the Bralrosid"), the founder's name, the first wonder's name, else `names::ship`. While it is built (`s.ark`
0 → 1), it tells at most three beats: half built; what the people decide to carry (from the saga: relics of saints,
a stone of the first wonder, the founder's name, the book); and who stays (the rival of the oldest feud, unless
reconciled; peoples in older ages).

### 6.4 The epilogue

The ending passage stays the voice's paragraph (B1 fixed: one entry). The **ending screen** and the tale add up to
three short epilogue paragraphs built from the saga:

1. **What they carried** (Ark endings): two to four of: the Ladder prophecy if fulfilled (quoted with its year), the
   founder's name, a saint's words, a stone of the first wonder, the oldest house's name.
2. **Who stayed** (Ark endings): the oldest feud's rival, the peoples left behind, the doubter's followers; or, for a
   reconciled feud, the rival that went with them.
3. **Of the god:** the arc of the people's names and tenets for the god, oldest to last ("First the Vai called the
   god the Waker; in the Age of Iron, the Rain-Giver; at the end, the Gentle Hand.") and the people's last saying.

Silence endings use the same parts as *what was lost*, *who was last*, *what the wind remembers*.

---

## 7. Myths: what the people believe about the god

### 7.1 Tenets

Each people holds an ordered list of tenets; the newest of each kind is "current". A tenet has a kind, a variant
(chosen from the people's memory at that moment), a year, a source (an entry or a figure) and a **saying**: a
one-line proverb in the people's voice, rendered by the chronicle (`chronicle::saying`), quoted in the culture
panel, in some entries, on the ending screen and on the planet card.

| Tenet | Forms when | Variants (from memory and history) | Example saying |
|---|---|---|---|
| **Origin** | within 50 years of the people's birth | woken by the sea / by the first rain / in the forest / from fire (a volcano or meteor seen first) / a daughter: "we left" | "We were woken by water, and to water we go home." |
| **Covenant** | year 300 of the people, or its first prophet | Giver (answered prayers), Judge (wrath), Sleeper (absence), Shaper (raise / lower), Two-Handed (mixed) | Giver: "Ask once, and wait." Judge: "Do not be seen at noon." |
| **Doctrine** | a famous prophet's death (saint) | the prophet's teaching, keyed to their prophecy kind and its outcome | "As Bralrosid taught: the sky keeps its promises late." |
| **Heresy** | a daughter people's covenant differs from its parent's, or a crusade edict | a flipped covenant, or a saint the parent rejects | "The Vai pray as if the god were deaf." |
| **Champion** | the god struck a people's enemy during their named war, or blessed them during it | chosen / spared | "The god stands behind the Thrask in battle." |
| **Silence** | no deed witnessed for 300+ years | sleeps / has gone / is dead / was never | "The god sleeps. Speak softly." |
| **Revival** | a deed witnessed after a Silence | returned / forgave / woke angry | "In the year 1,904 the god woke." |
| **Doubt** | Industry age or later and devotion < 0.25, three failed prophecies, or a doubter's book unanswered for 30 years | weather / a story for children / a word for luck | "Rain is only weather." |
| **Reckoning** | the people's Atomic age, or the world's first nuclear strike | stewards of the god's fire (verdict > 0.2), thieves of it (< -0.2), no god, only fire (forgotten) | "We hold the god's fire now, and must not drop it." |
| **Ark faith** | `ark:started` | pilgrimage (to the god's other house), flight (beyond its reach), coming of age (grown children), alone | "We go to find the god's other house." |

A tenet that a later one contradicts is marked overturned and told once ("The Vai stopped saying that rain was only
weather in the year the sky caught fire over Tel Asha."). Daughters inherit their parent's tenets as of the split.

### 7.2 Culture families colour the cast and the creed

`names::family(lang)` (a read-only accessor, data need D9) weights figures, prophecies and sayings, so the eight
families tell different histories:

| Family | Favoured figures | Favoured prophecies | Sayings lean on |
|---|---|---|---|
| Velessan (harbour, vineyard) | voyagers, rulers | Sea, Rain | the sea, the harvest, the harbour light |
| Korvagh (steppe, pass) | heroes, rulers | Fall, Kin | horses, the forge, the open sky |
| Aavik (lakes, birch) | sages, founders | Return, Healing | ice, still water, long winters |
| Qa'tiri (oasis, star-readers) | sages, prophets | **Ladder**, Star | stars, wells, the road |
| Hanuwe (islands) | voyagers, founders | Sea, Ladder | canoes, reefs, the wind's path |
| Mbendi (forest, savanna) | founders, prophets | Rain, Kin | ancestors, the great trees, rain |
| Thrask (fjords) | heroes, voyagers | Fall, Star | winter, ships, the cold sea |
| Pheraxi (old empire) | rulers (long houses), sages | Great work, Fall | law, stone, the founding |

### 7.3 Perception versus truth

The people can be wrong. When a *natural* event (`natural: true` volcano, quake, lightning, tsunami) strikes a
people whose devotion is >= 0.5, the chronicle may (25 %, fork) tell it as the god's act, and Story records a
**misread** on the people's tenets ("They said the god opened the mountain"). Society's memory stays correct (it
never counts natural events: finding 7), so endings are fair; only belief is wrong. The culture panel shows both
("They believe the god sent the fire on the mountain in 1190. It was the mountain."), which is the voice doc's
open question 3 answered deliberately: flavour in the words, honesty in the rules.

---

## 8. UI moments

All copy below is the chronicle's voice except the HUD's plain labels; colours, fonts and sizes follow
`docs/concept/ui/`. Every story widget reads `story::` accessors (read-only) and the chronicle; none adds per-frame
allocation (refresh on the `story:*` events).

### 8.1 The chronicle feed, the book and toasts

- **Story toasts** (new kind `story`, a quill icon, gold): only for *keystones*: a legend's death, a named feud, a
  fulfilled or failed prophecy, a reconquest, a fate omen, the Ark's name. At most one per 20 s (queue the rest
  into the feed); clicking flies the camera to the cell and opens the book at the entry.
- **The banner** (weight 3) keeps its role; add headings for `act` ("The Age of Iron"), `prophecy` ("As It Was
  Foretold"), `feud` ("An Old Hatred"), `fate` ("What They Expect").
- **The book gets chapters**: entries grouped under each act's title, the act open as the chapter's first line and
  the act close as its last. A **Story** filter shows weight >= 2 story types and keystones only.
- Two new book tabs:
  - **People** (dramatis personae): famous figures and legends, newest first: name, byname, kind glyph, people
    colour, life years, one line of their greatest deed, and Fate. Click: fly to their home.
  - **Threads**: each pair of peoples that met: origin, named wars as a small timeline (year, name, victor, treaty),
    the feud or compact, grudges owed ("Tel Asha, taken 1402"), and whether they are at peace.

### 8.2 The culture panel and the inspector

- **Culture panel:** "Ruler: Ossaro Teodo, the Second of the House of Teodo (34 years on the throne)"; "Prophet:
  Fallolfe, who foretold that the sea would walk into Tel Asha" (open prophecies, with no numbers or game terms);
  "Feuds: the Salt Feud with the Thrask (three wars; Tel Asha owed)"; "They believe:" the current tenets' sayings
  (at most three), with misreads flagged.
- **Settlement inspector:** relic and memory lines: "Here lies the prophet Bralrosid." "Birthplace of Teodo the
  Builder." "Taken by the Thrask in 1402. The Vai still call it theirs." "Fallolfe foretold the sea would come for
  this town."

### 8.3 The HUD fate line

From the Industry age, under the god's epithet: one italic line, the current fate omen in a short form ("The Vai
expect the god to come with them."), dimmed when it is a forecast and gold once an Ark is being built. It changes
only on `story:fate`. This is the player's long-range compass for the ending.

### 8.4 The act title card

On `story:act` (open): a three-second, non-modal title over the planet (gold Cinzel caps), "THE THIRD AGE · THE
KINGDOMS", the epigraph beneath in serif italic, fading out; never pauses the game; skipped while a screen is open.
Accessibility: also written to the feed.

### 8.5 The ending screen, the tale and the timelapse

- **Ending screen:** the ending passage, then the epilogue's three short paragraphs (6.4), then the People tab's top
  five as a roll ("Remembered: Teodo the Builder · Bralrosid the prophet · Dovak, who held Qe-Luun · ...").
- **Tale v4** (section 9.4) replaces the counts-based tale.
- **Timelapse captions:** `story::moments(w)` returns (year, cell, short title) for the keystones; the timelapse's
  caption shows the nearest one ("1402 · The Thrask take Tel Asha").

### 8.6 The planet card

Two new lines on the card's word column, beside the epithet: an **epigraph** (the ending people's last saying,
in italics) and **Remembered** (the most famous figure and the named feud or the Ark's name: "Teodo the Builder ·
the Salt Feud · the Ark Bralrosid"). `story::cardLines(w)` provides both; the existing five numbers stay.

---

## 9. New entry types and templates (proposed text)

All templates are original, written for DEMIURGE in the approved voice: no game terms, no numbers but years and short
counts, no gendered pronouns for placeholders, the god seen from below, minor and notable lines under about 45 words.
Each family should get at least two more variants before shipping (a writing task fit for a helper once the hold is
lifted); the chronicle's template cap and least-recently-used choice apply.

### 9.1 New placeholders

| Placeholder | Meaning | Example |
|---|---|---|
| `{ruler}` / `{ruler_name}` | a ruler's style / name only (+ regnal ordinal) | `Ossaro Teodo` / `Teodo the Second` |
| `{byname}` | a figure's byname | `the Builder` |
| `{house}` | a house | `the House of Teodo` |
| `{reign_ord}` | year of the reign as an ordinal word | `ninth` |
| `{figure}` | any figure's name (+ byname when famous) | `Dovak the Star-Reader` |
| `{hero}`, `{sage}`, `{founder}`, `{doubter}`, `{captain}`, `{voyager}` | figures by kind | `Dovak` |
| `{ship}` / `{ark}` | a ship's name / the Ark's name | `the Ondessa Tavarra` / `the Bralrosid` |
| `{war}` / `{war_ord}` | a named war / its number for the pair | `the War for Tel Asha` / `second` |
| `{feud}` / `{compact}` / `{treaty}` | named threads | `the Salt Feud` / `the Compact of Qe-Luun` / `the Peace of Qe-Luun` |
| `{owed}` | a grievance town | `Tel Asha` |
| `{prophecy}` | the prophecy's words, short, in quotes | `"the sea will walk into Tel Asha"` |
| `{years_ago}` | years since, in words | `six hundred` |
| `{saying}` | a tenet's saying | `Ask once, and wait.` |
| `{act}` | the act's title | `the Kingdoms` |
| `{ruler_name_prev}` / `{capital}` | the previous ruler / the people's capital | `Teodo the Second` / `Tel Asha` |
| `{years}` | a length of time in words (reigns, wars) | `thirty-four` |
| `{age_deed}` | what the age's discovery was, as a verb phrase (one per age) | `married copper and tin` |
| `{fulfilment}` / `{prophecy_short}` | how the prophecy came true / its words without quotes | `the sea walked into Tel Asha` |
| `{old_saying}` / `{deed_short}` | an overturned tenet's saying / the deed that overturned it | `Rain is only weather.` / `set the sky on fire` |
| `{act_state}` / `{act_summary}` | clauses built from the saga for act open / close | `The House of Teodo ruled at Tel Asha.` |
| `{ark_source}` / `{carried}` / `{carried_list}` | what the Ark is named after / what it carries | `Bralrosid the prophet` / `a stone of the Sunwell` |
| `{prophecy_year}` | the year a prophecy was uttered, as an ordinal | `1,758th` |
| `{epithet_first}` / `{epithet_mid}` / `{epithet_last}` / `{age_mid}` | a people's god-names over time / the middle age | `the Waker` / `the Rain-Giver` / `the Gentle Hand` / `Iron` |

As in the voice doc, plural culture placeholders already include "the" and are capitalised at the start of a
sentence; epithets carry their own article.

### 9.2 Templates

**Rulers and houses** (`reign`, w1; a new house w2; a famous ruler's death w2)
- Accession, heir: "{ruler_name} took the stool of {ruler_name_prev} at {capital}, and {culture_plural} said the
  new hand was much like the old."
- Accession, heir: "When {ruler_name_prev} died, the elders of {capital} put the ring on {ruler_name}, who had been
  waiting politely for years."
- New house, conqueror: "With {capital} taken, the old house of {culture_plural} was finished. {ruler} sat in
  its hall and called it {house}, and no one argued aloud."
- New house, ruin: "The house of {ruler_name_prev} fell with the walls of {capital}. From the survivors the
  {culture_plural} chose {ruler}, and {house} began in a tent."
- New house, usurper: "In a hungry year {ruler} came to {capital} with friends and knives, and {house} has ruled the
  {culture_plural} ever since, or says so."
- Death with byname: "{ruler_name} died after {years} years, and was called {byname} from then on, which was more
  than most rulers are called."
- Death, plain: "{ruler_name} of {house} was laid in the ground at {capital}. The bread was the same price the next
  morning."
- Reign opener (prefix): "In the {reign_ord} year of {ruler_name}, ..."

**Heroes** (`hero`, w2)
- Rise, defender: "Before {settlement}, when the walls were lost and the gate was not, {hero} held the gate. The
  {other_plural} went home and told it as an accident."
- Rise, taker: "{hero} of {culture_plural} was first over the wall at {settlement}. The songs say first; the
  {other_plural} say only."
- Legend: "In the {year_ord} year {hero} fell at {settlement}, as heroes do, before the songs about {hero} were
  finished. {culture_plural} finished them anyway."

**Sages, voyagers, founders, doubters, the captain**
- Sage (w1; inspired w2): "It was {sage} of {settlement} who first {age_deed}, or so {settlement} says; four other
  towns say the same of their own."
- Sage, inspired: "{sage} of {settlement} woke one night with the whole of it in mind, and said ever after that the
  idea had not been {sage}'s own."
- Voyager (w2): "{voyager} sailed {ship} out of {settlement} past the last island anyone had named, and came back
  with the news that there were more."
- Voyager lost (w2): "{ship} went out from {settlement} under {voyager} and did not come back. Its name is said at
  every launching since, in case."
- Founder, schism (w2): "It was {founder} who led the far valleys away from {parent_plural}, with a walking stick
  and a list of complaints, and became the first of {culture_plural}."
- Doubter (w2): "{doubter} of {settlement} wrote a book saying that {god_name} was a story told to make children eat
  their bread. It sold well."
- Doubter answered (w2): "Within the year the sky over {settlement} did what {doubter} had said it never would.
  {doubter} did not write a second book."
- Captain (w2): "{culture_plural} chose {captain} to lead the ship, because {captain} had never once in a long
  life been seen to hurry."

**Wars, feuds, compacts, peaces** (`war-named`, `feud`, `treaty`, `compact`, `betrayal`, `reconquest`,
`reconciled`)
- Declared, numbered (w2): "So began {war}, the {war_ord} between {culture_plural} and {other_plural}. The
  old soldiers on both sides recognised the road."
- Declared, reconquest (w2): "{culture_plural} had not forgotten {owed}. In the {year_ord} year they went to
  take it back, and called it {war}."
- Declared, kin (w2): "{war} began: {culture_plural} against {other_plural}, who had the same grandmothers
  and used them as arguments."
- Declared, holy (w2): "{prophet} said that {other_plural} prayed badly, and {war} began, which taught both
  peoples to pray worse."
- Old friends (w2): "War came between {culture_plural} and {other_plural}, who had traded salt and songs for
  {years_ago} years. The traders were the last to believe it."
- Treaty (w2): "{war} ended in {treaty}, signed at {settlement} in the {year_ord} year. Both peoples kept a copy, and
  read them differently."
- God's peace (w2): "{war} ended under a sign in the sky, and the peoples called it {treaty} and were careful,
  for once, to keep it."
- Feud named (w2): "After {war}, people stopped counting the wars of {culture_plural} and {other_plural},
  and began to speak of {feud}, as of the weather."
- Reconquest (w2): "After {years_ago} years, {owed} belonged to {culture_plural} again. The old people who
  remembered it the first time were taken through the gate first."
- Third time (w2): "For the third time {culture_plural} and {other_plural} met at war. The children had
  learned the songs of both earlier ones."
- Reconciled (w2): "In the {year_ord} year no one alive could remember why there had been {feud}, and the
  {culture_plural} and {other_plural} let it lapse, like an old debt."
- Compact (w2): "{culture_plural} and {other_plural} swore {compact}: each to stand by the other's gate.
  The oath was written twice, once in each tongue."
- Betrayal (w2): "{culture_plural} broke {compact} and marched on {other_plural}. The oath stone at
  {settlement} was turned face down."

**Prophecy** (`prophecy`, uttered w2, fulfilled w2 (keystone), failed w1 or w2 with a doubter)
- Uttered: "{prophet} stood in the square of {settlement} and said: {prophecy}. Some wrote it down. Most went to
  market."
- Uttered, Ladder: "{prophet} lay on the roof of {settlement} all night counting stars, and in the morning said: {prophecy}. {culture_plural} laughed, and remembered."
- Fulfilled: "As {prophet} had foretold {years_ago} years before, {fulfilment}. {culture_plural} took the old
  scroll down from the shelf and read it aloud."
- Fulfilled, Return: "The god spoke again over {settlement}, as {prophet} had said it would. {culture_plural}
  had half stopped listening, and were glad they had only half stopped."
- Failed: "The years that {prophet} had promised ran out, and {prophecy_short} did not come. The scroll was moved to
  a higher shelf."
- Failed, town gone: "{owed} was gone before the sea could come for it, and the words of {prophet} were kept for
  another town."

**Myth** (`myth`, formed w1 or w2, overturned w2, misread w1)
- Formed: "In those years {culture_plural} began to say: {saying}"
- Covenant, Giver: "{culture_plural} had prayed often enough to have learned a rule, and they taught it to their
  children: {saying}"
- Silence: "The shrines of {culture_plural} had heard nothing for so long that the priests stopped asking, and
  began to explain. {saying}"
- Revival: "After {years_ago} years of silence the god moved over {settlement}, and {culture_plural} had to
  find out again what they believed."
- Overturned: "{culture_plural} stopped saying '{old_saying}' in the year the god {deed_short} over
  {settlement}."
- Misread: "The mountain above {settlement} opened by itself. {culture_plural} said {god_epithet} had opened it,
  and nobody could prove otherwise."

**Acts** (`act`, open and close w2; never dropped)
- Open: "Here begins the Age of {age}. {act_state}" (`act_state`: one or two clauses from the saga: the leading
  people's ruler or house, its feud, its tenet.)
- Close: "So ended the Age of {age}. In it {act_summary}." (at most three clauses: a famous figure's deed, a thread
  change, a tenet.)

**Ark** (`ark`, w2 except the start and the launch)
- Named: "{culture_plural} named the ship {ark}, after {ark_source}, so that the name would go where they went."
- Carried: "They argued for a year over what to carry, and in the end took {carried}, and too many seeds, and
  not enough music."
- Who stays: "{other_plural} would not go. They said someone had to stay with {god_name}, and meant it either as
  kindness or as a curse."

**Epilogue** (ending screen and tale; one of each paragraph kind that applies)
- Carried: "They carried {carried_list}. And they carried the words of {prophet}, who had said in the {prophecy_year}
  year that their children would walk among the lights; it was the one prophecy they never stopped reading."
- Stayed: "{other_plural} stayed on {planet}, with {feud} unfinished and the god to themselves."
- Of the god: "First {culture_plural} called the god {epithet_first}. In the Age of {age_mid} they said
  {epithet_mid}. At the end they said {epithet_last}, and the last thing they were heard to say on {planet} was:
  {saying}"

### 9.3 Weights and the budget

Story entries use the existing story bucket (`storyPerDecade 0.25`, about 3 notable a minute at 1x). **Keystones**
(act open and close, a feud named, a prophecy fulfilled, a legend's death, a fate omen, the Ark's name) are never
dropped or demoted, but are rate-limited by design: at most one keystone per people per 40 years and three per real
minute in the world. Rulers' accessions and sages are minor and foldable into the decade digest ("three rulers sat in
turn at Tel Asha"). The design target is that the story layer **adds at most 1.5 notable entries per real minute**
and **changes the words of many more**: wars, peaces, prophet lines and destroyed towns now carry names and memory.

### 9.4 Tale v4

`chronicle::tale` becomes act-shaped (`taleMaxParagraphs` 8 → 12):

1. Genesis (as today).
2. The first people and its founder; the origin saying.
3. One paragraph per act reached, from its act-close summary (merge Stone and Bronze when Stone has nothing named).
4. **The people of the book:** the three to five most famous figures, one clause each.
5. **The threads:** the oldest feud and its wars by name, and whether it ended; a compact kept or broken.
6. **The god:** the epithet arc (first, middle, last), the covenant, prophecies kept and broken (with counts in
   words), the misread miracles.
7. The ending passage and the epilogue (6.4).

"These things the scribes remembered above all" is replaced by significance ranking: keystones first (legend
deaths, fulfilled prophecies, feuds, wonders, the first of an age), abandonments last (B5).

---

## 10. Data needed from the simulation

Everything else Story needs already exists (`society::memory`, `regard`, `culture.seen`, `vars.ark`, `s.ark`, the
alliance and climate events, `settlement:destroyed.cause`, `war:declared.blamed/ally`, `war:ended.thanked`).

| # | Need | Owner | Fallback without it |
|---|---|---|---|
| D1 | `settlement:founded.from` (origin settlement id) and `.naval` (crossed open sea) | Civ (`civ::found`) | Story infers the origin from the nearest live settlement of the culture; no sea crossing voyagers |
| D2 | `culture:age.settlement` (the town where the discovery happened: the culture's largest academy town, else the capital) | Economy (emits `culture:age`) | the capital |
| D3 | `battle.odds` (attacker strength / defender strength at the battle) | Diplomacy (`onArrive`) | heroes only from captures of capitals and deciding battles |
| D4 | `war:declared.cause` hint (`border`, `trade`, `crusade`, `ally`, `blamed`) | Diplomacy | Story infers from edicts, grievances and the existing flags |
| D5 | Alliances on (`allyForm <= 1`, `allyCall`), tuned by the sim track | Diplomacy / Balance | friendships narrated without a sim effect |
| D6 | `prophet:died.cause` (`term`, `road`, `town`) | Society | Story infers from `unit:died` and `settlement:destroyed` on the same tick |
| D7 | `names::family(lang)` → 0..7, read-only | Names | uniform weights (loses the family flavour of 7.2) |
| D8 | Saga storage (2.4): `World::saga` + save version 2, or the `vars.str` fallback | State / Schema / Save | the fallback |
| D9 | Bus rows for the `story:*` events (2.3) | Foundation (Events.h) | none: needed |
| D10 | Call order: `story::attach` before `chronicle::attach` in `sim::use`; `story::yearly` after `society::yearly` | Sim | none: needed |
| D11 | (S10 only) `society::addDevotion(w, culture, x)` and a Diplomacy read of `story::temperament(w, culture)` | Society, Diplomacy | phase 1 has no teeth |

---

## 11. Where the player's choices enter the story

| The player does | Story consequence | Payoff later |
|---|---|---|
| Answers a prophet's Rain or Healing prophecy | Prophecy fulfilled; the prophet famous; Giver covenant | The saint's doctrine quoted for centuries; beloved epilogue carries the words |
| Ignores it | Prophecy failed; a false prophet; doubt seeded | A doubter in the Industry age; forgotten or parting fate omen |
| Consecrates a capital during a reign | "the Builder" byname; Great work prophecy kept | The wonder named on the Ark, or its stone carried |
| Strikes one side of a named war | Champion tenet for the other side; blamed war; grievance | The feud remembers the god's side; the struck people's fate leans feared |
| Ends a war with an Omen of Peace | "the God's Peace of X"; a peacemaker byname; a feud may lapse | Reconciliation; the rival goes on the Ark instead of staying |
| Stays silent for centuries | Silence tenet; Return prophecy | Acting then is a Revival (a strong, legible moment); never acting → forgotten |
| Acts in a doubter's people within 30 years | The doubter answered; doubt reversed | Fate omen shifts from forgotten to parting or beloved |
| Lowers land under a town the Sea prophecy named | The prophecy fulfilled cruelly; Judge covenant | Feared fate; the epilogue remembers who drowned it |
| Raises a prophet in a people of its choosing | Who carries the next prophecy, and of which kind | The player steers which promise the world will test |

---

## 12. Sequenced implementation plan with headless tests

Each step is one owner's change, built and then independently verified like every other track. Story tests live in
`SimTests/test_story*.cpp` (real Chronicle, Society, Names; stubs for the rest where possible) and a new simrun
flag `--story` prints the counters below per run and per age. "8 × 4" means 8 seeds × the policies passive, kind,
cruel, chaotic (plus curious, prayers and sculptor where stated), 4,500 years.

**S0. Fix what the run showed (chronicle and society owners, can start now).**
B1 (one ending entry), B2 (one omen line per omen: "over the armies of X and Y"), B3 (one prophet death line),
B4 (the build edict picks a town without a wonder; a failed edict is told), B5 (tale significance ranking), B6
(the god's small acts under `templateCap`), B7 (one awakening line), B8 (sea-taken cities told as the sea's).
*Tests:* no two entries of weight 3 in the same year with the same type family (8 × 4); omen of peace ≤ 2 entries
per use; no duplicate (type, text) on the same year; every build edict is followed within 40 years by a wonder or
an "edict failed" entry (8 × 4, ≥ 95 %); no template over 12 uses per run including the god's acts; the tale's
"remembered" paragraph contains no abandonment when a wonder, war or prophecy entry exists.

**S1. Story module skeleton and wiring (story track; D8, D9, D10).**
`Story.h/.cpp`, the saga storage, `attach`/`yearly`, the event queue, `story:*` events, simrun `--story`.
*Tests:* sim fingerprint identical with Story on and off (8 × 4, 30 of 30 like the budget verifier); the saga
identical across 3 runs of the same seed (8 seeds) and after `--saveload 1500` versus no save; save size growth
< 40 KB at year 4,500; run time within +3 %; attach order: a `war:declared` entry already has its war name.

**S2. Rulers and houses.**
*Tests:* from the Bronze age every live people with a capital has exactly one reigning ruler (every year, 8 × 4);
reigns 15-45 years; a capital captured or destroyed always starts a new house; median 1 new house per people per
600-1,000 years; no two living rulers with the same style; ruler entries ≤ 1 per real minute per age; regnal
ordinals increase within a house.

**S3. Threads: named wars, feuds, treaties, grievances, reconquests (D4, D5 optional).**
*Tests:* every `war:declared` has a name and an ordinal that increases per pair; every `war:ended` names a treaty;
a feud is named for every pair with ≥ 2 wars (100 %); a reconquest entry for every grievance town retaken (100 %);
at least one named feud on ≥ 6/8 seeds for passive and chaotic; the tale names at least one war on every seed with a
war.

**S4. Figures and fame: founders, heroes, sages, voyagers, doubters, the captain (D1, D2, D3, D6, D7).**
*Tests:* fame within 0..1 always; ≤ 160 figures; every people has a founder; every `culture:age` names a sage; every
famous figure has ≥ 2 deeds; 8-15 famous and 3-6 legends per history on ≥ 6/8 seeds (tune if the counts are off);
the share of notable entries from the Iron age on that name a figure, a thread or a tenet (**continuity share**) is
≥ 35 % (8 × 4; my estimate for today, from reading seed 2, is under 10 %: only prophet and edict lines name
anyone; S1's `--story` counter should measure the baseline first).

**S5. Prophecy.**
*Tests (scripted, `test_story_prophecy`):* each kind's fulfilment and failure from a hand-built event sequence;
every prophet utters exactly one; a subject town destroyed fails it at once. *Histories:* `kind` fulfils ≥ 2
prophecies on ≥ 6/8 seeds; `passive` fulfils fewer than `kind` on ≥ 7/8; the Ladder prophecy is uttered before
the Ark on ≥ 5/8 seeds and quoted at the launch when kept; no prophecy names a dead town when uttered.

**S6. Myths, sayings and perception.**
*Tests:* every people has an origin tenet within 50 years of birth and a covenant by its 300th year or first prophet;
`passive` forms a Silence tenet on ≥ 6/8; a deed after a Silence forms a Revival (100 %); misreads only for natural
events and devotion ≥ 0.5 (100 %) and Society's memory is unchanged by them (grace and wrath identical with
misreads on and off); the culture panel's "They believe" has 1-3 sayings for every live people.

**S7. Acts and fate omens.**
*Tests:* exactly one act open per `world:age` and one close before it (8 × 4); every act close names ≥ 2 of
{figure, thread, tenet} from the Bronze age on; every fate omen's mood equals `society::memory(w, c).mood` when told
(100 %); ≤ 1 fate omen per people per 80 years; on `kind` the last fate omen before the Ark is beloved on ≥ 6/8, on
`cruel` feared on ≥ 6/8 (the omen predicts the ending).

**S8. The Ark payoff, epilogue, tale v4, card and timelapse accessors.**
*Tests:* the Ark is named on every Ark ending; the epilogue references ≥ 2 planted seeds (founder, saint, wonder,
feud, Ladder) on ≥ 7/8 seeds per policy; the tale has 6-12 paragraphs and names ≥ 1 figure and ≥ 1 war when any
exist; `story::cardLines` non-empty for every ending; `story::moments` sorted by year, ≤ 60 items.
**Voice lint for every story template and every generated entry (8 × 4):** no unfilled `{...}`; none of "Awe",
"devotion", "tier", "edict", "verdict", "fame", "bucket"; no "he", "she", "his", "her" within six words after a
figure placeholder; weight 1-2 entries ≤ 50 words; no template body used > 10 times per history.

**S9. Client (C7 HUD and C8 screens, after wave 3 releases those files).**
Story toasts and banner headings, the act title card, the culture panel's ruler, prophet, feuds and beliefs, the
inspector's relic lines, the book's chapters and People and Threads tabs, the fate line, the ending epilogue and
roll, the planet card's two lines, timelapse captions. *Tests:* HUD model tests (headless `FDmHudContext`) for the
20-second toast spacing and grouping; scenario shots `story-book`, `story-panel`, `story-actcard`, `story-ending`,
`story-card` on a seeded world, judged by eye; no per-frame allocation (refresh only on `story:*`); 60 fps held
(the panels rebuild on events, not per frame).

**S10. Optional teeth (sim track, each behind a tune flag that is off by default, each with simrun targets).**
A ruler's temperament (fork-rolled, nudged by byname) multiplies the people's aggression ±15 %; a fulfilled
prophecy adds devotion +0.05 to the people (D11); a hero adds +10 % defence to the town while living; a grievance
town owed adds a small war pull toward its holder. *Tests:* the SPEC-SIM section 4 targets and the wave-3 ending
table still hold on 8 × all policies with each flag on; Ark years move < 5 %; `kind` vs `passive` prophecy-devotion
difference visible in `--story`.

**Order and parallelism.** S0 now (no dependency). S1 needs D8-D10 (small foundation changes). S2, S3 and S6 can
run in parallel after S1 (separate parts of `Story.cpp`; split it into `Story.cpp`, `StoryThreads.cpp`,
`StoryMyth.cpp` to allow it); S4 after S2; S5 after S4 (prophets are figures); S7 after S3 and S6; S8 after S5 and
S7; S9 after S8 and after wave 3 hands back the client; S10 last, only if the owner wants the story to move the sim.

---

## 13. Risks and choices

- **Text volume.** The budget's lesson is that more events make the chronicle worse. The story layer earns its
  place by naming and connecting entries that already exist; its own keystones are capped (9.3).
- **Over-explaining.** The voice forbids mechanics; prophecies, fate omens and sayings are the in-world way to show
  the player stakes. The HUD fate line is the one plain-ish hint; keep it in the people's words.
- **Determinism.** The chronicle-budget track had a determinism alarm (UNREAL.md). Story uses no unordered
  containers, iterates saga vectors by index, and its tests run 3 times per seed.
- **Save compatibility.** Version-1 saves load with a rebuilt minimal saga (one ruler per capital, an origin tenet
  per people); a test loads a v1 save fixture.
- **Ownership.** Nothing here may start while wave 3 owns the client and the sim track owns `Sim/`; S0 goes to
  those owners as findings now.
