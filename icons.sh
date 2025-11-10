#!/bin/zsh
# Build phraims.icns from a 1024x1024 PNG source on macOS using iconutil conventions.
# Usage: just run in a directory containing phraims.png (1024x1024). Optional: ./make_icns.zsh <source.png>

set -euo pipefail

# --- locate source (prefer explicit arg, else common filenames) ---
SRC="phraims_icon_1024.png"
if [[ -z "${SRC}" || ! -f "${SRC}" ]]; then
  echo "Source 1024×1024 PNG not found." >&2
  exit 1
fi

# --- prepare iconset folder ---
ICONSET="phraims.iconset"
rm -rf "${ICONSET}"
mkdir -p "${ICONSET}"

# --- helper to resize with sips (lossless PNG) ---
resize() {
  local size="$1" out="$2"
  /usr/bin/sips -s format png -z "$size" "$size" "${SRC}" --out "${out}" >/dev/null
}

# --- generate required sizes ---
resize 16  "${ICONSET}/icon_16x16.png"
resize 32  "${ICONSET}/icon_16x16@2x.png"

resize 32  "${ICONSET}/icon_32x32.png"
resize 64  "${ICONSET}/icon_32x32@2x.png"

resize 128 "${ICONSET}/icon_128x128.png"
resize 256 "${ICONSET}/icon_128x128@2x.png"

resize 256 "${ICONSET}/icon_256x256.png"
resize 512 "${ICONSET}/icon_256x256@2x.png"

resize 512  "${ICONSET}/icon_512x512.png"
# 1024 is the original; copy or upscale if needed
if /usr/bin/sips -g pixelWidth -g pixelHeight "${SRC}" | grep -q "pixelWidth: 1024"; then
  cp "${SRC}" "${ICONSET}/icon_512x512@2x.png"
else
  resize 1024 "${ICONSET}/icon_512x512@2x.png"
fi

# --- package to .icns ---
/usr/bin/iconutil -c icns "${ICONSET}" -o "resources/phraims.icns"

# --- verify and report ---
SHA=$(shasum -a 256 "resources/phraims.icns" | awk '{print $1}')
BYTES=$(stat -f%z "resources/phraims.icns")
echo "resources/phraims.icns SHA256: ${SHA}"
echo "resources/phraims.icns size:   ${BYTES} bytes"