#!/usr/bin/env bash
set -euo pipefail

DEBUG="${DEBUG:-0}" # DEBUG=1 for verbose diagnostics

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
ARCH_NAME="${BUILD_ARCH:-arm64}"
BUILD_DIR="${BUILD_DIR:-${REPO_ROOT}/build_macos_${ARCH_NAME}}"
APP_PATH="${BUILD_DIR}/Phraims.app"
LOG_FILE="${BUILD_DIR}/macdeployqt.log"
STAGING_LIB_DIR="${BUILD_DIR}/lib"
QTWEBENGINE_VER="${QTWEBENGINE_VER:-6.9.3}"
QT_WEBENGINE_PROP_PREFIX="${QT_WEBENGINE_PROP_PREFIX:-${REPO_ROOT}/.qt/${QTWEBENGINE_VER}-prop-macos}"
QT_MODULES=(qtbase qtdeclarative qtwebchannel qtpositioning qtvirtualkeyboard qtsvg brotli)

step() { printf "\n==> %s\n" "$*"; }
debug() {
  if [ "$DEBUG" -eq 1 ]; then
    printf "  [debug] %s\n" "$*"
  fi
  return 0
}

ensure_homebrew() {
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
}

install_formulae() {
  export HOMEBREW_NO_AUTO_UPDATE=1
  export HOMEBREW_NO_ENV_HINTS=1
  local packages=("$@")
  step "Ensuring Homebrew packages: ${packages[*]}"
  if [ "${FORCE_BREW_UPDATE:-1}" -eq 1 ]; then
    step "Updating Homebrew"
    brew update --quiet
  fi
  for pkg in "${packages[@]}"; do
    if ! brew list --versions "$pkg" >/dev/null 2>&1; then
      echo "  - installing $pkg"
      brew install "$pkg"
    else
      debug "$pkg already installed"
    fi
  done
}

require_custom_webengine() {
  local framework="${QT_WEBENGINE_PROP_PREFIX}/lib/QtWebEngineCore.framework/Versions/A/QtWebEngineCore"
  local cmake_cfg="${QT_WEBENGINE_PROP_PREFIX}/lib/cmake/Qt6WebEngineWidgets/Qt6WebEngineWidgetsConfig.cmake"
  if [ ! -f "$framework" ] || [ ! -f "$cmake_cfg" ]; then
    cat <<EOF
Custom QtWebEngine not found at ${QT_WEBENGINE_PROP_PREFIX}
Run ./ci/build-qtwebengine-macos.sh first (or set QT_WEBENGINE_PROP_PREFIX/QTWEBENGINE_VER to your install prefix).
EOF
    exit 1
  fi
}

run_macdeployqt() {
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

  step "Running macdeployqt"
  local -a args=("${APP_PATH}" "-always-overwrite")
  if [ "$DEBUG" -eq 1 ]; then
    args+=("-verbose=2")
  fi
  for p in "${lib_paths[@]}"; do args+=("-libpath=$p"); done
  if [ "$DEBUG" -eq 1 ]; then
    macdeployqt "${args[@]}" | tee "${LOG_FILE}"
  else
    macdeployqt "${args[@]}" >"${LOG_FILE}" 2>&1
  fi
  if grep -q "ERROR:" "${LOG_FILE}"; then
    echo "macdeployqt reported errors; see ${LOG_FILE}" >&2
    exit 1
  fi
}

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

ensure_rpath() {
  local file=$1 target=$2
  otool -l "$file" | grep -F "path $target" >/dev/null 2>&1 || install_name_tool -add_rpath "$target" "$file"
}

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

sync_webengine_payload() {
  step "Syncing QtWebEngine resources"
  local src="${QT_WEBENGINE_PROP_PREFIX}/lib/QtWebEngineCore.framework/Versions/A"
  local dest="${APP_PATH}/Contents/Frameworks/QtWebEngineCore.framework/Versions/A"
  [ -d "${src}" ] || { echo "Missing QtWebEngineCore.framework under ${QT_WEBENGINE_PROP_PREFIX}" >&2; exit 1; }
  rsync -a "${src}/Resources/" "${dest}/Resources/"
  rsync -a "${src}/Helpers/QtWebEngineProcess.app/" "${dest}/Helpers/QtWebEngineProcess.app/"
}

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
    local rpaths=()
    while IFS= read -r rp; do rpaths+=("$rp"); done < <(file_rpaths "$f")
    while IFS= read -r line; do
      local dep="${line#"	"}"; dep="${dep%% (*}"
      case "$dep" in
        @executable_path/*|@loader_path/*|${APP_PATH}/*) ;;
        /System/*|/usr/lib/*) ;;
        /opt/homebrew/*|/usr/local/*)
          echo "  [!] $f depends on $dep (Homebrew path)"
          bad=1
          ;;
        @rpath/*)
          local tail="${dep#@rpath/}" resolved=0
          for rp in "${rpaths[@]}"; do
            if [ -f "$(expand_path "$f" "$rp")/${tail}" ]; then resolved=1; break; fi
          done
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

adhoc_sign_bundle() {
  step "Ad-hoc signing app bundle (deep)"
  codesign --force --deep --sign - --timestamp=none "${APP_PATH}"
  codesign -vv "${APP_PATH}"
}

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

main() {
  cd "${REPO_ROOT}"
  step "Repository root: ${REPO_ROOT} (arch=${ARCH_NAME})"

  ensure_homebrew
  install_formulae qtbase qtdeclarative qtwebchannel qtpositioning qtvirtualkeyboard qtsvg brotli ninja cmake
  require_custom_webengine
  step "QtWebEngine prefix: ${QT_WEBENGINE_PROP_PREFIX}"

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

  step "Configuring CMake (Release, Ninja, arm64)"
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

  step "Building Phraims"
  cmake --build "${BUILD_DIR}" --config Release
  [ -d "${APP_PATH}" ] || { echo "Expected app bundle at ${APP_PATH}" >&2; exit 1; }

  run_macdeployqt
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

  step "Creating dmg"
  rm -f "${BUILD_DIR}/Phraims.dmg"
  hdiutil detach "/Volumes/Phraims" >/dev/null 2>&1 || true
  hdiutil create -format UDZO -srcfolder "${APP_PATH}" -volname Phraims -ov "${BUILD_DIR}/Phraims.dmg"
  step "Done. DMG available at ${BUILD_DIR}/Phraims.dmg"
}

main "$@"
