# DEMIURGE integration (C10): bring the shipped OFL fonts into the game.
#   powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/fonts.ps1
# Copies the four SIL OFL 1.1 families of assets/fonts (Cinzel, Cormorant Garamond, EB Garamond, Inter: the
# font files the UI uses, each family's OFL.txt and the README with sources) into Content/Fonts of the project
# this script belongs to (main or a lane). Config/DefaultGame.ini stages Content/Fonts as UFS, so the packaged
# game carries them inside its pak; Game/UI/DmFonts.cpp loads them at runtime (FCompositeFont, lazy load).
# Why not UFontFace assets: in UE 5.7 the font importer calls FSlateApplication::Get() from
# UFontFace::PostEditChangeProperty, which asserts in a headless editor (no Slate application), so fonts
# cannot be imported as assets by an unattended script. Re-runnable: replaces Content/Fonts.
$ErrorActionPreference = 'Stop'
$proj = Split-Path -Parent $PSScriptRoot
$root = $proj
for ($i = 0; $i -lt 6; $i++) { if (Test-Path -LiteralPath (Join-Path $root 'assets\fonts')) { break }; $root = Split-Path -Parent $root }
$src = Join-Path $root 'assets\fonts'
if (-not (Test-Path -LiteralPath $src)) { Write-Host "fonts: assets/fonts not found above $proj"; exit 1 }
$dst = Join-Path $proj 'Content\Fonts'
if (Test-Path -LiteralPath $dst) { Remove-Item -LiteralPath $dst -Recurse -Force }
[void](New-Item -ItemType Directory -Force -Path $dst)
$files = @(
	'eb-garamond\EBGaramond-Regular.ttf', 'eb-garamond\EBGaramond-Medium.ttf', 'eb-garamond\EBGaramond-Italic.ttf', 'eb-garamond\OFL.txt',
	'cormorant-garamond\CormorantGaramond-SemiBold.otf', 'cormorant-garamond\CormorantGaramond-Medium.otf', 'cormorant-garamond\CormorantGaramond-Italic.otf', 'cormorant-garamond\OFL.txt',
	'cinzel\Cinzel-Regular.ttf', 'cinzel\OFL.txt',
	'inter\Inter-Regular.ttf', 'inter\Inter-SemiBold.ttf', 'inter\Inter-Bold.ttf', 'inter\OFL.txt',
	'README.md')
$n = 0; $bytes = 0
foreach ($f in $files) {
	$s = Join-Path $src $f
	if (-not (Test-Path -LiteralPath $s)) { Write-Host "fonts: MISSING $f"; exit 1 }
	$d = Join-Path $dst $f
	[void](New-Item -ItemType Directory -Force -Path (Split-Path -Parent $d))
	Copy-Item -LiteralPath $s -Destination $d -Force
	$n++; $bytes += (Get-Item -LiteralPath $d).Length
}
"fonts: $n files ($([Math]::Round($bytes / 1MB, 2)) MB) -> $dst"
