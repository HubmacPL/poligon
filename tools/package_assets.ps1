# Packs the assets of forest_scene and model_viewer for a release:
#   build\release_package\poligon-VERSION-assets\poligon-assets\cooked\assets.pak
#   build\release_package\poligon-VERSION-assets\poligon-assets\ATTRIBUTION.md, README.txt
# and, with -Zip, build\release_package\poligon-VERSION-assets.zip of that folder.
# The assets come from tools\forest_scene\assets.txt without the ones named in -Exclude
# (those whose license does not allow them to be given away), cooked fresh from
# ..\poligon-assets\source together with the land of tools\forest_scene\forest.level, and
# packed into one archive. Needs the release build (tools\package.ps1 makes it).
# Usage:  powershell -ExecutionPolicy Bypass -File tools\package_assets.ps1 [-Version 0.1.0] [-Zip]
param(
    [string]$Version = '0.1.0',
    [string[]]$Exclude = @('animals/fox'),
    [switch]$Zip
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$sources = Join-Path (Split-Path -Parent $root) 'poligon-assets\source'
$cooker = Join-Path $root 'build\release\bin\asset_cooker.exe'
$out = Join-Path $root 'build\release_package'
$stage = Join-Path $out "poligon-$Version-assets"
$cooked = Join-Path $out 'assets_cooked'
$list = Join-Path $out 'assets_release.txt'

if (-not (Test-Path $cooker)) { throw "$cooker not found; run tools\package.ps1 first." }
if (-not (Test-Path $sources)) { throw "$sources not found." }

# The asset list without the excluded assets: an asset is its "model NAME" or "texture NAME"
# line and the lines after it, up to the next such line.
$kept = @()
$skip = $false
foreach ($line in Get-Content (Join-Path $root 'tools\forest_scene\assets.txt')) {
    if ($line -match '^(model|texture)\s+(\S+)') {
        $skip = $Exclude -contains $Matches[2]
        if ($skip) { Write-Host "left out: $($Matches[2])" }
    }
    if (-not $skip) { $kept += $line }
}
New-Item -ItemType Directory -Force $out | Out-Null
$kept | Out-File -Encoding ascii $list

# Cooked fresh, so that nothing left from other lists gets into the archive.
if (Test-Path $cooked) { Remove-Item -Recurse -Force $cooked }
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
$target = Join-Path $stage 'poligon-assets'
New-Item -ItemType Directory -Force (Join-Path $target 'cooked') | Out-Null
& $cooker $list $sources $cooked --level (Join-Path $root 'tools\forest_scene\forest.level') `
    --terrain-cache (Join-Path $root 'build\terrain_cache') --pack (Join-Path $target 'cooked\assets.pak')
if ($LASTEXITCODE -ne 0) { throw "asset_cooker failed (exit code $LASTEXITCODE)." }

Copy-Item (Join-Path $PSScriptRoot 'assets\ATTRIBUTION.md') $target
@"
Poligon $Version assets
=======================

The models, textures and land that forest_scene and model_viewer of Poligon $Version load,
cooked by asset_cooker into one archive (cooked\assets.pak; names hashed, not encrypted).

Unpack this archive so that the folder poligon-assets lies next to the folder of the
programs (poligon-$Version-windows-x64) or inside it, then start forest_scene.exe.

Authors, sources and licenses of every asset: ATTRIBUTION.md. Some assets are under
CC-BY 4.0: when you give them away, keep ATTRIBUTION.md with them.
"@ | Out-File -Encoding utf8 (Join-Path $target 'README.txt')

Get-ChildItem $target -Recurse -File | ForEach-Object {
    '{0,10:N1} MB  {1}' -f ($_.Length / 1MB), $_.FullName.Substring($stage.Length + 1)
}

if ($Zip) {
    $archive = Join-Path $out "poligon-$Version-assets.zip"
    if (Test-Path $archive) { Remove-Item -Force $archive }
    Compress-Archive -Path $target -DestinationPath $archive -CompressionLevel Optimal
    '{0}: {1:N1} MB' -f $archive, ((Get-Item $archive).Length / 1MB)
}
