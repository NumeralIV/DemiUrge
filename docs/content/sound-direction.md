# Sound and music direction (proposal, T-012, Grok)

Every sound in DEMIURGE is synthesised in code, and nobody on the team can listen to the output. This file is the written target to turn into numbers.

It covers `DM.Audio` and `DM.Music` (SPEC-UI §7) and the Unreal module C9 `audio` (SPEC-UNREAL). All writing is original, and no recording or existing piece of music is a target.

**Tone** (CONCEPT.md): mythic, warm, a little wry. In sound, that means:
- **Mythic:** big, slow and far away. Long tails, low fundamentals, plenty of air and space. Nothing is fast or busy.
- **Warm:** soft attacks and rolled-off highs. Bright edges go only on the moments that deserve them.
- **A little wry:** small, human sounds in a godlike space. A goat bell under a thunderstorm. A chime that is slightly too pleased with itself when you heal a village.

## 0. Conventions used in this file

- **Levels:**
  - dB values are relative to full scale (dBFS) on the master unless they say "re music", which means relative to the music bus's short-term loudness at that moment.
  - LUFS figures are ITU-R BS.1770 integrated or short-term (3 s) loudness.
- **Envelopes** are written `A/D/S/R`:
  - attack, decay and release in ms or s
  - sustain as a level from 0 to 1
  - "exp" means an exponential curve (`setTargetAtTime`), with the time constant given as τ
- **Frequencies** in Hz. A "band" is a band-pass filter (`BiquadFilter` type `bandpass`) with centre frequency `f` and quality factor `Q`.
- **Noise** means white noise from a 2 s looped buffer of uniform random values, filled once from the world rng.
  - **Pink noise** is that buffer passed through the standard three-pole pinking filter (a fixed IIR).
  - **Brown noise** is that buffer integrated with leak 0.02.
- **Karplus–Strong (KS):** a noise burst with length = one period, fed into a delay line of that period, with a feedback low-pass (two-point average) and loop gain `g`. Decay time T60 ≈ `−3 / (f · ln g)` seconds. Pick `g` from the decay given below.
- **FM pairs** are written `carrier:modulator ratio, index I`. The index can follow its own envelope.
- **Reverbs:** all of them are one shared convolution reverb per bus, with impulses generated in code. Impulse = noise × exp decay, low-passed over time. Three impulses are used:
  - **Hall:** 3.2 s, pre-delay 30 ms, damping 6 kHz falling to 2 kHz over the tail
  - **Sky:** 7 s, pre-delay 80 ms, damping 3 kHz falling to 900 Hz. This is the "godlike" space.
  - **Room:** 0.6 s, pre-delay 8 ms, damping 5 kHz
- **Pitch:** A4 = 440 Hz. The world's **tonic** is chosen from the seed among D2 to G2 (73.4 to 98.0 Hz), so every world has its own key. Tones of the world's chosen scale are called scale degrees (1, 2, 3, …).
- **Randomisation:** every one-shot gets ±3 % pitch and ±1.5 dB of random variation per play, from the audio rng, which is seeded and not `Math.random`, so offline tests stay deterministic.

## 1. Music

### 1.1 How the score works

The score is **generative and sparse**. It is built from **phrases**, short gestures of 2–7 notes, placed on a slow grid with a lot of rest between them. One phrase plays roughly every 4–12 s.

**Four layers:**

| Layer | Content | Where it sits |
|---|---|---|
| **Ground** | a drone or pedal on degree 1, sometimes 5 | always present while the music bus is on |
| **Voice** | the age's lead instrument, which plays the phrases | |
| **Colour** | a second instrument that answers a phrase or holds a chord tone | |
| **Pulse** | a percussion or ostinato figure | only in some ages and moods |

**Zoom thinning** (SPEC-UI: "thins out when zoomed far away"):

| zoom01 | Layers playing |
|---|---|
| < 0.2 (space) | ground only, plus very sparse voice phrases at −6 dB |
| 0.2 to 0.6 | ground, voice, colour |
| > 0.6 | all four layers |

Layers fade in and out over 8 s and never cut.

**Harmony.** Each world picks a scale family from its seed. Each age then picks a **mode** inside that family, so the music changes character with the age while staying "the same world". Two families:

| Family | Ages 0–1 | Ages 2–3 | Ages 4–5 | Age 6 |
|---|---|---|---|---|
| **Even** (seed parity 0) | major pentatonic | Mixolydian | Dorian | Lydian |
| **Odd** (seed parity 1) | minor pentatonic | Dorian | Aeolian | Lydian with a raised 2 used sparingly |

Chord changes happen every 16–32 s. Only the ground and colour move; the ground moves between degrees 1, 4 (or 5) and 6, and never more often than that.

**Phrase grammar:**
- Phrases start on degrees 1, 3 or 5 and end on 1, 2 or 5.
- Steps are mostly ±1 or ±2 scale degrees, with one leap allowed per phrase (up to a 6th).
- Note lengths are drawn from {1, 1, 2, 3} beats.
- After every three phrases, the fourth repeats the first phrase transposed by a 4th or 5th, so it sounds composed rather than random.

**Silences matter.** In any 60 s window, the voice is silent at least 50 % of the time.

### 1.2 Music by age

**Stone (age 0): bone flute and hand drum.**
- **Tempo:** 56–66 BPM, but the pulse is felt, not a grid. Each note onset is jittered by ±40 ms.
- **Scale:** pentatonic, range limited to a 6th, voice in the octave above middle C (C5).
- **Voice, bone flute:**
  - Sine plus 18 % 2nd harmonic plus 6 % 3rd.
  - Breath noise: pink noise through a band at 2× the note frequency, Q 1.5, at −20 dB re the tone, with its own 30 ms attack.
  - Envelope 80 ms / 0 / 1 / 400 ms.
  - Vibrato 4.5 Hz, depth 12 cents, fading in after 300 ms.
  - Pitch slides up 30–60 cents into each note over 120 ms (an imperfect, human instrument).
- **Pulse, hand drum (frame drum):**
  - Body: sine at 90 Hz with a pitch drop to 70 Hz over 60 ms. Envelope 2 ms / 220 ms exp.
  - Skin: noise through a band at 1.1 kHz (Q 0.8), 1 ms / 40 ms.
  - Pattern: 2–4 hits per bar, never on every beat, with a ghost hit at −14 dB.
- **Ground:** a hummed drone. Two detuned sines at the tonic (±3 cents) through a low-pass at 600 Hz, with a 0.07 Hz amplitude swell of ±2 dB.
- **Texture and dynamics:** very bare and very quiet. Music bus short-term loudness about −30 LUFS.

**Bronze (age 1): the flute is joined by the lyre.**
- **Tempo:** 60–70 BPM.
- **Scale:** pentatonic, range widened to an octave.
- **Colour, lyre:** KS pluck.
  - Excitation: 3 ms of noise low-passed at 5 kHz, which gives the bright attack.
  - Loop gain set for a 1.8 s T60.
  - Strummed chords: 3 notes, 25 ms apart, low to high.
- **Pulse:** the drum continues, now with a second, higher drum at 180 Hz on off-beats.
- **Change from Stone:** the lyre answers the flute's phrases one beat after they end. Same family of sound, just more of it. Loudness about −29 LUFS.

**Iron (age 2): lyre and reed.**
- **Tempo:** 64–76 BPM.
- **Scale:** the modal step from §1.1 (Mixolydian or Dorian).
- **Voice, reed (a double-reed shawm feel):**
  - Sawtooth through a band at 1.2 kHz (Q 2) plus a band at 2.6 kHz (Q 3), mixed 1 : 0.4.
  - Low-pass at 3.5 kHz.
  - Envelope 50 ms / 100 ms / 0.8 / 300 ms. Vibrato 5.5 Hz, 15 cents.
  - **The reed must never exceed −6 dB re the ground plus colour.** Reeds become harsh fast.
- **Colour:** the lyre, now also playing 2-note fifths.
- **Pulse:** a low drum with a 60 Hz body (high-passed at 45 Hz) on beat 1 every second bar, plus a shaker (noise through a band at 6 kHz, 1 ms / 30 ms, at −24 dB).
- **Ground:** the drone becomes a bowed fifth: two sawtooths at the tonic and the 5th, low-passed at 400 Hz.

**Sail (age 3): strings and bells arrive.**
- **Tempo:** 66–80 BPM.
- **Scale:** the full mode, range about 1.5 octaves.
- **Voice, solo string (bowed):**
  - Sawtooth through a low-pass that follows the envelope (800 Hz rising to 2.5 kHz during the attack).
  - Plus a formant band at 900 Hz (Q 1.2).
  - Envelope 250 ms / 0 / 1 / 900 ms. Vibrato 5 Hz, 10 cents, delayed 400 ms.
- **Colour, bell:**
  - FM 1 : 3.5, index 2.5 decaying to 0.3 over 2 s.
  - Envelope 1 ms / 3 s exp. Plus an undamped partial at 2.76× for shimmer, at −12 dB.
- **Ground:** a string pad. Four detuned sawtooths (±6 cents), low-pass at 1.2 kHz, attack 3 s.
- **Pulse:** the lyre carries an ostinato of four notes per bar, very soft (−18 dB re voice).
- **Feel:** the first age that sounds like an orchestra in a hall, so use the hall reverb with a 25 % send.

**Industry (age 4): brass and piano.**
- **Tempo:** 72–92 BPM, with a clearer grid (jitter ±10 ms).
- **Scale:** Dorian or Aeolian; the tonic minor colour arrives.
- **Voice, piano:**
  - Additive, 8 partials with a slight stretch (partial n at n × (1 + 0.0004 n²) × f).
  - Hammer noise: 4 ms of noise band-passed at 3 kHz.
  - Each partial decays over (4 / n) s. Velocity raises the cutoff of a gentle low-pass, from 2 kHz to 6 kHz.
- **Colour, brass (a soft horn section):**
  - Sawtooth with a low-pass whose cutoff swells from 400 Hz to 1.8 kHz over 300 ms (the "blat"), Q 0.9.
  - Envelope 120 ms / 0 / 1 / 500 ms.
  - Only on held chord tones, and never louder than mezzo-forte.
- **Pulse:** a soft mechanical ostinato, the first hint of machines. A muted KS pluck with a 0.25 s decay, playing eighth notes on two pitches (1 and 5), at −20 dB.
- **Ground:** the string pad, darker (low-pass 900 Hz).

**Atomic (age 5): brass, piano and electricity.**
- **Tempo:** 76–96 BPM.
- **Scale:** Aeolian or Dorian. The 6th degree is used for tension.
- **Voice:** the piano, now with phrases in octaves.
- **Colour:** the brass and an electric organ.
  - Organ: 5 sines at 1, 2, 3, 4 and 8× f, levels 1, .8, .5, .3, .2.
  - Rotary wobble: amplitude modulation at 6 Hz, 15 %.
- **Pulse:**
  - A ticking clock figure: a sine blip at 2 kHz, 2 ms / 15 ms, every beat, at −28 dB.
  - A heartbeat-like low thump: a 55 Hz sine at −20 dB, high-passed at 40 Hz to protect the sub-bass budget.
  - This is the age of nuclear tension, so the pulse should feel like waiting.
- **Ground:** a low synth string, with a long two-pole low-pass at 700 Hz.

**Space (age 6): synth pads.**
- **Tempo:** 52–64 BPM. It slows down again: the people are looking up.
- **Scale:** Lydian. Its raised 4th is the "wonder" note, so use it at the top of phrases.
- **Voice, glass lead:**
  - Sine plus a sine at 2.01× f (an octave, slightly detuned, which beats slowly), at −8 dB.
  - Envelope 400 ms / 0 / 1 / 2 s, sent 35 % to the sky reverb.
- **Colour, pad:**
  - Six sawtooths spread ±15 cents through a low-pass at 1.5 kHz.
  - The filter is modulated by a 0.05 Hz LFO (±400 Hz). Attack 4 s, release 6 s.
- **Pulse:** an arpeggio of four notes, rising, a sine with a 5 ms / 300 ms envelope, 8th notes, at −22 dB. It plays only at zoom01 > 0.6.
- **Ground:** sub-free. The drone sits at tonic × 2 (an octave up), so the low end is left to the launch roar.
- **Callback:** **the bone flute returns** for one phrase in every four. The same people, a long way from the cave.

### 1.3 Music by mood

The mood is computed from the world state, so it is a derivation and not a new sim field. Moods overlay the age music; they do not replace it.

| Mood | Trigger (suggested) | What changes |
|---|---|---|
| **Peace** (default) | no war, no plague, no disaster in the last 20 years | The age music as written. |
| **War** | any active war (`war:declared` until `war:ended`) | <ul><li>Tempo +8 %.</li><li>A low drum on every bar: 70 Hz body, 3 ms / 300 ms.</li><li>The mode flattens its 3rd and 7th (major → minor colour).</li><li>A low brass or bowed-string pedal on degree 5.</li><li>The voice plays shorter phrases with more rests.</li><li>The darker layer from SPEC-UI.</li></ul> |
| **Plague** | any settlement with the `plague` status | <ul><li>Tempo −10 %.</li><li>Pulse removed.</li><li>The voice is detuned by −12 cents and drifts (a 0.1 Hz LFO, ±8 cents): sickly but not grotesque.</li><li>A distant toll: a bell, one hit every 16–24 s, at −10 dB re music.</li><li>The ground drops the 5th and keeps only the tonic.</li></ul> |
| **Golden age** | total devotion high and population rising for 30 years, or a miracle just landed | <ul><li>Tempo +5 %.</li><li>The colour plays full chords.</li><li>The ground adds the 3rd.</li><li>A high sparkle layer: bell partials at 2–4 kHz, very sparse (one every 3–6 s), at −24 dB.</li><li>Hall send +10 %.</li></ul> |
| **Doom** | CO₂ very high, sea rising fast, a nuclear event, or the world shrinking (population falling 20 % in 10 years) | <ul><li>Tempo −15 %.</li><li>The ground becomes a cluster: tonic plus a minor 2nd, very soft, beating at about 3 Hz.</li><li>The voice is reduced to a single long note per phrase.</li><li>A low "wind through the rafters": brown noise through a band at 180 Hz (Q 4), swept ±40 Hz at 0.03 Hz.</li><li>Doom never gets loud. It gets empty.</li></ul> |

**Blending between ages and moods.** No hard cuts anywhere.
- **Age changes:**
  - Wait for the end of the current phrase, then play the `age` stinger (§3.3).
  - Then crossfade over **12 s**. The old ground fades out while the new ground fades in, already on the new mode's tonic, which is the same tonic: only the mode changes.
  - The new voice's first phrase is the old age's last phrase, played on the new instrument. This hands the melody across the ages.
- **Mood changes:**
  - Overlays fade in over 6 s (war, doom) or 10 s (golden age, plague), and out over 15 s.
  - Tempo changes glide over 8 s and never jump.
  - War and doom take priority over golden age; plague combines with any other mood.
- **Genesis** ("a held breath"):
  - No voice and no pulse. Only the ground, at the tonic and the 5th, with a slow 0.03 Hz swell.
  - Each genesis step adds one partial to the drone (3rd, then 9th, then 6th), so by Awakening the chord is complete and the Stone age flute enters on the first settlement founding.

### 1.4 Stingers (`Music.stinger(name)`)

- **Ducking:** a stinger ducks the rest of the music by −8 dB (attack 300 ms, release 4 s) and is not ducked by effects.
- **Key:** all stingers are in the world's key and mode.
- **Rate limit:** at most one stinger every 6 s. The later one waits, and is dropped if more than 20 s late, except endings.

| Name | Duration | Content |
|---|---|---|
| `age` | 6 s | The new age's voice plays the rising 1–5–8 (octave) motif, the **age motif**, followed by the colour holding 3 and 5. A low swell under it: the ground at +4 dB for 4 s. It is the same motif in every age, so the player learns it. |
| `founding` | 2.5 s | A small, friendly gesture: the current colour instrument plays degrees 5–6–5 softly at −6 dB re music. It is wry and modest, since foundings are frequent. Limit it to one per 30 s; skip it otherwise. |
| `miracle` | 5 s | <ul><li>A bright, open chord (1-5-9-3 voiced up two octaves) as a bell cluster.</li><li>A choir-like "aah" pad from the choir patch (§3.2), at 1 and 5.</li><li>4 s swell, sky reverb 40 %.</li></ul> |
| `wrath` | 5 s | <ul><li>A low unison on degrees 1 and ♭2 (both an octave below the tonic, high-passed at 45 Hz) for 1 s.</li><li>Then a falling minor 3rd in the brass or reed.</li><li>Heavy and slow. No sting or stab, because gods do not sting.</li></ul> |
| `ending-beloved` | 20 s | The age motif 1–5–8 played by **every instrument of every age in turn**: flute, lyre, reed, string, piano, glass lead. Each enters 2 s after the last, all held in a final Lydian chord with the 3rd on top. The bone flute ends it alone. |
| `ending-feared` | 18 s | <ul><li>The age motif played *descending* (8–5–1) in the low brass over a doom cluster.</li><li>Then the launch roar, filtered down to a distant rumble.</li><li>It ends unresolved, on degree 2.</li></ul> |
| `ending-forgotten` | 16 s | <ul><li>The motif plays once on the glass lead, quietly, then again with one note missing, then once more with two missing.</li><li>It ends on a single sustained tonic that fades over 6 s.</li><li>Wry and a little sad: they left without looking back.</li></ul> |
| `ending-parting` | 18 s | <ul><li>Two voices, the bone flute and the glass lead, play the motif in canon 1 beat apart.</li><li>They diverge: the flute descends while the glass lead ascends.</li><li>It ends on an open 5th with no 3rd: neither happy nor sad.</li></ul> |
| `ending-silence` | 24 s | <ul><li>Only the genesis drone returns: tonic and 5th.</li><li>Then the 5th drops out at 8 s.</li><li>Then wind (§2) alone from 16 s, fading to −60 dB by 24 s.</li><li>After that, **true silence for 4 s** before the ending screen's own ambience. This is the only place the game is ever silent on purpose.</li></ul> |

The ending stingers are used for these `world.ending` values:

| Stinger | Endings |
|---|---|
| `ending-beloved` | `ark-beloved` |
| `ending-feared` | `ark-feared` |
| `ending-forgotten` | `ark-forgotten` |
| `ending-parting` | `ark-parting` |
| `ending-silence` | `silence-quiet`, and `silence-ash` |

For `silence-ash`, add a low crackle bed at −30 dB for the first 12 s.

## 2. Ambience

**How ambience is built.** Ambience is a set of continuous **beds** on the ambience bus.
- Each bed is a looping generator whose gain is computed every frame from `zoom01` and from what is under the camera focus: biome, coast, settlement, weather and day (SPEC-UI `Audio.update` ctx).
- Gains are smoothed with τ = 1.5 s, so beds never pop.
- **Budget:** at most **6 beds** audible at once. If more qualify, the quietest are dropped.

### 2.1 By zoom level

| Zoom (zoom01) | Beds and recipe | Character |
|---|---|---|
| **Space** (< 0.12) | **Space hum.** Two sines at 55 Hz and 82.5 Hz (a 5th), each with slow amplitude drift (0.04 Hz, ±3 dB). Plus pink noise through a low-pass at 300 Hz at −30 dB. Everything high-passed at 40 Hz. | Huge, quiet and soft. A planet you hold in your hand. The quietest point of the game: ambience about −38 LUFS. |
| **High orbit** (0.12–0.3) | The space hum fades to −6 dB. **High air** fades in: pink noise through a band at 700 Hz (Q 0.7), with a slow random walk of the centre (500–900 Hz, at 0.05 Hz). | Wind heard from a great height, more breath than gust. |
| **Clouds** (0.3–0.5) | **Wind:** brown plus pink noise (1 : 0.4) through a band whose centre walks between 250 and 700 Hz. Gusts are amplitude envelopes of 2–6 s, every 5–15 s, at +4 dB. When a cloud is near the camera: **cloud whoosh**, a band of noise at 1.2 kHz (Q 1) that swells over 3 s as a cloud passes. | You are flying. Wind is the main sound. |
| **Landscape** (0.5–0.8) | Wind drops to −8 dB. Biome beds (§2.2) take over at full level. Surf near coasts. Weather beds. | You can hear the land breathing. |
| **Town** (> 0.8, settlement within about 3 cells of focus) | Biome beds at −4 dB. The **town bed** by age (below) at full level. | Life, small and distant. Never a city recording, always "a town heard from a hill". |

**Town bed by age.** Every element is a sparse one-shot from a pool, scheduled randomly within its rate limit.

| Age | Elements |
|---|---|
| **Stone** | <ul><li>Fire crackle (§3.2 `crackle`) at −8 dB.</li><li>Occasional voices: a formant blip pair (bands at 600 Hz and 1.1 kHz on a 0.3 s noise burst), 2 every 8–20 s.</li><li>A goat bell: FM 1 : 2.4, I 1, 1 ms / 400 ms, at 1.6 kHz.</li></ul> |
| **Bronze, Iron** | Add hammering: a KS pluck at 900 Hz, 0.15 s decay, plus a click, in groups of 3–5 hits 300 ms apart, every 10–25 s. |
| **Sail** | Add a church-like bell every 40–80 s: the §1.2 bell at degree 1, two octaves up, at −10 dB. Add gulls (near the coast only): a sine glide from 1.8 to 2.4 kHz and back over 300 ms, with 20 % noise. |
| **Industry** | Replace hammering with a machine loop: a 2 Hz noise pulse through a band at 400 Hz plus a 50 Hz hum, both high-passed at 45 Hz, at −14 dB. Add a train whistle: two sines a minor 3rd apart at 600 and 715 Hz, 1.5 s, every 60–120 s. |
| **Atomic** | Add a traffic hum: brown noise through a low-pass at 250 Hz with a 0.2 Hz swell. Add a distant horn: sawtooth through a band at 350 Hz, 0.4 s, every 20–40 s. |
| **Space** | A clean electric hum: sines at 120 and 240 Hz at −24 dB, plus a soft maglev whoosh (noise through a band swept from 300 Hz to 1.5 kHz and back over 2 s, every 15–30 s). Bells return. |

**Day and night** (`ctx.day`, from 0 to 1 at the focus):
- At night, town elements slow to half their rate and voices are removed.
- In ages 4 to 6, a faint night hum continues at −6 dB.

### 2.2 By biome

Biome beds are mixed in proportion to the biome cells within a radius of about 4 cells of the focus.

| Biome | Day | Night |
|---|---|---|
| **Ocean, open** | Low swell: brown noise through a low-pass at 400 Hz, with a 0.08–0.12 Hz amplitude wave (±5 dB). | Same, −3 dB. |
| **Coast** (any land cell next to sea) | **Surf:** pink noise through a band at 900 Hz (Q 0.6), shaped by wave envelopes: rise over 1.5 s, crash at +6 dB, then a 3 s hiss tail with a low-pass falling from 4 kHz to 1 kHz. One wave every 6–10 s; alternate the stereo side ±0.3. | Same. |
| **Temperate forest** | **Birdsong:** 3 "species" built from short sine chirps. <ul><li>(a) A falling glide from 4 to 3 kHz over 120 ms, ×3.</li><li>(b) A two-note whistle at 2.2 and 2.9 kHz, each 200 ms.</li><li>(c) A rapid trill: AM at 25 Hz on a 3.5 kHz tone for 600 ms.</li></ul> Each species calls every 4–12 s. Plus leaf rustle: noise through a band at 3 kHz with gusts. | **Crickets:** a sine at 4.5 kHz with AM at 30 Hz, in 3 pulses of 50 ms every 0.8 s, at −24 dB. Plus an owl every 30–60 s: a 380 Hz sine, two hoots of 300 ms with a 15 % pitch drop. |
| **Rainforest** | Denser birdsong (5 species, add a low dove call at 600 Hz). Insect drone: a 5.5 kHz sine with AM at 90 Hz at −30 dB. Dripping: KS blips at 2–3 kHz with a 50 ms decay, randomly at 3 per s. | Frogs: a 1.1 kHz square wave through a band, with AM at 18 Hz, in 150 ms croaks, in chorus. |
| **Grassland, savanna** | Wind through grass: pink noise through a band at 2 kHz with slow gusts. Sparse birds (species b). | Crickets. |
| **Desert** | Very dry wind: brown noise through a band at 300 Hz, plus occasional sand hiss (noise through a high-pass at 5 kHz, 2 s swell) at −26 dB. Almost nothing else. | Near silence: wind at −8 dB. **The desert is where silence lives.** |
| **Tundra, ice** | Thin high wind: a band at 1.4 kHz (Q 3) with a whistle-like resonance. Ice creaks: a KS pluck at 120 Hz with a 1.2 s decay and the pitch falling 20 %, every 20–60 s. | Same, plus a deep ice groan: a sine glide from 70 to 50 Hz over 2 s, high-passed at 45 Hz, every 60–120 s. |
| **Mountain** | Wind with more gusts and an occasional distant rockfall: 3–6 low clicks (noise through a low-pass at 600 Hz, 10 ms) over 1 s. | Same, −4 dB. |
| **Wetland** | Water: a gentle bubble pool (sine blips at 500 Hz–1.5 kHz with upward glides, 20 ms). Wading birds: the species (a) call an octave lower. | Frogs, loud: the main night sound here. |

**Weather beds:**

| Weather | Recipe |
|---|---|
| **Rain** | Pink noise through a high-pass at 500 Hz and a low-pass at 8 kHz, plus a drop layer (single-sample impulses through a band at 2–5 kHz, density 60–300 per s with rain intensity). At heavy rain, add a low body (brown noise through a low-pass at 300 Hz). |
| **Storm** | Rain at full, plus wind gusts at +6 dB, plus distant `thunder` at random (§3.2), every 8–20 s, at −6 dB. |
| **Fire or lava near** (within 3 cells) | `crackle` as a bed (§3.2), plus a low roar: brown noise through a low-pass at 200 Hz, high-passed at 50 Hz. |

## 3. Powers and disasters

### 3.1 General rules: what makes it godlike rather than cartoonish

1. **Slow attacks on big things.** A meteor is heard *before* it lands. A volcano grows. Nothing big starts with a click; only small things, such as lightning and UI sounds, are allowed sharp transients.
2. **Distance first.** Every effect is filtered by distance to the camera:
   - Distance comes from `cell` and `zoom01`. Low-pass cutoff = `20 kHz × (1 − 0.85·d)`, where d runs from 0 (at focus, close) to 1 (far side of the planet at orbit zoom).
   - Plus a delayed **far echo**, so a big event has a hint of the land answering:
     - Delay `80 ms + 400 ms · d`.
     - Low-passed at 1.5 kHz.
     - −10 dB.
3. **Weight in the low mids, not the sub.**
   - Rumble is shaped mostly between 60 and 250 Hz.
   - Everything is high-passed at **35 Hz**, and nothing puts more than 25 % of its energy below 60 Hz (SPEC-UI's rule).
   - Big events "shake" with slow amplitude modulation (3–7 Hz), not with sub-bass.
4. **Long tails into the sky reverb.** Godlike = the world rings afterwards. Wrath powers send 25–40 % to the sky reverb, and grace powers 30–50 %.
5. **No comedy sounds:**
   - no boing
   - no pitch-up sweeps on impacts
   - no instant cut-offs
   - nothing faster than 12 events per second
   - no exact repeats (±3 % pitch and ±1.5 dB per play, as above)
6. **Music answers.** Wrath powers call `Music.stinger('wrath')` at most once per 20 s, and grace powers call `'miracle'` at most once per 20 s. The music ducks under the effect, so the effect reads as an event inside the score.
7. **Loudness:** "re music" levels below are peak short-term loudness relative to the current music bus. With the mix in §5, nothing exceeds the master limiter ceiling.

### 3.2 Every cue

**Mapping of SPEC-UI §7 events to one-shot names** (binding):

| Power or event | One-shot names |
|---|---|
| Lightning | `thunder` |
| Meteor fall and impact | `roar` (approach) + `impact` + `rumble` |
| Volcano eruption | `rumble` + `crackle` |
| Earthquake | `rumble` |
| Tsunami or wave | `wave` |
| Rain brush | `rainfall` |
| Bloom | `shimmer` |
| Raise or lower (sculpting) | `rumble` (soft variant) |
| Heal or bless | `chime` |
| Prophet | `choir` |
| War declared | `horn` |
| Plague | `toll` |
| Launch | `roar` |
| Fire | `crackle` |

**Proposed additions.** Inspire, Omen of Peace and the genesis powers (comet, seed, awaken) have no names in SPEC-UI. I propose this, flagged for the lead:

| Power | Proposed mapping |
|---|---|
| Inspire | `chime` with `vol` 0.8 + `shimmer` |
| Omen of Peace | `chime` at the low variant + the `miracle` stinger |
| Comet | `roar` (soft, high) + `wave` (water arriving) |
| Seed life | `shimmer` |
| Awaken | `choir` (single voice) |

Name variants are chosen by the caller through `vol` and by the event context. Where a name has two recipes below, `Audio.play` picks by context.

---

**`thunder`: Lightning**
- **Description:** a close, bright crack followed by a long rolling grumble across the sky. Near, it feels physical; far, it is only the roll.
- **Duration:** 4–7 s.
- **Attack (0–40 ms):**
  - White noise burst, 1 ms / 35 ms, high-passed at 1.5 kHz.
  - Plus a crack made of 3 impulses 4–9 ms apart.
  - Spans 1.5–12 kHz.
  - Played at full level only if d < 0.3; otherwise the attack is dropped and the body starts at 100–600 ms (the sound travels).
- **Body (40 ms–2 s):**
  - Brown noise through a low-pass at 400 Hz, with amplitude driven by a random "roll" envelope (6–10 lumps of 120–400 ms each, decreasing).
  - Spans 50–400 Hz.
- **Tail (2–7 s):**
  - The body continues, low-passed down to 200 Hz, into the sky reverb at 35 %.
  - Spans 50–200 Hz.
- **Loudness:** +6 dB re music at peak when close; 0 dB when far.
- **Godlike, not cartoonish:** the delay between flash and sound, and a roll that is irregular and long. The crack is short and never repeated. Never add a "zap".

**`impact`: Meteor impact** (and the heavy hit at the end of a meteor fall)
- **Description:** the ground punched by the sky. A thud you feel more than hear, then debris raining down.
- **Duration:** 6–9 s.
- **Attack (0–80 ms):**
  - A sine at 70 Hz dropping to 38 Hz over 80 ms (high-passed at 35 Hz), plus a mid thump (noise through a band at 180 Hz, Q 1, 3 ms / 120 ms).
  - Plus a broadband crack (noise through a high-pass at 800 Hz, 2 ms / 60 ms) at −6 dB re the thump.
  - Spans 38–5,000 Hz.
- **Body (80 ms–3 s):**
  - Brown noise through a low-pass at 250 Hz, swelling to a peak at 0.6 s, with 5 Hz amplitude shake (±4 dB) that slows to 3 Hz.
  - Debris: 40–80 short clicks (noise through a band at 1.5–4 kHz, 5 ms each), density falling over 3 s.
  - Spans 60–4,000 Hz.
- **Tail (3–9 s):**
  - Sky reverb at 40 %, plus the far echo at −8 dB.
  - Low rumble fades with τ 1.5 s.
  - Spans 60–300 Hz.
- **Loudness:** +8 dB re music. This is **the loudest event in the game**, which is why music ducks by −10 dB (§5).
- **Godlike, not cartoonish:** the approach (`roar`, below) makes the impact inevitable. The shake is slow, and the debris is long and soft. No explosion "bang" sample shape: there is no flat-top distortion and no pitch-up.

**`roar`: two recipes**

*(a) Meteor fall.* Played 2.5 s before `impact`.
- **Description:** something enormous tearing through the air above you.
- **Duration:** 2.5 s, ending exactly at the impact.
- **Attack:** fade-in over 1.5 s.
- **Body:**
  - Pink noise through a band whose centre falls from 3 kHz to 400 Hz (the Doppler sense of descent), Q 1.5.
  - Plus a growing rumble (brown noise through a low-pass at 300 Hz).
  - The level rises 18 dB over the 2.5 s.
- **Tail:** none. It is cut by the impact; **the cut is masked by the impact attack**, which is the one place a cut is allowed.
- **Loudness:** up to +4 dB re music.
- **Godlike, not cartoonish:** the falling filter, never a falling *pitch* whistle. Cartoon falls whistle; this one *roars*.

*(b) Launch.* Played when a launch fires and in the ark endings.
- **Description:** a column of fire lifting a city's hope. A deep crackling roar that slowly climbs away.
- **Duration:** 12–20 s.
- **Attack (0–2 s):**
  - Ignition: a soft thump (sine at 60 Hz, 20 ms / 300 ms), then the roar fades in over 1.5 s.
  - Spans 50–3,000 Hz.
- **Body (2–12 s):**
  - Brown plus pink noise (1 : 0.5) through a low-pass at 1.2 kHz, high-passed at 45 Hz.
  - Plus crackle: random impulses at 80–200 per s, band-passed at 2 kHz.
  - Amplitude has 7 Hz shake at ±2 dB.
  - The low-pass falls from 1.2 kHz to 500 Hz as the rocket climbs.
  - Spans 45–2,500 Hz.
- **Tail (12–20 s):**
  - Distance filtering takes over, to a low-pass at 250 Hz.
  - Sky reverb at 45 %. Fade with τ 3 s.
  - Spans 45–250 Hz.
- **Loudness:** +6 dB re music at peak.
- **Godlike, not cartoonish:** this is *their* sound, not yours. It must feel proud and slow. Leave the music audible under it (duck only −6 dB) so the score carries the emotion.

**`rumble`: four uses**

*(a) Volcano eruption.*
- **Description:** the mountain clears its throat, then speaks.
- **Duration:** 8–14 s.
- **Attack:** 1.5 s swell. No transient.
- **Body:**
  - Brown noise through a low-pass at 180 Hz with 4 Hz amplitude shake.
  - Plus periodic deep "chuffs": noise through a band at 120 Hz, 50 ms / 600 ms, every 0.8–2 s.
  - Plus `crackle` at −8 dB.
  - Spans 45–1,500 Hz.
- **Tail:** a 4 s fade into the sky reverb at 30 %.
- **Loudness:** +4 dB re music.
- **Godlike, not cartoonish:** irregular breaths, like a living thing.

*(b) Earthquake.*
- **Description:** the whole surface moving. A grinding drone with cracks.
- **Duration:** as long as the power is held, plus 2 s, capped at 12 s.
- **Attack:** 600 ms swell.
- **Body:**
  - Brown noise through a band at 90 Hz (Q 1.2), with amplitude modulation at 6–9 Hz that is irregular (the rate itself modulated ±2 Hz at 0.5 Hz).
  - Plus a grinding layer: noise through a band at 350 Hz (Q 4) at −10 dB.
  - Plus rock cracks: KS plucks at 150–300 Hz, 0.3 s decay, 1–3 per second.
  - Spans 50–800 Hz.
- **Tail:** a 2 s fade, then 2–3 settling clicks.
- **Loudness:** +3 dB re music.
- **Godlike, not cartoonish:** no rattling objects. It is the *land* that sounds, not dishes.

*(c) Sculpting, raise and lower.*
- **Description:** clay under a thumb. A soft, continuous earthy push.
- **Duration:** while held; release over 600 ms.
- **Layers:**
  - Attack: 200 ms.
  - Body: brown noise through a band at 140 Hz for raise and 100 Hz for lower, with the band rising (raise) or falling (lower) by 10 % over each second held.
  - Plus a faint gritty layer: noise through a band at 2 kHz at −22 dB.
- **Frequency ranges:** 60–400 Hz, and 2 kHz for the grit.
- **Loudness:** −4 dB re music. It is constant, so it must be quiet.
- **Godlike, not cartoonish:** the sense of something heavy and malleable. No mechanical whir.

*(d) Comet* (genesis).
- **Description:** a soft roar, a high variant: the band falls from 5 kHz to 1.2 kHz over 2.5 s.
- **Then:** `wave` (water arriving), 1.5 s after the start.
- **Loudness:** 0 dB re music. Genesis is gentle.

**`wave`: Tsunami, flood, comet water**
- **Description:** the sea standing up and walking inland.
- **Duration:** 6–10 s.
- **Attack (0–2 s):**
  - A rising wall: pink noise through a band whose centre rises from 300 Hz to 900 Hz, with the level +15 dB over 2 s.
  - Spans 200–1,200 Hz.
- **Body (2–5 s):**
  - The crash: broadband noise through a low-pass at 6 kHz, peaking at 2.2 s, plus brown noise through a low-pass at 250 Hz for weight.
  - Spans 60–6,000 Hz.
- **Tail (5–10 s):**
  - The receding hiss: the low-pass falls from 4 kHz to 700 Hz, plus gurgle (sine blips at 300–900 Hz, 10 per s, fading).
  - Spans 300–4,000 Hz.
- **Loudness:** +5 dB re music for a tsunami, 0 dB for comet water.
- **Godlike, not cartoonish:** slow build, long tail. A wave too short sounds like a splash.

**`rainfall`: Rain brush**
- **Description:** cool rain arriving where you paint it, soothing.
- **Duration:** while held, plus a 2 s release.
- **Layers:**
  - Attack: 400 ms.
  - Body: the rain bed recipe from §2.2 at medium intensity, panned to the brush.
  - Plus a gentle "first drops" layer for the first 600 ms: 15–30 single drops, band-passed at 2.5–5 kHz.
- **Frequency ranges:** 500–8,000 Hz.
- **Loudness:** −2 dB re music.
- **Godlike, not cartoonish:** the first drops. A god's rain starts softly.

**`shimmer`: Bloom, seed life, inspire**
- **Description:** green rushing out of the ground, a glittering warmth.
- **Duration:** 3–5 s per use; repeating while the brush is held, but at most every 1.2 s.
- **Attack (0–300 ms):** an upward glissando of 6 sine grains on scale degrees (1–2–3–5–6–8, two octaves above the tonic), 40 ms apart, each with a 5 ms / 600 ms envelope.
- **Body:** a soft breathy pad (pink noise through a band at 2.5 kHz, Q 2), swelling over 1 s.
- **Tail:** sky reverb at 50 %, about 3 s.
- **Frequency ranges:** 800–6,000 Hz.
- **Loudness:** −3 dB re music.
- **Godlike, not cartoonish:** grains in the world's own scale (so it is part of the music), never a "magic wand" sweep. Inspire is the same recipe with the grains a 5th higher and a held bell at the end.

**`chime`: Heal, bless, Omen of Peace**
- **Description:** a warm bell that blooms; the sound of being forgiven.
- **Duration:** 4–6 s.
- **Layers:**
  - Attack: an FM bell (1 : 1.4, I 2 decaying to 0.2 over 1 s) at degree 5, with a 2 ms attack.
  - Body: a second bell at degree 1 an octave lower, entering 120 ms later. Plus a soft pad at degrees 1 and 3.
  - Tail: sky reverb at 40 %.
- **Frequency ranges:** 200–5,000 Hz.
- **Loudness:** −2 dB re music.
- **Godlike, not cartoonish:** the delayed second bell, a reply from below. The Omen of Peace variant uses three bells (5, 3, 1, descending), slower (250 ms apart).

**`choir`: Prophet, awaken**
- **Description:** distant human voices on an open vowel. Not a church: a hillside.
- **Duration:** 5–7 s.
- **Voice synthesis:**
  - 4 voices, each a sawtooth with ±8 cents random detune and vibrato at 5 Hz ±10 cents.
  - Each goes through a parallel formant bank for the vowel "ah": bands at 700 Hz (Q 6), 1,220 Hz (Q 8) and 2,600 Hz (Q 10), with gains 1, 0.5, 0.2.
- **Layers:**
  - Attack: 800 ms.
  - Body: the voices sing a 1–5 dyad and move to 1–6 at mid-way.
  - Tail: hall reverb, 2 s.
- **Frequency ranges:** 150–3,000 Hz.
- **Loudness:** −3 dB re music.
- **Godlike, not cartoonish:** formants, not an "angelic" sine pad. It should sound like people. Awaken uses a single voice, very soft (−8 dB re music): the first human sound in the world.

**`horn`: War declared**
- **Description:** a long horn call over hills. Ominous and dignified.
- **Duration:** 4 s.
- **Layers:**
  - Attack: 150 ms.
  - Body: two calls. A sawtooth at the tonic (two octaves above the root, about 300 Hz), through a low-pass that swells from 500 Hz to 2 kHz with each note, Q 1.2. The second call is a 4th higher.
  - Tail: hall reverb, 2 s, plus the far echo.
- **Frequency ranges:** 150–3,000 Hz.
- **Loudness:** 0 dB re music.
- **Godlike, not cartoonish:** the age decides the horn:
  - Stone to Iron: horn
  - Sail: brass
  - Industry and Atomic: a siren-like two-tone, very soft, so never a modern siren at full level
  - Space: a long synthesised brass pad

  Keep it slow.

**`toll`: Plague**
- **Description:** one low bell, far away, the sound of a town counting its dead.
- **Duration:** 7 s.
- **Layers:**
  - Attack: an FM bell (1 : 2.76, I 3 decaying to 0.5) at the tonic, one octave above the root (about 150 Hz), with a 3 ms attack.
  - Body: a hum partial at 0.5× and a "tierce" at 1.2×.
  - Tail: a 6 s decay, hall reverb.
- **Frequency ranges:** 75–2,000 Hz.
- **Loudness:** −4 dB re music.
- **Godlike, not cartoonish:** one stroke only. Repetition is left to the plague mood in the music (§1.3).

**`crackle`: Fire, lava**
- **Description:** fire eating dry wood.
- **Duration:** a 2 s grain, chained while burning.
- **Layers:**
  - Random impulses at 20–60 per s, each 1–3 ms of noise through a band at 1.5–5 kHz (random per impulse).
  - Plus a soft bed: brown noise through a low-pass at 300 Hz.
  - Plus occasional pops (every 1–3 s): 10 ms of noise through a band at 800 Hz, +6 dB.
- **Frequency ranges:** 50–5,000 Hz.
- **Loudness:** −8 dB re music, or −4 dB when the camera is very close.
- **Godlike, not cartoonish:** irregular timing, with no pattern ever audible.

### 3.3 Stinger ownership

**Age advance:** `world:age` triggers the `age` stinger (§1.4) plus a low `rumble` swell (variant (c) at −6 dB re music, 3 s): a ground-deep sense of change.

## 4. UI sounds

UI sounds are on the `ui` bus.
- They are **dry** (no reverb) except chronicle entry and age change.
- They are short and soft.
- They are never ducked and never duck anything.
- Each one fits under the music. Peak −14 dBFS or lower.

| Sound (one-shot name) | Recipe | Duration | Level re music | Notes |
|---|---|---|---|---|
| **Hover** (`hover`) | A sine at 2.2 kHz, 1 ms / 25 ms, plus its 2nd harmonic at −14 dB | 30 ms | −16 dB | Rate-limited to one per 80 ms. Pitch steps by scale degree with the button's index in its group, so sweeping the palette plays a tiny scale. |
| **Click** (`click`) | A KS pluck at 1.1 kHz with a 120 ms decay, plus a 2 ms noise tick through a band at 4 kHz at −12 dB | 120 ms | −10 dB | A wooden, warm "tok". |
| **Arm a power** (`arm`) | Two grains a 5th apart (degrees 1 and 5, octave 5), 30 ms apart, each 3 ms / 250 ms, sine plus a 3rd harmonic at 20 %. Plus a soft whoosh: noise through a band at 1.5 kHz, 80 ms swell. | 300 ms | −8 dB | Pitch by power group: Shape low (octave 4), Sky middle, Wrath a minor-3rd pair at octave 4, Grace a major-3rd pair at octave 5. Disarming plays the same recipe reversed (5 to 1), at −4 dB further. |
| **Cancel / deny** (`deny`) | Two low sines, a minor 2nd apart (at 220 and 233 Hz), 5 ms / 160 ms, with a soft low-pass at 1.2 kHz | 180 ms | −10 dB | Gentle refusal, never a buzzer. Rate-limited to one per 300 ms. |
| **Unlock** (`unlock`) | The age motif (1–5–8) on the KS lyre, 70 ms apart, plus a bell at 8 with a 1.5 s decay | 1.8 s | −6 dB | A new power is available. |
| **Toast** (notification) | Proposed: `click` at 0.7 volume plus a single bell grain at degree 3 (FM 1 : 3.5, 1 ms / 600 ms) | 0.6 s | −12 dB | No name for this in SPEC-UI. Implement through `chime` with `vol: 0.3`, or add `toast` to the list (flagged for the lead). Rate-limited to one per 1.5 s. |
| **Chronicle entry** | Proposed: a soft "quill": pink noise through a band at 3.5 kHz, 3 ms / 90 ms, ×3 at 60–110 ms intervals, plus a low KS pluck at degree 1 (octave 3) with a 400 ms decay, room reverb 20 % | 0.5 s | −14 dB | Very quiet; there are many entries. Skip it if an entry sound played in the last 4 s. Major entries (founding, war, age) use the matching stinger instead. Not in the SPEC-UI list: flagged. |
| **Age change** | The `age` stinger (§1.4) plus `unlock` if powers unlock at the same time (`unlock` waits 2 s) | 6 s | stinger | |
| **Ending** | The ending stinger (§1.4); all UI sounds are muted while it plays except `click` | 16–24 s | stinger | |

Settings sliders play `click` at the new volume of their own bus, so the player hears the level they set, at most every 120 ms.

## 5. Mix rules

### 5.1 Bus structure and levels

**Routing:**
- `music`, `sfx`, `ambience` and `ui` each feed into `master`.
- `master` goes through a high-pass at **30 Hz** (12 dB/oct), then a **glue compressor**, then the **limiter**, then the destination.

**Glue compressor:** threshold −18 dB, ratio 2 : 1, attack 30 ms, release 300 ms, knee 6 dB.

**Limiter:** `DynamicsCompressor` with threshold −6 dB, knee 0, ratio 20, attack 2 ms, release 200 ms, followed by a fixed gain such that **sample peaks never exceed −1.0 dBFS**.
- WebAudio's compressor is not a true brickwall, so add a final `WaveShaper` soft clip (tanh) scaled to −1.0 dBFS as a safety net.
- In C9 Unreal, use a lookahead brickwall at −1.0 dBTP.

**Loudness targets** (short-term, with the default volume settings):

| Bus | Alone |
|---|---|
| Music | −28 LUFS (Space and Stone quieter, −30; Industry and Atomic −26) |
| Ambience | −32 LUFS at landscape zoom, −38 LUFS in space |
| SFX | peaks up to −14 LUFS short-term (`impact`) |
| UI | never above −30 LUFS short-term |

The **whole mix** should average **−23 LUFS integrated** over a 40-minute session (a calm, listen-for-hours level), with momentary peaks up to −12 LUFS on the biggest events.

**Default user volumes:** master 0.8, music 0.7, sfx 0.9, ambience 0.8, ui 0.6.

### 5.2 Ducking

| Trigger | Ducks | Depth | Attack | Release |
|---|---|---|---|---|
| `impact`, `wave` (tsunami), `roar` (launch) | music and ambience | −10 dB music, −6 dB ambience | 50 ms | 3 s |
| `thunder` (close), `rumble` (eruption or quake) | music | −6 dB | 80 ms | 2 s |
| any stinger | music (the rest of it) | −8 dB | 300 ms | 4 s |
| `choir`, `chime`, `horn`, `toll` | music | −4 dB | 200 ms | 2 s |
| UI sounds | nothing | | | |

Implement ducking as a gain node on each bus driven by the sum of active duck requests. **Take the deepest request, never the sum.**

### 5.3 Voices and rates

- **At most 24 simultaneous one-shot voices.** The oldest quietest voice is stolen with a 30 ms fade, never a cut.
- **Per-name limits:**
  - at most 4 `thunder`
  - at most 2 `impact`
  - at most 3 `rumble`
  - at most 1 `roar` of each variant
  - at most 2 `wave`
  - at most 6 `crackle`
  - at most 2 of anything else
- **Rate limits** are as listed per sound. At most 12 one-shot starts per second overall, with extra ones dropped. Holding lightning at 8× speed must not become a machine gun.
- **Music:** at most 16 oscillator voices; pads count their detuned copies.
- **Ambience:** at most 6 beds (§2).

### 5.4 Must never happen

1. **Clipping, NaN, DC or denormals:**
   - Every bus has a DC blocker (a one-pole high-pass at 10 Hz).
   - All feedback loops (KS, reverbs) have loop gain < 0.999.
   - `renderTest` must report `clipped: false` and `nan: false` in every scenario.
2. **Harshness:**
   - Sustained energy at 2–5 kHz must stay below 30 % of the mix in any 3 s window, except during rain.
   - No square or sawtooth reaches the master without a low-pass at 5 kHz or lower.
   - No sound above 12 kHz lasts longer than 100 ms.
3. **Sub-bass build-up:** energy below 60 Hz must stay under **25 %** of the total in every `renderTest` scenario (SPEC-UI). Every low layer is high-passed at 35–45 Hz as specified.
4. **Fatigue over 40 minutes:**
   - No loop shorter than 8 s is audible as a loop: all beds are generated, not looped audio, and randomised.
   - No one-shot plays with identical parameters twice in a row.
   - The music has at least 50 % silence in its voice layer.
   - The master short-term loudness never stays above −18 LUFS for more than 20 s.
5. **Sound before a user gesture,** or any exception when the context is missing or suspended (SPEC-UI).
6. **Silence where sound is expected:** in every scenario except `ending-silence`, RMS must stay above −60 dBFS.

### 5.5 Numeric checks for `Audio.renderTest`

Suggested pass ranges, measured over the whole render. These are proposals for the tests, to tune once the first build exists.

| Scenario | Peak (dBFS) | RMS (dBFS) | Sub (< 60 Hz) | High (> 4 kHz) |
|---|---|---|---|---|
| `orbit` | ≤ −12 | −45 … −30 | < 25 % | < 10 % |
| `coast-day` | ≤ −8 | −36 … −24 | < 20 % | 10–30 % |
| `forest-night` | ≤ −10 | −40 … −28 | < 15 % | 5–25 % |
| `city` | ≤ −8 | −36 … −24 | < 20 % | 5–25 % |
| `storm` | ≤ −3 | −30 … −18 | < 25 % | 15–40 % |
| `meteor` | ≤ −1 | −28 … −16 | < 25 % | < 25 % |
| `age-advance` | ≤ −4 | −34 … −22 | < 20 % | < 25 % |
| `stone` | ≤ −10 | −40 … −28 | < 20 % | < 15 % |
| `industry` | ≤ −8 | −36 … −24 | < 20 % | < 25 % |
| `space` | ≤ −10 | −40 … −28 | < 15 % | < 20 % |

## 6. Reference descriptions (words only)

These describe what to aim for. None of them is a recording or a piece to copy.

- **Stone and Bronze:** one person playing a flute made from a bird's wing bone at the mouth of a cave at dusk, a little out of tune, with a friend tapping a skin drum without hurry. The valley answers with a slight echo.
- **Iron and Sail:** a small court band playing in a stone hall: a plucked lyre, a nasal reed, and a bowed string. Then bells from a harbour tower carried on the wind. The music is ceremonial but not grand; people are still small.
- **Industry and Atomic:** a piano in an upstairs room, the window open on a working town. Brass from a park bandstand far away. Then the same room at night, decades later, with a clock ticking too loudly while the radio talks about something nobody wants to say.
- **Space:** a glass instrument played in a vast, quiet hangar, with long soft pads like light through high windows. Once in a while, the old bone flute: someone has kept it.
- **The god's world, overall:** a diorama you can hear. A planet the size of a table, with tiny weather, tiny towns, very large silences. Effects are the weather of the gods: distant first, then all at once, then ringing for a long time.
- **Wry moments:** a single goat bell in the middle of a thunderstorm. The founding stinger that is a bit too pleased with itself. The deny sound that politely says "no, your Divinity".

### Technique references (open, for the engineers)

These are general published techniques and open-licence tools, cited for method only.
- **Karplus–Strong plucked-string synthesis** (Karplus and Strong, 1983): a published algorithm, freely implementable.
- **Freeverb,** the Schroeder–Moorer reverb by Jezar at Dreampoint: public domain. A model for the algorithmic reverb if the generated-impulse convolution is too costly in C9.
- **Formant (vowel) synthesis:** parallel band-pass resonators at published vowel formant frequencies, a general technique. The "ah" values above are standard textbook figures.
- **Web Audio API** (W3C Recommendation; W3C Document License): `OscillatorNode`, `BiquadFilterNode`, `ConvolverNode`, `DynamicsCompressorNode`, `OfflineAudioContext`.
- **ITU-R BS.1770 and EBU R 128,** both freely available standards documents, for measuring LUFS in tests.
