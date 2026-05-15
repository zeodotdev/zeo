[CmdletBinding()]
param(
    # Root of the zeo monorepo (the dir containing code\zeo and code\zeo-python).
    # Auto-detected by walking up from this script if not specified — handles both
    # the dev layout (kicad-win-builder at repo root) and the prod layout (under packaging\).
    [string]$ZeoRoot,

    # CMake build directory (output of configure). Defaults to .build\... under this script.
    [string]$BuildDir,

    # CMake install prefix (where the bundled Zeo install gets staged). Defaults to .out\... under this script.
    [string]$InstallDir,

    # Visual Studio install path (provides MSVC dev shell + bundled CMake).
    # Auto-detected via vswhere if not specified — works for both full IDE and
    # Build Tools installs.
    [string]$VsPath
)

if (-not $VsPath) {
    $vswhere = "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        # -products * is required to include BuildTools (filtered out by default)
        $VsPath = (& $vswhere -latest -products * -property installationPath | Select-Object -First 1)
    }
    if (-not $VsPath -or -not (Test-Path $VsPath)) {
        Write-Error "Could not auto-detect Visual Studio install. Pass -VsPath explicitly."
        exit 1
    }
    Write-Host "Using VS install: $VsPath" -ForegroundColor Gray
}

# NOTE: Defaults that depend on $PSScriptRoot must be resolved after the param
# block — Windows PowerShell 5.1 does not expose $PSScriptRoot inside param() defaults.
if (-not $BuildDir)   { $BuildDir   = Join-Path $PSScriptRoot ".build\kicad\build\x64-windows-Release" }
if (-not $InstallDir) { $InstallDir = Join-Path $PSScriptRoot ".out\x64-windows-Release" }

if (-not $ZeoRoot) {
    $candidate = $PSScriptRoot
    while ($candidate -and -not (Test-Path (Join-Path $candidate "code\zeo"))) {
        $parent = Split-Path -Parent $candidate
        if ($parent -eq $candidate) { break }
        $candidate = $parent
    }
    if (-not $candidate -or -not (Test-Path (Join-Path $candidate "code\zeo"))) {
        Write-Error "Could not auto-detect Zeo repo root (no code\zeo found above $PSScriptRoot). Pass -ZeoRoot explicitly."
        exit 1
    }
    $ZeoRoot = $candidate
}

$cmakeExe = Join-Path $VsPath "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

# Set up MSVC environment.
# Enter-VsDevShell only ships with the full VS IDE; VS Build Tools does not include it.
# VsDevCmd.bat is present in both, so we shell out to it and import the resulting env.
$vsDevCmd = Join-Path $VsPath "Common7\Tools\VsDevCmd.bat"
if (-not (Test-Path $vsDevCmd)) {
    Write-Error "VsDevCmd.bat not found at $vsDevCmd"
    exit 1
}
if (-not $env:VSCMD_VER) {
    $envDump = & cmd.exe /c "`"$vsDevCmd`" -arch=amd64 -host_arch=amd64 -no_logo && set"
    foreach ($line in $envDump) {
        if ($line -match '^([^=]+)=(.*)$') {
            Set-Item -Path "env:$($Matches[1])" -Value $Matches[2]
        }
    }
}

# Use the vcpkg-bundled python.exe explicitly. Bare `python` on Windows resolves
# to the Microsoft Store stub when no real Python is on PATH, which silently
# breaks pip-install and proto-generation steps below (Out-Null swallows the
# stub's "Python was not found" message). The vcpkg port ships Python 3.11
# at this path during build.
$pythonBin = Join-Path $BuildDir "vcpkg_installed\x64-windows\tools\python3\python.exe"
if (-not (Test-Path $pythonBin)) {
    Write-Error "Bundled python.exe not found at $pythonBin. Run a full vcpkg-aware build first (build.ps1)."
    exit 1
}

Write-Host "Building kiplatform target..." -ForegroundColor Cyan
& $cmakeExe --build $BuildDir --config Release --target kiplatform -- -j8
if ($LASTEXITCODE -ne 0) {
    Write-Host "kiplatform build failed!" -ForegroundColor Red
    exit 1
}

Write-Host "Building kicad target..." -ForegroundColor Cyan
& $cmakeExe --build $BuildDir --config Release --target kicad -- -j8
if ($LASTEXITCODE -ne 0) {
    Write-Host "kicad build failed!" -ForegroundColor Red
    exit 1
}

# pcbnew / eeschema link against kicommon. Whenever common-side ABI changes
# (vtable layout, struct size, KICOMMON_API annotations), stale kiface DLLs
# cause cross-DLL heap corruption. Always rebuild them alongside kicad.
# kicad_3dsg loads inside the pcbnew process (3D viewer) and shares heap
# allocations with kicommon-aware code, so include it too.
Write-Host "Building pcbnew + eeschema + kicad_3dsg targets..." -ForegroundColor Cyan
& $cmakeExe --build $BuildDir --config Release --target pcbnew pcbnew_kiface eeschema eeschema_kiface kicad_3dsg -- -j8
if ($LASTEXITCODE -ne 0) {
    Write-Host "pcbnew/eeschema/3dsg build failed!" -ForegroundColor Red
    exit 1
}

Write-Host "Building terminal target..." -ForegroundColor Cyan
& $cmakeExe --build $BuildDir --config Release --target terminal_kiface -- -j8
if ($LASTEXITCODE -ne 0) {
    Write-Host "terminal build failed!" -ForegroundColor Red
    exit 1
}

Write-Host "Building agent target..." -ForegroundColor Cyan
& $cmakeExe --build $BuildDir --config Release --target agent -- -j8
if ($LASTEXITCODE -ne 0) {
    Write-Host "agent build failed!" -ForegroundColor Red
    exit 1
}

Write-Host "Building zeo-mcp target..." -ForegroundColor Cyan
& $cmakeExe --build $BuildDir --config Release --target zeo-mcp -- -j8
if ($LASTEXITCODE -ne 0) {
    Write-Host "zeo-mcp build failed!" -ForegroundColor Red
    exit 1
}

Write-Host "Installing..." -ForegroundColor Cyan
& $cmakeExe --install $BuildDir --prefix $InstallDir

# Install kipy (KiCad Python bindings) and dependencies into embedded Python
$sitePackages   = Join-Path $InstallDir   "bin\Lib\site-packages"
$kicadPythonDir = Join-Path $ZeoRoot      "code\zeo-python"
$kipySource     = Join-Path $kicadPythonDir "kipy"
$protoInput     = Join-Path $ZeoRoot      "code\zeo\api\proto"
$protoc         = Join-Path $PSScriptRoot "vcpkg\packages\protobuf_x64-windows\tools\protobuf\protoc.exe"

# Generate protobuf Python files (always regenerate to pick up proto changes)
Write-Host "Generating kipy protobuf files..." -ForegroundColor Cyan
Push-Location $kicadPythonDir
& $pythonBin tools\generate_protos.py --protoc $protoc --input $protoInput --output "$kipySource\proto"
$protoExit = $LASTEXITCODE
Pop-Location
if ($protoExit -ne 0) {
    Write-Host "Proto generation failed!" -ForegroundColor Red
    exit 1
}
Write-Host "  Generated protobuf Python files" -ForegroundColor Gray

# Generate kipy/kicad_api_version.py from `git describe`. This is what
# code/zeo-python/build.py:36-39 does during a wheel build; rebuild_agent.ps1
# bypasses build.py entirely, so we replicate the step inline. Without this,
# `from kipy.kicad_api_version import KICAD_API_VERSION` fails at runtime.
Write-Host "Generating kipy/kicad_api_version.py..." -ForegroundColor Cyan
$zeoSrcDir = Join-Path $ZeoRoot "code\zeo"
$gitDescribe = (& git -C $zeoSrcDir describe --long 2>$null)
if ([string]::IsNullOrWhiteSpace($gitDescribe)) {
    $shortSha = (& git -C $zeoSrcDir rev-parse --short HEAD).Trim()
    $gitDescribe = "0.0.0-0-g$shortSha"
}
$gitDescribe = $gitDescribe.Trim()
$apiVersionFile = Join-Path $kipySource "kicad_api_version.py"
@"
# This file is automatically generated, do not modify it
KICAD_API_VERSION = "$gitDescribe"
"@ | Set-Content -Path $apiVersionFile -Encoding utf8
Write-Host "  KICAD_API_VERSION = $gitDescribe" -ForegroundColor Gray

Write-Host "Installing kipy and dependencies..." -ForegroundColor Cyan
# Always update kipy (it's fast and ensures proto files are current)
if (Test-Path "$sitePackages\kipy") {
    Remove-Item -Recurse -Force "$sitePackages\kipy"
}
Copy-Item -Recurse $kipySource $sitePackages\kipy
Write-Host "  Updated kipy package" -ForegroundColor Gray

# Install kipy Python dependencies (protobuf, pynng, etc.) if not already present.
# Use $pythonBin (vcpkg python) — bare `python` would resolve to the Microsoft
# Store stub when no real Python is on PATH and silently no-op.
if (-not (Test-Path "$sitePackages\google\protobuf")) {
    Write-Host "  Installing Python dependencies (protobuf, pynng, typing_extensions)..." -ForegroundColor Gray
    & $pythonBin -m pip install --no-user --target $sitePackages "protobuf>=6.33" "pynng>=0.8.0,<0.9.0" typing_extensions
    if ($LASTEXITCODE -ne 0) {
        Write-Host "  Dependency install failed!" -ForegroundColor Red
        exit 1
    }
    Write-Host "  Dependencies installed" -ForegroundColor Gray
} else {
    Write-Host "  Python dependencies already present" -ForegroundColor Gray
}

# Install mcp SDK (and its transitive deps) for the bundled zeo-mcp.exe launcher
if (-not (Test-Path "$sitePackages\mcp")) {
    Write-Host "  Installing mcp SDK..." -ForegroundColor Gray
    & $pythonBin -m pip install --no-user --target $sitePackages "mcp>=1.0.0"
    if ($LASTEXITCODE -ne 0) {
        Write-Host "  mcp SDK install failed!" -ForegroundColor Red
        exit 1
    }
    Write-Host "  mcp SDK installed" -ForegroundColor Gray
} else {
    Write-Host "  mcp SDK already present" -ForegroundColor Gray
}

# Install KiCad symbol and footprint libraries.
# Submodules live at $ZeoRoot directly (kicad-symbols/, kicad-footprints/, ...),
# not under a libraries/ subdir.
$libsBase    = $ZeoRoot
$shareDir    = Join-Path $InstallDir "share\kicad"
$templateDir = Join-Path $shareDir   "template"

if (-not (Test-Path "$shareDir\symbols\Device.kicad_sym")) {
    Write-Host "Installing KiCad symbol libraries..." -ForegroundColor Cyan
    New-Item -ItemType Directory -Path "$shareDir\symbols" -Force | Out-Null
    Copy-Item "$libsBase\kicad-symbols\*.kicad_sym" "$shareDir\symbols\" -Force
    Write-Host "  Installed symbol libraries" -ForegroundColor Gray
} else {
    Write-Host "  Symbol libraries already installed" -ForegroundColor Gray
}

if (-not (Test-Path "$shareDir\footprints")) {
    Write-Host "Installing KiCad footprint libraries..." -ForegroundColor Cyan
    New-Item -ItemType Directory -Path "$shareDir\footprints" -Force | Out-Null
    Copy-Item "$libsBase\kicad-footprints\*.pretty" "$shareDir\footprints\" -Recurse -Force
    Write-Host "  Installed footprint libraries" -ForegroundColor Gray
} else {
    Write-Host "  Footprint libraries already installed" -ForegroundColor Gray
}

# Always (re)write sym-lib-table and fp-lib-table templates with KICAD9->KICAD10
# rewrite. Mirrors the substitution in build.ps1's Start-Prepare-Package.
# Done unconditionally so incremental rebuilds also fix stale templates.
# PS5.1 Set-Content defaults to UTF-16 LE (BOM); KiCad's parser expects UTF-8,
# so always pass -Encoding utf8.
Write-Host "Patching lib-table templates (KICAD9_* -> KICAD10_*)..." -ForegroundColor Cyan
$symTable = Get-Content "$libsBase\kicad-symbols\sym-lib-table" -Raw
$symTable = $symTable -replace 'KICAD9_', 'KICAD10_'
Set-Content "$templateDir\sym-lib-table" $symTable -NoNewline -Encoding utf8
$fpTable = Get-Content "$libsBase\kicad-footprints\fp-lib-table" -Raw
$fpTable = $fpTable -replace 'KICAD9_', 'KICAD10_'
Set-Content "$templateDir\fp-lib-table" $fpTable -NoNewline -Encoding utf8
Write-Host "  Patched sym-lib-table + fp-lib-table" -ForegroundColor Gray

Write-Host "Done!" -ForegroundColor Green
