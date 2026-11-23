# T-021 — Design review of the god-interaction systems (Mythos)

**Assignee:** Astra · **Status:** open · **Branch:** `helper/astra/T-021`
**Deliver to:** `docs/reviews/astra-mythos-review.md` only.

## Why
`docs/SPEC-MYTHOS.md` adds 28 ways to be a god beyond the power palette: decrees, messengers, the Word,
judgement and mercy, pacts, legends, omens, trials, faiths and more, delivered through decision cards with a
director that paces them. Version 1 is being built in the simulation now. Your T-006 review was the sharpest
input the project has had on the core rules; this is the same kind of review for the new layer, before
versions 2 and 3 are built.

## Read first
`AGENTS.md`; `docs/CONCEPT.md`; `docs/SPEC-MYTHOS.md` in full (section 0 has the owner's decisions and the
binding principles); your own `docs/reviews/astra-design-review.md`; `docs/reviews/gameplay-v4.md`;
`docs/content/cultures.md`.

## Deliver
A player-focused review of the Mythos layer:
- Do the cards create real decisions (several defensible answers, visible consequences years later), or
  chores? Is the director's pacing (first card about year 90, minimum gap 70, target 200, forced after 420,
  at most 2 open) right for a 30–40 minute history? How does it share attention with your Risks-and-pleas
  proposal (the lead decided they share one late-game attention budget)?
- Can a kind, a cruel and an absent god each have a full, different game? Where does one of them run out
  of things to do?
- Which of the 28 systems are strongest, which overlap, and which would you cut or merge for version 2?
- Free will: do agency, obedience and dependence (3.4) make the people feel like people rather than units?
- Neutral naming (0.2): does any wording lean toward one real religion more than the light echoes the
  owner likes? Suggest neutral alternatives where it does.
For each finding give the problem, the spec section, a concrete proposed change, and a headless test
(simrun policy or SimTests setup) that would show it worked. Rank by impact, and close with the five
changes you would make before version 2.

## Acceptance criteria
Specific, actionable and tied to spec sections; do not rewrite the spec itself. Do not design a playable
mortal avatar (the owner reserved it). Original analysis only; no copied design from existing games.
