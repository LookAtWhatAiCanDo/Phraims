# Downloads proprietary-codec QtWebEngine artifacts for Windows (x64/arm64) from
# the private LookAtWhatAiCanDo/QtWebEngineProprietaryCodecs repo and builds +
# deploys Phraims. Mirrors the macOS flow: validate proprietary prefix, resolve
# host Qt (via aqtinstall when needed), configure with Ninja inside a VS2022
# dev shell, run windeployqt, and materialize the WebEngine payload in-place.
param(
  [string[]]$Arch = ${env:QTWEBENGINE_ARCH} ? ${env:QTWEBENGINE_ARCH}.Split(",") : @("x64","arm64"),
  [string]$QtWebEngineVer = ${env:QTWEBENGINE_VER} ? ${env:QTWEBENGINE_VER} : "6.9.3",
  [string]$QtHostPrefix = ${env:QT_HOST_PREFIX},
  [int]$Debug = ${env:DEBUG} ? [int]${env:DEBUG} : 0
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")

function Step($msg) {
  Write-Host ""
  Write-Host "==> $msg"
}

function DebugLog($msg) {
  if ($Debug -gt 0) {
    Write-Host "[debug] $msg"
  }
}

function Ensure-Tool($name, $chocoName) {
  if (-not (Get-Command $name -ErrorAction SilentlyContinue)) {
    Step "Installing $name via Chocolatey"
    choco install $chocoName -y --no-progress | Out-Host
  } else {
    DebugLog "$name already available"
  }
}

function Ensure-Aqt {
  Step "Ensuring aqtinstall"
  python -m pip install --upgrade aqtinstall | Out-Host
}

function Ensure-VsDevCmd {
  Step "Locating vsdevcmd.bat"
  $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
  if (-not (Test-Path $vswhere)) { throw "vswhere.exe not found at $vswhere" }
  $vsInstall = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
  if (-not $vsInstall) { throw "Visual Studio installation not found" }
  $vsDevCmd = Join-Path $vsInstall "Common7\Tools\VsDevCmd.bat"
  if (-not (Test-Path $vsDevCmd)) { throw "VsDevCmd not found at $vsDevCmd" }
  return $vsDevCmd
}

function Get-HostPrefix($archVal) {
  $archLower = $archVal.ToLower()
  if ($QtHostPrefix) {
    if (-not (Test-Path $QtHostPrefix)) { throw "QT_HOST_PREFIX does not exist: $QtHostPrefix" }
    return $QtHostPrefix
  }
  $base = Join-Path $env:RUNNER_TEMP "qt"
  $dest = if ($archLower -eq "arm64") { Join-Path $base "$QtWebEngineVer\msvc2022_arm64" } else { Join-Path $base "$QtWebEngineVer\msvc2022_64" }
  if (-not (Test-Path (Join-Path $dest "bin\qmake.exe"))) {
    Ensure-Aqt
    $spec = if ($archLower -eq "arm64") { "win64_msvc2022_arm64" } else { "win64_msvc2022_64" }
    aqt install-qt windows desktop $QtWebEngineVer $spec --modules qtwebchannel qtpositioning qtvirtualkeyboard --outputdir $base
  } else {
    DebugLog "Host Qt already present at $dest"
  }
  return $dest
}

function Require-CustomWebEngine($prefix) {
  $cfg = Join-Path $prefix "lib\cmake\Qt6WebEngineWidgets\Qt6WebEngineWidgetsConfig.cmake"
  $dll = Join-Path $prefix "bin\Qt6WebEngineCore.dll"
  if (-not (Test-Path $cfg) -or -not (Test-Path $dll)) {
    throw "QtWebEngine prefix not found or incomplete at $prefix (missing $cfg or $dll). Run ci/get-qtwebengine-windows.ps1 first."
  }
}

function Invoke-InVsEnv($vsDevCmd, $devArch, $command) {
  $cmdLine = "`"$vsDevCmd`" -arch=$devArch -host_arch=amd64 && $command"
  cmd /c $cmdLine
  if ($LASTEXITCODE -ne 0) {
    throw "Command failed (arch=$devArch): $command"
  }
}

function Sync-WebEnginePayload($prefix, $packageDir) {
  Step "Syncing QtWebEngine resources"
  $resourceSrc = Join-Path $prefix "resources"
  $resourceDest = Join-Path $packageDir "resources"
  if (-not (Test-Path $resourceSrc)) {
    throw "Missing QtWebEngine resources under $resourceSrc"
  }
  New-Item -ItemType Directory -Force -Path $resourceDest | Out-Null
  Copy-Item -Path (Join-Path $resourceSrc "*") -Destination $resourceDest -Recurse -Force

  $helperSrc = Join-Path $prefix "bin\QtWebEngineProcess.exe"
  if (-not (Test-Path $helperSrc)) {
    throw "QtWebEngineProcess.exe missing under $prefix\bin"
  }
  Copy-Item $helperSrc (Join-Path $packageDir "QtWebEngineProcess.exe") -Force
}

function Validate-WebEnginePayload($packageDir, $archLower) {
  Step "Validating packaged QtWebEngine payload"
  $required = @(
    "QtWebEngineProcess.exe",
    "resources\icudtl.dat",
    "resources\qtwebengine_devtools_resources.pak",
    "resources\qtwebengine_resources.pak",
    "resources\qtwebengine_resources_100p.pak",
    "resources\qtwebengine_resources_200p.pak"
  )
  $snapshotCandidates = @("resources\v8_context_snapshot.bin", "resources\v8_context_snapshot.$archLower.bin")
  $snapshot = $snapshotCandidates | Where-Object { Test-Path (Join-Path $packageDir $_) } | Select-Object -First 1
  if (-not $snapshot) {
    throw "Missing v8_context_snapshot.*.bin in $packageDir\resources"
  }
  foreach ($rel in $required) {
    $full = Join-Path $packageDir $rel
    if (-not (Test-Path $full)) {
      throw "Missing WebEngine resource: $full"
    }
  }
  $locales = Join-Path $packageDir "resources\qtwebengine_locales"
  if (-not (Test-Path $locales)) {
    throw "Missing WebEngine locales at $locales"
  }
  DebugLog "WebEngine payload validated (snapshot: $snapshot)"
}

function Invoke-Windeploy($qtHost, $packageExe, $packageDir) {
  Step "Running windeployqt"
  $windeploy = Join-Path $qtHost "bin\windeployqt.exe"
  if (-not (Test-Path $windeploy)) { throw "windeployqt not found at $windeploy" }
  $args = @("--dir", $packageDir, "--release", "--webengine", "--no-compiler-runtime", $packageExe)
  if ($Debug -gt 0) { $args += "--verbose" }
  & $windeploy @args | Out-Host
  if ($LASTEXITCODE -ne 0) { throw "windeployqt failed" }
}

function Build-One($archVal) {
  $archLower = $archVal.ToLower()
  $outputDir = Join-Path $repoRoot ".qt\$QtWebEngineVer-prop-win-$archLower"
  $buildDir = Join-Path $repoRoot "build\windows-$archLower"
  $packageDir = Join-Path $buildDir "Phraims-Windows-$archLower"
  $vsDevCmd = Ensure-VsDevCmd
  Ensure-Tool "cmake" "cmake"
  Ensure-Tool "ninja" "ninja"

  Step "Preparing build for $archLower"
  Require-CustomWebEngine $outputDir
  $qtHost = Get-HostPrefix $archLower
  Write-Host "  Proprietary QtWebEngine: $outputDir"
  Write-Host "  Host Qt: $qtHost"

  New-Item -ItemType Directory -Force -Path $buildDir | Out-Null
  if (Test-Path (Join-Path $buildDir "CMakeCache.txt")) { Remove-Item (Join-Path $buildDir "CMakeCache.txt") -Force }

  $cmakePrefix = "$outputDir;$qtHost"
  $qt6Dir = Join-Path $qtHost "lib\cmake\Qt6"
  $qtWebEngineWidgetsDir = Join-Path $outputDir "lib\cmake\Qt6WebEngineWidgets"
  $devCmdArch = if ($archLower -eq "arm64") { "arm64" } else { "amd64" }
  $pathSuffix = "$($outputDir)\bin;$($qtHost)\bin"

  Step "Configuring CMake (Release, Ninja)"
  $cmakeCmd = "set `"PATH=%PATH%;$pathSuffix`" && set `"CMAKE_PREFIX_PATH=$cmakePrefix`" && set `"Qt6_DIR=$qt6Dir`" && set `"Qt6WebEngineWidgets_DIR=$qtWebEngineWidgetsDir`" && cmake -S `"$repoRoot`" -B `"$buildDir`" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_SYSTEM_PROCESSOR=$archLower -DQT_NO_CREATE_VERSIONLESS_FUNCTIONS=OFF -DQt6_DIR=`"$qt6Dir`" -DQt6WebEngineWidgets_DIR=`"$qtWebEngineWidgetsDir`""
  Invoke-InVsEnv $vsDevCmd $devCmdArch $cmakeCmd

  Step "Building Phraims ($archLower)..."
  $buildCmd = "set `"PATH=%PATH%;$pathSuffix`" && cmake --build `"$buildDir`" --config Release -- -v"
  Invoke-InVsEnv $vsDevCmd $devCmdArch $buildCmd

  $exePath = Join-Path $buildDir "Phraims.exe"
  if (-not (Test-Path $exePath)) {
    $exePath = Join-Path $buildDir "Release\Phraims.exe"
  }
  if (-not (Test-Path $exePath)) {
    throw "Phraims.exe not found after build in $buildDir"
  }

  if (Test-Path $packageDir) { Remove-Item $packageDir -Recurse -Force }
  New-Item -ItemType Directory -Force -Path $packageDir | Out-Null
  $packageExe = Join-Path $packageDir "Phraims.exe"
  Copy-Item $exePath $packageExe -Force

  Invoke-Windeploy $qtHost $packageExe $packageDir
  Sync-WebEnginePayload $outputDir $packageDir
  Validate-WebEnginePayload $packageDir $archLower

  Step "Done ($archLower). Packaged output in $packageDir"
}

foreach ($a in $Arch) {
  Build-One $a
}
