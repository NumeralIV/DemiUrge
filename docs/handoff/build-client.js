export const meta = {
  name: 'demiurge-build-client',
  description: 'Build the DEMIURGE Unreal client in private lanes: bridge first, then planet, sky, camera/input, nature, towns, fx, HUD, screens and audio in parallel, each built then independently verified; Opus for complex work, Haiku for simpler work; markers on disk',
  phases: [
    { title: 'Bridge', detail: 'sim subsystem, fixture world, lanes, scenario/shot harness; then verification' },
    { title: 'Client build', detail: 'nine client modules in parallel lanes' },
    { title: 'Client verify', detail: 'independent adversarial reviewer per module, visual and contract checks' },
  ],
}

const ROOT = 'C:/Users/Relly/Documents/Fable5.2 Test/demiurge'
const UE = 'C:/Program Files/Epic Games/UE_5.7'
const OPUS = { model: 'opus', effort: 'high' }
const HAIKU = { model: 'haiku', effort: 'xhigh' }
const REPORT = {
  type: 'object',
  properties: {
    status: { type: 'string', enum: ['done', 'partial', 'blocked'] },
    report: { type: 'string', description: 'What you built, deviations, commands with results, screenshots taken (file names) and what they show, performance numbers, known weaknesses, decisions for the lead' },
  },
  required: ['status', 'report'],
}

const HEAD = `You are a senior Unreal Engine 5.7 C++ engineer and technical artist on DEMIURGE, a god game on one small living planet. The look must be cinematic and near-photoreal: the owner has rejected low-poly and procedural-primitive art twice.
Project root (Windows): "${ROOT}" (contains a space: quote paths). Unreal 5.7 at "${UE}". Visual Studio 2022 (MSVC 14.44) is installed. GPU: RTX 3060 12 GB. You have PowerShell and Git Bash; invoke .ps1 files as powershell -NoProfile -ExecutionPolicy Bypass -File "<script>".
Other engineers work in parallel and you cannot talk to them; docs/ is the contract.
Read first, completely: docs/UNREAL.md, docs/SPEC-UNREAL.md (binding for you), docs/FOUNDATION-CPP.md (the C++ simulation API you build against), docs/CONCEPT.md; then the old-spec sections SPEC-UNREAL.md names for your module (they define WHAT the player sees and does), and the reference lab: UnrealLab/PlanetLab (LabPlanet.cpp, LabGameMode.cpp, Scripts/make_materials.py, shot.ps1) and its screenshots shots/ue-lab-orbit*.png.
Rules: edit only files you own (SPEC-UNREAL section 0 and your assignment); work in your lane (SPEC-UNREAL 0.1); never open the editor GUI; no network, git or downloads; never touch anything outside the project root. Prove everything with builds, logs and screenshots you look at with the Read tool. If a file you own already holds work (an interrupted earlier run), judge it and continue or rewrite. Your final answer is a report object.`

const C = [
  { key: 'planet', build: OPUS, verify: OPUS, notes: 'The planet is the star. Port the lab, then chase quality: crack-free chunked LOD for the descent to the surface, coastline detail, ocean, rivers visible from orbit, and every per-cell state the sim can change. Take screenshot series: full orbit day and night, terminator, a coast at mid zoom, a mountain range near the ground, a river delta.' },
  { key: 'sky', build: OPUS, verify: OPUS, notes: 'Clouds must look like weather, not tiled puffs: large systems, fronts, a cyclone visible from orbit, clear deserts. Generate the cloud-cover texture from the sim fields (fixture first). Stars and a moon in the black of space.' },
  { key: 'camera-input', build: OPUS, verify: OPUS, notes: 'Follow SPEC-UI sections 1 and 2 exactly. Prove the descent from full orbit to just above a valley with a screenshot series, and every binding with logged input tests driven from a scenario.' },
  { key: 'nature', build: OPUS, verify: HAIKU, notes: 'Trees must not read as cones and balls. Build shaded generated meshes with real silhouettes (layered leaf cards with alpha-tested foliage material generated in the material graph, trunks with taper), instanced by biome density from the world. Forest canopy must also read from orbit.' },
  { key: 'towns', build: OPUS, verify: OPUS, notes: 'Settlements must look like places people live: organic street layouts following terrain, varied roofs, walls and towers, docks on coasts, fields around villages, lit windows at night. Age progression from huts to towers must be obvious.' },
  { key: 'fx', build: OPUS, verify: OPUS, notes: 'Spectacle with clarity. Each effect needs a scenario and a 3-6 frame screenshot sequence (use -warm values to step through time).' },
  { key: 'hud', build: OPUS, verify: HAIKU, notes: 'Quiet gold on ink, serif type, the planet stays the star. Every element in SPEC-UI 3, 4 and 4b, with screenshots of each state.' },
  { key: 'screens', build: HAIKU, verify: HAIKU, notes: 'Every screen in SPEC-UI 5 and 6 with screenshots, matching the HUD style (read the hud engineer\'s files once they exist in main; do not edit them).' },
  { key: 'audio', build: OPUS, verify: HAIKU, notes: 'Nobody can listen: verify numerically (render offline buffers in a scenario and analyse RMS, peaks, spectra). Warm, mythic, never harsh.' },
]

const SKIP = new Set((args && args.skip) || [])
const BUILT = new Set((args && args.built) || [])
const LIMIT = (args && args.limit) || 6
let active = 0
const waiters = []
async function slot(fn) {
  while (active >= LIMIT) await new Promise((r) => waiters.push(r))
  active++
  try { return await fn() } finally { active--; const w = waiters.shift(); if (w) w() }
}
const run = (prompt, opts) => slot(() => agent(prompt, { schema: REPORT, ...opts }))
const ok = (r) => r && r.status === 'done'
const results = {}

phase('Bridge')
const BRIDGE = `${HEAD}

YOUR ASSIGNMENT: C0 "bridge" from docs/SPEC-UNREAL.md section 2, including lane.ps1, shot.ps1, the scenario harness, the fixture loader and Config. You work directly in the main project (lanes do not exist yet); the simulation engineers are concurrently editing Unreal/Source/Demiurge/Sim/ and Unreal/SimTests/: never edit those. If the main build fails because of a half-written Sim file, make your own lane first (build lane.ps1 early) and test there.
Requirements beyond the spec: lane builds must not block each other (each lane is its own project; use Build.bat with -NoMutex only if UBT's mutex is global, and prove two lanes can build at once). shot.ps1 must take a machine-wide lock so that at most 2 game instances render at once (the GPU has 12 GB), with a timeout that kills a hung instance. Lane creation must be fast (no Intermediate copy). The fixture world must look like a living planet: load UnrealLab/data/planet_7.lab (format in UnrealLab/export_planet.js), then generate settlements, cultures, roads and units consistent with the habitability field. Provide a demo scenario "orbit" that renders the fixture with a placeholder planet (you may reuse the lab code as a temporary visual) so later engineers start from a working shot.
When all of it works, write docs/handoff/done-ue/client-bridge.built.txt (one line: date, key, build result, scenario screenshot path).`

const BVERIFY = (rep) => `${HEAD}

YOUR ASSIGNMENT: independent adversarial verification of C0 "bridge". Nine engineers start the moment you sign off, each in a lane made by lane.ps1, each relying on the subsystem, fixture, scenario harness and shot.ps1. You may fix anything C0 owns; record every fix. The builder reported:
---
${rep || '(built in an earlier run; see docs/handoff/done-ue/client-bridge.built.txt)'}
---
Check with your own runs: lane new / sync on two keys at once (and that sync copies only owned files); two lanes building at once; three shot.ps1 calls at once (the lock holds; nothing hangs); the fixture world against the sim API (no NaN, ids consistent, settlements on habitable land); sim ticking at every speed, pause, save / load round trip through the subsystem; every sim event reaches OnSimEvent with its payload; cell picking accuracy at orbit and near the ground; the "orbit" scenario screenshot. Write docs/handoff/done-ue/client-bridge.txt (one line: date, key, summary, your 1-10 score) when it holds.`

if (!SKIP.has('bridge')) {
  let br = null
  if (!BUILT.has('bridge')) {
    br = await run(BRIDGE, { ...OPUS, label: 'build:bridge', phase: 'Bridge' })
    results['bridge.build'] = br ? br.status : 'failed'
    if (!ok(br)) return { stopped: 'bridge-build', results, report: br && br.report }
  }
  const bv = await run(BVERIFY(br && br.report), { ...OPUS, label: 'verify:bridge', phase: 'Bridge' })
  results.bridge = bv ? bv.status : 'failed'
  if (!ok(bv)) return { stopped: 'bridge-verify', results, report: bv && bv.report }
}

const BUILD_C = (m) => `${HEAD}

YOUR ASSIGNMENT: client module "${m.key}" from docs/SPEC-UNREAL.md section 2. Create your lane first: powershell -NoProfile -ExecutionPolicy Bypass -File "${ROOT}/Unreal/lane.ps1" new ${m.key}; write Unreal/lanes/${m.key}.owned listing your files before writing code.
Notes from the lead: ${m.notes}
Build it, register your scenarios, take screenshots and LOOK at them, iterate until it looks right, not merely until it runs. Then sync your owned files to main (lane.ps1 sync ${m.key}) and confirm the main project still builds (Unreal/build.ps1; if main fails only because of someone else's file, say so and move on). Write docs/handoff/done-ue/client-${m.key}.built.txt (one line: date, key, build result, key screenshots) when done.`

const VERIFY_C = (m, rep) => `${HEAD}

YOUR ASSIGNMENT: independent adversarial verification of client module "${m.key}". You did not build it. Make a fresh lane from main (lane.ps1 new ${m.key}-verify), review the code against SPEC-UNREAL and the old spec sections, run its scenarios and your own harder ones (worst cases: the whole planet in view, night, close to the ground, extreme sim states, fast time), check performance numbers, and judge the screenshots like a demanding art director comparing against cinematic references. Fix what you can in the module's own files (then sync with lane.ps1 sync ${m.key}-verify after adding the files you changed to Unreal/lanes/${m.key}-verify.owned) and record every fix. The builder reported:
---
${rep || '(built in an earlier run; see docs/handoff/done-ue/client-' + m.key + '.built.txt)'}
---
Write docs/handoff/done-ue/client-${m.key}.txt (one line: date, key, summary, your 1-10 quality score) when it holds.`

phase('Client build')
await pipeline(
  C.filter((m) => !SKIP.has(m.key)),
  async (m) => {
    if (BUILT.has(m.key)) return { status: 'done', report: '' }
    const r = await run(BUILD_C(m), { ...m.build, label: 'build:' + m.key, phase: 'Client build' })
    results[m.key + '.build'] = r ? r.status : 'failed'
    return r
  },
  async (r, m) => {
    if (!ok(r)) { log(m.key + ': build not done'); return null }
    const v = await run(VERIFY_C(m, r.report), { ...m.verify, label: 'verify:' + m.key, phase: 'Client verify' })
    results[m.key] = v ? v.status : 'failed'
    return v
  },
)
return { results }
