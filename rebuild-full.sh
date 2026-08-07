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
AUDACITY_SRC="/Users/homegrownentllc/HgeMusicStudio/audacity-3.7.7"
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

# Generate a portable Python patcher script (avoids macOS sed/quotas/attr issues)
PY_PATCHER="/tmp/hge_patcher.py"
cat > "$PY_PATCHER" << 'PYEOF'
import os, sys, tempfile

def safe_write(path, content):
    """Write file via temp + atomic replace to bypass macOS provenance restrictions."""
    fd, tmp = tempfile.mkstemp(dir=os.path.dirname(path), prefix='.hge_patch_')
    try:
        with os.fdopen(fd, 'w') as f:
            f.write(content)
        os.replace(tmp, path)
    except:
        if os.path.exists(tmp):
            os.unlink(tmp)
        raise

def patch_wrapper(src):
    if not os.path.isfile(src):
        print(f"  ⚠️  Wrapper.c not found: {src}")
        return False
    with open(src, 'r') as f:
        content = f.read()
    if 'VST_PATH' in content and 'VST3_PATH' in content:
        print("  ✅ Wrapper.c bundled plugin paths already applied")
        return True
    block = (
        '  // HGE: Set bundled plugin paths for self-contained DMGs\n'
        '  {\n'
        '    size_t path_len = strlen(path);\n'
        '    char *res_dir = alloca(path_len + 32);\n'
        '    strcpy(res_dir, path);\n'
        '    char *p = strrchr(res_dir, \'/\');\n'
        '    if (p) strcpy(p, "/..");\n'
        '\n'
        '    char plugins_dir[1024];\n'
        '    char vst3_dir[1024];\n'
        '    snprintf(plugins_dir, sizeof(plugins_dir), "%s/Plug-Ins", res_dir);\n'
        '    snprintf(vst3_dir, sizeof(vst3_dir), "%s/plug-ins/VST3", res_dir);\n'
        '    setenv("VST_PATH", plugins_dir, 0);\n'
        '    setenv("VST3_PATH", vst3_dir, 0);\n'
        '    setenv("LV2_PATH", plugins_dir, 0);\n'
        '  }\n'
    )
    content = content.replace('  execve(', block + '  execve(', 1)
    safe_write(src, content)
    print("  ✅ Wrapper.c patched")
    return True

def patch_vst3(src):
    if not os.path.isfile(src):
        print(f"  ⚠️  VST3EffectsModule.cpp not found: {src}")
        return False
    with open(src, 'r') as f:
        content = f.read()
    if 'VST3_PATH' in content:
        print("  ✅ VST3_PATH patch already applied")
        return True
    if '#include <wx/tokenzr.h>' not in content:
        content = content.replace(
            '#include <wx/utils.h>',
            '#include <wx/tokenzr.h>\n#include <wx/utils.h>', 1
        )
    block = (
        '\n  // HGE: Support VST3_PATH env var for bundled VST3 plugins\n'
        '  {\n'
        '     wxString vst3path;\n'
        '     if (wxGetEnv(wxT("VST3_PATH"), &vst3path) && !vst3path.empty())\n'
        '     {\n'
        '        wxStringTokenizer tok(vst3path, wxPATH_SEP);\n'
        '        while (tok.HasMoreTokens())\n'
        '           pathList.push_back(tok.GetNextToken());\n'
        '     }\n'
        '  }\n'
    )
    # Insert inside FindModulePaths(), after custom paths are appended to
    # pathList and before the traversal loop — must be function-scope code,
    # never dropped at file scope (that fails to compile).
    anchor = 'std::copy(customPaths.begin(), customPaths.end(), std::back_inserter(pathList));\n   }\n'
    if anchor in content:
        content = content.replace(anchor, anchor + block, 1)
    else:
        print("  ⚠️  Could not find safe anchor point for VST3_PATH patch; skipping (manual patch needed)")
        return False
    safe_write(src, content)
    print("  ✅ VST3_PATH patched")
    return True

if __name__ == '__main__':
    cmd = sys.argv[1] if len(sys.argv) > 1 else ''
    path = sys.argv[2] if len(sys.argv) > 2 else ''
    if cmd == 'wrapper':
        patch_wrapper(path)
    elif cmd == 'vst3':
        patch_vst3(path)
    else:
        print(f"Usage: {sys.argv[0]} <wrapper|vst3> <path>")
        sys.exit(1)
PYEOF

# Strip macOS provenance attribute if present (blocks in-place edits on Sequoia)
for f in "$AUDACITY_SRC/mac/Wrapper.c" "$AUDACITY_SRC/libraries/lib-vst3/VST3EffectsModule.cpp"; do
  if [ -f "$f" ]; then
    xattr -d com.apple.provenance "$f" 2>/dev/null || true
  fi
done

# Patch 1: Wrapper.c — VST_PATH + LV2_PATH env vars
WRAPPER_SRC="$AUDACITY_SRC/mac/Wrapper.c"
python3 "$PY_PATCHER" wrapper "$WRAPPER_SRC" 2>&1 | sed 's/^/  /' || {
  # Fallback: copy, patch the copy, copy back (bypasses provenance attr)
  echo "  ⚠️  Python patch failed, trying cp-based fallback..."
  TMPFILE=$(mktemp)
  cp "$WRAPPER_SRC" "$TMPFILE"
  python3 "$PY_PATCHER" wrapper "$TMPFILE" 2>&1 | sed 's/^/  /'
  cp "$TMPFILE" "$WRAPPER_SRC"
  rm -f "$TMPFILE"
  echo "  ✅ Wrapper.c patched (cp fallback)"
}

# Patch 2: VST3EffectsModule.cpp — VST3_PATH env var support
if [ "$SCRIPT_MODE" != "no-vst3" ]; then
  VST3_MODULE="$AUDACITY_SRC/libraries/lib-vst3/VST3EffectsModule.cpp"
  python3 "$PY_PATCHER" vst3 "$VST3_MODULE" 2>&1 | sed 's/^/  /' || {
    echo "  ⚠️  Python patch failed, trying cp-based fallback..."
    TMPFILE=$(mktemp)
    cp "$VST3_MODULE" "$TMPFILE"
    python3 "$PY_PATCHER" vst3 "$TMPFILE" 2>&1 | sed 's/^/  /'
    cp "$TMPFILE" "$VST3_MODULE"
    rm -f "$TMPFILE"
    echo "  ✅ VST3_PATH patched (cp fallback)"
  }
fi

rm -f "$PY_PATCHER"

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
add_file "HgeAiChat.h" "HgeAiChat.h"
add_file "HgeAiChat.cpp" "HgeAiChat.cpp"
add_file "HgeAiSettings.h" "HgeAiSettings.h"
add_file "HgeAiSettings.cpp" "HgeAiSettings.cpp"
# Kept local to the module (not in libraries/) because CMake's source_group
# requires all module SOURCES to live under the module's own directory.
add_file "PluginCategoryManager.h" "PluginCategoryManager.h"
add_file "PluginCategoryManager.cpp" "PluginCategoryManager.cpp"
add_file "PluginDisplayName.h" "PluginDisplayName.h"
add_file "PluginDisplayName.cpp" "PluginDisplayName.cpp"
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

# Wire the plugin-manager module into the build. Copying the files above is
# not enough — CMake only descends into modules/plugin-manager if it has its
# own CMakeLists.txt AND is listed in modules/CMakeLists.txt's FOLDERS.
PLUGIN_MGR_CMAKE="$AUDACITY_SRC/modules/plugin-manager/CMakeLists.txt"
if [ ! -f "$PLUGIN_MGR_CMAKE" ]; then
  cp "$MODULE_SRC/modules__plugin-manager__CMakeLists.txt" "$PLUGIN_MGR_CMAKE"
  echo "  ✅ modules/plugin-manager/CMakeLists.txt"
fi

MODULES_ROOT_CMAKE="$AUDACITY_SRC/modules/CMakeLists.txt"
if [ -f "$MODULES_ROOT_CMAKE" ] && ! grep -q "plugin-manager" "$MODULES_ROOT_CMAKE"; then
  python3 -c "
import sys
p = sys.argv[1]
with open(p) as f:
    c = f.read()
c = c.replace('   sharing\n)', '   sharing\n   plugin-manager\n)', 1)
with open(p, 'w') as f:
    f.write(c)
" "$MODULES_ROOT_CMAKE"
  echo "  ✅ modules/CMakeLists.txt (added plugin-manager to FOLDERS)"
fi

# HGE UX/productization overlays for stock Audacity source paths.
# These keep the creator startup template, HGE menu labels, hidden legacy
# effect-store entry, and provider text replacement stable across rebuilds.
copy_overlay() {
  local src_file="$MODULE_SRC/$1"
  local dest_file="$AUDACITY_SRC/$2"
  if [ -f "$src_file" ]; then
    mkdir -p "$(dirname "$dest_file")"
    cp "$src_file" "$dest_file"
    echo "  ✅ $2"
  else
    echo "  ⚠️  Missing overlay: $src_file"
  fi
}

copy_overlay "src__ProjectManager.cpp" "src/ProjectManager.cpp"
copy_overlay "src__ProjectAudioManager.cpp" "src/ProjectAudioManager.cpp"
copy_overlay "src__ProjectFileManager.cpp" "src/ProjectFileManager.cpp"
copy_overlay "src__ProjectFileManager.h" "src/ProjectFileManager.h"
copy_overlay "src__DropTarget.cpp" "src/DropTarget.cpp"
copy_overlay "src__menus__PluginMenus.cpp" "src/menus/PluginMenus.cpp"
copy_overlay "src__PluginRegistrationDialog.cpp" "src/PluginRegistrationDialog.cpp"
copy_overlay "src__RealtimeEffectPanel.cpp" "src/RealtimeEffectPanel.cpp"
copy_overlay "src__TrackInfo.h" "src/TrackInfo.h"
copy_overlay "src__tracks__playabletrack__ui__PlayableTrackControls.cpp" "src/tracks/playabletrack/ui/PlayableTrackControls.cpp"
copy_overlay "src__tracks__playabletrack__ui__PlayableTrackControls.h" "src/tracks/playabletrack/ui/PlayableTrackControls.h"
copy_overlay "src__tracks__playabletrack__ui__PlayableTrackButtonHandles.cpp" "src/tracks/playabletrack/ui/PlayableTrackButtonHandles.cpp"
copy_overlay "src__tracks__playabletrack__ui__PlayableTrackButtonHandles.h" "src/tracks/playabletrack/ui/PlayableTrackButtonHandles.h"
copy_overlay "src__tracks__playabletrack__wavetrack__ui__WaveTrackControls.cpp" "src/tracks/playabletrack/wavetrack/ui/WaveTrackControls.cpp"
copy_overlay "src__tracks__playabletrack__wavetrack__ui__WaveTrackControls.h" "src/tracks/playabletrack/wavetrack/ui/WaveTrackControls.h"
copy_overlay "src__tracks__playabletrack__notetrack__ui__NoteTrackControls.cpp" "src/tracks/playabletrack/notetrack/ui/NoteTrackControls.cpp"
copy_overlay "src__tracks__playabletrack__notetrack__ui__NoteTrackControls.h" "src/tracks/playabletrack/notetrack/ui/NoteTrackControls.h"

# HGE branding — app name, About dialog, splash screen, app icon
copy_overlay "libraries__lib-utility__ModuleConstants.cpp" "libraries/lib-utility/ModuleConstants.cpp"
copy_overlay "src__AboutDialog.cpp" "src/AboutDialog.cpp"
copy_overlay "images__AudacityLogoWithName.xpm" "images/AudacityLogoWithName.xpm"
copy_overlay "images__Audacity-splash.xpm" "images/Audacity-splash.xpm"
copy_overlay "mac__Resources__Audacity.icns" "mac/Resources/Audacity.icns"
# Black & gold track/UI color theme
copy_overlay "libraries__lib-theme__AllThemeResources.h" "libraries/lib-theme/AllThemeResources.h"
# Neutered Help buttons (no more manual.audacityteam.org / support.audacityteam.org)
copy_overlay "libraries__lib-wx-init__HelpSystem.cpp" "libraries/lib-wx-init/HelpSystem.cpp"
copy_overlay "libraries__lib-wx-init__ErrorReportDialog.cpp" "libraries/lib-wx-init/ErrorReportDialog.cpp"
# Window title default, Help menu (Quick Help/Manual/Audacity Support removed,
# About renamed), Welcome-to-Audacity startup dialog disabled
copy_overlay "libraries__lib-project-file-io__ProjectFileIO.cpp" "libraries/lib-project-file-io/ProjectFileIO.cpp"
copy_overlay "src__menus__HelpMenus.cpp" "src/menus/HelpMenus.cpp"
copy_overlay "src__AudacityApp.cpp" "src/AudacityApp.cpp"

# Full branding sweep — every remaining reachable "Audacity" string found by
# an independent audit (dialog titles, error messages, vendor/description
# strings, Preferences panels, effect "get more" links, etc.)
copy_overlay "src__widgets__MissingPluginsErrorDialog.cpp" "src/widgets/MissingPluginsErrorDialog.cpp"
copy_overlay "src__TimerRecordDialog.cpp" "src/TimerRecordDialog.cpp"
copy_overlay "src__AutoRecoveryDialog.cpp" "src/AutoRecoveryDialog.cpp"
copy_overlay "libraries__lib-wx-init__LogWindow.cpp" "libraries/lib-wx-init/LogWindow.cpp"
copy_overlay "src__toolbars__ToolBar.cpp" "src/toolbars/ToolBar.cpp"
copy_overlay "src__MixerBoard.cpp" "src/MixerBoard.cpp"
copy_overlay "src__AudacityFileConfig.cpp" "src/AudacityFileConfig.cpp"
copy_overlay "src__AudacityMirProject.cpp" "src/AudacityMirProject.cpp"
copy_overlay "src__effects__VST__VSTEffect.cpp" "src/effects/VST/VSTEffect.cpp"
copy_overlay "src__Legacy.cpp" "src/Legacy.cpp"
copy_overlay "libraries__lib-project-file-io__ProjectSerializer.cpp" "libraries/lib-project-file-io/ProjectSerializer.cpp"
copy_overlay "libraries__lib-theme__Theme.cpp" "libraries/lib-theme/Theme.cpp"
copy_overlay "libraries__lib-files__FileException.cpp" "libraries/lib-files/FileException.cpp"
copy_overlay "libraries__lib-import-export__Import.cpp" "libraries/lib-import-export/Import.cpp"
copy_overlay "libraries__lib-exceptions__InconsistencyException.cpp" "libraries/lib-exceptions/InconsistencyException.cpp"
copy_overlay "libraries__lib-module-manager__ModuleManager.cpp" "libraries/lib-module-manager/ModuleManager.cpp"
copy_overlay "libraries__lib-module-manager__ModuleSettings.cpp" "libraries/lib-module-manager/ModuleSettings.cpp"
copy_overlay "src__prefs__ApplicationPrefs.cpp" "src/prefs/ApplicationPrefs.cpp"
copy_overlay "src__prefs__KeyConfigPrefs.cpp" "src/prefs/KeyConfigPrefs.cpp"
copy_overlay "src__prefs__ImportExportPrefs.cpp" "src/prefs/ImportExportPrefs.cpp"
copy_overlay "src__prefs__TracksBehaviorsPrefs.cpp" "src/prefs/TracksBehaviorsPrefs.cpp"
copy_overlay "src__prefs__PrefsDialog.cpp" "src/prefs/PrefsDialog.cpp"
copy_overlay "src__menus__PluginMenus.cpp" "src/menus/PluginMenus.cpp"
copy_overlay "src__PluginRegistrationDialog.cpp" "src/PluginRegistrationDialog.cpp"
copy_overlay "src__effects__EqualizationCurvesDialog.cpp" "src/effects/EqualizationCurvesDialog.cpp"
copy_overlay "libraries__lib-effects__LoadEffects.cpp" "libraries/lib-effects/LoadEffects.cpp"
copy_overlay "libraries__lib-effects__Effect.cpp" "libraries/lib-effects/Effect.cpp"
copy_overlay "libraries__lib-vst__VSTEffectsModule.cpp" "libraries/lib-vst/VSTEffectsModule.cpp"
copy_overlay "libraries__lib-vst3__VST3EffectsModule.cpp" "libraries/lib-vst3/VST3EffectsModule.cpp"
copy_overlay "libraries__lib-lv2__LoadLV2.cpp" "libraries/lib-lv2/LoadLV2.cpp"
copy_overlay "libraries__lib-ladspa__LadspaEffectsModule.cpp" "libraries/lib-ladspa/LadspaEffectsModule.cpp"
copy_overlay "libraries__lib-nyquist-effects__LoadNyquist.cpp" "libraries/lib-nyquist-effects/LoadNyquist.cpp"
copy_overlay "libraries__lib-nyquist-effects__NyquistBase.cpp" "libraries/lib-nyquist-effects/NyquistBase.cpp"
copy_overlay "libraries__lib-audio-unit__AudioUnitEffectsModule.cpp" "libraries/lib-audio-unit/AudioUnitEffectsModule.cpp"
copy_overlay "src__effects__vamp__LoadVamp.cpp" "src/effects/vamp/LoadVamp.cpp"
copy_overlay "src__commands__LoadCommands.cpp" "src/commands/LoadCommands.cpp"
copy_overlay "src__commands__AudacityCommand.cpp" "src/commands/AudacityCommand.cpp"
copy_overlay "libraries__lib-preference-pages__PrefsPanel.cpp" "libraries/lib-preference-pages/PrefsPanel.cpp"
copy_overlay "libraries__lib-wx-init__HelpText.cpp" "libraries/lib-wx-init/HelpText.cpp"
copy_overlay "libraries__lib-wave-track__Sequence.cpp" "libraries/lib-wave-track/Sequence.cpp"
# File > Quit menu item, and a few last stragglers found by a final broad sweep
copy_overlay "src__menus__FileMenus.cpp" "src/menus/FileMenus.cpp"
copy_overlay "libraries__lib-audio-io__AudioIO.cpp" "libraries/lib-audio-io/AudioIO.cpp"

# Copy plugin-aliases.xml to app support resources
ALIASES_DEST="$AUDACITY_SRC/build-hge/Release/HgeMusicStudio.app/Contents/Resources/plugin-aliases.xml"
if [ -f "$MODULE_SRC/plugin-aliases.xml" ]; then
  mkdir -p "$(dirname "$ALIASES_DEST")"
  cp "$MODULE_SRC/plugin-aliases.xml" "$ALIASES_DEST"
  echo "  ✅ plugin-aliases.xml"
fi

# ── Step 3: Verify build environment ─────────────────────────────────────
echo ""
echo "═══════════════════════════════════════════════════════════════"
echo "  STEP 3: Verify build environment"
echo "═══════════════════════════════════════════════════════════════"

cd "$AUDACITY_SRC"
mkdir -p "$BUILD_DIR"

# Check for Conan — CMake handles Conan internally via audacity_conan_enabled
echo "  🔍 Checking build tools..."
if ! command -v conan &>/dev/null; then
  echo "  ❌ Conan package manager not found!"
  echo ""
  echo "     This build requires Conan for dependency management."
  echo "     Install it with:"
  echo "     python3 -m pip install --user conan==1.64"
  echo ""
  echo "  ⚠️  Build cannot proceed without Conan."
  exit 1
fi

# Show versions
echo "  ✅ Conan:    $(conan --version 2>&1)"
echo "  ✅ CMake:    $(cmake --version 2>&1 | head -1)"
echo "  ✅ Compiler: $(/usr/bin/cc --version 2>&1 | head -1)"
echo ""

# ── Step 4: CMake configuration ───────────────────────────────────────────
echo ""
echo "═══════════════════════════════════════════════════════════════"
echo "  STEP 4: CMake configuration"
echo "═══════════════════════════════════════════════════════════════"

CMAKE_FLAGS=(
  -DCMAKE_BUILD_TYPE=Release
  -Daudacity_has_vst2=ON
  -Daudacity_has_lv2=ON
  -Daudacity_has_audiocom=OFF
  # CRITICAL: the stock updater will download and install the REAL Audacity
  # over this rebrand (checks updates.audacityteam.org, shows an "Install
  # Audacity 4" promo, runs the downloaded installer) — must stay off.
  -Daudacity_has_updates_check=OFF
  -Daudacity_conan_enabled=ON
  -Daudacity_conan_allow_prebuilt_binaries=ON
  # No custom profiles — conan_runner.py auto-generates them with correct Clang version
  -Daudacity_obey_system_dependencies=OFF
  -Daudacity_lib_preference=local
)

if [ "$SCRIPT_MODE" != "no-vst3" ]; then
  CMAKE_FLAGS+=( -Daudacity_has_vst3=ON )
  echo "  🔧 VST3: ENABLED"
else
  CMAKE_FLAGS+=( -Daudacity_has_vst3=OFF )
  echo "  🔧 VST3: DISABLED"
fi

if [ "$SCRIPT_MODE" == "no-browser" ]; then
  echo "  🔧 Custom browser: DISABLED"
else
  echo "  🔧 Custom browser: ENABLED"
  CMAKE_FLAGS+=( -DHGE_EFFECT_BROWSER=ON )
fi

echo ""
cmake -S "$AUDACITY_SRC" -B "$BUILD_DIR" -G "Unix Makefiles" "${CMAKE_FLAGS[@]}" \
  2>&1 | sed 's/^/     /'

echo "  ✅ CMake configured"

# ── Step 5: Build ─────────────────────────────────────────────────────────
echo ""
echo "═══════════════════════════════════════════════════════════════"
echo "  STEP 5: Building"
echo "═══════════════════════════════════════════════════════════════"
echo "  This takes 5-15 minutes on Apple Silicon..."
echo ""

# No --target: build the default "all" target so every module (mod-mp3,
# mod-flac, mod-script-pipe, mod-plugin-manager, etc.) gets compiled too —
# "--target Audacity" only builds the main app and skips every module dylib.
cmake --build "$BUILD_DIR" -- -j$(sysctl -n hw.ncpu) \
  2>&1 | sed 's/^/     /'

echo "  ✅ Build complete"

# ── Step 5b: Rename bundle to HgeMusicStudio.app ──────────────────────────
# Note: the CMake target/executable stays "Audacity" internally (renaming it
# would mean threading changes through the whole build system). Wrapper.c
# is told via AUDACITY_BUNDLE_EXECUTABLE to exec the "Audacity" binary, so
# only the outer .app folder + Info.plist branding need to change.
echo ""
echo "═══════════════════════════════════════════════════════════════"
echo "  STEP 5b: Renaming bundle to HgeMusicStudio.app"
echo "═══════════════════════════════════════════════════════════════"

# COPY (never move/rename) Audacity.app — CMake/Make's incremental build
# tracking keys off that exact path. If it gets renamed away, the next
# incremental build silently skips re-copying Contents/Frameworks (the main
# binary still relinks fine, so this is easy to miss: app builds, launches
# once, then dyld-fails on every framework dependency on the next rebuild).
RAW_APP="$BUILD_DIR/Release/Audacity.app"
APP="$BUILD_DIR/Release/HgeMusicStudio.app"
rm -rf "$APP"
cp -R "$RAW_APP" "$APP"

plutil -replace CFBundleName -string "HGE Music Studio" "$APP/Contents/Info.plist"
plutil -replace CFBundleDisplayName -string "HGE Music Studio" "$APP/Contents/Info.plist"
plutil -replace CFBundleIdentifier -string "com.hgemusicstudio.HgeMusicStudio" "$APP/Contents/Info.plist"
echo "  ✅ Bundle renamed + Info.plist rebranded"

# ── Step 6: Rebuild Wrapper ───────────────────────────────────────────────
echo ""
echo "═══════════════════════════════════════════════════════════════"
echo "  STEP 6: Rebuilding Wrapper"
echo "═══════════════════════════════════════════════════════════════"

/usr/bin/cc \
  -DAUDACITY_BUNDLE_EXECUTABLE=\"Audacity\" \
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

# HGE self-contained bundled plugins. These are tracked in module-source so
# customer DMGs do not depend on this Mac's /Library or ~/Library plugin folders.
if [ -d "$MODULE_SRC/bundled-plugins/VST2" ]; then
  cp -R "$MODULE_SRC/bundled-plugins/VST2/"* "$PLUGINS/VST2/" 2>/dev/null || true
  echo "  ✅ HGE bundled VST2 plugins"
fi

if [ -d "$MODULE_SRC/bundled-plugins/VST3" ]; then
  cp -R "$MODULE_SRC/bundled-plugins/VST3/"* "$PLUGINS/VST3/" 2>/dev/null || true
  echo "  ✅ HGE bundled VST3 plugins"
fi

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
file "$HGE_APP/Contents/MacOS/Audacity" | sed 's/^/    /'

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
if strings "$HGE_APP/Contents/MacOS/Audacity" | grep -q "VST3"; then
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
