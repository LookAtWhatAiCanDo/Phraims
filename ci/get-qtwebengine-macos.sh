#!/usr/bin/env bash
# Usage: ./ci/get-qtwebengine-macos.sh <QTWEBENGINE_VER> <QTWEBENGINE_ARCH> <OUTPUT_DIR>
# Example: ./ci/get-qtwebengine-macos.sh 6.9.3 arm64 .qt/6.9.3-prop-macos-arm64
set -euo pipefail

if [ -z "${QTWEBENGINE_TOKEN:-}" ]; then
  echo "Set QTWEBENGINE_TOKEN in the environment (PAT with actions:read on the private repo)" >&2
  exit 1
fi
if [ $# -lt 3 ]; then
  echo "Usage: $0 <QTWEBENGINE_VER> <QTWEBENGINE_ARCH> <OUTPUT_DIR>" >&2
  exit 1
fi

QTWEBENGINE_VER="$1"
QTWEBENGINE_ARCH="$2"
OUTPUT_DIR="$3"

# Logic begin

OWNER="LookAtWhatAiCanDo"
REPO="QtWebEngineProprietaryCodecs"
ARTIFACT_NAME="qtwebengine-macos-${QTWEBENGINE_VER}-${QTWEBENGINE_ARCH}"

echo "==> Artifact: ${ARTIFACT_NAME}"
echo "==> Output: ${OUTPUT_DIR}"

ARTIFACT_ID=$(curl -sSL \
  -H "Authorization: Bearer ${QTWEBENGINE_TOKEN}" \
  -H "X-GitHub-Api-Version: 2022-11-28" \
  "https://api.github.com/repos/${OWNER}/${REPO}/actions/artifacts?name=${ARTIFACT_NAME}" \
  | python3 -c "import sys,json; arts=json.load(sys.stdin).get('artifacts', []); print(arts[0]['id'] if arts else '')")
echo "Artifact ID: ${ARTIFACT_ID}"
if [ -z "${ARTIFACT_ID}" ]; then
  echo "Artifact not found: ${ARTIFACT_NAME}" >&2
  exit 1
fi
mkdir -p "${OUTPUT_DIR}"
ZIP_PATH="${OUTPUT_DIR}/${ARTIFACT_NAME}.zip"
if [ -f "${ZIP_PATH}" ] && unzip -tq "${ZIP_PATH}" >/dev/null 2>&1; then
  echo "Using existing zip at ${ZIP_PATH}"
else
  echo "Downloading artifact to ${ZIP_PATH} ..."
  curl -L \
    -H "Authorization: Bearer ${QTWEBENGINE_TOKEN}" \
    -H "X-GitHub-Api-Version: 2022-11-28" \
    -o "${ZIP_PATH}" \
    "https://api.github.com/repos/${OWNER}/${REPO}/actions/artifacts/${ARTIFACT_ID}/zip"
fi
echo "==> Unzipping artifact to ${OUTPUT_DIR} ..."
unzip -o "${ZIP_PATH}" -d "${OUTPUT_DIR}"
echo "==> Contents of ${OUTPUT_DIR}:"
ls -la "${OUTPUT_DIR}"
