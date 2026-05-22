#!/bin/bash
# ==========================================================================
# HGE Music Studio — Full Rebuild with VST3 + Custom Effect Browser
# ==========================================================================
# Builds the app from source with:
#   - VST3 support enabled (audacity_has_vst3=ON)
#   - mod-plugin-manager compiled in (PluginCategoryManager + HGE Browser)
#   - Wrapper with VST_PATH/LV2_PATH for bundled plugins
#   - All bundled plugins copied into the app
#
# NON-DESTRUCTIVE: Stock Effects menu remains as fallback.
# The HGE Effect Browser is added alongside via Tools menu.
#
# PREREQUISITES:
#   Xcode 15+           (xcode-select --install)
#   CMake 3.25+         (brew install cmake)
#   Conan 1.x           (pip install conan==1.64)
#   Python 3.9+         (built into macOS)
#
# USAGE:
#   ./rebuild-full.sh              # Full production build
#   ./rebuild-full.sh --quick      # Skip Conan install, use cached deps
#   ./rebuild-full.sh --no-vst3    # Build without VST3 (VST2 only)
#   ./rebuild-full.sh --no-browser # Build without the custom browser
#   ./rebuild-full.sh --dmg        # Build + create DMG on Desktop
#
# ==========================================================================

set -euo pipefail

# ─── Configuration ─────────────────────────────────────────────────────────
HGE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
AUDACITY_SRC="/Users/homegrownentllc/Documents/VSCODE/HGE MUSIC STUDIO/audacity-3.7.7"
BUILD_DIR="$AUDACITY_SRC/build-vst3"
MODULE_SRC="$HGE_DIR/module-source"

SCRIPT_MODE="full"
BUILD_DMG="no"

for arg in "$@"; do
  case "$arg" in
    --quick)    SCRIPT_MODE="quick" ;;
    --no-vst3)  SCRIPT_MODE="no-vst3" ;;
    --no-browser) SCRIPT_MODE="no-browser" ;;
    --dmg)      BUILD_DMG="yes" ;;
  esac
done

echo "╔══════════════════════════════════════════════════════════════╗"
echo "║        HGE Music Studio — Full Rebuild                      ║"
echo "╚══════════════════════════════════════════════════════════════╝"
echo ""
echo "  Source:      $AUDACITY_SRC"
echo "  Build:       $BUILD_DIR"
echo "  Mode:        $SCRIPT_MODE"
echo "  Create DMG:  $BUILD_DMG"
echo ""

# ── Check prerequisites ──────────────────────────────────────────────────
if [ ! -d "$AUDACITY_SRC" ]; then
  echo "❌ Source directory not found!"
  echo "   Expected: $AUDACITY_SRC"
  echo "   Clone the repo first or update AUDACITY_SRC in this script."
  exit 1
fi

if ! command -v cmake &>/dev/null; then
  echo "❌ CMake not found! Install with: brew install cmake"
  exit 1
fi

# ── Step 1: Apply source patches ─────────────────────────────────────────
echo "═══════════════════════════════════════════════════════════════"
echo "  STEP 1: Apply source patches"
echo "═══════════════════════════════════════════════════════════════"

# Patch 1: Wrapper.c — VST_PATH + LV2_PATH env vars
WRAPPER_SRC="$AUDACITY_SRC/mac/Wrapper.c"
if grep -q "VST_PATH" "$WRAPPER_SRC" 2>/dev/null; then
  echo "  ✅ Wrapper.c VST_PATH patch already applied"
else
  echo "  📝 Applying Wrapper.c patch..."
  # Find the execve call and add VST_PATH/LV2_PATH before it
  cat > /tmp/wrapper_patch.c << 'WRAPPERPATCH'
  // HGE: Set VST_PATH and LV2_PATH for bundled plugins
  {
    size_t path_len = strlen(path);
    char *res_dir = alloca(path_len + 32);
    strcpy(res_dir, path);
    char *p = strrchr(res_dir, '/');
    if (p) strcpy(p, "/..");

    char plugins_dir[1024];
    snprintf(plugins_dir, sizeof(plugins_dir), "%s/Plug-Ins", res_dir);
    setenv("VST_PATH", plugins_dir, 0);
    setenv("LV2_PATH", plugins_dir, 0);
  }
WRAPPERPATCH
  # Insert before execve
  sed -i '' '/^[[:space:]]*execve(/{
    r /tmp/wrapper_patch.c
  }' "$WRAPPER_SRC"
  echo "  ✅ Wrapper.c patched"
fi

# Patch 2: VST3EffectsModule.cpp — VST3_PATH env var support
if [ "$SCRIPT_MODE" != "no-vst3" ]; then
  VST3_MODULE="$AUDACITY_SRC/libraries/lib-vst3/VST3EffectsModule.cpp"
  if grep -q "VST3_PATH" "$VST3_MODULE" 2>/dev/null; then
    echo "  ✅ VST3_PATH patch already applied"
  else
    echo "  📝 Applying VST3_PATH patch..."
    # Add wx/tokenzr.h include
    sed -i '' 's/#include <wx\/utils.h>/#include <wx\/tokenzr.h>\n#include <wx\/utils.h>/' "$VST3_MODULE"
    # Add VST3_PATH scanning in FindModulePaths after existing path gathering
    sed -i '' '/\/\/ Check for VST3_PATH environment variable/{
      a\
      \  // HGE: Support VST3_PATH env var for bundled VST3 plugins\
      \  {\
      \     wxString vst3path;\
      \     if (wxGetEnv(wxT("VST3_PATH"), \&vst3path) \&\& !vst3path.empty())\
      \     {\
      \        wxStringTokenizer tok(vst3path, wxPATH_SEP);\
      \        while (tok.HasMoreTokens())\
      \           pathList.push_back(tok.GetNextToken());\
      \     }\
      \  }
    }' "$VST3_MODULE"
    echo "  ✅ VST3_PATH patched"
  fi
fi

# ── Step 2: Install module source files ──────────────────────────────────
echo ""
echo "═══════════════════════════════════════════════════════════════"
echo "  STEP 2: Install module source files"
echo "═══════════════════════════════════════════════════════════════"

MODULE_DIR="$AUDACITY_SRC/modules/plugin-manager/mod-plugin-manager"
mkdir -p "$MODULE_DIR"

# Map from our flat storage to the proper source tree.
# Using parallel indexed arrays for bash 3.x compatibility.
FILE_SRC=()
FILE_DST=()

add_file() {
  FILE_SRC[${#FILE_SRC[@]}]="$1"
  FILE_DST[${#FILE_DST[@]}]="$2"
}

add_file "modules__plugin-manager__mod-plugin-manager__PluginManagerModule.h" "PluginManagerModule.h"
add_file "modules__plugin-manager__mod-plugin-manager__PluginManagerModule.cpp" "PluginManagerModule.cpp"
add_file "modules__plugin-manager__mod-plugin-manager__PluginManagerPanel.h" "PluginManagerPanel.h"
add_file "modules__plugin-manager__mod-plugin-manager__PluginManagerPanel.cpp" "PluginManagerPanel.cpp"
add_file "modules__plugin-manager__mod-plugin-manager__PluginCache.h" "PluginCache.h"
add_file "modules__plugin-manager__mod-plugin-manager__PluginCache.cpp" "PluginCache.cpp"
add_file "modules__plugin-manager__mod-plugin-manager__PluginValidator.h" "PluginValidator.h"
add_file "modules__plugin-manager__mod-plugin-manager__PluginValidator.cpp" "PluginValidator.cpp"
add_file "HgeEffectBrowser.h" "HgeEffectBrowser.h"
add_file "HgeEffectBrowser.cpp" "HgeEffectBrowser.cpp"
add_file "HgeEffectModule.h" "HgeEffectModule.h"
add_file "HgeEffectModule.cpp" "HgeEffectModule.cpp"
add_file "modules__plugin-manager__mod-plugin-manager__CMakeLists.txt" "CMakeLists.txt"

for ((i=0; i<${#FILE_SRC[@]}; i++)); do
  src_file="$MODULE_SRC/${FILE_SRC[$i]}"
  dest_file="$MODULE_DIR/${FILE_DST[$i]}"
  if [ -f "$src_file" ]; then
    cp "$src_file" "$dest_file"
    echo "  ✅ ${FILE_DST[$i]}"
  else
    echo "  ⚠️  Missing: $src_file"
  fi
done

# Copy plugin-aliases.xml to app support resources
ALIASES_DEST="$AUDACITY_SRC/build-hge/Release/HgeMusicStudio.app/Contents/Resources/plugin-aliases.xml"
if [ -f "$MODULE_SRC/plugin-aliases.xml" ]; then
  mkdir -p "$(dirname "$ALIASES_DEST")"
  cp "$MODULE_SRC/plugin-aliases.xml" "$ALIASES_DEST"
  echo "  ✅ plugin-aliases.xml"
fi

# Copy PluginCategoryManager and PluginDisplayName to libraries
LIBSRC="$AUDACITY_SRC/libraries/lib-plugin-category"
mkdir -p "$LIBSRC"
cp "$MODULE_SRC/PluginCategoryManager.h" "$LIBSRC/" 2>/dev/null || true
cp "$MODULE_SRC/PluginCategoryManager.cpp" "$LIBSRC/" 2>/dev/null || true
cp "$MODULE_SRC/PluginDisplayName.h" "$LIBSRC/" 2>/dev/null || true
cp "$MODULE_SRC/PluginDisplayName.cpp" "$LIBSRC/" 2>/dev/null || true

# ── Step 3: Conan dependencies ────────────────────────────────────────────
echo ""
echo "═══════════════════════════════════════════════════════════════"
echo "  STEP 3: Install dependencies"
echo "═══════════════════════════════════════════════════════════════"

cd "$AUDACITY_SRC"
mkdir -p "$BUILD_DIR"

if [ "$SCRIPT_MODE" == "quick" ]; then
  echo "  ⚡ Quick mode: skipping Conan (using cached deps)"
else
  # Check if Conan is installed
  if ! command -v conan &>/dev/null; then
    echo "  ❌ Conan package manager not found!"
    echo ""
    echo "     Install Conan with:"
    echo "     pip install --user conan==1.64"
    echo ""
    echo "     Or if you don't have pip:"
    echo "     python3 -m pip install --user conan==1.64"
    echo ""
    echo "     After installing, verify:"
    echo "     conan --version"
    echo ""
    echo "  ⚡ Falling back to quick mode (skipping Conan)..."
    echo "  ⚡ CMake may fail if dependencies aren't cached."
  else
    echo "  📦 Running Conan..."
    # Apple Clang 21 not recognized by Conan 1.64 — shim to v15
    CONAN_FLAGS="-s build_type=Release -s compiler=apple-clang -s compiler.version=15 -s compiler.libcxx=libc++"
    if [ "$SCRIPT_MODE" != "no-vst3" ]; then
      CONAN_FLAGS="$CONAN_FLAGS -o vst3sdk=True"
    fi
    conan install . --build=missing $CONAN_FLAGS 2>&1 | sed 's/^/     /'
    echo "  ✅ Conan complete"
  fi
fi

# ── Step 4: CMake configuration ───────────────────────────────────────────
echo ""
echo "═══════════════════════════════════════════════════════════════"
echo "  STEP 4: CMake configuration"
echo "═══════════════════════════════════════════════════════════════"

CMAKE_FLAGS="-DCMAKE_BUILD_TYPE=Release"
CMAKE_FLAGS="$CMAKE_FLAGS -Daudacity_has_vst2=ON"
CMAKE_FLAGS="$CMAKE_FLAGS -Daudacity_has_lv2=ON"
CMAKE_FLAGS="$CMAKE_FLAGS -Daudacity_has_audiocom=OFF"

if [ "$SCRIPT_MODE" != "no-vst3" ]; then
  CMAKE_FLAGS="$CMAKE_FLAGS -Daudacity_has_vst3=ON"
  echo "  🔧 VST3: ENABLED"
else
  CMAKE_FLAGS="$CMAKE_FLAGS -Daudacity_has_vst3=OFF"
  echo "  🔧 VST3: DISABLED"
fi

if [ "$SCRIPT_MODE" == "no-browser" ]; then
  echo "  🔧 Custom browser: DISABLED"
else
  echo "  🔧 Custom browser: ENABLED"
  CMAKE_FLAGS="$CMAKE_FLAGS -DHGE_EFFECT_BROWSER=ON"
fi

echo ""
cmake -S "$AUDACITY_SRC" -B "$BUILD_DIR" -G "Unix Makefiles" $CMAKE_FLAGS \
  2>&1 | sed 's/^/     /'

echo "  ✅ CMake configured"

# ── Step 5: Build ─────────────────────────────────────────────────────────
echo ""
echo "═══════════════════════════════════════════════════════════════"
echo "  STEP 5: Building"
echo "═══════════════════════════════════════════════════════════════"
echo "  This takes 5-15 minutes on Apple Silicon..."
echo ""

cmake --build "$BUILD_DIR" --target HgeMusicStudio -- -j$(sysctl -n hw.ncpu) \
  2>&1 | sed 's/^/     /'

echo "  ✅ Build complete"

# ── Step 6: Rebuild Wrapper ───────────────────────────────────────────────
echo ""
echo "═══════════════════════════════════════════════════════════════"
echo "  STEP 6: Rebuilding Wrapper"
echo "═══════════════════════════════════════════════════════════════"

APP="$BUILD_DIR/Release/HgeMusicStudio.app"
/usr/bin/cc \
  -DAUDACITY_BUNDLE_EXECUTABLE=\"HgeMusicStudio\" \
  -O3 -arch arm64 \
  -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk \
  -mmacosx-version-min=10.15 \
  -o "$APP/Contents/MacOS/Wrapper" \
  "$AUDACITY_SRC/mac/Wrapper.c" \
  2>&1 | sed 's/^/     /'

echo "  ✅ Wrapper rebuilt"

# ── Step 7: Copy bundled plugins ──────────────────────────────────────────
echo ""
echo "═══════════════════════════════════════════════════════════════"
echo "  STEP 7: Copying bundled plugins"
echo "═══════════════════════════════════════════════════════════════"

PLUGINS="$APP/Contents/plug-ins"
mkdir -p "$PLUGINS/VST2" "$PLUGINS/VST3"

# VST2 plugins
for src in \
  "/Library/Audio/Plug-Ins/VST/TDR Nova.vst" \
  "/Library/Audio/Plug-Ins/VST/Softube/Saturation Knob.vst"; do
  if [ -d "$src" ]; then
    cp -R "$src" "$PLUGINS/VST2/" 2>/dev/null && echo "  ✅ $(basename "$src")"
  fi
done

# TDR Kotelnikov VST2 (may be at different path)
for src in \
  "/Library/Audio/Plug-Ins/VST/TDR Kotelnikov.vst" \
  "/Users/homegrownentllc/HgeMusicStudio/HgeMusicStudio.app/Contents/plug-ins/VST2/TDR Kotelnikov.vst"; do
  if [ -d "$src" ]; then
    cp -R "$src" "$PLUGINS/VST2/" 2>/dev/null && echo "  ✅ $(basename "$src")"
    break
  fi
done

# VST3 plugins
for src in \
  "/Library/Audio/Plug-Ins/VST3/SSQ.vst3" \
  "/Users/homegrownentllc/HgeMusicStudio/HgeMusicStudio.app/Contents/plug-ins/VST3/SSQ.vst3"; do
  if [ -d "$src" ]; then
    cp -R "$src" "$PLUGINS/VST3/" 2>/dev/null && echo "  ✅ $(basename "$src")"
    break
  fi
done

for src in \
  "/Library/Audio/Plug-Ins/VST3/TDR Nova.vst3" \
  "/Users/homegrownentllc/HgeMusicStudio/HgeMusicStudio.app/Contents/plug-ins/VST3/TDR Nova.vst3"; do
  if [ -d "$src" ]; then
    cp -R "$src" "$PLUGINS/VST3/" 2>/dev/null && echo "  ✅ $(basename "$src")"
    break
  fi
done

for src in \
  "/Library/Audio/Plug-Ins/VST3/TDR Kotelnikov.vst3" \
  "/Users/homegrownentllc/HgeMusicStudio/HgeMusicStudio.app/Contents/plug-ins/VST3/TDR Kotelnikov.vst3"; do
  if [ -d "$src" ]; then
    cp -R "$src" "$PLUGINS/VST3/" 2>/dev/null && echo "  ✅ $(basename "$src")"
    break
  fi
done

# Nyquist scripts
NYQUIST_SRC="$AUDACITY_SRC/nyquist"
if [ -d "$NYQUIST_SRC" ]; then
  cp "$NYQUIST_SRC"/*.ny "$PLUGINS/" 2>/dev/null && echo "  ✅ Nyquist scripts"
fi

# ── Step 8: Copy app to HGE directory ─────────────────────────────────────
echo ""
echo "═══════════════════════════════════════════════════════════════"
echo "  STEP 8: Installing to HGE directory"
echo "═══════════════════════════════════════════════════════════════"

HGE_APP="$HGE_DIR/HgeMusicStudio.app"
rm -rf "$HGE_APP"
cp -R "$APP" "$HGE_APP"
echo "  ✅ Installed to: $HGE_APP"

# ── Step 9: Verification ──────────────────────────────────────────────────
echo ""
echo "═══════════════════════════════════════════════════════════════"
echo "  STEP 9: Verification"
echo "═══════════════════════════════════════════════════════════════"

echo ""
echo "  Architecture:"
file "$HGE_APP/Contents/MacOS/Wrapper" | sed 's/^/    /'
file "$HGE_APP/Contents/MacOS/HgeMusicStudio" | sed 's/^/    /'

echo ""
echo "  VST2 plugins:"
ls "$HGE_APP/Contents/plug-ins/VST2/" 2>/dev/null | sed 's/^/    • /' || echo "    (none)"

echo ""
echo "  VST3 plugins:"
ls "$HGE_APP/Contents/plug-ins/VST3/" 2>/dev/null | sed 's/^/    • /' || echo "    (none)"

echo ""
echo "  Nyquist scripts:"
ls "$HGE_APP/Contents/plug-ins/"*.ny 2>/dev/null | wc -l | sed 's/^/    /' | xargs echo "    count:"

echo ""
echo "  App size:"
du -sh "$HGE_APP" | sed 's/^/    /'

echo ""
echo "  VST3 support in binary:"
if strings "$HGE_APP/Contents/MacOS/HgeMusicStudio" | grep -q "VST3"; then
  echo "    ✅ VST3 symbols present"
else
  echo "    ⚠️  No VST3 symbols found"
fi

echo ""
echo "  Module loaded:"
if [ -f "$HGE_APP/Contents/modules/mod-plugin-manager.so" ]; then
  echo "    ✅ mod-plugin-manager.so present"
  file "$HGE_APP/Contents/modules/mod-plugin-manager.so" | sed 's/^/    /'
else
  echo "    ⚠️  mod-plugin-manager.so NOT found in modules/"
fi

# ── Optional: Build DMG ──────────────────────────────────────────────────
if [ "$BUILD_DMG" == "yes" ]; then
  echo ""
  echo "═══════════════════════════════════════════════════════════════"
  echo "  STEP 10: Building DMG"
  echo "═══════════════════════════════════════════════════════════════"

  STAGING="/tmp/hge-dmg-staging"
  DMG_NAME="HgeMusicStudio-4.0.0"
  DMG_PATH="$HOME/Desktop/$DMG_NAME.dmg"

  rm -rf "$STAGING"
  mkdir -p "$STAGING"
  cp -R "$HGE_APP" "$STAGING/"
  ln -s /Applications "$STAGING/Applications"

  # Delete old DMGs on Desktop
  rm -f "$HOME/Desktop/HgeMusicStudio"*.dmg 2>/dev/null || true

  hdiutil create -volname "Hge Music Studio" \
    -srcfolder "$STAGING" \
    -ov -format UDZO \
    -fs HFS+ \
    -megabytes 512 \
    "$DMG_PATH" 2>&1 | sed 's/^/     /'

  rm -rf "$STAGING"
  echo "  ✅ DMG: $DMG_PATH ($(du -h "$DMG_PATH" | cut -f1))"
  echo "  📍 Desktop"
fi

# ── Done ──────────────────────────────────────────────────────────────────
echo ""
echo "╔══════════════════════════════════════════════════════════════╗"
echo "║  ✅ BUILD COMPLETE                                          ║"
echo "╚══════════════════════════════════════════════════════════════╝"
echo ""
echo "  App: $HGE_APP"
echo ""
echo "  📋 Next steps:"
echo "     1. Open the app: open \"$HGE_APP\""
echo "     2. Open Tools → HGE Effect Browser (or Ctrl+Shift+E)"
echo "     3. Verify plugins show in categories"
echo "     4. If stock menu still shows, it's normal — browser is additive"
echo ""
echo "  🔧 To reset plugin registry:"
echo "     rm ~/Library/Application\\ Support/HgeMusicStudio/pluginregistry.cfg"
echo ""
echo "  🔧 To disable browser (fallback to stock):"
echo "     rm \"$HGE_APP/Contents/modules/mod-plugin-manager.so\""
echo ""
