# Zeo Windows Fast Build Script (win_build_fast.ps1)
# Runs cmake --build directly in the inner build directory.
# Requires a prior full build to set up cmake.
# Typical agent rebuild: 5-15s for one changed .cpp file.

param(
    [switch]$Agent,
    [string]$Target,
    [switch]$Python,
    [switch]$Install,
    [switch]$Quit,
    [switch]$Launch,
    [switch]$Debug,
    [switch]$Help
)

# --- Configuration ---

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$WorkspaceDir = Split-Path -Parent $ScriptDir

$KicadSourceDir = "$WorkspaceDir\code\zeo"
$KicadPythonDir = "$WorkspaceDir\code\zeo-python"
$BuilderDir = "$WorkspaceDir\packaging\kicad-win-builder"
$LibrariesDir = "$WorkspaceDir\libraries"

$VsPath = "C:\Program Files\Microsoft Visual Studio\18\Community"
$CmakeExe = "$VsPath\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$BuildDir = "$BuilderDir\.build\kicad\build\x64-windows-Release"
$InstallDir = "$BuilderDir\.out\x64-windows-Release"

$Protoc = "$BuilderDir\vcpkg\packages\protobuf_x64-windows\tools\protobuf\protoc.exe"

$NumCPU = (Get-CimInstance Win32_Processor).NumberOfLogicalProcessors

# --- Help ---

if ($Help) {
    Write-Host @"
Usage: .\win_build_fast.ps1 [OPTIONS]

Fast incremental build - runs cmake --build directly in the build tree.
Requires a prior full build.

Options:
  (default)          Build all targets
  -Agent             Build agent target only
  -Target <name>     Build a specific cmake target
  -Python            Force reinstall kipy (auto-installed if missing)
  -Install           Run cmake --install to populate output directory
  -Quit              Kill running Zeo before build
  -Launch            Launch Zeo after build
  -Debug             Launch with WXTRACE debug tracing (implies -Launch)
  -Help              Show this help message
"@
    exit 0
}

# --- Resolve flags ---

$BuildTarget = ""
if ($Agent) { $BuildTarget = "agent" }
if ($Target) { $BuildTarget = $Target }
if ($Debug) { $Launch = $true }

# --- Helper Functions ---

function Log($msg) {
    Write-Host "[FAST] $msg" -ForegroundColor Cyan
}

function Quit-Zeo {
    $procs = Get-Process -Name "Zeo" -ErrorAction SilentlyContinue
    if (-not $procs) { return }

    Log "Closing running Zeo instances..."
    $procs | ForEach-Object {
        try { $_.CloseMainWindow() | Out-Null } catch {}
    }

    # Wait up to 10s for graceful exit
    for ($i = 0; $i -lt 20; $i++) {
        $procs = Get-Process -Name "Zeo" -ErrorAction SilentlyContinue
        if (-not $procs) { return }
        Start-Sleep -Milliseconds 500
    }

    # Force kill
    Log "Zeo didn't quit gracefully, force killing..."
    Stop-Process -Name "Zeo" -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 500
}

# --- Validate Prerequisites ---

if (-not (Test-Path $BuildDir)) {
    Write-Host "Error: Build directory not found at:" -ForegroundColor Red
    Write-Host "  $BuildDir"
    Write-Host ""
    Write-Host "Run the full build first to set up the cmake build tree."
    exit 1
}

# --- Main ---

$StartTime = Get-Date

Write-Host "=============================================="
Write-Host "Zeo Fast Build (Windows)"
Write-Host "=============================================="
if ($BuildTarget) {
    Write-Host "Target:   $BuildTarget"
} else {
    Write-Host "Target:   (all)"
}
if ($Quit)    { Write-Host "Quit:     yes" }
if ($Python)  { Write-Host "Python:   yes" }
if ($Install) { Write-Host "Install:  yes" }
if ($Launch)  { Write-Host "Launch:   yes" }
if ($Debug)   { Write-Host "Debug:    yes" }
Write-Host "CPUs:     $NumCPU"
Write-Host "=============================================="

# --- Set up MSVC environment ---

if (-not $env:VSINSTALLDIR) {
    Log "Setting up Visual Studio environment..."
    Import-Module "$VsPath\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
    Enter-VsDevShell -VsInstallPath $VsPath -SkipAutomaticLocation -Arch amd64
}

# --- Quit running instance ---

if ($Quit) {
    Quit-Zeo
}

# --- Compile ---

Log "Building in $BuildDir ..."

if ($BuildTarget) {
    & $CmakeExe --build $BuildDir --config Release --target $BuildTarget --parallel $NumCPU
    if ($LASTEXITCODE -ne 0) {
        Log "ERROR: Build failed for target '$BuildTarget'"
        exit 1
    }
} else {
    # Build the standard set of targets (same as rebuild_agent.ps1)
    $targets = @("kiplatform", "kicad", "terminal_kiface", "vcs_kiface", "agent")
    foreach ($t in $targets) {
        Log "Building $t..."
        & $CmakeExe --build $BuildDir --config Release --target $t --parallel $NumCPU
        if ($LASTEXITCODE -ne 0) {
            Log "ERROR: Build failed for target '$t'"
            exit 1
        }
    }
}

Log "Build complete."

# --- Quick-copy agent DLL when using -Agent (avoids full cmake --install) ---

if ($Agent -and -not $Install) {
    $agentDll = Join-Path $BuildDir 'agent\_agent.dll'
    $agentExe = Join-Path $BuildDir 'agent\agent.exe'
    $destBin  = Join-Path $InstallDir 'bin'

    if (Test-Path $agentDll) {
        Copy-Item $agentDll "$destBin\_agent.dll" -Force
        Copy-Item $agentExe "$destBin\agent.exe" -Force
        Log "Copied agent DLL to output directory."
    }

    # Also copy Python tool scripts and prompts
    $buildPython = Join-Path $BuildDir 'agent\python'
    $buildPrompts = Join-Path $BuildDir 'agent\prompts'
    if (Test-Path $buildPython) {
        if (Test-Path "$destBin\agent\python") { Remove-Item -Recurse -Force "$destBin\agent\python" }
        Copy-Item -Recurse $buildPython "$destBin\agent\python"
    }
    if (Test-Path $buildPrompts) {
        if (Test-Path "$destBin\agent\prompts") { Remove-Item -Recurse -Force "$destBin\agent\prompts" }
        Copy-Item -Recurse $buildPrompts "$destBin\agent\prompts"
    }
}

# --- Install ---

if ($Install) {
    Log "Running cmake --install..."
    & $CmakeExe --install $BuildDir --prefix $InstallDir
    if ($LASTEXITCODE -ne 0) {
        Log "ERROR: Install failed"
        exit 1
    }
    Log "Install complete."
}

# --- Install kipy ---

function Install-Kipy {
    Log "Installing kipy (KiCad Python bindings)..."

    $sitePackages = "$InstallDir\bin\Lib\site-packages"
    $kipySource = "$KicadPythonDir\kipy"
    $protoInput = "$KicadSourceDir\api\proto"

    # Generate protobuf Python files (always regenerate to pick up proto changes)
    Log "Generating protobuf Python files..."
    Push-Location $KicadPythonDir
    python tools\generate_protos.py --protoc $Protoc --input $protoInput --output "$kipySource\proto"
    Pop-Location

    # Copy kipy package
    if (Test-Path "$sitePackages\kipy") {
        Remove-Item -Recurse -Force "$sitePackages\kipy"
    }
    New-Item -ItemType Directory -Path $sitePackages -Force | Out-Null
    Copy-Item -Recurse $kipySource "$sitePackages\kipy"
    Log "Updated kipy package."

    # Install Python dependencies if missing
    if (-not (Test-Path "$sitePackages\google\protobuf")) {
        Log "Installing Python dependencies (protobuf, pynng, typing_extensions, matplotlib, cairosvg)..."
        python -m pip install --no-user --target $sitePackages "protobuf>=6.33" "pynng>=0.8.0,<0.9.0" typing_extensions matplotlib cairosvg 2>&1 | Out-Null
    }

    # Ensure matplotlib and cairosvg are installed (may have been missed on initial install)
    if (-not (Test-Path "$sitePackages\matplotlib")) {
        Log "Installing matplotlib..."
        python -m pip install --no-user --target $sitePackages matplotlib 2>&1 | Out-Null
    }
    if (-not (Test-Path "$sitePackages\cairosvg")) {
        Log "Installing cairosvg..."
        python -m pip install --no-user --target $sitePackages cairosvg 2>&1 | Out-Null
    }

    Log "kipy installed."
}

$sitePackages = "$InstallDir\bin\Lib\site-packages"
if ($Python) {
    Install-Kipy
} elseif (-not (Test-Path "$sitePackages\kipy")) {
    Install-Kipy
}

# --- Install libraries (one-time) ---

$shareDir = "$InstallDir\share\kicad"
$templateDir = "$shareDir\template"

if ((Test-Path $LibrariesDir) -and -not (Test-Path "$shareDir\symbols\Device.kicad_sym")) {
    Log "Installing KiCad symbol libraries..."
    New-Item -ItemType Directory -Path "$shareDir\symbols" -Force | Out-Null
    Copy-Item "$LibrariesDir\kicad-symbols\*.kicad_sym" "$shareDir\symbols\" -Force
    New-Item -ItemType Directory -Path $templateDir -Force | Out-Null
    $symTable = Get-Content "$LibrariesDir\kicad-symbols\sym-lib-table" -Raw
    $symTable = $symTable -replace 'KICAD9_SYMBOL_DIR', 'KICAD10_SYMBOL_DIR'
    Set-Content "$templateDir\sym-lib-table" $symTable
}

if ((Test-Path $LibrariesDir) -and -not (Test-Path "$shareDir\footprints")) {
    Log "Installing KiCad footprint libraries..."
    New-Item -ItemType Directory -Path "$shareDir\footprints" -Force | Out-Null
    Copy-Item "$LibrariesDir\kicad-footprints\*.pretty" "$shareDir\footprints\" -Recurse -Force
    New-Item -ItemType Directory -Path $templateDir -Force | Out-Null
    $fpTable = Get-Content "$LibrariesDir\kicad-footprints\fp-lib-table" -Raw
    $fpTable = $fpTable -replace 'KICAD9_FOOTPRINT_DIR', 'KICAD10_FOOTPRINT_DIR'
    Set-Content "$templateDir\fp-lib-table" $fpTable
}

# --- Launch ---

if ($Launch) {
    $zeoBin = Join-Path $InstallDir 'bin\Zeo.exe'

    if (-not (Test-Path $zeoBin)) {
        Log ('Zeo.exe not found at ' + $zeoBin + ' - did you forget -Install?')
        exit 1
    }

    $logFile = Join-Path $InstallDir 'bin\zeo_stderr.log'
    $binDir = Join-Path $InstallDir 'bin'
    $env:PATH = $binDir + ';' + $env:PATH

    if ($Debug) {
        Log 'Launching with debug tracing KICAD_ENABLE_WXTRACE=1...'
        $env:KICAD_ENABLE_WXTRACE = '1'
        Start-Process -FilePath $zeoBin -WorkingDirectory $binDir -RedirectStandardError $logFile
        Log ('Logs: ' + $logFile)
    } else {
        Log 'Launching Zeo...'
        Start-Process -FilePath $zeoBin -WorkingDirectory $binDir
        Log ('Logs: ' + $logFile)
    }
}

# --- Timing ---

$Elapsed = [math]::Round(((Get-Date) - $StartTime).TotalSeconds)
Log ('Done in ' + $Elapsed + 's.')
