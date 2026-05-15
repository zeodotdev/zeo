# Zeo Windows Installer Build Script (win_build_installer.ps1)
# Creates an NSIS installer with Zeo, Agent, Terminal, libraries, and 3D models.
# Parallel to mac_build_dmg.sh and linux_build_appimage.sh.
#
# Usage:
#   .\dev\win_build_installer.ps1                       # Full build + installer
#   .\dev\win_build_installer.ps1 -Release "1.0"        # Named release installer
#   .\dev\win_build_installer.ps1 -Light                # Light version (no 3D models)

param(
    [string]$Release,
    [switch]$Light,
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
Usage: .\win_build_installer.ps1 [OPTIONS]

Creates an NSIS installer with all dependencies including 3D models.
Requires a prior full build to set up the cmake build tree.

Options:
  (default)              Full clean build + installer
  -Release <name>        Set release name (e.g., "1.0", "beta")
  -Light                 Build light version without 3D models (~300MB vs ~1.1GB)
  -Help                  Show this help message

Examples:
  .\dev\win_build_installer.ps1                          # Full build + installer
  .\dev\win_build_installer.ps1 -Release "1.0"           # Release installer
  .\dev\win_build_installer.ps1 -Light                   # Light installer (no 3D models)
"@
    exit 0
}

# --- Prevent sleep during build ---

# Use SetThreadExecutionState to keep the system awake while building.
# ES_CONTINUOUS (0x80000000) + ES_SYSTEM_REQUIRED (0x00000001) prevents sleep.
# Restored automatically at the end of the script.
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public class SleepGuard {
    [DllImport("kernel32.dll")]
    public static extern uint SetThreadExecutionState(uint esFlags);
    public static void PreventSleep() {
        SetThreadExecutionState(0x80000001); // ES_CONTINUOUS | ES_SYSTEM_REQUIRED
    }
    public static void AllowSleep() {
        SetThreadExecutionState(0x80000000); // ES_CONTINUOUS (clear previous flags)
    }
}
"@

[SleepGuard]::PreventSleep()
Write-Host "[INSTALLER] Sleep prevention enabled (will restore when build finishes)." -ForegroundColor Cyan

# Ensure sleep is re-enabled even if the script fails
$null = Register-EngineEvent PowerShell.Exiting -Action { [SleepGuard]::AllowSleep() }
trap {
    [SleepGuard]::AllowSleep()
    throw $_
}

# --- Banner ---

$StartTime = Get-Date

Write-Host "=============================================="
Write-Host "Zeo Windows Installer Builder"
Write-Host "=============================================="
Write-Host "Workspace:    $WorkspaceDir"
Write-Host "Source Dir:   $KicadSourceDir"
Write-Host "Builder Dir:  $BuilderDir"
Write-Host "Libraries:    $LibrariesDir"
Write-Host "Output Dir:   $InstallDir"
Write-Host "Clean Build:  yes"
Write-Host "Release:      $(if ($Release) { $Release } else { '<auto>' })"
Write-Host "Light Build:  $Light"
Write-Host "CPUs:         $NumCPU"
Write-Host "=============================================="

# --- Helper Functions ---

function Log($msg) {
    Write-Host "[INSTALLER] $msg" -ForegroundColor Cyan
}

function Log-Error($msg) {
    Write-Host "[INSTALLER] ERROR: $msg" -ForegroundColor Red
}

function Quit-Zeo {
    $procs = Get-Process -Name "Zeo" -ErrorAction SilentlyContinue
    if (-not $procs) { return }

    Log "Closing running Zeo instances..."
    $procs | ForEach-Object {
        try { $_.CloseMainWindow() | Out-Null } catch {}
    }

    for ($i = 0; $i -lt 20; $i++) {
        $procs = Get-Process -Name "Zeo" -ErrorAction SilentlyContinue
        if (-not $procs) { return }
        Start-Sleep -Milliseconds 500
    }

    Log "Zeo didn't quit gracefully, force killing..."
    Stop-Process -Name "Zeo" -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 500
}

# --- Validation ---

if (-not (Test-Path $KicadSourceDir)) {
    Log-Error "KiCad source directory not found at $KicadSourceDir"
    exit 1
}

if (-not (Test-Path $BuilderDir)) {
    Log-Error "kicad-win-builder directory not found at $BuilderDir"
    exit 1
}

if (-not (Test-Path $BuildDir)) {
    Log-Error "Build directory not found at $BuildDir"
    Write-Host "  Run the full build first to set up the cmake build tree."
    exit 1
}

# --- Set up MSVC environment ---

if (-not $env:VSINSTALLDIR) {
    Log "Setting up Visual Studio environment..."
    Import-Module "$VsPath\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
    Enter-VsDevShell -VsInstallPath $VsPath -SkipAutomaticLocation -Arch amd64
}

# --- Close running instance ---

Quit-Zeo

# --- Phase 1: Clean Build ---

Write-Host ""
Write-Host "=========================================="
Write-Host "PHASE 1: Building KiCad/Zeo (clean)"
Write-Host "=========================================="

# Clean build to avoid stale cached artifacts (icons, templates, etc.)
Log "Cleaning previous build..."
& $CmakeExe --build $BuildDir --target clean 2>$null

# Build all targets (not just specific ones, so resources like images.tar.gz are included)
Log "Building all targets..."
& $CmakeExe --build $BuildDir --config Release --parallel $NumCPU
if ($LASTEXITCODE -ne 0) {
    Log-Error "Build failed"
    exit 1
}

Log "Build complete."

# --- cmake --install ---

Log "Running cmake --install..."
& $CmakeExe --install $BuildDir --prefix $InstallDir
if ($LASTEXITCODE -ne 0) {
    Log-Error "cmake --install failed"
    exit 1
}
Log "cmake --install complete."

# --- Phase 2: Install Libraries ---

Write-Host ""
Write-Host "=========================================="
Write-Host "PHASE 2: Installing Libraries"
Write-Host "=========================================="

$shareDir = "$InstallDir\share\kicad"
$templateDir = "$shareDir\template"

# --- Symbols ---

if (Test-Path "$LibrariesDir\kicad-symbols") {
    Log "Installing symbol libraries..."
    New-Item -ItemType Directory -Path "$shareDir\symbols" -Force | Out-Null
    Copy-Item "$LibrariesDir\kicad-symbols\*.kicad_sym" "$shareDir\symbols\" -Force
    New-Item -ItemType Directory -Path $templateDir -Force | Out-Null
    $symTable = Get-Content "$LibrariesDir\kicad-symbols\sym-lib-table" -Raw
    $symTable = $symTable -replace 'KICAD9_SYMBOL_DIR', 'KICAD10_SYMBOL_DIR'
    Set-Content "$templateDir\sym-lib-table" $symTable
    Log "Symbols installed."
} else {
    Log-Error "Symbol libraries not found at $LibrariesDir\kicad-symbols"
}

# --- Footprints ---

if (Test-Path "$LibrariesDir\kicad-footprints") {
    Log "Installing footprint libraries..."
    New-Item -ItemType Directory -Path "$shareDir\footprints" -Force | Out-Null
    Copy-Item "$LibrariesDir\kicad-footprints\*.pretty" "$shareDir\footprints\" -Recurse -Force
    New-Item -ItemType Directory -Path $templateDir -Force | Out-Null
    $fpTable = Get-Content "$LibrariesDir\kicad-footprints\fp-lib-table" -Raw
    $fpTable = $fpTable -replace 'KICAD9_FOOTPRINT_DIR', 'KICAD10_FOOTPRINT_DIR'
    Set-Content "$templateDir\fp-lib-table" $fpTable
    Log "Footprints installed."
} else {
    Log-Error "Footprint libraries not found at $LibrariesDir\kicad-footprints"
}

# --- 3D Models ---

if (-not $Light) {
    if (Test-Path "$LibrariesDir\kicad-packages3D") {
        Log "Installing 3D model libraries (this may take a moment)..."
        $packages3dDir = "$shareDir\3dmodels"
        New-Item -ItemType Directory -Path $packages3dDir -Force | Out-Null

        # Copy all .3dshapes directories with their .wrl and .step files
        $shapeDirs = Get-ChildItem "$LibrariesDir\kicad-packages3D" -Directory -Filter "*.3dshapes"
        $totalDirs = $shapeDirs.Count
        $currentDir = 0

        foreach ($dir in $shapeDirs) {
            $currentDir++
            if ($currentDir % 20 -eq 0 -or $currentDir -eq $totalDirs) {
                Write-Host "  [$currentDir/$totalDirs] $($dir.Name)" -ForegroundColor Gray
            }
            $destDir = "$packages3dDir\$($dir.Name)"
            New-Item -ItemType Directory -Path $destDir -Force | Out-Null
            Copy-Item "$($dir.FullName)\*.wrl" $destDir -Force -ErrorAction SilentlyContinue
            Copy-Item "$($dir.FullName)\*.step" $destDir -Force -ErrorAction SilentlyContinue
        }

        Log "3D models installed ($totalDirs libraries)."
    } else {
        Log-Error "3D model libraries not found at $LibrariesDir\kicad-packages3D"
        Write-Host "  The installer will be missing 3D models (~800MB)."
        Write-Host "  Clone the repo: git clone https://gitlab.com/kicad/libraries/kicad-packages3D.git $LibrariesDir\kicad-packages3D"
    }
} else {
    Log "Skipping 3D models (-Light specified)"
}

# --- Templates ---

if (Test-Path "$LibrariesDir\kicad-templates\Projects") {
    Log "Installing templates..."
    New-Item -ItemType Directory -Path $templateDir -Force | Out-Null
    # Copy only the project templates (not repo files like CMakeLists.txt, README, etc.)
    # KiCad expects each subdirectory to have a meta/ folder
    Copy-Item "$LibrariesDir\kicad-templates\Projects\*" $templateDir -Recurse -Force
    # Also copy the default project template file if present
    if (Test-Path "$LibrariesDir\kicad-templates\kicad.kicad_pro") {
        Copy-Item "$LibrariesDir\kicad-templates\kicad.kicad_pro" $templateDir -Force
    }
    Log "Templates installed."
} elseif (Test-Path "$LibrariesDir\kicad-templates") {
    Log "Warning: kicad-templates found but Projects/ subdirectory missing."
}

# --- Freerouting + Bundled JRE ---

$freeroutingDir = "$WorkspaceDir\tools\freerouting"
$freeroutingJar = "$freeroutingDir\integrations\KiCad\kicad-freerouting\plugins\jar\freerouting-2.1.0.jar"
if (-not (Test-Path $freeroutingJar)) {
    # Try the gradle build output
    $freeroutingJar = "$freeroutingDir\build\libs\freerouting-executable.jar"
}

if (Test-Path $freeroutingJar) {
    Log "Installing Freerouting..."
    $destFreerouting = "$InstallDir\bin\freerouting"
    New-Item -ItemType Directory -Path $destFreerouting -Force | Out-Null
    Copy-Item $freeroutingJar "$destFreerouting\freerouting.jar" -Force
    Log "Freerouting JAR installed."

    # Bundle a minimal JRE so freerouting works without system Java
    $jreDest = "$destFreerouting\jre"
    if (-not (Test-Path "$jreDest\bin\java.exe")) {
        $jreVersion = "21"
        $jreZip = "$BuilderDir\.support\temurin-jre-$jreVersion-x64.zip"
        $jreExtract = "$BuilderDir\.support\temurin-jre-$jreVersion"

        # Download Adoptium Temurin JRE if not cached
        if (-not (Test-Path $jreZip)) {
            Log "Downloading Adoptium Temurin JRE $jreVersion..."
            $jreUrl = "https://api.adoptium.net/v3/binary/latest/$jreVersion/ga/windows/x64/jre/hotspot/normal/eclipse?project=jdk"
            Invoke-WebRequest -Uri $jreUrl -OutFile $jreZip -UseBasicParsing
            Log "JRE downloaded."
        }

        # Extract JRE
        if (-not (Test-Path $jreExtract)) {
            Log "Extracting JRE..."
            Expand-Archive -Path $jreZip -DestinationPath $jreExtract -Force
        }

        # Copy JRE to freerouting directory (find the inner jdk-* folder)
        # Use -LiteralPath to handle the '+' character in folder names like jdk-21.0.10+7-jre
        $jreInner = Get-ChildItem -LiteralPath $jreExtract -Directory | Select-Object -First 1
        if ($jreInner) {
            Log "Installing bundled JRE..."
            New-Item -ItemType Directory -Path $jreDest -Force | Out-Null
            Get-ChildItem -LiteralPath $jreInner.FullName | ForEach-Object {
                Copy-Item -LiteralPath $_.FullName -Destination $jreDest -Recurse -Force
            }
            Log "Bundled JRE installed."
        } else {
            Log-Error "Could not find JRE contents in extracted archive."
        }
    } else {
        Log "Bundled JRE already present."
    }
} else {
    Log "Warning: Freerouting JAR not found, skipping."
}

# --- Phase 3: Install kipy ---

Write-Host ""
Write-Host "=========================================="
Write-Host "PHASE 3: Installing kipy"
Write-Host "=========================================="

$sitePackages = "$InstallDir\bin\Lib\site-packages"

function Install-Kipy {
    Log "Installing kipy (KiCad Python bindings)..."

    $kipySource = "$KicadPythonDir\kipy"
    $protoInput = "$KicadSourceDir\api\proto"

    # Generate protobuf Python files
    if (-not (Test-Path "$kipySource\proto\common\envelope_pb2.py")) {
        Log "Generating protobuf Python files..."
        $protoFiles = Get-ChildItem -Path $protoInput -Filter "*.proto" -Recurse | Select-Object -ExpandProperty FullName
        & $Protoc --python_out="$kipySource\proto" --proto_path=$protoInput @protoFiles
        python -m protoletariat --dont-create-package --in-place --exclude-google-imports --python-out "$kipySource\proto" protoc --protoc-path $Protoc --proto-path $protoInput @protoFiles
    }

    # Copy kipy package
    if (Test-Path "$sitePackages\kipy") {
        Remove-Item -Recurse -Force "$sitePackages\kipy"
    }
    New-Item -ItemType Directory -Path $sitePackages -Force | Out-Null
    Copy-Item -Recurse $kipySource "$sitePackages\kipy"

    # Install Python dependencies if missing
    if (-not (Test-Path "$sitePackages\google\protobuf")) {
        Log "Installing Python dependencies..."
        python -m pip install --no-user --target $sitePackages "protobuf>=6.33" "pynng>=0.8.0,<0.9.0" typing_extensions matplotlib cairosvg 2>&1 | Out-Null
    }

    Log "kipy installed."
}

if (Test-Path $KicadPythonDir) {
    Install-Kipy
} else {
    Log "kipy source not found at $KicadPythonDir, skipping."
}

# --- Phase 4: Create NSIS Installer ---

Write-Host ""
Write-Host "=========================================="
Write-Host "PHASE 4: Creating NSIS Installer"
Write-Host "=========================================="

# Find NSIS
$nsisExe = $null
$nsisSearchPaths = @(
    "$BuilderDir\.support\nsis-*\bin\makensis.exe",
    "C:\Program Files (x86)\NSIS\makensis.exe",
    "C:\Program Files\NSIS\makensis.exe"
)

foreach ($pattern in $nsisSearchPaths) {
    $found = Get-ChildItem $pattern -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($found) {
        $nsisExe = $found.FullName
        break
    }
}

if (-not $nsisExe) {
    # Try PATH
    $nsisExe = Get-Command makensis.exe -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source
}

if (-not $nsisExe) {
    Log-Error "NSIS (makensis.exe) not found."
    Write-Host "  Install NSIS or run 'build.ps1 -Init' to download it."
    Write-Host ""
    Write-Host "  The output directory is ready at:"
    Write-Host "    $InstallDir"
    Write-Host ""
    Write-Host "  You can create the installer manually with build.ps1 -Package"
    exit 1
}

Log "Using NSIS: $nsisExe"

# Copy NSIS scripts to output
$nsisSource = "$BuilderDir\nsis"
if (-not (Test-Path $nsisSource)) {
    Log-Error "NSIS scripts not found at $nsisSource"
    exit 1
}

Copy-Item $nsisSource -Destination $InstallDir -Recurse -Container -Force

# Get version info from KiCadVersion.cmake (same method as build.ps1)
$kicadVersion = "0.1.0"
$packageVersion = "0.1.0"
try {
    $versionCmake = "$KicadSourceDir\cmake\KiCadVersion.cmake"
    if (-not (Test-Path $versionCmake)) {
        $versionCmake = "$KicadSourceDir\CMakeModules\KiCadVersion.cmake"
    }
    # Get version from git tag (e.g. "0.1.0")
    Push-Location $KicadSourceDir
    $revCount = git describe --long --tags 2>$null
    Pop-Location
    if ($revCount) {
        # Strip commit count and hash suffix (e.g. "0.1.0-3-g61714d0" -> "0.1.0")
        $tagVersion = $revCount -replace '-\d+-g[0-9a-f]+$', ''
        $kicadVersion = $tagVersion
        $packageVersion = $tagVersion
    }
} catch {
    try { Pop-Location } catch {}
    Log "Could not read version info, using defaults."
}

# Get git revision
$gitRev = "unknown"
try {
    Push-Location $KicadSourceDir
    $gitRev = git rev-parse --short HEAD 2>$null
    Pop-Location
} catch {
    Pop-Location
}

# Determine output filename
$now = Get-Date -Format "yyyyMMdd-HHmmss"
if ($Release) {
    $outFileName = "zeo-$Release-x64.exe"
} else {
    $outFileName = "zeo-$now-$gitRev-x64.exe"
}

if ($Light) {
    $outFileName = $outFileName -replace '\.exe$', '-lite.exe'
}

# HACKFIX: Remove files with paths too long for NSIS (same fix as build.ps1)
$longPathPatterns = @(
    "share\kicad\demos\*\*.pretty\DSUB-9_Female_Horizontal_P2.77x2.84mm_EdgePinOffset7.70mm_Housed_MountingHolesOffset9.12mm.kicad_mod",
    "share\kicad\demos\*\*\*.pretty\DSUB-9_Female_Horizontal_P2.77x2.84mm_EdgePinOffset7.70mm_Housed_MountingHolesOffset9.12mm.kicad_mod",
    "share\kicad\demos\*\*\*.pretty\DSUB-9_Female_Horizontal_P2.77x2.84mm_EdgePinOffset14.56mm_Housed_MountingHolesOffset15.98mm.kicad_mod"
)
foreach ($pattern in $longPathPatterns) {
    $badFiles = Get-ChildItem (Join-Path $InstallDir $pattern) -ErrorAction SilentlyContinue
    foreach ($f in $badFiles) {
        Log "Removing long-path file: $($f.Name)"
        Remove-Item -Path $f.FullName -Force
    }
}

# Determine NSIS script
$nsisScript = "$InstallDir\nsis\install.nsi"
if (-not (Test-Path $nsisScript)) {
    Log-Error "NSIS script not found at $nsisScript"
    exit 1
}

# Handle vcredist
$vcredistBuild = ""
$testPath = "$InstallDir\bin\vcruntime140.dll"
if (-not (Test-Path $testPath)) {
    $vcredistDest = "$InstallDir\nsis\vcredist"
    if (-not (Test-Path $vcredistDest)) {
        New-Item -Path $vcredistDest -ItemType "directory" | Out-Null
    }
    if ($env:VCToolsRedistDir) {
        Copy-Item -Path "$env:VCToolsRedistDir\*" -Destination $vcredistDest -Include vc_redist*
        $redistVersion = [System.Diagnostics.FileVersionInfo]::GetVersionInfo("$env:VCToolsRedistDir\vc_redist.x64.exe")
        $vcredistBuild = $redistVersion.FileBuildPart
    }
}

# Build NSIS args
$nsisArgs = @(
    "/DPACKAGE_VERSION=$packageVersion",
    "/DKICAD_VERSION=$kicadVersion",
    "/DOUTFILE=..\..\$outFileName",
    "/DARCH=x64",
    "/DVCRUNTIME_MINIMUM_BLD=$vcredistBuild",
    "/DMSVC"
)

# In Light mode, define LIBRARIES_TAG so NSIS makes 3D models a download-on-demand section
if ($Light) {
    $nsisArgs += "/DLIBRARIES_TAG=master"
}

Log "Building installer: $outFileName"
Log "NSIS args: $($nsisArgs -join ' ')"

& $nsisExe @nsisArgs $nsisScript

if ($LASTEXITCODE -ne 0) {
    Log-Error "NSIS installer build failed"
    exit 1
}

# Clean up NSIS files from output
$nsisFolder = "$InstallDir\nsis"
if (Test-Path $nsisFolder) {
    Remove-Item $nsisFolder -Recurse -Force
}

# --- Done ---

$installerPath = Join-Path $InstallDir "..\..\$outFileName"
$installerPath = [System.IO.Path]::GetFullPath((Join-Path $InstallDir $outFileName))

# The NSIS output is relative to the nsis script dir, which is $InstallDir\nsis
# With /DOUTFILE=..\..\$outFileName, it goes to $BuilderDir
$installerPath = "$BuilderDir\$outFileName"

Write-Host ""
Write-Host "=========================================="
Write-Host "INSTALLER BUILD COMPLETE"
Write-Host "=========================================="
Write-Host ""

if (Test-Path $installerPath) {
    $size = (Get-Item $installerPath).Length
    $sizeMB = [math]::Round($size / 1MB)
    Write-Host "Installer: $installerPath"
    Write-Host "Size:      ${sizeMB} MB"
} else {
    # Search for it
    $found = Get-ChildItem "$BuilderDir\$outFileName" -ErrorAction SilentlyContinue
    if (-not $found) {
        $found = Get-ChildItem "$BuilderDir\.out\$outFileName" -ErrorAction SilentlyContinue
    }
    if ($found) {
        $installerPath = $found.FullName
        $sizeMB = [math]::Round($found.Length / 1MB)
        Write-Host "Installer: $installerPath"
        Write-Host "Size:      ${sizeMB} MB"
    } else {
        Write-Host "Installer: $outFileName (check $BuilderDir for output)"
    }
}

if ($Light) {
    Write-Host "Type:      Light (no 3D models)"
} else {
    Write-Host "Type:      Full (with 3D models)"
}

# Re-enable sleep
[SleepGuard]::AllowSleep()
Log "Sleep prevention disabled."

$Elapsed = [math]::Round(((Get-Date) - $StartTime).TotalSeconds)
Write-Host ""
Write-Host "Completed in ${Elapsed}s."
Write-Host "=============================================="
