#!/bin/bash
# ==========================================================================
# HGE Music Studio — VST3 Rebuild Script
# ==========================================================================
# This script rebuilds the app with VST3 support enabled.
# Prerequisites: Xcode 15+, CMake 3.25+, Python 3.9+, Conan
#
# Before running: apply VST3_PATH patch to VST3EffectsModule.cpp
# ==========================================================================

set -euo pipefail

HGE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
AUDACITY_SRC="/Users/homegrownentllc/Documents/VSCODE/HGE MUSIC STUDIO/audacity-3.7.7"
BUILD_DIR="$AUDACITY_SRC/build-vst3"

echo "=========================================="
echo " HGE Music Studio — VST3 Rebuild"
echo "=========================================="
echo "  Source: $AUDACITY_SRC"
echo "  Build:  $BUILD_DIR"
echo ""

# ── Check source exists ────────────────────────────────────────────────
if [ ! -d "$AUDACITY_SRC" ]; then
  echo "❌ Source directory not found at: $AUDACITY_SRC"
  echo "   Clone the repo first or update AUDACITY_SRC in this script."
  exit 1
fi

# ── Apply VST3_PATH patch ─────────────────────────────────────────────
VST3_MODULE="$AUDACITY_SRC/libraries/lib-vst3/VST3EffectsModule.cpp"
if grep -q "VST3_PATH" "$VST3_MODULE" 2>/dev/null; then
  echo "✅ VST3_PATH patch already applied"
else
  echo "📝 Applying VST3_PATH env var support..."
  # Add tokenizer include
  sed -i '' 's/#include <wx\/utils.h>/#include <wx\/tokenzr.h>\n#include <wx\/utils.h>/' "$VST3_MODULE"
  # Add VST3_PATH scanning after the opening of FindModulePaths
  # (This is a simplified patch; see module-source for full patch)
  echo "⚠️  Manual patch may be needed — see module-source/ for full patch"
fi

# ── Conan dependencies ─────────────────────────────────────────────────
echo "📦 Installing Conan dependencies..."
cd "$AUDACITY_SRC"
mkdir -p "$BUILD_DIR"
conan install . --build=missing \
  -s build_type=Release \
  -o vst3sdk=True 2>&1 | sed 's/^/   /'

# ── CMake configuration ────────────────────────────────────────────────
echo "🔧 Configuring CMake..."
cmake -S . -B "$BUILD_DIR" -G "Unix Makefiles" \
  -DCMAKE_BUILD_TYPE=Release \
  -Daudacity_has_vst2=ON \
  -Daudacity_has_lv2=ON \
  -Daudacity_has_vst3=ON \
  -Daudacity_has_audiocom=OFF \
  2>&1 | sed 's/^/   /'

# ── Build ──────────────────────────────────────────────────────────────
echo "🏗️  Building (this will take a while)..."
cmake --build "$BUILD_DIR" --target HgeMusicStudio -- -j$(sysctl -n hw.ncpu) \
  2>&1 | sed 's/^/   /'

# ── Rebuild Wrapper ────────────────────────────────────────────────────
echo "🔨 Rebuilding Wrapper..."
/usr/bin/cc \
  -DAUDACITY_BUNDLE_EXECUTABLE=\"HgeMusicStudio\" \
  -O3 -arch arm64 \
  -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk \
  -mmacosx-version-min=10.13 \
  -o "$BUILD_DIR/Release/HgeMusicStudio.app/Contents/MacOS/Wrapper" \
  "$AUDACITY_SRC/mac/Wrapper.c" \
  2>&1 | sed 's/^/   /'

# ── Copy bundled plugins ──────────────────────────────────────────────
echo "📋 Copying bundled plugins..."
APP="$BUILD_DIR/Release/HgeMusicStudio.app"
PLUGINS="$APP/Contents/plug-ins"

mkdir -p "$PLUGINS/VST2" "$PLUGINS/VST3"

# Copy from system installations
if [ -d "/Library/Audio/Plug-Ins/VST/TDR Nova.vst" ]; then
  cp -R "/Library/Audio/Plug-Ins/VST/TDR Nova.vst" "$PLUGINS/VST2/"
  echo "  ✅ TDR Nova.vst"
fi
if [ -d "/Library/Audio/Plug-Ins/VST3/SSQ.vst3" ]; then
  cp -R "/Library/Audio/Plug-Ins/VST3/SSQ.vst3" "$PLUGINS/VST3/"
  echo "  ✅ SSQ.vst3"
fi
if [ -d "/Library/Audio/Plug-Ins/VST/Softube/Saturation Knob.vst" ]; then
  cp -R "/Library/Audio/Plug-Ins/VST/Softube/Saturation Knob.vst" "$PLUGINS/VST2/"
  echo "  ✅ Saturation Knob.vst"
fi

# ── Verify ─────────────────────────────────────────────────────────────
echo ""
echo "=========================================="
echo " 🔍 Verification"
echo "=========================================="
file "$APP/Contents/MacOS/Wrapper"
file "$APP/Contents/MacOS/HgeMusicStudio"
echo ""
echo "VST2 plugins:"
ls "$PLUGINS/VST2/" 2>/dev/null || echo "  (none)"
echo "VST3 plugins:"
ls "$PLUGINS/VST3/" 2>/dev/null || echo "  (none)"
echo ""
du -sh "$APP"
echo ""
echo "=========================================="
echo " ✅ Build complete!"
echo "    App: $APP"
echo "=========================================="
