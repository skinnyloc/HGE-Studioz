# HGE Music Studio — Self-Contained Plugin Architecture

## Overview

HGE Music Studio (based on Audacity 4.x / Muse Studio fork) is a professional DAW 
with a self-contained plugin ecosystem. All bundled plugins live inside the app 
bundle — no symlinks, no system installs, no terminal commands. DMG distribution 
works instantly.

## Architecture

### Plugin Support Status

| Format  | Status           | Notes                                 |
|---------|------------------|---------------------------------------|
| VST2    | ✅ Enabled       | Via `VSTEffectsModule` (compiled in)  |
| LV2     | ✅ Enabled       | Via `LV2EffectsModule` (compiled in)  |
| VST3    | ❌ Needs rebuild | Set `audacity_has_vst3=ON` in CMake   |
| AU      | ❌ Needs rebuild | Requires Audio Unit SDK setup         |
| Nyquist | ✅ Enabled       | 28 scripts bundled in plug-ins/       |

### Bundle Structure

```
HgeMusicStudio.app/
  Contents/
    MacOS/
      Wrapper           # Launcher — sets VST_PATH & LV2_PATH, execs main binary
      HgeMusicStudio    # Main DAW binary (VST2 + LV2 + Nyquist)
    plug-ins/            # (same as Plug-Ins on case-insensitive APFS)
      VST2/
        TDR Nova.vst          # TDR Nova EQ (universal binary)
        Saturation Knob.vst   # Softube Saturation Knob (universal, PACE iLok)
      VST3/
        TDR Nova.vst3         # TDR Nova VST3 (for future use)
        SSQ.vst3              # SSQ VST3 (for future use)
      AU/                     # Audio Units (future)
      HGE/                    # HGE Certified plugins (future)  
      Cache/                  # Plugin cache (future)
      Legacy/                 # Hidden legacy plugins (future)
      *.ny                    # 28 Nyquist scripts
    nyquist/                  # Nyquist runtime (33 files)
    modules/                  # 14 .so import/export modules
    Frameworks/               # 131 Qt + dependency frameworks
    Resources/                # Application resources
```

### Discovery Mechanism

The **Wrapper** launcher (modified from Audacity's `mac/Wrapper.c`) sets env vars:

```c
setenv("VST_PATH", "<bundle>/Contents/Plug-Ins", 0);   // VST2 discovery
setenv("LV2_PATH", "<bundle>/Contents/Plug-Ins", 0);   // LV2 discovery
```

The main binary scans these paths at startup via `Au3AudioPluginScanner`.
Plugins are registered in `~/Library/Application Support/HgeMusicStudio/pluginregistry.cfg`.

## Building from Source

### Prerequisites

- Xcode 15+ (with command line tools)
- CMake 3.25+
- Python 3.9+
- Conan package manager

### VST2 Build (Working)

The current build has VST2 enabled. To rebuild:

```bash
cd audacity-3.7.7
mkdir -p build && cd build
cmake .. -G "Unix Makefiles" \
  -DCMAKE_BUILD_TYPE=Release \
  -Daudacity_has_vst2=ON \
  -Daudacity_has_lv2=ON \
  -Daudacity_has_vst3=OFF
cmake --build . --target HgeMusicStudio
```

### VST3 Build (Needs Fixes)

To enable VST3, the `VST3_PATH` env var support must be added to 
`libraries/lib-vst3/VST3EffectsModule.cpp` (see Integration Guide below).
Then build with:

```bash
cmake .. -G "Unix Makefiles" \
  -DCMAKE_BUILD_TYPE=Release \
  -Daudacity_has_vst2=ON \
  -Daudacity_has_lv2=ON \
  -Daudacity_has_vst3=ON
```

### Wrapper Rebuild

The Wrapper needs recompiling whenever plugin paths change:

```bash
/usr/bin/cc \
  -DAUDACITY_BUNDLE_EXECUTABLE=\"HgeMusicStudio\" \
  -O3 -arch arm64 \
  -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk \
  -mmacosx-version-min=10.13 \
  -o Wrapper mac/Wrapper.c
```

## DMG Distribution

### Quick Command

```bash
./build-dmg.sh --dev        # Unsigned DMG for testing
./build-dmg.sh              # Signed DMG (requires Apple Developer ID)
./build-dmg.sh --notarize   # Signed + notarized DMG
```

### Environment Variables for Notarization

```bash
export SIGNING_IDENTITY="Developer ID Application: HGE Network LLC"
export TEAM_ID="XXXXXXXXXX"
export APPLE_ID="developer@hgenetwork.com"
export APPLE_PASSWORD="@keychain:AC_PASSWORD"
```

## Plugin Development

### Adding New Bundled Plugins

1. Copy the plugin bundle into `Contents/plug-ins/VST2/` (or `VST3/` once enabled)
2. Rebuild the Wrapper if paths change
3. On first launch, the app scans and registers the plugin
4. Verify in `~/Library/Application Support/HgeMusicStudio/pluginregistry.cfg`

### HGE Certified Plugin Badge

For HGE branded plugins, place them in `Contents/plug-ins/HGE/`.
The plugin display name system will show "HGE Certified" badges once the 
`lib-plugin-display` library is integrated.

## Plugin Manager Module (In Development)

The following modules are ready for integration but need compilation:

| Module | Files | Purpose |
|--------|-------|---------|
| `lib-plugin-display` | `PluginDisplayName.h/.cpp` | Named aliasing, vendor cleanup |
| `lib-plugin-curation` | `PluginCategoryManager.h/.cpp` | Category organization, favorites, stars |
| `mod-plugin-manager` | `PluginManagerModule(.h/.cpp)`, `PluginCache(.h/.cpp)`, `PluginValidator(.h/.cpp)`, `PluginManagerPanel(.h/.cpp)` | Full plugin management UI |

## Required Fixes for VST3

1. Add `VST3_PATH` env var support to `libraries/lib-vst3/VST3EffectsModule.cpp`
2. Fix the bundled VST3 path resolution (currently resolves to `MacOS/VST3/`)
3. Set `audacity_has_vst3=ON` in CMake configuration
4. Rebuild

## Troubleshooting

### Plugin Not Showing
- Check `Contents/plug-ins/VST2/` exists with .vst bundles
- Verify the Wrapper binary is set as CFBundleExecutable in Info.plist
- Check `~/Library/Application Support/HgeMusicStudio/pluginregistry.cfg`
- Launch from Terminal to see log output

### Saturation Knob (PACE/iLok) Issues
- PACE requires internet for first-time activation
- The `__Pace_Eden.bundle` must be inside the plugin bundle
- Some PACE versions need Rosetta 2 on Apple Silicon

### TCC / Sandbox Issues
- If the source directory becomes inaccessible, use `tccutil reset All`
- For development, keep a git clone outside the Documents folder
- The app bundle itself doesn't trigger TCC restrictions
