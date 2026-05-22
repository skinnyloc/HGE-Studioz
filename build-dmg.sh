#!/bin/bash
# ==========================================================================
# HGE Music Studio — DMG Build & Packaging Script
# ==========================================================================
# Creates a production-ready .dmg from the build-hge output.
# Usage:  ./build-dmg.sh [--dev] [--notarize]
#
# Options:
#   --dev       Skip code signing, create unsigned DMG for testing
#   --notarize  Submit to Apple for notarization (requires Apple ID setup)
# ==========================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP_NAME="HgeMusicStudio"
APP_BUNDLE="$SCRIPT_DIR/$APP_NAME.app"
DMG_NAME="HgeMusicStudio-4.0.0"
STAGING_DIR="/tmp/hge-dmg-staging"
SIGNING_IDENTITY="${SIGNING_IDENTITY:-Developer ID Application: HGE Network LLC}"
TEAM_ID="${TEAM_ID:-}"
APPLE_ID="${APPLE_ID:-}"
APPLE_PASSWORD="${APPLE_PASSWORD:-}"
NOTARIZE="${NOTARIZE:-no}"
DEV_MODE="${DEV_MODE:-no}"

# ── Parse arguments ─────────────────────────────────────────────────────
for arg in "$@"; do
  case "$arg" in
    --dev) DEV_MODE="yes" ;;
    --notarize) NOTARIZE="yes" ;;
  esac
done

echo "=========================================="
echo " HGE Music Studio — DMG Builder"
echo "=========================================="
echo "  App bundle: $APP_BUNDLE"
echo "  Mode:       $([ "$DEV_MODE" == "yes" ] && echo "DEVELOPER (unsigned)" || echo "PRODUCTION (signed)")"
echo ""

# ── Verify app bundle exists ────────────────────────────────────────────
if [ ! -d "$APP_BUNDLE" ]; then
  echo "❌ Error: $APP_BUNDLE not found!"
  echo "   Build the app first or copy it to this directory."
  exit 1
fi

# ── Clean staging ───────────────────────────────────────────────────────
echo "📦 Preparing DMG staging area..."
rm -rf "$STAGING_DIR"
mkdir -p "$STAGING_DIR"

# ── Code signing (production only) ─────────────────────────────────────
if [ "$DEV_MODE" == "no" ]; then
  echo "🔏 Signing app bundle (deep)..."
  codesign --force --deep --options runtime \
    --sign "$SIGNING_IDENTITY" \
    "$APP_BUNDLE" 2>&1 | sed 's/^/   /'
  
  echo "🔏 Verifying signature..."
  codesign -dv --verbose=2 "$APP_BUNDLE" 2>&1 | sed 's/^/   /'
else
  echo "🔓 Skipping code signing (--dev mode)"
fi

# ── Create DMG ─────────────────────────────────────────────────────────
echo "💿 Creating DMG..."
cp -R "$APP_BUNDLE" "$STAGING_DIR/$APP_NAME.app"

# Create a symbolic link to /Applications for drag-and-drop install
ln -s /Applications "$STAGING_DIR/Applications"

# Create DMG with appropriate settings
hdiutil create -volname "Hge Music Studio" \
  -srcfolder "$STAGING_DIR" \
  -ov -format UDZO \
  -fs HFS+ \
  -megabytes 512 \
  "$SCRIPT_DIR/$DMG_NAME.dmg" 2>&1 | sed 's/^/   /'

# Clean staging
rm -rf "$STAGING_DIR"

DMG_PATH="$SCRIPT_DIR/$DMG_NAME.dmg"

# ── Notarization (optional, production) ────────────────────────────────
if [ "$NOTARIZE" == "yes" ] && [ "$DEV_MODE" == "no" ]; then
  echo "☁️  Submitting for notarization..."
  xcrun notarytool submit "$DMG_PATH" \
    --apple-id "$APPLE_ID" \
    --team-id "$TEAM_ID" \
    --password "$APPLE_PASSWORD" \
    --wait 2>&1 | sed 's/^/   /'

  echo "🔏 Stapling notarization ticket..."
  xcrun stapler staple "$DMG_PATH" 2>&1 | sed 's/^/   /'
fi

echo ""
echo "=========================================="
echo " ✅ DMG ready: $DMG_PATH"
echo "    Size: $(du -h "$DMG_PATH" | cut -f1)"
echo "=========================================="
