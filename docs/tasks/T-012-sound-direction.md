# T-012 — Sound and music direction

**Assignee:** Grok · **Status:** open · **Branch:** `helper/grok/T-012`
**Deliver to:** `docs/content/sound-direction.md` only.

## Why
All sound and music in DEMIURGE is synthesised in code (no recordings), and the engineers building
it cannot listen to their output. They need a precise written target they can turn into numbers.

## Read first
`AGENTS.md`; `docs/CONCEPT.md`; `docs/SPEC-UI.md` section 7 (audio and music); `docs/SPEC-UNREAL.md`
module C9 (audio).

## Deliver `docs/content/sound-direction.md`
1. **Music by age** (Stone to Space) and by mood of the world (peace, war, plague, golden age,
   doom): for each, tempo range (BPM), mode or scale, instrument timbres described as synthesis
   (for example "plucked string: Karplus-Strong, bright attack, 1.5 s decay"), texture, dynamics,
   and how one blends into the next.
2. **Ambience by zoom level** (space, high orbit, clouds, landscape, town) and by biome.
3. **Every power and disaster cue** in SPEC-UI section 7: a description, duration, layers (attack,
   body, tail), frequency ranges, loudness relative to music, and what makes it feel godlike rather
   than cartoonish.
4. **UI sounds:** click, hover, arm, cancel, toast, chronicle entry, age change, ending.
5. **Mix rules:** loudness targets (LUFS), ducking, a limiter ceiling, maximum simultaneous
   voices, what must never happen (harshness, clipping, fatigue over a 40-minute session).
6. **Reference descriptions** in words only (no copyrighted tracks named as targets).

## Acceptance criteria
Concrete enough that an engineer could synthesise each sound from the text alone; consistent
with the game's tone (mythic, warm, a little wry).
