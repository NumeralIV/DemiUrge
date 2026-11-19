// DEMIURGE — js/sim/names.js : DM.Names (SPEC-SIM §3.1). Procedural languages and the names made in them.
//
//   Names.language(rng, parent = null) → lang     a language as plain JSON: sound inventories, syllable shapes,
//                                                  stress, favourite endings, a small lexicon of roots. With a parent it
//                                                  is a daughter language: the parent's roots, endings and god-name after
//                                                  a few regular sound shifts, plus some habits of its own.
//   Names.word(lang, rng, minSyl = 1, maxSyl = 3) → lowercase word (3 letters or more: a word too short to stand
//                              alone is rolled again, in the end one syllable longer)
//   Names.place(lang, rng)   → 'Tel Asha', 'Vornhal', 'Qe-Luun'
//   Names.person(lang, rng)  → 'Umari', 'Dovak'. Now and then a name is built on the god's own name, as people do.
//   Names.culture(lang, rng) → { name, adj, plural }        'Umar', 'Umaran', 'the Umari'
//   Names.god(lang, rng)     → the language's own word for the player-god. It is fixed in the language (rng is not
//                              used) and passes through the sound shifts, so related cultures call the god by
//                              related names. No town, person or people is ever given exactly this name.
//   Names.planet(rng)        → a default world name
//   Names.cap(s)             → s with its first letter in upper case
//   Extras (not in the spec): Names.KINDS, Names.ok(name), Names.roots(lang), Names.cognates(parent, daughter),
//   Names.describe(lang), Names.stats. A third argument to Names.language forces one of Names.KINDS for a new root
//   language; a third argument to Names.culture is an array of culture names already taken (never returned).
//
// Every name is 3–14 characters (places, people and peoples: 4 or more; plural: 'the ' + at most 10), has no run
// of four consonant letters or three identical letters (counted strictly: across hyphens and spaces, with y, w
// and accented vowels all counting as consonants), is checked against a blocklist, and is never a common English
// word or a famous name of ours. No Math.pow / log / sin anywhere, so results are identical on every engine.
// A missing or malformed lang never throws: a built-in language answers instead.
(function (DM) {
  'use strict';

  const Names = (DM.Names = {});
  const TUNE = (DM.TUNE = DM.TUNE || {});
  TUNE.names = {
    minLen: 3, maxLen: 14,   // every name
    placeMin: 4, personMin: 4, cultMin: 4,   // shortest place, personal and culture name (a Names.word may have three letters)
    tries: 16,               // re-rolls before the plain fallback name
    roots: 56,               // recurring stems kept by a language
    newShifts: 4,            // sound shifts a daughter language adds (at most)
    maxShifts: 24,           // longest chain of shifts a language may carry
    changed: 0.62,           // stop adding shifts once this share of the parent's words look different
    drift: 0.4,              // largest mean edit distance (0..1) between a daughter's words and its parent's
    replace: 0.12,           // share of roots a daughter replaces with new coinages
    minCons: 8,              // no shift may merge a language's consonants down to fewer than this …
    rootLen: 5.6,            // … or lengthen its words once they average more than this many letters
    english: true,           // re-roll names that are common English words ('Mike', 'Belt', 'The')
    word: [11, 4],           // longest single word of a place name: letters, vowel groups (a language's `mx` overrides)
    personLen: 10,           // longest single word of a personal name
    sample: 300,             // names drawn to measure how varied a new language's names are …
    distinct: 0.96,          // … and the share of them that must be different, for places and for people
    rounds: 4,               // how many times a new language may be loosened to get there
  };

  // ------------------------------------------------------------------ sounds
  // Words are strings in a one-character-per-sound alphabet (all ASCII, so languages serialize compactly):
  //   vowels      a e i o u, long A E I O U
  //   consonants  p b t d k g q ' (glottal stop) f v s z h m n l r w y, and
  //               T th   D dh   S sh   Z zh   x kh   G gh   c ts   C ch   j (as in "jar")   J dz   L tl   N ng   Y ny
  //               H ll   R rh   P p'   1 t'   K k'   Q q'   2 ch' (ejectives)   3 ph   4 th   5 kh   6 bh   7 dh   8 gh
  //               (aspirates)   W kw   9 gw
  const VOW = 1, LONG = 2, FRONT = 4, CONS = 8, NASAL = 16;
  const CLS = new Uint8Array(128);
  function mark(chars, flag) { for (let i = 0; i < chars.length; i++) CLS[chars.charCodeAt(i)] |= flag; }
  mark('aeiou', VOW); mark('AEIOU', VOW | LONG); mark('eiEI', FRONT);
  mark("pbtdkgq'fvszhTDSZxGcCjJLmnNYlrHRwyP1KQ2345678W9", CONS); mark('mnNY', NASAL);
  const SBASE = 'aeiou', LBASE = 'AEIOU';

  // default spellings of the sounds that are not their own letter
  const SPELL = {
    T: 'th', D: 'dh', S: 'sh', Z: 'zh', x: 'kh', G: 'gh', c: 'ts', C: 'ch', J: 'dz', L: 'tl', N: 'ng', Y: 'ny', H: 'hl', R: 'rh',
    P: "p'", 1: "t'", K: "k'", Q: "q'", 2: "ch'", 3: 'ph', 4: 'th', 5: 'kh', 6: 'bh', 7: 'dh', 8: 'gh', W: 'kw', 9: 'gw',
  };
  // other ways a language may come to write a sound (daughters change their spelling habits)
  const ALT = {
    x: ['kh', 'ch', 'x', 'h'], S: ['sh', 'x', 'š'], C: ['ch', 'č', 'tch'], Z: ['zh', 'ž'], c: ['ts', 'tz', 'c'],
    j: ['j', 'dj'], y: ['y', 'j'], w: ['w', 'v'], D: ['dh', 'dd'], Y: ['ny', 'ñ', 'gn'], W: ['kw', 'qu'], H: ['hl', 'll'], f: ['f', 'ph'],
  };
  // how a long vowel is written
  const LV = {
    dbl: ['aa', 'ee', 'ii', 'oo', 'uu'],
    acute: ['á', 'é', 'í', 'ó', 'ú'],
    circ: ['â', 'ê', 'î', 'ô', 'û'],
    macron: ['ā', 'ē', 'ī', 'ō', 'ū'],
    none: ['a', 'e', 'i', 'o', 'u'],
  };

  // ------------------------------------------------------------------ checking a finished name
  // PV[code] = 1 for the plain vowel letters. FOLD maps every accented letter this file can produce to its base letter.
  const PV = new Uint8Array(128);
  for (const ch of 'aeiouAEIOU') PV[ch.charCodeAt(0)] = 1;
  const FOLD = {};
  (function () {
    const rows = [
      ['a', 'áâāÁÂĀ'], ['e', 'éêēëÉÊĒË'], ['i', 'íîīÍÎĪ'],
      ['o', 'óôōÓÔŌ'], ['u', 'úûūÚÛŪ'],
      ['n', 'ñÑ'], ['s', 'šŠşŞ'], ['c', 'čČçÇ'], ['z', 'žŽ'],
    ];
    for (const r of rows) for (let i = 0; i < r[1].length; i++) FOLD[r[1].charCodeAt(i)] = r[0].charCodeAt(0);
  })();

  // obvious English profanity, slurs and a few words nobody wants on a map: anywhere in the name …
  const BAD_ANY = new RegExp('fuck|fuk|shit|cunt|cock|dick|piss|slut|whore|bitch|twat|wank|nigg|nigr|fagg|rape|anus|arse|penis|vagin|' +
    'porn|nazi|turd|crap|puss|boob|dildo|semen|sperm|bastard|kike|pedo|jizz|poop|butt|hitler|dyke|tits|homo|sex|cum[sz]|ass[eh]|' +
    'prick|negro|retard|kaka|caca|chink|phuk|kunt|biatch|kill|molest|uterus|labia|vulva|lesb|enema|boner|sadis|hentai|bukkake|' +
    // … and what nobody should find on their world by accident: the sacred and the infamous of ours, and mock-language pairs
    'allah|jesus|christ|yahweh|jehova|buddha|muham|moham|islam|muslim|quran|koran|torah|jihad|satan|lucifer|taliban|qaeda|stalin|' +
    'ch[io]ngch[aio]ng|pingpong|dingdong|kingkong|hongkong');
  // … or as a whole word
  const BAD_WORD = new RegExp('^(ass|cum|tit|fag|poo|pee|wee|die|dead|kill|hell|damn|spic|coon|gook|chink|anal|suck|slag|pig|hag|' +
    'bum|bra|gay|god|devil|satan|jew|pimp|dung|fart|puke|vomit|urine|idiot|moron|loser|kok|kock|kox|dik|dic|pis|kum|shat|teat|' +
    'puta|puto|wang|dong|poon|spaz|tard|perv|sodom|pube|nob|bung|snot|wuss|twit|git|dolt|bimbo|skank|ho|hos|nig|nip|jap|kyke|fuc|fuq|' +
    'phuc|kunt|bich|hoar|isis|hamas|kkk|sieg|heil|wop|dago|wog|yid|shag|thong|nuke|loin|groin|pubic|scat|smut|lech|orgy|tush|' +
    // … and the best-known oaths of the tongues these languages lean toward
    'baka|kuso|mierda|culo|pene|verga|polla|merda|merde|cazzo|fica|figa|suka|blyad|pizda|govno|mudak|zhopa|chut|lund|gand|randi|' +
    'harami|khara|zib|orospu|yarak|amcik|perse|paska|vittu|pillu|kusi|kyrpa)$');

  // Structure and decency of a finished name: length, a vowel in every word, no word of a single letter beside
  // others, no run of four consonant letters or three identical letters, nothing from the blocklists.
  let OKF = '';                                           // the name okName last accepted, folded to plain lowercase letters, its words separated by spaces
  function okName(s, lo, hi) {
    const n = s.length;
    if (n < lo || n > hi) return false;
    let run = 0, vrun = 0, same = 0, prev = -1, pbase = -1, pacc = false, partV = false, anyV = false, folded = '', part = '', all = '';
    for (let i = 0; i < n; i++) {
      const ch = s.charCodeAt(i);
      if (ch === 32 || ch === 45) {                       // space or hyphen: a new word; letter runs carry across it
        if (i === 0 || i === n - 1 || !partV || part.length < 2 || BAD_WORD.test(part)) return false;
        partV = false; vrun = 0; pbase = -1; all += part + ' '; part = '';
        continue;
      }
      if (ch === 39) {                                    // apostrophe
        if (i === 0 || i === n - 1) return false;
        vrun = 0; pbase = -1;
        continue;
      }
      let lc = ch, base;
      if (ch < 128) { if (ch >= 65 && ch <= 90) lc = ch + 32; base = lc; if (lc < 97 || lc > 122) return false; }
      else { base = FOLD[ch]; if (!base) return false; lc = ch | 0x10000; }
      if (ch < 128 && PV[ch]) { run = 0; vrun++; partV = anyV = true; }
      else if (ch >= 128 && PV[base]) { run++; vrun++; partV = anyV = true; }      // an accented vowel still counts toward the run
      else { run++; vrun = 0; }
      if (run > 3 || vrun > 3) return false;
      if (base === pbase && PV[base] && (ch >= 128 || pacc)) return false;          // a marked vowel never stands beside the same vowel ('Ōo')
      pbase = base; pacc = ch >= 128;
      // identical letters (an accented letter equals only itself; upper and lower case of an accented letter differ, which is fine)
      if (lc === prev) { if (++same > 2) return false; } else { same = 1; prev = lc; }
      const b = String.fromCharCode(base);
      folded += b; part += b;
    }
    if (!anyV || !partV || (all !== '' && part.length < 2)) return false;
    if (BAD_ANY.test(folded) || BAD_WORD.test(part)) return false;
    OKF = all + part;
    return true;
  }

  // Common English words. A name that is one of them, or is made only of them ('She Ran'), is re-rolled: nothing
  // breaks the fiction faster than a village called Mike. (The longer words are those the generators were seen to hit.)
  const ENGLISH =
    'ad al am an as at be by da de di do du el go he hi if in is it la le lo me my no of oh on or ox qi ra so to ul up ur us we wu abe abu ace act ada add ' +
    'ado age ago aid ail aim air ale ali all amy ana and ann ant any ape apt arc are ark arm art ash ask ate ava awe axe aye bad bag bai ban bar bat bay ' +
    'bed bee beg ben bet bid big bin bit boa bob bog boo bow box boy bud bug bum bun bus but buy bye cab can cap car cat chu cod cog con coo cop cot cow ' +
    'coy cry cub cud cue cup cut dab dad dai dam dan das day del den der des dew did die dig dim din dip doe dog don dot dry dub dud due dug duo dye ear ' +
    'eat eel egg ego elf elk elm emu end ent eon era eru eva eve ewe eye fad fan far fat fax fed fee fen few fig fin fir fit fix flu fly foe fog for fox ' +
    'fry fun fur gag gap gas geb gel gem get gig gin gnu goa gob got gum gun gus gut guy gym had hag hal ham han has hat hay hel hem hen her hew hex hey ' +
    'hid him hip his hit hob hod hoe hog hop hot how hub hue hug hui hum hun hut ian ibn ibo ice icy ida ill imp ink inn ion ira ire irk its ivy iwi jab ' +
    'jag jah jam jan jar jaw jay jet jig jim job joe jog jon jot joy jug jut kai keg ken key kia kid kim kin kit lab lad lag lao lap las law lax lay lea ' +
    'led lee leg leo les let lid lie lip lit liz lob log lop los lot lou low lug mad mae man map mar mat maw max may mel men met mia mid mix mob mom mon ' +
    'mop mow mud mug mum nab nag nan nap nay ned net new nil nip nit nod nor not now nub nun nut oaf oak oar oat odd ode off oft oil old one opt ora orb ' +
    'orc ore ork our out ova owe owl own pad pal pam pan par pat paw pay pea peg pen pep per pet pew pie pig pin pit ply pod poi pop pot pro pry pub pug ' +
    'pun pup pur pus put qin rag ram ran rap rat raw ray red ref rex rib rid rig rim rio rip rob rod roe ron rot row roy rub rue rug rum run rus rut rye ' +
    'sac sad sag sal sam san sao sap sat saw say sea see set sew she shu shy sid sin sip sir sis sit six ski sky sly sob sod sol son sop sow soy spa spy ' +
    'stu sty sub sue sum sun sup sur tab tad tag tan tap tar tax tea ted tee ten the thy tic tie tim tin tip toe tog tom ton too top tot tow toy try tub ' +
    'tug two tyr uma urn use ute val van vat vet vex via vic vie von vow wad wag war was wax way web wed wee wei wes wet who why wig win wit woe wok won ' +
    'woo wow yak yam yan yao yap yaw yea yen yes yet yew you yue zap zip zoe zoo abad abel able ache acid acne acre adam aged agni aide ainu akan alan alba ' +
    'alex ally also alto amen amid amos amun andy ankh anna anne anti apex arab arch arda area ares aria arid arlo army aron asia aten atom atua audi aunt ' +
    'aura auto avar avid away axel axis axle baal baba baby back bait bake baku bald bale bali ball balm band bane bang bank bare bark barn base bash bask ' +
    'bass bast bath bead beak beam bean bear beat beef been beer bell belt bend bent berg bern best beth bias bike bile bill bind bird bite blob blue blur ' +
    'boar boat body boil bold bolt bomb bond bone book boom boon boot bore born boss both bout bowl brad brag bran brat brew brim bulb bulk bull bump burg ' +
    'burn bury bush bust busy cage cake calf call calm came camp cane cape card care carl carp cart case cash cast cave cell celt chad cham chap chat chef ' +
    'cher chin chip chop cite city clad clam clan clap claw clay clip club clue coal coat coca coco code cody coil coin cold cole colt coma comb come cone ' +
    'cook cool cope copy cord core cork corn cost cosy coup cove crab cree crew crib crop crow cuba cube cult curb cure curl cute czar dada dale dame damp ' +
    'dana dane danu dare dark dart dash data date dave dawn days daze deaf deal dean dear debt deck deed deem deep deer deli demo dent deny desk deva dial ' +
    'dice diet dime dina dine dire dirt disc dish dive dock dodo does doha dole doll dome done doom door dora dorf dose dote dove down doze drag draw drew ' +
    'drip drop drug drum dual duce duck dude duel dues duet duke dull duly dumb dump dune dunk dusk dust duty each earl earn ease east easy echo eden edge ' +
    'edit eire elam elsa else emil emir emit emma enki envy enzo epic eric erik erin eros evan even ever evil ewok exam exit ezra face fact fade fail fair ' +
    'fake fall fame fang fare farm fast fate fawn fear feat feed feel feet fell felt fend fern feud fiat fiji file fill film find fine finn fire firm fish ' +
    'fist five flag flak flap flat flaw flea fled flee flew flip flop flow foal foam foil fold folk fond font food fool foot ford fore fork form fort foul ' +
    'four fowl fred free frog from fuel fuji fula full fume fund fury fuse fuss gaia gail gain gait gala gale gall game gang gape garb gary gate gaul gave ' +
    'gaza gaze gear gene germ gift gild gill gilt gina girl gist gita give glad glee glen glow glue glum gnat gnaw goal goat goes gogo gold golf gone gong ' +
    'good gore gory goth gown grab grad gram gray greg grew grey grid grim grin grip grit grow grub guam gulf gull gulp guru gush gust gwen hack hail hair ' +
    'haka hale half hall halo halt hand hang hank hans hard hare harm harp hart hash hate haul have hawk haze hazy head heal heap hear heat heed heel heim ' +
    'heir held helm help hemp hera herb herd here hero hers hide high hike hill hilo hilt hind hint hire hiss hive hoax hold hole holy home hone honk hood ' +
    'hoof hook hoop hope hopi horn hose host hoth hour howl huge hugo hula hulk hull hump hung hunk hunt hurl hurt hush hutu hymn icon idea idle idly idol ' +
    'igbo ikea imam inca inch ines info inti into iota iowa iran iraq iris iron isis isla isle item ivan jack jade jail jake jane java jazz jean jedi jeer ' +
    'jeff jest jill jive joan joel john join jojo joke jolt jose juan judo judy juju july jump june junk juno jury just jute kaka kale kali kama kami kane ' +
    'kapu kara kari karl kate kava keel keen keep kelp kept khan kick kiev kiki kiln kilt kind king kira kirk kiss kite kiwi knee knew knit knob knot know ' +
    'kobe komi kona kurd kurt kyle lace lack lada lady laid lair lake lala lamb lame lamp lana land lane lani laos lapp lara lard lark lars lash lass last ' +
    'late lava lawn lazy lead leaf leah leak lean leap left lego lena lend lens lent leon less liam liar lice lick lied lien lies lieu life lift like lilt ' +
    'lily lima limb lime limp lina line link lint lion lisa list lisu live load loaf loam loan lobe lock lode loft logo loki lola lolo lone long lono look ' +
    'loom loon loop loot lord lore lori lose loss lost loud love luau luca luck lucy lugh luis luke lull lulu lump luna lung lure lurk lush lust lute lyle ' +
    'lynx lyre maat mace made maid mail maim main make male mali mall malt mama mana mane manx many mare mari mark mars mary mash mask mass mast mata mate ' +
    'math matt maui maul maya maze mead meal mean meat meek meet meld melt memo mend menu meow mere mesa mesh mess miao mice mien mike mila mild mile milk ' +
    'mill milo mime mimi mina mind mine ming mini mink mint minx mira mire miso miss mist mite mitt moan moat mock mode mojo mold mole molt momo mona monk ' +
    'mont mood moon moor moot more morn moro moss most moth move much muck mule mull muse mush musk must mute myra myth naga nail nala name nana nani nape ' +
    'nara nata navy naxi near neat neck need neil nell neon nerd nest news newt next nice nick nico nike niki nile nina nine nini noah node noel none nono ' +
    'nook noon nope nora nord norm nose note noun nude null numb oahu oath obey oboe odin odor ogle ogre ohio oily okay olaf olga oman omar omen omit once ' +
    'only onto onus ooze opal opel open opus oral orca orla oslo otto oval oven over owed owen pace pack pact page paid pail pain pair pale pall palm pane ' +
    'pang pant papa park part pass past path paul pave pawn peak peal pear peat peck peek peel peer pele pelt perk peru pest pete pick pict pier pike pile ' +
    'pill pine pink pint pipe pith pity plan play plea plod plot plow ploy plug plum plus poem poet poke pole poll polo pomp pond pony pool poor pope popo ' +
    'pore pork port pose posh post pour pout pray prey prim prod prom prop prow ptah puff pull pulp puma pump punk puny pupa pure push qing quay quit quiz ' +
    'race rack raft rage raid rail rain raja rake rama ramp rang rani rank rant rare rash rasp rate raul rave ravi raze read real ream reap rear reed reef ' +
    'reek reel rein rely rend rene reno rent rest rhea rice rich rick ride rife rift riga ring riot ripe rise risk rita rite road roam roar robe rock rode ' +
    'role roll roma rome romp roof room root rope rory rosa rose rosy rote rout rove rude rudy ruin rule rump rune rung runt ruse rush russ rust ruth ryan ' +
    'sack safe saga sage said sail sake sale salt same sami sana sand sane sang sank sara sari sash saul save scan scar scot seal seam sean sear seat sect ' +
    'seed seek seem seen seep sega self sell semi send sent serb serf seth shan shea shed shin ship shoe shoo shop shot show shun shut siam sick side sift ' +
    'sigh sign sikh silk sill silo silt sing sink sire sita site sith size skew skid skim skin skip slab slam slap slat slav slay sled slew slid slim slip ' +
    'slit slob slop slot slow slug slum slur smog smug snag snap snip snob snow snub snug soak soap soar soba sock soda sofa soft soil sold sole solo soma ' +
    'some song sony soon soot sore sort soul soup sour sown span spar spat sped spin spit spot spun spur stab stag stan star stay stem step stew stir stop ' +
    'stow stub stud stun styx such suds suit sulk sumo sung sunk sure surf swan swap sway swim tabu tack taco tact tail take tale talk tall tame tami tane ' +
    'tang tank tape tapu tara taro tart task tata taxi teak teal team tear teem tell tend tent term tern tess test text thai than that thaw thea thee them ' +
    'then theo they thin this thor thud thug thus tick tide tidy tier tiki tile till tilt time tina tine tint tiny tire toad toby todd toes tofu toga togo ' +
    'toil told toll tomb tome tone tony took tool toot tore tori torn toss toto tour tout town trap tray tree trek trim trio trip trod trot troy true tsar ' +
    'tuba tube tuck tuft tuna tune tuor tupi turf turk turn tusk tutu twig twin type udon ugly ukko undo unit unto upon urge uruk used user utah vain vale ' +
    'vane vary vase vast vayu veal veer veil vein vent vera verb very vest veto vial vibe vice viet view vile vine visa vito void volt vote wade waft wage ' +
    'wail wait wake walk wall walt wand wane want ward ware warm warn warp wart wary wash wasp wave wavy waxy weak wean wear weed week weep weld well welt ' +
    'went wept were west what when whim whip whom wick wide wife wild will wilt wily wimp wind wine wing wink wipe wire wise wish wisp with woke wolf womb ' +
    'wood wool word wore worf work worm worn wove wrap wren writ yaga yama yank yara yard yarn yawn yeah year yell yelp yoda yoga yoke yolk your yoyo yuan ' +
    'yule yuma yurt zane zara zaza zeal zeke zero zest zeus zhao zhou zinc zion zoey zone zoom zulu zuni aaron about above abuse accra acorn actor admit ' +
    'adobe adult after again agent agile agony ahura akkad alarm album alert alice alien alike alive allan allow aloha alone along aloud altar alter amaze ' +
    'amber amino among angel anger angle angry anime anita ankle apart apple arawn arena argue arise armor aroma arose arrow arryn arwen ashur aside aslan ' +
    'asura atari atlas attic audio audit avoid awake award aware awful aztec babel bacon badge badly bagel baker baldr balin bambi banal banjo bantu baron ' +
    'barry basic basin basis baton began begin being bella belly below benin benny beren betty bible bilbo billy birth black blade blame bland blank blast ' +
    'blaze bleak blend bless blind bloke blond blood bloom board bobby bogus bongo bonus boron bosom bound brain brake brand brave bread break brian bride ' +
    'brief bring broad broke broom broth brown brush brute build burma burnt bwana cabin cable cacao cairo camel canal candy canoe canon cargo carib carla ' +
    'carol carry cause cedar celia ceres chair chaos charm chase cheap cheek chess chest chief child chile chili china chloe choir cisco civil claim clara ' +
    'class clean clear clerk climb clock close cloth cloud clown cobra cocoa colon color comet comic comma conan conga congo coral costa could count court ' +
    'cover crane crash crazy cream crete crime croat cross crowd crown cruel curse cycle czech dagda dagon daily dairy daisy dakar dance danny david dayak ' +
    'death debut decay delhi della delta demon denim dense depot depth devil dhaka diana diary digit diner dirty disco ditto diver dizzy dogma donna donor ' +
    'dorne doubt dozen draft drain drama dream dress drink drive drone dubai dumbo durga durin dutch dwarf eager eagle early earth easel eaten ebony edema ' +
    'edgar edith edwin eject elbow elder elena elisa elite ellen elton email ember emily empty endor enemy enjoy enlil enter entry eomer eowyn epona equal ' +
    'erika error essay essos ethan ethos event every exile exist extra fable facet faith false fancy fanta fatal fauna favor feast felix femur feral ferry ' +
    'fetal fever fiber field final finer fiona first fjord flame flora flour fluid focal focus folio force forum found frame frank fresh freya freyr frigg ' +
    'frodo front fruit fugue fujin fungi funny gabon gamma ganon gauze gecko gemma genie genre ghana ghost giant gimli given glass globe glory going gorod ' +
    'grace grain grand grass grave great greek green greta grief groan groin groot group guard guess guest guide habit hades haiku haiti halal hamas hanna ' +
    'hanoi happy harem harry hausa haven hazel heart heavy heidi helen hello hence henry heron hindu hmong holly homer honda honey honor horde horse horus ' +
    'hotel house human humid humor hurin hyena ibiza idaho ideal idiom idiot igloo image inane inari index india indra inert infer inner input intel inuit ' +
    'irene irish irony isaac issue italy ivana ivory jacob jambo james janet japan jason jelly jenna jerry jesse jewel jimmy joint joker jonas judge juice ' +
    'julia jumbo junta kaaba kabob kabul karel karen karla karma katie kauai kayak kebab keiki kelly kenny kenya kevin khmer khnum kiara kiera koala korea ' +
    'kyoto label labor lager lagos lance large larry larva laser later latin laugh laura layer learn least leave legal leila lemon lemur lenny lethe level ' +
    'lewis lhasa liana libya lidia light lilia limit linda linen liter liver livia llama lobby local logan logic loner loose loren lorna loser lotus lover ' +
    'lower loyal lucas lucia lucid lunar lunch lydia mabel macau macho madam mafia magda magic magma maiar maine major maker malay malta mamba mambo manga ' +
    'mango mania manor maori maple march maria marie mario marta masai mason match maybe mayor mazda mecca medal media megan melon mercy merit metal meter ' +
    'metro miami might milan miles mimic minas miner minor minus moana model mogul moira moist molar molly money monte month moral moria moron motel motor ' +
    'motto mouse mouth movie mural music naboo nacho nadia naive naked nancy naomi nasal nauru naval navel nelly nepal nerve never niger night nikki nikon ' +
    'ninja noble noise nokia nolan nomad norma norse north novel nubia nurse nylon oasis ocean ohana olive olmec omaha omega onion opera orbit order organ ' +
    'osaka oscar other otter ought ounce outer ovary owner pablo paint palau pampa panel panic papal paper paris parka party pasha pasta patio paula pause ' +
    'peace pearl pedal pedro penal penny pepsi peril perth perun petal peter petra phase phone photo piano pilot pinto pizza place plain plane plant plate ' +
    'pluto point polar polio polka polly porto poser power press price pride prime print prior prize proud pupil puree pwyll qatar queen quipu quite quito ' +
    'quota rabbi radar radio rainy raise rally ralph ramen ranch range rangi rania rapid ratio raven razor reach ready realm rebel regal reign relax relic ' +
    'remit renal renee renew repel resin retro rhino rider rifle right rigor riker rinse risen rival river robin robyn roger rohan roman ronny rosie rotor ' +
    'round route royal rufus rumba rumor rural saber sadie sadly saint salad salem sally salon salsa samba samoa santa sarah satin sauna saxon scale scene ' +
    'sedan selma sense seoul serum seven shade shake shame shape share sheep sheet sheik shelf sheol shila shine shire shirt shona shore short shrek silly ' +
    'simba simon sinai since sioux siren sitar sixty skill skoda slave sleep small smaug smile smoke snake sobek sofia solar solid sonar sonia sonic sonny ' +
    'sonya sorry sound south space spade spain spare speak spend spice spite spock spoon stadt staff stage stain stake stale stare stark start state steal ' +
    'steam steve stole stone store storm story sudan sugar sumer sunny super surya susan sushi sutra swami swazi sweat swede sweet swiss syria table taboo ' +
    'taiga taino taken talia talon tamer tamil tampa tango tania tanya taper tardy taste tatar tenor tepee tepid terra terry tesla tessa texas thank their ' +
    'there these thing those thoth three tiara tibet tibia tiger timer tired titan today token tokyo tomas tommy tonal toner tonga tonic total totem touch ' +
    'tower toxic trace trade trail train tribe trina troll truly tulip tully tulsa tumor tuner tunic tunis turin tutor tutsi tuvan ulcer ultra uncle under ' +
    'union unite unity until upper urban usual utero uther uzbek vader vague valar valid valor value vanna vapor vegas veles venom venue venus video vigor ' +
    'villa ville vilma vinyl viola viper viral virus visit vista vital vocal vodka voice volvo voter wagon wales waltz wanda water welsh wendy whale wheat ' +
    'where which while white whole wilma woman women world worse worst wotan would xhosa yahve yakut yemen yield young youth yukon zebra zelda zelma adidas ' +
    'adonai africa alaska alpaca altmer amazon angola animal ankara anorak anubis apache apollo arabia arthur asgard athena athens attain avalon avatar ' +
    'balrog bamako banana bandit banner barbie barrel basket basque beirut berber berlin bikini bogota bolero bonito borneo bosmer boston brahma brazil ' +
    'breton brigid burial buryat caliph camera canada canary candle canine cannon canopy carbon carrot casino castle cavern cereal chakra cinema citrus ' +
    'ciudad coffin colony column comedy common condor connor corona cotton cousin coyote dakota dallas danger darwin dazbog debris decade delete demise ' +
    'denial dental denver desert desire detail detain dharma dinner direct divine doctor dollar domain donate dragon dunmer durban dwalin editor effort ' +
    'eleven elohim elrond empire enamel encore energy engine enkidu entire eragon eraser erebor escape estate ethnic europa europe exotic fabric famine ' +
    'father feline female fiasco figure finale finger finite forest formal fossil fremen french fuhrer future galore garden garlic geisha gender genius ' +
    'gentle gerbil german ginger global goblet goblin golden gollum gondor google gospel gravel greece guitar hakuna hamlet hammer handle harare harbor ' +
    'harden harold hathor havana hawaii hazard heaven helmet hermit heroin hidden hobbit homage honest horror humane humble hunter hyrule iguana immune ' +
    'impala import inanna income indigo infant ingrid inland inmate innate insane insect inside ishtar island israel jaguar jargon jersey jordan junior ' +
    'kahuna kaiser karate kazakh kennel kernel kettle kidnap kigali kimono kitten kuwait ladder lagoon lakota lament laptop latent latter lawyer leader ' +
    'legend lentil lesson letter liquor lisbon listen litter little lizard lolita london lorien luanda lumber lunate lusaka maasai madman madrid magnet ' +
    'magyar mahalo maiden malaga malawi mallet mammal manchu manila manner mantle mantra manual manure maputo marduk marina marine market marlin maroon ' +
    'martyr matata matter mature meadow medina medley melkor mellow member memoir memory menace mental mentor merger merlin meteor method metric mexica ' +
    'mexico mezcal midair midgar mikado miller minion minute mirror misery mister mithra mixtec modern modest module mokosh moloch moment monaco mongol ' +
    'monkey morale morana mordor morsel mortal mortar moscow mosque mother motion motive mufasa mullah mumbai murder muscat muscle museum mutant mutiny ' +
    'nanook napkin narnia narrow nation native nature nausea navajo nazgul nectar needle nephew nevada niamey nickel ninive nirvan nissan noldor noodle ' +
    'normal norway notion novice number nutmeg object oblong obtain ocelot office oliver online opener oppose option oracle orange orchid ordain ordeal ' +
    'origin ornate orphan osiris ottawa outlaw outlet pakeha palace pallet panama pantry papaya parade parcel pardon parent parlor parrot pastel pastor ' +
    'patent patrol patron peanut pebble pedant pelvis pencil people pepper period permit persia person petite picard pigeon pillar pillow pirate pistol ' +
    'planet plasma plural poison poland police polite pollen potato potent potion powder prison profit proton pseudo public puerto pulsar pumice punish ' +
    'puppet purple quenya quinoa rabbit racket radial radish radius rafiki raijin random ransom rascal rather rating ration ravine reason recoil record ' +
    'redeem reform refuse regain regard regime region regret reject relate relish remain remedy remote render rental repair repeat report rescue resent ' +
    'reside resist resort result retail retain retina retire return reveal revere revise revolt reward ribbon riddle ritual riyadh rocket rodent roster ' +
    'rotate rotten rubber rubric rudder ruined rumble runner russia rwanda sacred saddle safari sahara saigon sailor sakura salami saline salmon saloon ' +
    'salute sample sandal sandra satire saturn sauron savage scalar scheme school season second secret sedate select senate senior sensor serbia serial ' +
    'series sermon server sesame settle severe sewage shadow shaman shelob shiver shogun sierra signal silent silken silver silvia simple sindar sinner ' +
    'sister sketch skyrim slogan social socket sodium soften solemn sonata sonnet sorrow source sparta sphere spider spinal spiral spirit splash stable ' +
    'stamen staple starve static statue status stella steppe stolen strain strand stream street stress strict strike string stroke strong studio submit ' +
    'subtle suburb sudden suffer sultan summer summit sunset supper supply survey suture suzuki svarog sweden switch sydney symbol syntax system tablet ' +
    'tahiti tailor taipei talent tanaka tandem tantra target tartan tavern tehran teleri telugu temper temple tenant tender tennis tenure terror tetris ' +
    'thanos thesis throne tiamat ticket timber tinsel tirade tirith tissue tlaloc toggle toilet toltec tomato tongue tonsil torque torrid toward toyota ' +
    'treaty tremor trench tribal trivia tropic trowel tundra tunnel turban turkey turnip turtle tuvalu tycoon tyrant tyrell ubuntu udmurt uganda umpire ' +
    'unable unison unreal upload uproar uranus urinal usable utmost valley vandal vanish varied varuna vector velvet vendor veneer verbal vermin vessel ' +
    'viable victim victor vienna viktor violet violin virgin virile vision visual vizier volume voodoo vortex voyage vulcan vulgar wahine wallet walnut ' +
    'walrus wander warden wasabi weapon weasel whenua window winter wisdom wizard wonder wooden wookie worker yasmin yoruba zambia zenith zhuang zombie ' +
    'america anarion aragorn arrakis avocado axolotl azeroth bashkir boromir burrito camelot chuvash cornish elendil elysium england faramir gandalf ' +
    'ganesha guarani gwydion hanuman ireland isildur izanagi izanami jupiter kanaloa khajiit klingon lakshmi legolas luthien mercury midgard morgana ' +
    'morgoth nahuatl nairobi neptune nirvana numenor olympus parvati pikachu pokemon quetzal romulan samurai sekhmet susanoo swahili tamriel tangata ' +
    'taranis tequila theoden zapotec alderaan argonian atreides celeborn denethor earendil godzilla isengard lancelot morrigan niflheim ragnarok rhiannon ' +
    'tangaroa tartarus tatooine valhalla westeros alagaesia amaterasu cernunnos galadriel gilgamesh ilmarinen pachamama ungoliant viracocha yggdrasil ' +
    'vainamoinen quetzalcoatl' +
    // more of the same, found by running two million generated names past a spelling dictionary, and names the world already knows
    ' aha bro moo yum loo gal baa emo bot yuk rad yep zen goo pip dis foo mod yup gee biz ugh huh zit nah veg cad fab fib yay wiz heh bah pox mac nix bib ' +
    'rev sec vac lye meh duh cig mic pic jib doc eek tux ilk amp err gab tat tut sot pap cur bop lav nib bod lib bub fop pud fob guv lol nano lama limo ' +
    'lech mono lingo thong sine mange nuke chain chum sham aero wino lunge loin kilo hinge dill vino anon singe wiki meta lite thru pita thou lathe nave ' +
    'khaki peon nova meme akin prong alas shone ante magi tush hath faun dram phat thine demi tinge karat manana mote drat peso lope shush reran vole agar ' +
    'gaga tram bingo cola noir wile binge shim soya dike midi bade pram tithe chime redo zine niche mage manta laze nosh asana wadi loch blini vise mega ' +
    'unmet feta henge beta sago biker shale boing lido bail doth fringe aloe babe change guano hiya nadir teen thigh ether filing theme mayo chemo dinar ' +
    'saner sinus gran taut repo natal manna riser viva glam bathe usher mope chute jingo mocha aver loath saith kanji fave chore dingo dacha zeta tenon ' +
    'mica yore lube senor bide rarer henna leer bach flange fete vape lithe chose rabid afar mosh hobo savor nous bier briar bling misuse anima chic dais ' +
    'basal ajar shave boor goner planar loris avian venal yeti tense liken doer rerun seer roil tenet yogi manse gibe taker demur rook nigh agog bugler ' +
    'loco cache anion sorta alias deja ladle durum zonal lamer boner pogo carer velar tarn dosh laud seine thrum dour theta talus leek pagan shaken rumen ' +
    'diva weir abet goer grok brook plat blah reiki jibe minima choke shiny title quiche titian hatch ammo ghat maned rube liner okra bozo hark radon ravel ' +
    'taser arson ashore orang miler rand tapas riven sous within aqua kinda adze gent cede grog grade rind prat prep jape romeo kook remake farina enema ' +
    'luge unman zing codon axon ulna modal rondo rosin rover lacer senora grail acer ocher suet cater loci tiler gland loll dell flare throng tonne bode ' +
    'gird rill tutti lasso reset senna tarot amok waken futon shag ganja panda torus cheer wafer aural ardor erase darn thorn prune fling fife urea adage ' +
    'canto conic chitin lilac manic sabot baser danish rebar awash leash piton dojo krone plaza flume lever largo glare stove recon nitre racer arbor pater ' +
    'shiner snit shiv coke bloat riper rasher trash drove saver boson haired beret drake aster aider tidal terse brig laden boric berth bard bruin sneer ' +
    'noose hallo ochre beige zebu panto gash chalk dint shekel cant gosh york surer borer ender meson revel geek raver ouch carom veld liege older flan ' +
    'cred abase lease blog phish krona gather miser apse muzak garish rower newer mater blip fluke dupe plop asap inset basil grove chosen snore ruler ' +
    'swine spore vegan heath ripen aspen hater jerk snare hoot raisin atone moose shove ashen genus tease hovel cull trial wiser third grime cigar slide ' +
    'sloth paste purse peep doing serene cheese shout pecan latex toxin abide futile prose safer stair sober visor logos trout spike skate slain sting ' +
    'skirt greet vivid noted breath grind torso spell hippo fallen widen sheen garage alibi nicer inlet singer mount molten lesser dealer chant center ' +
    'dread truth glade career flash float teach torch birch recent search aisle larger merge bravo verge cider please azure clone praise gloom burger gnome ' +
    'chill woven trauma fewer vowel either gauge mauve evoke booth trait towel robot harder seize bunker bison faster travel dope haha hehe hihi hoho huhu ' +
    'yada nada rara mumu gigi fifi bobo dong nori bento banzai sensei senpai kawaii katana origami tsunami karaoke naruto sasuke yusuke akira hiro yuki ' +
    'goku totoro luigi subaru nagoya lanai saab abba bjork viking amigo gringo pueblo chai naan curry masala tandoor ganesh shiva vishnu buddha haram hijab ' +
    'burka fatwa kazak pharaoh sphinx sigma alpha kappa lambda kungfu taichi fengshui wonton husky ' +
    'nee dee bio poo pee tech char chem brie chit elan thane tote rile maxi alum blot goon libel xenon lapel lumen chide maven sever shah lout lethal flair ' +
    'mart deter limbo humus tort dried irate murk clod glean elope valet modem timid banish glove brine glaze balk flake jilt spud kosher gonna felon kaput ' +
    'belle bred snail amuse church repose manage quote locate mutual derail trove tether louse shark disk stave gloat artisan alder spam sport oust serve ' +
    'fret teeth adorn deft spoil grate grill tempo cent cheat beech slate beach floral chisel median plunge plume fetus geld ibis agate datum cacti radii ' +
    'yodel sarong batik kendo betel mitre aeon aether ' +
    'korean chinese italian persian russian indian mexican swedish polish british haitian tongan samoan albanian armenian syrian libyan egyptian spanish ' +
    'turkish finnish african american asian european japanese javanese burmese maltese siamese arabic arabian iranian iraqi israeli afghan celtic nordic ' +
    'slavic germanic hellenic english scottish belgian austrian hungarian romanian bulgarian serbian croatian bosnian ukrainian georgian mongolian tibetan ' +
    'vietnamese filipino hawaiian canadian brazilian peruvian chilean cuban jamaican nigerian kenyan ethiopian somali moroccan algerian tunisian';
  let common = null;
  function isCommon(w) {
    if (!common) {
      common = Object.create(null);
      const a = ENGLISH.split(' ');
      for (let i = 0; i < a.length; i++) common[a[i]] = 1;
    }
    return common[w] === 1;
  }
  // a known word, or one with an English ending on it ('Noses', 'Liked', 'Miner', 'Mining', 'Sandy')
  function isEnglish(w) {
    if (isCommon(w)) return true;
    const n = w.length;
    if (n < 4) return false;
    const a = w.charCodeAt(n - 1), b = w.charCodeAt(n - 2);
    if (a === 115) return isCommon(w.slice(0, n - 1)) || (b === 101 && n > 4 && isCommon(w.slice(0, n - 2)));                    // -s, -es
    if (b === 101 && (a === 100 || a === 114)) return isCommon(w.slice(0, n - 1)) || (n > 4 && isCommon(w.slice(0, n - 2)));     // -d, -ed, -r, -er
    if (a === 103 && b === 110 && n > 5 && w.charCodeAt(n - 3) === 105) return isCommon(w.slice(0, n - 3)) || isCommon(w.slice(0, n - 3) + 'e');   // -ing
    if (a === 121 && n > 4) return isCommon(w.slice(0, n - 1));                                                                  // -y
    return false;
  }
  // a word that is one short syllable said twice and nothing more ('Haha', 'Lulu'), or one consonant opening three
  // syllables running ('Mamimi', 'Rarur'): baby talk in any language
  const STUTTER = /(?:^| )(..)\1(?: |$)|([^aeiou ])[aeiou]+\2[aeiou]+\2/;
  // okName, and it reads well: a name of several words has five letters or more ('Qe-Luun', not 'Na Ma'), no word
  // stutters, and nothing in it reads as a word of ours: no word of three letters or more is a known one
  // ('Rat-Doter'), and the name is not made of known words only ('To Be')
  function fine(s, lo, hi) {
    if (!okName(s, lo, hi)) return false;
    const f = OKF;
    let all = true, words = 1;
    const english = TUNE.names.english;
    for (let i = 0, a = 0; i <= f.length; i++) {
      if (i === f.length || f.charCodeAt(i) === 32) {
        if (i < f.length) words++;
        if (english) {
          const w = f.slice(a, i);
          if (i - a > 2) { if (isEnglish(w)) return false; all = false; }
          else if (!isCommon(w)) all = false;
        }
        a = i + 1;
      }
    }
    if (english && all) return false;
    if (words > 1 && f.length - (words - 1) < 5) return false;
    return !STUTTER.test(f);
  }
  // true when a word of the name is heavier than the language likes: more than maxW letters or maxG groups of vowels
  function wordy(s, maxW, maxG) {
    let w = 0, g = 0, inV = false;
    for (let i = 0; i < s.length; i++) {
      const ch = s.charCodeAt(i);
      if (ch === 32 || ch === 45) { w = 0; g = 0; inV = false; continue; }
      if (ch === 39) { inV = false; continue; }
      if (++w > maxW) return true;
      if (ch < 128 ? PV[ch] === 1 : PV[FOLD[ch] | 0] === 1) { if (!inV) { inV = true; if (++g > maxG) return true; } }
      else inV = false;
    }
    return false;
  }

  // ------------------------------------------------------------------ small helpers
  function str(x) { return typeof x === 'string' ? x : ''; }
  function arr(x) { return Array.isArray(x) ? x : []; }
  function num(x, d) { return typeof x === 'number' && x === x ? x : d; }

  // cumulative weights 1/(i + flat): rank-frequency curve (flat = 1 is Zipf; larger is flatter)
  function cumRank(n, flat) {
    const a = new Float64Array(n);
    let t = 0;
    for (let i = 0; i < n; i++) { t += 1 / (i + flat); a[i] = t; }
    return a;
  }
  function cumOf(ws, n) {
    const a = new Float64Array(n);
    let t = 0;
    for (let i = 0; i < n; i++) { const w = +ws[i]; if (w > 0) t += w; a[i] = t; }
    return a;
  }
  function pick(rng, cum) {
    const n = cum.length - 1, r = rng.f() * cum[n];
    for (let i = 0; i < n; i++) if (r < cum[i]) return i;
    return n;
  }
  function capWord(s) { return s ? s.charAt(0).toUpperCase() + s.slice(1) : s; }

  let spare = null;                                       // used only when a caller passes no rng
  function useRng(rng) {
    if (rng && typeof rng.f === 'function') return rng;
    return spare || (spare = DM.U.rng(0x6e616d65));
  }

  // helpers for the language recipes
  function shuf(r, s) { const a = s.split(''); r.shuffle(a); return a.join(''); }
  function tiers(r, list) { let s = ''; for (const t of list) s += shuf(r, t); return s; }             // most frequent tier first
  function some(r, list, lo, hi) { const a = list.slice(); r.shuffle(a); return a.slice(0, r.int(lo, Math.min(hi, a.length))); }
  function ins(r, s, ch, from) { const i = r.int(Math.min(s.length, Math.round(s.length * from)), s.length); return s.slice(0, i) + ch + s.slice(i); }
  function opt(r, s, ch, p) { return r.chance(p) ? ins(r, s, ch, 0.5) : s; }

  // ------------------------------------------------------------------ spelling
  function spLen(c, s) { let n = 0; for (let i = 0; i < s.length; i++) n += c.slen[s.charCodeAt(i) & 127]; return n; }

  function spell(c, s) {
    const n = s.length, sp = c.sp;
    let out = '';
    for (let i = 0; i < n; i++) {
      const k = s.charCodeAt(i) & 127, cl = CLS[k];
      if (cl & LONG) {
        const j = LBASE.indexOf(s.charAt(i));
        if (c.lvMark) {
          // An accent is only written where the letters around it leave room: crude checkers count an accented
          // vowel as a consonant, and no name may show a run of four.
          let run = 1;
          for (let p = out.length - 1; p >= 0; p--) { const o = out.charCodeAt(p); if (o < 128 && PV[o]) break; run++; }
          for (let q = i + 1; q < n && run <= 3; q++) {
            const kk = s.charCodeAt(q) & 127;
            if ((CLS[kk] & (VOW | LONG)) === VOW) break;
            run += (CLS[kk] & LONG) ? 1 : c.slen[kk];
          }
          out += run <= 3 ? c.lvTab[j] : SBASE.charAt(j);
        } else out += c.lvTab[j];
        continue;
      }
      if (k === 39) { if (i > 0 && i < n - 1) out += "'"; continue; }               // a glottal stop is not written at the edge of a word
      if (k === 107) {                                                              // k
        const nx = i + 1 < n ? s.charCodeAt(i + 1) & 127 : 0;
        if (c.xk && nx === 115) { out += 'x'; i++; continue; }                      // ks written x
        if (c.cq) { out += (CLS[nx] & FRONT) ? 'qu' : 'c'; continue; }              // c / qu
      }
      if (k === 101 && c.fe && i === n - 1 && n > 2 && (CLS[s.charCodeAt(i - 1) & 127] & CONS)) {   // final e written with a diaeresis
        let run = 1;
        for (let p = out.length - 1; p >= 0; p--) { const o = out.charCodeAt(p); if (o < 128 && PV[o]) break; run++; }
        out += run <= 3 ? 'ë' : 'e';
        continue;
      }
      out += sp[k];
    }
    return out;
  }

  // ------------------------------------------------------------------ regular sound shifts
  // A shift is a small JSON array. 'm' maps sounds one-to-one in a context, 'd' deletes, 'r' rewrites sequences;
  // the rest are named processes. Contexts: * anywhere, i word-initial, j word-initial before a vowel, f word-final,
  // v between vowels, e before a front vowel, b before a back vowel, a after a vowel, c before a consonant, n after
  // a nasal, s the vowel of the first syllable, u any later vowel.
  function nucleus(s) {                                   // [start, end) of the first run of vowels
    let p = 0;
    const n = s.length;
    while (p < n && !(CLS[s.charCodeAt(p) & 127] & VOW)) p++;
    let q = p;
    while (q < n && (CLS[s.charCodeAt(q) & 127] & VOW)) q++;
    return p * 64 + q;
  }
  function inCtx(s, i, n, ctx, ini, fin) {
    switch (ctx) {
      case '*': return true;
      case 'i': return i === 0 && ini;
      case 'j': return i === 0 && ini && n > 1 && (CLS[s.charCodeAt(1) & 127] & VOW) !== 0;
      case 'f': return i === n - 1 && fin;
      case 'v': return i > 0 && i < n - 1 && (CLS[s.charCodeAt(i - 1) & 127] & VOW) !== 0 && (CLS[s.charCodeAt(i + 1) & 127] & VOW) !== 0;
      case 'e': return i < n - 1 && (CLS[s.charCodeAt(i + 1) & 127] & FRONT) !== 0;
      case 'b': return i < n - 1 && (CLS[s.charCodeAt(i + 1) & 127] & (VOW | FRONT)) === VOW;
      case 'a': return i > 0 && (CLS[s.charCodeAt(i - 1) & 127] & VOW) !== 0;
      case 'c': return i < n - 1 && (CLS[s.charCodeAt(i + 1) & 127] & CONS) !== 0;
      case 'n': return i > 0 && (CLS[s.charCodeAt(i - 1) & 127] & NASAL) !== 0;
      case 's': { if (!ini) return false; const pq = nucleus(s); return i === (pq >> 6) && (pq & 63) === i + 1; }
      case 'u': { if (!ini) return true; return i >= (nucleus(s) & 63); }
    }
    return false;
  }
  const BAD_FINAL = "hwy'P1KQ2345678W9";                  // sounds that do not close a word

  // is this a shift compileRule understands? (a language read back from a damaged save must not throw later)
  const CTX = '*ijfvebacnsu';
  function isStr(x) { return typeof x === 'string'; }
  function strList(x) { return Array.isArray(x) && x.every(isStr); }
  function validRule(r) {
    if (!Array.isArray(r)) return false;
    const ctx = isStr(r[1]) && r[1].length === 1 && CTX.indexOf(r[1]) >= 0;
    switch (r[0]) {
      case 'm': return ctx && isStr(r[2]) && isStr(r[3]) && r[2].length > 0 && r[2].length === r[3].length;
      case 'd': return ctx && isStr(r[2]) && r[2].length > 0;
      case 'r': return ctx && strList(r[2]) && strList(r[3]) && r[2].length === r[3].length && r[2].every((s) => s.length > 0);
      case 'par': case 'pro': case 'esc': return isStr(r[1]) && r[1].length === 1;
      case 'um': return isStr(r[1]) && isStr(r[2]) && isStr(r[3]) && r[2].length === 1 && r[3].length > 0;
      case 'brk': return isStr(r[1]) && isStr(r[2]) && r[1].length === 1 && r[2].length > 0;
      case 'apo': case 'fcl': case 'epi': case 'ccs': case 'deg': case 'nasl': case 'len': return true;
    }
    return false;
  }

  function compileRule(r) {
    const type = r[0];
    switch (type) {
      case 'm': {
        const ctx = r[1], from = r[2], to = r[3];
        return function (s, ini, fin) {
          let out = null;
          const n = s.length;
          for (let i = 0; i < n; i++) {
            const ch = s.charAt(i), k = from.indexOf(ch);
            if (k >= 0 && inCtx(s, i, n, ctx, ini, fin)) { if (out === null) out = s.slice(0, i); out += to.charAt(k); }
            else if (out !== null) out += ch;
          }
          return out === null ? s : out;
        };
      }
      case 'd': {
        const ctx = r[1], set = r[2];
        return function (s, ini, fin) {
          const n = s.length;
          if (n <= 2) return s;
          let out = null, left = n;
          for (let i = 0; i < n; i++) {
            const ch = s.charAt(i);
            if (left > 2 && set.indexOf(ch) >= 0 && inCtx(s, i, n, ctx, ini, fin)) {
              if (out === null) out = s.slice(0, i);
              left--;
              // two equal vowels brought together become one long vowel
              if (out.length && i + 1 < n) {
                const a = out.charAt(out.length - 1), b = s.charAt(i + 1), ia = SBASE.indexOf(a.toLowerCase());
                if (ia >= 0 && SBASE.indexOf(b.toLowerCase()) === ia) { out = out.slice(0, -1) + LBASE.charAt(ia); i++; left--; }
              }
            } else if (out !== null) out += ch;
          }
          return out === null ? s : out;
        };
      }
      case 'r': {
        const ctx = r[1], from = arr(r[2]), to = arr(r[3]);
        return function (s, ini, fin) {
          for (let k = 0; k < from.length; k++) {
            const a = from[k], b = to[k];
            if (ctx === 'f') { if (fin && s.length > a.length && s.slice(s.length - a.length) === a) s = s.slice(0, s.length - a.length) + b; }
            else if (ctx === 'i') { if (ini && s.length > a.length && s.slice(0, a.length) === a) s = b + s.slice(a.length); }
            else if (s.indexOf(a) >= 0) s = s.split(a).join(b);
          }
          return s;
        };
      }
      case 'apo':                                         // loss of a final short vowel: asha → ash
        return function (s, ini, fin) {
          const n = s.length;
          if (!fin || !ini || n < 4) return s;
          const a = CLS[s.charCodeAt(n - 1) & 127], b = s.charAt(n - 2), c = CLS[s.charCodeAt(n - 3) & 127];
          if ((a & (VOW | LONG)) !== VOW || !(CLS[b.charCodeAt(0) & 127] & CONS) || !(c & VOW) || BAD_FINAL.indexOf(b) >= 0) return s;
          return s.slice(0, n - 1);
        };
      case 'par': {                                       // a vowel grows after a final consonant: vorn → vorna
        const v = r[1];
        return function (s, ini, fin) { return fin && (CLS[s.charCodeAt(s.length - 1) & 127] & CONS) ? s + v : s; };
      }
      case 'fcl':                                         // final stops fall silent
        return function (s, ini, fin) {
          const n = s.length;
          return fin && n > 2 && "ptkbdgq'".indexOf(s.charAt(n - 1)) >= 0 && (CLS[s.charCodeAt(n - 2) & 127] & VOW) ? s.slice(0, n - 1) : s;
        };
      case 'pro': {                                       // vowel-initial words gain a consonant
        const k = r[1];
        return function (s, ini) { return ini && (CLS[s.charCodeAt(0) & 127] & VOW) ? k + s : s; };
      }
      case 'esc': {                                       // a vowel before s + consonant: skar → eskar
        const v = r[1];
        return function (s, ini) {
          return ini && s.length > 2 && 'sSz'.indexOf(s.charAt(0)) >= 0 && (CLS[s.charCodeAt(1) & 127] & CONS) ? v + s : s;
        };
      }
      case 'epi':                                         // an initial cluster is broken up: kra → kara
        return function (s, ini) {
          if (!ini || s.length < 3 || !(CLS[s.charCodeAt(0) & 127] & CONS) || !(CLS[s.charCodeAt(1) & 127] & CONS)) return s;
          const v = s.charAt(2).toLowerCase();
          return SBASE.indexOf(v) >= 0 ? s.charAt(0) + v + s.slice(1) : s;
        };
      case 'ccs':                                         // an initial cluster loses its first sound: kna → na
        return function (s, ini) {
          return ini && s.length > 3 && (CLS[s.charCodeAt(0) & 127] & CONS) && (CLS[s.charCodeAt(1) & 127] & CONS) ? s.slice(1) : s;
        };
      case 'deg':                                         // double consonants become single
        return function (s) {
          let out = null;
          for (let i = 1; i < s.length; i++) {
            const same = s.charCodeAt(i) === s.charCodeAt(i - 1) && (CLS[s.charCodeAt(i) & 127] & CONS);
            if (same) { if (out === null) out = s.slice(0, i); } else if (out !== null) out += s.charAt(i);
          }
          return out === null ? s : out;
        };
      case 'nasl':                                        // vowel + nasal before a consonant or at the end → long vowel
        return function (s, ini, fin) {
          const n = s.length;
          if (n < 3) return s;
          let out = null;
          for (let i = 1; i < n; i++) {
            const ch = s.charAt(i), pv = SBASE.indexOf(s.charAt(i - 1));
            const hit = pv >= 0 && (ch === 'n' || ch === 'm' || ch === 'N') &&
              (i === n - 1 ? fin && i > 1 : (CLS[s.charCodeAt(i + 1) & 127] & CONS) !== 0 && s.charAt(i + 1) !== ch);
            if (hit) {
              if (out === null) out = s.slice(0, i);
              if (!(i > 1 && (CLS[s.charCodeAt(i - 2) & 127] & VOW))) out = out.slice(0, -1) + LBASE.charAt(pv);     // after a diphthong the nasal just falls away
            } else if (out !== null) out += ch;
          }
          return out === null ? s : out;
        };
      case 'um': {                                        // a vowel is coloured by the vowel of the next syllable
        const trig = r[1], from = r[2], to = r[3];
        return function (s) {
          const n = s.length;
          let out = null;
          for (let i = 0; i < n; i++) {
            const ch = s.charAt(i);
            let hit = false;
            if (ch === from) {
              let j = i + 1;
              while (j < n && (CLS[s.charCodeAt(j) & 127] & VOW)) j++;
              if (j > i + 1) j = n;                         // only a simple vowel shifts
              while (j < n && (CLS[s.charCodeAt(j) & 127] & CONS)) j++;
              hit = j < n && j > i + 1 && trig.indexOf(s.charAt(j)) >= 0;
            }
            if (hit) { if (out === null) out = s.slice(0, i); out += to; } else if (out !== null) out += ch;
          }
          return out === null ? s : out;
        };
      }
      case 'brk': {                                       // the vowel of the first syllable breaks in two: ker → kier
        const from = r[1], to = r[2];
        return function (s, ini) {
          if (!ini) return s;
          const pq = nucleus(s), p = pq >> 6;
          return (pq & 63) === p + 1 && s.charAt(p) === from ? s.slice(0, p) + to + s.slice(p + 1) : s;
        };
      }
      case 'len':                                         // the vowel of an open first syllable grows long
        return function (s, ini) {
          if (!ini) return s;
          const pq = nucleus(s), p = pq >> 6, n = s.length;
          const j = SBASE.indexOf(s.charAt(p));
          if (j < 0 || (pq & 63) !== p + 1 || p + 2 >= n) return s;
          return (CLS[s.charCodeAt(p + 1) & 127] & CONS) && (CLS[s.charCodeAt(p + 2) & 127] & VOW) ? s.slice(0, p) + LBASE.charAt(j) + s.slice(p + 1) : s;
        };
    }
    return function (s) { return s; };
  }

  // the shifts a daughter language may pick from: [family, kind, rule]. One per family, a mix of kinds
  // (C consonants, V vowels, S shape of the word), and only those that visibly change the parent's words.
  const SHIFTS = [
    ['len', 'C', ['m', '*', 'ptk', 'fTx']], ['len', 'C', ['m', 'v', 'ptk', 'fTx']], ['len', 'C', ['m', 'v', 'ptk', 'bdg']],
    ['len', 'C', ['m', 'i', 'ptk', 'fTx']],
    ['spi', 'C', ['m', 'v', 'bdg', 'vDG']], ['spi', 'C', ['m', '*', 'b', 'v']],
    ['dev', 'C', ['m', '*', 'bdg', 'ptk']], ['dev', 'C', ['m', 'f', 'bdgvzDGZj', 'ptkfsTxSC']],
    ['fvo', 'C', ['m', 'v', 'sfTSx', 'zvDZG']],
    ['th', 'C', ['m', '*', 'T', 't']], ['th', 'C', ['m', '*', 'T', 's']], ['th', 'C', ['m', '*', 'T', 'f']],
    ['dh', 'C', ['m', '*', 'D', 'd']], ['dh', 'C', ['m', '*', 'D', 'z']],
    ['kh', 'C', ['m', '*', 'x', 'h']], ['kh', 'C', ['m', '*', 'x', 'k']],
    ['gh', 'C', ['m', '*', 'G', 'g']], ['gh', 'C', ['d', 'v', 'G']],
    ['q', 'C', ['m', '*', 'q', 'k']], ['q', 'C', ['m', '*', 'q', 'x']],
    ['h', 'C', ['d', '*', 'h']], ['h', 'C', ['d', 'i', 'h']], ['h', 'C', ['m', '*', 'h', 'x']],
    ['gl', 'C', ['d', '*', "'"]],
    ['f', 'C', ['m', '*', 'f', 'h']], ['f', 'C', ['m', '*', 'f', 'p']],
    ['p', 'C', ['m', '*', 'p', 'f']], ['p', 'C', ['m', '*', 'p', 'h']],
    ['s', 'C', ['m', 'j', 's', 'h']], ['s', 'C', ['m', 'v', 's', 'h']], ['s', 'C', ['m', 'v', 's', 'r']], ['s', 'C', ['m', '*', 's', 'S']],
    ['s', 'C', ['m', 'e', 's', 'S']],
    ['sh', 'C', ['m', '*', 'S', 's']], ['sh', 'C', ['m', '*', 'S', 'x']],
    ['z', 'C', ['m', '*', 'z', 'r']], ['z', 'C', ['m', '*', 'z', 's']], ['z', 'C', ['m', '*', 'z', 'J']],
    ['zh', 'C', ['m', '*', 'Z', 'z']], ['zh', 'C', ['m', '*', 'Z', 'j']], ['zh', 'C', ['m', '*', 'Z', 'S']],
    ['wv', 'C', ['m', '*', 'w', 'v']], ['wv', 'C', ['m', '*', 'v', 'w']], ['wv', 'C', ['m', '*', 'v', 'b']], ['wv', 'C', ['m', 'i', 'w', '9']],
    ['yj', 'C', ['m', 'i', 'y', 'j']], ['yj', 'C', ['m', '*', 'j', 'y']], ['yj', 'C', ['m', '*', 'j', 'Z']], ['yj', 'C', ['m', '*', 'y', 'j']],
    ['af', 'C', ['m', '*', 'C', 'S']], ['af', 'C', ['m', '*', 'c', 's']], ['af', 'C', ['m', '*', 'C', 'c']], ['af', 'C', ['m', '*', 'c', 'C']],
    ['af', 'C', ['m', '*', 'J', 'z']],
    ['tl', 'C', ['m', '*', 'L', 't']], ['tl', 'C', ['m', '*', 'L', 'C']], ['tl', 'C', ['m', '*', 'L', 'l']],
    ['liq', 'C', ['m', '*', 'l', 'r']], ['liq', 'C', ['m', '*', 'r', 'l']], ['liq', 'C', ['m', 'v', 'd', 'r']], ['liq', 'C', ['m', '*', 'H', 'l']],
    ['nas', 'C', ['m', '*', 'N', 'n']], ['nas', 'C', ['m', '*', 'Y', 'n']], ['nas', 'C', ['m', 'e', 'n', 'Y']], ['nas', 'C', ['m', 'f', 'm', 'n']],
    ['nas', 'C', ['m', 'f', 'n', 'N']], ['nas', 'C', ['m', 'f', 'n', 'm']],
    ['pal', 'C', ['m', 'e', 'kg', 'Cj']], ['pal', 'C', ['m', 'e', 'td', 'cJ']], ['pal', 'C', ['m', 'e', 'td', 'Cj']], ['pal', 'C', ['m', 'e', 'kx', 'cS']],
    ['lab', 'C', ['m', '*', 'W9', 'pb']], ['lab', 'C', ['m', '*', 'W9', 'kg']],
    ['ej', 'C', ['m', '*', 'P1KQ2', 'ptkqC']], ['ej', 'C', ['m', '*', 'P1KQ2', 'bdgGj']],
    ['asp', 'C', ['m', '*', '345', 'fTx']], ['asp', 'C', ['m', '*', '345678', 'ptkbdg']], ['asp', 'C', ['m', '*', '678', 'vDG']],
    ['pnv', 'C', ['m', 'n', 'ptk', 'bdg']],
    ['nd', 'C', ['r', '*', ['mb', 'nd'], ['mm', 'nn']]], ['nd', 'C', ['r', '*', ['kt', 'pt'], ['tt', 'tt']]],

    ['vfin', 'V', ['m', 'f', 'a', 'e']], ['vfin', 'V', ['m', 'f', 'a', 'o']], ['vfin', 'V', ['m', 'f', 'e', 'a']], ['vfin', 'V', ['m', 'f', 'e', 'i']],
    ['vfin', 'V', ['m', 'f', 'o', 'u']], ['vfin', 'V', ['m', 'f', 'o', 'a']], ['vfin', 'V', ['m', 'f', 'u', 'o']], ['vfin', 'V', ['m', 'f', 'i', 'e']],
    ['vfin', 'V', ['m', 'f', 'u', 'a']],
    ['vuns', 'V', ['m', 'u', 'eo', 'iu']], ['vuns', 'V', ['m', 'u', 'iu', 'eo']], ['vuns', 'V', ['m', 'u', 'a', 'e']], ['vuns', 'V', ['m', 'u', 'a', 'o']],
    ['vuns', 'V', ['m', 'u', 'o', 'a']], ['vuns', 'V', ['m', 'u', 'e', 'a']],
    ['vstr', 'V', ['m', 's', 'a', 'o']], ['vstr', 'V', ['m', 's', 'a', 'e']], ['vstr', 'V', ['m', 's', 'e', 'i']], ['vstr', 'V', ['m', 's', 'o', 'u']],
    ['vstr', 'V', ['m', 's', 'u', 'o']], ['vstr', 'V', ['m', 's', 'i', 'e']], ['vstr', 'V', ['m', 's', 'o', 'a']], ['vstr', 'V', ['m', 's', 'e', 'a']],
    ['vall', 'V', ['m', '*', 'eo', 'iu']], ['vall', 'V', ['m', '*', 'iu', 'eo']], ['vall', 'V', ['m', '*', 'o', 'a']], ['vall', 'V', ['m', '*', 'u', 'o']],
    ['vall', 'V', ['m', '*', 'e', 'a']], ['vall', 'V', ['m', '*', 'u', 'i']], ['vall', 'V', ['m', '*', 'o', 'u']], ['vall', 'V', ['m', '*', 'e', 'i']],
    ['vall', 'V', ['m', '*', 'i', 'e']],
    ['brk', 'V', ['brk', 'e', 'ie']], ['brk', 'V', ['brk', 'o', 'uo']], ['brk', 'V', ['brk', 'e', 'ia']], ['brk', 'V', ['brk', 'o', 'ua']],
    ['brk', 'V', ['brk', 'i', 'ai']], ['brk', 'V', ['brk', 'u', 'au']], ['brk', 'V', ['brk', 'a', 'ae']], ['brk', 'V', ['brk', 'e', 'ei']],
    ['brk', 'V', ['brk', 'o', 'ou']],
    ['lng', 'V', ['m', '*', 'AEIOU', 'aeiou']], ['lng', 'V', ['m', '*', 'EO', 'IU']], ['lng', 'V', ['r', '*', ['I', 'U'], ['ai', 'au']]],
    ['lng', 'V', ['r', '*', ['E', 'O'], ['ei', 'ou']]], ['lng', 'V', ['m', '*', 'A', 'O']], ['lng', 'V', ['len']],
    ['dip', 'V', ['r', '*', ['ai', 'au'], ['e', 'o']]], ['dip', 'V', ['r', '*', ['ai', 'au'], ['E', 'O']]], ['dip', 'V', ['r', '*', ['ei', 'ou'], ['i', 'u']]],
    ['dip', 'V', ['r', '*', ['ei', 'ou'], ['ai', 'au']]], ['dip', 'V', ['r', '*', ['oi', 'ui'], ['e', 'i']]], ['dip', 'V', ['r', '*', ['ia', 'ua'], ['e', 'o']]],
    ['dip', 'V', ['r', '*', ['ie', 'uo'], ['i', 'u']]], ['dip', 'V', ['r', '*', ['ae', 'oe'], ['e', 'e']]], ['dip', 'V', ['r', '*', ['ae', 'ao'], ['ai', 'au']]],
    ['dip', 'V', ['r', '*', ['ea', 'eo', 'oa'], ['ia', 'io', 'ua']]], ['dip', 'V', ['r', '*', ['eu', 'iu'], ['u', 'u']]],
    ['um', 'V', ['um', 'iI', 'a', 'e']], ['um', 'V', ['um', 'uU', 'a', 'o']], ['um', 'V', ['um', 'aA', 'i', 'e']], ['um', 'V', ['um', 'aA', 'u', 'o']],
    ['um', 'V', ['um', 'iI', 'u', 'i']], ['um', 'V', ['um', 'iI', 'o', 'e']],

    ['end', 'S', ['apo']], ['end', 'S', ['par', 'a']], ['end', 'S', ['par', 'e']], ['end', 'S', ['par', 'u']], ['end', 'S', ['par', 'i']],
    ['end', 'S', ['par', 'o']], ['end', 'S', ['fcl']], ['end', 'S', ['d', 'f', 's']],
    ['ini', 'S', ['pro', 'h']], ['ini', 'S', ['pro', 'y']], ['ini', 'S', ['pro', 'w']], ['ini', 'S', ['esc', 'e']], ['ini', 'S', ['esc', 'i']],
    ['ini', 'S', ['epi']], ['ini', 'S', ['ccs']],
    ['gem', 'S', ['deg']], ['nasl', 'S', ['nasl']], ['hia', 'S', ['d', 'v', 'h']], ['hia', 'S', ['d', 'v', 'y']],
  ];

  // ------------------------------------------------------------------ compiling a language for use
  const CACHE = new WeakMap();
  let fallbackC = null;

  function compile(lang) {
    const w = lang.w || {};
    const c = { lang: lang };
    c.C = str(lang.C); c.V = str(lang.V);
    if (!c.C || !c.V || lang.v !== 1) throw new Error('bad lang');
    for (let i = 0; i < c.C.length; i++) if (!(CLS[c.C.charCodeAt(i) & 127] & CONS)) throw new Error('bad lang');
    for (let i = 0; i < c.V.length; i++) if (SBASE.indexOf(c.V.charAt(i)) < 0) throw new Error('bad lang');
    const strs = (x) => arr(x).filter((s) => typeof s === 'string' && s.length > 0);
    c.D = strs(lang.D); c.on = strs(lang.on); c.co = strs(lang.co); c.cf = strs(lang.cf);
    if (!c.cf.length) c.cf = c.co;
    c.w = {
      no: num(w.no, 0.1), noM: num(w.noM, 0), on: num(w.on, 0), onM: num(w.onM, 1), co: num(w.co, 0), coF: num(w.coF, 0),
      dip: num(w.dip, 0), long: num(w.long, 0), gem: num(w.gem, 0),
    };
    const fc = Math.max(0.5, num(w.fc, 3)), fv = Math.max(0.5, num(w.fv, 2));
    c.cC = cumRank(c.C.length, fc); c.cV = cumRank(c.V.length, fv);
    c.cD = cumRank(c.D.length || 1, 2); c.cOn = cumRank(c.on.length || 1, 3);
    c.cCo = cumRank(c.co.length || 1, 2); c.cCf = cumRank(c.cf.length || 1, c.cf === c.co ? 2 : 1);
    const syl = arr(lang.syl);
    c.cSyl = cumOf(syl.length ? syl : [0, 1], Math.max(1, Math.min(4, syl.length || 2)));
    c.mono = !(syl[1] > 0 || syl[2] > 0 || syl[3] > 0);
    c.harm = lang.harm | 0;
    c.neutral = str(lang.neutral);
    c.stress = lang.stress === 'initial' ? 0 : lang.stress === 'final' ? 2 : 1;
    c.med = Math.max(1, lang.med | 0);
    c.noInit = str(lang.noInit);
    c.ban = {};
    for (const b of strs(lang.ban)) c.ban[b] = 1;
    c.hasBan = strs(lang.ban).length > 0;
    c.hiatus = c.w.noM > 0 || c.D.length > 0;
    // spelling
    c.sp = new Array(128); c.slen = new Uint8Array(128);
    const over = lang.sp && typeof lang.sp === 'object' ? lang.sp : {};
    for (let k = 0; k < 128; k++) {
      const ch = String.fromCharCode(k);
      let s = typeof over[ch] === 'string' ? over[ch] : (SPELL[ch] || ch);
      if (CLS[k] & LONG) s = ch.toLowerCase();
      c.sp[k] = s; c.slen[k] = k === 39 ? 0 : s.replace(/'/g, '').length;
    }
    c.lvTab = LV[lang.lv] || LV.none;
    c.lvMark = c.lvTab !== LV.none && c.lvTab !== LV.dbl;
    if (c.lvTab === LV.dbl) for (let j = 0; j < 5; j++) c.slen[LBASE.charCodeAt(j)] = 0;
    c.cq = lang.cq ? 1 : 0; c.xk = lang.xk ? 1 : 0; c.fe = lang.fe ? 1 : 0;
    c.gemOK = new Uint8Array(128);
    for (let i = 0; i < c.C.length; i++) { const k = c.C.charCodeAt(i); if (c.sp[k].length === 1 && "hwyq'".indexOf(c.C.charAt(i)) < 0) c.gemOK[k] = 1; }
    if (!arr(lang.shifts).every(validRule)) throw new Error('bad lang');
    c.shifts = arr(lang.shifts).map(compileRule);
    // lexicon
    c.roots = strs(lang.roots);
    c.god = str(lang.god);
    c.linkV = c.V.charAt(0);
    c.linkC = 'n';
    for (const k of 'nrltkmsh') if (c.C.indexOf(k) >= 0) { c.linkC = k; break; }
    const P = lang.place || {}, Q = lang.person || {}, K = lang.cult || {};
    const ends = strs(P.ends), parts = strs(P.parts);
    const t = arr(P.t).slice(0, 6);
    while (t.length < 6) t.push(0);
    if (!ends.length) t[0] = 0;
    if (!parts.length) t[1] = 0;
    if (c.roots.length < 2) { t[2] = 0; t[3] = 0; }
    if (c.mono) t[5] = 0;
    c.redup = t[5] > 0;
    if (!(t[0] > 0 || t[1] > 0 || t[2] > 0 || t[3] > 0 || t[4] > 0 || t[5] > 0)) t[4] = 1;
    c.place = {
      cT: cumOf(t, 6), ends: ends, cE: cumRank(ends.length || 1, 2.5), parts: parts, cP: cumRank(parts.length || 1, 2),
      sj: str(P.sj), sep: str(P.sep), join: str(P.join), after: P.after ? 1 : 0, pe: num(P.pe, 0), lex: num(P.lex, 0.4),
      cN: cumOf(arr(P.n).length ? P.n : [0, 1], Math.max(1, Math.min(3, arr(P.n).length || 2))),
      cNb: cumOf(arr(P.nb).length ? P.nb : [0, 1, 1], Math.max(1, Math.min(4, arr(P.nb).length || 3))),
    };
    const mx = arr(lang.mx);
    c.maxW = Math.max(6, Math.min(14, num(mx[0], TUNE.names.word[0]) | 0));
    c.maxG = Math.max(2, Math.min(7, num(mx[1], TUNE.names.word[1]) | 0));
    const pe = arr(Q.ends).filter((s) => typeof s === 'string');
    const pt = arr(Q.t).slice(0, 4);
    while (pt.length < 4) pt.push(0);
    if (!pe.length) pt[0] = 0;
    if (!c.god || c.mono) pt[3] = 0;
    if (!(pt[0] > 0 || pt[1] > 0 || pt[2] > 0)) pt[2] = 1;
    c.person = {
      cT: cumOf(pt, 4), ends: pe, cE: cumRank(pe.length || 1, 1.8), join: str(Q.join), lex: num(Q.lex, 0.25),
      cN: cumOf(arr(Q.n).length ? Q.n : [0, 1], Math.max(1, Math.min(3, arr(Q.n).length || 2))),
    };
    c.person.cN2 = c.mono ? c.person.cN : cumOf([1, 1], 2);   // syllables in each half of a two-stem name
    c.cult ={ lex: num(K.lex, 0.5), cN: cumOf(arr(K.n).length ? K.n : [0, 1], Math.max(1, Math.min(3, arr(K.n).length || 2))) };
    const dm = lang.dem || {};
    c.dem = { adj: str(dm.adj) || 'an', pl: typeof dm.pl === 'string' ? dm.pl : 'i', link: str(dm.link) || 'n' };
    c.godSp = c.god ? capWord(spell(c, c.god)) : '';        // the god's name as written: no town, person or people may bear it
    return c;
  }

  function get(lang) {
    if (lang && typeof lang === 'object') {
      let c = CACHE.get(lang);
      if (c) return c;
      try { c = compile(lang); } catch (e) { c = null; }
      if (!c) c = fallback();
      CACHE.set(lang, c);
      return c;
    }
    return fallback();
  }
  function fallback() {
    if (!fallbackC) fallbackC = compile(Names.language(DM.U.rng(0x5eed), null, 'star'));
    return fallbackC;
  }

  // ------------------------------------------------------------------ making words
  function pickV(c, rng, hc) {
    let v = c.V.charAt(pick(rng, c.cV));
    if (hc) {
      for (let t = 0; t < 3 && !fits(c, v, hc); t++) v = c.V.charAt(pick(rng, c.cV));
      if (!fits(c, v, hc)) v = hc === 2 ? (v === 'u' ? 'i' : 'e') : (v === 'i' ? 'u' : 'a');
    }
    return v;
  }
  // vowel harmony class: 1 back (a o u), 2 front (e i); neutral vowels fit both
  function fits(c, v, hc) {
    for (let i = 0; i < v.length; i++) {
      const ch = v.charAt(i);
      if (c.neutral.indexOf(ch.toLowerCase()) >= 0) continue;
      if (((CLS[ch.charCodeAt(0) & 127] & FRONT) ? 2 : 1) !== hc) return false;
    }
    return true;
  }
  function classOf(c, v) {
    for (let i = 0; i < v.length; i++) {
      const ch = v.charAt(i);
      if (c.neutral.indexOf(ch.toLowerCase()) < 0) return (CLS[ch.charCodeAt(0) & 127] & FRONT) ? 2 : 1;
    }
    return 0;
  }

  // Two consonants that do not sit side by side inside a word: the same sound twice (doubling is decided elsewhere),
  // tl before anything, and two stops made in the same place of the mouth (dt, kg, qk, bp).
  const SPOT = new Uint8Array(128);
  SPOT[112] = SPOT[98] = 1; SPOT[116] = SPOT[100] = 2; SPOT[107] = SPOT[103] = SPOT[113] = 3;      // p b · t d · k g q
  function clash(x, y) {
    return x === y || x === 76 || (SPOT[x & 127] !== 0 && SPOT[x & 127] === SPOT[y & 127]);
  }

  // a word of n syllables in the proto-language (before this language's sound shifts)
  function protoWord(c, rng, n) {
    const W = c.w, stress = c.stress === 0 ? 0 : c.stress === 2 ? n - 1 : (n > 1 ? n - 2 : 0);
    let out = '', coda = 0, lastOn = '', hc = 0, echo = '';
    for (let i = 0; i < n; i++) {
      // ---- onset
      let on = '';
      if (coda > 0 || rng.f() >= (i === 0 ? W.no : W.noM)) {
        if (c.on.length && coda < 2 && rng.f() < (i === 0 ? W.on : W.on * W.onM)) on = c.on[pick(rng, c.cOn)];
        else {
          on = c.C.charAt(pick(rng, c.cC));
          if (on === lastOn && rng.f() < 0.6) on = c.C.charAt(pick(rng, c.cC));
          if (i === 0) {
            for (let t = 0; t < 4 && c.noInit.indexOf(on) >= 0; t++) on = c.C.charAt(pick(rng, c.cC));
            if (c.noInit.indexOf(on) >= 0) on = c.C.charAt(0);
          } else if (on === "'" && coda > 0) on = c.C.charAt(0);                 // a glottal stop only between vowels
          lastOn = on;
          if (i > 0 && coda === 0 && W.gem > 0 && rng.f() < W.gem && c.gemOK[on.charCodeAt(0)]) on += on;
        }
        if (coda > 0) {
          // keep the cluster in the middle of the word within the language's limit and within three letters
          const tail = out.slice(out.length - coda), last = tail.charCodeAt(coda - 1);
          let bad = coda + on.length > c.med || spLen(c, tail) + spLen(c, on) > 3 || clash(last, on.charCodeAt(0));
          if (bad && on.length > 1) {
            on = on.charAt(0);
            bad = coda + 1 > c.med || spLen(c, tail) + spLen(c, on) > 3 || clash(last, on.charCodeAt(0));
          }
          if (bad) { out = out.slice(0, out.length - coda); coda = 0; }
        }
      }
      // ---- nucleus
      const hi = on === '' && i > 0 ? out.charAt(out.length - 1).toLowerCase() : '';      // the vowel before, when two vowels meet
      let v;
      if (i === stress && !hi && W.long > 0 && rng.f() < W.long) v = LBASE.charAt(SBASE.indexOf(pickV(c, rng, hc)));
      else if (c.D.length && !hi && rng.f() < W.dip) { v = c.D[pick(rng, c.cD)]; if (hc && !fits(c, v, hc)) v = pickV(c, rng, hc); }
      else v = pickV(c, rng, hc);
      if (c.harm === 2) {                                   // echo vowels: later syllables mostly repeat the first
        if (!echo) echo = v.charAt(0).toLowerCase();
        else if (v.length === 1 && !(CLS[v.charCodeAt(0)] & LONG) && rng.f() < 0.62) v = echo;
      }
      if (hi && v.charAt(0).toLowerCase() === hi) {
        for (let t = 0; t < 3 && v.charAt(0).toLowerCase() === hi; t++) v = pickV(c, rng, hc);
        if (v.charAt(0).toLowerCase() === hi) v = c.V.charAt(c.V.charAt(0) === hi ? 1 % c.V.length : 0);
        if (v === hi) { on = c.C.charAt(0); }               // one-vowel language: no hiatus after all
      }
      if (c.hasBan && on) {
        const k = on.charAt(on.length - 1);
        for (let t = 0; t < 4 && c.ban[k + v.charAt(0).toLowerCase()]; t++) v = pickV(c, rng, hc);
      }
      if (c.harm === 1 && !hc) hc = classOf(c, v);
      // ---- coda
      const lastSyl = i === n - 1;
      coda = 0;
      if (rng.f() < (lastSyl ? W.coF : W.co)) {
        let k = lastSyl ? c.cf[pick(rng, c.cCf)] : c.co[pick(rng, c.cCo)];
        if (k) {
          if (k.length > 1 && (v.length > 1 || (CLS[v.charCodeAt(0)] & LONG))) k = k.charAt(0);     // a heavy vowel takes one consonant
          out += on + v + k; coda = k.length;
          continue;
        }
      }
      out += on + v;
    }
    return out;
  }

  // a fresh word of n syllables as this language says it
  function surf(c, rng, n) {
    let s = protoWord(c, rng, n);
    const sh = c.shifts;
    for (let i = 0; i < sh.length; i++) s = sh[i](s, true, true);
    return s;
  }
  function shifted(c, s, ini, fin) {
    const sh = c.shifts;
    for (let i = 0; i < sh.length; i++) s = sh[i](s, ini, fin);
    return s;
  }

  // a stem: one of the language's recurring roots, or a fresh word
  function stemOf(c, rng, pLex, cumN) {
    if (c.roots.length && rng.f() < pLex) return c.roots[(rng.f() * c.roots.length) | 0];
    return surf(c, rng, 1 + pick(rng, cumN));
  }

  // suffix vowels follow the stem in a harmony language
  function harmonize(c, stem, e) {
    if (c.harm !== 1 || !e) return e;
    let hc = 0;
    for (let i = stem.length - 1; i >= 0 && !hc; i--) {
      const ch = stem.charAt(i);
      if ((CLS[ch.charCodeAt(0) & 127] & VOW) && c.neutral.indexOf(ch.toLowerCase()) < 0) hc = (CLS[ch.charCodeAt(0) & 127] & FRONT) ? 2 : 1;
    }
    if (!hc) return e;
    let out = '';
    for (let i = 0; i < e.length; i++) {
      const ch = e.charAt(i), k = ch.charCodeAt(0) & 127;
      if (!(CLS[k] & VOW) || c.neutral.indexOf(ch.toLowerCase()) >= 0) { out += ch; continue; }
      const front = (CLS[k] & FRONT) !== 0;
      if (hc === 2 && !front) out += ch === 'u' ? 'i' : ch === 'U' ? 'I' : (CLS[k] & LONG) ? 'E' : 'e';
      else if (hc === 1 && front) out += ch === 'i' ? 'u' : ch === 'I' ? 'U' : (CLS[k] & LONG) ? 'A' : 'a';
      else out += ch;
    }
    return out;
  }

  // join two pieces into one word, smoothing the seam
  function fuse(c, a, b) {
    if (!a) return b;
    if (!b) return a;
    const x = a.charCodeAt(a.length - 1) & 127, y = b.charCodeAt(0) & 127;
    if ((CLS[x] & VOW) && (CLS[y] & VOW)) {
      const same = a.charAt(a.length - 1).toLowerCase() === b.charAt(0).toLowerCase();
      const heavy = a.length > 1 && (CLS[a.charCodeAt(a.length - 2) & 127] & VOW) !== 0 || (b.length > 1 && (CLS[b.charCodeAt(1) & 127] & VOW) !== 0);
      if (!same && c.hiatus && !heavy && !(CLS[x] & LONG) && !(CLS[y] & LONG)) return a + b;
      let vowels = 0;
      for (let i = 0; i < a.length - 1; i++) if (CLS[a.charCodeAt(i) & 127] & VOW) vowels++;
      if (vowels > 0 && a.length >= 3) return a.slice(0, a.length - 1) + b;              // the stem's last vowel gives way
      return a + c.linkC + b;
    }
    if ((CLS[x] & CONS) && (CLS[y] & CONS)) {
      let ta = 1, lb = 1;
      while (ta < a.length && (CLS[a.charCodeAt(a.length - 1 - ta) & 127] & CONS)) ta++;
      while (lb < b.length && (CLS[b.charCodeAt(lb) & 127] & CONS)) lb++;
      if (x === y) {
        if (ta === 1 && lb === 1 && c.gemOK[x] && c.w.gem > 0) return a + b;
        return b.length > 1 ? a + b.slice(1) : a;
      }
      if (clash(x, y)) return ta === 1 && a.length > 2 ? a.slice(0, a.length - 1) + b : a + c.linkV + b;      // coatl + tepek → coatepek
      if (ta + lb > c.med || spLen(c, a.slice(a.length - ta)) + spLen(c, b.slice(0, lb)) > 3) return a + c.linkV + b;
    }
    return a + b;
  }
  function capSpell(c, s) { return capWord(spell(c, s)); }

  function placeOnce(c, rng) {
    const P = c.place;
    switch (pick(rng, P.cT)) {
      case 0: {                                             // a stem with one of the favourite endings
        const stem = stemOf(c, rng, P.lex, P.cN), e = harmonize(c, stem, P.ends[pick(rng, P.cE)]);
        return P.sj ? capSpell(c, stem) + P.sj + capSpell(c, e) : capSpell(c, fuse(c, stem, e));
      }
      case 1: {                                             // a particle and a stem: 'Tel Asha'
        const p = P.parts[pick(rng, P.cP)];
        let stem = stemOf(c, rng, P.lex, P.cN);
        if (P.ends.length && rng.f() < P.pe) stem = fuse(c, stem, harmonize(c, stem, P.ends[pick(rng, P.cE)]));
        if (!P.sep) return capSpell(c, P.after ? fuse(c, stem, p) : fuse(c, p, stem));
        return P.after ? capSpell(c, stem) + P.sep + capSpell(c, p) : capSpell(c, p) + P.sep + capSpell(c, stem);
      }
      case 2: {                                             // two stems
        const a = stemOf(c, rng, P.lex, P.cN), b = otherRoot(c, rng, a, a);
        return P.join ? capSpell(c, a) + P.join + capSpell(c, b) : capSpell(c, fuse(c, a, b));
      }
      case 3: {                                             // three short stems
        const R = c.roots, a = R[(rng.f() * R.length) | 0], b = otherRoot(c, rng, a, a), d = otherRoot(c, rng, a, b);
        return P.join ? capSpell(c, a) + P.join + capSpell(c, b) + P.join + capSpell(c, d) : capSpell(c, fuse(c, fuse(c, a, b), d));
      }
      case 5: {                                             // a short word said twice: 'Lomilomi', 'Bora Bora'
        const s = capSpell(c, surf(c, rng, 2));
        if (s.length > 5) return s;
        return s.length < 4 || rng.f() < 0.5 ? s + s.toLowerCase() : s + ' ' + s;
      }
    }
    return capSpell(c, surf(c, rng, 1 + pick(rng, P.cNb)));   // a bare word
  }
  // a root of the language other than the two given (compounds do not repeat themselves)
  function otherRoot(c, rng, a, b) {
    const R = c.roots;
    let s = R[(rng.f() * R.length) | 0];
    for (let t = 0; t < 4 && (s === a || s === b); t++) s = R[(rng.f() * R.length) | 0];
    return s;
  }

  function personOnce(c, rng) {
    const P = c.person;
    switch (pick(rng, P.cT)) {
      case 0: {
        const stem = stemOf(c, rng, P.lex, P.cN);
        return capSpell(c, fuse(c, stem, harmonize(c, stem, P.ends[pick(rng, P.cE)])));
      }
      case 1: {                                             // two stems, each short: a name is one breath
        const a = stemOf(c, rng, P.lex, P.cN2);
        let b = stemOf(c, rng, P.lex, P.cN2);
        if (b === a) b = stemOf(c, rng, 0, P.cN2);
        return P.join ? capSpell(c, a) + P.join + capSpell(c, b) : capSpell(c, fuse(c, a, b));
      }
      case 3: {                                             // named for the god: the god's word with an ending, or beside a short root
        const g = c.god, R = c.roots;
        let s = '';
        if (P.ends.length && rng.f() < 0.45) s = fuse(c, g, harmonize(c, g, P.ends[pick(rng, P.cE)]));
        else if (R.length) { const r = R[(rng.f() * R.length) | 0]; if (r.length <= 5) s = rng.f() < 0.5 ? fuse(c, g, r) : fuse(c, r, g); }
        if (s && s !== g) return capSpell(c, s);
        break;                                              // nothing short enough came up: a plain name
      }
    }
    return capSpell(c, surf(c, rng, 1 + pick(rng, P.cN)));
  }

  // last resorts: plain consonant-vowel words in the language, then a word that cannot fail
  const SAFE_C = 'kmnlrst', SAFE_V = 'aeiou';
  function safeName(c, rng, lo, hi) {
    for (let t = 0; t < 24; t++) {
      let s = '';
      const n = hi < 5 ? 2 : 2 + (t & 1);
      for (let i = 0; i < n; i++) {
        let k = c.C.charAt(pick(rng, c.cC));
        if (k === "'" || c.slen[k.charCodeAt(0)] !== 1) k = c.linkC;
        s += k + c.V.charAt(pick(rng, c.cV));
      }
      const name = capSpell(c, shifted(c, s, true, true));
      if (fine(name, lo, hi)) return name;
    }
    for (;;) {
      let s = '';
      for (let i = 0; i < 2; i++) s += SAFE_C.charAt(rng.int(0, SAFE_C.length - 1)) + SAFE_V.charAt(rng.int(0, SAFE_V.length - 1));
      s = capWord(s + (hi > 4 ? 'n' : ''));
      if (fine(s, lo, hi)) return s;
    }
  }

  // a word that is one piece said twice ('Rozaroza'): welcome only in languages that double words on purpose
  function doubled(s) {
    const n = s.length, h = n >> 1;
    if (n < 6 || (n & 1)) return false;
    for (let i = 0; i < h; i++) {
      const a = s.charCodeAt(i), b = s.charCodeAt(i + h);
      if (a !== b && !(i === 0 && s.charAt(0).toLowerCase() === s.charAt(h))) return false;
    }
    return true;
  }

  // Names.stats counts how the generators are doing (diagnostics only; nothing reads it back)
  const STATS = { names: 0, rerolls: 0, fallbacks: 0 };

  function placeName(c, rng) {
    const T = TUNE.names;
    STATS.names++;
    for (let t = 0; t < T.tries; t++) {
      const s = placeOnce(c, rng);
      if (fine(s, T.placeMin, T.maxLen) && !wordy(s, c.maxW, c.maxG) && (c.redup || !doubled(s)) && s !== c.godSp) return s;
      STATS.rerolls++;
    }
    STATS.fallbacks++;
    return safeName(c, rng, T.placeMin, T.maxLen);
  }
  function personName(c, rng) {
    const T = TUNE.names, hi = Math.min(c.maxW, T.personLen);
    STATS.names++;
    for (let t = 0; t < T.tries; t++) {
      const s = personOnce(c, rng);
      if (fine(s, T.personMin, 12) && !wordy(s, hi, 4) && (c.redup || !doubled(s)) && s !== c.godSp) return s;
      STATS.rerolls++;
    }
    STATS.fallbacks++;
    return safeName(c, rng, T.personMin, 8);
  }

  // ------------------------------------------------------------------ public: names
  Names.cap = function (s) {
    if (s === null || s === undefined) return '';
    s = String(s);
    for (let i = 0; i < s.length; i++) {
      const ch = s.charAt(i), up = ch.toUpperCase();
      if (up !== ch.toLowerCase()) return s.slice(0, i) + up + s.slice(i + 1);       // the first letter
    }
    return s;
  };

  Names.ok = function (name) { return typeof name === 'string' && fine(name, TUNE.names.minLen, TUNE.names.maxLen); };
  Names.stats = STATS;

  Names.word = function (lang, rng, minSyl, maxSyl) {
    const c = get(lang), T = TUNE.names;
    rng = useRng(rng);
    let lo = minSyl === undefined ? 1 : minSyl | 0, hi = maxSyl === undefined ? 3 : maxSyl | 0;
    lo = lo < 1 ? 1 : lo > 5 ? 5 : lo;
    hi = hi < lo ? lo : hi > 5 ? 5 : hi;
    STATS.names++;
    for (let t = 0; t < T.tries; t++) {
      // a word too short to stand alone is tried again one syllable longer
      const n = Math.min(5, rng.int(lo, hi) + (t > 3 ? 1 : 0));
      const s = spell(c, surf(c, rng, n));
      if (fine(s, T.minLen, T.maxLen)) return s;
      STATS.rerolls++;
    }
    STATS.fallbacks++;
    return safeName(c, rng, T.minLen, T.maxLen).toLowerCase();
  };

  Names.place = function (lang, rng) { return placeName(get(lang), useRng(rng)); };

  Names.person = function (lang, rng) { return personName(get(lang), useRng(rng)); };

  // English-style endings on a name, written as the chronicle's translator would: Umar → Umaran, Asha → Ashan
  function addSuffix(name, suf, link) {
    if (!suf) return name;
    const lastCode = name.charCodeAt(name.length - 1);
    const last = String.fromCharCode(lastCode < 128 ? lastCode : (FOLD[lastCode] || 120)).toLowerCase();
    const f = suf.charAt(0);
    if (SBASE.indexOf(last) < 0 || SBASE.indexOf(f) < 0) return name + suf;
    if (last === f) return name + suf.slice(1);                                  // Asha + an → Ashan, Kali + ite → Kalite
    if ((last === 'i' || last === 'u') && f !== 'e') return name + suf;          // Kelian, Keluan
    if (f === 'a' && name.length > 3) return name.slice(0, name.length - 1) + suf;      // Kelo + an → Kelan
    return name + link + suf;                                                    // Asha + ite → Ashanite
  }

  // `avoid` (optional, not in the spec): an array of culture names already taken in this world. Related languages share
  // roots, so a daughter people may otherwise come to bear its parent's name.
  Names.culture = function (lang, rng, avoid) {
    const c = get(lang), T = TUNE.names, D = c.dem;
    rng = useRng(rng);
    const taken = Array.isArray(avoid) ? avoid : null;
    let name = '';
    for (let t = 0; t < T.tries * 2; t++) {
      name = capSpell(c, stemOf(c, rng, c.cult.lex, c.cult.cN));
      if (!fine(name, T.cultMin, 8) || wordy(name, 8, 3) || !/[^aeiou']/.test(OKF) || /[aeiou]{3}/.test(OKF) || name === c.godSp ||
        (taken && taken.indexOf(name) >= 0)) { name = ''; continue; }
      const adj = addSuffix(name, D.adj, D.link);
      const pl = D.pl === '+s' ? adj + 's' : D.pl === '=' ? adj : addSuffix(name, D.pl, D.link);
      // the derived forms are checked like names, and must not land on a people of ours ('Korean', 'Swedish')
      if (okName(adj, T.minLen, 12) && !isEnglish(OKF) && okName(pl, T.minLen, 10) && !isEnglish(OKF)) return { name: name, adj: adj, plural: 'the ' + pl };
    }
    // plainest forms
    for (let t = 0; t < 8; t++) {
      name = safeName(c, rng, T.cultMin, 6);
      const adj = addSuffix(name, 'an', 'n');
      if (okName(adj, T.minLen, 12) && !(taken && taken.indexOf(name) >= 0)) return { name: name, adj: adj, plural: 'the ' + name };
    }
    return { name: name, adj: name, plural: 'the ' + name };
  };

  Names.god = function (lang, rng) {
    const c = get(lang), T = TUNE.names;
    const s = c.god ? capSpell(c, c.god) : '';
    if (fine(s, T.minLen, 10)) return s;
    // a language without a usable god-word (hand-made or damaged): coin one from its own seed-independent stream
    return safeName(c, DM.U.rng(0x60d), T.minLen, 8);
  };

  // ------------------------------------------------------------------ public: world names
  // A world's default name belongs to no language of its peoples: soft, open syllables and a handful of endings that
  // sound like a place in an old star catalogue. (Entries are repeated to weight them.)
  const PL_ON = ['', '', '', '', '', '', '', 'l', 'l', 'l', 'm', 'm', 'n', 'n', 'n', 'r', 'r', 's', 's', 's', 'v', 'v', 'v', 't', 't', 'k', 'k',
    'd', 'h', 'h', 'c', 'c', 'th', 'th', 'th', 'z', 'ph', 'y', 'sh', 'tr', 'dr'];
  const PL_V = ['a', 'a', 'a', 'a', 'a', 'e', 'e', 'e', 'e', 'e', 'i', 'i', 'i', 'o', 'o', 'o', 'o', 'u', 'y', 'ae', 'ae', 'au', 'ei', 'ai'];
  const PL_V2 = ['a', 'e', 'i', 'o', 'a', 'e', 'i', 'o', 'u', 'a', 'e'];
  const PL_MID = ['l', 'l', 'l', 'r', 'r', 'r', 'n', 'n', 'n', 'm', 'm', 's', 's', 'th', 'th', 'v', 'v', 'd', 'd', 't', 'k', 'z', 'ph', 'sh',
    'll', 'rr', 'nn', 'ss', 'nd', 'nd', 'ld', 'rn', 'rd', 'lm', 'rs', 'nt', 'nth', 'lv', 'lth', 'rth', 'ndr', 'lt', 'rv'];
  const PL_END = ['a', 'a', 'a', 'a', 'ia', 'ia', 'ia', 'ia', 'is', 'is', 'on', 'on', 'or', 'or', 'ar', 'ar', 'as', 'el', 'el', 'iel', 'en', 'en',
    'eth', 'an', 'an', 'ion', 'une', 'ara', 'ael', 'yr', 'yn', 'ys', 'ath', 'oth', 'ea', 'ira', 'ora', 'ene', 'ine', 'aris', 'eon', 'essa',
    'elle', 'yra', 'ane', 'ais', 'alis', 'ande', 'anthe', 'ador', 'amar'];
  // worlds that already have owners
  const PL_REAL = new RegExp('^(terra|earth|mars|venus|pluto|saturn|uranus|neptune|mercury|luna|ceres|eris|titan|europa|gaia|gaea|tellus|sol|' +
    'vesta|pallas|juno|io|charon|triton|ganymede|callisto|vulcan|krypton|pandora|arrakis|tatooine|hoth|endor|naboo|arda|narnia|thedas|' +
    'azeroth|tamriel|toril|krynn|athas|roshar|solaris|solaria|aurora|helion|helios|selene|rhea|dione|mimas|tethys|oberon|ariel|miranda|' +
    'umbriel|phobos|deimos|sedna|makemake|haumea|kepler|sirius|vega|altair|rigel|antares|arcturus|polaris|mira|lyra|orion|andromeda|' +
    'avalon|eden|elysium|arcadia|atlantis|lemuria|asgard|midgard|utopia|thalassa|aether|ether|aeon|alderaan|coruscant|caladan|trantor|' +
    'gethen|anarres|urras|pern|thessia|palaven|aiur|nirn|mundus|spira|ivalice|eorzea|erathia|enroth|golarion|eberron|oerth)$');
  // a pile of vowels, y beside a vowel, a stutter (lal, nen), three consonants to begin with
  const PL_UGLY = /[aeiouy]{3}|y[aeiouy]|[aeiou]y|([^aeiouy])[aeiouy]+\1|^[^aeiouy]{3}/;
  function pl(rng, list) { return list[(rng.f() * list.length) | 0]; }
  // two neighbouring groups of consonants that echo each other: the same sound at the join, or one cluster inside the other
  function echo(a, b) {
    if (!a || !b) return false;
    return a.charAt(a.length - 1) === b.charAt(0) || (a.length > 1 && b.indexOf(a) >= 0) || (b.length > 1 && a.indexOf(b) >= 0);
  }

  Names.planet = function (rng) {
    rng = useRng(rng);
    for (let t = 0; t < 60; t++) {
      const on = pl(rng, PL_ON), v = pl(rng, PL_V);
      const long = rng.f() < 0.4;
      const m1 = long ? pl(rng, PL_MID) : '', v2 = long ? pl(rng, PL_V2) : '';
      const m2 = pl(rng, PL_MID), end = pl(rng, PL_END);
      const s = on + v + m1 + v2 + m2 + end;
      if (s.length < 5 || s.length > 9 || PL_REAL.test(s) || PL_UGLY.test(s)) continue;
      if (echo(on, m1 || m2) || echo(m1, m2) || (on.length > 1 && m2.indexOf(on) >= 0)) continue;
      const name = capWord(s);
      if (fine(name, 5, 9)) return name;
    }
    return 'Velara';
  };

  // ------------------------------------------------------------------ recipes for new languages
  // Each returns the sounds, syllable shapes and naming habits of one kind of language, varied by the rng. They are
  // patterned on the range of real-world phonologies (an island language of few consonants, a language of ejectives,
  // one of consonant clusters, one of vowel harmony …) but every word is generated; nothing is copied.
  // st: t = place templates [stem+ending, particle+stem, stem+stem, three stems, bare word]; sj / sep / join = what is
  // written between stem and ending / particle and stem / two stems; n, nb = syllables in a stem / a bare word;
  // es, ps, pes = shapes of place endings, particles, person endings (C consonant, V vowel, D diphthong, K coda,
  // W a whole one-syllable word); pt = person templates [stem+ending, two stems, bare word]; th = weight of a fourth
  // person template, names built on the god's own name (0.3 if not given; never in languages of one-syllable words);
  // cn = culture-name syllables.
  const KINDS = {};

  KINDS.isle = function (r) {
    const liq = r.chance(0.5) ? 'l' : 'r';
    let C = tiers(r, ['k' + liq + 'n', 'mhp', 't' + (r.chance(0.3) ? 'v' : 'w')]);
    C = opt(r, C, "'", 0.6); C = opt(r, C, 'N', 0.35); C = opt(r, C, 'f', 0.3); C = opt(r, C, 's', 0.25);
    return {
      C: C, V: tiers(r, ['a', 'io', 'ue']), D: some(r, ['ai', 'au', 'oa', 'ua', 'ei', 'ia', 'ao', 'ou', 'ui'], 2, 4), on: [], co: [], cf: [],
      w: { fc: 2.5, fv: 1.5, on: 0, onM: 0, co: 0, coF: 0, no: 0.26, noM: 0.13, dip: 0.12, long: 0.14, gem: 0 },
      syl: [0, 5, 4, 1], harm: r.chance(0.25) ? 2 : 0, stress: 'penult', med: 1, noInit: "'N", ban: [],
      sp: {}, lv: r.pick(['macron', 'dbl', 'none', 'none']),
      st: { mx: [10, 5], t: [3, 1.5, 0.5, 0, 5, 0.6], sj: '', sep: ' ', join: '', pe: 0.1, lex: 0.28, n: [1, 5, 1.5], nb: [0, 1, 5, 3],
        es: ['CV', 'CV', 'CD', 'CVCV', 'V', 'CV'], ps: ['CV', 'CV', 'V'], ne: 8, np: 2,
        pt: [5, 0, 3], th: 0.3, pes: ['CV', 'V', 'CD', ''], pj: '', pn: [0.5, 5, 2], cn: [0.2, 5, 2],
        dems: [['an', ''], ['an', 'i'], ['an', '+s'], ['i', '']] },
    };
  };

  KINDS.silk = function (r) {
    const C = tiers(r, ['lnSmC', 'kthyZ', 'wpjbdg', 'scfz']).slice(0, r.int(13, 17));
    const pin = r.chance(0.5), join = r.pick(['-', '-', ' ', '']);
    return {
      C: C, V: tiers(r, ['a', 'iu', 'eo']), D: some(r, ['ai', 'ei', 'ao', 'ou', 'ia', 'ua', 'uo', 'ie', 'iu', 'ui', 'ue'], 5, 7), on: [],
      co: ['n', 'N'], cf: ['n', 'N'],
      w: { fc: 4, fv: 2, on: 0, onM: 0, co: 0.4, coF: 0.4, no: 0.06, noM: 0, dip: 0.35, long: 0, gem: 0 },
      syl: [1, 0, 0, 0], harm: 0, stress: 'initial', med: 2, noInit: '', ban: ['wu', 'wo', 'yi'],
      sp: pin ? { S: 'x', C: 'q', c: 'c' } : {}, lv: 'none',
      st: { mx: [10, 3], t: [3.5, 0, 5.5, 1, 0], sj: join, sep: join, join: join, pe: 0, lex: 0.4, n: [1], nb: [1],
        es: ['W'], ps: [], ne: 9, np: 0,
        pt: [0, 5, 0], th: 0, pes: [], pj: r.pick(['', '-', '-']), pn: [1], cn: [1, 3],
        dems: [['ese', '='], ['i', ''], ['an', ''], ['ic', '']] },
    };
  };

  KINDS.maize = function (r) {
    let C = tiers(r, ['tkln', 'mCLc', 'Sswyp']);
    C = opt(r, C, 'W', 0.6);
    return {
      C: C, V: tiers(r, ['a', 'io', 'e']), D: [], on: [], co: tiers(r, ['ln', 'kSs', 'LC']).split(''), cf: tiers(r, ['Ln', 'klS', 's']).split(''),
      w: { fc: 2.5, fv: 1.2, on: 0, onM: 0, co: 0.24, coF: 0.5, no: 0.16, noM: 0.04, dip: 0, long: 0, gem: 0 },
      syl: [0.2, 4, 5, 1.5], harm: 0, stress: 'penult', med: 2, noInit: '', ban: [],
      sp: { S: 'x', c: 'tz', w: 'hu', W: 'cu', s: r.chance(0.6) ? 'z' : 's' }, cq: 1, lv: 'none',
      st: { mx: [12, 5], t: [6, 0, 1, 0, 3], sj: '', sep: ' ', join: '', pe: 0, lex: 0.28, n: [1, 5, 2], nb: [0, 2, 5, 2],
        es: ['CVK', 'CV', 'VK', 'CVCV', 'CV', 'VK'], ps: [], ne: 8, np: 0,
        pt: [5, 0, 3], th: 0.5, pes: ['VK', 'CV', 'V', 'CVK'], pj: '', pn: [0.5, 5, 2], cn: [0.3, 5, 1.5],
        dems: [['ec', ''], ['an', ''], ['ec', '+s'], ['an', 'i']] },
    };
  };

  KINDS.dune = function (r) {
    let C = tiers(r, ['rlmn', 'bdksh', 'qSxzt', 'fjGwy']);
    C = opt(r, C, "'", 0.5);
    let V = tiers(r, ['a', 'iu']);
    if (r.chance(0.2)) V += r.pick(['e', 'o']);
    return {
      C: C, V: V, D: r.chance(0.5) ? ['ai', 'au'] : [], on: [],
      co: tiers(r, ['rlmn', 'sSdbk', 'qtfxz']).split('').slice(0, r.int(8, 12)), cf: [],
      w: { fc: 4, fv: 1, on: 0, onM: 0, co: 0.3, coF: 0.6, no: 0.12, noM: 0, dip: 0.07, long: 0.25, gem: 0.1 },
      syl: [1, 5, 3, 0.4], harm: 0, stress: r.pick(['final', 'penult']), med: 2, noInit: "'", ban: [],
      sp: {}, lv: r.pick(['dbl', 'circ', 'macron', 'none']),
      st: { mx: [10, 4], t: [3.5, 3.6, 0, 0, 3], sj: '', sep: r.chance(0.7) ? ' ' : '-', join: '', pe: 0.15, lex: 0.28, n: [1, 5, 1.5], nb: [0.3, 5, 3, 0.3],
        es: ['VK', 'V', 'VK', 'CV', 'VCV'], ps: ['CVK', 'VK', 'CVK', 'CV'], ne: 7, np: 3,
        pt: [5, 0, 3], th: 0.9, pes: ['VK', 'V', 'VK', 'CV', ''], pj: '', pn: [1, 5, 1], cn: [1, 5, 1],
        dems: [['i', 'im'], ['i', ''], ['ite', '+s'], ['i', 'in'], ['an', 'i']] },
    };
  };

  KINDS.birch = function (r) {
    let C = tiers(r, ['vskrln', 'tdmzpb', 'gSCZcx', 'y']);
    C = opt(r, C, 'f', 0.1);
    const caron = r.chance(0.45);
    return {
      C: C, V: tiers(r, ['ao', 'ei', 'u']), D: r.chance(0.25) ? ['ia', 'ie'] : [],
      on: some(r, ['vl', 'vr', 'zd', 'zl', 'zv', 'sl', 'sm', 'sn', 'sv', 'sk', 'st', 'sp', 'kr', 'kl', 'kv', 'gr', 'gl', 'dr', 'dv', 'tr', 'tv', 'pr',
        'pl', 'br', 'bl', 'ml', 'zn', 'mr', 'gn', 'xr', 'xl'], 9, 13),
      co: tiers(r, ['nrl', 'kvts', 'mSCcdz']).split('').slice(0, 10).concat(some(r, ['sk', 'st', 'rn', 'rk', 'lk'], 0, 2)), cf: [],
      w: { fc: 5, fv: 2.5, on: 0.3, onM: 0.35, co: 0.26, coF: 0.6, no: 0.08, noM: 0, dip: 0.05, long: 0, gem: 0 },
      syl: [1, 5, 3, 0.3], harm: 0, stress: 'penult', med: 2, noInit: '', ban: [],
      sp: caron ? { S: 'š', C: 'č', Z: 'ž', c: 'c', x: r.chance(0.5) ? 'ch' : 'h', y: 'j' } : {}, lv: 'none',
      st: { mx: [11, 4], t: [7, 0, 1, 0, 2], sj: '', sep: ' ', join: '', pe: 0, lex: 0.28, n: [3, 5, 0.5], nb: [0.3, 5, 3, 0.2],
        es: ['VK', 'V', 'VK', 'CV', 'VCV', 'VK'], ps: [], ne: 8, np: 0,
        pt: [5, 0, 2], th: 0.4, pes: ['VK', 'V', 'VK', 'CV', ''], pj: '', pn: [2, 5, 0.5], cn: [2, 5, 0.5],
        dems: [['ian', '+s'], ['ic', 'i'], ['an', 'i'], ['ish', ''], ['ak', 'i']] },
    };
  };

  KINDS.fjord = function (r) {
    return {
      C: tiers(r, ['rnstl', 'dkgvfh', 'mbTyp']), V: tiers(r, ['ae', 'io', 'u']), D: some(r, ['ei', 'au', 'ai', 'oi'], 1, 3),
      on: some(r, ['sk', 'st', 'sv', 'sn', 'sl', 'sp', 'br', 'dr', 'fr', 'gr', 'kr', 'tr', 'Tr', 'hr', 'hl', 'fl', 'gl', 'kl', 'bl', 'kn', 'fy', 'dv',
        'hv', 'by', 'sy'], 10, 14),
      co: tiers(r, ['rnl', 'sdkg', 'tmfT']).split('').concat(some(r, ['rn', 'rd', 'rg', 'rk', 'ld', 'lf', 'nd', 'N', 'st', 'sk', 'lm', 'rm', 'rT',
        'lt', 'rt', 'nn', 'll', 'ft'], 5, 8)), cf: [],
      w: { fc: 5, fv: 2.5, on: 0.3, onM: 0.2, co: 0.4, coF: 0.78, no: 0.14, noM: 0, dip: 0.12, long: 0.15, gem: 0 },
      syl: [3, 5, 1.2, 0], harm: 0, stress: 'initial', med: 3, noInit: '', ban: [],
      sp: r.chance(0.65) ? { y: 'j' } : {}, lv: r.pick(['acute', 'dbl', 'none', 'none']),
      st: { mx: [10, 4], t: [6, 0, 3, 0, 1.5], sj: '', sep: ' ', join: '', pe: 0, lex: 0.3, n: [5, 3, 0], nb: [0.5, 5, 1.5, 0],
        es: ['CVK', 'VK', 'CVK', 'VK', 'CV', 'CVK'], ps: [], ne: 8, np: 0,
        pt: [5, 1, 2], th: 0.5, pes: ['VK', 'V', 'VK', 'CV', ''], pj: '', pn: [4, 4, 0], cn: [3, 4, 0],
        dems: [['ish', ''], ['ish', 'ar'], ['ic', ''], ['an', '+s'], ['en', 'ar']] },
    };
  };

  KINDS.lake = function (r) {
    return {
      C: tiers(r, ['ktlns', 'mrpvh', 'y']), V: tiers(r, ['ai', 'eu', 'o']), D: some(r, ['ai', 'ei', 'oi', 'ui', 'au', 'ou', 'ie', 'uo', 'iu'], 3, 5),
      on: [], co: tiers(r, ['n', 's', 'lr', 'tkh']).split(''), cf: tiers(r, ['n', 's', 't']).split(''),
      w: { fc: 4, fv: 3, on: 0, onM: 0, co: 0.2, coF: 0.28, no: 0.2, noM: 0.02, dip: 0.12, long: 0.3, gem: 0.28 },
      syl: [0.2, 5, 4, 1], harm: 1, neutral: 'i', stress: 'initial', med: 2, noInit: '', ban: [],
      sp: r.chance(0.6) ? { y: 'j' } : {}, lv: 'dbl',
      st: { mx: [12, 5], t: [6, 0, 1, 0, 3], sj: '', sep: ' ', join: '', pe: 0, lex: 0.28, n: [1, 5, 1.5], nb: [0, 3, 5, 1.5],
        es: ['CV', 'CV', 'VCV', 'CVK', 'CV', 'VK'], ps: [], ne: 8, np: 0,
        pt: [5, 0, 3], th: 0.25, pes: ['CV', 'V', 'VK', 'CV', ''], pj: '', pn: [1, 5, 1], cn: [1, 5, 1],
        dems: [['an', 'i'], ['ic', ''], ['i', ''], ['an', '+s'], ['en', 'at']] },
    };
  };

  KINDS.condor = function (r) {
    let C = tiers(r, ['kqtn', 'mlswyCp', 'hYr']);
    for (const e of some(r, ['K', '1', 'Q', '2', 'P'], 2, 4)) C = ins(r, C, e, 0.3);
    C = opt(r, C, 'S', 0.35);
    let V = tiers(r, ['a', 'iu']);
    if (r.chance(0.3)) V += r.pick(['e', 'o']);
    const sp = {};
    if (r.chance(0.4)) sp.Y = 'ñ';
    if (r.chance(0.3)) sp.S = 'x';
    return {
      C: C, V: V, D: [], on: [], co: tiers(r, ['nq', 'ksl', 'rmyC']).split('').slice(0, r.int(5, 8)), cf: [],
      w: { fc: 4, fv: 1.2, on: 0, onM: 0, co: 0.28, coF: 0.35, no: 0.14, noM: 0.02, dip: 0, long: 0, gem: 0 },
      syl: [0.3, 5, 4, 0.6], harm: r.chance(0.3) ? 2 : 0, stress: 'penult', med: 2, noInit: '', ban: [],
      sp: sp, lv: 'none',
      st: { mx: [11, 5], t: [6, 0.5, 1, 0, 3, 0.3], sj: '', sep: ' ', join: '', pe: 0, lex: 0.28, n: [1, 5, 1.5], nb: [0, 3, 5, 1],
        es: ['CV', 'VK', 'CVK', 'CV', 'CVCV', 'V'], ps: ['CVK', 'CV'], ne: 8, np: 2,
        pt: [5, 0, 3], th: 0.4, pes: ['V', 'VK', 'CV', '', 'CV'], pj: '', pn: [1, 5, 1], cn: [1, 5, 1],
        dems: [['an', ''], ['an', 'i'], ['i', ''], ['an', '+s']] },
    };
  };

  KINDS.reed = function (r) {
    return {
      C: tiers(r, ['kstn', 'hmr', 'ygSz', 'dbwCcf']).slice(0, r.int(13, 17)), V: tiers(r, ['a', 'iou', 'e']),
      D: some(r, ['ai', 'ei', 'oi', 'ui'], 0, 2), on: [], co: ['n'], cf: ['n'],
      w: { fc: 4, fv: 3, on: 0, onM: 0, co: 0.12, coF: 0.08, no: 0.18, noM: 0.05, dip: 0.06, long: 0.1, gem: 0.05 },
      syl: [0, 5, 4, 1], harm: 0, stress: 'penult', med: 2, noInit: '',
      ban: ['wi', 'we', 'wu', 'wo', 'yi', 'ye', 'ca', 'ci', 'ce', 'co', 'fa', 'fi', 'fe', 'fo'],
      sp: {}, lv: r.pick(['macron', 'dbl', 'none', 'none']),
      st: { mx: [11, 5], t: [5, 0, 1, 0, 4], sj: '', sep: ' ', join: '', pe: 0, lex: 0.28, n: [1, 5, 1], nb: [0, 2, 5, 2],
        es: ['CV', 'CVCV', 'CV', 'CVK', 'CV', 'CVCV'], ps: [], ne: 8, np: 0,
        pt: [5, 0, 3], th: 0.25, pes: ['CV', 'V', 'CV', '', 'CVCV'], pj: '', pn: [0.5, 5, 1.5], cn: [0.3, 5, 2],
        dems: [['ese', '='], ['an', ''], ['an', 'i'], ['i', '']] },
    };
  };

  KINDS.savanna = function (r) {
    let C = tiers(r, ['bkmnl', 'tdzsw', 'ygfvSYpj']);
    C = opt(r, C, 'h', 0.3);
    C = C.slice(0, r.int(14, 19));
    return {
      C: C, V: tiers(r, ['a', 'ui', 'eo']), D: [],
      on: some(r, ['mb', 'nd', 'ng', 'nz', 'nk', 'mp', 'nt', 'nj', 'mv', 'ns'], 5, 7).concat(
        some(r, ['kw', 'bw', 'mw', 'gw', 'sw', 'tw', 'nw', 'zw', 'lw'], 3, 5), some(r, ['mbw', 'ngw', 'ndw', 'nkw'], 0, 2)),
      co: [], cf: [],
      w: { fc: 5, fv: 2, on: 0.28, onM: 1.1, co: 0, coF: 0, no: 0.1, noM: 0.03, dip: 0, long: 0, gem: 0 },
      syl: [0, 5, 4, 1], harm: 0, stress: 'penult', med: 1, noInit: '', ban: ['wu', 'wo', 'yi'],
      sp: {}, lv: 'none',
      st: { mx: [11, 5], t: [2, 5, 0, 0, 3, 0.4], sj: '', sep: '', join: '', pe: 0.15, lex: 0.28, n: [0.5, 5, 2], nb: [0, 2, 5, 2],
        es: ['CV', 'CV', 'V', 'CVCV', 'CV'], ps: ['CV', 'CV', 'V', 'CV'], ne: 7, np: 4,
        pt: [4, 0, 4], th: 0.4, pes: ['CV', 'V', '', 'CV'], pj: '', pn: [0.3, 5, 2], cn: [0.2, 5, 2],
        dems: [['an', ''], ['i', ''], ['an', 'i'], ['an', '+s']] },
    };
  };

  KINDS.steppe = function (r) {
    let C = tiers(r, ['ktrlns', 'mbdgq', 'SCzyxj']);
    C = opt(r, C, 'G', 0.4); C = opt(r, C, 'p', 0.3); C = opt(r, C, 'h', 0.2);
    const turk = r.chance(0.35), long = r.chance(0.25);
    return {
      C: C, V: tiers(r, ['ae', 'iu', 'o']), D: [], on: [],
      co: tiers(r, ['nrl', 'kqsSt', 'mNzy']).split('').slice(0, r.int(8, 11)).concat(some(r, ['rt', 'lt', 'nt', 'rk', 'lk', 'rs'], 0, 3)), cf: [],
      w: { fc: 5, fv: 3, on: 0, onM: 0, co: 0.42, coF: 0.8, no: 0.18, noM: 0, dip: 0, long: long ? 0.22 : 0, gem: 0 },
      syl: [2, 5, 2, 0.2], harm: r.chance(0.85) ? 1 : 0, neutral: '', stress: 'final', med: 2, noInit: 'NG', ban: [],
      sp: turk ? { S: 'ş', C: 'ç' } : {}, lv: long ? 'dbl' : 'none',
      st: { mx: [10, 4], t: [7, 0, 1, 0, 2.5], sj: '', sep: ' ', join: '', pe: 0, lex: 0.28, n: [3, 5, 0.5], nb: [0.5, 5, 2, 0.2],
        es: ['CVK', 'VK', 'CVK', 'VK', 'CV', 'CVK'], ps: [], ne: 8, np: 0,
        pt: [5, 1, 2], th: 0.45, pes: ['VK', 'CVK', 'V', '', 'VK'], pj: '', pn: [3, 5, 0.3], cn: [3, 5, 0.3],
        dems: [['i', ''], ['ic', ''], ['i', 'lar'], ['an', ''], ['ite', '+s']] },
    };
  };

  KINDS.mist = function (r) {
    let C = tiers(r, ['lrnd', 'mgktb', 'sTDxfwhv']);
    C = opt(r, C, 'H', 0.5);
    return {
      C: C, V: tiers(r, ['ae', 'io', 'u']), D: some(r, ['ae', 'ai', 'oe', 'ei', 'ia', 'ua', 'au', 'io', 'ea'], 3, 5),
      on: some(r, ['gw', 'gl', 'gr', 'br', 'bl', 'dr', 'tr', 'kr', 'kl', 'fl', 'fr', 'sl', 'sn', 'Tr', 'gn', 'rh'], 6, 9),
      co: tiers(r, ['nrl', 'dTxs', 'mg']).split('').concat(some(r, ['rn', 'rd', 'rT', 'lx', 'nt', 'ld', 'rx', 'rg', 'nn', 'll'], 3, 5)), cf: [],
      w: { fc: 5, fv: 2.5, on: 0.22, onM: 0.2, co: 0.38, coF: 0.75, no: 0.22, noM: 0, dip: 0.2, long: 0, gem: 0 },
      syl: [2.5, 5, 1.5, 0], harm: 0, stress: 'initial', med: 3, noInit: 'D', ban: [],
      sp: { k: 'c', x: 'ch', D: r.chance(0.6) ? 'dd' : 'dh', H: 'll' }, lv: 'none',
      st: { mx: [11, 4], t: [4, 4, 1.5, 0, 2], sj: '', sep: ' ', join: '', pe: 0.1, lex: 0.3, n: [4, 4, 0], nb: [0.5, 5, 1.5, 0],
        es: ['VK', 'CVK', 'VK', 'V', 'CVK', 'DK'], ps: ['CVK', 'VK', 'CDK', 'CVK'], ne: 8, np: 3,
        pt: [5, 0, 2], th: 0.3, pes: ['VK', 'V', 'VK', '', 'DK'], pj: '', pn: [3, 5, 0], cn: [3, 4, 0],
        dems: [['ic', ''], ['ish', ''], ['an', '+s'], ['ic', 'i'], ['ian', '+s']] },
    };
  };

  KINDS.lotus = function (r) {
    let C = tiers(r, ['rntk', 'msdvpl', 'yhSjbg', 'C']);
    for (const a of some(r, ['6', '7', '5', '3', '4', '8'], 3, 5)) C = ins(r, C, a, 0.3);
    return {
      C: C, V: tiers(r, ['a', 'iu', 'eo']), D: r.chance(0.4) ? ['ai', 'au'] : [],
      on: some(r, ['pr', 'tr', 'kr', 'br', 'dr', 'gr', 'Sr', 'sv', 'dv', 'ty', 'ky', 'vy', 'sm', 'sn'], 5, 8),
      co: tiers(r, ['nr', 'msS', 'tkl']).split('').slice(0, r.int(5, 8)), cf: [],
      w: { fc: 5, fv: 0.7, on: 0.14, onM: 0.5, co: 0.22, coF: 0.2, no: 0.12, noM: 0, dip: 0.05, long: 0.25, gem: 0 },
      syl: [0.4, 4, 5, 2], harm: 0, stress: 'penult', med: 2, noInit: '', ban: [],
      sp: {}, lv: r.pick(['dbl', 'macron', 'none', 'none']),
      st: { mx: [12, 5], t: [7, 0, 1.5, 0, 2], sj: '', sep: ' ', join: '', pe: 0, lex: 0.28, n: [1, 5, 1.5], nb: [0, 2, 5, 2],
        es: ['CV', 'CVK', 'V', 'CVCV', 'VCV', 'CV'], ps: [], ne: 8, np: 0,
        pt: [5, 0, 3], th: 0.7, pes: ['V', 'CV', 'VK', '', 'V'], pj: '', pn: [0.5, 5, 2], cn: [0.5, 5, 1],
        dems: [['an', '+s'], ['i', ''], ['ic', ''], ['an', 'i'], ['ite', '+s']] },
    };
  };

  KINDS.marble = function (r) {
    const sp = {}, kc = r.chance(0.4);
    if (r.chance(0.65)) sp.f = 'ph';
    sp.x = kc ? 'ch' : 'kh';
    if (kc) sp.k = 'c';
    return {
      C: tiers(r, ['snrl', 'tkmpd', 'Tfxgbz']), V: tiers(r, ['aeo', 'i', 'u']),
      D: some(r, ['ai', 'ei', 'oi', 'ou', 'eu', 'au', 'ia', 'io', 'eo', 'ea'], 4, 6),
      on: some(r, ['pr', 'tr', 'kr', 'pl', 'kl', 'st', 'sp', 'sk', 'Tr', 'fr', 'xr', 'dr', 'gr', 'gl', 'ps', 'ks', 'fl', 'sm'], 7, 10),
      co: tiers(r, ['snr', 'lm', 'kp']).split(''), cf: ['s', 'n', 'r'],
      w: { fc: 5, fv: 3, on: 0.2, onM: 0.4, co: 0.2, coF: 0.72, no: 0.25, noM: 0.04, dip: 0.14, long: 0, gem: 0 },
      syl: [0.3, 4, 5, 1.5], harm: 0, stress: 'penult', med: 2, noInit: '', ban: [],
      sp: sp, xk: 1, lv: 'none',
      st: { mx: [11, 5], t: [7, 0, 0.5, 0, 3], sj: '', sep: ' ', join: '', pe: 0, lex: 0.28, n: [1, 5, 1.5], nb: [0, 3, 5, 1],
        es: ['VK', 'V', 'VK', 'DK', 'D', 'VCV'], ps: [], ne: 9, np: 0,
        pt: [6, 0, 2], th: 0.7, pes: ['VK', 'VK', 'V', 'DK', 'VK'], pj: '', pn: [0.5, 5, 1.5], cn: [0.5, 5, 1],
        dems: [['ian', '+s'], ['an', '+s'], ['ic', 'i'], ['ine', 'i'], ['ean', '+s']] },
    };
  };

  KINDS.forge = function (r) {
    const hy = r.chance(0.5);
    return {
      C: tiers(r, ['kgrzd', 'bxtnm', 'GTSvlqh']).slice(0, r.int(13, 17)), V: tiers(r, ['au', 'o', 'ie']), D: [],
      on: some(r, ['kr', 'gr', 'dr', 'br', 'tr', 'zr', 'Tr', 'xr', 'sk', 'gl', 'bl', 'gn'], 5, 8),
      co: tiers(r, ['krz', 'gdnm', 'xGl']).split('').concat(some(r, ['rk', 'rz', 'rg', 'zd', 'nd', 'ld', 'mb', 'rn', 'rd', 'lg', 'zg', 'rx'], 4, 6)), cf: [],
      w: { fc: 4, fv: 1.3, on: 0.2, onM: 0.25, co: 0.45, coF: 0.9, no: 0.1, noM: 0, dip: 0, long: 0.28, gem: 0 },
      syl: [3, 5, 1, 0], harm: 0, stress: 'final', med: 3, noInit: '', ban: [],
      sp: {}, lv: r.pick(['circ', 'circ', 'dbl', 'none']),
      st: { mx: [10, 4], t: hy ? [3, 0, 5, 0, 1.5] : [5, 0, 3, 0, 2], sj: '', sep: ' ', join: hy ? '-' : '', pe: 0, lex: 0.3, n: [4, 4, 0], nb: [1, 5, 1, 0],
        es: ['VK', 'CVK', 'VK', 'VK', 'CVK', 'V'], ps: [], ne: 8, np: 0,
        pt: [5, 1, 3], th: 0.3, pes: ['VK', 'VK', '', 'V', 'VK'], pj: '', pn: [4, 4, 0], cn: [3, 4, 0],
        dems: [['ite', '+s'], ['ic', ''], ['i', 'im'], ['ar', ''], ['ish', '']] },
    };
  };

  KINDS.star = function (r) {
    const sp = { W: 'qu' };
    if (r.chance(0.7)) sp.k = 'c';
    return {
      C: tiers(r, ['lnrm', 'Tsdtv', 'fgkhwyD']), V: tiers(r, ['ae', 'io', 'u']),
      D: some(r, ['ae', 'ai', 'ie', 'ia', 'io', 'ea', 'ui', 'au', 'ei', 'eo', 'ua'], 4, 6),
      on: some(r, ['gl', 'Tr', 'dr', 'tr', 'fl', 'W', 'ly', 'ny', 'gw', 'br', 'kr'], 3, 6),
      co: tiers(r, ['nlr', 'sT', 'dm']).split('').concat(some(r, ['nd', 'ld', 'rn', 'll', 'nn', 'rT', 'lm'], 2, 4)),
      cf: tiers(r, ['nlr', 'sT']).split('').concat(some(r, ['nd', 'ld', 'rn', 'll'], 0, 2)),
      w: { fc: 3, fv: 3, on: 0.1, onM: 0.3, co: 0.28, coF: 0.68, no: 0.35, noM: 0.05, dip: 0.2, long: 0.18, gem: 0 },
      syl: [1, 5, 4, 1], harm: 0, stress: 'penult', med: 2, noInit: 'D', ban: ['wu', 'yi'],
      sp: sp, fe: r.chance(0.4) ? 1 : 0, lv: r.pick(['acute', 'acute', 'circ', 'none']),
      st: { mx: [11, 4], t: [6, 0, 1, 0, 3], sj: '', sep: ' ', join: '', pe: 0, lex: 0.28, n: [2, 5, 1], nb: [0, 4, 5, 1],
        es: ['VK', 'DK', 'V', 'CVK', 'VCV', 'VK'], ps: [], ne: 8, np: 0,
        pt: [6, 0, 2], th: 0.4, pes: ['VK', 'DK', 'V', 'VK', ''], pj: '', pn: [2, 5, 0.5], cn: [2, 5, 0.5],
        dems: [['in', ''], ['ian', '+s'], ['ic', 'i'], ['an', 'i'], ['in', 'i']] },
    };
  };

  KINDS.frost = function (r) {
    return {
      C: tiers(r, ['qktn', 'lmsp', 'Nvyrg']), V: tiers(r, ['a', 'iu']), D: some(r, ['ai', 'ui', 'au', 'ia', 'ua'], 0, 2), on: [],
      co: tiers(r, ['qk', 'tn', 'lmrsN']).split('').slice(0, r.int(5, 8)), cf: tiers(r, ['qk', 't', 'n']).split(''),
      w: { fc: 4, fv: 2, on: 0, onM: 0, co: 0.32, coF: 0.72, no: 0.25, noM: 0.06, dip: 0.06, long: 0.25, gem: 0.2 },
      syl: [0, 3, 5, 3], harm: 0, stress: 'penult', med: 2, noInit: 'Nr', ban: [],
      sp: r.chance(0.5) ? { y: 'j' } : {}, lv: 'dbl',
      st: { mx: [12, 5], t: [6, 0, 0.5, 0, 4], sj: '', sep: ' ', join: '', pe: 0, lex: 0.28, n: [1, 5, 2], nb: [0, 2, 5, 3],
        es: ['VK', 'CVK', 'VK', 'CV', 'VK', 'CVK'], ps: [], ne: 8, np: 0,
        pt: [5, 0, 3], th: 0.25, pes: ['VK', 'V', 'VK', '', 'CV'], pj: '', pn: [0.5, 5, 2], cn: [0.5, 5, 1],
        dems: [['ic', ''], ['i', ''], ['an', ''], ['ic', 'it']] },
    };
  };

  KINDS.monsoon = function (r) {
    let C = tiers(r, ['ktnl', 'mpsrh', 'bdNycw']);
    for (const a of some(r, ['3', '4', '5'], 2, 3)) C = ins(r, C, a, 0.2);
    C = opt(r, C, 'C', 0.5);
    const join = r.pick([' ', ' ', '-', '']), long = r.chance(0.3);
    return {
      C: C, V: tiers(r, ['a', 'iou', 'e']), D: some(r, ['ia', 'ua', 'ai', 'ao', 'oi', 'ue', 'ie', 'uo', 'au', 'eo'], 4, 6),
      on: some(r, ['kr', 'kl', 'pr', 'pl', 'tr', '5r', '3r', '3l', '5l', 'kw', '5w', 'sr', 'br', 'bl', 'dr'], 4, 7),
      co: tiers(r, ['Nn', 'mk', 'tp']).split(''), cf: [],
      w: { fc: 5, fv: 3, on: 0.18, onM: 1, co: 0.58, coF: 0.58, no: 0.05, noM: 0, dip: 0.3, long: long ? 0.15 : 0, gem: 0 },
      syl: [1, 0, 0, 0], harm: 0, stress: 'initial', med: 3, noInit: '', ban: ['wu', 'wo', 'yi'],
      sp: {}, lv: long ? 'dbl' : 'none',
      st: { mx: [10, 3], t: [1.5, 3.5, 5, 0.8, 0], sj: join, sep: join, join: join, pe: 0, lex: 0.4, n: [1], nb: [1],
        es: ['W'], ps: ['W'], ne: 7, np: 6,
        pt: [0, 5, 0], th: 0, pes: [], pj: r.pick(['', '-', ' ']), pn: [1], cn: [1, 2.5],
        dems: [['ese', '='], ['i', ''], ['an', ''], ['ic', '']] },
    };
  };

  KINDS.papyrus = function (r) {
    return {
      C: tiers(r, ['ntkrs', 'mphb', 'fxSwdj']).slice(0, r.int(12, 15)), V: tiers(r, ['ae', 'u', 'io']), D: [], on: [],
      co: tiers(r, ['ntr', 'skmx', 'pfSb']).split('').slice(0, r.int(7, 10)), cf: tiers(r, ['tn', 'rsx', 'pfkm']).split(''),
      w: { fc: 5, fv: 2, on: 0, onM: 0, co: 0.42, coF: 0.72, no: 0.26, noM: 0, dip: 0, long: 0, gem: 0 },
      syl: [2, 5, 1, 0], harm: 0, stress: 'initial', med: 2, noInit: '', ban: ['wu', 'yi'],
      sp: {}, lv: 'none',
      // house-of and city-of particles before a hyphen, a few short endings, and compounds of two words
      st: { mx: [10, 4], t: [3, 3.5, 2.5, 0, 2], sj: '', sep: '-', join: '-', pe: 0.1, lex: 0.34, n: [2, 5, 0.5], nb: [0, 5, 2],
        es: ['VK', 'V', 'VK', 'CV', 'VCV', 'VK'], ps: ['CVK', 'CV', 'VK', 'CVK'], ne: 6, np: 3,
        pt: [4, 3, 3], th: 1.1, pes: ['VK', 'V', 'CVK', 'VK', ''], pj: r.pick(['', '', '-']), pn: [1, 5, 1], cn: [0.4, 5, 0.6],
        dems: [['ite', '+s'], ['ic', ''], ['i', 'u'], ['an', '+s'], ['ian', '+s']] },
    };
  };

  const KIND_LIST = Object.keys(KINDS);
  Names.KINDS = KIND_LIST.slice();

  // ------------------------------------------------------------------ building the lexicon of a new language
  // an ending, particle or other affix of the given shape, in the proto-language
  function affix(c, rng, shape) {
    let s = '';
    for (let i = 0; i < shape.length; i++) {
      const t = shape.charAt(i);
      if (t === 'W') s += protoWord(c, rng, 1);
      else if (t === 'C') {
        let k = c.C.charAt(pick(rng, c.cC));
        for (let j = 0; j < 4 && (k === "'" || (i === 0 && s === '' && c.noInit.indexOf(k) >= 0)); j++) k = c.C.charAt(pick(rng, c.cC));
        s += k === "'" ? c.C.charAt(0) : k;
      } else if (t === 'K') { if (c.cf.length) s += c.cf[pick(rng, c.cCf)]; }
      else if (t === 'D') s += c.D.length ? c.D[pick(rng, c.cD)] : c.V.charAt(pick(rng, c.cV));
      else if (t === 'V') {
        let v = c.V.charAt(pick(rng, c.cV));
        const k = s.charAt(s.length - 1);
        for (let j = 0; j < 4 && (c.ban[k + v] || k === v); j++) v = c.V.charAt(pick(rng, c.cV));
        s += v;
      }
    }
    return s;
  }
  function affixes(c, rng, shapes, count, lo, hi, ini) {
    const out = [];
    for (let t = 0; out.length < count && t < count * 14 && shapes.length; t++) {
      const shape = shapes[t % shapes.length];
      if (shape === '') { if (out.indexOf('') < 0) out.push(''); continue; }
      const e = shifted(c, affix(c, rng, shape), ini, true);
      const sp = spell(c, e);
      if (sp.length < lo || sp.length > hi || out.indexOf(e) >= 0) continue;
      if (!okName(ini ? sp : 'a' + sp.replace(/^[^a-z]+/, ''), 2, 14) && !okName('ta' + sp, 2, 14)) continue;
      if (ini && okName(sp, 2, 14) && isCommon(OKF)) continue;         // a particle that reads as English ('To', 'No', 'She') taints every name it leads
      out.push(e);
    }
    return out;
  }
  function newRoots(c, rng, roots, count) {
    const seen = {};
    for (const s of roots) seen[s] = 1;
    for (let t = 0; roots.length < count && t < count * 14; t++) {
      const s = surf(c, rng, 1 + pick(rng, c.cSyl));
      if (s.length < 2 || seen[s]) continue;
      const sp = spell(c, s);
      if (sp.length < 2 || sp.length > 8 || !okName(sp, 2, 8)) continue;
      if (sp.length > 2 && TUNE.names.english && isCommon(OKF)) continue;       // a root comes back in name after name: not 'rat', not 'bantu'
      seen[s] = 1; roots.push(s);
    }
    return roots;
  }
  function newGod(c, rng) {
    for (let t = 0; t < 40; t++) {
      const s = c.mono ? fuse(c, surf(c, rng, 1), surf(c, rng, 1)) : surf(c, rng, t < 30 && rng.f() < 0.8 ? 2 : 3);
      const sp = capWord(spell(c, s));
      if (fine(sp, 4, 8) && !wordy(sp, 8, 3) && c.roots.indexOf(s) < 0) return s;
    }
    return c.linkC + c.V.charAt(0) + c.linkC + c.V.charAt(c.V.length > 1 ? 1 : 0);
  }

  function rootLanguage(rng, kind) {
    const T = TUNE.names;
    const lang = KINDS[kind](rng), st = lang.st;
    delete lang.st;
    // one fixed key order, so equal seeds give byte-identical JSON
    const out = {
      v: 1, kind: kind, gen: 0,
      C: lang.C, V: lang.V, D: lang.D, on: lang.on, co: lang.co, cf: lang.cf, w: lang.w, syl: lang.syl,
      harm: lang.harm, neutral: lang.neutral || '', stress: lang.stress, med: lang.med, noInit: lang.noInit, ban: lang.ban,
      shifts: [], sp: lang.sp, lv: lang.lv, cq: lang.cq ? 1 : 0, xk: lang.xk ? 1 : 0, fe: lang.fe ? 1 : 0,
      roots: [], god: '',
      mx: st.mx,
      place: { t: st.t, sj: st.sj, sep: st.sep, join: st.join, after: 0, pe: st.pe, lex: st.lex, n: st.n, nb: st.nb, ends: [], parts: [] },
      person: { t: st.pt.concat([num(st.th, 0.3)]), join: st.pj, lex: 0.15, n: st.pn, ends: [] },
      cult: { n: st.cn, lex: 0.5 },
      dem: { adj: 'an', pl: 'i', link: rng.pick(['n', 'n', 'n', 'r', 'l', 't', 'v', 'k']) },
    };
    const dm = rng.pick(st.dems);
    out.dem.adj = dm[0]; out.dem.pl = dm[1];
    const c = compile(out);
    out.roots = newRoots(c, rng, [], T.roots);
    c.roots = out.roots;
    out.place.ends = affixes(c, rng, st.es, st.ne, 1, 5, false);
    out.place.parts = affixes(c, rng, st.ps, st.np, 2, 4, true);
    out.person.ends = affixes(c, rng, st.pes, Math.min(5, st.pes.length), 1, 4, false);
    out.god = newGod(c, rng);
    return out;
  }

  // How varied are the names of a language? Draws TUNE.names.sample place names and as many personal names and
  // returns the share of each that were different, the share of raw candidates that were usable, and how often
  // the plain fallback name had to answer.
  function measure(lang, seed) {
    const c = compile(lang), T = TUNE.names, n = T.sample, rng = DM.U.rng(seed);
    const k0 = STATS.names, k1 = STATS.rerolls, k2 = STATS.fallbacks;
    let dp = 0, dq = 0, reps = 0;
    for (;;) {
      const seenP = Object.create(null), seenQ = Object.create(null);
      for (let i = 0; i < n; i++) {
        const p = placeName(c, rng), q = personName(c, rng);
        if (!seenP[p]) { seenP[p] = 1; dp++; }
        if (!seenQ[q]) { seenQ[q] = 1; dq++; }
      }
      // a second sample when the first is too close to the mark to call: one alone is noisy
      const a = dp / n - T.distinct, b = dq / n - T.distinct;
      if (++reps > 1 || !((a > -0.03 && a < 0.02) || (b > -0.03 && b < 0.02))) break;
    }
    const drawn = 2 * n * reps, rerolls = STATS.rerolls - k1, falls = STATS.fallbacks - k2;
    STATS.names = k0; STATS.rerolls = k1; STATS.fallbacks = k2;                 // measuring is not use
    return { place: dp / (n * reps), person: dq / (n * reps), valid: drawn / (drawn + rerolls), fallbacks: falls / drawn };
  }
  function r2(x) { return Math.round(x * 100) / 100; }
  // make room for more names: fewer recurring roots, longer stems, and (for languages of one-syllable words) more compounds
  function widen(lang, P, isPlace) {
    P.lex = r2(Math.max(0.08, num(P.lex, 0.3) * 0.75));
    const longer = (a) => { for (let i = a.length - 2; i >= 0; i--) { const d = 0.2 * a[i]; a[i] = r2(a[i] - d); a[i + 1] = r2(a[i + 1] + d); } };
    if (Array.isArray(P.n) && P.n.length > 1) longer(P.n);
    const t = P.t;
    if (isPlace) {
      if (Array.isArray(P.nb) && P.nb.length > 1) longer(P.nb);
      if (!(lang.syl[1] > 0 || lang.syl[2] > 0 || lang.syl[3] > 0)) {
        // one-syllable words: fewer names on the favourite endings and particles, more free compounds
        const d0 = 0.25 * t[0], d1 = 0.25 * t[1];
        t[0] -= d0; t[1] -= d1; t[2] += 0.7 * (d0 + d1); t[3] += 0.3 * (d0 + d1);
      } else if (t[2] > 0) t[2] *= 1.25;
      balance(t, 3);
    } else {
      if (t[1] > 0) t[1] = t[1] * 1.25 + 0.2;               // more two-stem names
      if (t[3] > 0) t[3] *= 0.6;                            // fewer on the god's name: they are few
      balance(t, 3);
    }
  }
  // Template weights, kept sane however often daughters and calibration push them: they sum to 10, no template
  // takes more than 70 % (the template `rare`, three-stem compounds, no more than 22 %), and none in use falls under 3 %.
  function balance(t, rare) {
    for (let pass = 0; pass < 3; pass++) {
      let sum = 0;
      for (let i = 0; i < t.length; i++) { if (!(t[i] > 0)) t[i] = 0; sum += t[i]; }
      if (!(sum > 0)) return;
      for (let i = 0; i < t.length; i++) {
        if (t[i] <= 0) continue;
        const hi = (i === rare ? 0.22 : 0.7) * sum, lo = 0.03 * sum;
        t[i] = t[i] > hi ? hi : t[i] < lo ? lo : t[i];
      }
    }
    let sum = 0;
    for (let i = 0; i < t.length; i++) sum += t[i];
    for (let i = 0; i < t.length; i++) t[i] = r2(10 * t[i] / sum);
  }
  // loosen a new language until its names are varied enough; returns the last measurement
  function calibrate(lang, seed) {
    const T = TUNE.names;
    let m = measure(lang, seed);
    for (let r = 0; r < T.rounds && (m.place < T.distinct || m.person < T.distinct || m.valid < 0.5); r++) {
      if (m.valid < 0.5) {
        // most candidates are thrown away, nearly always for their length (old words grow as shifts pile up): shorter stems
        shorter(lang.place.n); shorter(lang.place.nb); shorter(lang.person.n);
      } else {
        if (m.place < T.distinct) widen(lang, lang.place, true);
        if (m.person < T.distinct) widen(lang, lang.person, false);
      }
      m = measure(lang, seed);
    }
    return m;
  }
  // move a third of each weight in a list of syllable counts one step toward fewer syllables
  function shorter(a) {
    if (!Array.isArray(a)) return;
    for (let i = 1; i < a.length; i++) { const d = 0.34 * num(a[i], 0); a[i] = r2(a[i] - d); a[i - 1] = r2(num(a[i - 1], 0) + d); }
  }
  function score(m) { return Math.min(m.place, m.person) + Math.min(0, m.valid - 0.5) - 5 * m.fallbacks; }
  function sound(m, slack) {
    const T = TUNE.names;
    return m.place >= T.distinct - slack && m.person >= T.distinct - slack && m.valid >= 0.5 && m.fallbacks <= 0.01;
  }

  // ------------------------------------------------------------------ daughter languages
  function lev(a, b) {
    const n = a.length, m = b.length;
    if (!n) return m;
    if (!m) return n;
    let prev = new Array(m + 1), cur = new Array(m + 1);
    for (let j = 0; j <= m; j++) prev[j] = j;
    for (let i = 1; i <= n; i++) {
      cur[0] = i;
      for (let j = 1; j <= m; j++) {
        const sub = prev[j - 1] + (a.charCodeAt(i - 1) === b.charCodeAt(j - 1) ? 0 : 1);
        const del = prev[j] + 1, add = cur[j - 1] + 1;
        cur[j] = sub < del ? (sub < add ? sub : add) : (del < add ? del : add);
      }
      const t = prev; prev = cur; cur = t;
    }
    return prev[m];
  }

  // every stored word of a language with the flags its position gives it: [list, word-initial, word-final]
  function lexLists(lang) {
    return [[lang.roots, true, true], [lang.place.ends, false, true], [lang.place.parts, true, true], [lang.person.ends, false, true]];
  }

  // what a list of words is made of: [different consonants, different vowel qualities, letters as spelled]
  function census(words, spelled) {
    let cons = '', vow = '', letters = 0;
    for (let i = 0; i < words.length; i++) {
      const w = words[i];
      letters += spelled[i].length;
      for (let j = 0; j < w.length; j++) {
        const ch = w.charAt(j), cl = CLS[w.charCodeAt(j) & 127];
        if (cl & CONS) { if (cons.indexOf(ch) < 0) cons += ch; }
        else if (cl & VOW) { const b = ch.toLowerCase(); if (vow.indexOf(b) < 0) vow += b; }
      }
    }
    return [cons.length, vow.length, letters];
  }

  function chooseShifts(pc, items, rng, maxNew) {
    const T = TUNE.names, n = items.length;
    const base = items.map((it) => spell(pc, it.s));
    let cur = items.map((it) => it.s), curSp = base.slice();
    let have = census(cur, curSp);
    const order = [];
    for (let i = 0; i < SHIFTS.length; i++) order.push(i);
    rng.shuffle(order);
    const picked = [], groups = {}, quota = { C: 2, V: 2, S: 1 };
    for (let o = 0, tried = 0; o < order.length && picked.length < maxNew && tried < 64; o++) {
      const def = SHIFTS[order[o]];
      if (groups[def[0]] || quota[def[1]] <= 0) continue;
      tried++;
      const f = compileRule(def[2]);
      const next = new Array(n), nextSp = new Array(n);
      let changed = 0, bad = 0, dist = 0, differ = 0;
      for (let i = 0; i < n; i++) {
        const s = f(cur[i], items[i].ini, items[i].fin);
        next[i] = s;
        const sp = s === cur[i] ? curSp[i] : spell(pc, s);
        nextSp[i] = sp;
        if (sp !== curSp[i]) { changed++; if (!sp || (items[i].free && !okName(sp, 2, 14))) bad++; }
        if (sp !== base[i]) { differ++; dist += lev(base[i], sp) / Math.max(base[i].length, sp.length, 1); }
      }
      if (changed < 0.08 * n || bad > 0.1 * changed || dist / n > T.drift) continue;
      // A language must stay a language however many shifts its line has been through: mergers may not wear its
      // sounds down to a handful, and words already long may not grow longer.
      const will = census(next, nextSp);
      if ((will[0] < have[0] && will[0] < T.minCons) || (will[1] < have[1] && will[1] < 3)) continue;
      if (will[2] > have[2] && will[2] > T.rootLen * n) continue;
      picked.push(def[2]); groups[def[0]] = 1; quota[def[1]]--;
      cur = next; curSp = nextSp; have = will;
      if (picked.length >= 3 && differ >= T.changed * n) break;
    }
    return picked;
  }

  // the daughter's own habits: favourite endings move, a new coinage or two, and two visible changes of custom
  function drift(lang, rng) {
    const P = lang.place, Q = lang.person;
    if (P.ends.length > 2) P.ends.unshift(P.ends.splice(rng.int(1, P.ends.length - 1), 1)[0]);
    if (Q.ends.length > 2) Q.ends.unshift(Q.ends.splice(rng.int(1, Q.ends.length - 1), 1)[0]);
    if (P.parts.length > 1) P.parts.unshift(P.parts.splice(rng.int(1, P.parts.length - 1), 1)[0]);
    for (let i = 0; i < P.t.length; i++) if (P.t[i] > 0) P.t[i] = P.t[i] * rng.range(0.6, 1.7);
    const habits = [0, 1, 2, 3, 4, 5];
    rng.shuffle(habits);
    let done = 0;
    for (let h = 0; h < habits.length && done < 2; h++) {
      switch (habits[h]) {
        case 0: {                                           // how compounds and particles are written
          const mono = !(lang.syl[1] > 0 || lang.syl[2] > 0);
          const cur = P.t[1] > 0 && P.parts.length ? P.sep : P.t[2] > 0 ? P.join : null;
          if (cur === null) break;
          const next = rng.pick(['', '-', ' '].filter((x) => x !== cur));
          if (P.t[1] > 0 && P.parts.length) P.sep = next;
          if (P.t[2] > 0 && (mono || P.join)) P.join = next;
          if (mono) P.sj = next;
          done++;
          break;
        }
        case 1: {                                           // how long vowels are written
          const has = lang.w.long > 0 || /[AEIOU]/.test(lang.roots.join(''));
          if (!has) break;
          const opts = ['dbl', 'acute', 'circ', 'macron'].filter((x) => x !== lang.lv);
          lang.lv = rng.pick(opts);
          done++;
          break;
        }
        case 2: {                                           // a sound gets a new spelling
          const inv = inventory(lang), used = {};
          for (const ch of inv) used[(lang.sp[ch] !== undefined ? lang.sp[ch] : SPELL[ch]) || ch] = 1;
          const cands = Object.keys(ALT).filter((ch) => inv.indexOf(ch) >= 0);
          rng.shuffle(cands);
          for (const ch of cands) {
            const now = (lang.sp[ch] !== undefined ? lang.sp[ch] : SPELL[ch]) || ch;
            const tight = inCluster(lang, ch);                // a sound that stands beside other consonants takes no longer spelling ('Phjar')
            const alts = ALT[ch].filter((a) => a !== now && !used[a] && !(a.length === 1 && a.charCodeAt(0) < 128 && inv.indexOf(a) >= 0) &&
              !(tight && a.length > now.length));
            if (!alts.length) continue;
            lang.sp[ch] = rng.pick(alts);
            done++;
            break;
          }
          break;
        }
        case 3:                                             // what the people are called
          lang.dem.pl = rng.pick(['i', '', '+s', 'im', 'in', 'ar', 'ai', 'u', 'a'].filter((x) => x !== lang.dem.pl));
          done++;
          break;
        case 4:                                             // particles: taken up, or worn down into prefixes
          if (P.parts.length && P.t[1] > 0) { P.t[1] = Math.round(P.t[1] * (rng.chance(0.5) ? 0.3 : 2.2) * 100) / 100; done++; }
          else if (P.t[4] > 0 && P.t[0] > 0) { const k = rng.chance(0.5) ? 0 : 4; P.t[k] = Math.round(P.t[k] * 2.5 * 100) / 100; done++; }
          break;
        case 5:                                             // vowel harmony decays
          if (lang.harm) { lang.harm = 0; done++; }
          break;
      }
    }
    balance(P.t, 3);
  }

  // put fresh coinages into a list of favourites at the given ranks, keeping the list's length
  function renew(list, fresh, at) {
    const n = list.length;
    let k = 0;
    for (const e of fresh) {
      if (!e || list.indexOf(e) >= 0 || k >= at.length) continue;
      list.splice(Math.min(at[k++], list.length), 0, e);
    }
    while (list.length > n && list.length > 1) {
      // forget the last entry that is not the empty ending
      let i = list.length - 1;
      if (list[i] === '' && i > 0) i--;
      list.splice(i, 1);
    }
  }

  // every sound a language's stored words use
  function inventory(lang) {
    let s = '';
    const lists = lexLists(lang);
    for (const L of lists) for (const w of L[0]) for (let i = 0; i < w.length; i++) if (s.indexOf(w.charAt(i)) < 0) s += w.charAt(i);
    for (let i = 0; i < lang.god.length; i++) if (s.indexOf(lang.god.charAt(i)) < 0) s += lang.god.charAt(i);
    return s;
  }

  // is the sound ever next to another consonant in the language's stored words?
  function inCluster(lang, ch) {
    for (const L of lexLists(lang)) {
      for (const w of L[0]) {
        for (let i = w.indexOf(ch); i >= 0; i = w.indexOf(ch, i + 1)) {
          if ((i > 0 && (CLS[w.charCodeAt(i - 1) & 127] & CONS)) || (i + 1 < w.length && (CLS[w.charCodeAt(i + 1) & 127] & CONS))) return true;
        }
      }
    }
    return false;
  }

  // a sound that is new to the language must not be written like one it already has
  function respell(lang, before) {
    const inv = inventory(lang), used = {};
    const of = (ch) => (lang.sp[ch] !== undefined ? lang.sp[ch] : SPELL[ch]) || ch;
    for (const ch of inv) if (before.indexOf(ch) >= 0) used[of(ch)] = 1;
    if (lang.cq) { used.c = 1; used.qu = 1; }
    for (const ch of inv) {
      if (before.indexOf(ch) >= 0 || (CLS[ch.charCodeAt(0) & 127] & VOW)) continue;
      if (used[of(ch)] && ALT[ch]) {
        const alt = ALT[ch].filter((a) => !used[a] && !(a.length === 1 && a.charCodeAt(0) < 128 && inv.indexOf(a) >= 0))[0];
        if (alt) lang.sp[ch] = alt;
      }
      used[of(ch)] = 1;
    }
  }

  // has the language every part a daughter inherits? (a hand-made or damaged one can still give names, but is no parent)
  function complete(L) {
    const P = L.place, Q = L.person, obj = (x) => !!x && typeof x === 'object' && !Array.isArray(x);
    return strList(L.roots) && isStr(L.god) && Array.isArray(L.shifts) && Array.isArray(L.syl) && obj(L.w) && obj(L.sp) && obj(L.dem) &&
      obj(P) && Array.isArray(P.t) && strList(P.ends) && strList(P.parts) && obj(Q) && Array.isArray(Q.t) && strList(Q.ends);
  }

  function daughter(rng, parent) {
    const T = TUNE.names;
    let pc = get(parent);
    if (!complete(pc.lang)) pc = fallback();
    const src = pc.lang;                                    // the parent (or the built-in language if the parent was unusable)
    const base = JSON.parse(JSON.stringify(src));
    // the parent's speech: its stored words and a handful of fresh ones
    const items = [];
    for (const L of lexLists(base)) for (const s of L[0]) if (s) items.push({ s: s, ini: L[1], fin: L[2], free: L[1] });
    if (base.god) items.push({ s: base.god, ini: true, fin: true, free: true });
    const sr = rng.fork('speech');
    for (let i = 0; i < 40; i++) items.push({ s: surf(pc, sr, 1 + pick(sr, pc.cSyl)), ini: true, fin: true, free: true });
    const room = Math.max(0, T.maxShifts - base.shifts.length);
    const picked = chooseShifts(pc, items, rng, Math.min(T.newShifts, room));
    const seed = rng.next();
    let best = null, bestScore = -1e9;
    for (let keep = picked.length; keep >= 0; keep = keep > 0 ? 0 : -1) {     // with its new shifts; failing that, without them
      const lang = JSON.parse(JSON.stringify(base));
      lang.gen = (lang.gen | 0) + 1;
      const rules = picked.slice(0, keep), fs = rules.map(compileRule), before = inventory(lang);
      const run = (s, ini, fin) => { for (let i = 0; i < fs.length; i++) s = fs[i](s, ini, fin); return s; };
      lang.shifts = lang.shifts.concat(rules);
      if (rules.some((r) => r[0] === 'apo' || r[0] === 'fcl' || r[0] === 'nasl')) lang.med = Math.max(2, lang.med | 0);
      const uniq = (list, ini, fin) => {
        const out = [];
        for (const s of list) { const t = s ? run(s, ini, fin) : s; if (out.indexOf(t) < 0) out.push(t); }
        return out;
      };
      lang.roots = uniq(lang.roots, true, true);
      lang.place.ends = uniq(lang.place.ends, false, true);
      lang.place.parts = uniq(lang.place.parts, true, true);
      lang.person.ends = uniq(lang.person.ends, false, true);
      lang.god = run(lang.god, true, true);
      const dr = DM.U.rng(seed);
      drift(lang, dr);
      // some old roots fall out of use; new ones are coined in the daughter's own sounds
      const c = compile(lang);
      const drop = Math.round(lang.roots.length * T.replace);
      for (let i = 0; i < drop && lang.roots.length > 4; i++) lang.roots.splice(dr.int(0, lang.roots.length - 1), 1);
      newRoots(c, dr, lang.roots, Math.max(T.roots, lang.roots.length));
      // New settlers name their towns in new ways: the daughter's favourite place ending is its own coinage, an old
      // favourite comes second, another coinage third; the endings least used are forgotten. Likewise its leading
      // particle and its commonest ending for personal names.
      const mono = c.mono;
      renew(lang.place.ends, affixes(c, dr, mono ? ['W'] : ['VK', 'CV', 'CVK', 'V', 'VCV'], 2, 1, 5, false), [0, 2]);
      if (lang.place.parts.length) renew(lang.place.parts, affixes(c, dr, mono ? ['W'] : ['CVK', 'CV', 'VK'], 1, 2, 4, true), [0]);
      if (lang.person.ends.length > 1) renew(lang.person.ends, affixes(c, dr, ['VK', 'V', 'CV'], 1, 1, 4, false), [0]);
      respell(lang, before);
      const sp = capWord(spell(compile(lang), lang.god));
      if (!fine(sp, T.minLen, 10)) lang.god = newGod(compile(lang), dr);
      // the daughter must still give varied, well-formed names; if these shifts wore the words down too far, keep fewer
      const m = calibrate(lang, (seed ^ 0x51ed) >>> 0), sc = score(m) + 0.004 * keep;
      if (sc > bestScore) { best = lang; bestScore = sc; }
      if (sound(m, 0.01)) { best = lang; break; }
    }
    return best;
  }

  // ------------------------------------------------------------------ public: languages
  Names.language = function (rng, parent, kind) {
    rng = useRng(rng);
    if (parent && typeof parent === 'object') return daughter(rng, parent);
    if (typeof kind !== 'string' || !KINDS[kind]) kind = KIND_LIST[rng.int(0, KIND_LIST.length - 1)];
    let best = null, bestScore = -1e9;
    for (let t = 0; t < 4; t++) {
      const lang = rootLanguage(rng, kind);
      const m = calibrate(lang, rng.next()), sc = score(m);
      if (sc > bestScore) { best = lang; bestScore = sc; }
      if (sound(m, 0)) { best = lang; break; }
    }
    return best;
  };

  // the roots of a language as it writes them
  Names.roots = function (lang) {
    const c = get(lang);
    return c.roots.map((s) => spell(c, s));
  };

  // the roots a daughter language inherited, beside the parent's form of each: [[parentWord, daughterWord], …]
  // (the parent's root, put through the sound shifts the daughter added, is still a root of the daughter)
  Names.cognates = function (parent, daughter) {
    const pc = get(parent), dc = get(daughter), out = [];
    const added = arr(dc.lang.shifts).slice(arr(pc.lang.shifts).length).map(compileRule);
    if (dc === pc || !added.length && dc.lang.gen === pc.lang.gen) return out;
    for (const p of pc.roots) {
      let d = p;
      for (let i = 0; i < added.length; i++) d = added[i](d, true, true);
      if (dc.roots.indexOf(d) >= 0) out.push([spell(pc, p), spell(dc, d)]);
    }
    return out;
  };

  // one line about a language, for logs and reports
  Names.describe = function (lang) {
    const c = get(lang), L = c.lang;
    const cons = c.C.split('').map((ch) => c.sp[ch.charCodeAt(0)]).join(' ');
    return (L.kind || '?') + ' gen ' + (L.gen | 0) + ' | C: ' + cons + ' | V: ' + c.V.split('').join(' ') +
      (c.D.length ? ' ' + c.D.join(' ') : '') + ' | long: ' + (L.lv || 'none') + ' | stress: ' + (L.stress || 'penult') +
      ' | shifts: ' + (arr(L.shifts).map((r) => r.map((x) => Array.isArray(x) ? x.join('/') : x).join(' ')).join('; ') || 'none');
  };
})(globalThis.DM = globalThis.DM || {});
