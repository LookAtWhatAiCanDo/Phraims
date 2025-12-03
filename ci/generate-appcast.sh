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

# Helper function to get file size portably
get_file_size() {
  local file="$1"
  if [[ ! -f "$file" ]]; then
    echo ""
    return
  fi
  
  # Try BSD stat first (macOS)
  if stat -f%z "$file" 2>/dev/null; then
    return
  fi
  
  # Try GNU stat (Linux)
  if stat -c%s "$file" 2>/dev/null; then
    return
  fi
  
  # Fallback to wc -c for maximum portability
  wc -c < "$file" 2>/dev/null || echo ""
}

if [[ -f "$ARM64_DMG" ]]; then
  ARM64_LENGTH=$(get_file_size "$ARM64_DMG")
  debug "ARM64 DMG size: $ARM64_LENGTH bytes"
fi

if [[ -f "$X86_64_DMG" ]]; then
  X86_64_LENGTH=$(get_file_size "$X86_64_DMG")
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
EOF

# Add ARM64 enclosure
cat >> appcast.xml <<EOF
      <enclosure
        url="https://github.com/LookAtWhatAiCanDo/Phraims/releases/download/$TAG_NAME/Phraims-$TAG_NAME-macOS-arm64.dmg"
        sparkle:version="$VERSION"
        sparkle:shortVersionString="$VERSION"
EOF

# Add length attribute only if we have the file size
if [[ -n "$ARM64_LENGTH" ]]; then
  echo "        length=\"$ARM64_LENGTH\"" >> appcast.xml
fi

cat >> appcast.xml <<EOF
        type="application/octet-stream"
        sparkle:edSignature="<!-- Ed25519 signature will be added by signing process -->"
        sparkle:minimumSystemVersion="11.0"
      />
EOF

# Add x86_64 enclosure
cat >> appcast.xml <<EOF
      <enclosure
        url="https://github.com/LookAtWhatAiCanDo/Phraims/releases/download/$TAG_NAME/Phraims-$TAG_NAME-macOS-x86_64.dmg"
        sparkle:version="$VERSION"
        sparkle:shortVersionString="$VERSION"
EOF

# Add length attribute only if we have the file size
if [[ -n "$X86_64_LENGTH" ]]; then
  echo "        length=\"$X86_64_LENGTH\"" >> appcast.xml
fi

cat >> appcast.xml <<EOF
        type="application/octet-stream"
        sparkle:edSignature="<!-- Ed25519 signature will be added by signing process -->"
        sparkle:minimumSystemVersion="10.15"
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
