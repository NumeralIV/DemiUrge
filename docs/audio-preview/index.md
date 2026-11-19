# DEMIURGE audio previews

Rendered 2026-11-19 by `tools/audio-preview/` from `docs/content/sound-direction.md` (the approved direction). Nobody on the team can hear these; the owner can. These are previews of the **direction**, synthesised offline, not recordings of the current C++ synth.

**26 files, 170 MB, 48 kHz 16-bit stereo WAV. 26 of 26 pass every numeric check.**

## How to listen

- Use headphones or decent speakers. Several sounds live below 150 Hz and the quietest file (space) is meant to be barely there.
- **Do not normalise the files.** They are at their in-game levels: music at -26 to -30 LUFS, ambience at -32 to -38 LUFS, and each cue at its loudness "re music" (3.2) on a -28 LUFS music bus. Set your volume once on `music-mood-peace.wav` and leave it; everything else is then heard at its intended level, and the game will sound quieter than commercial music on purpose (a calm, listen-for-hours mix).
- Each file has a spectrogram in `spectrograms/` (30 Hz to 20 kHz, log scale; the strip underneath is the short-term loudness, with the gold line at the file's target and grey lines every 6 LU).
- To tell the team what you hear, the file name and a time (for example "sail, 0:24, bell too bright") is enough.

## Music: the seven ages

| File | Length | What it is | Listen for |
|---|---|---|---|
| [music-age-0-stone.wav](music-age-0-stone.wav) ([spectrogram](spectrograms/music-age-0-stone.png)) | 60 s | Stone age: bone flute and hand drum, over a hummed drone. Major pentatonic on D. | One flute, a little out of tune: every note slides up into pitch and the vibrato only arrives after the note has settled. A frame drum that never keeps a strict beat, a ghost stroke now and then. Long silences (the flute plays less than half the time) and a faint valley echo after each phrase. Very bare and very quiet: this is the softest music in the game. |
| [music-age-1-bronze.wav](music-age-1-bronze.wav) ([spectrogram](spectrograms/music-age-1-bronze.png)) | 60 s | Bronze age: the flute is joined by the lyre. | The lyre answers each flute phrase one beat after it ends, with a soft strum (three strings, low to high) and an echo of the phrase's last note an octave down. A second, higher drum on some off-beats. Same family of sound as Stone, just more of it. |
| [music-age-2-iron.wav](music-age-2-iron.wav) ([spectrogram](spectrograms/music-age-2-iron.png)) | 60 s | Iron age: lyre and reed (Mixolydian). | A nasal, shawm-like reed carries the tune but sits deliberately under the accompaniment (the direction says the reed must stay 6 dB under ground plus colour, because reeds turn harsh fast). Underneath, a bowed open fifth that moves with the harmony, lyre fifths answering the reed, a low drum every second bar and a barely-there shaker. |
| [music-age-3-sail.wav](music-age-3-sail.wav) ([spectrogram](spectrograms/music-age-3-sail.png)) | 60 s | Sail age: strings and bells arrive (Mixolydian), in a hall. | A bowed solo string with delayed vibrato, FM bells answering its phrases, a slow-attack string pad underneath and a very soft lyre ostinato (four notes a bar). The first age that should sound like a small orchestra in a stone hall. |
| [music-age-4-industry.wav](music-age-4-industry.wav) ([spectrogram](spectrograms/music-age-4-industry.png)) | 60 s | Industry age: brass and piano (Dorian, the tonic-minor colour arrives). | A piano with a soft hammer, a muted horn section that swells ("blat") on held chord tones and never gets loud, a darker string pad, and a muted plucked eighth-note figure on 1 and 5: the first hint of machines. A clearer, steadier grid than the earlier ages. |
| [music-age-5-atomic.wav](music-age-5-atomic.wav) ([spectrogram](spectrograms/music-age-5-atomic.png)) | 60 s | Atomic age: brass, piano and electricity (Dorian). | The piano now plays its phrases in octaves; an electric organ holds the chord with a slow rotary wobble; brass on held tones. Under it a clock tick on every beat and a low heartbeat thump: the pulse should feel like waiting. |
| [music-age-6-space.wav](music-age-6-space.wav) ([spectrogram](spectrograms/music-age-6-space.png)) | 60 s | Space age: synth pads (Lydian). | A glass lead (a sine with a slightly sharp octave that beats slowly), a wide six-voice pad whose filter breathes over twenty seconds, a soft rising arpeggio, a drone an octave up with no sub-bass. Listen for the raised 4th at the top of phrases (the "wonder" note) and for the bone flute from the Stone age returning for one phrase in the middle of the minute. |

## Music: the moods

| File | Length | What it is | Listen for |
|---|---|---|---|
| [music-mood-peace.wav](music-mood-peace.wav) ([spectrogram](spectrograms/music-mood-peace.png)) | 60 s | Mood: peace (the Sail-age score as written). | The reference for the other moods: peace, war, plague and golden age all use the same world seed, so their opening phrases come from the same material and you can A/B them directly. |
| [music-mood-war.wav](music-mood-war.wav) ([spectrogram](spectrograms/music-mood-war.png)) | 60 s | Mood: war, over the Sail-age score. | Tempo up 8 %, the 3rd and 7th flattened (major turns minor), a low drum on every bar, a low bowed pedal on the 5th and a low string on the tonic, and the tune in shorter phrases with longer rests. Darker, not louder. |
| [music-mood-plague.wav](music-mood-plague.wav) ([spectrogram](spectrograms/music-mood-plague.png)) | 60 s | Mood: plague, over the Sail-age score. | Tempo down 10 %, no pulse at all, the solo string detuned 12 cents flat and drifting slowly (sickly but not grotesque), the ground reduced to the tonic alone, and a distant toll every 16-24 s at 10 dB under the music. |
| [music-mood-golden-age.wav](music-mood-golden-age.wav) ([spectrogram](spectrograms/music-mood-golden-age.png)) | 60 s | Mood: golden age, over the Sail-age score. | Tempo up 5 %, the bells answer with full chords, the ground adds its 3rd, a sparse high sparkle (bell partials at 2-4 kHz every 3-6 s) and 10 % more hall. |
| [music-mood-doom.wav](music-mood-doom.wav) ([spectrogram](spectrograms/music-mood-doom.png)) | 60 s | Mood: doom, over the Atomic-age score. | Tempo down 15 %, the voice reduced to one long note per phrase, the ground replaced by a very soft cluster (tonic and minor 2nd) that beats at about 3 Hz, and a low wind through the rafters. The clock keeps ticking, the heartbeat has stopped. Doom never gets loud: it gets empty. |

## Ambience beds

| File | Length | What it is | Listen for |
|---|---|---|---|
| [ambience-space.wav](ambience-space.wav) ([spectrogram](spectrograms/ambience-space.png)) | 10 s | Ambience: space (zoom01 < 0.12). Seamless loop. | A low, soft hum (a 5th: 55 and 82.5 Hz, drifting slowly) with a trace of dark noise. The quietest point of the game: on laptop speakers you may hear almost nothing, which is intended; use headphones. |
| [ambience-orbit.wav](ambience-orbit.wav) ([spectrogram](spectrograms/ambience-orbit.png)) | 10 s | Ambience: high orbit (zoom01 0.12-0.3). Seamless loop. | The space hum 6 dB down, and high air fading in: wind heard from a great height, more breath than gust, its colour wandering slowly. |
| [ambience-landscape.wav](ambience-landscape.wav) ([spectrogram](spectrograms/ambience-landscape.png)) | 14 s | Ambience: landscape (zoom01 0.5-0.8), a temperate forest on a coast, by day. Seamless loop. | Wind pulled back 8 dB; the biome beds take over: surf waves (a rise, a broadband crash, then a hiss that closes down), the open-sea swell, three bird species (a falling triple chirp, a two-note whistle, a trill) and leaves rustling in gusts. "You can hear the land breathing." |
| [ambience-town.wav](ambience-town.wav) ([spectrogram](spectrograms/ambience-town.png)) | 18 s | Ambience: town (zoom01 > 0.8), a Sail-age harbour town on the same coast. Seamless loop. | The landscape beds pulled back, and the town heard from a hill: fire crackle, a murmur of voices, a goat bell, a smith hammering in groups of hits, a harbour bell, gulls. Never a city recording. (The rates are compressed so one loop holds every element; in the game they are sparser.) |

## Power and disaster cues

| File | Length | What it is | Listen for |
|---|---|---|---|
| [cue-comet.wav](cue-comet.wav) ([spectrogram](spectrograms/cue-comet.png)) | 13 s | Comet (genesis): calls water down. roar (soft, high) + wave (comet water). | A soft roar whose band falls from 5 to 1.2 kHz, then the water arriving: a rising wall, the crash, a receding hiss with small gurgles. Gentle: genesis is not wrath. |
| [cue-meteor.wav](cue-meteor.wav) ([spectrogram](spectrograms/cue-meteor.png)) | 12 s | Meteor fall and impact: roar (fall) + impact. | You hear it before it lands: a roar whose filter falls (a descent, never a falling whistle), cut by the impact, a punch you feel more than hear, a slow shake, debris raining down for seconds and a long ring in the sky. The loudest event in the game. No bang, no pitch-up. |
| [cue-volcano.wav](cue-volcano.wav) ([spectrogram](spectrograms/cue-volcano.png)) | 16 s | Volcano eruption: rumble (eruption) + crackle. | No transient: a 1.5 s swell, then the mountain breathing in irregular deep chuffs over a shaken low rumble, with fire crackle on top, fading for 4 s into the sky. |
| [cue-lightning.wav](cue-lightning.wav) ([spectrogram](spectrograms/cue-lightning.png)) | 8 s | Lightning: thunder (close). | A short bright crack (three impulses a few ms apart), then a long, irregular roll in lumps that get smaller, darkening into the sky reverb. No zap. |
| [cue-earthquake.wav](cue-earthquake.wav) ([spectrogram](spectrograms/cue-earthquake.png)) | 11 s | Earthquake: rumble (earthquake), the power held for 6 s. | A grinding low drone shaking irregularly (6-9 Hz), rock cracks one to three a second, then a 2 s fade and a few settling clicks. The land sounds, not the dishes. |
| [cue-rain.wav](cue-rain.wav) ([spectrogram](spectrograms/cue-rain.png)) | 8 s | Rain brush: rainfall, held for 4 s. | A god's rain starts softly: a scatter of first drops, then the hiss and patter, then a 2 s release. |
| [cue-bloom.wav](cue-bloom.wav) ([spectrogram](spectrograms/cue-bloom.png)) | 8 s | Bloom: shimmer, the brush used twice. | Six sine grains rising in the world's own key, a breathy glitter swelling under them, a long sky tail; the second use repeats it with a small variation. Not a "magic wand" sweep. |
| [cue-plague.wav](cue-plague.wav) ([spectrogram](spectrograms/cue-plague.png)) | 6 s | Plague: toll. | One low bell, far away, with its hum note an octave below and a minor-third tierce. One stroke only. |
| [cue-blessing.wav](cue-blessing.wav) ([spectrogram](spectrograms/cue-blessing.png)) | 7 s | Heal / bless: chime. | A warm bell on the 5th that blooms, and 120 ms later a reply from below (the tonic an octave and a half lower) over a soft pad. "The sound of being forgiven." |
| [cue-ark-launch.wav](cue-ark-launch.wav) ([spectrogram](spectrograms/cue-ark-launch.png)) | 23 s | Ark launch: roar (launch). | A soft ignition thump, then a deep crackling roar fading in and slowly climbing away: its filter closes as it rises, until distance takes over and the sky rings. Their sound, not yours: proud and slow. |

## The numbers

Measured on the WAV files as written (16-bit, dithered). Loudness is ITU-R BS.1770 (K-weighted, gated integrated; short-term = 3 s windows). Targets: music and ambience by integrated loudness (5.1, 1.2), cues by their peak short-term loudness (3.2 "re music" on -28 LUFS). "Trim" is the gain that put the raw render on its target before the master chain; "limiter" is the most the limiter had to pull down.

| File | Target | Integrated | Short-term max / median | True peak | < 60 Hz | > 4 kHz | 2-5 kHz worst 3 s | Quietest 1 s | Trim | Limiter | Result |
|---|---|---|---|---|---|---|---|---|---|---|---|
| music-age-0-stone | -30 | -30.0 | -26.5 / -31.6 | -13.3 dBTP | 0.3 % | 0.1 % | 1.3 % | -41.0 dBFS | -7.7 dB | 0.0 dB | PASS |
| music-age-1-bronze | -29 | -29.0 | -25.5 / -29.0 | -11.7 dBTP | 0.3 % | 0.4 % | 18.9 % | -40.2 dBFS | -7.5 dB | 0.0 dB | PASS |
| music-age-2-iron | -28 | -28.0 | -26.3 / -27.9 | -11.8 dBTP | 0.1 % | 0.2 % | 6.0 % | -42.3 dBFS | -10.7 dB | 0.0 dB | PASS |
| music-age-3-sail | -28 | -28.0 | -24.8 / -28.0 | -15.6 dBTP | 0.0 % | 0.3 % | 17.8 % | -37.2 dBFS | -8.0 dB | 0.0 dB | PASS |
| music-age-4-industry | -26 | -26.0 | -22.2 / -26.0 | -10.9 dBTP | 0.0 % | 0.1 % | 2.1 % | -36.3 dBFS | +4.5 dB | 0.0 dB | PASS |
| music-age-5-atomic | -26 | -26.0 | -22.4 / -26.6 | -10.8 dBTP | 0.2 % | 0.1 % | 4.0 % | -33.6 dBFS | +2.3 dB | 0.0 dB | PASS |
| music-age-6-space | -30 | -30.0 | -24.5 / -32.6 | -18.2 dBTP | 0.0 % | 0.0 % | 1.3 % | -38.8 dBFS | -12.2 dB | 0.0 dB | PASS |
| music-mood-peace | -28 | -28.0 | -24.2 / -27.9 | -16.1 dBTP | 0.0 % | 0.2 % | 10.0 % | -40.0 dBFS | -7.8 dB | 0.0 dB | PASS |
| music-mood-war | -27 | -27.0 | -23.3 / -28.6 | -14.5 dBTP | 0.0 % | 0.1 % | 7.3 % | -34.2 dBFS | -7.3 dB | 0.0 dB | PASS |
| music-mood-plague | -29 | -29.0 | -25.4 / -29.5 | -15.7 dBTP | 0.0 % | 0.1 % | 10.1 % | -38.0 dBFS | -8.6 dB | 0.0 dB | PASS |
| music-mood-golden-age | -27 | -27.0 | -23.1 / -26.9 | -15.7 dBTP | 0.0 % | 0.2 % | 23.4 % | -38.7 dBFS | -7.3 dB | 0.0 dB | PASS |
| music-mood-doom | -29 | -29.0 | -26.1 / -29.5 | -12.8 dBTP | 0.2 % | 0.0 % | 3.7 % | -35.0 dBFS | +0.9 dB | 0.0 dB | PASS |
| ambience-space | -38 | -38.0 | -37.0 / -38.5 | -30.1 dBTP | 16.7 % | 0.0 % | 0.0 % | -39.2 dBFS | -31.7 dB | 0.0 dB | PASS |
| ambience-orbit | -36 | -36.0 | -35.5 / -36.2 | -25.5 dBTP | 7.1 % | 0.6 % | 2.9 % | -38.5 dBFS | -29.6 dB | 0.0 dB | PASS |
| ambience-landscape | -32 | -32.0 | -28.4 / -32.0 | -15.8 dBTP | 1.9 % | 10.4 % | 21.4 % | -41.5 dBFS | -18.2 dB | 0.0 dB | PASS |
| ambience-town | -32 | -32.0 | -29.2 / -31.7 | -14.2 dBTP | 0.4 % | 9.8 % | 22.3 % | -42.7 dBFS | -13.1 dB | 0.0 dB | PASS |
| cue-comet | -28 | -31.7 | -28.0 / -35.7 | -16.9 dBTP | 0.5 % | 5.7 % | 12.5 % (in mix) | -64.3 dBFS | -19.1 dB | 0.0 dB | PASS |
| cue-meteor | -20 | -22.2 | -19.8 / -26.2 | -6.8 dBTP | 3.8 % | 0.1 % | 1.9 % (in mix) | -57.0 dBFS | -7.5 dB | 0.0 dB | PASS |
| cue-volcano | -24 | -24.8 | -23.9 / -24.9 | -11.5 dBTP | 6.2 % | 0.1 % | 1.5 % (in mix) | -58.8 dBFS | -19.6 dB | 0.0 dB | PASS |
| cue-lightning | -22 | -23.5 | -22.0 / -33.0 | -2.3 dBTP | 3.8 % | 0.6 % | 9.0 % (in mix) | -57.3 dBFS | +1.3 dB | 0.0 dB | PASS |
| cue-earthquake | -25 | -25.8 | -25.0 / -26.5 | -13.3 dBTP | 2.5 % | 0.1 % | 2.1 % (in mix) | -62.6 dBFS | -16.9 dB | 0.0 dB | PASS |
| cue-rain | -30 | -30.5 | -30.0 / -32.5 | -12.0 dBTP | 0.0 % | 37.9 % | 45.7 % (rain: exempt) | -72.8 dBFS | -14.3 dB | 0.0 dB | PASS |
| cue-bloom | -31 | -32.2 | -31.0 / -36.5 | -20.3 dBTP | 0.0 % | 1.9 % | 10.1 % (in mix) | -72.2 dBFS | -9.5 dB | 0.0 dB | PASS |
| cue-plague | -32 | -31.6 | -32.0 / -44.9 | -17.3 dBTP | 0.0 % | 0.0 % | 4.9 % (in mix) | -68.5 dBFS | -11.7 dB | 0.0 dB | PASS |
| cue-blessing | -30 | -30.9 | -30.0 / -40.0 | -19.2 dBTP | 0.0 % | 0.1 % | 9.8 % (in mix) | -70.4 dBFS | -13.5 dB | 0.0 dB | PASS |
| cue-ark-launch | -22 | -22.9 | -21.6 / -22.7 | -10.1 dBTP | 3.6 % | 0.1 % | 9.0 % (in mix) | -54.2 dBFS | -16.0 dB | 0.0 dB | PASS |

Also checked on every file, all passing: 0 clipped samples, no NaN, DC offset under -70 dBFS (worst -113 dBFS), no sound above 12 kHz lasting more than 100 ms, short-term loudness never above -18 LUFS for 20 s; ambience loops have no step or level jump at the wrap; cues have no dropouts.
The per-file check list (with values) is in [metrics.json](metrics.json).

Music details:

| File | Tempo | Mode | Voice silent | Stem gains (dB, set by measured loudness) |
|---|---|---|---|---|
| music-age-0-stone | 64.3 BPM | penta major | 58 % | ground +5.6, pulse +10.7 |
| music-age-1-bronze | 69.4 BPM | penta major | 52 % | colour +10.5, ground +3.8, pulse +11.3 |
| music-age-2-iron | 65.9 BPM | mixolydian | 52 % | colour +13.0, ground +8.0, pulse +10.7, voice -1.2; reed -6.5 LU re ground + colour (limit -6) |
| music-age-3-sail | 70.4 BPM | mixolydian | 52 % | colour +7.6, ground -2.2 |
| music-age-4-industry | 88.8 BPM | dorian | 55 % | colour -3.7, ground -9.4 |
| music-age-5-atomic | 88.8 BPM | dorian | 62 % | colour -5.2, ground -10.7 |
| music-age-6-space | 63.8 BPM | lydian | 58 % | colour -3.2, ground +4.5 |
| music-mood-peace | 73.9 BPM | mixolydian | 54 % | colour +6.7, ground -2.0 |
| music-mood-war | 79.8 BPM | dorian | 70 % | colour +8.2, ground -1.3, fx +0.4 |
| music-mood-plague | 66.5 BPM | mixolydian | 52 % | colour +7.5, ground +1.0, toll -2.2; toll at -10 dB re music |
| music-mood-golden-age | 77.6 BPM | mixolydian | 56 % | colour +2.2, ground -3.7 |
| music-mood-doom | 74.2 BPM | dorian | 60 % | colour -4.5, ground +1.7, fx -3.8 |

## Choices made for the previews

- The world: tonic D (D2 = 73.4 Hz), the "even" scale family: major pentatonic for Stone and Bronze, Mixolydian for Iron and Sail, Dorian for Industry and Atomic, Lydian for Space. All four music layers play (zoom01 > 0.6, a landscape or town camera).
- Each music file starts 3 s into its score, so the ground is already sounding at 0:00, and fades out over its last 4 s.
- In the Space preview the bone-flute callback is the third phrase so it falls inside the minute (in the game it is one phrase in four).
- Ambience files are seamless loops: set your player to repeat. In the game the beds are generated and never loop.
- Noise is fresh random noise, not the 2 s looped buffer of direction 0 (the loop would be audible in long beds).
- Everything goes through the master chain of 5.1: 30 Hz high-pass (12 dB/oct), DC blocker, glue compressor (-18 dB, 2:1, 30/300 ms, 6 dB knee) and a look-ahead limiter aimed at -1 dBTP.

## Flags for the lead (where the direction contradicts itself or its own checks)

- **Space hum vs the sub-bass rule.** The 2.1 recipe (two equal sines at 55 and 82.5 Hz) puts about half of the bed's energy below 60 Hz, against the 25 % rule of 5.4. The preview plays the 55 Hz sine 6 dB under the 82.5 Hz one (13 % below 60 Hz) and gives each a soft 2nd harmonic so the hum survives small speakers. Suggest the direction adopt this.
- **Pitch randomisation vs "in the world's key".** Direction 0 gives every one-shot +/- 3 % pitch (about +/- 50 cents), which would pull the in-scale cues (shimmer grains, chime, toll, the scale-locked UI sounds) out of key. The preview varies only level on tonal cues. Suggest +/- 3 % for noise-based cues and at most +/- 5 cents for tonal ones.
- **"exp" decay times.** Direction 0 says an "exp" time is the time constant tau; read literally, the Sail bell ("1 ms / 3 s exp") rings for 20 s and the drum for 1.5 s. The preview reads every decay time as the time to -60 dB, except where tau is named (impact tail, launch fade).
- **Shimmer register.** "Two octaves above the tonic" is D4 (294 Hz), but the same entry says 800-6,000 Hz. The preview puts the grains at D6 (1.2-2.3 kHz).
- **Doom cluster beat.** A tonic plus minor 2nd beats at 4-9 Hz in this register, not 3 Hz; the preview adds a copy of the tonic 3 Hz sharp to get the described 3 Hz beat.
- **Sail bell register.** The 1.2 bell (FM 1 : 3.5, index 2.5) in octave 5 puts its sidebands in 2-5 kHz; in the sparse plague mood one 3 s window reached 36 % (limit 30 %). The preview plays the Sail bells in octave 4.
- **Harshness rule on one-shots.** Shimmer is 800-6,000 Hz by recipe, so alone it is 56 % 2-5 kHz. Rule 5.4 is about the mix, so cues are checked over the peace music at -28 LUFS (rain stays exempt). The isolated value is in metrics.json.
- **Silence rule on one-shots.** A bell that decays as written falls under -60 dBFS while it is still ringing. For cues the preview checks for dropouts instead (no second more than 40 dB under the cue's loudest before its final decay) plus whole-file RMS above -60 dBFS. Music and ambience keep the absolute 1 s rule.
- **Coast-day ranges of 5.5.** Landscape at -32 LUFS, RMS of at least -36 dBFS and at least 10 % above 4 kHz only just overlap (a brighter bed reads louder in LUFS than in RMS). The 2.2 surf recipe alone is too dark for 10 %: the preview opens the crash to a broadband break and lets the forest leaves carry the highs. Suggest widening the RMS floor to -38 dBFS.
- **Isolated cue levels.** Cues are rendered alone at their in-game level ("re music" from 3.2 on a -28 LUFS music bus), so their short-term peak is the measured target; the music ducking of 5.2 does not apply here.

## How the current C++ synth differs (Game/Audio, read only)

The C9 synth predates the approved direction in places, so the game today will not sound exactly like these files. If the owner likes the previews, these are the main gaps to close:

- Tempo is fixed per age (56, 58, ... 68 BPM in `DmAudioMusic.cpp`); the direction asks for ranges from 52-64 (Space) to 76-96 (Atomic), with mood changes of -15 % to +8 %.
- Each phrase picks its instrument by weighted chance across the age's palette; the direction gives each age one lead voice, a colour instrument that answers it, and a pulse.
- The scale is one of eight modes chosen from the seed (`FDmScale::FromSeed`, tonic D to A); the direction chooses a family (even or odd) from the seed and changes the mode with the age (tonic D2 to G2).
- The chime rises through degrees 1-3-5 together; the direction wants the 5th first and a reply from the tonic below 120 ms later. The toll is eight additive partials rather than the FM bell with hum and tierce.
- The phrase grammar of 1.1 (start on 1, 3 or 5; end on 1, 2 or 5; the fourth phrase restating the first a 4th or 5th away; at least 50 % silence) and the mood overlays of 1.3 are not in the C++ score yet; it has a single "tension" darkening (cello drone, taiko, darker mode).

## Size

The set is 170 MB. The twelve 60-second music files alone are 138 MB at 48 kHz, 16-bit stereo; ambience is kept to 10-18 s loops and the cues stop 50 dB under their peak. Two ways to get under 150 MB if needed: mono cues (saves about 9 MB) or 45-second mood previews (saves about 14 MB).

## Regenerate

```
powershell -NoProfile -ExecutionPolicy Bypass -File "tools/audio-preview/run.ps1"            # everything (about 7 minutes)
powershell -NoProfile -ExecutionPolicy Bypass -File "tools/audio-preview/run.ps1" cue-meteor # one file
```

It needs Python with numpy (scipy is not used). The Python on PATH has no numpy, so `run.ps1` uses the Python bundled with Blender 5.2. Renders are deterministic for the seeds in `render.py`.
