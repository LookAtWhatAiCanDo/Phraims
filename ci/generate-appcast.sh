#!/bin/bash
# Generate Sparkle appcast XML feed for macOS updates
# This creates an RSS-style feed that Sparkle uses to check for updates

set -euo pipefail

# Function to print debug messages
debug() {
  if [[ "${DEBUG:-0}" -ge 1 ]]; then
    echo "[DEBUG] $*" >&2
  fi
}

# Check required environment variables
if [[ -z "${TAG_NAME:-}" ]]; then
  echo "Error: TAG_NAME environment variable is required" >&2
  exit 1
fi

if [[ -z "${RELEASE_NOTES:-}" ]]; then
  RELEASE_NOTES="See release page for details"
fi

# Strip 'v' prefix if present
VERSION="${TAG_NAME#v}"
debug "Generating appcast for version: $VERSION"

# Calculate file sizes (if files exist locally)
ARM64_DMG="build/macos-arm64/Phraims.dmg"
X86_64_DMG="build/macos-x86_64/Phraims.dmg"

ARM64_LENGTH=""
X86_64_LENGTH=""

if [[ -f "$ARM64_DMG" ]]; then
  ARM64_LENGTH=$(stat -f%z "$ARM64_DMG" 2>/dev/null || stat -c%s "$ARM64_DMG" 2>/dev/null || echo "")
  debug "ARM64 DMG size: $ARM64_LENGTH bytes"
fi

if [[ -f "$X86_64_DMG" ]]; then
  X86_64_LENGTH=$(stat -f%z "$X86_64_DMG" 2>/dev/null || stat -c%s "$X86_64_DMG" 2>/dev/null || echo "")
  debug "x86_64 DMG size: $X86_64_LENGTH bytes"
fi

# Generate appcast XML
cat > appcast.xml <<EOF
<?xml version="1.0" encoding="utf-8"?>
<rss version="2.0" xmlns:sparkle="http://www.andymatuschak.org/xml-namespaces/sparkle" xmlns:dc="http://purl.org/dc/elements/1.1/">
  <channel>
    <title>Phraims Updates</title>
    <link>https://github.com/LookAtWhatAiCanDo/Phraims</link>
    <description>Most recent updates for Phraims</description>
    <language>en</language>
    <item>
      <title>Version $VERSION</title>
      <link>https://github.com/LookAtWhatAiCanDo/Phraims/releases/tag/$TAG_NAME</link>
      <sparkle:version>$VERSION</sparkle:version>
      <sparkle:shortVersionString>$VERSION</sparkle:shortVersionString>
      <description><![CDATA[
        $RELEASE_NOTES
      ]]></description>
      <pubDate>$(date -R)</pubDate>
      <sparkle:minimumSystemVersion>11.0</sparkle:minimumSystemVersion>
      <enclosure
        url="https://github.com/LookAtWhatAiCanDo/Phraims/releases/download/$TAG_NAME/Phraims-$TAG_NAME-macOS-arm64.dmg"
        sparkle:version="$VERSION"
        sparkle:shortVersionString="$VERSION"
        ${ARM64_LENGTH:+length=\"$ARM64_LENGTH\"}
        type="application/octet-stream"
        sparkle:edSignature="<!-- Ed25519 signature will be added by signing process -->"
      />
      <sparkle:minimumSystemVersion>10.15</sparkle:minimumSystemVersion>
      <enclosure
        url="https://github.com/LookAtWhatAiCanDo/Phraims/releases/download/$TAG_NAME/Phraims-$TAG_NAME-macOS-x86_64.dmg"
        sparkle:version="$VERSION"
        sparkle:shortVersionString="$VERSION"
        ${X86_64_LENGTH:+length=\"$X86_64_LENGTH\"}
        type="application/octet-stream"
        sparkle:edSignature="<!-- Ed25519 signature will be added by signing process -->"
      />
    </item>
  </channel>
</rss>
EOF

debug "Generated appcast.xml:"
if [[ "${DEBUG:-0}" -ge 1 ]]; then
  cat appcast.xml >&2
fi

echo "Appcast feed generated successfully: appcast.xml"
