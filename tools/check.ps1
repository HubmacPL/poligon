# Full verification run: configure, build and test every preset.
# Usage:  powershell -ExecutionPolicy Bypass -File tools\check.ps1 [-Presets dev,asan,analyze]
# Exit code 0 means everything passed.
param(
    [string[]]$Presets = @('dev', 'asan', 'analyze')
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

. (Join-Path $PSScriptRoot 'dev_env.ps1')

# Presets that only compile (static analysis has nothing to run).
$buildOnly = @('analyze')

function Invoke-Step([string]$title, [scriptblock]$command) {
    Write-Host "== $title"
    & $command
    if ($LASTEXITCODE -ne 0) {
        Write-Host "FAILED: $title (exit code $LASTEXITCODE)"
        exit 1
    }
}

Push-Location $root
try {
    Invoke-Step "engine layers" { powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'check_layers.ps1') }
    foreach ($preset in $Presets) {
        Invoke-Step "$preset configure" { cmake --preset $preset }
        Invoke-Step "$preset build" { cmake --build --preset $preset }
        if ($buildOnly -notcontains $preset) {
            Invoke-Step "$preset test" { ctest --preset $preset }
        }
    }
}
finally {
    Pop-Location
}

Write-Host "CHECK PASSED: $($Presets -join ', ')"
exit 0
