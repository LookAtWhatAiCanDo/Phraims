#!/usr/bin/env bash
# Bash build script for macOS Phraims app bundle and DMG creation.
#
# This file's logical sections are ordered and commented to mirror the
# macOS script (`ci/build-phraims-macos.sh`) so side-by-side diffs line up.
# Where a macOS section has no Windows equivalent, a no-op
# placeholder is provided so the structure stays aligned.

# Ensure errors stop execution
set -euo pipefail

### SECTION: Initialization
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
ARCH_NAME="${BUILD_ARCH:-arm64}"
BUILD_DIR="${BUILD_DIR:-${REPO_ROOT}/build/macos-${ARCH_NAME}}"
APP_PATH="${BUILD_DIR}/Phraims.app"
LOG_FILE="${BUILD_DIR}/macdeployqt.log"
STAGING_LIB_DIR="${BUILD_DIR}/lib"
QTWEBENGINE_VER="${QTWEBENGINE_VER:-6.9.3}"
QT_WEBENGINE_PROP_PREFIX="${QT_WEBENGINE_PROP_PREFIX:-${REPO_ROOT}/.qt/${QTWEBENGINE_VER}-prop-macos-${ARCH_NAME}}"
QT_MODULES=(qtbase qtdeclarative qtwebchannel qtpositioning qtvirtualkeyboard qtsvg brotli)

DEBUG="${DEBUG:-0}" # DEBUG=1 for verbose diagnostics
if [ "$DEBUG" -eq 2 ]; then
  set -x
fi

### SECTION: Utilities
step() { printf "\n==> %s\n" "$*"; }
debug() {
  if [ "$DEBUG" -eq 1 ]; then
    printf "  [debug] %s\n" "$*"
  fi
  return 0
}

### SECTION: Initialize environment
initialize_environment() {
  step "Initializing environment"

  cd "${REPO_ROOT}"
  step "Repository root: ${REPO_ROOT} (arch=${ARCH_NAME})"

  # Ensure build dir exists
  mkdir -p "${BUILD_DIR}"

  # Ensure Metal toolchain is available for arm64 builds (needed by QtWebEngine/Chromium)
  if [ "${ARCH_NAME}" = "arm64" ]; then
    step "Ensuring Xcode Metal toolchain is installed (arm64)"
    xcodebuild -downloadComponent MetalToolchain
  fi

  ensure_homebrew

  # Ensure Sparkle framework is available for macOS auto-update (Sparkle)
  if ! ensure_sparkle; then
    echo "Warning: Sparkle framework not found or could not be installed automatically." >&2
    echo "Please install Sparkle (Homebrew formula 'sparkle' or place Sparkle.framework under /Library/Frameworks)" >&2
  fi

  ensure_hostqt qtbase qtdeclarative qtwebchannel qtpositioning qtvirtualkeyboard qtsvg qtserialport brotli ninja cmake
}

### SECTION: Ensure package manager
ensure_homebrew() {
  step "Ensuring Homebrew is installed"
  if ! command -v brew >/dev/null 2>&1; then
    step "Homebrew not found; installing..."
    /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
  fi

  if [ -x "/opt/homebrew/bin/brew" ]; then
    # Some environments block /bin/ps inside brew shellenv; ignore failures and fall back.
    local env_out
    env_out="$(/opt/homebrew/bin/brew shellenv 2>/dev/null || true)"
    if [ -n "${env_out}" ]; then
      eval "${env_out}"
    else
      debug "brew shellenv failed; falling back to PATH update only"
      export PATH="/opt/homebrew/bin:/opt/homebrew/sbin:${PATH}"
      export HOMEBREW_PREFIX="/opt/homebrew"
      export HOMEBREW_CELLAR="/opt/homebrew/Cellar"
      export HOMEBREW_REPOSITORY="/opt/homebrew"
    fi
  elif [ -x "/usr/local/bin/brew" ]; then
    local env_out
    env_out="$(/usr/local/bin/brew shellenv 2>/dev/null || true)"
    if [ -n "${env_out}" ]; then
      eval "${env_out}"
    else
      debug "brew shellenv failed; falling back to PATH update only"
      export PATH="/usr/local/bin:/usr/local/sbin:${PATH}"
      export HOMEBREW_PREFIX="/usr/local"
      export HOMEBREW_CELLAR="/usr/local/Cellar"
      export HOMEBREW_REPOSITORY="/usr/local"
    fi
  else
    echo "Homebrew installation not detected after install" >&2
    exit 1
  fi

  step "Updating Homebrew"
  brew update --quiet
}

### SECTION: Sparkle detection and copy (small, safe helpers)
find_sparkle_framework() {
  # Search common locations (limited depth) for Sparkle.framework
  local roots=("${BUILD_DIR}" "/opt/homebrew" "/usr/local" "$HOME/Library" "/Library" "/System/Library" )
  local r found
  for r in "${roots[@]}"; do
    [ -d "${r}" ] || continue
    found=$(find "${r}" -maxdepth 6 -type d -name 'Sparkle.framework' 2>/dev/null | head -n1 || true)
    if [ -n "${found}" ]; then
      SPARKLE_FRAMEWORK_PREFIX="${found}"
      return 0
    fi
  done
  return 1
}

ensure_sparkle() {
  step "Ensuring Sparkle.framework (detect & copy)"
  # If already present under repo Frameworks, done
  if [ -d "${REPO_ROOT}/Frameworks/Sparkle.framework" ]; then
    SPARKLE_FRAMEWORK_PREFIX="${REPO_ROOT}/Frameworks/Sparkle.framework"
    debug "Sparkle.framework already in repo Frameworks"
    return 0
  fi

  # Try to find an existing framework
  if find_sparkle_framework; then
    mkdir -p "${BUILD_DIR}/Frameworks"
    ditto "${SPARKLE_FRAMEWORK_PREFIX}" "${BUILD_DIR}/Frameworks/$(basename "${SPARKLE_FRAMEWORK_PREFIX}")"
    SPARKLE_FRAMEWORK_PREFIX="${BUILD_DIR}/Frameworks/$(basename "${SPARKLE_FRAMEWORK_PREFIX}")"
    step "Copied Sparkle.framework to ${SPARKLE_FRAMEWORK_PREFIX}"
    return 0
  fi

  # Attempt to install cask (non-fatal)
  if command -v brew >/dev/null 2>&1; then
    step "Installing Sparkle cask via Homebrew (if available)"
    brew tap homebrew/cask >/dev/null 2>&1 || true
    brew install --cask sparkle || true
  fi

  # Try detecting again after cask install
  if find_sparkle_framework; then
    mkdir -p "${BUILD_DIR}/Frameworks"
    ditto "${SPARKLE_FRAMEWORK_PREFIX}" "${BUILD_DIR}/Frameworks/$(basename "${SPARKLE_FRAMEWORK_PREFIX}")"
    SPARKLE_FRAMEWORK_PREFIX="${BUILD_DIR}/Frameworks/$(basename "${SPARKLE_FRAMEWORK_PREFIX}")"
    step "Copied Sparkle.framework to ${SPARKLE_FRAMEWORK_PREFIX}"
    return 0
  fi

  return 1
}

### SECTION: Host Qt acquisition
ensure_hostqt() {
  export HOMEBREW_NO_AUTO_UPDATE=1
  export HOMEBREW_NO_ENV_HINTS=1
  local packages=("$@")
  step "Ensuring Homebrew packages: ${packages[*]}"
  for pkg in "${packages[@]}"; do
    if ! brew list --versions "$pkg" >/dev/null 2>&1; then
      echo "  - installing $pkg"
      brew install "$pkg"
    else
      debug "$pkg already installed"
    fi
  done
}

### SECTION: Ensure custom QtWebEngine
require_custom_webengine() {
  local framework="${QT_WEBENGINE_PROP_PREFIX}/lib/QtWebEngineCore.framework/Versions/A/QtWebEngineCore"
  local cmake_cfg="${QT_WEBENGINE_PROP_PREFIX}/lib/cmake/Qt6WebEngineWidgets/Qt6WebEngineWidgetsConfig.cmake"
  if [ ! -f "$framework" ] || [ ! -f "$cmake_cfg" ]; then
    cat <<EOF
Custom QtWebEngine not found at ${QT_WEBENGINE_PROP_PREFIX}
Fetch a proprietary-codec QtWebEngine prefix from the private LookAtWhatAiCanDo/QtWebEngineProprietaryCodecs repo,
or set QT_WEBENGINE_PROP_PREFIX/QTWEBENGINE_VER to point at an existing install prefix.
EOF
    exit 1
  fi
}

### SECTION: Prefix custom QtWebEngine
prefix_custom_webengine() {
  QT_PREFIX="$(brew --prefix qtbase || brew --prefix qt6)"
  QT_DECLARATIVE_PREFIX="$(brew --prefix qtdeclarative)"
  QT_WEBCHANNEL_PREFIX="$(brew --prefix qtwebchannel)"
  QT_POSITIONING_PREFIX="$(brew --prefix qtpositioning)"
  QT_META_PREFIX="$(brew --prefix qt 2>/dev/null || true)"

  export PATH="${QT_PREFIX}/bin:${PATH}"
  local -a cmake_prefixes=("${QT_WEBENGINE_PROP_PREFIX}" "${QT_PREFIX}" "${QT_DECLARATIVE_PREFIX}" "${QT_WEBCHANNEL_PREFIX}" "${QT_POSITIONING_PREFIX}")
  if [ -n "${QT_META_PREFIX}" ] && [ -d "${QT_META_PREFIX}" ]; then
    cmake_prefixes+=("${QT_META_PREFIX}")
  fi
  CMAKE_PREFIX_PATH="$(IFS=';'; echo "${cmake_prefixes[*]}")"
  export CMAKE_PREFIX_PATH
}

### SECTION: Configure & Build
configure_cmake() {
  step "Configuring CMake (Release, Ninja)"
  rm -f "${BUILD_DIR}/CMakeCache.txt"
  cmake -S "${REPO_ROOT}" -B "${BUILD_DIR}" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="${CMAKE_PREFIX_PATH}" \
    -DQt6Quick_DIR="${QT_DECLARATIVE_PREFIX}/lib/cmake/Qt6Quick" \
    -DQt6QuickWidgets_DIR="${QT_DECLARATIVE_PREFIX}/lib/cmake/Qt6QuickWidgets" \
    -DQt6WebChannel_DIR="${QT_WEBCHANNEL_PREFIX}/lib/cmake/Qt6WebChannel" \
    -DQt6Qml_DIR="${QT_DECLARATIVE_PREFIX}/lib/cmake/Qt6Qml" \
    -DQt6Network_DIR="${QT_PREFIX}/lib/cmake/Qt6Network" \
    -DQt6Positioning_DIR="${QT_POSITIONING_PREFIX}/lib/cmake/Qt6Positioning" \
    -DQt6WebEngineWidgets_DIR="${QT_WEBENGINE_PROP_PREFIX}/lib/cmake/Qt6WebEngineWidgets" \
    -DCMAKE_OSX_ARCHITECTURES="${MACOS_ARCH:-$ARCH_NAME}"
}

build_project() {
  step "Building Phraims"
  cmake --build "${BUILD_DIR}" --config Release

  [ -d "${APP_PATH}" ] || { echo "Expected app bundle at ${APP_PATH}" >&2; exit 1; }
}

### SECTION: Patch Sparkle rpath before macdeployqt
patch_sparkle_rpath() {
  # Some tools (including macdeployqt) can choke on unresolved @rpath entries
  # for third-party frameworks like Sparkle. Before running macdeployqt, make
  # sure the main executable points directly at the copy of Sparkle we bundle
  # under Contents/Frameworks instead of using @rpath.
  local exe="${APP_PATH}/Contents/MacOS/Phraims"
  [ -f "$exe" ] || return 0

  local old="@rpath/Sparkle.framework/Versions/B/Sparkle"
  local new="@executable_path/../Frameworks/Sparkle.framework/Versions/B/Sparkle"

  if otool -L "$exe" | grep -q "$old"; then
    step "Patching Sparkle install name in main executable"
    install_name_tool -change "$old" "$new" "$exe"
  fi
}

### SECTION: Qt Platform Specific Deployment
run_qt_platform_deployment() {
  mkdir -p "${STAGING_LIB_DIR}" "${APP_PATH}/Contents/Frameworks"

  local -a lib_paths=("${QT_WEBENGINE_PROP_PREFIX}/lib" "${QT_PREFIX}/lib" "${APP_PATH}/Contents/Frameworks")
  if [ -d "${QT_WEBENGINE_PROP_PREFIX}/lib" ]; then
    ln -sf "${QT_WEBENGINE_PROP_PREFIX}"/lib/Qt*.framework "${STAGING_LIB_DIR}/" 2>/dev/null || true
    ln -sf "${QT_WEBENGINE_PROP_PREFIX}"/lib/lib*.dylib "${STAGING_LIB_DIR}/" 2>/dev/null || true
  fi
  for mod in "${QT_MODULES[@]}"; do
    if prefix="$(brew --prefix "$mod" 2>/dev/null)" && [ -d "${prefix}/lib" ]; then
      lib_paths+=("${prefix}/lib")
      ln -sf "${prefix}"/lib/Qt*.framework "${STAGING_LIB_DIR}/" 2>/dev/null || true
      ln -sf "${prefix}"/lib/lib*.dylib "${STAGING_LIB_DIR}/" 2>/dev/null || true
    fi
  done
  lib_paths+=("${STAGING_LIB_DIR}")

  local -a args=("${APP_PATH}" "-always-overwrite")
  if [ "$DEBUG" -eq 1 ]; then
    args+=("-verbose=2")
  fi
  for p in "${lib_paths[@]}"; do args+=("-libpath=$p"); done
  step "Running macdeployqt (can take several minutes) ..."
  echo "time macdeployqt ${args[*]}"
  if [ "$DEBUG" -eq 1 ]; then
    time macdeployqt "${args[@]}" | tee "${LOG_FILE}"
  else
    time macdeployqt "${args[@]}" >"${LOG_FILE}" 2>&1
  fi
  if grep -q "ERROR:" "${LOG_FILE}"; then
    echo "macdeployqt reported errors; see ${LOG_FILE}" >&2
    exit 1
  fi
}

### SECTION: Materialize bundle symlinks
materialize_bundle_symlinks() {
  step "Materializing external symlinks inside bundle"
  local targets=("${APP_PATH}/Contents/Frameworks" "${APP_PATH}/Contents/PlugIns")
  for root in "${targets[@]}"; do
    [ -d "$root" ] || continue
    while IFS= read -r link; do
      local resolved
      resolved="$(realpath "$link")"
      if [[ "$resolved" != ${APP_PATH}/* ]]; then
        debug "replacing symlink $link -> $resolved"
        rm "$link"
        ditto "$resolved" "$link"
      fi
    done < <(find "$root" -type l)
  done
}

### SECTION: Sync QtWebEngine payload
sync_webengine_payload() {
  step "Syncing QtWebEngine resources"
  local src="${QT_WEBENGINE_PROP_PREFIX}/lib/QtWebEngineCore.framework/Versions/A"
  local dest="${APP_PATH}/Contents/Frameworks/QtWebEngineCore.framework/Versions/A"
  [ -d "${src}" ] || { echo "Missing QtWebEngineCore.framework under ${QT_WEBENGINE_PROP_PREFIX}" >&2; exit 1; }
  rsync -a "${src}/Resources/" "${dest}/Resources/"
  rsync -a "${src}/Helpers/QtWebEngineProcess.app/" "${dest}/Helpers/QtWebEngineProcess.app/"
}

ensure_rpath() {
  local file=$1 target=$2
  otool -l "$file" | grep -F "path $target" >/dev/null 2>&1 || install_name_tool -add_rpath "$target" "$file"
}

### SECTION: Fix rpaths
fix_rpaths() {
  step "Normalizing install names and rpaths"
  local files=("${APP_PATH}/Contents/MacOS/Phraims")
  while IFS= read -r f; do files+=("$f"); done < <(find "${APP_PATH}/Contents/Frameworks" -type f \( -name "*.dylib" -o -name "Qt*" -o -perm -111 \))
  while IFS= read -r f; do files+=("$f"); done < <(find "${APP_PATH}/Contents/PlugIns" -type f -name "*.dylib")
  if [ -d "${APP_PATH}/Contents/Frameworks/QtWebEngineCore.framework/Versions/A/Helpers" ]; then
    while IFS= read -r f; do files+=("$f"); done < <(find "${APP_PATH}/Contents/Frameworks/QtWebEngineCore.framework/Versions/A/Helpers" -type f -perm -111)
  fi

  set_install_id() {
    local f=$1
    if [[ "$f" == *"/Contents/Frameworks/"*".framework/Versions/A/"* ]]; then
      local name; name="$(basename "$f")"
      install_name_tool -id "@rpath/${name}.framework/Versions/A/${name}" "$f"
    elif [[ "$f" == *"/Contents/Frameworks/lib"*".dylib" ]]; then
      install_name_tool -id "@loader_path/$(basename "$f")" "$f"
    fi
  }

  for f in "${files[@]}"; do
    file "$f" | grep -q "Mach-O" || continue

    # Skip all Sparkle.framework binaries
    if [[ "$f" == *"/Sparkle.framework/"* ]]; then
      debug "Skipping rpath/install-name fixes for Sparkle binary: $f"
      continue
    fi

    local helper_rpath="@executable_path/../Frameworks"
    if [[ "$f" == *"/QtWebEngineCore.framework/Versions/A/Helpers/QtWebEngineProcess.app/Contents/MacOS/QtWebEngineProcess" ]]; then
      helper_rpath="@executable_path/../../../../../../../Frameworks"
    fi

    ensure_rpath "$f" "@loader_path"
    ensure_rpath "$f" "@loader_path/../Frameworks"
    ensure_rpath "$f" "@executable_path/../Frameworks"
    ensure_rpath "$f" "$helper_rpath"
    set_install_id "$f"

    while IFS= read -r dep; do
      dep="${dep#"	"}"; dep="${dep%% (*}"
      case "$dep" in
        @executable_path/../Frameworks/Qt*.framework/Versions/A/*)
          local name; name="$(basename "$dep")"
          install_name_tool -change "$dep" "@rpath/${name}.framework/Versions/A/${name}" "$f"
          ;;
        @executable_path/../Frameworks/lib*.dylib)
          install_name_tool -change "$dep" "@rpath/$(basename "$dep")" "$f"
          ;;
        /opt/homebrew/opt/*/lib/Qt*.framework/Versions/A/*)
          local name; name="$(basename "$dep")"
          install_name_tool -change "$dep" "@rpath/${name}.framework/Versions/A/${name}" "$f"
          ;;
        /opt/homebrew/*/Qt*.framework/Versions/A/*)
          local name; name="$(basename "$dep")"
          install_name_tool -change "$dep" "@rpath/${name}.framework/Versions/A/${name}" "$f"
          ;;
        /opt/homebrew/*/lib/*.dylib)
          install_name_tool -change "$dep" "@loader_path/$(basename "$dep")" "$f"
          ;;
        /usr/local/opt/*/lib/Qt*.framework/Versions/A/*)
          local name; name="$(basename "$dep")"
          install_name_tool -change "$dep" "@rpath/${name}.framework/Versions/A/${name}" "$f"
          ;;
        /usr/local/*/Qt*.framework/Versions/A/*)
          local name; name="$(basename "$dep")"
          install_name_tool -change "$dep" "@rpath/${name}.framework/Versions/A/${name}" "$f"
          ;;
        /usr/local/*/lib/*.dylib)
          install_name_tool -change "$dep" "@loader_path/$(basename "$dep")" "$f"
          ;;
        "${QT_WEBENGINE_PROP_PREFIX}"/lib/Qt*.framework/Versions/A/*)
          local name; name="$(basename "$dep")"
          install_name_tool -change "$dep" "@rpath/${name}.framework/Versions/A/${name}" "$f"
          ;;
        "${QT_WEBENGINE_PROP_PREFIX}"/lib/lib*.dylib)
          install_name_tool -change "$dep" "@loader_path/$(basename "$dep")" "$f"
          ;;
        @rpath/libbrotlicommon.1.dylib|/opt/homebrew/opt/brotli/lib/libbrotlicommon.1.dylib)
          install_name_tool -change "$dep" "@loader_path/libbrotlicommon.1.dylib" "$f"
          ;;
        @rpath/libwebp.7.dylib|/opt/homebrew/opt/*/lib/libwebp.7.dylib)
          install_name_tool -change "$dep" "@loader_path/libwebp.7.dylib" "$f"
          ;;
        @rpath/libsharpyuv.0.dylib|/opt/homebrew/opt/*/lib/libsharpyuv.0.dylib)
          install_name_tool -change "$dep" "@loader_path/libsharpyuv.0.dylib" "$f"
          ;;
      esac
    done < <(otool -L "$f" | tail -n +2 | awk '{print $1}')

    # Drop rpaths that point to external prefixes so the bundle uses its own frameworks.
    while IFS= read -r rp; do
      case "$rp" in
        /opt/homebrew/*|/usr/local/*|${QT_WEBENGINE_PROP_PREFIX}/*)
          install_name_tool -delete_rpath "$rp" "$f" 2>/dev/null || true
          ;;
      esac
    done < <(otool -l "$f" | awk '/LC_RPATH/{getline;getline;print $2}')
  done

  if [ -f "${APP_PATH}/Contents/Frameworks/libbrotlicommon.1.dylib" ]; then
    install_name_tool -id "@loader_path/libbrotlicommon.1.dylib" "${APP_PATH}/Contents/Frameworks/libbrotlicommon.1.dylib"
  fi
}

### SECTION: Validate bundle links
validate_bundle_links() {
  step "Validating bundled dependencies"
  local bad=0
  local files=("${APP_PATH}/Contents/MacOS/Phraims")
  while IFS= read -r f; do files+=("$f"); done < <(find "${APP_PATH}/Contents/Frameworks" -type f \( -name "*.dylib" -o -name "Qt*" -o -perm -111 \))
  while IFS= read -r f; do files+=("$f"); done < <(find "${APP_PATH}/Contents/PlugIns" -type f -name "*.dylib")
  if [ -d "${APP_PATH}/Contents/Frameworks/QtWebEngineCore.framework/Versions/A/Helpers" ]; then
    while IFS= read -r f; do files+=("$f"); done < <(find "${APP_PATH}/Contents/Frameworks/QtWebEngineCore.framework/Versions/A/Helpers" -type f -perm -111)
  fi

  expand_path() {
    local base_file=$1 path=$2
    local loader_dir; loader_dir="$(cd "$(dirname "$base_file")" && pwd)"
    local exe_dir="${APP_PATH}/Contents/MacOS"
    path="${path//@loader_path/$loader_dir}"
    path="${path//@executable_path/$exe_dir}"
    echo "$path"
  }

  file_rpaths() { otool -l "$1" | awk '/LC_RPATH/{getline;getline;print $2}'; }

  for f in "${files[@]}"; do
    file "$f" | grep -q "Mach-O" || continue

    # Skip Sparkle.framework binaries from dependency validation.
    # Sparkle's own install name often appears as an @rpath self-reference,
    # and we intentionally avoid rewriting its load commands due to
    # headerpad/code-signing constraints. Treat these as self-contained.
    if [[ "$f" == *"/Sparkle.framework/"* ]]; then
      debug "Skipping dependency validation for Sparkle binary: $f"
      continue
    fi

    while IFS= read -r line; do
      local dep="${line#"	"}"; dep="${dep%% (*}"
      case "$dep" in
        @executable_path/*|@loader_path/*|${APP_PATH}/*) ;;
        /System/*|/usr/lib/*) ;;
        /usr/local/opt/*/lib/Qt*.framework/Versions/A/*)
          local name; name="$(basename "$dep")"
          install_name_tool -change "$dep" "@rpath/${name}.framework/Versions/A/${name}" "$f"
          ;;
        /usr/local/*/Qt*.framework/Versions/A/*)
          local name; name="$(basename "$dep")"
          install_name_tool -change "$dep" "@rpath/${name}.framework/Versions/A/${name}" "$f"
          ;;
        /usr/local/*/lib/*.dylib)
          install_name_tool -change "$dep" "@loader_path/$(basename "$dep")" "$f"
          ;;
        /opt/homebrew/*|/usr/local/*)
          echo "  [!] $f depends on $dep (Homebrew path)"
          bad=1
          ;;
        @rpath/*)
          local tail="${dep#@rpath/}" resolved=0
          # Walk this file's rpaths on the fly and see if any resolve the @rpath reference
          while IFS= read -r rp; do
            if [ -f "$(expand_path "$f" "$rp")/${tail}" ]; then
              resolved=1
              break
            fi
          done < <(file_rpaths "$f")

          if [ "$resolved" -eq 0 ]; then
            echo "  [!] $f depends on ${dep} (unresolved within bundle)"
            bad=1
          fi
          ;;
        *)
          echo "  [!] $f depends on $dep (outside bundle/system)"
          bad=1
          ;;
      esac
    done < <(otool -L "$f" | tail -n +2 | awk '{print $1}')
  done

  if [ "$bad" -ne 0 ]; then
    echo "Found non-bundled dependencies; failing build." >&2
    exit 1
  fi
}

### SECTION: Verify QtWebEngine payload
verify_webengine_payload() {
  step "Verifying QtWebEngine payload"
  local res="${APP_PATH}/Contents/Frameworks/QtWebEngineCore.framework/Versions/A/Resources"
  local helper="${APP_PATH}/Contents/Frameworks/QtWebEngineCore.framework/Versions/A/Helpers/QtWebEngineProcess.app/Contents/MacOS/QtWebEngineProcess"
  local snapshot="v8_context_snapshot.arm64.bin"
  case "${MACOS_ARCH:-$ARCH_NAME}" in
    x86_64) snapshot="v8_context_snapshot.x86_64.bin" ;;
  esac
  local required=(icudtl.dat qtwebengine_devtools_resources.pak qtwebengine_resources.pak qtwebengine_resources_100p.pak qtwebengine_resources_200p.pak "${snapshot}")
  for f in "${required[@]}"; do
    [ -f "${res}/${f}" ] || { echo "Missing WebEngine resource: ${res}/${f}" >&2; exit 1; }
  done
  [ -d "${res}/qtwebengine_locales" ] || { echo "Missing WebEngine locales: ${res}/qtwebengine_locales" >&2; exit 1; }
  [ -x "${helper}" ] || { echo "QtWebEngineProcess missing/not executable: ${helper}" >&2; exit 1; }
  if [ "$DEBUG" -eq 1 ]; then
    ls -lh "${res}" || true
    du -sh "${res}/qtwebengine_locales" || true
    otool -L "${helper}" | head -n 20 || true
  fi
}

### SECTION: Signing
adhoc_sign_bundle() {
  step "Code signing app bundle"

  local IDENTITY="${MACOS_IDENTITY:-}"
  local FWK="$APP_PATH/Contents/Frameworks/Sparkle.framework"
  local VER="$FWK/Versions/Current"

  if [ -z "$IDENTITY" ]; then
    step "No MACOS_IDENTITY provided; doing simple ad-hoc signing (no entitlements)"

    # Sparkle helpers
    codesign --force --sign - "$VER/Autoupdate"
    codesign --force --sign - "$VER/Updater.app"
    for svc in "$VER/XPCServices"/*.xpc; do
      [ -e "$svc" ] || continue
      codesign --force --sign - "$svc"
    done

    # Framework
    codesign --force --sign - "$FWK"

    # App
    codesign --force --deep --sign - "$APP_PATH"

    echo "Verifying (ad-hoc)…"
    # codesign -vvv --deep will return non-zero for ad-hoc builds that
    # don't satisfy a designated requirement; we don't want set -e to abort
    # the script in that case, so temporarily disable -e.
    set +e
    codesign -vvv --deep "$APP_PATH"
    codesign_status=$?
    set -e
    if [ "${codesign_status}" -ne 0 ]; then
      echo "WARN: codesign verification FAILED (exit code ${codesign_status}), but this is acceptable for ad-hoc signed builds"
    fi
    return
  fi

  step "Using code signing identity: $IDENTITY"

  echo "Signing Sparkle helpers…"
  codesign --force --options runtime --timestamp \
    --sign "$IDENTITY" \
    "$VER/Autoupdate"
  codesign --force --options runtime --timestamp \
    --sign "$IDENTITY" \
    "$VER/Updater.app"
  for svc in "$VER/XPCServices"/*.xpc; do
    [ -e "$svc" ] || continue
    codesign --force --options runtime --timestamp \
      --sign "$IDENTITY" \
      "$svc"
  done

  echo "Signing Sparkle.framework…"
  codesign --force --options runtime --timestamp \
    --sign "$IDENTITY" \
    "$FWK"

  echo "Signing app bundle with hardened runtime + entitlements…"
  codesign --force --options runtime --timestamp --deep \
    --entitlements "ci/Phraims.entitlements" \
    --sign "$IDENTITY" \
    "$APP_PATH"

  echo "Verifying…"
  codesign -vvv --deep --strict "$APP_PATH"
  echo "Running Gatekeeper assessment (spctl)…"
  local spctl_status=0
  if spctl --assess --type execute --verbose "$APP_PATH"; then
    echo "Gatekeeper assessment: PASS"
  else
    spctl_status=$?
    if [ "${PHRAIMS_RELEASE:-0}" -eq 1 ]; then
      echo "Gatekeeper assessment FAILED (exit code ${spctl_status}) for release build; aborting." >&2
      exit "${spctl_status}"
    else
      echo "WARN: Gatekeeper assessment FAILED (exit code ${spctl_status}), but this is normal for local/dev builds that have not been sent for notarization yet."
    fi
  fi
}

### SECTION: Debug artifacts
debug_artifacts() {
  [ "$DEBUG" -ne 1 ] && return
  step "Debug: staging lib dir (${STAGING_LIB_DIR})"
  ls -la "${STAGING_LIB_DIR}" || true
  step "Debug: Frameworks dir"
  ls -la "${APP_PATH}/Contents/Frameworks" || true
  step "Debug: RPATHs (main)"
  otool -l "${APP_PATH}/Contents/MacOS/Phraims" | grep -A2 LC_RPATH || true
  step "Debug: RPATHs (QtWebEngineProcess)"
  local helper="${APP_PATH}/Contents/Frameworks/QtWebEngineCore.framework/Versions/A/Helpers/QtWebEngineProcess.app/Contents/MacOS/QtWebEngineProcess"
  [ -f "$helper" ] && otool -l "$helper" | grep -A2 LC_RPATH || true
}

### SECTION: Create installer
create_installer() {
  step "Creating dmg"
  rm -f "${BUILD_DIR}/Phraims.dmg"
  hdiutil detach "/Volumes/Phraims" >/dev/null 2>&1 || true
  hdiutil create -format UDZO -srcfolder "${APP_PATH}" -volname Phraims -ov "${BUILD_DIR}/Phraims.dmg"  
  step "Done. DMG available at ${BUILD_DIR}/Phraims.dmg"
}

### SECTION: Main orchestration
main() {
  initialize_environment

  require_custom_webengine
  step "QtWebEngine prefix: ${QT_WEBENGINE_PROP_PREFIX}"
  prefix_custom_webengine

  configure_cmake
  build_project

  patch_sparkle_rpath

  run_qt_platform_deployment
  materialize_bundle_symlinks
  sync_webengine_payload
  if [ ! -f "${APP_PATH}/Contents/Frameworks/libbrotlicommon.1.dylib" ] && [ -f "${QT_PREFIX}/../lib/libbrotlicommon.1.dylib" ]; then
    cp "${QT_PREFIX}/../lib/libbrotlicommon.1.dylib" "${APP_PATH}/Contents/Frameworks/"
  fi
  fix_rpaths
  validate_bundle_links
  verify_webengine_payload

  adhoc_sign_bundle
  
  debug_artifacts

  create_installer
}

main "$@"
