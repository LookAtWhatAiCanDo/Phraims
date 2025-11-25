#!/usr/bin/env bash
set -eo pipefail

#######################################################################################
# Build Qt WebEngine $QTWEBENGINE_VER **with proprietary codecs** (H.264/AAC) on macOS.
#######################################################################################
QTWEBENGINE_VER="${QTWEBENGINE_VER:-6.9.3}"

QT_SERIES="${QTWEBENGINE_VER%.*}" # ex "6.9" from "6.9.3"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

TAR_BASE="qtwebengine-everywhere-src-${QTWEBENGINE_VER}"
TAR_NAME="${TAR_BASE}.tar.xz"
DOWNLOAD_URL="https://download.qt.io/official_releases/qt/${QT_SERIES}/${QTWEBENGINE_VER}/submodules/${TAR_NAME}"

QTWEBENGINE_SRC="${REPO_ROOT}/3rdparty/${TAR_BASE}"
MACOS_ARCH="${MACOS_ARCH:-$(uname -m)}"
QTWEBENGINE_BUILD="${REPO_ROOT}/build/qtwebengine-macos-${MACOS_ARCH}"
QT_PROP_PREFIX_DEFAULT="${REPO_ROOT}/.qt/${QTWEBENGINE_VER}-prop-macos-${MACOS_ARCH}"
QT_PROP_PREFIX="${QT_PROP_PREFIX:-${QT_WEBENGINE_PROP_PREFIX:-${QT_PROP_PREFIX_DEFAULT}}}"
QT_WEBENGINE_PROP_PREFIX="${QT_PROP_PREFIX}"

###############################################################################
# Resolve host Qt (prefer Homebrew)
###############################################################################
QT_HOST_PATH="${QT_HOST_PATH:-}"

if [[ -z "${QT_HOST_PATH}" ]]; then
  echo "==> QT_HOST_PATH not set; trying Homebrew Qt…"
  if command -v brew >/dev/null 2>&1; then
    if QT_HP="$(brew --prefix qt 2>/dev/null)"; then
      QT_HOST_PATH="$QT_HP"
    elif QT_HP6="$(brew --prefix qt@6 2>/dev/null)"; then
      QT_HOST_PATH="$QT_HP6"
    else
      echo "ERROR: Could not find Homebrew Qt. Run: brew install qt"
      exit 1
    fi
  else
    echo "ERROR: Homebrew not found and QT_HOST_PATH is unset."
    exit 1
  fi
fi

if [[ "$(uname)" != "Darwin" ]]; then
  echo "ERROR: This script is macOS-only."
  exit 1
fi

###############################################################################
SYS_VER_RAW="$(sw_vers -productVersion | cut -d. -f1-2)"
SYS_MAJOR="${SYS_VER_RAW%%.*}"

if [[ "${SYS_MAJOR}" -gt 14 ]]; then
  MACOS_DEPLOY="14.0"
else
  MACOS_DEPLOY="${SYS_VER_RAW}.0"
fi

CMAKE_PLATFORM_FLAGS="-DCMAKE_OSX_ARCHITECTURES=${MACOS_ARCH} -DCMAKE_OSX_DEPLOYMENT_TARGET=${MACOS_DEPLOY}"

if [[ "${MACOS_ARCH}" = "arm64" ]]; then
  echo "==> Ensuring Metal toolchain is present…"
  xcodebuild -downloadComponent MetalToolchain
fi
#brew remove qtwebengine # We don't want the Homebrew QtWebEngine; we are building our own.
brew install qtwebchannel
brew install vulkan-headers

echo
echo "==> Qt Host Path      = ${QT_HOST_PATH}"
echo "==> QtWebEngine Ver   = ${QTWEBENGINE_VER}"
echo "==> Qt Series         = ${QT_SERIES}"
echo "==> Install Prefix    = ${QT_PROP_PREFIX}"
echo "==> Source Path       = ${QTWEBENGINE_SRC}"
echo "==> Build Path        = ${QTWEBENGINE_BUILD}"
echo "==> macOS Arch        = ${MACOS_ARCH}"
echo "==> Deploy Target     = ${MACOS_DEPLOY}"
echo "==> Download URL      = ${DOWNLOAD_URL}"
echo

###############################################################################
# Python venv + deps
###############################################################################
VENV_DIR="${REPO_ROOT}/.venv-qweb-macos"

if [[ ! -d "${VENV_DIR}" ]]; then
  echo "==> Creating Python venv: ${VENV_DIR}"
  python3 -m venv "${VENV_DIR}"
fi

# shellcheck disable=SC1090
source "${VENV_DIR}/bin/activate"

echo "==> Installing Python deps (html5lib, six, webencodings)…"
pip install --upgrade pip setuptools wheel >/dev/null
pip install html5lib six webencodings >/dev/null

# Compute PYTHONPATH from venv site-packages
PYTHONPATH="$(python << 'EOF'
import site
paths = []
if hasattr(site, "getsitepackages"):
    paths.extend(site.getsitepackages())
if hasattr(site, "getusersitepackages"):
    paths.append(site.getusersitepackages())
candidates = [p for p in paths if "site-packages" in p]
print(candidates[0] if candidates else "")
EOF
)"
export PYTHONPATH

###############################################################################
# Fetch QtWebEngine source
###############################################################################
mkdir -p "${REPO_ROOT}/3rdparty"

if [[ ! -d "${QTWEBENGINE_SRC}" ]]; then
  echo "==> Downloading QtWebEngine source…"
  cd "${REPO_ROOT}/3rdparty"

  if [[ ! -f "${TAR_NAME}" ]]; then
    curl -L -o "${TAR_NAME}" "${DOWNLOAD_URL}"
  fi

  echo "==> Extracting ${TAR_NAME}…"
  tar xf "${TAR_NAME}"
else
  echo "==> Using existing source directory."
fi

if [[ ! -d "${QTWEBENGINE_SRC}" ]]; then
  echo "ERROR: Source directory '${QTWEBENGINE_SRC}' not found after extraction."
  exit 1
fi

###############################################################################
# Apply Chromium toolchain patch (Homebrew-style)
###############################################################################
TOOLCHAIN_GNI="${QTWEBENGINE_SRC}/src/3rdparty/chromium/build/toolchain/apple/toolchain.gni"

if [[ -f "${TOOLCHAIN_GNI}" ]]; then
  if grep -q 'rebase_path("$clang_base_path/bin/", root_build_dir)' "${TOOLCHAIN_GNI}"; then
    echo "==> Patching toolchain.gni…"
    sed -i '' 's/rebase_path("$clang_base_path\/bin\/", root_build_dir)/""/' "${TOOLCHAIN_GNI}"
  else
    echo "==> toolchain.gni already patched or pattern not found."
  fi
else
  echo "WARNING: toolchain.gni not found at:"
  echo "  ${TOOLCHAIN_GNI}"
fi

###############################################################################
# Prepare build + install dirs
###############################################################################
echo "==> Preparing build and install directories…"
#rm -rf "${QTWEBENGINE_BUILD}"
mkdir -p "${QTWEBENGINE_BUILD}" "${QT_PROP_PREFIX}"

export CMAKE_PREFIX_PATH="${QT_HOST_PATH}"
export PATH="${QT_HOST_PATH}/bin:${PATH}"

if ! command -v ninja >/dev/null 2>&1; then
  echo "ERROR: ninja not found. Run: brew install ninja"
  exit 1
fi

###############################################################################
# Configure
###############################################################################
cd "${QTWEBENGINE_BUILD}"

echo
echo "==> Configuring QtWebEngine ${QTWEBENGINE_VER}…"

cmake "${QTWEBENGINE_SRC}" \
  -G Ninja \
  -DCMAKE_PREFIX_PATH="${CMAKE_PREFIX_PATH}" \
  -DCMAKE_INSTALL_PREFIX="${QT_PROP_PREFIX}" \
  -DCMAKE_STAGING_PREFIX="${QT_PROP_PREFIX}" \
  ${CMAKE_PLATFORM_FLAGS} \
  -DFEATURE_webengine_proprietary_codecs=ON \
  -DFEATURE_webengine_kerberos=ON \
  -DFEATURE_webengine_native_spellchecker=ON \
  -DNinja_EXECUTABLE="$(command -v ninja)" \
  -DQT_NO_APPLE_SDK_AND_XCODE_CHECK=ON

###############################################################################
# Build + Install
###############################################################################
echo
echo "==> Building QtWebEngine ${QTWEBENGINE_VER}… (this will take a while)"
ninja

echo
echo "==> Installing into ${QT_PROP_PREFIX}…"
ninja install

echo
echo "✔ SUCCESS: QtWebEngine ${QTWEBENGINE_VER} **with proprietary codecs** is ready."
echo "✔ Install prefix: ${QT_PROP_PREFIX}"
echo
echo "To build Phraims against this QtWebEngine:"
echo "  mkdir -p build/phraims"
echo "  cmake -S . -B build/phraims -DCMAKE_PREFIX_PATH=\"${QT_PROP_PREFIX}\""
echo "  cmake -B build/phraims --parallel"
