# UI fonts (T-009, Grok)

These are the four open-licence families used by the approved UI mockups (T-007,
`docs/concept/ui/index.md`). The Unreal build ships them in place of the Windows system fonts that
the HUD currently uses as a stand-in.

All four are licensed under the **SIL Open Font License 1.1**. That licence lets us bundle, embed and
ship them in a commercial game. It does not allow selling the fonts on their own, and a modified
version may not keep a Reserved Font Name. The full licence text sits next to each family's files
as `OFL.txt`.

- **Files are unmodified upstream files.** They keep their upstream file names. Each one was checked
  with fontTools: name table and version match the release, and the static files have no `fvar`
  table.
- **No network is needed.** Everything the build needs is in this folder.
- **Date fetched:** 2026-11-19 for all files.

## Summary

| Family | Version | Source | Licence |
|---|---|---|---|
| Cinzel | 2.000 | https://github.com/NDISCOVER/Cinzel (commit `dd598495b0fb2ad84270d5cc75d642d2f1e8eabf`), the upstream repo that Google Fonts builds from | SIL OFL 1.1, Reserved Font Name "Cinzel Decorative" |
| Cormorant Garamond | 4.002 | https://github.com/CatharsisFonts/Cormorant/releases/tag/v4.002, asset `Cormorant_Install_v4.002.zip` | SIL OFL 1.1 |
| EB Garamond | 1.002 (Regular, Medium), 1.001 (Italic) | https://github.com/octaviopardo/EBGaramond12 (commit `106a4a6d377987459ae5e68673a4570f13b957fb`), the upstream repo that Google Fonts builds from | SIL OFL 1.1 |
| Inter | 4.1 (font version 4.001) | https://github.com/rsms/inter/releases/tag/v4.1, asset `Inter-4.1.zip`, folder `extras/ttf/` | SIL OFL 1.1 |

## Files per family

**Cinzel** — `cinzel/`
- `Cinzel-Regular.ttf` (from `fonts/ttf/`)
- `Cinzel[wght].ttf` (variable, 400–900, from `fonts/variable/`; see note 1)
- `OFL.txt`

**Cormorant Garamond** — `cormorant-garamond/` (the release ships static fonts as OTF only)
- `CormorantGaramond-Regular.otf`
- `CormorantGaramond-Medium.otf`
- `CormorantGaramond-SemiBold.otf`
- `CormorantGaramond-Italic.otf`
- `OFL.txt` (from the zip)

**EB Garamond** — `eb-garamond/`
- `EBGaramond-Regular.ttf`, `EBGaramond-Medium.ttf` and `EBGaramond-Italic.ttf` (from `fonts/ttf/`)
- `OFL.txt` (from the repo root)

**Inter** — `inter/`
- `Inter-Regular.ttf`, `Inter-SemiBold.ttf` and `Inter-Bold.ttf`
- `OFL.txt` (the zip's `LICENSE.txt`, unchanged except for its name)

## UI roles (from T-007)

| Font | Used for | Weights and styles the mockups use | File |
|---|---|---|---|
| Cinzel | Small-caps headings, group names, the DEMIURGE logo, the ITHE card title | 400 (small caps, 12–16 px) | `Cinzel-Regular.ttf` |
| | | 500 (logo, book title, card title) | `Cinzel[wght].ttf` at wght 500, or Regular (see note 1) |
| Cormorant Garamond | Display text of 20 px and up: panel titles, year, "Beloved", tooltip headings | Regular 400 (culture legend) | `CormorantGaramond-Regular.otf` |
| | Epithets and the Ark line in italic | Medium 500 (ending title) | `CormorantGaramond-Medium.otf` |
| | | SemiBold 600 (panel titles, year, armed-power name, tooltip headings) | `CormorantGaramond-SemiBold.otf` |
| | | Italic 400 (epithets, Ark mood line, culture "what they call you") | `CormorantGaramond-Italic.otf` |
| EB Garamond | Body text, chronicle, place labels, power names, world name, menu items | Regular 400 (body, chronicle, menu) | `EBGaramond-Regular.ttf` |
| | | Medium 500 (place labels, power names, edict names, world name) | `EBGaramond-Medium.ttf` |
| | | Italic 400 (tagline, card subtitle, ending epithets) | `EBGaramond-Italic.ttf` |
| Inter | Numbers, keys, controls, stats | Regular 400 (stats, controls, small text) | `Inter-Regular.ttf` |
| | | SemiBold 600 (Awe number, ocean %, reckoning values, score) | `Inter-SemiBold.ttf` |
| | | Bold 700 (letters in culture sigils) | `Inter-Bold.ttf` |

Total size is about 4.6 MB.

## Notes

1. **Cinzel has no static Medium.** Upstream only ships Regular, Bold and Black as static files.
   The mockups' 500 weight came from the variable font, so `Cinzel[wght].ttf` is included unchanged.
   If the build's font importer cannot select a variable instance, use `Cinzel-Regular.ttf` for
   those few titles. The difference at display sizes is slight.
2. **Inter uses static files.** The static TTFs come from the official release, not the Google
   Fonts variable `Inter[opsz,wght]`. They are the same design. The static files keep the build
   simple.
3. **File names** keep their upstream form (mixed case, and brackets for the variable font) so
   that every file can be traced to its source unchanged. This is an exception to the repo's
   lowercase-hyphen naming rule; the family folders follow the rule.
