# DEMIURGE: the eight culture families

*Design document for the names of peoples, places, persons, rulers and ships. Implemented as data tables in
`Unreal/Source/Demiurge/Sim/Names.cpp` (track "names-cultures", 2026-11-19). The tables there and this
document must agree. When one changes, change the other.*

## 1. What this is for

A DEMIURGE world grows one family tree of peoples. The first tribe wakes speaking a **root language**, drawn
from one of eight **culture families**. Every later people splits from an existing one and speaks a
**daughter language**: the same family, drifted by regular sound laws, sometimes touched by a neighbouring
family. Over 4,000 years a typical world holds 6 to 12 peoples and 130 to 150 named towns.

The aims, in the order the player feels them:

1. **Beautiful and pronounceable.** Every name reads aloud on the first try and sounds like a place
   someone loves.
2. **Recognisable.** A handful of place names is enough to tell which family is speaking (measured: 5
   names identify the family 99.8 % of the time, 1 name 81 %).
3. **Distinct within a world.** No two names in one world are near-twins. "Girtdimton" next to
   "Girtdemton" is impossible by construction (section 3.3).
4. **Clean.** No accidental real words, real places, real deities, famous invented names, slurs or
   profanity, in English or in the languages whose sounds the families echo (section 3.4).
5. **Related but different.** A daughter people's names are recognisably kin to its mother's, but not
   copies of them.

The families are invented. Several echo the *texture* of real language groups (that is what makes them
readable), but no word lists were copied, and real words that come out by chance are blocklisted.

## 2. The families at a glance

| # | Family | Who they are | You know them by | Sample places |
|---|---|---|---|---|
| 0 | **Velessan** | harbour and vineyard peoples of warm coasts | open vowel-final words, l r n s v, a doubled consonant after the stress, *-essa -enna -ara -etta* | Tenamimorra, Sovamirella, Dovra Besivia, Lusa Mavarra |
| 1 | **Korvagh** | horse-lords and smiths of steppe and high passes | closed syllables, kh gh zh, back vowels, hyphenated clan names, *-dar -mor -ukh -vakh* | Kruk-Ukokh, Zhalurak, Bozd-Obad, Zubazog |
| 2 | **Aavik** | lake and birch-forest peoples of the north | vowel harmony, doubled vowels (aa, uu) in the first syllable, doubled consonants, long words, *-lanna -vanto -harju* | Vultamorosti, Lemitekin, Avasolis, Molahotvanto |
| 3 | **Qa'tiri** | oasis-city traders and star-readers of the deserts | only a i u, q kh sh z, glottal stops written ', long final vowels, *-arun -isham -aqim*, two-word names | Khiduqash, Tiru Yarurad, Yahsa'udar, Jikha'abir |
| 4 | **Hanuwe** | island voyagers of the reefs | nine consonants, only open syllables, vowel pairs, reduplication, *-hiwa -kele -tola* | Kawamena, Tangohiwa, Mingilehu, Monimone |
| 5 | **Mbendi** | forest and savanna peoples | prenasal onsets mb nd ng nj nz (also word-initial), open syllables, bound prefixes u- ki- ma-, *-zimba -ndaba -lela* | Iwingoli, Losunjiri, Abozimba, Manilanda |
| 6 | **Thrask** | fjord peoples of the cold coasts | h-clusters hr hl hv thr, sk st sv, heavy codas rd rn ld nd, *-vard -heid -brekk -fjar* | Gafoveid, Dauteskel, Svigobrun, Nenuvang |
| 7 | **Pheraxi** | the old imperial tongue of law and stone | ph th x, clusters pr tr str kl, antepenultimate stress, *-ion -eum -axis -ostra -thalos* | Thosimophor, Hollalion, Uxiostra, Ailalos |

Every root language picks its family uniformly (measured over 400 seeds: 38 to 63 languages per family).

## 3. How a name is made (all families)

### 3.1 A language

A language is an opaque text record, `dm2;f=<family>;g=<generation>;c=<contact family>;L=<sound laws>;...`.
It holds the language's own inventories (each sound with a weight), its probabilities, its syllable and
form weights, its recurring roots, place endings, place prefixes, person endings, ruler titles, ship
words and its word for the player-god. A root language starts from its family table, then:

* **thins** the inventories (each sound may drop out, 12 % for consonants, 6 % for vowels, never more
  than a quarter of a small inventory) and jitters every weight by one;
* jitters every probability by up to a sixth;
* takes 16 to 20 of the family's place endings, 3 to 5 prefix words, 2 ruler titles and 3 ship words;
* coins 22 recurring roots and picks its god's word.

So two Velessan languages sound like cousins, not twins. Older `dm1` records from saves before this
track, and unreadable text, become a language derived from a hash of the text (same family every time),
and a `dm1` god's word is kept, so old saves keep naming deterministically.

### 3.2 Syllables, stress and euphony

A syllable is *onset + nucleus + coda*. Each family gives:

* **onsets**: one list for the start of a word, one for inside it, each with weights; an empty onset
  (a vowel-initial word) has its own probability. Inside a word an empty onset is a hiatus (Hanuwe,
  Pheraxi: only listed vowel pairs) or a glottal stop written `'` (Qa'tiri).
* **nuclei**: vowels with weights, plus long vowels and diphthongs that appear only in the stressed
  syllable.
* **codas**: one list inside a word, one at its end, each with a probability.
* **stress**: initial, penultimate, final or antepenultimate. It decides where long vowels and doubled
  consonants go (Velessan *Ve-LES-sa*, Aavik *KAA-ri-lan-na*).
* **forbidden sequences**: per family, plus a shared list (English-looking endings *-ton -ham -wick
  -ford -ville -burg -shire -field -land -ing -son -man -ley -by -ster -ness -hold -mark -port -town
  -wood -hill -dale*, a word starting *Iron* or *Iraq*, a long vowel pressed against a glottal stop).

Every name passes the shared sound rule (no 4 consonants or 3 vowels in a row, no letter three times)
and the **euphony rule**: no stutter (the same two, three or four letters twice in a row, "rara", "nene",
"ulul", "Rigirigil"; a reduplicating family may open a word with a doubled pair or a doubled syllable pair,
"Lelemoa", "Wihiwihi"), at most one glottal stop per word and never
between equal vowels other than *a*, and each word at most the family's word length (9 to 12 letters; Velessan 10).
The number of syllables is chosen from the language's own **collision entropy** (how often two random
draws come out the same), so small sound systems (Hanuwe) get longer names and rich ones (Thrask) stay
short, all inside the word cap.

### 3.3 The spelling code: no near-twins in any world

Every place, person and people name is spelled so that the values of its letters (a = 1, ..., z = 26,
separators ignored) sum to **11 modulo 29**. A single substitution, insertion or deletion changes that sum
by 1 to 26, never by a multiple of 29. So **no two coded names, in any languages, ever differ by one
letter**. A world can still hold the same name twice (Civ re-rolls an exact repeat), but never "Thora"
next to "Thova" or "Girtdimton" next to "Girtdemton".

Names of 10 or more letters also carry a second sum, the letters weighted by their positions (1, 2, ...),
**5 modulo 29**. Because 29 is prime, two coded long names of the same length can never differ by two
substitutions either: that would need d1 + d2 = 0 and i·d1 + j·d2 = 0 modulo 29, so (i − j)·d1 = 0,
which is impossible.

The code costs nothing audible. The last syllable of the specific part is the free **slot**: it is drawn
among every onset × nucleus × coda the language allows, weighted as the language would draw them,
keeping only the syllables that make the sums come out right. For long names where one ending leaves too
few syllables, the slot syllable and the ending are drawn together. Taking a twenty-ninth of the
possibilities costs about 4.9 bits of variety, which the syllable-count rule pays back.

Measured: in the 16 simulated worlds of section 6, near-duplicate pairs (one letter apart, or two for
names of 10+ letters) went from **60 to 0**. In 120 test worlds (16,800 towns) there were 0 pairs one
letter apart, 0 long pairs two substitutions apart, and 4 long pairs two edits apart that differ in length (the second code covers equal lengths).
Verifier's re-measurement (seeds 1 to 8 × four policies, 32 worlds, 4,510 town, people and prophet names):
one letter apart **128 before, 0 now**; two edits apart in names under 10 letters 1,024 before, 187 now
(about 6 a world, mostly shared endings with two vowels changed, "Limupako" / "Lumipako"; the code does not
cover these).

### 3.4 The blocklist

`Names.cpp` keeps two lists, grouped at compile time (fragments by first letter, whole words by first letter
and length):

* **fragments**, refused anywhere in a name's letters: English profanity and slurs (fuck, shit, cunt,
  nigg, kike, chink, gook, coon, ...); slurs and obscenities from the languages the families echo (kaffir,
  kafir, sharmut, puta, kurwa, pizd, vittu, perkele, mboro, malaka, manko, kontol, kanaka, ...); hitler,
  satan, lucifer; and famous invented names that would read as imitation (mordor, gondor, sauron, khazad,
  valinor, numenor, westeros, dothrak, valyria, tatooine, arrakis, azeroth, tamriel, skyrim, hogwart,
  narnia, hyrule, ...); and, added by the verifier, nazi, pedo, semen, titt, bozo, boner, the Finnish
  vulgar pillu kyrp mulkk runkk perse saatan and helvet (Aavik echoes Finnish), the Zulu and Swahili nyoko
  and msunu (Mbendi), kurva, chinga, anjing and kanake;
* **whole words**, refused as a whole name, as one word of it, or as a piece between glottal stops: short
  profanity and slurs (ass, tit, fag, homo, nazi, suka, khara, kuma, randi, baka, puki, titi, babi,
  sambo, bimbo, porra, verga, pula, kuk, ...); about 120 real deities and holy names (zeus, thor, odin,
  allah, jesus, shiva, osiris, pele, maui, tane, ukko, nzambi, ngai, akua, ...); about 250 real places
  the families can produce (rome, lima, oslo, malta, kenya, congo, tonga, samoa, fiji, oahu, vaasa,
  oulu, sparta, athens, troy, qatar, shiraz, zanzibar, ...); about 300 common real words, given names
  and loanwords the sound systems can make (banana, piano, sushi, aloha, safari, santo, villa, tina,
  maria, omar, dear, door, dog, god, orc, elf, endor, rohan, ...); about 1,400 short common English words
  (added by the verifier: the clan names, two-word places and ships had made "Dad-Vazhuk", "Pig-Vrubuk",
  "High-Todri", "Dovra Same", "Amme Side", "Lose Pefas", "Vessa Life", and a town called "Pirate"; the
  words of the default world names, such as vale, hold and keep, are left out); and the Polynesian vulgar
  ule, kohe, laho, tiko, ure, hika (Hanuwe).

A blocked name is redrawn. The test-side scans (sections 6 and 7) check 230,000 generated names against an
independent sample of the list and find none.

### 3.5 Kinds of names

| Kind | Pattern | Length | Coded |
|---|---|---|---|
| **place** | a form drawn by the family's weights: *specific + ending* (`Vel+essa`), *bare word* (`Amarenne`), *two words* (`Lusa Amarenne`), *hyphenated clan compound* (`Tor-Uzgal`), *reduplicated* (`Lele+moa`), *bound prefix* (`Ki+ndembe`); about 10 % use one of the language's recurring roots, with an ending or before two fresh syllables | 3 to 14 characters, each word at most the family cap | yes |
| **person** | fresh specific + a person ending (65 %), else a bare 2 to 3 syllable word | 3 to 10 letters | yes |
| **people** | name = fresh stem + people ending; adjective = name + adjective ending; plural = "the " + name + plural ending (a final vowel gives way to a suffix vowel) | name at most 10, plural at most 14 with "the " | name yes |
| **god** | a divine root + divine ending of the family, fixed in the language; daughters inherit it through their sound laws | 3 to 12 | no |
| **ruler** | `names::ruler`: a ruler title of the language + a person name, `Ossaro Teodo` | at most 24 | person yes |
| **epithet** | `names::epithet`: an English byname, 70 % from the family's list (15 % chance of the contact sister's), else a common one | at most 24 | n/a |
| **ship** | `names::ship`: a ship word + a short place-like word, `Ondessa Tavarra`, or a single place-like word | at most 24 | yes |

The plural rule keeps the stem: the people's name minus a final vowel begins the adjective and the plural,
so "the Umari" are "Umaran".

### 3.6 Daughters: how a people's sound drifts

When a people splits off, its language is the mother's after:

1. **one or two regular sound laws** of the family (at most 8 in a language's whole history), applied to
   every sound of the inventories (by position: word-initial onsets, inner onsets, nuclei, codas) and to
   every stored word: roots, place endings, prefixes, person endings, titles, ship words and the god's
   word. A law is only taken when it changes something, never undoes one already taken (u > o and
   o > u exclude each other), and a stored word keeps its old form if the law would make it unsound;
2. **weights drifting** by one on about a third of the sounds;
3. **about a third of the place endings renewed** from the family's stock (passed through the
   language's laws);
4. **about 60 % of the roots kept**, the rest coined anew (at most 32);
5. now and then (30 %, once per lineage) **contact** with the family's sister: three of the sister's
   place endings are borrowed as they are (they must suit the family's own forbidden sequences);
6. sometimes a new prefix word.

Fresh words in a daughter come from the drifted inventories, so new names show the laws too. The god's
name drifts a letter or two per generation (Pheraxi *Elaxeus > Elaseus > Elaseu*, Qa'tiri *Aqub > Akub*).
A daughter keeps its family. After four generations the test classifier still names its family, or its
contact sister, for all eight.

## 4. The families

Notation: `sound:weight`; *p* = probability; V/A/O/U in Aavik endings are harmonic vowels (A = a or e,
O = o or e, U = u or i, back or front).

### 4.0 Velessan: the harbour tongues

*Identity.* Peoples of warm coasts, river deltas and rain-cut terraces; seafarers, orchard keepers and
choral historians who prize welcome, graceful public works and promises kept in full view. They read a
kind god as the harbour-light that brings people safely together, but a cruel god as a faithless current
and an absent one as a horizon that has ceased to answer. Their names sound sunlit and lilting.

* **Architecture and colour**: limewashed stone stepped into slopes, shallow terracotta roofs, arcaded
  cistern courts, painted shutters and cloth awnings. The readable palette is warm chalk `#E7D7B8`, kiln
  red `#A84F32`, leaf green `#53704A`, deep-water blue `#244D63` and festival gold `#D7A83C`; daughters
  should change one accent, never the chalk-and-water base.

* **Onsets**, word-initial: l 4, m 3, n 2, r 2, s 4, v 4, t 3, d 3, b 2, f 2, p 2, g 1, tr 1, pr 1, br 1,
  fl 1; empty 12 %. Inner: l 5, m 3, n 4, r 4, s 3, v 3, t 3, d 3, f 1, b 1, p 1, g 1.
* **Vowels**: a 5, e 4, i 3, o 4, u 1; stressed long nuclei ia 2, io 1, ea 1 (*p* 9 %).
* **Codas**: inner l 3, n 3, r 3, s 2 (*p* 18 %); final n 3, l 2, r 2, s 2 (*p* 16 %: most words end in a
  vowel).
* **Shape and stress**: (C)V syllables, sometimes CVC; penultimate stress; after the stressed vowel a
  following l n r s t m may double (*p* 30 %): *Ve-les-sa, Tor-ra-no*.
* **Forbidden**: any vowel pair but ia io ea; clusters across syllables other than liquid or nasal + stop
  (nr nl lr rl ln mn vt dt sd ...).
* **Places** (cap 10 letters, 11 before the verifier: 38 % of places had 11 or more letters, "Lefemafanto",
  now 7 %): ending 60 %, bare 26 %, two words 14 %. Endings: *-essa* harbour, *-ara*
  vineyard, *-enna* spring, *-ossa* cliff, *-ella* shore, *-ino* bridge, *-anto* meadow, *-orra* tower,
  *-ivia* road, *-alle* vale, *-isca* fishing place, *-onde* bay, *-ora* shrine, *-ile* isle, *-etta*
  little, *-aro* field, *-elia* grove, *-anne* well, *-iro* hill, *-esca* market, *-ovia* way, *-arra*
  terrace, *-olla* cove, *-ianne* fountain, *-essi* steps, *-orano* old town. First words of two-word
  names: *Lusa* (light), *Sanne* (holy), *Dovra* (old), *Amme* (mother), *Tella* (high), *Viro* (green).
* **Persons**: endings -o, -a, -io, -eo, -ina, -ano, -ello, -ia, -etto, -ene, -ira: *Teodo, Lorina,
  Silvano*.
* **Peoples**: name endings none / -a / -e; adjective -an, -ese, -ine; plural -i: *Tavora / Tavoran /
  the Tavori*.
* **God**: roots aul ilv oren vael lum sar ombr + endings -ione -are -ello -ia -esse -iro: *Aulione,
  Sarello, Ilvare*.
* **Rulers**: *Ossaro* (harbour-lord), *Tavenne* (first mother), *Vessaro* (keeper of the sea),
  *Doravo* (elder).
* **Epithets**: the Tide-Born, the Fair-Sailing, the Vine-Crowned, the Golden-Voiced, the Gentle, the
  Far-Faring, the Lamp-Bearer, the Harbour-Builder, the Laughing, the Sea-Wed.
* **Ships**: *Lusa, Ondessa, Silvara, Morenna, Vellara* + a short place word: *Ondessa Larivetta*.
* **Drift laws**: t > d between vowels, b > v between vowels, ss > s, e > i, s > r between vowels,
  final a > e, u > o. **Sister**: Pheraxi.
* **Samples**: places Lanamavanne, Tenamimorra, Damigerivia, Lulosogile, Merigeganne, Lefemafanto, Trenefa, Limanuganto, Tapefu, Damasagorra, Dovra Besivia, Fofalaganto, Sillibamu, Masavovanne, Damisemenna, Lusa Rifivia; persons Marnofuseo, Trerogo, Bosavonia, Garoreneo, Bavanoma.

### 4.1 Korvagh: the hard tongues of steppe and pass

*Identity.* Mobile herders, smiths and oath-holds of high grasslands and mountain passes, valuing
endurance, frank speech, guest-right and the freedom to move. A god earns honour by meeting hardship
openly—rain in a lean season or fire against an invader—while unexplained punishment is remembered as a
broken oath. Their names are short, hard and proud.

* **Architecture and colour**: low wind-backed halls inside angular stone corrals, felted travel houses,
  blackened timber gates and hammered metal ridge marks. Use soot `#242323`, iron brown `#594238`, dry
  grass `#A88B54`, blood red `#8A3030` and cold turquoise `#3D7472`; daughter cultures shift the metal
  inlay colour while keeping dark massing and sharp silhouettes.

* **Onsets**, initial: k 3, g 3, d 3, t 2, b 3, r 2, v 2, z 1, kh 3, gh 2, zh 1, m 1, n 1, s 1, h 1, dr 2,
  gr 2, kr 2, br 1, tr 1, vr 1, khr 1; empty 15 %. Inner: k 2, g 3, d 3, t 2, b 2, r 3, v 2, z 1, kh 2,
  gh 2, zh 1, m 1, n 2, l 2.
* **Vowels**: a 4, o 4, u 4, e 1, i 1 (back-heavy); no long vowels.
* **Codas**: inner r 4, z 1, g 2, k 2, n 2, m 1, d 1, l 1 (*p* 35 %); final k 3, r 3, d 2, g 2, z 1, kh 3,
  gh 3, n 1, m 1, rk, rg, rd, zd, sk (*p* 85 %: words close on a consonant).
* **Shape and stress**: CVC, CCVC; initial stress; no doubled consonants, never two vowels together.
* **Forbidden**: any vowel pair; doubled consonants; h after a consonant except in kh gh zh; clashing
  stops (td gk bp dg ...); zh before a consonant.
* **Places** (cap 10): ending 58 %, bare 24 %, hyphenated clan compound 18 % (*Tor-Uzgal*). Endings:
  *-dar* fort, *-mor* hill, *-ukh* pass, *-gul* well, *-tash* stone, *-zog* camp, *-drun* gate, *-vakh*
  river, *-orn* peak, *-bod* ford, *-rak* crag, *-dum* hall, *-ghaz* cairn, *-kor* horse-run, *-uzd*
  lake, *-argh* fire-hill, *-osk* watch, *-druk* bridge, *-maz* field, *-gat* gorge, *-ubar* yard,
  *-oz* spring.
* **Persons**: endings -ak, -uk, -ar, -osh, -ag, -ur, -od, -ug, -az, -um: *Bograt, Uzhak, Drukmar*.
* **Peoples**: name with no ending; adjective -i; plural -ar: *Durvagh / Durvaghi / the Durvaghar*.
* **God**: roots ukh gor ard bur khor vog zhar + -tar -um -agh -od -ukh: *Ukhtar, Khorum, Ardagh*.
* **Rulers**: *Ughtar* (war-chief), *Dravokh* (iron-lord), *Zhogan* (high oath-speaker), *Borukh*
  (shield-father).
* **Epithets**: the Iron-Handed, the Unbowed, the Horse-Lord, the Stone-Breaker, the Grim, the Red-Axe,
  the Far-Rider, the Hammer, the Wolf-Eyed, the Thunder-Voiced.
* **Ships** (river barges and siege-rafts): *Ughrak, Morkh, Zagra, Durvakh* + a short word: *Durvakh
  Mibezosk*.
* **Drift laws**: g > gh between vowels, u > o *or* o > u (never both), final d > t, z > zh, initial
  b > p. **Sister**: Thrask.
* **Samples**: places Kruk-Ukokh, Huri-Zakumdrun, Moriboz, Zhalurak, Dragazoz, Gigonmor, Higidzun, Vrogturun, Nodevulum, Bozd-Obad, Krarunan, Sigtodrun, Zubazog, Zhodmugul, Vumakrak, Ghurubu; persons Zotorerur, Omorogo, Ugalug, Grudmuter, Agotebar.

### 4.2 Aavik: the harmonic tongues of the northern lakes

*Identity.* Lake-dwellers, trail herders and knot-song keepers of pale forests and long winters, valuing
patience, exact memory and stores shared before the thaw. They regard a helpful god as the unseen warmth
beneath ice; spectacular wrath is less frightening than fickleness, and long silence becomes a solemn
fact rather than an insult. Their names are long, flowing and patient.

* **Architecture and colour**: steep timber roofs on stone heat-banks, enclosed smoke courts, carved
  lintels and raised winter boardwalks. Use snow linen `#D9DED8`, lake blue `#436C78`, bark grey
  `#64645D`, ember orange `#C56C36` and berry violet `#71465F`; daughters alter carving rhythm and the
  ember accent while preserving pale roofs against dark vertical walls.

* **Onsets**, initial: k 3, t 3, h 3, m 2, n 2, p 2, r 1, s 3, v 3, j 1, l 2; empty 18 %. Inner: k 3, t 3,
  h 1, m 2, n 2, p 2, r 3, s 3, v 2, j 1, l 4. No onset clusters.
* **Vowels, harmony**: a word is all **back** (a 4, o 3, u 3, with neutral i 1) or all **front** (e 3,
  i 3). Stressed first-syllable long nuclei, written double: aa, oo, uu, ai, au, ou, uo, oi (back); ee,
  ii, ei, ie (front) (*p* 33 %).
* **Codas**: inner n 3, l 3, r 2, s 2, t 1, k 1 (*p* 26 %); final n 2, s 1, t 1 (*p* 12 %).
* **Shape and stress**: (C)V(C); initial stress; after the first syllable k t p l n s m may double
  (*p* 35 %): *Kaa-ri-lan-na, Sik-ka-ru-pak-ka*.
* **Forbidden**: j except before a vowel; h, v and most stops as a coda before a consonant; any vowel
  pair but the long nuclei.
* **Places** (cap 12): ending 70 %, bare 30 %. Harmonic endings: *-lAnnA* castle-hill, *-vAntO* bay,
  *-rOsti* ridge, *-mAkki* hill, *-sUlmi* strait, *-kOrpi* wilds, *-jArvO* lake, *-lAhtO* cove,
  *-hOlmi* islet, *-pAkkA* fell, *-nUmmi* heath, *-sAlO* forest, *-tOlA* farm, *-rAntA* shore,
  *-vUOtO* rapids, *-kAisU* narrows, *-tAistO* meeting place, *-rAvilA* winter road, *-kUUsi* spruce,
  *-hArjU* esker. Back: *Sikarupakka*; front: *Ilmeteli*.
* **Persons**: endings -O, -i, -A, -kki, -mO, -jA, -ri, -tti: *Lunumavo, Huppasoni, Vekiminiri*.
* **Peoples**: name ending -A or -O; adjective -n; plural -t: *Kaarela / Kaarelan / the Kaarelat*.
* **God**: roots halv sier kuuv erv tuov aarn + -AtAr -O -kki -rAs: *Tuovatar, Halvokki, Sierere*.
* **Rulers**: *Vaalto* (the chosen), *Kuura* (frost-elder), *Hallava* (keeper of the hall).
* **Epithets**: the Frost-Born, the Patient, the Lake-Singer, the Birch-Crowned, the Long-Wintered, the
  Rune-Wise, the Quiet, the Far-Hearing, the Reindeer-Keeper, the Snow-Eyed.
* **Ships**: *Ahvo, Kuulas, Raavi, Sarka* + a short word: *Sarka Neikinimmi*.
* **Drift laws**: long vowels shortened (aa > a ...), geminate stops single (kk > k ...), initial s > h,
  ei > e, oo > uo and ee > ie, e > i. **Sister**: Thrask.
* **Samples**: places Lorilirosti, Alasalkuusi, Vultamorosti, Santollinus, Ippajamakki, Lemitekin, Sotovavuoto, Avasolis, Antapasulmi, Miikelerke, Mukkolurosti, Sinelkeni, Molahotvanto, Hamivalkaisu, Otokovon, Massoranummi; persons Haimopuna, Vorosi, Inneltes, Mivissive, Seilmivi.

### 4.3 Qa'tiri: the desert tongues of the oasis cities

*Identity.* Route traders, well-finders and night-sky recorders of dry basin cities, valuing measured
speech, water held in common and debts that survive generations. They treat a benevolent god as the
keeper of the hidden reservoir, a destructive god as a squall without law, and silence as a test to be
answered with observation rather than panic. Their names are dry, bright and precise, with a catch in
the throat.

* **Architecture and colour**: thick earth walls around shaded slot-courts, wind towers, vaulted water
  chambers and luminous tile bands used only at civic thresholds. Use sun clay `#C88957`, shade plum
  `#493747`, salt white `#E7D8BB`, night indigo `#263C61` and copper green `#4F7C6A`; daughters change
  tile geometry and the night accent while retaining high blank walls and deep apertures.

* **Onsets**, initial: q 3, k 1, kh 2, sh 3, s 3, z 2, h 3, t 2, d 3, r 1, n 2, m 3, b 1, j 1, f 1, y 1;
  empty 14 %. Inner: q 2, kh 2, sh 2, s 2, z 1, h 2, t 2, d 2, r 4, n 3, m 2, b 2, f 1, l 3; an empty
  inner onset after a vowel is a **glottal stop** written ' (*p* 6 %): *Ki'ulur*.
* **Vowels**: only a 6, i 4, u 3. Long aa 4, ii 1, uu 2 in the stressed final syllable (*p* 18 %).
* **Codas**: inner r 3, n 2, m 2, sh 2, z 2, b 1, d 1, q 1, h 1 (*p* 40 %); final r 3, n 4, m 2, sh 2,
  z 2, d 1, q 1, b 1, t 1 (*p* 55 %).
* **Shape and stress**: CV(C); final stress.
* **Forbidden**: e and o; doubled h, k, q; q next to k, h, d, b or another stop; h after a consonant
  except kh sh; more than one glottal stop in a word; a long vowel against a glottal stop.
* **Places** (cap 11): ending 55 %, bare 30 %, two words 15 %. Endings (they begin with a vowel, so the
  specific closes on a consonant or meets them through a glottal stop): *-arun* well, *-isham* palace,
  *-udar* gate, *-aqim* oasis, *-azir* tower, *-iyun* springs, *-anuq* dune, *-ahar* garden, *-ushta*
  market, *-imar* citadel, *-abir* crossing, *-uzan* mountain, *-irun* cistern, *-aqar* rock, *-unab*
  palm-grove, *-ishan* shrine, *-zuun* stars, *-tibar* caravanserai, *-amil* camp, *-urad* walls. First
  words: *Qasu* (fortress), *Ishad* (well of), *Saru* (city), *Mahz* (camp), *Tiru* (holy),
  *Dimar* (house of).
* **Persons**: endings -ad, -un, -ir, -ia, -ima, -uz, -an, -it, -ash: *Nadakha, Hilamiq, Zahirun*.
* **Peoples**: name with no ending; adjective -i; plural -un: *Qushan / Qushani / the Qushanun*.
* **God**: roots il shu nur aq zar hab + -ash -ima -unar -ihun -ub: *Ilash, Aqub, Zarihun*.
* **Rulers**: *Qadun* (judge-king), *Ishar* (heir-speaker), *Zahar* (keeper of wells), *Hamzir*
  (water-counsellor).
* **Epithets**: the Star-Reader, the Patient, the Well-Finder, the Veiled, the Thrice-Wise, the
  Sand-Walker, the Generous, the Silent, the Lantern of the Dunes, the Caravan-Master.
* **Ships** (sand convoys and lateen sailers): *Shubara, Iyaru, Nubira, Qamira* + a short word:
  *Shubara Turaz*.
* **Drift laws**: q > k, kh > h, sh > s, long vowels shortened, b > v between vowels, t > d between
  vowels. **Sister**: Mbendi.
* **Samples**: places Mahz Nidni, Jimisunab, Khiduqash, Hazkhizi, Idaraqar, Tiru Yarurad, Qasu Sizurad, Akhahiyun, Jirusu, Yahsa'udar, Nadirub, Ishad Sabamil, Jikha'abir, Jashimi, Zira'udar, Imrilaqar; persons Nadakha, Ta'amfuzia, Zusilib, Amuliza'an, Baquhaar.

### 4.4 Hanuwe: the island tongues of the reefs

*Identity.* Navigators and reef-fishers who read stars and swells, valuing reciprocal gifts, skilled
improvisation and the obligation to return for those left behind. A god is understood as another great
presence in the weather: loved when it guides and replenishes, opposed when it demands submission, and
laughed about gently when its signs prove ambiguous. Their names are soft, rolling and generous.

* **Architecture and colour**: lashed timber frames on stone storm feet, layered leaf-fibre roofs,
  open meeting decks and bright wayfinding panels visible from the water. Use shell cream `#E9D9B7`,
  lagoon teal `#27858A`, leaf green `#4E7B55`, volcanic charcoal `#37383A` and coral `#D66B5D`;
  daughters vary the panel pattern and coral accent while keeping light roofs over porous walls.

* **Onsets**, initial: h 3, k 3, l 3, m 3, n 3, p 2, t 3, w 3, ng 1; empty 12 %. Inner: h 2, k 3, l 4,
  m 3, n 3, p 2, t 3, w 2, ng 1; an empty inner onset makes a vowel pair (*p* 16 %), only ai au ae ao ei eo
  oa oe ou ua ue ui ia io iu eu.
* **Vowels**: a 5, e 3, i 3, o 4, u 4. No long vowels written.
* **Codas**: none. Every syllable is open.
* **Shape and stress**: (C)V; penultimate stress; **reduplication** of the first syllable (14 % of places):
  *Lele-moa, Hehe-ka*.
* **Forbidden**: wu, uw, doubled h or w, ng before a consonant.
* **Places** (cap 9): ending 48 %, bare 38 %, reduplicated 14 %. Endings: *-kele* reef, *-lehu* ash-hill,
  *-hiwa* dark water, *-wane* beach, *-tola* landing, *-pilo* lagoon, *-nahe* gentle bay, *-mena* point,
  *-hoki* return, *-niwa* palms, *-tewa* outer isle, *-pako* rock, *-wiko* channel, *-hume* cave, *-lana*
  calm, *-poki* fish-weir, *-moa* bird isle, *-wae* fresh water, *-wehi* sacred grove, *-mone* white
  sand, *-nuka* hill, *-kipu* pass in the reef, *-toa* strong place, *-lewa* sky-hill.
* **Persons**: endings -a, -o, -ewa, -ono, -ahi, -ele, -ua: *Talekane, Opelu, Hinganao*.
* **Peoples**: name with no ending; adjective -an; plural = the name: *Kalewe / Kalewan / the Kalewe*.
* **God**: roots mahe kowa uhi pela awi + -walu -iwi -ao -eke -ani: *Maheke, Awiwalu, Kowaao*.
* **Rulers**: *Hekau* (steersman), *Pokane* (first of the canoes), *Lewahi* (sky-reader).
* **Epithets**: the Wave-Walker, the Star-Steerer, the Reef-Born, the Many-Islanded, the Child of Fire,
  the Long-Voyager, the Shell-Crowned, the Laughing, the Wind-Reader, the Net-Mender.
* **Ships** (voyaging canoes): *Kalowa, Hinemo, Tewaha, Maheke* + a short word: *Hinemo Lotoa*.
* **Drift laws**: t > k, l > r, ng > n, e > i, u > o (w is kept: it is the family's mark). **Sister**:
  Velessan.
* **Samples**: places Kawamena, Tokapoki, Hukalana, Poponua, Kopihume, Numohoka, Tangohiwa, Pokohiwa, Nanoweko, Mingilehu, Monimone, Pumomena, Wamolehu, Kenumone, Wenohiwa, Moaweka; persons Telole, Talilo, Moamiahi, Monemi, Nipui.

### 4.5 Mbendi: the forest and savanna tongues

*Identity.* Drum-towns of the rain forest and seasonal herders of open grass, valuing public counsel,
kinship made by service and wealth that circulates. They greet a responsive god as a powerful elder who
must still listen to the circle; mercy grows into affection, indiscriminate wrath into resistance, and
silence transfers sacred responsibility back to the living. Their names are round and resonant.

* **Architecture and colour**: clustered earth-and-timber compounds around circular council courts,
  deep woven eaves, raised granaries and patterned rain screens. Use wet earth `#604333`, canopy green
  `#2F6048`, clay ochre `#C47B3B`, woven cream `#D7C59B` and sky blue `#4B86A1`; daughters change rain-
  screen pattern and court accent while preserving clustered circles and generous shade.

* **Onsets**, initial: mb 3, nd 3, ng 2, nj 1, nz 1, b 1, d 1, k 3, t 2, l 2, m 2, n 1, s 2, w 2, y 1,
  ny 1, kw 1, gw 1, z 1; empty 10 %. Inner: mb 2, nd 2, ng 2, nj 1, nz 1, k 3, t 2, l 4, m 2, n 2, s 2,
  w 2, y 1, ny 1, z 1, b 1, d 1. **Prenasal onsets also open words**: *Mbaleko, Ndozimba*.
* **Vowels**: a 5, e 3, i 4, o 3, u 4.
* **Codas**: none.
* **Shape and stress**: (C)V with prenasal onsets; penultimate stress; a few places reduplicate.
* **Forbidden**: a prenasal next to its own repeat, wu, yi, kwu, gwu, doubled w or y.
* **Places** (cap 10): ending 54 %, bare 30 %, bound prefix 12 % (*u-*, *ki-*, *ma-*, *e-* "at, in":
  *Kindembe, Umbalo*), reduplicated 4 %. Endings: *-ndu* river, *-mbe* village, *-ngo* hill, *-lela*
  field, *-mbola* market, *-ndaba* meeting place, *-ngwe* leopard rock, *-yenga* lake, *-zimba* lion
  hill, *-mbuka* crossing, *-tende* palms, *-njiri* path, *-landa* plain, *-kumbe* forge, *-ngoli*
  granary, *-mbiri* two rivers, *-wazi* clearing, *-ndeni* inner town, *-kani* spring, *-sungu*
  baobab, *-mbalu* far town, *-ndari* drum-hill, *-yumbe* rain-place, *-tolo* bend.
* **Persons**: endings -a, -e, -o, -i, -isa, -ende, -ombe, -ulu, -ika: *Kikula, Lakala, Nyengondu*.
* **Peoples**: name endings -a, -o, -i; adjective -an or -i; plural -ni: *Ndova / Ndovan / the Ndovani*.
* **God**: roots mbal ndem yel kwan zumb tob + -u -ari -ende -ondo -iwe: *Mbalu, Kwanende, Zumbari*.
* **Rulers**: *Mbaku* (drum-chief), *Ndalo* (elder of elders), *Ngozu* (lion-king).
* **Epithets**: the Rain-Caller, the Drum-Voiced, the Lion-Hearted, the Elder, the Many-Herded, the
  Forest-Born, the Peacemaker, the Ironsmith, the Honey-Tongued, the River-Wise.
* **Ships** (river canoes): *Mbeli, Ngayo, Sumbe, Ndowa* + a short word: *Ngayo Sinimbola*.
* **Drift laws**: initial k > ch, l > r, d > l between vowels, e > i, u > o, initial b > p (the prenasals
  are kept: they are the family's mark). **Sister**: Qa'tiri.
* **Samples**: places Iwingoli, Mabembuka, Yayandeni, Esingo, Binalundu, Losunjiri, Kwengule, Babanjiye, Amindari, Abozimba, Wingowazi, Lulumulo, Wamondeni, Manilanda, Linula, Nelewiye; persons Wilike, Njosimiwo, Lunzumiyo, Sikinibulu, Aleduko.

### 4.6 Thrask: the fjord tongues of the cold coasts

*Identity.* Ship-builders, tale-keepers and oath witnesses of narrow storm-coasts, valuing practical
courage, exact testimony and shelter offered before questions. A god is worthy when it stands inside the
storm with them; fear can produce obedience, never trust, while a silent god becomes one more old story
whose truth must be tested at sea. Their names are short, weathered and heavy.

* **Architecture and colour**: stone-backed timber halls with turf mantles, steep boat sheds, beacon
  frames and narrow glazed wind slits. Use storm slate `#394A55`, tar black `#24272A`, weathered wood
  `#765D45`, lichen green `#6E7654` and beacon amber `#D18B35`; daughters vary prow marks and beacon
  colour while retaining long low profiles anchored into rock.

* **Onsets**, initial: hr 2, hl 1, hv 1, thr 2, th 2, sk 3, st 2, sv 2, kn 1, gr 1, br 1, fr 1, fl 1, b 2,
  d 2, g 2, h 2, k 2, r 1, s 2, t 1, v 2, f 2, m 1, n 1; empty 15 %. Inner: d 2, g 2, k 2, l 3, m 1, n 2,
  r 3, s 2, t 2, v 2, f 1, th 2, b 1.
* **Vowels**: a 4, e 4, i 3, o 3, u 2; stressed diphthongs ei 2, au 2 (*p* 12 %). (The verifier lowered u
  from 3 and raised e from 3: "Dodurkud", "Gubunbask" and "Knurguskum" read as Korvagh.)
* **Codas**: inner r 4, l 3, n 2, s 2, k 1, g 1, d 1 (*p* 42 %); final rd 2, rn 2, ld 2, lf 1, nd 2, rk 1,
  ng 1, st 2, r 2, n 2, k 1, d 1, m 1, sk 1, rg 1, lm 1 (*p* 93 %).
* **Shape and stress**: CCVC, CVCC; initial stress; l n k t may double after the stress (*p* 12 %).
* **Forbidden**: any vowel pair but ei and au; clashing stops (dt gk kd gt ...); voiced and voiceless
  fricatives together (fv vf ...); ng before a consonant.
* **Places** (cap 10): ending 66 %, bare 34 %. Endings: *-vard* watch-hill, *-skel* shelter, *-brekk*
  slope, *-heid* heath, *-rond* ring-fort, *-skar* cleft, *-fjar* far shore, *-hvarn* hollow, *-eid*
  isthmus, *-staf* post, *-ung* meadow, *-irn* iron-place, *-osk* river-mouth, *-hald* hold-hall, *-seld*
  shieling, *-thurn* thorn-hedge, *-vang* field, *-brun* spring, *-varp* cast-net beach, *-ey* island,
  *-sund* strait, *-hlid* hillside, *-nes* headland.
* **Persons**: endings -ar, -ir, -a, -und, -eld, -run, -vi, -mar, -dis: *Skeldvi, Hrothar, Ulfdis*.
* **Peoples**: name with no ending; adjective -ic; plural -ir: *Skeld / Skeldic / the Skeldir*.
* **God**: roots hrav skeld vald throm geir ulf + -ung -ar -ir -und -adr: *Thromir, Hravung, Geirund*.
* **Rulers**: *Hersk* (war-lord), *Thrund* (thing-speaker), *Jorvald* (earth-wielder).
* **Epithets**: the Oath-Keeper, the Fair-Haired, the Wave-Breaker, the Grey-Cloaked, the Bold, the
  Ring-Giver, the Frost-Bearded, the Wolf-Friend, the Skald-Beloved, the Storm-Rider.
* **Ships** (longships): *Brimra, Hvessa, Skeldra, Ulfvar* + a short word: *Brimra Rabrekk*.
* **Drift laws**: initial hr hl hv > r l v, th > t, initial sk > sh, final d > t, ei > e, au > o.
  **Sister**: Korvagh.
* **Samples**: places Rogligelm, Gufurnast, Gafoveid, Utevey, Efiskar, Urelung, Dauteskel, Varethurn, Flauladirn, Nenuvang, Skethuter, Svigobrun, Sketikin, Vorroru, Galmuvang, Dolakheid; persons Galeladir, Dongamo, Murerin, Tenkifarn, Ovaved.

### 4.7 Pheraxi: the old imperial tongue

*Identity.* The speech of an old civic league of law courts, waterworks and archive houses, whose people
value legible institutions, argument in public and monuments repaired rather than merely inherited.
They welcome a god who accepts witness and restraint, distrust miracles that bypass consent, and treat
silence as a jurisdiction the mortal city must fill. Their names are stately, carved and many-syllabled.

* **Architecture and colour**: fitted pale stone, long water arcades, stepped forum courts, bronze doors
  and coloured aggregate floors whose diagrams double as civic records. Use marble cream `#D8CEB8`,
  porphyry `#744451`, oxidised bronze `#4F7368`, ink blue `#344D68` and ceremonial saffron `#C89431`;
  daughters alter column spacing and floor diagrams while retaining axial courts and visible water.

* **Onsets**, initial: ph 3, th 3, k 3, t 2, d 2, p 2, l 2, m 2, n 2, s 2, r 1, kl 1, kr 1, pr 2, tr 1,
  str 1, pl 1, h 1; empty 18 %. Inner: ph 2, th 3, k 2, t 2, d 2, p 1, l 3, m 2, n 3, s 2, x 1, r 3, kr 1,
  tr 1; an empty inner onset makes a hiatus (*p* 4 %), only ea eo ia io.
* **Vowels**: a 4, e 4, i 3, o 4, u 1; stressed diphthongs ai 1, ei 2, oi 1, eu 2, ou 1 (*p* 15 %).
* **Codas**: inner s 3, n 3, r 2, l 2 (*p* 28 %); final s 5, n 4, x 1, r 1 (*p* 45 %).
* **Shape and stress**: (C)(C)V(C); antepenultimate stress; s l n r may double after it (*p* 12 %):
  *Thes-sa-li-on*.
* **Forbidden**: x next to s, m, n, r, l or a stop, x at a word's end after a vowel other than in -ax
  -ex -ix -ox; ph or th doubled or touching; most vowel pairs; liquid and nasal clashes (nr ln ml ...);
  nasal + stop clusters across syllables.
* **Places** (cap 12): ending 74 %, bare 26 %. Endings: *-ion* temple, *-eum* hall of learning, *-axis*
  fortress, *-onos* hill, *-eira* spring, *-anthe* garden, *-ostra* harbour, *-alos* shore, *-imos*
  market, *-erax* citadel, *-enth* old town, *-ophor* gate, *-eron* sanctuary, *-phane* shrine of
  light, *-thalos* sea-town, *-mene* precinct.
* **Persons**: endings -os, -ia, -on, -ides, -ax, -eus, -ra, -ine, -is: *Kesilexos, Elaphekos,
  Metrerakis*.
* **Peoples**: name with no ending; adjective -ian or -ine; plural -oi: *Pherax / Pheraxian / the
  Pheraxoi*.
* **God**: roots kryth elax onth istr thal + -ion -eus -ara -on -ios: *Elaxeus, Onthios, Krythion*.
* **Rulers**: *Ostrarch* (lord of harbours), *Thalarch* (lord of the sea), *Exarion* (first citizen).
* **Epithets**: the Lawgiver, the Builder, the Philosopher, the Victorious, the Golden-Tongued, the Just,
  the Restorer, the Saviour of Cities, the Learned, the Purple-Born.
* **Ships** (oared state galleys): *Phaelon, Erathe, Ostrine, Dorantha* + a short word: *Ostrine Kapis*.
* **Drift laws**: ph > f, th > t, x > s, final s lost, ei > e, t > d between vowels (a Pheraxi daughter
  drifts toward its sister Velessan: *-axis > -asi*, *-alos > -alo*). **Sister**: Velessan.
* **Samples**: places Thosimophor, Hollalion, Tronosko, Klonnonimeum, Lomotraxion, Himourophor, Uxiostra, Kolopon, Ailalos, Phixanupenth, Hixola, Halonainaxis, Troloxomene, Kontrosna, Lomotreumene, Koskonsin; persons Rolani, Hopoxas, Iskilomia, Toponamix, Olatu.

### 4.8 Curated authoring banks (15 of every requested kind)

These are reference banks for writers, concept artists and table implementers. They instantiate the
patterns above; they are not a promise that a particular seed emits them in this order. **God epithets**
below are the culture's translated titles for the player-god and are separate from the mortal ruler
bynames returned by `names::epithet`. In code, keep the translated meaning attached to the culture while
allowing the culture's own proper name for the god to drift with its language.

#### Velessan banks

* **Places (15):** Lanamavanne; Tenamimorra; Damigerivia; Lulosogile; Merigeganne; Lefemafanto;
  Trenefa; Limanuganto; Tapefu; Damasagorra; Dovra Besivia; Fofalaganto; Sillibamu; Masavovanne;
  Lusa Rifivia.
* **People (15):** Marnofuseo; Trerogo; Bosavonia; Garoreneo; Bavanoma; Teodo; Lorina; Silvano;
  Vessina; Tavereo; Moradia; Lusenetto; Dorello; Amenira; Vireano.
* **Rulers (15):** Ossaro Marnofuseo; Tavenne Lorina; Vessaro Teodo; Doravo Silvano; Ossaro Vessina;
  Tavenne Moradia; Vessaro Tavereo; Doravo Lusenetto; Ossaro Dorello; Tavenne Amenira; Vessaro Vireano;
  Doravo Trerogo; Ossaro Bosavonia; Tavenne Garoreneo; Vessaro Bavanoma.
* **God epithets (15):** the Tide-Born; the Fair-Sailing; the Vine-Crowned; the Golden-Voiced; the
  Gentle; the Far-Faring; the Lamp-Bearer; the Harbour-Builder; the Laughing; the Sea-Wed; the Wise; the
  Pious; the Twice-Crowned; the Peaceful; the Elder.
* **Ships (15):** Ondessa Larivetta; Lusa Tenamira; Silvara Mavarra; Morenna Besivia; Vellara Damasino;
  Lusa Rifivia; Ondessa Tavorella; Silvara Lusanora; Morenna Viretta; Vellara Tenamino; Lusa Dovraessa;
  Ondessa Meriganne; Silvara Masavaro; Morenna Sillibora; Vellara Lanamile.

#### Korvagh banks

* **Places (15):** Kruk-Ukokh; Huri-Zakumdrun; Moriboz; Zhalurak; Dragazoz; Gigonmor; Higidzun;
  Vrogturun; Nodevulum; Bozd-Obad; Krarunan; Sigtodrun; Zubazog; Zhodmugul; Vumakrak.
* **People (15):** Zotorerur; Omorogo; Ugalug; Grudmuter; Agotebar; Bograt; Uzhak; Drukmar; Borag;
  Kharuk; Zhavor; Grudaz; Torum; Vezod; Mordag.
* **Rulers (15):** Ughtar Zotorerur; Dravokh Omorogo; Zhogan Ugalug; Borukh Grudmuter; Ughtar Agotebar;
  Dravokh Bograt; Zhogan Uzhak; Borukh Drukmar; Ughtar Borag; Dravokh Kharuk; Zhogan Zhavor; Borukh
  Grudaz; Ughtar Torum; Dravokh Vezod; Zhogan Mordag.
* **God epithets (15):** the Iron-Handed; the Unbowed; the Horse-Lord; the Stone-Breaker; the Grim; the
  Red-Axe; the Far-Rider; the Hammer; the Wolf-Eyed; the Thunder-Voiced; the Great; the Bold; the
  Twice-Crowned; the Pious; the Unlucky.
* **Ships (15):** Ughrak Mibezosk; Morkh Zhalurak; Zagra Durvakh; Durvakh Vumakrak; Ughrak Zubazog;
  Morkh Dragazoz; Zagra Krarunan; Durvakh Higidzun; Ughrak Gigonmor; Morkh Moriboz; Zagra Zhodmugul;
  Durvakh Sigtodrun; Ughrak Nodevulum; Morkh Vrogturun; Zagra Ghurubu.

#### Aavik banks

* **Places (15):** Lorilirosti; Alasalkuusi; Vultamorosti; Santollinus; Ippajamakki; Lemitekin;
  Sotovavuoto; Avasolis; Antapasulmi; Miikelerke; Mukkolurosti; Sinelkeni; Molahotvanto; Hamivalkaisu;
  Massoranummi.
* **People (15):** Haimopuna; Vorosi; Inneltes; Mivissive; Seilmivi; Lunumavo; Huppasoni; Vekiminiri;
  Kaarikki; Tuovari; Sieritti; Halvimo; Ervije; Kuuvotti; Aarniri.
* **Rulers (15):** Vaalto Haimopuna; Kuura Vorosi; Hallava Inneltes; Vaalto Mivissive; Kuura Seilmivi;
  Hallava Lunumavo; Vaalto Huppasoni; Kuura Vekiminiri; Hallava Kaarikki; Vaalto Tuovari; Kuura Sieritti;
  Hallava Halvimo; Vaalto Ervije; Kuura Kuuvotti; Hallava Aarniri.
* **God epithets (15):** the Frost-Born; the Patient; the Lake-Singer; the Birch-Crowned; the
  Long-Wintered; the Knot-Wise; the Quiet; the Far-Hearing; the Trail-Herd; the Snow-Eyed; the Elder; the
  Silent; the Pious; the Peaceful; the Old.
* **Ships (15):** Ahvo Neikinimmi; Kuulas Lorilirosti; Raavi Antapasulmi; Sarka Molahotvanto; Ahvo
  Ippajamakki; Kuulas Sotovavuoto; Raavi Massoranummi; Sarka Hamivalkaisu; Ahvo Mukkolurosti; Kuulas
  Alasalkuusi; Raavi Avasolis; Sarka Lemitekin; Ahvo Sinelkeni; Kuulas Vultamorosti; Raavi Miikelerke.

#### Qa'tiri banks

* **Places (15):** Mahz Nidni; Jimisunab; Khiduqash; Hazkhizi; Idaraqar; Tiru Yarurad; Qasu Sizurad;
  Akhahiyun; Jirusu; Yahsa'udar; Nadirub; Ishad Sabamil; Jikha'abir; Jashimi; Zira'udar.
* **People (15):** Nadakha; Ta'amfuzia; Zusilib; Amuliza'an; Baquhaar; Hilamiq; Zahirun; Qashir;
  Shudima; Nurad; Haziit; Miruz; Saliash; Dumarun; Jariqia.
* **Rulers (15):** Qadun Nadakha; Ishar Ta'amfuzia; Zahar Zusilib; Hamzir Amuliza'an; Qadun Baquhaar;
  Ishar Hilamiq; Zahar Zahirun; Hamzir Qashir; Qadun Shudima; Ishar Nurad; Zahar Haziit; Hamzir Miruz;
  Qadun Saliash; Ishar Dumarun; Zahar Jariqia.
* **God epithets (15):** the Star-Reader; the Patient; the Well-Finder; the Veiled; the Thrice-Wise; the
  Sand-Walker; the Generous; the Silent; the Lantern of the Dunes; the Route-Keeper; the Wise; the Pious;
  the Elder; the Peaceful; the Unlucky.
* **Ships (15):** Shubara Turaz; Iyaru Nadirub; Nubira Khiduqash; Qamira Zira'udar; Shubara Jashimi;
  Iyaru Yahsa'udar; Nubira Hazkhizi; Qamira Jirusu; Shubara Akhahiyun; Iyaru Sabamil; Nubira Idaraqar;
  Qamira Jimisunab; Shubara Sizurad; Iyaru Yarurad; Nubira Jikha'abir.

#### Hanuwe banks

* **Places (15):** Kawamena; Tokapoki; Hukalana; Poponua; Kopihume; Numohoka; Tangohiwa; Pokohiwa;
  Nanoweko; Mingilehu; Monimone; Pumomena; Wamolehu; Kenumone; Wenohiwa.
* **People (15):** Telole; Talilo; Moamiahi; Monemi; Nipui; Talekane; Opelu; Hinganao; Kawono; Lehua;
  Tamewa; Nohiele; Wapono; Koluahi; Pakeo.
* **Rulers (15):** Hekau Telole; Pokane Talilo; Lewahi Moamiahi; Hekau Monemi; Pokane Nipui; Lewahi
  Talekane; Hekau Opelu; Pokane Hinganao; Lewahi Kawono; Hekau Lehua; Pokane Tamewa; Lewahi Nohiele;
  Hekau Wapono; Pokane Koluahi; Lewahi Pakeo.
* **God epithets (15):** the Wave-Walker; the Star-Steerer; the Reef-Born; the Many-Islanded; the Child
  of Fire; the Long-Voyager; the Shell-Crowned; the Laughing; the Wind-Reader; the Net-Mender; the Wise;
  the Younger; the Peaceful; the Twice-Crowned; the Pious.
* **Ships (15):** Kalowa Lotoa; Hinemo Kawamena; Tewaha Tokapoki; Maheke Tangohiwa; Kalowa Monimone;
  Hinemo Mingilehu; Tewaha Kenumone; Maheke Wamolehu; Kalowa Hukalana; Hinemo Pokohiwa; Tewaha
  Kopihume; Maheke Wenohiwa; Kalowa Nanoweko; Hinemo Pumomena; Tewaha Numohoka.

#### Mbendi banks

* **Places (15):** Iwingoli; Mabembuka; Yayandeni; Esingo; Binalundu; Losunjiri; Kwengule; Babanjiye;
  Amindari; Abozimba; Wingowazi; Lulumulo; Wamondeni; Manilanda; Linula.
* **People (15):** Wilike; Njosimiwo; Lunzumiyo; Sikinibulu; Aleduko; Kikula; Lakala; Nyengondu;
  Mbalisa; Ndewombe; Kwanika; Zumbulu; Tobende; Ngayisa; Mandelo.
* **Rulers (15):** Mbaku Wilike; Ndalo Njosimiwo; Ngozu Lunzumiyo; Mbaku Sikinibulu; Ndalo Aleduko;
  Ngozu Kikula; Mbaku Lakala; Ndalo Nyengondu; Ngozu Mbalisa; Mbaku Ndewombe; Ndalo Kwanika; Ngozu
  Zumbulu; Mbaku Tobende; Ndalo Ngayisa; Ngozu Mandelo.
* **God epithets (15):** the Rain-Caller; the Drum-Voiced; the Lion-Hearted; the Elder; the Many-Herded;
  the Forest-Born; the Peacemaker; the Ironsmith; the Honey-Tongued; the River-Wise; the Builder; the
  Pious; the Great; the Silent; the Twice-Crowned.
* **Ships (15):** Mbeli Sinimbola; Ngayo Iwingoli; Sumbe Mabembuka; Ndowa Binalundu; Mbeli Losunjiri;
  Ngayo Abozimba; Sumbe Wingowazi; Ndowa Manilanda; Mbeli Amindari; Ngayo Yayandeni; Sumbe Wamondeni;
  Ndowa Kwengule; Mbeli Linula; Ngayo Esingo; Sumbe Lulumulo.

#### Thrask banks

* **Places (15):** Rogligelm; Gufurnast; Gafoveid; Utevey; Efiskar; Urelung; Dauteskel; Varethurn;
  Flauladirn; Nenuvang; Skethuter; Svigobrun; Sketikin; Vorroru; Galmuvang.
* **People (15):** Galeladir; Dongamo; Murerin; Tenkifarn; Ovaved; Skeldvi; Hrothar; Ulfdis; Thrundar;
  Hveseld; Brimrun; Jorvi; Geirmar; Valdund; Frekeld.
* **Rulers (15):** Hersk Galeladir; Thrund Dongamo; Jorvald Murerin; Hersk Tenkifarn; Thrund Ovaved;
  Jorvald Skeldvi; Hersk Hrothar; Thrund Ulfdis; Jorvald Thrundar; Hersk Hveseld; Thrund Brimrun;
  Jorvald Jorvi; Hersk Geirmar; Thrund Valdund; Jorvald Frekeld.
* **God epithets (15):** the Oath-Keeper; the Fair-Haired; the Wave-Breaker; the Grey-Cloaked; the Bold;
  the Ring-Giver; the Frost-Bearded; the Wolf-Friend; the Tale-Beloved; the Storm-Rider; the Great; the
  Elder; the Unlucky; the Twice-Crowned; the Old.
* **Ships (15):** Brimra Rabrekk; Hvessa Gafoveid; Skeldra Dauteskel; Ulfvar Svigobrun; Brimra
  Varethurn; Hvessa Nenuvang; Skeldra Flauladirn; Ulfvar Galmuvang; Brimra Efiskar; Hvessa Urelung;
  Skeldra Gufurnast; Ulfvar Utevey; Brimra Skethuter; Hvessa Vorroru; Skeldra Rogligelm.

#### Pheraxi banks

* **Places (15):** Thosimophor; Hollalion; Tronosko; Klonnonimeum; Lomotraxion; Himourophor; Uxiostra;
  Kolopon; Ailalos; Phixanupenth; Hixola; Halonainaxis; Troloxomene; Kontrosna; Lomotreumene.
* **People (15):** Rolani; Hopoxas; Iskilomia; Toponamix; Olatu; Kesilexos; Elaphekos; Metrerakis;
  Thalonia; Thesira; Pherides; Kralon; Doreus; Stramine; Pelaxia.
* **Rulers (15):** Ostrarch Rolani; Thalarch Hopoxas; Exarion Iskilomia; Ostrarch Toponamix; Thalarch
  Olatu; Exarion Kesilexos; Ostrarch Elaphekos; Thalarch Metrerakis; Exarion Thalonia; Ostrarch Thesira;
  Thalarch Pherides; Exarion Kralon; Ostrarch Doreus; Thalarch Stramine; Exarion Pelaxia.
* **God epithets (15):** the Lawgiver; the Builder; the Philosopher; the Victorious; the Golden-Tongued;
  the Just; the Restorer; the Saviour of Cities; the Learned; the Purple-Born; the Wise; the Great; the
  Pious; the Peaceful; the Twice-Crowned.
* **Ships (15):** Phaelon Kapis; Erathe Thosimophor; Ostrine Uxiostra; Dorantha Ailalos; Phaelon
  Hollalion; Erathe Lomotraxion; Ostrine Phixanupenth; Dorantha Himourophor; Phaelon Kolopon; Erathe
  Halonainaxis; Ostrine Troloxomene; Dorantha Kontrosna; Phaelon Tronosko; Erathe Lomotreumene; Ostrine
  Klonnonimeum.

## 5. Sound laws

| id | law | where | used by |
|---|---|---|---|
| 0 | k > h | word start | (spare) |
| 1 | t > d | between vowels | Velessan, Qa'tiri, Pheraxi |
| 2 | p > f | word start | (spare) |
| 3 | s > h | word start | Aavik |
| 4 | e > i | everywhere | Velessan, Aavik, Hanuwe, Mbendi |
| 5 | b > v | between vowels | Velessan, Qa'tiri |
| 6 | g > gh | between vowels | Korvagh |
| 7 | kh > h | everywhere | Qa'tiri |
| 8 | th > t | everywhere | Thrask, Pheraxi |
| 9 | ph > f | everywhere | Pheraxi |
| 10 | x > s | everywhere | Pheraxi |
| 11 | u > o | everywhere | Velessan, Korvagh, Hanuwe, Mbendi |
| 12 | o > u | everywhere (never with 11) | Korvagh |
| 13 | ss > s | everywhere | Velessan |
| 14 | aa ee ii oo uu > a e i o u | everywhere | Aavik, Qa'tiri |
| 15 | d > t | word end | Korvagh, Thrask |
| 16 | s > r | between vowels | Velessan |
| 17 | mb nd > m n | everywhere | (spare: would erase the Mbendi mark) |
| 18 | w > v | everywhere | (spare: would erase the Hanuwe mark) |
| 19 | hr hl hv > r l v | word start | Thrask |
| 20 | q > k | everywhere | Qa'tiri |
| 21 | sh > s | everywhere | Qa'tiri |
| 22 | ei > e | everywhere | Aavik, Thrask, Pheraxi |
| 23 | au > o | everywhere | Thrask |
| 24 | t > k | everywhere | Hanuwe |
| 25 | l > r | everywhere | Hanuwe, Mbendi |
| 26 | ng > n | everywhere | Hanuwe |
| 27 | z > zh | everywhere | Korvagh |
| 28 | kk tt pp > k t p | everywhere | Aavik |
| 29 | oo > uo, ee > ie | everywhere | Aavik |
| 30 | a > e | word end | Velessan |
| 31 | k > ch | word start | Mbendi |
| 32 | d > l | between vowels | Mbendi |
| 33 | sk > sh | word start | Thrask |
| 34 | s > (lost) | word end | Pheraxi |
| 35 | b > p | word start | Korvagh, Mbendi |

A single-letter law never touches a letter that belongs to a digraph (kh gh zh sh th ph ch, mb nd ng nj nz
ny, kw gw).

## 6. Determinism

Names draw only from the `Rng` their caller passes, and every caller in the simulation passes its own fork
(`settle:<id>`, `culture:<id>`, `prophet:<culture>:<year>`, `wonder:<id>`, `chron:god:<id>`,
`sim:planet`), so the world's own stream and every other part of the state are untouched. Proof:
`simrun --hash` on two scratch copies of the sources (`Unreal/SimTests/build/names-det/old` and `new`,
identical except Names.cpp and Names.h), with a fingerprint that blanks every name string (towns, peoples,
adjectives, plurals, languages, gods, prophets, chronicle texts, the world's name) before hashing. Seeds
1 to 4 × passive, kind, cruel and chaotic, 4,000 years: **16 of 16 fingerprints identical**. The chronicle's
structure (entry ids, years, cells, cultures, weights and kinds of all 1,490 to 2,324 entries) is
identical too, as are results, events and peaks. Only the words changed.

Verifier's re-run (scratch copies `Unreal/SimTests/build/names-verify/old` and `new`, identical except
Names.cpp and Names.h, the original Names taken from an untouched lane copy; own simrun patch that also
prints the world rng state): seeds 1 to 8 × passive, kind, cruel and chaotic, 4,000 years: **32 of 32 runs
identical** in every line but the names and timings (nameless fingerprint, rng state, result, events,
peaks, awe). With the newer Chronicle.cpp of the chronicle-budget work (in progress during the check) the
final Names gave the same 32 results twice in a row. (With that Chronicle.cpp the ORIGINAL Names gave a
different chronicle length on each run, 159 / 159 / 291 / 170 entries for seed 1 passive: a matter for the
chronicle engineer, not for this module.)

## 7. Verification numbers (2026-11-19, after the verifier's fixes)

* `run.ps1 names`: 42 passed, 0 failed, 0 warnings; `run.ps1 link` 3 passed; `run.ps1 all` 734 passed,
  0 failed.
* 300 names per family per language (3 languages per family): places 280 to 299 distinct, persons and
  peoples 293 to 300; every name coded, sound and clean.
* 200 random languages × 300 names in each category: the least varied is 91.3 % distinct (contract 90 %).
* Family recognition by letter trigrams: 5 place names name the family 99.6 % of the time, 1 name 81.4 %.
* Blocklist: a verifier's own list (profanity and slurs in about twenty languages, ~450 real places,
  ~200 deities, ~1,400 English words, given names) over 166,848 names of 96 root languages and four
  generations of daughters each: 0 profanity or English-word hits after the fixes (before: 90 "nazi",
  53 "Lose ...", 30 "pedo", 22 "semen", 18 "titt" and about 100 English clan or second words). Left: a few
  obscure pieces (Egyptian god "Shu" in "Shu'anamil", Czech "kunda" in "Sakundari", Arabic given names
  between glottal stops, "Ama'arun").
* Speed (best of three; the machine was loaded by Unreal shots): 10,000 names 48 to 105 ms, root
  language 75 to 113 ms, 40th-generation daughter 74 to 108 ms (budget 150 ms). Before the verifier's
  fixes single runs read 125 to 267 ms and failed. The fixes: the slot's scratch lists are no longer
  zeroed on every call, records parse in one pass, and person names are drawn with no more syllables than
  fit 10 letters (a third of Korvagh's tries had been thrown away).
* Velessan places average 8.9 letters (9.6 before), 7 % of 11 letters or more (38 %).

## 8. For other modules

* `names::ruler`, `names::epithet` and `names::ship` (Names.h) are new and not yet called. A chronicle
  line about a ruler, a fleet or a caravan can use them. They take the culture's `lang` and a forked rng
  like the others.
* People names are at most 10 letters, places at most 14 characters, plurals at most 14 with "the ".
* The god's word is fixed per language. A culture's `godName` is set once at its birth, so a daughter's
  drifted god name shows up only in new cultures.

## 9. Final authoring guardrails: pronounceable, original and distinct

These families are setting inventions, not stand-ins for real peoples: biome, values, architecture and
belief may recombine independently, and the lexicons contain no borrowed real-language word lists. The
English glosses explain function; they are not etymologies. Before accepting any added name:

1. Say it once from its spelling. Reject it if stress or a digraph needs an explanation beyond the
   family's rules, or if it breaks the 3–14-character and forbidden-cluster limits.
2. Preserve the family's **place-name signature**: Velessan vowel endings; Korvagh hard codas and
   occasional hyphens; Aavik harmony and length; Qa'tiri `a/i/u` plus rare apostrophes; Hanuwe open
   syllables; Mbendi prenasal openings; Thrask heavy clusters; Pheraxi stately multisyllables.
3. Compare a candidate against the other seven families before comparing it within its own. A place
   that could belong equally to two unrelated families needs a stronger signature ending or onset.
4. Apply daughter laws regularly to stored roots, titles, generics and the god's proper name. Do not
   manufacture difference with random respelling; kinship should remain audible after four generations.
5. Run the profanity, real-place, deity, famous-fiction and near-twin filters last. A clean result is a
   requirement, not permission to weaken the family's phonology.
