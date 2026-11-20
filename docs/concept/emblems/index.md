# T-019 · Culture emblems and banners (Grok)

Sixteen invented emblems drawn in the same line style as the UI icons from T-007 (`docs/concept/ui/src/icons.js`):

- 24×24 viewBox
- stroke `currentColor`, 1.6 px wide
- round caps and joins
- no fills

The emblems are drawn in code as SVG and rendered with headless Chrome. No image generator was used, except that `banners.png` reuses the T-001 culture-town images as backgrounds. All lettering on the sheets is real text in OFL fonts from `assets/fonts/` (Cinzel, Inter and Cormorant Garamond).

| File | What it is |
|---|---|
| `emblem-01.svg` … `emblem-16.svg` | The emblems: 260–550 bytes each, one `<svg>` with paths, circles or ellipses, no text, valid XML |
| `emblems-sheet.png` (1920×1080) | All 16 in circular badges, each in one of the culture colours. Includes 16 px filled badges, 24 px outline badges, bare 16 px signs and a 4× pixel enlargement of them, plus one sign in all six `CULTURE_COLORS` |
| `banners.png` (1920×1080) | Close-ups of a hall banner, a mast pennant, a round shield and a roof flag. At game scale, the three culture towns from `docs/concept/settlements/` with small flags on their landmarks (dashed rings mark the main flag) |
| `src/emblems.js` | The emblem data (path strings) and an `svg()` helper |
| `src/write-svgs.js` | Writes the 16 SVG files: `node docs/concept/emblems/src/write-svgs.js` |
| `src/emblems-sheet.html`, `src/banners.html`, `src/render.js` | The sheets. Render with `node docs/concept/emblems/src/render.js emblems-sheet` (and the same for `banners`). |
| `src/zoom16.png` | The 4× pixel crop shown on the sheet. It is made in two passes: render once, crop the 16 px row whose box is written to the page title, then render again. |

**Where fills are allowed.** Only the dots are filled: the three stars of emblem 04 and the five hearth stones of emblem 15. These are `<circle r="1.1"/>` and `<circle r="1.2"/>` with `fill="currentColor"`, as tiny dots also are in the UI icon set. Everything else is stroke only.

**How colour works.** Any emblem works in any culture colour, in one of two ways:
- **Filled badge:** a culture-colour disc with the sign in ink `#0b0f17`.
- **Outline badge:** an ink disc with a culture-colour ring and sign.

On cloth, the sign is ink on the culture colour, or cream `#f3e6c4` on dark colours (see the green roof flag).

## The emblems

The names and meanings are written in the chronicle's voice. "Suits" lists the culture styles the emblem fits:

- **A** = Cliff-Terrace (`settlements/culture-a.png`)
- **B** = Lagoon-Weaver (`culture-b.png`)
- **C** = Ring-Builder (`culture-c.png`)
- Family names are the eight families in `docs/content/cultures.md`.

| # | Name | Meaning | Suits |
|---|---|---|---|
| 01 | The Swell | "The sea gave them nothing for free, and they went out on it anyway." | B; Velessan, Hanuwe |
| 02 | The Anvil Pass | "They forged in the high gaps, where the wind worked the bellows for them." | A, C; Korvagh |
| 03 | The Lake Birch | "One white tree standing in still water, which they took to mean patience." | C; Aavik |
| 04 | The Star Road | "Three lights over the dunes, and a promise that the road runs under them." | A; Qa'tiri |
| 05 | The Claw Sail | "A sail cut to catch the wind like a hand, and never give it back." | B; Hanuwe, Velessan |
| 06 | The Shade Tree | "Under its branches every quarrel was settled, slowly." | any; Mbendi |
| 07 | The Curled Prow | "A ship that looks back over its shoulder at home." | B; Thrask |
| 08 | The Lamp of Law | "One flame kept burning in the hall of judgement, so that no verdict is given in the dark." | A, C; Pheraxi |
| 09 | The Stacked Spire | "Stone set on stone until it nearly reached the god, and then one more." | C |
| 10 | The Cliff Stair | "Every house a step, and every roof a floor for someone else." | A |
| 11 | The Braid | "Three reeds woven into one rope, which is how they explained marriage, trade and the lagoon." | B |
| 12 | The Antler Crown | "The first shrine wore antlers, and so did the first king, briefly." | any; ties to the shrine (T-014) |
| 13 | The Sheaf | "The harvest bound, the year survived." | any farming people |
| 14 | The Rain Stair | "A ladder up to the cloud, for a god who answers rain prayers." Good for a culture whose epithet is the Rain-Giver. | A, C |
| 15 | The Hearth Ring | "One fire, and the stones that keep it." Echoes the Stone-age wonder in T-017. | any; early peoples |
| 16 | The Hive | "A people who store for winter and sting when robbed." Echoes the Bronze-age wonder in T-017. | A, C |

**How a culture could get its emblem (suggestion only; the spec is unchanged).**
- Seed-pick from the emblems that fit the culture's family and settlement style.
- A daughter culture (`culture.parent ≥ 0`) keeps its parent's emblem and adds a ring, or picks a neighbouring emblem from the same family list.
- The current HUD letter sigil could stay as the fallback, or sit beside the emblem in text panels.

## Acceptance criteria
- **Original.**
  - None of the emblems is a real-world flag, religious symbol or game insignia.
  - I checked shapes against common symbols and changed two drafts:
    - A pillar in a ring read like the zodiac sign ♊, so it became the Lamp of Law.
    - A dome with stars repeated the Hive's silhouette, so it became the Star Road.
  - The design avoids crosses, crescents, stars of David, wheels with spokes, triskeles and eyes.
- **Readable at 16 px.** See the bare signs and the 4× pixel strip. Every sign keeps a distinct silhouette. The 16 px *filled* badge (an 11 px sign) is the limit; for map markers, 20 px or more is better.
- **Consistent with the UI icon set:** same grid, stroke, caps, joins and dot convention.
- **SVGs valid and minimal:** parsed with an XML parser; each has one root `<svg>`, with no metadata, ids, styles or transforms except one `rotate` on each of the two side ears in the Sheaf.

## Known misses
- **Game-scale flags are 8–13 screen px** in the town views. That is about 2× real size, so they read as marks.
- **Close-up cloth is stylised:** gradient folds, a turbulence wave and a weave texture. It is not photoreal; it is a reference for layout and colour, not final cloth.
- **Two similar silhouettes:** Hive and Cliff Stair both use a doorway arch motif. At 16 px they stay distinct (a dome versus steps).
- **Some emblems need size:** Shade Tree and Curled Prow are the busiest signs and are best at 20 px or more.
