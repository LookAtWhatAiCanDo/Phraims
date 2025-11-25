#Requires -Version 5.1
<#
.SYNOPSIS
  Build QtWebEngine with proprietary codecs on Windows for a given architecture.

.PARAMETER Arch
  Target architecture: x64 or arm64. Defaults to $env:QT_ARCH or x64.

.PARAMETER QTWEBENGINE_VER
  QtWebEngine/Qt version to build. Defaults to $env:QTWEBENGINE_VER or 6.9.3.

.PARAMETER QT_HOST_DIR
  Root directory for host Qt installs (aqtinstall layout). Defaults to .qt/host.

.PARAMETER QT_WEBENGINE_PROP_PREFIX
  Install prefix for the built QtWebEngine. Defaults to .qt/<ver>-prop-win-<arch>.
#>
param(
  [string]$Arch = ${env:QT_ARCH} ? ${env:QT_ARCH} : "x64",
  [string]$QTWEBENGINE_VER = ${env:QTWEBENGINE_VER} ? ${env:QTWEBENGINE_VER} : "6.9.3",
  [string]$QT_HOST_DIR = ${env:QT_HOST_DIR},
  [string]$QT_WEBENGINE_PROP_PREFIX = ${env:QT_WEBENGINE_PROP_PREFIX}
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$repoRoot = Resolve-Path (Join-Path $scriptDir "..")

# Arch-specific settings
switch ($Arch.ToLower()) {
  "x64" {
    $qtHostSpec = "win64_msvc2022_64"
    $devCmdArch = "amd64"
    if (-not $QT_WEBENGINE_PROP_PREFIX) { $QT_WEBENGINE_PROP_PREFIX = Join-Path $repoRoot ".qt/$QTWEBENGINE_VER-prop-win64" }
    $buildDir = Join-Path $repoRoot "build/qtwebengine-win64"
  }
  "arm64" {
    $qtHostSpec = "win64_msvc2022_arm64"
    $devCmdArch = "arm64"
    if (-not $QT_WEBENGINE_PROP_PREFIX) { $QT_WEBENGINE_PROP_PREFIX = Join-Path $repoRoot ".qt/$QTWEBENGINE_VER-prop-win-arm64" }
    $buildDir = Join-Path $repoRoot "build/qtwebengine-win-arm64"
  }
  default { throw "Unsupported Arch '$Arch'. Use x64 or arm64." }
}

if (-not $QT_HOST_DIR) {
  $QT_HOST_DIR = Join-Path $repoRoot ".qt/host"
}

$qtHostPath = Join-Path $QT_HOST_DIR "$QTWEBENGINE_VER/$qtHostSpec"
$srcBase = Join-Path $repoRoot "3rdparty/qtwebengine-everywhere-src-$QTWEBENGINE_VER"
$srcArchive = "$srcBase.zip"

Write-Host "==> Repo root: $repoRoot"
Write-Host "==> Arch: $Arch (devcmd: $devCmdArch)"
Write-Host "==> Qt version: $QTWEBENGINE_VER"
Write-Host "==> Host Qt: $qtHostPath"
Write-Host "==> Prefix: $QT_WEBENGINE_PROP_PREFIX"
Write-Host "==> Build dir: $buildDir"

if (-not (Get-Command python -ErrorAction SilentlyContinue)) {
  throw "python is required on PATH"
}

Write-Host "==> Ensuring aqtinstall + ninja"
python -m pip install --upgrade aqtinstall ninja | Out-Host

if (-not (Test-Path (Join-Path $qtHostPath "bin"))) {
  Write-Host "==> Installing host Qt ($qtHostSpec) via aqtinstall"
  python -m aqt install-qt windows desktop $QTWEBENGINE_VER $qtHostSpec --modules qtwebchannel qtpositioning --outputdir $QT_HOST_DIR
}

if (-not (Test-Path $srcBase)) {
  Write-Host "==> Fetching QtWebEngine source $QTWEBENGINE_VER"
  $url = "https://download.qt.io/official_releases/qt/$($QTWEBENGINE_VER.Substring(0,$QTWEBENGINE_VER.LastIndexOf('.')))/$QTWEBENGINE_VER/submodules/qtwebengine-everywhere-src-$QTWEBENGINE_VER.zip"
  Invoke-WebRequest -Uri $url -OutFile $srcArchive
  Expand-Archive -Path $srcArchive -DestinationPath (Split-Path $srcBase)
}

if (-not (Test-Path $srcBase)) {
  throw "Source directory $srcBase not found"
}

Write-Host "==> Locating vsdevcmd"
$vswhere = Join-Path "${env:ProgramFiles(x86)}" "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { throw "vswhere.exe not found at $vswhere" }
$vsInstall = & $vswhere -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsInstall) { throw "Visual Studio installation not found" }
$vsDevCmd = Join-Path $vsInstall "Common7\Tools\VsDevCmd.bat"
if (-not (Test-Path $vsDevCmd)) { throw "VsDevCmd not found at $vsDevCmd" }

Write-Host "==> Configuring + building QtWebEngine"
New-Item -ItemType Directory -Force -Path $buildDir, $QT_WEBENGINE_PROP_PREFIX | Out-Null

$cmakeArgs = @(
  "-S", $srcBase,
  "-B", $buildDir,
  "-G", "Ninja",
  "-DCMAKE_PREFIX_PATH=$qtHostPath",
  "-DCMAKE_INSTALL_PREFIX=$QT_WEBENGINE_PROP_PREFIX",
  "-DCMAKE_STAGING_PREFIX=$QT_WEBENGINE_PROP_PREFIX",
  "-DFEATURE_webengine_proprietary_codecs=ON",
  "-DFEATURE_webengine_kerberos=ON",
  "-DFEATURE_webengine_native_spellchecker=ON"
)

$cmd = '"' + $vsDevCmd + '" -arch=' + $devCmdArch + ' && ' +
  'cmake ' + ($cmakeArgs -join ' ') + ' && ' +
  'cmake --build "' + $buildDir + '" && ' +
  'cmake --install "' + $buildDir + '"'

cmd /c $cmd

Write-Host "==> Done. Prefix: $QT_WEBENGINE_PROP_PREFIX"
