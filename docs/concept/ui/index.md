# Concept art: HUD, panels and screens (T-007, Grok)

Ten 1920×1080 mockups of the Demiurge interface, from SPEC-UI §4, §4b and §5. Each one keeps the
planet visible behind the interface.

**How they were made.** The interface is real HTML/CSS. Headless Chrome renders it at 1920×1080
(puppeteer-core, `src/render.js`) over a cinematic background, so every word is real type and
stays sharp. The backgrounds were made with Grok Bot's image generator from our own prompts. They
were generated at 1280×720 and upscaled to 1920×1080 with Lanczos.

The orbit background was generated with `shots/ue-cloudlab-terminator-final.png` as the reference,
so it matches the approved orbit shot. The other backgrounds used that orbit image as their
reference. No game UI, game, or named artist was imitated. All icons were drawn by hand as SVG
for this task (`src/icons.js`).

**Sources (all inside this folder).**

| File | What it is |
|---|---|
| `src/ui.css` | Shared tokens and components |
| `src/icons.js` | 72 line icons (24×24 viewBox, 1.6 px stroke, round caps and joins, `currentColor`) |
| `src/hud.js` | Top bar, palette, labels and cursor helpers |
| `src/insp.js` | Love–fear scale and edicts |
| `src/<page>.html` | One page per mockup |
| `src/bg/*.jpg` | Backgrounds |

To re-render: `cd src && node render.js <page…>` (needs puppeteer-core and Chrome).

## Shared visual language

**Colours** (the SPEC-UI tokens):

| Token | Value |
|---|---|
| ink | `#0b0f17` |
| ink-2 | `#131a26` |
| ink-3 | `#1b2434` |
| panel | `rgba(11,15,23,.84)` with 6 px backdrop blur, 1 px border `rgba(232,205,150,.22)`, radius 10 px |
| strong border | `rgba(232,205,150,.55)` |
| gold | `#e8cd96` |
| gold-hi | `#fff1c9` |
| text | `#e9e4d6` |
| dim | `#b3b9c4` |
| good | `#8fd6a0` |
| bad | `#e2836f` |
| awe | `#ffd98a` |
| water | `#7fc7e8` |

Culture colours use the Okabe–Ito colour-blind-safe set, and each culture always has a letter sigil:

| Culture | Colour | Sigil |
|---|---|---|
| Umari | `#e69f00` | U |
| Vesh | `#56b4e9` | V |
| Qoruun | `#009e73` | Q |
| Telessi | `#cc79a7` | T |

**Fonts.** All are from Google Fonts under the SIL Open Font License 1.1. They are referenced by
family name and are not in the repo.

| Font | Used for |
|---|---|
| Cinzel | Small-caps headings, the logo, ITHE |
| Cormorant Garamond | Display text of 20 px and up only |
| EB Garamond | Body text, place labels, power names, the chronicle |
| Inter | Numbers, keys and controls |

**Sizing rules.**
- No text is smaller than 12 px.
- Buttons are at least 40 px (palette buttons are 76×78 px).
- No signal relies on colour alone. Each one carries an icon and a word, or a sigil.

**Shared layout at 1080p.**
- **Top bar:** 0,0, 1920×76, gradient from ink .88 to transparent.
  - Left block starts at x 24: menu (44 px), world name (EB Garamond 26), age ring at 156,14 (48 px), then year and age.
  - Awe orb: 52 px, centred at about x 860, y 8.
  - Ark bar: 236 px wide, at right 300.
  - Speed buttons: 40×40 px, from x 1636, at y 18.
- **Power palette:** centred, bottom 20 px, about 355,938, 1210×122.
  - Four groups: Shape / Sky / Wrath / Grace.
  - Each button shows the icon (26 px), the name, the cost, and the hotkey (top right).
- **Place labels:** EB Garamond 500, 18 px (21 px for capitals), with an ink halo and a culture sigil.

## Images

### hud-orbit.png — the play HUD from orbit, late game
- **What it shows.** The planet Ithe in the Space Age, Year 3,212. Every element named in SPEC-UI §4 appears at once.
  - Some of these elements only appear in certain states (armed chip, first-steps card, toast, weight-3 banner, hover tip, legend). They are shown together here on purpose.
  - `#hud-eye` (the "show interface" eye) only appears when the HUD is hidden. Here it is represented by the "Hide interface · H" control in the overlay panel. When hidden, it sits alone at the top right, 40×40 px.
- **Background.** `src/bg/orbit.jpg`, made with the reference `shots/ue-cloudlab-terminator-final.png`. Prompt:
  > A small living planet seen from orbit, in exactly the look of the reference image: deep navy ocean with a broad soft sun glint, tan and green continents with fine ridges and a few river lines, dense white cumulus cloud fields and one spiral cyclone, a thin glowing blue atmosphere rim, the day-night terminator crossing the right third of the globe with warm golden city-light clusters on the night side, a small grey moon at upper left, black space with a few faint stars. The planet is large and centred slightly right, filling most of the frame height, with calm dark space around the top and bottom edges for interface overlays. Cinematic near-photoreal render, physically based lighting, subtle, no lens flare. No text, no labels, no UI, no watermark.
- **Layout** (x,y, w×h in px):
  - **Top bar.**
    - Menu, then "Ithe".
    - Age ring: rocket icon, 48 px, filled to 82 %.
    - "Year 3,212 / SPACE AGE".
    - Stats with icons: people 2.41 M, cultures 4, temperature +1.2 °C, sea +0.6 m.
    - Centre: Awe orb with "186 of 1,040 · +4.4 / yr" (Inter 600 30 px, awe colour), the epithet "the Rain-Giver" (Cormorant italic), and "Beloved" with a heart icon.
    - Ark bar: "They will leave you beloved", with "The Ark at Tel Asha · 38 %".
    - Speed: pause, linger, 1×, 2× (active), 4×, 8×.
  - **Weight-3 banner:** 560,104, 800 wide, with a radial ink fade (no box). It reads "Year 3,198 · The Umari" in Cinzel 13, then a 24 px Cormorant line.
  - **First-steps hint card** `#hud-genesis`: 24,96, 372×101.
  - **Chronicle feed:** 1556,96, 340×318. Three entries, each with a culture sigil and a year, plus "Open the Chronicle" (`#pan-chronicle-open`).
  - **Now strip** `#pan-now`: 1556,470, 340×186. War, plague, praying and storm, each as an icon plus words, coloured `#e2836f` / `#e8cd96` / `#b3b9c4`.
  - **Toast:** 1596,760, 300 wide. Reads "Not enough Awe for Volcano", with a 3 px bad-colour left rule.
  - **Cultures legend:** 24,600, 236 wide.
  - **Overlay switch:** 24,794, 236 wide. Options: None (active), Cultures, Temperature, Rainfall, Devotion, Habitability. Below them, "Hide interface · H".
  - **Target ring** on Tel Asha: 695,355, 80 px, dashed `#fff1c9`. Cursor at 758,420.
  - **Hover tip:** 786,444, 300×105.
  - **Armed chip** `#hud-armed`: 784,888, 353×44. Reads "Inspire · 60 Awe · Choose a settlement", with a ✕ button.
  - **Palette:** Inspire armed (gold border `#fff1c9`, 16 px glow); Volcano and Awaken poor (opacity .5).
  - **Labels:** Tel Asha (capital), Vornhal, Qe-Luun (praying glyph), Dar Imen (war glyph).

### hud-power-armed.png — a brush power armed, Iron Age
- **What it shows.** Close orbit over the main continent in Year 1,284. The player has Rain armed and is holding it over dry grassland.
  - This image also shows the palette's **locked** state (Meteor, Volcano, Awaken) and the **poor** state (Prophet, Peace).
  - It also shows the locked tooltip, worded as in SPEC-UI.
- **Background.** `src/bg/near.jpg`, made with the orbit image as reference. Prompt (paraphrased; see note below): the same planet seen from about 1,500 km over the main continent, curved horizon and atmosphere at the top, a large river system with tributaries, green lowlands and a dry tan upland, small town clusters along the river, scattered cumulus, ocean and turquoise shallows at the lower left, same warm low sun, near-photoreal, no text or UI.
- **Layout.**
  - **Top bar:** Iron-age state. Ring 64 %, Awe 96 of 340, +1.9 / yr, "the Green Hand", "Regarded", 18,400 people, 2 cultures, 1×.
  - **Rain brush ellipse:** svg at 420,390, 440×280 (rx 190, ry 110). Stroke `#7fc7e8`, fill at .12, with rain streaks. Cursor at 642,532.
  - **Hover tip:** 860,470, 296×105. Reads "Dry grassland / 24 °C · rainfall scarce / Rain · 5 Awe a second / + Thirsty land: they will notice this" (the last line in good green).
  - **Meteor tooltip:** 837,748, 340×135. Reads "Meteor · M · 150 Awe", then its one-line description, then a lock icon and "Wakes in the Sail Age, or when your Awe can hold 450."
  - **Armed chip:** 721,888, 478×44. Reads "Rain · 5 Awe / s · Hold over the land", with "Brush − / +" and ✕.
  - **Locked buttons:** opacity .38 with a lock icon at the top left.

### panel-settlement.png — settlement inspector
- **What it shows.** Tel Asha selected, Iron Age. The inspector (`#pan-inspector`, SPEC-UI §4b) is open.
- **Background.** `near.jpg`.
- **Layout.**
  - **Selection ring:** on Tel Asha at about 830,510.
  - **Inspector:** 1476,92, 420×749. Contents, in order:
    - Header "Tel Asha" (Cormorant 30) with sigil, then "City of the Umari · capital · Iron Age", and a close button.
    - "7,940 people, growing. The land could feed 9,600."
    - Buildings: an icon row in 40 px boxes.
    - Devotion bar at 72 %.
    - "How they feel about you": a love–fear scale (gradient `#e2836f` → `#7a6f66` → `#ffd98a`) labelled terrified / wary / indifferent / fond / adoring, with the marker at 71.
    - Status chips: "Praying for rain" and "Blessed, 12 more years".
    - Culture summary, with the epithet "Ashtavai, the Green Hand".
    - Neighbour line: Vesh, wary · trading.
    - Prophet line: "Dovak the prophet lives here, 41 more years".
    - Edicts in a 2×2 grid (Peace / Expand / Build / Crusade). Build is in force.

### panel-culture.png — culture view with the Cultures overlay
- **What it shows.** A **proposed** expanded culture view for the Umari, opened from the inspector's culture line. SPEC-UI has no separate culture panel; this is an open question.
  - The Cultures overlay is active. Territories are tinted with screen-blended radial washes and dotted borders.
- **Background.** `orbit.jpg`.
- **Layout.**
  - **Culture panel:** 1476,92, 420 wide. Contents:
    - "The Umari / Umaran · the first people · capital Tel Asha"
    - 1.62 M people, 14 towns, Space Age
    - "What they call you": *Ashtavai, the Rain-Giver*, with earlier epithets and their years
    - Devotion 58 %, and the love scale at 80
    - Four trait bars (Piety, Aggression, Curiosity, Seafaring)
    - Neighbours, each with an icon plus a word: wary / friendly / truce, 22 years
    - "Prophet and edicts". Edicts are dimmed to .5 because no prophet is present.
  - **Legend:** 24,600, 236×178.
  - **Overlay switch:** 24,794, 236×253, with Cultures active.

### chronicle-book.png — the Chronicle book
- **What it shows.** `#pan-chronicle` open as a two-page book over a dimmed planet (brightness .6, 1.5 px blur).
  - Filters: by people (All / the Umari / the Vesh) and by weight (Everything / Notable and great / Great only).
  - Weight-3 entries get a Cinzel drop cap and gold-hi text.
  - One entry is shown hovered, with "Fly to Dar Imen".
- **Text.** Real lines from `docs/content/chronicle-voice.md`, with the placeholders filled in: entries 1, 4b, 8, 25a, 12c, 13, 23, 27b, 28b, 29, 36 and 40.
- **Layout.**
  - Title "The Chronicle of Ithe" (Cinzel 40): 300,28.
  - Filters: 32 px pills at y 106.
  - Book: 300,156, 1320×880, gold-hi border at .55, with a darker spine gradient at 50 %.
  - Each entry is a grid: a 96 px year column (Cinzel 12, gold) and the text (EB Garamond 20 / 22 px).
  - Close button at top right.
  - Scroll thumb at the right edge.

### screen-title.png — title screen
- **Background.** `src/bg/title.jpg` (orbit as reference). Prompt (paraphrased): the same planet whole on the right half of the frame, half in night with golden city lights, the small moon at upper right, wide calm black space on the left for a title, near-photoreal, no text.
- **Layout.**
  - Left ink gradient, 1100 px wide.
  - "DEMIURGE": Cinzel 500, 104 px, letter-spacing .22em, `#fff1c9`, at 160,250.
  - Tagline "A world in your hands.": EB Garamond italic 28 px, gold.
  - Menu buttons: 400×56 at x 160, y 478, with 12 px gaps.
    - Continue: primary, with "Ithe · Year 3,212 · Space Age" in Inter 14.
    - New world.
    - Settings.
    - How to play.
  - Credit line at 164, bottom 48, 13 px.

### screen-genesis.png — a genesis step (water)
- **Background.** `src/bg/genesis.jpg` (orbit as reference). Prompt (paraphrased): the same planet in its genesis water phase, a dark basalt crust with glowing orange lava cracks, the first shallow blue seas pooling in basins, two comets with long tails streaking in from the upper left, faint steam, the moon at upper left, near-photoreal, no text.
- **Layout.**
  - Minimal top bar: "Genesis / The second day: water", time controls, and no Awe yet.
  - Comet aim reticle: 900,420, 90 px. Cursor at 948,468.
  - **Genesis card** `#hud-genesis`: 640,656, 640×264. Contents:
    - Steps Molten · **Water** · Life · People.
    - SPEC copy: "Call down comets to fill the seas. Raise or lower the land while the clay is soft." (EB Garamond 24, gold-hi).
    - "Ocean 41 % · a temperate world".
    - Water bar with the temperate band (35–75 %) bracketed in gold.
    - "Continue to life" button `#hud-genesis-next`, 44 px.
  - **Genesis palette:** 818,938. Comet (armed, free) and Shape: Raise and Lower, both free.

### screen-ending.png — the "Beloved" ending
- **Background.** `src/bg/ending.jpg`, resized from our merged T-002 image `docs/concept/landmarks/power-ark-launch.png`.
- **Layout.**
  - Left ink gradient.
  - Text block at 120,96, 820 wide:
    - "The ending of Ithe · Year 3,214".
    - "Beloved" in Cormorant 84.
    - The **ark-beloved** paragraph from chronicle-voice.md §4, filled in (EB Garamond 23).
    - "The tale": entries 2, 3, 1 and 23 in two columns, 18 px.
  - Reckoning panel: 1490,330, 390×542. Contents:
    - Years, ages, people, peoples, wonders, wars, warming.
    - Score 8,460 (Inter 34, awe colour).
    - Each culture's last epithet, with its sigil.
  - Buttons at 120,968, 48 px high: Watch the ages (primary), Planet card, Keep playing, New world.

### planet-card.png — shareable end card
- **What it shows.** The current frame (here the orbit background) with a double gold inset frame: 1 px at 36 px from the edge (.55) and 1 px at 44 px (.22).
- **Layout.**
  - "DEMIURGE" (Cinzel 16) at 96,84. The seed at top right.
  - "ITHE": Cinzel 92 px, letter-spacing .24em, at bottom-left 96.
  - "Kept by Ashtavai, the Rain-Giver" (EB Garamond italic 28).
  - A one-line summary.
  - Key stats at the bottom right: Years, Age, People, Peoples (sigils), Ending (good colour), Score (awe colour). Labels in Cinzel 12, values in Inter 600 24.
  - A bottom ink gradient keeps the text readable.

### icons-sheet.png — icon set
- **What it shows.** All 72 icons from `src/icons.js`. Every icon uses one line style: 24×24 grid, 1.6 px stroke, round caps and joins, no fills, `currentColor`.
  - Powers: 16
  - Ages: 7 (hand-axe, bronze blade, anvil, sail, chimney, atom, rocket)
  - Buildings: 12
  - Statuses: 7 (plague, famine, war, blessed, inspired, praying, rising sea)
  - Interface: 31
- **Layout.** 68 px tiles with 36 px icons and 13 px labels. A strip shows the rain icon at 16 / 20 / 24 / 32 / 48 px to check legibility.

## Notes and open questions
- **Background prompts.** The orbit prompt above is exact. The near, title and genesis prompts are given as faithful paraphrases; their exact wording was not kept.
- **Fonts.** SPEC-UI says "no external fonts", but that rule came from the old web build. These mockups propose four OFL fonts. Should the Unreal build ship them?
- **Culture panel.** `panel-culture` proposes a panel that SPEC-UI does not define yet.
- **hud-orbit density.** hud-orbit is deliberately dense. In play, the hint card, toast, banner and armed chip rarely appear together.
