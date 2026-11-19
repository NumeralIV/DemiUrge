# DEMIURGE — Camera, input, interface, audio and shell spec (v2)

Read `CONCEPT.md`, `FOUNDATION.md`, `SPEC-SIM.md` §0–3 (rules, entities, events, module APIs)
and `SPEC-RENDER.md` §11 (Renderer API) first. **Module APIs, DOM ids and selectors, CSS tokens
and UI event names are binding.**

## 0. Shell (written by the lead; it exists: read `js/game.js`, `index.html`, `css/base.css`, `dev/play.html`)

`index.html` loads `css/base.css`, `css/hud.css`, `css/panels.css`, `css/screens.css`, then
every script in order: core → sim → render → `js/ui/camera.js`, `js/ui/input.js`,
`js/ui/icons.js`, `js/ui/hud.js`, `js/ui/panels.js`, `js/ui/screens.js`, `js/audio/audio.js`,
`js/audio/music.js`, `js/save.js`, `js/game.js`. Every module looks others up as `DM.X.fn` at
call time and does nothing at load time except define itself.

```html
<canvas id="view"></canvas>      <!-- WebGL, fills the window -->
<div id="labels"></div>          <!-- settlement name labels (pointer-events: none) -->
<div id="ui"></div>              <!-- HUD root (pointer-events: none; children opt in) -->
<div id="screens"></div>         <!-- modal screens -->
<div id="nogl"></div>            <!-- shown when WebGL2 is unavailable -->
```

### `js/game.js` → `DM.Game`

```
Game.state            'boot' | 'title' | 'play' | 'ending' | 'timelapse'
Game.paused           bool            Game.speed    0.25 | 1 | 2 | 4 | 8   (1× = 8 ticks/s = 2 years per real second; 0.25× is "linger";
                                                    genesis phases always run at 8 ticks/s)
Game.power            armed power id or null          Game.brushAng   brush radius in radians (0.03–0.15, default 0.06)
Game.selected         selected settlement id or -1    Game.overlay    0..5
Game.hudHidden        bool                            Game.world      the active world
Game.newWorld({ seed, name, L, calamity, quick })     quick = skip genesis (Sim.genesis.skip)
Game.continue()       load the autosave               Game.toTitle()        Game.save()
Game.setSpeed(n)      Game.togglePause()              Game.setPaused(bool)
Game.armPower(id | null)   arming the already-armed power disarms it          Game.setBrush(ang)
Game.select(id)       Game.setOverlay(mode)           Game.hideHud(bool)
Game.usePower(cell, dt)       apply the armed power at a cell (called by input); returns bool
Game.nextPhase()      Sim.genesis.next                Game.setEdict(cultureId, edict)
Game.flyToCell(cell, zoom)    Game.showEnding()       Game.showTimelapse()    Game.keepPlaying()
```

Behaviour the other modules can rely on:

- Main loop (requestAnimationFrame): `Input.update` → sim ticks (time-sliced, ≤ ~6 ms per
  frame, each tick preceded by nothing: `Powers.update(realDt)` runs once per frame, even when
  paused) → `Camera.update` → `Renderer.frame` → `HUD.update` → `Panels.update` →
  `Audio.update`. Autosave every 2 minutes, on pause and when the tab is hidden.
- On `phase`, Game arms `comet` (water), `seed` (life), `awaken` (people) and disarms on
  `'history'`. During `'people'` it sets overlay 5 (habitability) and restores 0 afterwards.
  During genesis and the time-lapse the sun mode is `'follow'`; in history it follows the
  `sun` setting. `awaken` is disarmed after it succeeds. On `people:lost` Game arms `awaken`.
- After the first awaken (or a quick start) Game selects the camp and flies to it.
- With setting `autoSlow`, when a weight-3 chronicle entry arrives at speed ≥ 4 Game drops to
  1× for 6 s, then restores the previous speed unless the player changed it.
- On `ending`: HUD hidden, speed 1×. Type `'ark'`: fly to the launch settlement and hold while
  FX plays the plume, ease back, then open `Screens.ending`. Type `'silence'`: ease out to far
  orbit, then open it.
- While a power is armed Game sets `Post.set.focusY` to the pointer's height and softens the
  tilt-shift, so the brush area stays sharp.
- First run: `quality` −1 (auto) becomes 0 on a small touch device, 1 without HDR targets, else
  2. When `Renderer.slow` is true Game lowers the quality one step (at most once per 20 s) and
  toasts 'Graphics lowered for smoothness'.
- Game calls `Camera.setViewport(w, h)` and `Renderer.resize()` on resize.

UI events on `DM.Bus` (besides the sim events in SPEC-SIM §2):

| event | payload |
|---|---|
| `ui:state` | `{ state, prev }` |
| `ui:speed` | `{ speed, paused }` |
| `ui:power` | `{ id }` (null = disarmed) |
| `ui:select` | `{ id }` (−1 = nothing) |
| `ui:overlay` | `{ mode }` |
| `ui:hud` | `{ hidden }` |
| `ui:settings` | `{ }` (settings changed; read `DM.Save.settings`) |
| `camera:shake` | `{ amount }` |
| `ui:toast` | `{ text, kind }` (ask the HUD to show a short message; kind `'info' | 'good' | 'bad' | 'epoch'`) |

### `DM.debug` (for automated tests through tools/shot.js)

```
debug.new({ seed, name, L, quick = true, years = 0, calamity }) → info    start a world without menus
debug.fixture({ seed }) → info     (dev/play.html only) a populated world from DM.Fixtures, no sim needed
debug.ff(years) → info             advance the sim synchronously with Sim.advance, no rendering
debug.cam(latDeg, lonDeg, zoom)    debug.camCell(cell, zoom)     jump the camera
debug.use(powerId, cell, seconds = 0)    synchronous: brush powers are held for `seconds`; click powers are followed by
                                         Powers.update(world, 3) so a comet or meteor has landed before it returns.
                                         debug.freeAwe = true lifts the cost.
debug.find(what) → cell            'capital' | 'city' | 'coast' | 'forest' | 'peak' | 'sea' | 'desert'
debug.settle(frames = 3) → Promise render a few frames so buffers are current
debug.freeze(t | null)             Renderer.freeze
debug.info() → { state, phase, year, age, pop, settlements, cultures, units, awe, aweMax, seaLevel, anomaly, speed, paused,
                 power, selected, frameMs, simMs, fps, errors, ending }
```

### `css/base.css` — design tokens (exists; do not redefine)

```
--ink: #0b0f17;   --ink-2: #131a26;   --ink-3: #1b2434;   --line: rgba(232,205,150,0.22);   --line-hi: rgba(232,205,150,0.55);
--gold: #e8cd96;  --gold-hi: #fff1c9; --text: #e9e4d6;    --dim: #b3b9c4;   --good: #8fd6a0;  --bad: #e2836f;
--awe: #ffd98a;   --water: #7fc7e8;
--font-display: 'Palatino Linotype', 'Book Antiqua', Palatino, Georgia, serif;
--font-ui: 'Segoe UI', system-ui, -apple-system, 'Helvetica Neue', Arial, sans-serif;
--fs-small: 13px; --fs-body: 15px; --fs-title: 22px;      (all × --ui-scale; no text under 12 px)
--radius: 10px;   --pad: 12px;        --panel-bg: rgba(11,15,23,0.84);   --shadow;   --ui-scale: 1;
.dm-panel  .dm-btn  .dm-btn.primary  .dm-title  .dm-serif  .dm-num  .dm-dim  .dm-rule  .dm-bar > i
```

Quiet, celestial, gold-on-ink; serif for names and headings, sans for numbers and controls. No
emoji, no external fonts or images; icons are inline SVG from `DM.Icons`. **No signal is
colour-only**: statuses are icon plus word, good and bad changes carry + or −, culture colours
are backed by the culture's initial as a sigil. Everything must work at 1280×720, 1920×1080 and
a 390×844 portrait phone (touch targets ≥ 40 px; all edges respect `env(safe-area-inset-*)`).

### Selectors (binding, so tests and modules can find things)

HUD element ids start with `hud-`, panel ids with `pan-`, screen ids with `scr-`.
`#hud-year`, `#hud-age`, `#hud-awe` (the number), `#hud-pop`, `#hud-menu`, `#hud-eye`,
`[data-speed="0|0.25|1|2|4|8"]` (0 = pause), `[data-power="<id>"]` with state classes `locked` /
`poor` / `armed`, `#hud-armed` (the armed chip) with `[data-action="disarm"]`,
`#hud-genesis` and `#hud-genesis-next`, `[data-overlay="0..5"]`, `#hud-toasts`.
`#pan-inspector`, `[data-edict="<edict>"]`, `#pan-chronicle-open`, `#pan-chronicle`,
`.pan-entry[data-entry="<id>"]`, `.pan-label[data-settlement="<id>"]`, `#pan-now`.
One `#scr-<name>` root per screen; buttons carry
`[data-action="continue|new|settings|help|begin|resume|exit|timelapse|card|keep|close|back"]`;
`#scr-name`, `#scr-seed`.

## 1. `js/ui/camera.js` → `DM.Camera` (pure math; loads in Node)

```
Camera.state = { focus: [x,y,z] (unit), zoom: 0..1, heading: radians }      zoom 0 = far orbit, 1 = closest
Camera.reset()                       Camera.setViewport(wPx, hPx)
Camera.update(dt, groundAt)          groundAt(dir) → radius (Planet.groundAt) or null; smooths toward targets, applies inertia, fly-to and shake
Camera.view() → { eye, target, up, right, focusDir, fov, aspect, near, far, zoom01, altitude }
                                     vectors are Float32Array(3); fov is the VERTICAL field of view in radians; altitude = height of the
                                     eye above the ground beneath it
Camera.orbit(dxPx, dyPx)             drag by pixels: the surface under the cursor follows the cursor at any zoom; inertia on release
Camera.zoomBy(amount, towardDir = null)   positive = closer (wheel / pinch); optional unit direction to zoom toward
Camera.turn(dAng)                    change heading (which way is "up" on screen)
Camera.flyTo(dir, zoom = null, seconds = 1.6)      eased great-circle flight
Camera.jump(dir, zoom, heading = 0)  no animation
Camera.shake(amount)                 0..1, decays; scaled by the `shake` setting
Camera.busy() → bool                 true while flying
```

fov ≈ 35° vertical. Distance of the eye from the planet centre at zoom 0 =
`1.08 × (DM.R + 17) / sin(atan(tan(fov/2) × min(1, aspect)))`: about 420 in landscape and about
880 on a 390×844 phone, so the whole planet and its atmosphere fit in any orientation. Height
above the ground runs exponentially from that down to 6 at zoom 1, measured above `groundAt`
under the eye, and never less than 1.5 above the ground anywhere. The view looks straight down
at the focus when far and tilts toward the horizon as it closes in (0° until zoom ≈ 0.35,
rising smoothly to ≈ 58° at zoom 1), orbiting the terrain point under the focus.
`near = clamp(0.05 × altitude, 0.25, 150)`; `far = sqrt(d² − 93²) + 75` with d = |eye| (reaches
the far limb of the atmosphere shell). Poles must not cause flips or NaN; heading is preserved
by parallel transport, not a global "up" vector.

## 2. `js/ui/input.js` → `DM.Input`

```
Input.init(canvas)      Input.update(dt)       per frame: held keys, held brush, hover picking (≤ 10 Hz)
Input.hover = { hit, cell, dir, settlement, can }   what is under the pointer (settlement id or -1; can = Powers.can result when a power is armed)
```

Input is ignored (pointer and keys) while `DM.Screens && DM.Screens.current` is truthy, and
keys are ignored while `document.activeElement` is an input or textarea. Pointer events over
HUD panels never reach the canvas. The canvas never scrolls or zooms the page and shows no
context menu.

- **No power armed**: drag = orbit; wheel / pinch = zoom toward the pointer; two-finger twist
  or right-drag horizontally = heading; click or tap a settlement = `Game.select`; click empty
  ground = deselect; double-click / double-tap = fly closer to that point. Touch long-press
  (500 ms, under 10 px of movement) shows the hover tip for that cell or settlement.
- **Click power armed**: a primary press + release inside 8 px (mouse) or 14 px (touch) fires
  `Game.usePower(cell, 0)`; dragging past that threshold orbits instead and cancels the click.
- **Brush power armed**: the power is applied every frame while the primary button or one
  finger is held (`Game.usePower(cell, dt)`). On touch, application starts 100 ms after
  touch-down and only if no second pointer is down; a second pointer at any time ends the stroke
  and the gesture becomes orbit / pinch / twist until all fingers lift. Right-drag, middle-drag
  or two fingers always orbit.
- **Ring and feedback**: while a power is armed, `Renderer.setBrush(hover.dir, radius, rgba)`
  with radius = `Game.brushAng` for brush powers and `seed`, otherwise `Powers.byId[id].ang`
  (hidden for settlement-targeted powers, which highlight the settlement under the pointer
  instead), coloured by power group. When `Powers.can` is not ok for the hover cell the ring is
  drawn dashed (alpha 0.35). A rejected click emits `ui:toast` kind `'bad'` with plain copy per
  `why`: 'Choose open sea', 'Choose land', 'Choose a settlement', 'Nothing can live here yet',
  'Too close to another people'. On touch the ring appears at touch-down.
- **Disarm**: right-click without dragging, Escape, or the HUD's armed chip / palette button.
- **Keyboard**: `WASD` / arrows orbit, `Q` / `E` heading, `+` / `-` or `R` / `F` zoom, `Space`
  pause, `1 2 3 4` speed 1× 2× 4× 8×, `0` linger (0.25×), `Tab` cycles overlays, `H` hides the
  HUD, `[` `]` brush size (× 0.8 / × 1.25), `Esc` = disarm → deselect → pause menu, and each
  power's `Powers.byId[id].key` arms it. Input never defines its own letters for powers.

## 3. `js/ui/icons.js` → `DM.Icons`

`Icons.svg(name, size = 20) → string` of inline SVG (stroke = currentColor, 24×24 viewBox, 1.6
stroke, round joins). An unknown name returns a small neutral dot and never throws. Names: one
per power id (`comet, seed, awaken, raise, lower, rain, bloom, lightning, quake, plague, meteor,
volcano, heal, inspire, prophet, peace`), ages (`age0 … age6`), buildings (`shrine, temple,
granary, walls, port, market, mine, factory, academy, plant, launch, wonder`), statuses
(`plague, famine, war, blessed, inspired, praying, rising-sea`), and `awe, people, culture,
trade, ship, thermometer, sea, pause, play, linger, fast, faster, fastest, eye, layers, scroll,
gear, close, back, save, home, camera, sun, moon, lock, check, arrow, plus, minus, heart, skull`.

## 4. `js/ui/hud.js` → `DM.HUD` (+ `css/hud.css`)

```
HUD.init()             build the DOM inside #ui; subscribe to the Bus
HUD.update(dt)         per frame; internal throttling (numbers ≤ 4 Hz)
HUD.toast(text, kind)
```

Visible only in `Game.state === 'play'`; hidden when `Game.hudHidden` except one 44 px `eye`
button (`#hud-eye`) in the top-right corner that brings it back.

- **Top bar**: menu button (`#hud-menu`, opens `Screens.pause()`); world name; the year
  (`Chronicle.yearStr`) and the current age with its icon inside a progress ring
  (`Civ.ageProgress` of the leading culture); the **Awe** orb with its number, "+n / yr" from
  `Society.aweIncome`, and under it the god's current epithet and the regard word with an icon
  (Beloved / Feared / Forgotten / Regarded, from `Society.regard(world)`); people, living
  cultures, temperature anomaly (`Climate.anomaly`) and sea-level change (relative to
  `vars.seaBase`), each shown only once non-zero; from `ark:started`, an Ark bar (largest `ark`)
  captioned 'They will leave you {mood}' that flies to the launch site when clicked; time
  controls (pause, linger, 1×, 2×, 4×, 8×).
- **Power palette**: buttons grouped Shape / Sky / Wrath / Grace (and the genesis power of the
  current phase), each with icon, name, cost (`Powers.cost`), hotkey (`def.key`). Locked powers
  are dimmed with a lock and a tooltip built from `def.unlock`: 'Wakes in the {age} Age, or when
  your Awe can hold {awe}.' A power whose cost exceeds `god.aweMax` says 'Your Awe cannot hold
  this much yet: they must raise more shrines and temples.' Unaffordable ones are dimmed
  (`poor`); the armed one is highlighted, and clicking it again disarms. A newly unlocked power
  pulses once. Each tooltip has the one-line `def.desc`.
- **Armed chip** (`#hud-armed`): while a power is armed: icon, name, cost, a close button, and
  brush size − / + for brush powers.
- **Genesis guide** (`#hud-genesis`): during `molten / water / life / people`, a card with one
  sentence, a progress bar from `Sim.genesis.progress`, and the continue button
  (`#hud-genesis-next` → `Game.nextPhase`) when `canAdvance`. Copy (the HUD owns it):
  molten 'Your world is cooling. Drag to turn it, scroll or pinch to come closer.';
  water 'Call down comets to fill the seas. Raise or lower the land while the clay is soft.'
  plus 'Ocean {n} %' and a word (under 35 % 'a dry, hard world', 35–75 % 'a temperate world',
  over 75 % 'a world of islands');
  life 'Touch the land to seed it. Seed each shore you want green.';
  people 'Choose where your people wake. Green ground is kind; the glowing mark is the kindest.'
  After `people:lost`: 'Your people are gone. Wake another.'
- **First steps**: after history begins the guide card shows one-time hints, each on its first
  trigger, dismissible, never blocking, remembered in localStorage `demiurge.hints.v1`, off with
  setting `hints`: (1) history start: 'Your people are awake. Their faith fills the gold orb
  with Awe. Awe is what miracles cost.' (2) Awe ≥ 20: 'Arm Rain and hold it over dry ground near
  them. They will notice.' (3) first shrine: 'A shrine. Your Awe now holds more and fills
  faster.' (4) first prayer, famine or plague: 'They are asking for you. Answer or do not; they
  will remember either.' (5) first war: 'War. An Omen of Peace ends it; a Prophet can forbid
  it.' (6) first new age: 'A new age. New powers wake with them.' (7) `ark:started`: 'They are
  building a ship to leave. How they feel about you now is how the story ends.'
- **Overlay switch** (none / cultures / temperature / rainfall / devotion / habitability) with a
  legend, and a hide-HUD button.
- **Hover tip**: a small tooltip near the pointer after a short delay: biome, temperature,
  rainfall, or the settlement's name and population; with a power armed, its cost and why the
  target is invalid.
- **Toasts** (`#hud-toasts`) for `ui:toast`, `awe:short` ('Not enough Awe') and notable events;
  weight-3 chronicle entries get a large centred banner with a flourish (clickable: fly there).
- **Narrow layout** (viewport width < 600 px): top bar row 1 (44 px): menu, Awe meter with
  number, year with age icon, pause, one speed button that cycles 1× 2× 4× 8×; row 2 (small
  text): epithet, people, cultures, anomaly and sea change when non-zero. Bottom: four group
  tabs (44 px high) and one row with that group's powers (icon and cost only; the name appears
  in the armed chip; no hotkeys); the genesis power replaces the tabs during genesis. Panels
  (§4b) open one at a time as a bottom sheet (≤ 45 % of the height) with a close button; the
  palette hides while a sheet is open.

Numbers use `DM.U.fmtBig`. All text is plain language; no internal ids are shown to the player.
`HUD.update` ≤ 0.4 ms on average.

## 4b. `js/ui/panels.js` → `DM.Panels` (+ `css/panels.css`)

```
Panels.init()          build the DOM inside #ui and #labels; subscribe to the Bus
Panels.update(dt)      per frame; labels follow the camera every frame while it moves, other content ≤ 4 Hz
Panels.openChronicle(bool)
```

- **Inspector** (`#pan-inspector`), shown while a settlement is selected: name, culture (colour
  dot with sigil, age), tier, population with trend and capacity in words ('412 people,
  growing. The land could feed 520.'), buildings as small icons, a devotion bar, a love ↔ fear
  marker on a labelled scale (terrified, wary, indifferent, fond, adoring), statuses (plague,
  famine, blessed, inspired, praying for …, at war) as icon plus word, and its culture's
  summary: traits in words, relations with other cultures (`Society.peek`, never `rel`), their
  name and epithet for the god, the living prophet, and the four edict buttons
  (`[data-edict]` → `Game.setEdict`) with one line each saying what the edict does.
- **Chronicle feed**: the latest entries fade in at the side; clicking one flies to its cell.
  `#pan-chronicle-open` opens the full chronicle (`#pan-chronicle`: scrollable, filter by
  culture and by weight). On a narrow layout the feed is a one-line ticker above the palette.
- **Now strip** (`#pan-now`): a compact list of what needs attention, built from world state at
  2 Hz: wars (A vs B), settlements with plague or famine (counts), prayers, active disasters;
  clicking one flies to it and cycles through its places.
- **Settlement labels** in `#labels`: names positioned with `Renderer.project`; capitals and
  cities appear from far away, towns closer, villages closest; at most ~40 by importance; a
  culture-coloured dot with sigil; up to two status glyphs (plague, famine, war, blessed,
  praying, 'sea rising' when the centre is less than 0.15 above the sea); a 2 px ink halo so
  they read over ice and cloud; fade instead of popping; never block pointer events.

## 5. `js/ui/screens.js` → `DM.Screens` (+ `css/screens.css`)

```
Screens.init()
Screens.current        PROPERTY (never a function): 'title' | 'setup' | 'pause' | 'settings' | 'help' | 'ending' | 'timelapse' | 'card' | null
Screens.title()        logo "DEMIURGE", tagline, Continue (if Save.meta(): world name / year / age), New World, Settings, How to play, credit line
Screens.setup()        name (with a "roll" button using Names.planet), seed (text; empty = random), calamities (calm / normal / wild),
                       start (Genesis: shape it yourself — or — Quick start: begin at the first tribe); Begin → Game.newWorld.
                       If Save.meta() is not null, Begin first asks 'This replaces {name}, {year}. Begin anyway?'
Screens.pause()        Resume, Settings, How to play, Save and exit to title; shows the world name
Screens.settings()     volumes (master, music, effects, ambience), graphics quality (auto / low / medium / high), sun (day and night / always lit),
                       miniature blur, flashes and screen shake (0–100 %), interface scale, settlement labels, hints, slow down for great
                       events; writes DM.Save.settings, calls Save.saveSettings and emits ui:settings
Screens.help()         controls (mouse, keyboard, touch) and the seven first-steps lines
Screens.ending(ending) the ending's title and text by type and mood, the Chronicle.tale paragraphs, statistics and Sim.score, each culture's
                       last epithet for the god; buttons: Watch the ages (Game.showTimelapse), Planet card, Keep playing, New world
Screens.timelapse()    plays world.snapshots through Renderer.showSnapshot with a year counter, a scrub bar and weight-3 chronicle entries as
                       captions while the camera slowly orbits; a visible Close button (and Esc) returns to the ending screen or the game
Screens.card()         a shareable "planet card" (2D canvas: the current frame from Renderer.capture, world name, years, age, epithet, key
                       stats) downloaded as PNG
Screens.close()
```

The title screen sits over the live planet (the game renders a slowly turning world behind it).
Every screen has a visible Back / Close button; Escape is only a shortcut. Screens fade in and
out. Keyboard and touch friendly. Ending texts: one short paragraph per ending (ark-beloved,
ark-feared, ark-forgotten, ark-parting, silence-quiet, silence-ash) in the chronicle's voice.

## 6. `js/save.js` → `DM.Save`

```
Save.settings          { master: 0.8, music: 0.7, sfx: 0.9, ambience: 0.8, quality: -1 (auto), sun: 'orbit', tilt: 1, flashes: 1, shake: 1,
                         uiScale: 1, labels: true, hints: true, autoSlow: true }      (flashes and shake default to 0.3 under prefers-reduced-motion)
Save.loadSettings()    Save.saveSettings()            localStorage key 'demiurge.settings.v1'
Save.write(world) → Promise<bool>     autosave slot: State.serialize(world) + world.snapshots into IndexedDB ('demiurge' / 'saves');
                                      falls back to localStorage (without snapshots) when IndexedDB is unavailable
Save.read() → Promise<world | null>   State.deserialize (+ snapshots); null when there is no save or it is unreadable.
                                      (The caller runs Sim.use on the result.)
Save.meta() → { name, year, age, pop, savedAt } | null      small summary kept in localStorage for the title screen
Save.clear() → Promise
```

Never throws; storage failures resolve to `false` / `null`.

## 7. `js/audio/audio.js` → `DM.Audio` and `js/audio/music.js` → `DM.Music`

Everything is synthesized with WebAudio. No samples. Nobody on the team can listen to it, so it
is engineered for safety and verified numerically.

```
Audio.init()           create the context lazily on the first user gesture; safe to call repeatedly; then DM.Music.init(ctx, Audio.buses.music)
Audio.buses            { master, music, sfx, ambience, ui }  GainNodes; master → limiter → destination
Audio.update(dt, ctx)  ctx = { state, paused, speed, zoom01, focusCell, day (0..1 at the focus), world }; also calls DM.Music.update(dt, ctx)
Audio.play(name, { cell = -1, vol = 1 } = {})     one-shot effect; cell positions it (volume and pan from its angle to the camera focus and the zoom)
Audio.setVolumes()     read DM.Save.settings
Audio.suspend(bool)
Audio.renderTest(seconds, scenario) → Promise<{ peak, rms, clipped, nan, bands: { sub, low, mid, high }, zeroCrossings }>
        renders the whole mix offline (OfflineAudioContext, music included) for a scripted scenario: 'orbit', 'coast-day', 'forest-night',
        'city', 'storm', 'meteor', 'age-advance', 'stone', 'industry', 'space'

Music.init(ctx, outNode)       Music.update(dt, ctx)       Music.stinger(name)     name: 'age' | 'founding' | 'miracle' | 'wrath' |
                                                           'ending-beloved' | 'ending-feared' | 'ending-forgotten' | 'ending-parting' | 'ending-silence'
```

One-shot names (binding; `Audio.play` ignores unknown names): `hover, click, arm, deny, unlock,
thunder, impact, rumble, wave, rainfall, shimmer, chime, choir, horn, toll, roar, crackle`.

- **audio.js** owns the context, buses, limiter, effects and ambience, and maps sim events to
  effects (thunder, meteor fall and impact, eruption rumble, quake, wave, rain brush, bloom
  shimmer, sculpting rumble, healing chime, prophet choir, war horn, plague toll, launch roar)
  and UI events (hover, click, arm, deny, unlock). **Ambience** follows the camera: far out, a
  soft airy hum; closer, wind; near coasts, surf; over forests birdsong by day and crickets by
  night; rain and thunder in storms; a crackle near fires and lava; near settlements, faint life
  that changes with the age (hammering, bells, engines, traffic hum).
- **music.js**: slow generative score. Instrumentation and harmony evolve with `world.age`
  (bone flute and hand drum → lyre and reed → strings and bells → brass and piano → synth pads),
  in a scale chosen per world from its seed; thins out when zoomed far away, a darker layer in
  war or disaster, a held breath during genesis. Stingers on new age, founding, miracle, wrath
  and the ending (one per mood). Sparse rather than busy.
- Mix: music sits under everything; nothing clips (master limiter); sub-bass under control
  (energy below 60 Hz under ~25 % of the total in every scenario); volumes from settings. No
  sound before a user gesture; every call is safe while the context is missing or suspended.
