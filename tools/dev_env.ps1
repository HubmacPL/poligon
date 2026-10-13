# Loads the Visual Studio 2022 x64 build environment (cl, cmake, ninja) into the current
# PowerShell process. Changes nothing outside this process.
$ErrorActionPreference = 'Stop'

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) {
    throw 'vswhere.exe not found. Install Visual Studio 2022 or Build Tools 2022.'
}

$installPath = & $vswhere -latest -products * -version '[17.0,18.0)' `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $installPath) {
    throw 'Visual Studio 2022 with the C++ x64 toolset was not found.'
}

$vcvars = Join-Path $installPath 'VC\Auxiliary\Build\vcvars64.bat'
$lines = cmd /c "`"$vcvars`" >nul 2>&1 && set"
if ($LASTEXITCODE -ne 0) {
    throw "vcvars64.bat failed with exit code $LASTEXITCODE."
}

# Always use the vcpkg copy bundled with Visual Studio, even if another one is configured globally.
$bundledVcpkg = Join-Path $installPath 'VC\vcpkg'
if (-not (Test-Path (Join-Path $bundledVcpkg 'vcpkg.exe'))) {
    throw 'The vcpkg component of Visual Studio 2022 is not installed.'
}
$env:VCPKG_ROOT = $bundledVcpkg

# Ask for English compiler and linker messages. Takes effect only when the English
# language pack of Visual Studio is installed; otherwise the installed language is used.
$env:VSLANG = '1033'

foreach ($line in $lines) {
    $separator = $line.IndexOf('=')
    if ($separator -gt 0) {
        Set-Item -Path ("Env:" + $line.Substring(0, $separator)) -Value $line.Substring($separator + 1)
    }
}
