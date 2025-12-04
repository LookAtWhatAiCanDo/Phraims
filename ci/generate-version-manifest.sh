#!/bin/bash
# Generate version manifest JSON for update checks
# This script creates a version.json file that the UpdateChecker can consume

set -euo pipefail

# Function to print debug messages
debug() {
  if [[ "${DEBUG:-0}" -ge 1 ]]; then
    echo "[DEBUG] $*" >&2
  fi
}

# Check if TAG_NAME is provided
if [[ -z "${TAG_NAME:-}" ]]; then
  echo "Error: TAG_NAME environment variable is required" >&2
  exit 1
fi

# Strip 'v' prefix if present
VERSION="${TAG_NAME#v}"
debug "Generating manifest for version: $VERSION"

# Create JSON manifest
cat > version.json <<EOF
{
  "version": "$VERSION",
  "releaseDate": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
  "platforms": {
    "macos": {
      "arm64": {
        "url": "https://github.com/LookAtWhatAiCanDo/Phraims/releases/download/$TAG_NAME/Phraims-$TAG_NAME-macOS-arm64.dmg",
        "minSystemVersion": "11.0"
      },
      "x86_64": {
        "url": "https://github.com/LookAtWhatAiCanDo/Phraims/releases/download/$TAG_NAME/Phraims-$TAG_NAME-macOS-x86_64.dmg",
        "minSystemVersion": "10.15"
      }
    },
    "windows": {
      "x64": {
        "url": "https://github.com/LookAtWhatAiCanDo/Phraims/releases/download/$TAG_NAME/Phraims-$TAG_NAME-Windows-x64.exe"
      },
      "arm64": {
        "url": "https://github.com/LookAtWhatAiCanDo/Phraims/releases/download/$TAG_NAME/Phraims-$TAG_NAME-Windows-arm64.exe"
      }
    },
    "linux": {
      "releaseUrl": "https://github.com/LookAtWhatAiCanDo/Phraims/releases/tag/$TAG_NAME"
    }
  },
  "releaseNotesUrl": "https://github.com/LookAtWhatAiCanDo/Phraims/releases/tag/$TAG_NAME"
}
EOF

debug "Generated version.json:"
if [[ "${DEBUG:-0}" -ge 1 ]]; then
  cat version.json >&2
fi

echo "Version manifest generated successfully: version.json"
