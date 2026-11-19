# T-009 — Add the open-licence UI fonts to the repository

**Assignee:** Grok · **Status:** open · **Branch:** `helper/grok/T-009`
**Deliver to:** `assets/fonts/` only.

## Why
The approved UI mockups (T-007, `docs/concept/ui/index.md`) use Cinzel, Cormorant Garamond,
EB Garamond and Inter. The lead answers your question: **yes, the Unreal build will ship them**,
replacing the Windows system fonts the HUD uses as a stand-in (those cannot ship). The build
machine has no network, so the font files must come through the repository.

## Deliver
1. The static TTF (or OTF) files of each family, only the weights and styles the mockups use
   (list them), from the official upstream sources (Google Fonts or the families' own
   repositories): `assets/fonts/<family>/<file>`.
2. Each family's licence text next to its files (`assets/fonts/<family>/OFL.txt`).
3. `assets/fonts/README.md`: per family the source URL, version, date fetched, licence (SIL OFL
   1.1), the files included and which UI role each serves (copy the table from T-007).

## Acceptance criteria
Unmodified official files; licence text included for every family; no fonts beyond these four;
total size reasonable (under about 10 MB).
