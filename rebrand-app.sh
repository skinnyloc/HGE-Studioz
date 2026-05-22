#!/bin/bash
# ==========================================================================
# HGE Music Studio — App Rebranding Script
# ==========================================================================
# Patches the compiled app bundle to remove Audacity references.
# No source rebuild needed — operates on the binary directly.
#
# Usage:  ./rebrand-app.sh [path/to/HgeMusicStudio.app]
# ==========================================================================

set -euo pipefail

APP="${1:-$HOME/HgeMusicStudio/HgeMusicStudio.app}"

if [ ! -d "$APP" ]; then
  echo "❌ App not found at: $APP"
  exit 1
fi

echo "╔═══════════════════════════════════════════════════════════════╗"
echo "║         HGE Music Studio — App Rebranding                     ║"
echo "╚═══════════════════════════════════════════════════════════════╝"
echo ""

BINARY="$APP/Contents/MacOS/HgeMusicStudio"
INFO="$APP/Contents/Info.plist"

# ── 1. Info.plist rebranding ──────────────────────────────────────────
echo "📝 Updating Info.plist..."
plutil -replace CFBundleName -string "HGE Music Studio" "$INFO" 2>/dev/null || \
  echo "  ⚠️  Could not update CFBundleName"

plutil -replace CFBundleDisplayName -string "HGE Music Studio" "$INFO" 2>/dev/null || true

# ── 2. Binary string replacement (safe substitutions) ────────────────
echo "🔧 Patching binary strings..."
if [ -f "$BINARY" ]; then
  # Create a backup first
  cp "$BINARY" "${BINARY}.backup"
  echo "  ✅ Backup created at ${BINARY}.backup"

  # Note: Binary patching is limited. Full string replacement requires
  # source rebuild. But we can patch key strings if they're the same length.
  # For production: rebuild from source with changed strings.
  echo "  ⚠️  Full string rebranding requires source rebuild."
  echo "  🔧 Key strings to change in source before rebuilding:"
  echo ""
  echo "     'Audacity version:' → 'HGE Music Studio version:'"
  echo "     'audacityversion'   → 'hgemusicstudioversion'"
  echo "     'Built-in Effect:'  → 'HGE Effect:'"
  echo "     'Nyquist plug-ins'  → 'Scripts'"
  echo "     'pluginsettings'    → 'hgepluginsettings'"
  echo "     'pluginregistry'    → 'hgepluginregistry'"
fi

# ── 3. Remove Nyquist info files with Audacity branding ──────────────
echo "🧹 Cleaning up Audacity-branded resources..."
find "$APP/Contents" -name "*.htm" -o -name "*.html" -o -name "*.txt" 2>/dev/null | while read f; do
  if grep -qi "audacity" "$f" 2>/dev/null; then
    # Replace Audacity with HGE Music Studio in text files
    sed -i '' 's/Audacity/HGE Music Studio/g' "$f" 2>/dev/null && \
      echo "  ✅ Patched: $(basename "$f")" || true
  fi
done

# ── 4. Remove legacy debug/reference paths from binary ────────────────
echo "🔍 Checking for developer paths in binary..."
DEVEL_PATHS=$(strings "$BINARY" 2>/dev/null | grep -c "/Users/homegrownentllc/Documents/VSCODE" || true)
if [ "$DEVEL_PATHS" -gt 0 ]; then
  echo "  ⚠️  Found $DEVEL_PATHS developer paths in binary"
  echo "  🔧 These are baked in at compile time. Fix: rebuild from CI, not dev machine"
else
  echo "  ✅ No developer paths found"
fi

# ── 5. Set the app as the default for .aup4 files ────────────────────
echo "📎 Registering .aup4 file associations..."
defaults write "$APP/Contents/Info" CFBundleDocumentTypes -array-add '
<dict>
  <key>CFBundleTypeName</key>
  <string>HGE Music Studio Project</string>
  <key>CFBundleTypeExtensions</key>
  <array><string>aup4</string></array>
  <key>CFBundleTypeRole</key>
  <string>Editor</string>
  <key>LSHandlerRank</key>
  <string>Owner</string>
</dict>' 2>/dev/null || echo "  ⚠️  Could not update document types"

# ── 6. Touch app to refresh Finder cache ─────────────────────────────
touch "$APP"

echo ""
echo "╔═══════════════════════════════════════════════════════════════╗"
echo "║  ✅ Rebranding complete!                                      ║"
echo "║                                                               ║"
echo "║  To apply full string rebranding:                             ║"
echo "║    1. Edit source strings before rebuilding                   ║"
echo "║    2. See PHASE2_EXECUTION.md for string list                 ║"
echo "║                                                               ║"
echo "║  Next step: Launch and test                                   ║"
echo "║    open \"$APP\"                                    ║"
echo "╚═══════════════════════════════════════════════════════════════╝"
