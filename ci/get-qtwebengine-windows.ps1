# Downloads the proprietary-codec QtWebEngine artifact for Windows
# from the private LookAtWhatAiCanDo/QtWebEngineProprietaryCodecs repo.
# Usage: powershell -File ci/get-qtwebengine-windows.ps1 -QTWEBENGINE_VER 6.9.3 -QTWEBENGINE_ARCH x64 -OUTPUT_DIR ".qt\6.9.3-prop-win-x64"
param(
  [Parameter(Mandatory = $true)][string]$QTWEBENGINE_VER,
  [Parameter(Mandatory = $true)][ValidateSet("x64","arm64")][string]$QTWEBENGINE_ARCH,
  [Parameter(Mandatory = $true)][string]$OUTPUT_DIR,
  [string]$QTWEBENGINE_TOKEN = ${env:QTWEBENGINE_TOKEN}
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if (-not ${QTWEBENGINE_TOKEN}) { throw "Set QTWEBENGINE_TOKEN in the environment (PAT with actions:read on the private repo)." }

# Logic begin

$OWNER = "LookAtWhatAiCanDo"
$REPO = "QtWebEngineProprietaryCodecs"
$ARTIFACT_NAME = "qtwebengine-windows-${QTWEBENGINE_VER}-${QTWEBENGINE_ARCH}"

Write-Host "==> Artifact: ${ARTIFACT_NAME}"
Write-Host "==> Output: ${OUTPUT_DIR}"

$ARTIFACT_ID = curl -sSL `
  -H "Authorization: Bearer ${QTWEBENGINE_TOKEN}" `
  -H "X-GitHub-Api-Version: 2022-11-28" `
  "https://api.github.com/repos/${OWNER}/${REPO}/actions/artifacts?name=${ARTIFACT_NAME}" |
  python -c "import sys,json; arts=json.load(sys.stdin).get('artifacts', []); print(arts[0]['id'] if arts else '')"
Write-Host "==> Artifact ID: ${ARTIFACT_ID}"
if (-not ${ARTIFACT_ID}) {
  throw "Artifact not found: ${ARTIFACT_NAME}"
}
New-Item -ItemType Directory -Force -Path ${OUTPUT_DIR} | Out-Null
$ZIP_PATH = Join-Path ${OUTPUT_DIR} "${ARTIFACT_NAME}.zip"
if (Test-Path -Path ${ZIP_PATH} -PathType Leaf) {
  Write-Host "==> Using existing zip at ${ZIP_PATH}"
} else {
  Write-Host "==> Downloading artifact to ${ZIP_PATH} ..."
  curl -L `
    -H "Authorization: Bearer ${QTWEBENGINE_TOKEN}" `
    -H "X-GitHub-Api-Version: 2022-11-28" `
    -o ${ZIP_PATH} `
    "https://api.github.com/repos/${OWNER}/${REPO}/actions/artifacts/${ARTIFACT_ID}/zip"
}
Write-Host "==> Unzipping artifact to ${OUTPUT_DIR} ..."
Expand-Archive -Path ${ZIP_PATH} -DestinationPath ${OUTPUT_DIR} -Force
Write-Host "==> Contents of ${OUTPUT_DIR}:"
Get-ChildItem ${OUTPUT_DIR} | Format-List Name,FullName
