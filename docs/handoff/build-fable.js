export const meta = {
  name: 'demiurge-build-fable',
  description: 'Build every DEMIURGE module on Fable, each followed by an independent adversarial verification; paced a few agents at a time, finished work marked on disk and skipped on resume',
  phases: [
    { title: 'Stage A build', detail: '23 independent modules: simulation, render core, fixtures, models, towns, camera, icons, audio, save' },
    { title: 'Stage A verify', detail: 'independent reviewer per module: adversarial tests, fixes in place' },
    { title: 'Sim integration', detail: 'whole-simulation runs across seeds, cross-module fixes, balance' },
    { title: 'Stage B build', detail: 'planet, sky, objects, effects, HUD, panels, screens on top of the render core' },
    { title: 'Stage B verify', detail: 'independent visual and contract review per module' },
  ],
}

const ROOT = 'C:/Users/Relly/Documents/Fable5.2 Test/demiurge'

const HEAD = `You are an engineer on DEMIURGE, a zero-dependency WebGL2 god game played on a small living 3D planet (plain JavaScript, custom renderer, synthesized audio, runs from file://).
Project root (Windows): "${ROOT}" (the path contains a space: quote it in shell commands; run commands from the project root).
About thirty engineers are building modules in this folder at the same time. The documents in docs/ are the contract between you. You cannot talk to the others. Do not touch anything outside the project root (the parent folder holds an unrelated, finished game).`

const READ = (specs) => `Read, in this order, completely:
  1. docs/CONCEPT.md (what the game is)
  2. docs/FOUNDATION.md (binding foundation APIs: DM.U, DM.V3, DM.M4, DM.Bus, DM.Geo, DM.State, tools, test conventions)
  3. ${specs}
  4. docs/BUILD.md: "Rules for everyone", "Tools", and the acceptance checks for your assignment
  5. docs/TOOLS.md (the headless browser rig) if your work is visible on screen or runs in a page
Then skim the existing code you will build on: js/core/*.js, js/sim/state.js, js/sim/schema.js (entity factories, DM.EVENTS), tests/helpers/world.js, tools/t.js, tools/load.js, tools/mapimg.js, and any file your spec section names. A placeholder file already exists for each of your source files: replace its contents.`

const SIM_SPEC = 'docs/SPEC-SIM.md in full (sections 0-2 are the shared rules, entities, writers table and events; then your module section; read the other module sections too, because you call them and they call you)'
const RENDER_SPEC = 'docs/SPEC-RENDER.md in full, and docs/SPEC-SIM.md sections 0-2 (rules, entities, events) plus the section 3 function lists for anything you call; docs/SPEC-UI.md section 1 (the Camera.view() object) if you use the view'
const UI_SPEC = 'docs/SPEC-UI.md in full, docs/SPEC-SIM.md sections 0-3 (rules, entities, events and the module APIs you display or call), and docs/SPEC-RENDER.md section 11 (Renderer API)'

const A = [
  { id: 'A1', key: 'names', specs: SIM_SPEC, files: 'js/sim/names.js, tests/names.test.js',
    notes: 'The names carry the whole fiction: a player should be able to tell two cultures apart by their place names alone, and should want to read them aloud. Study real-world phonologies for variety (but invent, do not copy real words).' },
  { id: 'A2', key: 'terrain', specs: SIM_SPEC, files: 'js/sim/terrain.js, tests/terrain.test.js',
    notes: 'The planet shape is the first thing the player sees and everything else (climate, rivers, where cities grow) follows from it. Use tools/mapimg.js constantly and judge the maps like a cartographer. Plain fBm blobs are not good enough: aim for continents with character (domain-warped shapes, mountain belts, peninsulas, archipelagos, shelves, a few deep trenches).' },
  { id: 'A3', key: 'climate', specs: SIM_SPEC, files: 'js/sim/climate.js, tests/climate.test.js',
    notes: 'js/sim/terrain.js is being written in parallel: do not depend on it in tests (use tests/helpers/world.js makeWorld with basics:false, or your own synthetic elevation). Stub DM.Life.cover. The rain-shadow effect and the wet/dry belts must be visible on a map. Pay special attention to the genesis rules (datum, calibrate, yearly doing nothing before history): a reviewer showed that getting them wrong freezes the planet or turns the sea level into NaN.' },
  { id: 'A4', key: 'hydro', specs: SIM_SPEC, files: 'js/sim/hydro.js, tests/hydro.test.js',
    notes: 'Rivers are a signature visual and decide where cities grow. Use makeWorld (it fills moist) and render maps with rivers to judge them.' },
  { id: 'A5', key: 'life', specs: SIM_SPEC, files: 'js/sim/life.js, tests/life.test.js',
    notes: 'The spreading green wave in the genesis life phase is one of the showpiece moments: make the front organic (not a perfect circle) and make sure it reaches every plantable region of a continent. Use makeWorld for inputs (it fills temp, moist, slope, coast, fresh). The fire rule is a percolation process with a sharp threshold: implement the documented numbers exactly and measure fire sizes.' },
  { id: 'A6', key: 'path-units', specs: SIM_SPEC, files: 'js/sim/path.js, js/sim/units.js, tests/path.test.js, tests/units.test.js',
    notes: 'Stub DM.Civ.found, DM.Civ.near/live and DM.Society.onArrive in tests. Pathfinding is called from yearly civ logic: keep it allocation-free and fast, with scratch buffers keyed by world.n.' },
  { id: 'A7', key: 'civ', specs: SIM_SPEC, files: 'js/sim/civ.js, tests/civ.test.js',
    notes: 'You write the lifecycle half of DM.Civ (js/sim/civ.js). A colleague writes js/sim/economy.js (Civ.yearly, Civ.capacity) in parallel: your file must work when economy.js is still a placeholder (call DM.Civ.capacity only if it exists, else use a simple estimate) and must not define yearly or capacity. Five other modules call your API, so follow the signatures to the letter and keep the single-writer rules of SPEC-SIM section 1.1.' },
  { id: 'A8', key: 'economy', specs: SIM_SPEC, files: 'js/sim/economy.js, tests/economy.test.js',
    notes: 'You write Civ.yearly and Civ.capacity in js/sim/economy.js, extending the DM.Civ namespace created by js/sim/civ.js (being written in parallel by a colleague: in your tests, stub the civ.js functions you call with T.stub when they are absent, with behaviour matching the spec). This module sets the pacing of the whole game. Implement the documented formulas exactly first, then check the five capacity gate numbers and the research timeline. Guard every division: a settlement must never produce NaN.' },
  { id: 'A9', key: 'society', specs: SIM_SPEC, files: 'js/sim/society.js, tests/society.test.js',
    notes: 'You write the belief half of DM.Society (js/sim/society.js). A colleague writes js/sim/diplomacy.js (rel, peek, atWar, declareWar, makePeace, onArrive, diplomacy) in parallel: your Society.yearly ends by calling DM.Society.diplomacy(world) if it exists, and you must not define the diplomacy functions. Build small hand-made scenarios in tests with the DM.State factories on a makeWorld planet and stub the collaborators (Civ, Units, Names, Chronicle, Climate). The witness formulas are the heart of the Awe economy: implement them exactly as written.' },
  { id: 'A10', key: 'diplomacy', specs: SIM_SPEC, files: 'js/sim/diplomacy.js, tests/diplomacy.test.js',
    notes: 'You extend DM.Society (created by js/sim/society.js, being written in parallel: start your file with const Society = (DM.Society = DM.Society || {});) with rel, peek, atWar, declareWar, makePeace, onArrive and diplomacy. Stub Civ, Units, Path, Climate, Life and the belief-side Society functions you need in tests. Wars must be legible to a player: a clear start, visible armies, a clear end.' },
  { id: 'A11', key: 'powers', specs: SIM_SPEC, files: 'js/sim/powers.js, tests/powers.test.js',
    notes: 'You write the first half of DM.Powers (LIST, byId, unlocked, cost, can, use and every instant effect). A colleague writes js/sim/disasters.js (start, update, tick, natural) in parallel: when Powers.start is absent your use() for comet / meteor / volcano must fail gracefully (return false), and your tests stub it. The power table in the spec is binding data: copy it exactly. These are the player\'s hands: every power must have an immediate, visible consequence in the world state. Real-time rate limits are counted from the dt arguments (a small clock advanced in use() and, when present, in Powers.update), never from a wall clock.' },
  { id: 'A12', key: 'disasters', specs: SIM_SPEC, files: 'js/sim/disasters.js, tests/disasters.test.js',
    notes: 'You extend DM.Powers (created by js/sim/powers.js, being written in parallel: start with const Powers = (DM.Powers = DM.Powers || {});) with start, update, tick and natural. Stub Terrain, Life, Civ, Society, Units and Climate in tests (and use the real ones when present for the map renders). Lava and tsunami propagation are the two hard parts: make them robust and bounded, and make their state easy for the effects engineer to read (data.front, power, ang as documented).' },
  { id: 'A13', key: 'chronicle', specs: SIM_SPEC, files: 'js/sim/chronicle.js, tests/chronicle.test.js',
    notes: 'This is the voice of the game: an old chronicle, translated, dry and a little wry, never purple. Write many templates per event and vary sentence shape. Read your own 40 sample entries critically and rewrite the weak ones. Build cultures and settlements with the DM.State factories on a makeWorld planet and stub DM.Names when absent (js/sim/names.js is being written in parallel; when present, use the culture\'s lang to coin words). Stub Society.regard.' },
  { id: 'A14', key: 'sim', specs: SIM_SPEC, files: 'js/sim/sim.js, tools/simrun.js, tools/policies.js, tests/sim.test.js',
    notes: 'You write the orchestrator while the modules it orchestrates are being written by others, so your tests use call-recording stubs (T.stub) for every other sim module; when real modules appear in js/sim/ during your work, also try running with everything loaded and report what happens (do not edit them). The tick schedule in the spec is exact: follow it literally. simrun and the policies will be used heavily for balancing later: make the timeline compact and informative.' },
  { id: 'A15', key: 'render-core', specs: RENDER_SPEC, files: 'js/render/gl.js, js/render/post.js, js/render/renderer.js, dev/view.html, dev/view.js, tests/browser/rendercore.scn.js',
    notes: 'You are the foundation of everything visible: four other engineers will build planet, sky, objects and effects on your GL helper, Frame block, shader chunks, post chain and dev viewer, without talking to you, so follow the spec to the letter. The shared lighting chunks (shade, atmosphere, inscatter, lightAt, sunShadow, cloudCover) define the look of the whole game: make them beautiful within the calibration block (soft terminator with a warm band, a cool moonlit night side that is still readable, rich sky fill). Start from tools/smoke/ (the capability smoke test shows what headless Edge supports and has working code for MSAA float targets, UBOs and shadow samplers). Sim modules and dev/fixtures.js are being written in parallel and may be placeholders while you work: the viewer must run with only the foundation present (build a simple noise planet itself when DM.Fixtures and DM.Sim are undefined) and use them when they exist.' },
  { id: 'A16', key: 'fixtures', specs: RENDER_SPEC, files: 'dev/fixtures.js, tests/fixtures.test.js',
    notes: 'Six rendering and interface engineers will develop against your fixture world before the real simulation works, so it must be rich, valid and stable. It is pure JS that loads in Node (via tools/load.js) and in the browser. Read SPEC-SIM sections 1-3 carefully so the world you fake is exactly what the real sim will produce (entity fields via the DM.State factories, rivers shape, _cells, _fires, disasters semantics). tests/helpers/world.js has closed-form stand-ins for terrain and climate you can adapt.' },
  { id: 'A17', key: 'models', specs: RENDER_SPEC, files: 'js/render/models.js, tests/models.test.js, dev/models.html',
    notes: 'Tiny low-poly models that must read instantly from far away: strong silhouettes, a few bold colour planes, slightly exaggerated proportions (toy-like). js/render/gl.js is being written in parallel, so give dev/models.html its own minimal WebGL2 code. The models file must be pure data and load in Node.' },
  { id: 'A18', key: 'towns', specs: RENDER_SPEC, files: 'js/render/towns.js, tests/towns.test.js',
    notes: 'Pure layout logic, no GL: it decides what a camp, village, town, city and metropolis look like in every age, so a settlement tells its story at a glance (tier, age, culture, what it has built). The model names are fixed by SPEC-RENDER section 8 (js/render/models.js is being written in parallel: do not depend on it; validate names against the spec list in your test). Use DM.State factories and tests/helpers/world.js to build test settlements. Plot plans as PNGs and judge them like a town planner.' },
  { id: 'A19', key: 'camera-input', specs: UI_SPEC, files: 'js/ui/camera.js, js/ui/input.js, tests/camera.test.js, dev/input.html, tests/browser/input.scn.js',
    notes: 'The camera is how the planet feels in the hand: dragging must feel like turning a globe with your finger, zoom must be smooth and land where the cursor points, and the tilt-in at close range is what makes the diorama look work. camera.js must load in Node (no DOM). js/game.js exists: read it to see exactly how Game, Camera and Input interact. The renderer is being written in parallel, so dev/input.html must stub what is missing. The touch rules in the spec exist because a reviewer found the game unplayable on a phone without them: implement them exactly.' },
  { id: 'A20', key: 'icons', specs: UI_SPEC, files: 'js/ui/icons.js, dev/icons.html',
    notes: 'One coherent hand: thin gold line icons with a slightly mythic, astronomical flavour. Each must be recognizable at 20 px.' },
  { id: 'A21', key: 'audio', specs: UI_SPEC, files: 'js/audio/audio.js, dev/audio.html, tests/browser/audio.scn.js',
    notes: 'Nobody on this team can listen to the result, so engineer for safety and verify numerically: conservative gains, a limiter on the master bus, slow attacks, no harsh raw oscillators (filter everything), reverb via a generated impulse response, gentle stereo. Use Audio.renderTest and inspect spectra / RMS envelopes to catch silence, clipping, DC offset, mud and harshness. js/audio/music.js (DM.Music) is being written in parallel by a colleague: call it only if it exists.' },
  { id: 'A22', key: 'music', specs: UI_SPEC, files: 'js/audio/music.js, dev/music.html, tests/browser/music.scn.js',
    notes: 'Nobody on this team can listen to the result, so compose by rule and verify numerically. Sparse and beautiful beats busy: long notes, consonant intervals from a pentatonic or modal scale chosen from the world seed, slow harmonic rhythm, soft attacks, everything filtered, gentle reverb from a generated impulse response. Each age needs its own timbre family built from simple synthesis. Music.init(ctx, outNode) must work with any AudioContext or OfflineAudioContext and must not need DM.Audio.' },
  { id: 'A23', key: 'save', specs: UI_SPEC, files: 'js/save.js, dev/save.html, tests/browser/save.scn.js',
    notes: 'Storage must never break the game: every failure path resolves quietly. Test over file:// (how most players will run the game) and http. dev/fixtures.js is being written in parallel: build your own small test world with DM.State.create and the factories.' },
]

const B = [
  { id: 'B1', key: 'planet', specs: RENDER_SPEC, files: 'js/render/planet.js, tests/browser/planet.scn.js',
    notes: 'The planet surface is 80% of every frame. Read js/render/gl.js, js/render/renderer.js, dev/view.js and dev/fixtures.js first to see exactly how you are called and what the shared chunks provide. While developing, load only your module in the viewer (only=planet) so half-written neighbours cannot break your screenshots; test together with the others at the end. The close-zoom fragment detail and the calibration block exist because a reviewer predicted the game would look cheap without them: take them seriously.' },
  { id: 'B2', key: 'sky', specs: RENDER_SPEC, files: 'js/render/sky.js, tests/browser/sky.scn.js',
    notes: 'Read js/render/gl.js (especially the clouds and lighting chunks you must stay consistent with), js/render/renderer.js and dev/view.js first. The atmosphere rim and the clouds are what make a sphere read as a living world from orbit: study how Earth looks from space (thin bright limb, soft terminator, cloud systems that swirl) and stylize it. The limb must match the surface haze exactly: zoom your screenshots on the planet edge.' },
  { id: 'B3', key: 'objects', specs: RENDER_SPEC, files: 'js/render/objects.js, tests/browser/objects.scn.js',
    notes: 'Read js/render/gl.js, js/render/models.js, js/render/towns.js, js/render/renderer.js, dev/view.js and dev/fixtures.js first. If js/render/planet.js is not ready, fall back to DM.State.groundR for placement but use DM.Planet.groundAt when it exists. The rolling sweep and the instance caps exist because rebuilding 20,000 trees in one frame would hitch twice a second.' },
  { id: 'B4', key: 'fx', specs: RENDER_SPEC, files: 'js/render/fx.js, tests/browser/fx.scn.js',
    notes: 'Read js/render/gl.js, js/render/renderer.js, dev/view.js and dev/fixtures.js first. These are the moments players will remember and screenshot: a meteor strike must feel enormous, lightning must crack, a volcano must glow at night. Timing matters as much as shape: anticipation, a hard bright hit, a slow aftermath.' },
  { id: 'B5', key: 'hud', specs: UI_SPEC, files: 'js/ui/hud.js, css/hud.css, tests/browser/hud.scn.js',
    notes: 'Read css/base.css, js/game.js, js/ui/icons.js, dev/play.html and dev/fixtures.js first. The simulation is being integrated in parallel and may misbehave: develop against DM.debug.fixture() and also try DM.debug.new(). A colleague builds the inspector, chronicle, now strip and labels in js/ui/panels.js in parallel: do not build those; on a narrow layout leave room for their bottom sheet. The HUD must stay out of the way of the planet: compact, translucent, calm. On a phone in portrait it must be fully operable with thumbs.' },
  { id: 'B6', key: 'panels', specs: UI_SPEC, files: 'js/ui/panels.js, css/panels.css, tests/browser/panels.scn.js',
    notes: 'Read css/base.css, js/game.js, js/ui/icons.js, dev/play.html and dev/fixtures.js first. Develop against DM.debug.fixture() (it has settlements in every state, a prophet, a war, chronicle entries) and also try DM.debug.new(). A colleague builds the top bar, palette, guide and toasts in js/ui/hud.js in parallel: position your panels so they cannot collide with a top bar (about 56 px) and a bottom palette (about 110 px), and use only pan- ids. Read-only access to the sim: Society.peek, never Society.rel.' },
  { id: 'B7', key: 'screens', specs: UI_SPEC, files: 'js/ui/screens.js, css/screens.css, tests/browser/screens.scn.js',
    notes: 'Read css/base.css, js/game.js, js/save.js, js/ui/icons.js, dev/play.html and dev/fixtures.js first. The title screen is the first impression: the planet turning behind an elegant, restrained title. The ending screen is the emotional payoff: give the six ending texts real feeling (one short paragraph each, in the chronicle\'s voice).' },
]

const buildPrompt = (m) => `${HEAD}

YOUR ASSIGNMENT: ${m.id} (${m.key}) in docs/BUILD.md.
FILES YOU OWN (edit only these; create the ones that do not exist yet): ${m.files}

${READ(m.specs)}

NOTE - this build was interrupted once by a usage limit. Before writing anything, look at your files: if one holds more than a placeholder, an earlier engineer started it and was cut off mid-work. Read it critically against the spec; if it is sound, finish, test and correct it rather than starting over (its tests may be missing or failing); if it is poor, rewrite it. Other modules around you may also be finished, half-finished or placeholders.

Then build your module to the spec, write its tests / dev page / browser scenario, run every acceptance check listed for ${m.id} in docs/BUILD.md, LOOK at your output (maps via tools/mapimg.js, screenshots via tools/shot.js) and iterate until it is right, not merely passing. When everything passes, write the file docs/handoff/done/${m.key}.built.txt containing one line (date, module key, test summary line): it tells the lead the build step is complete, so do not write it earlier. Finally report in the format BUILD.md rule 6 requires, with real numbers.

Notes from the lead: ${m.notes}`

const verifyPrompt = (m, report) => `${HEAD}

YOU ARE THE INDEPENDENT VERIFIER for assignment ${m.id} (${m.key}) in docs/BUILD.md. A colleague built it and reported it finished. You did not write it. Assume it has defects until you have shown otherwise.
FILES YOU MAY EDIT: ${m.files}, plus your own adversarial test file tests/${m.key}.adv.test.js (or, for on-screen work, dev/adv-${m.key}.html and tests/browser/${m.key}.adv.scn.js). Everything else is read-only.

${READ(m.specs)}

Then:
1. Read the implementation critically, line by line, against the spec: every API function, argument order, unit, return value, event name and payload, entity field, writer rule, formula and number; robustness (bad targets, empty worlds, poles, missing collaborators, dead entities, placeholder neighbours); determinism (no Math.random, Date, performance or key-order dependence in sim code); performance budgets; no DOM/WebGL access from sim code; nothing done at load time.
2. Do not trust the builder's tests. Write your own adversarial tests that try to break it (edge cases, long runs, hostile inputs, property checks against brute force), and re-run the acceptance checks for ${m.id} yourself. For anything visible, produce your own maps or screenshots and LOOK at them with a critical eye: is it actually good, or merely present?
3. Fix every defect you find directly in the owned files, keeping the documented API. Improve what is weak if you can do so safely. Re-run the module's tests and your own, and make sure you broke nothing.
4. When you are finished, write the file docs/handoff/done/${m.key}.txt containing one line (date, module key, your test summary line, your 1-10 quality score): it tells the lead the module is verified, so do not write it earlier.
5. Report: each defect found (with evidence) and what you changed; what you could not fix and why; the acceptance numbers you measured; your honest assessment of quality on a 1-10 scale with the main reason.

The builder's report, for context only (verify, do not trust):
${report}`

const SIM_KEYS = ['names', 'terrain', 'climate', 'hydro', 'life', 'path-units', 'civ', 'economy', 'society', 'diplomacy', 'powers', 'disasters', 'chronicle', 'sim']
const B_NEEDS = ['render-core', 'fixtures', 'models', 'towns', 'camera-input', 'icons', 'save']

// ---- pacing: at most LIMIT agents at a time, so finished work is banked before a usage limit can hit.
// args.skip  = module keys already verified (docs/handoff/done/<key>.txt exists)
// args.built = module keys built but not yet verified (docs/handoff/done/<key>.built.txt exists)
const SKIP = new Set((args && args.skip) || [])
const BUILT = new Set((args && args.built) || [])
const LIMIT = (args && args.limit) || 6
let active = 0
const waiting = []
const slot = () => new Promise((res) => { if (active < LIMIT) { active++; res() } else waiting.push(res) })
const release = () => { const next = waiting.shift(); if (next) next(); else active-- }
const run = async (prompt, opts) => { await slot(); try { return await agent(prompt, opts) } finally { release() } }

const runModule = (m, stage) => {
  if (SKIP.has(m.key)) return Promise.resolve({ id: m.id, key: m.key, skipped: true })
  const built = BUILT.has(m.key)
    ? Promise.resolve('(The build step finished in an earlier run and its report is not available. Read the code and tests.)')
    : run(buildPrompt(m), { label: 'build:' + m.key, phase: stage + ' build' })
  return built
    .then((report) => run(verifyPrompt(m, report), { label: 'verify:' + m.key, phase: stage + ' verify' })
      .then((verify) => ({ id: m.id, key: m.key, build: report, verify })))
    .catch((e) => ({ id: m.id, key: m.key, error: String(e) }))
}

const byKey = {}
for (const m of A) byKey[m.key] = m
const P = {}
// queue order: the render core first (seven Stage B modules wait on it), then the simulation, then the rest of Stage A
const order = ['render-core'].concat(SIM_KEYS, ['fixtures', 'models', 'towns', 'camera-input', 'icons', 'save', 'audio', 'music'])
for (const k of order) P[k] = runModule(byKey[k], 'Stage A')
const integratePrompt = `${HEAD}

YOUR ASSIGNMENT: "Stage A-int — simulation integration" in docs/BUILD.md.
FILES YOU OWN: everything under js/sim/ except schema.js and state.js, plus tools/simrun.js, tools/policies.js and tests/integration.test.js. (The module authors and verifiers have finished; you are now the single owner of the simulation.) Do not edit other tests except to fix a test that encodes behaviour you deliberately and justifiably changed; never weaken a test to hide a bug.

${READ(SIM_SPEC)}

All sixteen sim files were written in parallel by different engineers against the spec and verified in isolation, with each other stubbed. They have never run together. Your job is to make them one working simulation:
1. Run the whole test suite (node tools/test.js). Then run tools/simrun.js for one seed and fix whatever breaks first: exceptions, NaN, mismatched assumptions between modules (units, field ownership, event payloads, who clears which flag, the two halves of DM.Civ / DM.Society / DM.Powers), missing calls, ordering problems. Repeat until 4,000-year runs complete on many seeds.
2. Check determinism (simrun --hash twice gives the same fingerprint) and a mid-run save/load round trip (State.serialize, JSON, deserialize, Sim.use, continue) that keeps running sanely.
3. Measure performance (mean and worst tick at L = 6) and fix hot spots.
4. Balance: compare with SPEC-SIM section 4 across at least 6 seeds for the passive policy and 3 each for kind / cruel / chaotic. Put tuning overrides in js/sim/balance.js (and fix rules where the dynamics are wrong) until the targets hold or you can explain precisely why not. Watch for degenerate outcomes: early extinction, runaway growth, no wars ever or endless war, Awe pegged at zero or at the cap, climate that does nothing or kills everything, expansion that never happens.
5. Render owner and biome maps (tools/mapimg.js) at years 500, 1,500 and 3,000 for one seed and inspect them: do settlements cluster along rivers and coasts, do cultures hold coherent territories, does farmland replace forest, does the coastline change when the ice melts?
6. Write tests/integration.test.js with fast versions of the above (short runs at L = 4 or 5) so regressions are caught.
Keep every documented API and event unchanged. When all of the above is done, write docs/handoff/done/integration.txt with one line (date, "integration", the test and simrun summary). Report a per-seed table (year each age was reached, settlements and population at years 1,000 / 2,000 / 3,000, cultures, wars, ending and its year), the performance numbers, every cross-module bug you fixed, and what remains off-target.`


const simDone = Promise.all(SIM_KEYS.map((k) => P[k]))
const integP = SKIP.has('integration') ? Promise.resolve('skipped')
  : simDone.then((rs) => rs.some((r) => r.error) ? 'NOT RUN: a simulation module failed (' + rs.filter((r) => r.error).map((r) => r.key).join(', ') + ')'
      : run(integratePrompt, { label: 'integrate:simulation', phase: 'Sim integration' })).catch((e) => 'ERROR: ' + String(e))

const bReady = Promise.all(B_NEEDS.map((k) => P[k]))
const bP = bReady.then((rs) => rs.some((r) => r.error) ? [{ error: 'NOT RUN: a Stage A dependency failed (' + rs.filter((r) => r.error).map((r) => r.key).join(', ') + ')' }]
  : Promise.all(B.map((m) => runModule(m, 'Stage B'))))

const stageA = await Promise.all(order.map((k) => P[k]))
const stageB = await bP
const integration = await integP
return { stageA, stageB, integration }
