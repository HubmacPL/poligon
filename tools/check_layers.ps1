# Checks that every engine library includes only its own headers and those of the libraries
# below it (see engine/CMakeLists.txt). A wrong include would compile, because all headers
# share one include folder, so this is checked by reading the files.
# Usage:  powershell -ExecutionPolicy Bypass -File tools\check_layers.ps1
# Exit code 0 means every include is allowed.

$ErrorActionPreference = 'Stop'
$engine = Join-Path (Split-Path -Parent $PSScriptRoot) 'engine'

# Area -> the areas its files may include (itself included).
$allowed = @{
    'core'      = @('core')
    'ecs'       = @('core', 'ecs')
    'render'    = @('core', 'render')
    'animation' = @('core', 'animation')
    'physics'   = @('core', 'physics')
    'assets'    = @('core', 'render', 'animation', 'assets')
    'world'     = @('core', 'render', 'animation', 'assets', 'physics', 'world')
    'debug'     = @('core', 'render', 'ecs', 'debug')
}

# Tool-side libraries (what a released game can leave out) and their headers: only they may
# include these headers, so that the game-side libraries never need them.
$toolHeaders = @('poligon/animation/animation_build.h', 'poligon/assets/model_import.h', 'poligon/assets/cooking.h')
$toolFolders = @('src\animation\build\', 'src\assets\cook\')

$failures = @()
foreach ($file in Get-ChildItem -Path $engine -Recurse -File -Include *.h, *.cpp) {
    $relative = $file.FullName.Substring($engine.Length + 1)
    $isToolSide = ($toolFolders | Where-Object { $relative.StartsWith($_) }).Count -gt 0
    $isToolHeader = ($toolHeaders | Where-Object { $relative -eq ('include\' + $_.Replace('/', '\')) }).Count -gt 0
    if ($isToolSide -or $isToolHeader) {
        continue
    }
    foreach ($header in $toolHeaders) {
        $match = Select-String -Path $file.FullName -SimpleMatch -Pattern ('#include "' + $header + '"')
        if ($match) {
            $failures += "$($file.FullName):$($match[0].LineNumber): a game-side library may not include the tool-side $header"
        }
    }
}
foreach ($root in @('include\poligon', 'src')) {
    $base = Join-Path $engine $root
    foreach ($file in Get-ChildItem -Path $base -Recurse -File -Include *.h, *.cpp) {
        $area = $file.FullName.Substring($base.Length + 1).Split('\')[0]
        if (-not $allowed.ContainsKey($area)) {
            $failures += "$($file.FullName): not in any engine area"
            continue
        }
        foreach ($match in Select-String -Path $file.FullName -Pattern '#include "poligon/([a-z_]+)/' -AllMatches) {
            foreach ($found in $match.Matches) {
                $included = $found.Groups[1].Value
                if ($allowed[$area] -notcontains $included) {
                    $failures += "$($file.FullName):$($match.LineNumber): $area may not include poligon/$included"
                }
            }
        }
    }
}

if ($failures.Count -gt 0) {
    $failures | ForEach-Object { Write-Host $_ }
    Write-Host "LAYERS FAILED: $($failures.Count) include(s) reach above their library"
    exit 1
}
Write-Host "Layers OK"
exit 0
