# DEMIURGE security review (2026-11-20, night track "security")

Scope: the whole repository at `main` 1240bc0 (79 commits, 1,719 tracked files), its full git history, and the
packaged build in `dist/DEMIURGE-Win64-Development`. Read-only review: no code under `Unreal/Source` or
`Unreal/Config` was changed (wave 3 and the sim track own them). Every fix below is a recommendation for the
owning track. **Safe & small** means a change of a few lines that does not change gameplay or the save format
and can be checked with the existing tests.

Threat model. DEMIURGE is a single-player, offline game. There is no network code in `Game/` or `Sim/` (no
HTTP, sockets, process launches or URL opening: checked by grep). So the realistic attackers are:
(a) a **hostile file** the player is given (a "shared save", a settings file, a mod);
(b) a **hostile command line** (a doctored shortcut or launcher option);
(c) **a web page** open on the developer's machine while dev tools run;
(d) **anyone reading the public GitHub repository**, or opening a pull request against it.

## Summary

| # | Severity | Finding | Safe & small fix? |
|---|---|---|---|
| H1 | High | Helper AIs push with the owner's GitHub account; PR authorship can be faked; an agent reads PR text from a public repo | Partly (watcher filter: yes; account split: owner action) |
| M1 | Medium | A crafted save makes the game allocate gigabytes and crash (snapshot and list amplification, 1 GiB file cap) | **Yes** |
| M2 | Medium | The test harness and debug switches ship in every build: `-shot=` deletes and writes any path, plus file reads and dumps | **Yes** (one guard) |
| M3 | Medium | The packaged build is Development: console, 153 engine plugins, a 351 MB PDB, logs naming the PC and user, 11 minidumps in the archive | Yes (packaging settings) |
| M4 | Medium | Test scenarios overwrite the player's real save `slot1` and settings | **Yes** |
| M5 | Medium | The owner's personal email address is public in the metadata of 16 commits | Owner action (GitHub setting) |
| L1 | Low | Save loader accepts cell `-1` where a real cell is required; hostile counters can overflow (UB) | **Yes** |
| L2 | Low | Title screen reads and checksums every save in full (up to 1 GiB each); hint file read has no size cap | **Yes** |
| L3 | Low | Windows user name in 85 tracked files; a stray scratchpad folder committed at the repo root | **Yes** (rm + relative paths) |
| L4 | Low | `tools/server.js` sends `Access-Control-Allow-Origin: *` and accepts unauthenticated writes | **Yes** |
| L5 | Low | Template AndroidFileServer token in `DefaultEngine.ini`; editor-only plugins enabled for all targets | **Yes** |
| L6 | Low | Shipped font binaries came through a helper PR with no recorded hashes | Yes (record SHA-256) |
| I1–I4 | Info | CPU governor scope, Blender autoexec, PS 5.1 junction deletes, `-res=` into console | Yes |

What is already good (keep it): save slot names are generated (`slot%d`, `auto`) and validated against
`[A-Za-z0-9_-]{1,64}` before any path is built (`Sim/Save.cpp:606-622`), so path traversal through slot names
is impossible (and `DmBridgeCheck.cpp:511` tests `../escape`, `a/b`, `C:/x`). Writes are atomic (temp file then
rename, `Save.cpp:201-224`). Paths with NUL or invalid UTF-8 are refused (`Save.cpp:151-161`). The container
checks magic, version, flags, exact size and CRC before parsing (`Save.cpp:378-388`). `ByteReader` is
overflow-safe (`Core/Util.cpp:321-325`: `n > n_ - pos_`). Every list length is bounded by the bytes left
(`State.cpp:323-327`), enums and ids are range-checked, and cross-references are validated in `refsOk`
(`State.cpp:408-436`). `sim::use` refuses non-finite numbers (`Sim.cpp:560-612`). Settings parsing clamps every
value, ignores unknown keys and cannot be line-injected (`Save.cpp:433-463`). The world name is capped at 40
characters in the UI (`UI/Screens/DmScreenPages.cpp:182`) and sanitised with `FPaths::MakeValidFileName` for
the planet card (`DmScreenSubsystem.cpp:630`). No script elevates (no `RunAs`, no `Set-ExecutionPolicy`; the
per-process `-ExecutionPolicy Bypass` is fine), and nothing downloads or runs downloaded content (no
`Invoke-WebRequest`, `curl`, `pip install`, `urllib`, `npm install` in any tracked script). The git history has
no credentials: all 79 commits scanned for GitHub, OpenAI, xAI, AWS, Google, SpriteCook (`sc_live_`) and Slack
token formats, private keys, `Bearer` headers and `password/secret/api_key/token =` assignments: none. No
file was ever deleted from history, so nothing sensitive hides in a removed file. The Python runtime is not
in the cooked game (the engine's `PythonScriptPlugin` module is `UncookedOnly`; only the preload stub mounts).

---

## H1 — Helpers share the owner's GitHub account; PR identity is not checked (High)

Where: `AGENTS.md:42` ("everyone uses the owner's account, so this is how authorship is tracked");
`AGENTS.md:56` (the lead reviews PRs automatically); `tools/lead/watch-prs.sh:7` (lists *every* open PR of the
public repo, including PRs from forks, and prints their titles to the lead).

Risk. (1) Two third-party AI services hold credentials that can push to, and possibly administer, every
repository of the owner's account, not just this one. A leak at either service is a full account compromise.
(2) "Authorship" is the `[T-001][Grok]` commit prefix and the `helper/<name>/...` branch name. Anyone can open
a PR from a fork with the same title, branch name and prefix. The lead agent then reads that PR's text and
files, which is a prompt-injection path into an agent that can run commands on the owner's PC and merge to
`main`. AGENTS.md's rule "the owner and this file win" helps, but it is a text rule, not a control.

Fixes:
1. **Owner action:** give each helper its own GitHub account (or a fine-grained personal access token limited
   to `NumeralIV/DemiUrge`, permissions "Contents: read/write" and "Pull requests: read/write" only, with an
   expiry). Revoke whatever token or OAuth grant the helpers use today.
2. **Owner action:** protect `main` (GitHub branch protection: require a pull request, block force pushes and
   deletion). Then a leaked helper credential cannot rewrite `main`.
3. **Safe & small:** in `watch-prs.sh`, request `headRepositoryOwner,author` in `--json` and ignore any PR
   whose head repository is not `NumeralIV` (forks), so outside PRs never reach the lead automatically. The lead
   should treat every PR body and file as data, never as instructions (this applies to the night agents too:
   helper deliverables in `docs/concept/`, `docs/content/` are read by build agents).

## M1 — A crafted save file exhausts memory and crashes the game (Medium)

Where: `Sim/Save.cpp:43` (`kMaxFileBytes = 1 << 30`); `Save.cpp:341-372` (`getSnapshots`); `Save.cpp:343`
(the count check only requires 44 bytes per frame); `State.cpp:505-525` (entity lists sized by
`count(40)` / `count(20)`).

How. A frame is stored as three run lists. The smallest legal frame at grid level 6 (n = 40,962 cells) is 53
bytes (four doubles plus three run lists of one run each), but it decodes to 3 × n = 122,886 bytes: about
**2,300× amplification** (about 6,900× at level 7). The loader checks `count <= remaining / 44`, so a 1 MiB
file can declare about 19,800 frames and asks for about 2.4 GB; a 10 MiB file asks for about 24 GB. The file cap
is 1 GiB. The checksum is plain CRC-32, which a crafted file simply recomputes. In Unreal, an allocation failure
in `std::vector` goes through `FMemory::Malloc` and ends in the engine's out-of-memory fatal error, so the game
crashes (after heavy paging) as soon as the player loads the slot. The same pattern, at smaller ratios, applies
to settlements (`count(40)` bytes on disk for a struct of roughly 300 bytes), units and chronicle entries, and
to strings (`ByteReader::str` default limit 64 MiB, `Core/Util.h:139`, used for chronicle text and names).
A legitimate level-6, year-3000 save is 1.6 MB (`docs/handoff/done-ue/save.txt`); the sim keeps at most
`ChronicleTune::maxFrames = 200` history frames plus the genesis frames (`Sim/Chronicle.h:24`).

Fix (**safe & small**, no format change, all in `Sim/Save.cpp` / `Sim/State.cpp`):
- `kMaxFileBytes = size_t(64) << 20` (40× the largest real save).
- In `getSnapshots`, refuse `count > 1024` (`kMaxSnapshots`; > 200 + genesis frames with room to spare).
- In `load`, cap each list: settlements and cultures ≤ 65,536, units and disasters ≤ 262,144, chronicle ≤
  1,000,000 (or whatever the sim's own caps are), and pass a 4 KiB limit to `r.str()` for names and a 64 KiB
  limit for chronicle text.
- Add three tests to `SimTests/test_save_adv.cpp`: a container claiming 100,000 one-run frames, one claiming
  10 million settlements, and a 70 MB file; each must be refused quickly with a reason and allocate little.

## M2 — The scenario harness and debug switches are in every build, including Shipping (Medium)

Where: there is not a single `UE_BUILD_SHIPPING` guard in `Source/Demiurge` (grep). 93 scenarios register
unconditionally. `Game/Core/DmScenario.cpp:58-95` parses `-scenario= -shot= -fixture= -seed= -seq= -res=` and
more from the command line in any build.
- `-shot=<path>`: `DmScenario.cpp:65` makes it absolute; `Core/DmGameMode.cpp:187-191` (`QueueShot`) creates the
  folder, **deletes the file at that path** and then writes a PNG there. With `-seq=` it writes more files next to
  it (`DmGameMode.cpp:289`). So a doctored shortcut such as `Demiurge.exe -scenario=orbit -shot=C:/Users/<u>/Documents/thesis.docx`
  deletes and replaces any file the user can write.
- `-naturedumptex=<dir>` writes large BMPs to any folder (`Nature/DmNatureTextures.cpp:649`, `:642`).
- `-abfile=<file>` reads any file (`Core/DmPerfScenarios.cpp:270-273`); `-fixture=<path>` reads and parses any
  file (`Core/DmFixture.cpp:989-990`; the parser itself is bounds-checked, `DmFixture.cpp:39-65`).
- `-res=` is pasted into a console command, `GEngine->Exec(W, "r.SetRes %sw")` (`DmGameMode.cpp:102`), with no
  validation.
- Several scenarios write into the player's real save folder and settings (see M4).

A command line is a weaker trust boundary than a save file (whoever controls it can also pass engine options),
but deleting arbitrary files through a screenshot option is more than the engine itself allows, and none of
this is needed by players.

Fix (**safe & small**, owner: integration):
- In `FDmShotArgs::Get()` wrap everything after `bParsed = true;` in `#if !UE_BUILD_SHIPPING ... #endif`, so a
  Shipping build always runs scenario `play` with default arguments. In `ADmGameMode::StartPlay`, under
  `UE_BUILD_SHIPPING`, use `FDmScenarios::Find(TEXT("play"))` regardless of the name. (Do not compile the
  registry out: the real game *is* the `play` scenario.)
- In non-shipping builds too: refuse a `-shot=` that does not end in `.png`, and never `Delete` a file that the
  harness did not create in this run (overwrite is enough; `FScreenshotRequest` replaces the file).
- Validate `-res=` with `^\d{3,5}x\d{3,5}$` before the `Exec`, else keep `1920x1080`.

## M3 — The packaged build is a Development build, archived with debug files and local logs (Medium)

Where: `Unreal/Config/DefaultGame.ini:11` (`BuildConfiguration=PPBC_Development`); `Unreal/package.ps1:10`
(`-Config` defaults to `Development`) and `:23` (`-archive` straight into `dist/`).

What the archive at `dist/DEMIURGE-Win64-Development/Windows` contains today:
- `Demiurge/Binaries/Win64/Demiurge.pdb`, 351 MB, despite `IncludeDebugFiles=False` (`DefaultGame.ini:15`).
- `Demiurge/Saved/` from the owner's playtests (a Development build writes `Saved/` next to the exe):
  `Logs/Demiurge.log` records the **computer name and Windows user name** and full local paths;
  `Saved/Crashes/` holds **11 minidumps** (process memory) from engine ensures
  (`StaticMesh.cpp:5178 UVChannelData.bInitialized`, a towns/nature mesh issue for the owning track).
- A Development executable: the in-game console (`~`) is open to players (any console variable, `stat`,
  `exec`, `open`), and 153 engine plugins mount, among them `UdpMessaging`, `TcpMessaging`, `OnlineSubsystem*`,
  `StudioTelemetry`/`RuntimeTelemetry` (a telemetry session starts; it has no remote endpoint configured) and
  the Python preload stub. No listener was observed in the log, but each is attack surface a single-player game
  does not need.

Fixes (owner: integration / bridge):
- Any build given to someone else: `package.ps1 -Config Shipping` (console, cheats and the stat system are
  compiled out; `Saved/` moves to `%LOCALAPPDATA%\Demiurge\Saved`). **Safe & small:** default `-Config` to
  `Shipping` and keep Development for the lead's own playtests.
- Archive to a fresh, empty folder per run (`dist/DEMIURGE-Win64-<Config>-<yyyymmdd-hhmm>`), never into a folder
  that has been played from; when zipping, exclude `Saved/` and `*.pdb` (keep the PDB privately, matched to the
  build, for crash analysis).
- In `Demiurge.uproject`, disable plugins the game does not use (`"Enabled": false` for `UdpMessaging`,
  `TcpMessaging`, `OnlineSubsystem`, `OnlineSubsystemNull`, `StudioTelemetry`), after checking the editor
  scripts still run.

## M4 — Test scenarios overwrite the player's own save slot and settings (Medium, integrity)

Where: `UDmSimSubsystem::GetSaveDir()` (`Core/DmSimSubsystem.cpp:293-296`) is the only save folder; tests use
it: `UI/Screens/DmScreenScenarios.cpp:205, 256, 273, 455` save to **`slot1`** (the first slot the player sees),
`:446` and `:487-516` write `settings.dmsettings` (restored at the end, but not if the run is killed);
`Core/DmBridgeCheck.cpp:503` writes slot `bridgecheck`; `Core/DmCoreScenarios.cpp:408` writes `bridge-simtest`.

Every verification run in the main project (or on a player's machine through M2) silently replaces the
owner's real playtest save in slot 1 and can leave test settings behind (for example music volume 0.123 if a
`flow` run is stopped between `:494` and `:495`). Those extra slots then show up in the player's Load list.

Fix (**safe & small**): when the active scenario is not `play`, make `GetSaveDir()` return
`Saved/SaveGames-Test` and the settings path `Saved/SaveGames-Test/settings.dmsettings` (one `bTestSaves` flag
set by `ADmGameMode::StartPlay`; `DmScreenSubsystem.cpp:31` and `DmScreenScenarios.cpp:446` read it). The
hint and guide files (`UI/Hud/DmHudModel.cpp:217, 293`) should follow the same flag.

## M5 — The owner's personal email address is public in commit metadata (Medium, privacy)

Where: the author email of the 16 merge commits made through the GitHub web interface (PR merges #1 to #16,
e.g. `05f10f6`, `20efb47`) is the owner's personal Gmail address. The other 63 commits correctly use the
`users.noreply.github.com` address. The address appears in no file content (checked with `git grep` over every
revision), only in commit headers, which anyone can read with `git log` or the GitHub API.

Fixes (owner action, not code): in GitHub, Settings > Emails, turn on "Keep my email addresses private" and
"Block command line pushes that expose my email"; web merges will then use the noreply address. The 16
existing commits can only be cleaned by rewriting `main`'s history (and the address may already be cached by
mirrors); recommend accepting it and fixing forward. This review does not repeat the address.

## L1 — The save loader accepts "no cell" where a real cell is required; hostile counters overflow (Low)

Where: `Loader::cellRef` allows `-1` (`State.cpp:318-322`). It is used for `settlement.cell` (`:345`), every
entry of `unit.path` (`:381`), `unit.dest`, `disaster.cell` and every entry of `disaster.cells` (`:393`).
`refsOk` (`:408-436`) does not refuse `-1` for a living settlement or inside a path. The sim mostly guards these
with `w.valid(...)` (checked: `Economy.cpp:353`, `Units.cpp:61-73, 113-115, 193, 215`), so I found no crash, but
any one place that indexes `w.field[size_t(cell)]` without the guard (including client code in `Game/Towns`,
`Game/Planet`, `Game/UI`) reads far out of bounds on a crafted save. Separately, plain `int` counters taken from
the file can be `INT_MAX` and then overflow (undefined behaviour): `s.apart + 1` (`Society.cpp:482`),
`w.nextId.entry++` (`Chronicle.cpp:3669`), and likewise `nextId.unit`, `nextId.disaster`, `s.famine`.

Fix (**safe & small**, owner: sim track): in `refsOk`, refuse a living settlement whose `cell` is `-1`, any `-1`
inside `unit.path` or `disaster.cells`, and `nextId.*`, `apart`, `famine`, `weight` outside `0 .. 1e8`; also
require `nextId.unit` > every unit id (and the same for disasters and entries) so ids stay unique. Extend the
existing fuzzer (`SimTests/test_save.cpp:446-470`, `editedPayloads`): for every *accepted* mutated save, also run
`sim::use` and `sim::advance(w, 200)`, and run that test once under `/fsanitize=address` (MSVC supports it).

## L2 — Unbounded reads on the title screen and for the hint file (Low)

Where: `listSlots` calls `readMeta` for every `*.dmsave` (`Save.cpp:642-662`), and `readMeta` reads the whole
file and checksums it (`Save.cpp:596-601`), up to 1 GiB each, just to show a name and a year. Planting a few
large files in `SaveGames` makes the title screen stall. `FDmHudContext::LoadHints` reads `hints.v1.txt` with no
size limit (`UI/Hud/DmHudModel.cpp:224`), and `LoadGuide` likewise (`:299`).

Fix (**safe & small**): the M1 cap (64 MiB) already bounds the first; better, let `readMeta` read only the first
64 KiB and parse the header without the checksum (the checksum is checked again on load anyway). For the two
text files, check `IFileManager::Get().FileSize(...) <= 4096` before loading.

## L3 — Personal data and machine paths in tracked files (Low, privacy)

Where:
- A stray folder at the repository root, `SERS…ocaltemp/claude/…/scratchpad/dbg1.png` (commit `f4d72da`): an
  agent's Windows temp path with its backslashes stripped, committed by `git add -A`. Its name carries the
  Windows user name.
- The Windows user name inside absolute `C:/Users/<user>/...` paths in **85 tracked files**: scripts
  (`Unreal/build.ps1:4`, `Unreal/SimTests/run.ps1`, `Unreal/Tools/simrun/simrun.ps1`), the workflow scripts
  `docs/handoff/build-*.js`, `docs/FOUNDATION-CPP.md`, `docs/handoff/package.md`, `spec-critique-raw.json`,
  engine logs committed as `UnrealLab/{AssetLab,LifeLab,TreeLab}/final.txt` and `final2.txt`, and the source
  import paths baked into 64 `.uasset` files under `UnrealLab/` (cooking strips these from the game).
- The owner's time zone in `tools/flow_provenance.py:32`.
Combined with M5 this links a first name to the account. Low on its own.

Fix (**safe & small**): `git rm -r` the stray folder and add `/SERS*/` plus `UnrealLab/*/final*.txt` to
`.gitignore`; in scripts, replace absolute project paths with `$PSScriptRoot` (or `Split-Path`) so the user name
never needs to be written; in docs, write `<project root>` instead of the full path. Agents should stage files
by explicit path, not `git add -A`. History keeps the old copies; accept that.

## L4 — The legacy dev server allows any web page to read the project and write files (Low)

Where: `tools/server.js:39-43` (`Access-Control-Allow-Origin: *` on every response, including the static
files and directory listings), `:75-101` (`POST /__save` writes any body up to 256 MB, `:18`, into `shots/` with
no token), `:112-119` (serves dot-folders such as `/.git/`). It binds to `127.0.0.1` (`:146`), which is good,
but the wildcard CORS header lets any site open in the developer's browser `fetch` the whole project tree,
including untracked local files (`dist/`, logs, textures, `.git/`) and overwrite curated `shots/*.png`; with no
`Host` check, DNS rebinding works too. The tool belongs to the old JavaScript build, so it is rarely run now.

Fix (**safe & small**): remove the three CORS headers (the pages it serves are same-origin); answer 403 unless
the `Host` header is `127.0.0.1:<port>` or `localhost:<port>`; refuse any path segment starting with `.`;
require `?token=` matching a random value printed at start-up for `/__save`; lower `MAX_BODY` to 32 MB.

## L5 — Template secrets and editor plugins in project config (Low)

Where: `Unreal/Config/DefaultEngine.ini:49-60`, the `[AndroidFileServerEditor]` section with a committed
`SecurityToken=` (`:52`). It is the token the project template generates, used only by the Android file server
(no Android target; `bIncludeInShipping=False`), so it protects nothing here, but it is now public.
`Unreal/Demiurge.uproject:15-20` enables `PythonScriptPlugin` and `EditorScriptingUtilities` for every target.

Fix (**safe & small**): delete the AndroidFileServer section (or regenerate the token if Android is ever
added). Add `"TargetAllowList": [ "Editor" ]` to the two editor-only plugin entries in `Demiurge.uproject`
(and in the seven `UnrealLab/*/PlanetLab.uproject`), so the packaged game does not mount them.

## L6 — Shipped font binaries have no recorded hashes (Low, supply chain)

Where: the four OFL families in `assets/fonts/` arrived through a helper pull request (`234afbc`, T-009) and
are copied into the pak by `Unreal/Scripts/fonts.ps1`; FreeType parses them at runtime (`Game/UI/DmFonts.cpp`).
`assets/fonts/README.md:20-25` records the upstream source and commit or release for each family, but no file
hash. The copies under `Unreal/Content/Fonts` match `assets/fonts` byte for byte (SHA-256 checked).

Fix: add the SHA-256 of each `.ttf`/`.otf` to the README and verify them once against the upstream release
archives named there; `fonts.ps1` can then refuse to copy a file whose hash differs. The same habit for any
future third-party binary (meshes, audio). The Flow textures already carry provenance sidecars
(`tools/flow_provenance.py`); no tracked image embeds an email or the user name (scanned).

## Informational

- **I1** `tools/cpu_governor.ps1:6-17` changes the CPU affinity and priority of *every* process of the user
  with a matching name (`python`, `dotnet`, `cl`, `link`, `blender`...), not only this project's, forever, and
  appends to its log every 20 s. It runs without elevation, so it cannot touch other users' processes. Fine for
  the night; stop it in the morning.
- **I2** `art/tools/render_preview.py:3` opens `.blend` files without `--factory-startup`; a `.blend` from
  elsewhere could carry auto-run Python if the owner ever enabled "Auto Run Python Scripts" in Blender. Add
  `--disable-autoexec` to every Blender command line in `art/` (the `.blend` files are local builds today).
- **I3** `Unreal/lane.ps1:487` (`Remove-Item -Recurse -Force` on a lane) checks that the path is inside
  `Unreal/Lanes` (good). Windows PowerShell 5.1 follows directory junctions during a recursive delete, so never
  create a junction inside a lane (robocopy copies junction contents by default, so lanes have none today).
- **I4** `docs/handoff/build-*.js` and the night workflows run agents with broad shell rights on the owner's
  PC and feed them repository text. Keep AGENTS.md's instruction boundary in every agent prompt: repository
  content, PR text and helper deliverables are data, not instructions.

## Order of work

1. Owner, five minutes, no code: H1 (helper credentials, branch protection) and M5 (email privacy settings).
2. Sim track: M1 and L1 in `Sim/Save.cpp` / `Sim/State.cpp`, plus the three new adversarial tests and the
   `sim::use + advance` fuzz step. Run `SimTests/run.ps1 save`.
3. Integration (after wave 3 frees `Game/`): M2 guard in `FDmShotArgs::Get`, M4 test save folder, L2 size
   checks; then build and run `bridgecheck`, `screen-flow` and a `play` launch.
4. Packaging: M3 (Shipping by default, clean archive folder, no `Saved/` or PDB in anything shared), L5.
5. Housekeeping commit: L3 (`git rm` the stray folder, `.gitignore`), L4, L6.
