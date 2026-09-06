#requires -Version 5.1
<#
    UnityRuntimeExplorer build driver. Wraps the CMakePresets.json presets
    (clang-debug, clang-release, msvc-debug, msvc-release) and takes care of
    locating a Visual Studio toolchain so cl.exe / link.exe / the Windows SDK
    are on PATH even when this isn't run from a Developer Prompt.
#>
[CmdletBinding()]
param(
    [ValidateSet('msvc', 'clang')]
    [string]$Compiler = 'clang',

    [ValidateSet('Debug', 'Release')]
    [string]$Config = 'Release',

    [switch]$Clean,

    [string]$Target,

    [switch]$Tests,

    [string]$DeployDir
)

$ErrorActionPreference = 'Stop'
$RepoRoot = $PSScriptRoot

function Fail($Message) {
    Write-Host "ERROR: $Message" -ForegroundColor Red
    exit 1
}

function Find-Tool($Name, $InstallHint) {
    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if (-not $cmd) {
        Fail "'$Name' not found on PATH. $InstallHint"
    }
    return $cmd.Source
}

Write-Host "== UnityRuntimeExplorer build ($Compiler, $Config) ==" -ForegroundColor Cyan

Find-Tool 'cmake' "Install it with: winget install Kitware.CMake  (then restart your shell)" | Out-Null
Find-Tool 'ninja' "Install it with: winget install Ninja-build.Ninja  (then restart your shell)" | Out-Null

# --- Locate a Visual Studio install and import its dev environment. ---------
# Needed even for clang builds: on Windows, clang++ still links via link.exe
# and needs the Windows SDK / UCRT paths that only vcvars sets up.
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) {
    Fail "Visual Studio Build Tools not found (vswhere.exe missing). Install 'Visual Studio Build Tools' with the 'Desktop development with C++' workload: https://visualstudio.microsoft.com/downloads/"
}

$vsInstallPath = & $vswhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath
if (-not $vsInstallPath) {
    Fail "No Visual Studio install with the C++ build tools (Microsoft.VisualStudio.Component.VC.Tools.x86.x64) was found. Install the 'Desktop development with C++' workload."
}

$vcvars = Join-Path $vsInstallPath 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path $vcvars)) {
    Fail "vcvars64.bat not found under '$vsInstallPath'."
}

Write-Host "Importing VC environment from: $vcvars"
$envDump = & cmd /c "`"$vcvars`" >nul 2>&1 && set"
if ($LASTEXITCODE -ne 0 -or -not $envDump) {
    Fail "vcvars64.bat failed to run. Your Visual Studio install may be broken; try repairing it from the Visual Studio Installer."
}
foreach ($line in $envDump) {
    $idx = $line.IndexOf('=')
    if ($idx -gt 0) {
        $name = $line.Substring(0, $idx)
        $value = $line.Substring($idx + 1)
        Set-Item -Path "Env:$name" -Value $value
    }
}

if ($Compiler -eq 'clang') {
    Find-Tool 'clang++' "Install LLVM with: winget install LLVM.LLVM  (then restart your shell)" | Out-Null
} else {
    Find-Tool 'cl' "cl.exe should have been on PATH after importing vcvars64.bat; the VS install may be missing the MSVC v143 toolset component." | Out-Null
}

$presetName = "$Compiler-$($Config.ToLower())"
$buildDir = Join-Path $RepoRoot "out\build\$presetName"

if ($Clean -and (Test-Path $buildDir)) {
    Write-Host "Cleaning $buildDir"
    Remove-Item -Recurse -Force $buildDir
}

Push-Location $RepoRoot
try {
    $configureArgs = @('--preset', $presetName)
    $configureArgs += @('-DURK_BUILD_TESTS=' + $(if ($Tests) { 'ON' } else { 'OFF' }))
    if ($DeployDir) {
        $configureArgs += @('-DURK_DEPLOY_DIR=' + $DeployDir)
    }

    Write-Host "-- Configuring ($presetName) --" -ForegroundColor Cyan
    & cmake @configureArgs
    if ($LASTEXITCODE -ne 0) { Fail "CMake configure failed (exit $LASTEXITCODE)." }

    $buildArgs = @('--build', '--preset', $presetName, '--parallel')
    if ($Target) {
        $buildArgs += @('--target', $Target)
    }

    Write-Host "-- Building ($presetName) --" -ForegroundColor Cyan
    & cmake @buildArgs
    if ($LASTEXITCODE -ne 0) { Fail "Build failed (exit $LASTEXITCODE)." }
}
finally {
    Pop-Location
}

Write-Host ""
Write-Host "== Build succeeded ==" -ForegroundColor Green
Write-Host "Output directory: $buildDir"

$outputs = Get-ChildItem -Path $buildDir -Include *.dll, *.exe -Recurse -ErrorAction SilentlyContinue |
    Where-Object { $_.DirectoryName -eq $buildDir }

$sumsFile = Join-Path $buildDir 'SHA256SUMS.txt'
$hashLines = $outputs | ForEach-Object {
    $hash = (Get-FileHash -Path $_.FullName -Algorithm SHA256).Hash.ToLower()
    "$hash  $($_.Name)"
}
$hashLines | Set-Content -Path $sumsFile -Encoding ascii

foreach ($o in $outputs) { Write-Host "  $($o.FullName)" }
Write-Host ""
Write-Host "SHA256:"
$hashLines | ForEach-Object { Write-Host "  $_" }
Write-Host "Checksums written to: $sumsFile"
