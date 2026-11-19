# DEMIURGE: packaged Windows build (C10 integration, 2026-11-19)

## Command

```
powershell -NoProfile -ExecutionPolicy Bypass -File "C:\Users\Relly\Documents\Fable5.2 Test\demiurge\Unreal\package.ps1"
```

which runs (Unreal 5.7, from the main project, after `Unreal/build.ps1` has built the editor target):

```
"C:\Program Files\Epic Games\UE_5.7\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun
  -project="C:\Users\Relly\Documents\Fable5.2 Test\demiurge\Unreal\Demiurge.uproject" -noP4 -platform=Win64
  -clientconfig=Development -build -cook -stage -pak -iostore -prereqs -archive
  -archivedirectory="C:\Users\Relly\Documents\Fable5.2 Test\demiurge\dist\DEMIURGE-Win64-Development"
  -unattended -utf8output -nocompileeditor -skipbuildeditor
```

Log of each run: `Unreal/Saved/Logs/package-<date>.log`. First full cook: 802 s; later runs are incremental
(18-160 s). Result: `BUILD SUCCESSFUL`, exit code 0.

## Output

`dist/DEMIURGE-Win64-Development/Windows/` (outside `Unreal/`, so lanes never copy it):

| Part | Size |
|---|---|
| Whole archive as packaged | 1,056,163,850 bytes (0.98 GB) |
| `Demiurge/Binaries/Win64/Demiurge.exe` (monolithic Development game) | 302.8 MB |
| `Demiurge.pdb` (debug symbols; not needed to play) | 348.5 MB |
| `Demiurge/Content/Paks` (IoStore `.ucas/.utoc` + `.pak`: every /Game asset, the OFL fonts as UFS files) | 261.4 MB |
| `Engine/` (runtime binaries, plugins, prerequisites) | 106 MB |

Without the PDB the game is about 0.71 GB.

Packaging settings live in `Unreal/Config/DefaultGame.ini` (`[/Script/UnrealEd.ProjectPackagingSettings]`): every
`/Game` directory is always cooked (all assets are loaded by path), plus the engine content loaded by path
(`/Engine/BasicShapes`, `/Engine/EngineDebugMaterials`, `/Engine/EngineSky/VolumetricClouds`); `Content/Fonts` is
staged as UFS (the SIL OFL 1.1 families with their `OFL.txt`, copied there by `Unreal/Scripts/fonts.ps1`); the start
map is `/Engine/Maps/Entry` with `ADmGameMode`. `DefaultEngine.ini` turns on the pipeline file cache
(`r.ShaderPipelineCache.*`): the player's first run records its PSOs to `Saved/Demiurge_PCD3D_SM6.upipelinecache`,
and later runs precompile them at start (run 1: 42 PSOs compiled on first use; run 2: 0).

## Launch check

```
dist\DEMIURGE-Win64-Development\Windows\Demiurge.exe -RenderOffscreen -windowed -ResX=1920 -ResY=1080 -nosplash
  -unattended -nosound -shot=<png> -shotui -warm=360
```

Exit code 0 in 30 s; log: `DmFlow: title world = seed 21, 240 years of history, in 765 ms`, the seven OFL fonts
loaded (`DmFonts: EBGaramond-Regular ... Cinzel-Regular`), `DmFlow: boot -> title`, 81 fps on the title. Picture:
`shots/ue-package-title.png` (the title over the turning planet, Cinzel logo, EB Garamond / Inter menu). With no
arguments the game starts the same way, windowed or full screen. The harness scenarios are compiled in, so the
packaged game also runs `-scenario=playthrough`, `-scenario=perf-play` and the rest.
