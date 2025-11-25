#!/usr/bin/env pwsh
#
# PowerShell build script for the Phraims Windows bundle and installer.
#
# Purpose & behavior:
# - Installs a private host Qt kit via `aqtinstall` with QtWebEngine to
#   `build/windows-<arch>/qt-host/...`.
# - Overwrites that [non-"proprietary codec"] QtWebEngine with a copy of
#   LookAtWhatAiCanDo/QtWebEngineProprietaryCodecs pre-downloaded to
#   `.qt/<QtVersion>-prop-win-<arch>`.
# - Builds Phraims for Windows into `build/windows-<arch>`
# - Produces a `deploy` folder via `windeployqt`.
# - if NSIS is successfully installed then an installer is also created.
#
# Prerequisites (developer machine or CI runner):
# - Visual Studio 2022
# - Python 3.x
# - `aqtinstall` Python package to install Qt kits.
# - `winget` to install NSIS.

### SECTION: Initialization
param(
  [ValidateSet('x64','arm64')]
  [string]$Arch,

  [ValidateSet('Release','Debug','RelWithDebInfo','MinSizeRel')]
  [string]$Config,

  # Qt version to install when host Qt is not available
  [string]$QtVersion,

  # MSVC toolset (used by CMake generator); auto-detected if empty
  [string]$VSToolset,

  # Verbose diagnostics
  [int]$DEBUG
)

# Ensure errors stop execution
$ErrorActionPreference = 'Stop'

# Defaults for parameters if not provided
if (-not $PSBoundParameters.ContainsKey('Arch')) { $Arch = 'x64' }
if (-not $PSBoundParameters.ContainsKey('Config')) { $Config = 'Release' }
if (-not $PSBoundParameters.ContainsKey('QtVersion')) { $QtVersion = '6.9.3' }
if (-not $PSBoundParameters.ContainsKey('VSToolset')) { $VSToolset = '' }
if (-not $PSBoundParameters.ContainsKey('DEBUG')) { $DEBUG = 0 }

# Resolve workspace root (script lives in ci/)
$RepoRoot = Split-Path -Parent (Split-Path -Parent $PSCommandPath)

# Paths
$BuildDir = Join-Path $RepoRoot ("build/windows-$Arch")
$QtRoot = $null
$AqtEnvDir = Join-Path $BuildDir 'qt-host'

# Custom QtWebEngine payload candidate path (relative to repo): .qt/<version>-prop-win-<arch>
$CustomQtDirRoot = Join-Path $RepoRoot '.qt'
$CustomPropCandidate = Join-Path $CustomQtDirRoot ("$QtVersion-prop-win-$Arch")

### SECTION: Utilities
function Write-Info($msg) { Write-Host "[INFO] $msg" -ForegroundColor Cyan }
function Write-Warn($msg) { Write-Host "[WARN] $msg" -ForegroundColor Yellow }
function Write-Err($msg)  { Write-Host "[ERROR] $msg" -ForegroundColor Red }

# Return a path relative to $RepoRoot when possible, otherwise return the absolute path.
function Format-RepoPath([string]$Path) {
  if (-not $Path) { return '<none>' }
  try {
    $full = (Resolve-Path -LiteralPath $Path -ErrorAction Stop).ProviderPath
  } catch {
    try { $full = [System.IO.Path]::GetFullPath($Path) } catch { $full = $Path }
  }
  try {
    $repoFull = (Get-Item $RepoRoot -ErrorAction Stop).FullName.TrimEnd('\','/')
  } catch {
    $repoFull = $RepoRoot
  }
  if ($repoFull -and $full.StartsWith($repoFull, [System.StringComparison]::InvariantCultureIgnoreCase)) {
    $rel = $full.Substring($repoFull.Length).TrimStart('\','/')
    if ($rel -eq '') { return '.' }
    return ".\$rel"
  }
  return $full
}

# Prepend a directory to the process PATH if it's not already present.
function Prepend-ToPath([string]$newPath) {
  if (-not $newPath) { return }
  if ($env:PATH.IndexOf($newPath, [System.StringComparison]::InvariantCultureIgnoreCase) -ge 0) {
    Write-Info "Path already contains: $newPath; skipping prepend."
  } else {
    $env:PATH = "$newPath;" + $env:PATH
    Write-Info "Prepended to PATH: $newPath"
  }
}

# Detect other qmake/qmake6 executables on PATH that do not belong to our
# expected private kit. Logs a warning for each found installation and returns
# the list of discovered directories. Call this before prepending our kit to
# PATH so Get-Command won't resolve our ephemeral qmake first.
function Detect-OtherQtOnPath([string]$expectedQtBin) {
  $found = @()
  $expectedFull = $null
    $expectedFull = $null
    # Only resolve expected path if it actually exists; when installing the kit
    # the expected path will not yet exist and we should not treat that as a
    # discovered 'other' Qt on PATH.
    if (Test-Path $expectedQtBin) {
      try {
        $expectedFull = (Get-Item $expectedQtBin -ErrorAction Stop).FullName.TrimEnd('\','/')
      } catch {
        $expectedFull = $expectedQtBin.TrimEnd('\','/')
      }
    } else {
      $expectedFull = $null
    }

  $qmakeCmds = @()
  $qmakeCmds += (Get-Command qmake6.exe -ErrorAction SilentlyContinue -All)
  $qmakeCmds += (Get-Command qmake.exe -ErrorAction SilentlyContinue -All)

  foreach ($c in $qmakeCmds) {
    if (-not $c) { continue }
    $src = $c.Source
    if (-not $src) { continue }
    try {
      $parent = (Get-Item (Split-Path -Parent $src) -ErrorAction Stop).FullName.TrimEnd('\','/')
    } catch {
      $parent = (Split-Path -Parent $src).TrimEnd('\','/')
    }
    if (-not $parent) { continue }
    if ($expectedFull -and $parent.Equals($expectedFull, [System.StringComparison]::InvariantCultureIgnoreCase)) { continue }
    $found += $parent
  }

  $found = $found | Select-Object -Unique
  foreach ($p in $found) {
    # Try to query the qmake in that folder for a version string
    $qmakePath = Join-Path $p 'qmake.exe'
    $ver = $null
    try {
      $ver = & $qmakePath -query QT_VERSION 2>$null
    } catch {
      try {
        $out = & $qmakePath -v 2>$null
        if ($out -match 'Qt version\s+([0-9]+\.[0-9]+\.[0-9]+)') {
          $ver = $Matches[1]
        }
      } catch {}
    }
    if ($ver) {
      Write-Warn "Found other Qt on PATH: $p (qmake version: $ver)"
    } else {
      Write-Warn "Found other Qt on PATH: $p (qmake present but version unknown)"
    }
  }

  return $found
}

### SECTION: Initialize environment
function Initialize-Environment {
  Write-Info "Initializing environment"

  Set-Location $RepoRoot
  Write-Info "Repo root: $RepoRoot"

  # Ensure build dir exists
  New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

  Import-VsDevEnv

  Ensure-HostQt
}

function Import-VsDevEnv {
  ###############################################
  # Seed MSVC environment via VsDevCmd
  ###############################################
  Write-Info "Locating Visual Studio installation..."
  $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
  if (-not (Test-Path $vswhere)) {
    throw "vswhere.exe not found at $vswhere"
  }
  $vsPath = & $vswhere -latest -requires Microsoft.Component.MSBuild -products * -property installationPath
  if (-not $vsPath) {
    throw 'Visual Studio with MSBuild not found'
  }
  $devCmd = Join-Path $vsPath 'Common7\Tools\VsDevCmd.bat'
  if (-not (Test-Path $devCmd)) {
    throw "VsDevCmd.bat not found at $devCmd"
  }

  # Prevent re-importing the VS environment multiple times. Re-running
  # VsDevCmd.bat repeatedly in the same process can append to PATH and
  # other variables, quickly exceeding environment length limits. If the
  # environment is already loaded for the requested arch, skip re-import.
  if ($env:PHRAIMS_VSENV_LOADED -eq '1') {
    Write-Info 'VS environment already imported (sentinel present); skipping import.'
    return
  }
  if ($env:VSCMD_ARG_TGT_ARCH -and ($env:VSCMD_ARG_TGT_ARCH -eq $Arch)) {
    Write-Info "VS environment already present for arch '$Arch'; skipping import."
    $env:PHRAIMS_VSENV_LOADED = '1'
    return
  }

  Write-Info "Importing VS dev environment from: $devCmd"
  # Use cmd to run VsDevCmd and emit environment with `set`,
  # then import into current PowerShell session
  $cmd = '"{0}" -arch={1} & set' -f $devCmd, $Arch
  $output = cmd /c $cmd
  foreach ($line in $output) {
    if ($line -match '^(.*?)=(.*)$') {
      $name = $Matches[1]
      $value = $Matches[2]
      [Environment]::SetEnvironmentVariable($name, $value, 'Process')
    }
  }
  # Mark that we've loaded the environment so subsequent invocations won't re-run VsDevCmd
  $env:PHRAIMS_VSENV_LOADED = '1'
  Write-Info "VS environment imported for $Arch"
}

### SECTION: Host Qt acquisition
# Ensure a private host Qt kit is available under $AqtEnvDir.
# If the requested kit is already present at the expected layout, reuse it (avoid re-downloading).
# Otherwise install it via aqtinstall in a venv.
function Ensure-HostQt {
  $qtArchMap = @{ 'x64' = 'win64_msvc2022_64'; 'arm64' = 'win64_msvc2022_arm64' }
  $qtHostSpec = $qtArchMap[$Arch]
  if (-not $qtHostSpec) {
    throw "Unsupported arch: $Arch"
  }

  # Known local kit layout: $AqtEnvDir\$QtVersion\<kitDir>
  $kitDir = if ($Arch -eq 'arm64') { 'msvc2022_arm64' } else { 'msvc2022_64' }
  $expectedQtRoot = Join-Path (Join-Path $AqtEnvDir $QtVersion) $kitDir

  if (Test-Path (Join-Path $expectedQtRoot 'bin')) {
    # Reuse installed kit
    $qtBin = Join-Path $expectedQtRoot 'bin'
  
    # Detect any other Qt on PATH before we put ourself first in PATH — warn but continue.
    Detect-OtherQtOnPath $qtBin | Out-Null

    Prepend-ToPath $qtBin

    $script:QtRoot = $expectedQtRoot
    Write-Info "Re-using existing host Qt kit at: $(Format-RepoPath $QtRoot)"
    return
  }

  Write-Info "Host Qt kit not found at expected path: $(Format-RepoPath $expectedQtRoot) — installing via aqtinstall."

  # Ensure host Python exists
  $hostPython = Get-Command python -ErrorAction SilentlyContinue
  if (-not $hostPython) {
    throw 'Python is required to install a host Qt kit via aqtinstall'
  }

  # Create venv under $AqtEnvDir/venv
  $venvDir = Join-Path $AqtEnvDir 'venv'
  if (-not (Test-Path $venvDir)) {
    Write-Info "Creating Python venv at: $(Format-RepoPath $venvDir)"
    New-Item -ItemType Directory -Force -Path $AqtEnvDir | Out-Null
    & $hostPython.Source -m venv $venvDir
  } else {
    Write-Info "Re-using existing venv at: $(Format-RepoPath $venvDir)"
  }

  # Ensure pip and install aqtinstall inside the venv
  $venvPython = Join-Path $venvDir 'Scripts\python.exe'
  if (-not (Test-Path $venvPython)) {
    throw "Virtualenv python not found at: $(Format-RepoPath $venvPython)"
  }
  Write-Info "Upgrading pip/setuptools in venv"
  & $venvPython -m pip install --upgrade pip setuptools wheel | Out-Null
  Write-Info "Installing/upgrading aqtinstall in venv"
  & $venvPython -m pip install --upgrade aqtinstall | Out-Null

  New-Item -ItemType Directory -Force -Path $AqtEnvDir | Out-Null
  Write-Info "Downloading Qt $QtVersion ($qtHostSpec) to $(Format-RepoPath $AqtEnvDir) using venv python"
  & $venvPython -m aqt install-qt windows desktop $QtVersion $qtHostSpec --outputdir "$AqtEnvDir" -m qtpositioning qtserialport qtwebchannel qtwebengine qtwebsockets
  # Verify installation and set QtRoot
  $qtRoot = $expectedQtRoot
  if (-not (Test-Path (Join-Path $qtRoot 'bin'))) {
    throw "Qt $QtVersion kit not found at $qtRoot after install"
  }
  
  $qtBin = Join-Path $qtRoot 'bin'

  # Detect other Qt installs on PATH before we put ourself first in PATH — warn but continue.
  Detect-OtherQtOnPath $qtBin | Out-Null

  Prepend-ToPath $qtBin

  $script:QtRoot = $qtRoot
  Write-Info "Installed host Qt at: $(Format-RepoPath $QtRoot)"
}

### SECTION: Ensure custom QtWebEngine
function Require-CustomWebEngine {
  if (-not (Test-Path $CustomPropCandidate)) {
    Write-Err "Required custom QtWebEngine payload not found at: $CustomPropCandidate"
    Write-Err 'Aborting build: place the "QtWebEngine with proprietary codecs" payload at the path above and re-run.'
    exit 1
  }
  Write-Info "Found required custom QtWebEngine payload at: $(Format-RepoPath $CustomPropCandidate)"
}

### SECTION: Prefix custom QtWebEngine
# Purpose: Copy the custom "QtWebEngine with proprietary codecs" payload
# located at `$CustomPropCandidate` (i.e. `.qt/<QtVersion>-prop-win-<arch>`)
# onto the `$QtRoot` directory (i.e. `build\windows-x64\qt-host\6.9.3\msvc2022_64`).
# This prepares a local host Qt kit populated with the proprietary WebEngine
# runtime and resources so packaging or local testing can use the proprietary
# codecs without modifying the system/global Qt installation.
# Behavior:
# - Recursively copies all files and subfolders from the payload, preserving
#   relative paths under `$QtRoot`.
# - Creates any missing destination directories.
# - Logs every file copied and indicates whether the destination file was
#   newly created (`Copied:`) or replaced (`Overwritten:`).
# - If the source payload is missing, the function logs a warning and returns.
# Note: This operation WILL overwrite existing files in `$QtRoot`; run with care.
# The function intentionally targets the local `$QtRoot` build area
# rather than a system-wide Qt installation.
function Prefix-CustomWebEngine {
  Write-Info "Prefix-CustomWebEngine: Copy `"QtWebEngine with proprietary codecs`" $(Format-RepoPath $CustomPropCandidate) onto $(Format-RepoPath $QtRoot)"

  # Source is $CustomPropCandidate ('.qt/<ver>-prop-win-<arch>')
  if (-not (Test-Path $CustomPropCandidate)) {
    Write-Warn "Prefix-CustomWebEngine: Custom payload not found at: $(Format-RepoPath $CustomPropCandidate); skipping"
    return
  }

  # Destination: host Qt root ($QtRoot). Prefer the host Qt used for building.
  if (-not $QtRoot -or -not (Test-Path $QtRoot)) {
    Write-Err "Prefix-CustomWebEngine: Host Qt root ($(Format-RepoPath $QtRoot)) not found. Aborting to avoid unintended copies."
    exit 1
  }

  $destRoot = $QtRoot

  $srcRootFull = (Get-Item $CustomPropCandidate).FullName.TrimEnd('\','/')
  $destRootFull = (Get-Item $destRoot).FullName.TrimEnd('\','/')

  Write-Info "Prefix-CustomWebEngine: Copying files from $(Format-RepoPath $srcRootFull) -> $(Format-RepoPath $destRootFull)"

  # Enumerate files and copy preserving relative paths; log each file and whether overwrote
  $allFiles = Get-ChildItem -Path $CustomPropCandidate -File -Recurse -ErrorAction SilentlyContinue
  foreach ($f in $allFiles) {
    try {
      $rel = $f.FullName.Substring($srcRootFull.Length).TrimStart('\','/')
    } catch {
      # fallback: use Path relative join
      $rel = $f.FullName
    }
    $destPath = Join-Path $destRootFull $rel
    $destDir = Split-Path -Parent $destPath
    if (-not (Test-Path $destDir)) {
      New-Item -ItemType Directory -Force -Path $destDir | Out-Null
    }

    $didExist = Test-Path $destPath
    try {
      Copy-Item -Path $f.FullName -Destination $destPath -Force -ErrorAction Stop
      if ($didExist) {
        Write-Info "Overwritten: $(Format-RepoPath $destPath) <= $(Format-RepoPath $f.FullName)"
      } else {
        Write-Info "Copied:      $(Format-RepoPath $destPath) <= $(Format-RepoPath $f.FullName)"
      }
    } catch {
      Write-Warn "Failed to copy $(Format-RepoPath $f.FullName) -> $(Format-RepoPath $destPath): $_"
    }
  }

  Write-Info "Prefix-CustomWebEngine: finished applying custom QtWebEngine payload"
}

### SECTION: Configure & Build
function Configure-CMake {
  $generator = 'Ninja Multi-Config'
  Write-Info "Configuring CMake generator: $generator"

  $cacheFile = Join-Path $BuildDir 'CMakeCache.txt'
  $cmakeFilesDir = Join-Path $BuildDir 'CMakeFiles'
  if (Test-Path $cacheFile) {
    Write-Info 'Removing stale CMakeCache.txt'
    Remove-Item -Force $cacheFile
  }
  if (Test-Path $cmakeFilesDir) {
    Write-Info 'Removing stale CMakeFiles directory'
    Remove-Item -Recurse -Force $cmakeFilesDir
  }

  $cmakeArgs = @('-S', '.', '-B', $(Format-RepoPath $BuildDir), '-G', $generator)
  if ($QtRoot) {
    $cmakeArgs += @('-DCMAKE_PREFIX_PATH=' + $QtRoot)
  }
  if ($VSToolset) {
    $cmakeArgs += @('-T', $VSToolset)
  }
  Write-Info "Running: cmake $($cmakeArgs -join ' ')"
  cmake @cmakeArgs
}

function Build-Project {
  Write-Info "Building configuration: $Config"
  cmake --build $BuildDir --config $Config

  # Determine produced executable
  # Try common Ninja Multi-Config output paths
  $exeCandidates = @(
    (Join-Path $BuildDir ("$Config\Phraims.exe")),
    (Join-Path $BuildDir ("Phraims.dir\$Config\Phraims.exe")),
    (Join-Path $BuildDir ("Release\Phraims.exe")),
    (Join-Path $BuildDir ("RelWithDebInfo\Phraims.exe"))
  )
  $script:exe = $exeCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
  if ($exe) {
    Write-Info "Build succeeded: $(Format-RepoPath $exe)"
  } else {
    Write-Warn "Build completed but executable not found. Checked: $($exeCandidates -join ', ')"
  }
}

### SECTION: Qt Platform Specific Deployment
# Run windeployqt to collect Qt runtime files so the exe can run standalone
function Run-QtPlatformDeployment {
  param($exePath)
  $qtBin = $null
  if ($QtRoot) {
    $qtBin = Join-Path $QtRoot 'bin'
  } else {
    $qtBin = Get-Command windeployqt.exe -ErrorAction SilentlyContinue | ForEach-Object { Split-Path -Parent $_.Source }
  }
  $windeploy = Join-Path $qtBin 'windeployqt.exe'
  if (-not (Test-Path $windeploy)) {
    Write-Warn "windeployqt.exe not found under $qtBin; skipping deployment"
    return $null
  }

  $deployDir = Join-Path (Split-Path -Parent $exePath) 'deploy'
  New-Item -ItemType Directory -Force -Path $deployDir | Out-Null

  Write-Info "Running windeployqt for $(Format-RepoPath $exePath) -> $(Format-RepoPath $deployDir)"
  # Use Start-Process so windeployqt stdout/stderr are written to the console but not captured as function output
  $args = @('--release','--dir',$deployDir,$exePath)
  Start-Process -FilePath $windeploy -ArgumentList $args -NoNewWindow -Wait

  # Copy the exe into the deploy dir root for convenience
  Copy-Item -Force -Path $exePath -Destination (Join-Path $deployDir (Split-Path $exePath -Leaf)) | Out-Null

  Write-Info "Deployment contains windeployqt output only."

  return $deployDir
}

### SECTION: Materialize bundle symlinks
function Materialize-BundleSymlinks {
  Write-Info "Materialize-BundleSymlinks: not applicable on Windows (placeholder)"
}

### SECTION: Sync QtWebEngine payload
function Sync-WebEnginePayload {
  Write-Info "Verify-WebEnginePayload: not applicable on Windows [if `Prefix-CustomWebEngine` did its job] (placeholder)"
}

### SECTION: Fix rpaths
function Fix-RPaths {
  Write-Info "Fix-RPaths: not applicable on Windows [if `Prefix-CustomWebEngine` did its job] (placeholder)"
}

### SECTION: Validate bundle links
function Validate-BundleLinks {
  Write-Info "Validate-BundleLinks: not applicable on Windows [if `Prefix-CustomWebEngine` did its job] (placeholder)"
}

### SECTION: Verify QtWebEngine payload
function Verify-WebEnginePayload {
  Write-Info "Verify-WebEnginePayload: not applicable on Windows [if `Prefix-CustomWebEngine` did its job] (placeholder)"
}

### SECTION: Signing
function Adhoc-SignBundle {
  Write-Info "Ad-hoc signing: not implemented on Windows (placeholder)"
}

### SECTION: Create installer
# Optionally produce an NSIS installer (require winget to install NSIS if needed)
function Create-Installer {
  param([string]$DeployDir)
  if (-not $DeployDir) {
    Write-Warn 'No deploy directory found; skipping installer generation'
    return
  }

  $installerExe = Join-Path $BuildDir ("Phraims-Installer-$Arch-$Config.exe")
  $nsiFile = Join-Path $BuildDir 'phraims-installer.nsi'

  # Check for makensis; if missing, require winget to install it. No portable-zip fallback.
  $makensis = Get-Command makensis.exe -ErrorAction SilentlyContinue
  if (-not $makensis) {
    $winget = Get-Command winget -ErrorAction SilentlyContinue
    if (-not $winget) {
      Write-Err 'makensis not found and winget is not available. Install NSIS (makensis.exe) or enable winget.'
      return
    }

    Write-Info 'Attempting to install NSIS via winget (may require elevated permissions)'
    try {
      & $winget.Source @('install','-e','--id','NSIS.NSIS','--accept-package-agreements','--accept-source-agreements')
      Start-Sleep -Seconds 1
    } catch {
      Write-Err "winget failed to install NSIS: $_"
      return
    }

    # Re-check for makensis in PATH and common locations
    $makensis = Get-Command makensis.exe -ErrorAction SilentlyContinue
    if (-not $makensis) {
      $possiblePaths = @(
        "${env:ProgramFiles}\NSIS\makensis.exe",
        "${env:ProgramFiles(x86)}\NSIS\makensis.exe"
      )
      foreach ($p in $possiblePaths) {
        if (Test-Path $p) {
          $makensis = Get-Command $p -ErrorAction SilentlyContinue
          break
        }
      }
    }
    if (-not $makensis) {
      Write-Err 'makensis still not found after winget install. Install NSIS manually and re-run.'
      return
    }
  }

  Write-Info "Using makensis at: $($makensis.Source) to generate NSIS installer"

  # Use Windows-style backslashes for NSIS file patterns and append wildcard
  $deployForNSIS = $DeployDir.TrimEnd('\')

  $nsiTemplate = @'
Name "Phraims"
OutFile "__OUTFILE__"
InstallDir "$PROGRAMFILES\Phraims"
RequestExecutionLevel admin

Section "Install"
  SetOutPath "$INSTDIR"
  File /r "__DEPLOY__"
  CreateDirectory "$SMPROGRAMS\Phraims"
  CreateShortCut "$SMPROGRAMS\Phraims\Phraims.lnk" "$INSTDIR\Phraims.exe"
  # Write an uninstaller so the Uninstall section will be emitted correctly
  WriteUninstaller "$INSTDIR\Uninstall.exe"
SectionEnd

Section "Uninstall"
  # Remove application files
  Delete "$INSTDIR\Phraims.exe"
  # Remove the uninstaller we wrote during install
  Delete "$INSTDIR\Uninstall.exe"
  RMDir /r "$INSTDIR"
  Delete "$SMPROGRAMS\Phraims\Phraims.lnk"
  RMDir "$SMPROGRAMS\Phraims"
SectionEnd
'@

  # Replace placeholders with the actual installer path and deploy filespec (deploy path is expanded, NSIS variables remain literal)
  $deployFilespec = ($deployForNSIS + '\\*.*') -replace '\\+','\\'
  $nsi = $nsiTemplate -replace '__OUTFILE__', $installerExe -replace '__DEPLOY__', $deployFilespec
  Set-Content -Path $nsiFile -Value $nsi -Encoding ASCII
  & "$($makensis.Source)" $nsiFile
  if ($LASTEXITCODE -eq 0) {
    Write-Info "Installer created: $(Format-RepoPath $installerExe)"
  } else {
    Write-Warn "makensis failed (exit $LASTEXITCODE)"
  }
}

### SECTION: Main orchestration
function Main {
  Initialize-Environment

  Require-CustomWebEngine
  Prefix-CustomWebEngine

  Configure-CMake
  Build-Project
  if (-not $exe) {
    Write-Error "No built executable found; exiting"
    exit 1
  }
  
  $deploy = Run-QtPlatformDeployment -exePath $exe
  # If we have a deploy folder, use it; otherwise, fallback to exe directory
  if ($deploy) {
    $target = $deploy
    Write-Info "Deployed application files to: $(Format-RepoPath $target)"
  } elseif ($exe) {
    $target = Split-Path -Parent $exe
    Write-Warn "windeployqt did not produce a deploy folder; applying proprietary payload next to exe at $(Format-RepoPath $target)"
  } else {
    Write-Warn "No deploy nor exe available for post-deploy sync; exiting"
    exit 1
  }
  Materialize-BundleSymlinks
  Sync-WebEnginePayload -DeployDir $target -PayloadRoot $CustomPropCandidate
  Fix-RPaths
  Validate-BundleLinks
  Verify-WebEnginePayload -DeployDir $target

  Adhoc-SignBundle

  Create-Installer -DeployDir $deploy
}

Main
