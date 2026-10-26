# Builds the release preset and packs the programs for a release:
#   build\release_package\poligon-VERSION-windows-x64.zip
# with forest_scene, toy, model_viewer, asset_cooker, the examples, the DLLs they need,
# forest_scene's asset list and level, LICENSE, THIRD_PARTY_LICENSES.md, the license texts of
# the libraries (licenses\) and README.txt. The assets are packed separately
# (tools\package_assets.ps1).
# Usage:  powershell -ExecutionPolicy Bypass -File tools\package.ps1 [-Version 0.1.0]
param(
    [string]$Version = '0.1.0'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

. (Join-Path $PSScriptRoot 'dev_env.ps1')

function Invoke-Step([string]$title, [scriptblock]$command) {
    Write-Host "== $title"
    & $command
    if ($LASTEXITCODE -ne 0) {
        Write-Host "FAILED: $title (exit code $LASTEXITCODE)"
        exit 1
    }
}

# The license text in the header comment of one of our files that holds code of others:
# the lines indented under "under this license:" (or the file's own LICENSE).
function Get-HeaderLicense([string]$path) {
    $lines = Get-Content $path
    $start = ($lines | Select-String -SimpleMatch 'under this license:' | Select-Object -First 1).LineNumber
    $text = @()
    for ($i = $start; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -notmatch '^//') { break }
        if ($lines[$i] -match '^//\s*$') { $text += ''; continue }
        if ($lines[$i] -notmatch '^//   ') { break }
        $text += $lines[$i].Substring(5)
    }
    return ($text -join "`r`n").Trim()
}

Push-Location $root
try {
    Invoke-Step 'release configure' { cmake --preset release }
    Invoke-Step 'release build' { cmake --build --preset release }

    $bin = Join-Path $root 'build\release\bin'
    $out = Join-Path $root 'build\release_package'
    $name = "poligon-$Version-windows-x64"
    $stage = Join-Path $out $name
    if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
    New-Item -ItemType Directory -Force $stage | Out-Null

    # Programs: the tools named here and every example; no tests. No .pdb files (crash
    # reports then show addresses without function names).
    $programs = @('forest_scene', 'toy', 'model_viewer', 'asset_cooker') |
        ForEach-Object { Join-Path $bin "$_.exe" }
    $programs += (Get-ChildItem $bin -Filter 'example_*.exe').FullName
    foreach ($program in $programs) {
        Copy-Item $program $stage
    }
    Copy-Item (Join-Path $bin '*.dll') $stage
    Copy-Item 'tools\forest_scene\assets.txt', 'tools\forest_scene\forest.level' $stage
    Copy-Item 'LICENSE', 'THIRD_PARTY_LICENSES.md' $stage

    # License texts: every library vcpkg installed for the programs (not the build tools),
    # and the code of others built into the shaders.
    $licenses = Join-Path $stage 'licenses'
    New-Item -ItemType Directory -Force $licenses | Out-Null
    $notShipped = @('directx-dxc', 'vcpkg-cmake', 'vcpkg-cmake-config', 'vcpkg-cmake-get-vars')
    foreach ($package in Get-ChildItem 'vcpkg_installed\x64-windows\share' -Directory) {
        $copyright = Join-Path $package.FullName 'copyright'
        if ($notShipped -notcontains $package.Name -and (Test-Path $copyright)) {
            Copy-Item $copyright (Join-Path $licenses "$($package.Name).txt")
        }
    }
    $shaders = 'engine\src\render\shaders'
    Copy-Item "$shaders\third_party\xegtao\LICENSE" (Join-Path $licenses 'xegtao.txt')
    Get-HeaderLicense "$shaders\common\atmosphere.hlsli" | Out-File -Encoding utf8 (Join-Path $licenses 'unreal-engine-sky-atmosphere.txt')
    Get-HeaderLicense "$shaders\post\agx.hlsli" | Out-File -Encoding utf8 (Join-Path $licenses 'agx-minimal.txt')

    @"
Poligon $Version for Windows x64
================================

A C++ game engine for open-world games, built for work with an AI agent.
Source code, documentation and building: see the project's repository (README.md).

Requirements
- Windows 10 or 11, x64, and a graphics card with Direct3D 12 or Vulkan 1.3.
- The Microsoft Visual C++ Redistributable for Visual Studio 2015-2022 (x64), which most
  Windows computers already have. If a program says that MSVCP140.dll or VCRUNTIME140.dll
  is missing, install it from https://aka.ms/vs/17/release/vc_redist.x64.exe

Programs
- toy.exe            the toy game, a test bed for the engine (needs no assets).
- example_*.exe      one small example per engine feature (need no assets). Most write a
                     picture or a file when given a path; see the source in examples\.
- forest_scene.exe   the showcase: a forest above a lake. Needs the asset archive
                     poligon-$Version-assets.zip unpacked so that the folder poligon-assets
                     lies next to this folder or inside it.
                     Keys: W/S/A/D walk, Shift runs, Space jumps, hold the left mouse button
                     to look around, F flies, Tab hides the debug windows, F11 fullscreen.
                     --quality low|medium|high|ultra chooses the graphics preset.
- model_viewer.exe   shows the models of the asset archive (N/P next and previous), or a
                     model file dropped on it.
- asset_cooker.exe   cooks models and textures listed in an asset list into the engine's
                     files and archives (for your own assets).

Common options: --headless --ticks N --dump-frame FILE.png --backend d3d12|vulkan.

License: MIT (LICENSE). Libraries and code of others in these programs: THIRD_PARTY_LICENSES.md,
with the license texts in licenses\.
"@ | Out-File -Encoding utf8 (Join-Path $stage 'README.txt')

    $zip = Join-Path $out "$name.zip"
    if (Test-Path $zip) { Remove-Item -Force $zip }
    Compress-Archive -Path $stage -DestinationPath $zip -CompressionLevel Optimal
    '{0}: {1:N1} MB' -f $zip, ((Get-Item $zip).Length / 1MB)
}
finally {
    Pop-Location
}
