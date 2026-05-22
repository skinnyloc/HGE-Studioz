# HGE Music Studio — Phase 2 Execution Plan

**Goal**: Transform from technically functional to consumer-ready commercial DAW.

**Current Status**: Architecture complete. VST2 + LV2 working. 2 bundled plugins. 28 Nyquist scripts preserved. Plugin manager source code ready for compilation.

---

## ⚡ Priority 1: Full VST3 Rebuild

### Problem
VST3 is compiled out (`audacity_has_vst3=OFF`). The binary lacks VST3 symbols. This blocks SSQ.vst3, TDR Nova.vst3, and all future VST3 plugins.

### What Needs to Happen

**Step 1 — Apply VST3_PATH patch to VST3EffectsModule.cpp**

The stock Audacity VST3 scanner doesn't support `VST3_PATH` env var. We need to add it (mirroring VST2's `VST_PATH` support). The patch:

```cpp
// File: libraries/lib-vst3/VST3EffectsModule.cpp
// 1. Add include at top of file:
#include <wx/tokenzr.h>

// 2. Inside FindModulePaths(), after the opening brace and before
//    "//Note: The host recursively scans..." comment, add:
{
   wxString vst3path;
   if (wxGetEnv(wxT("VST3_PATH"), &vst3path) && !vst3path.empty())
   {
      wxStringTokenizer tok(vst3path, wxPATH_SEP);
      while (tok.HasMoreTokens())
         pathList.push_back(tok.GetNextToken());
   }
}
```

**Step 2 — Enable VST3 in CMake**

```bash
cd audacity-3.7.7
mkdir -p build-vst3 && cd build-vst3
cmake .. -G "Unix Makefiles" \
  -DCMAKE_BUILD_TYPE=Release \
  -Daudacity_has_vst2=ON \
  -Daudacity_has_lv2=ON \
  -Daudacity_has_vst3=ON
cmake --build . --target HgeMusicStudio -- -j$(sysctl -n hw.ncpu)
```

**Known Build Issue**: `conan_runner.py` may fail with `ModuleNotFoundError: No module named 'helpers.conan_environment'`. Fix:
```bash
export PYTHONPATH="$PYTHONPATH:$(pwd)/conan"
```

**Step 3 — Rebuild Wrapper with VST3_PATH**

```bash
/usr/bin/cc \
  -DAUDACITY_BUNDLE_EXECUTABLE=\"HgeMusicStudio\" \
  -O3 -arch arm64 \
  -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk \
  -mmacosx-version-min=10.13 \
  -o Wrapper mac/Wrapper.c
```

**Step 4 — Verify VST3**

```bash
# Check VST3 symbols exist in binary
nm HgeMusicStudio.app/Contents/MacOS/HgeMusicStudio | grep -i "vst3"
# Should show: VST3EffectsModule, VST3_HOST, etc.

# Check VST3_PATH is set
strings Wrapper | grep VST3_PATH
```

### Full Patch File Location
See `~/HgeMusicStudio/module-source/libraries__lib-vst3__VST3EffectsModule.cpp.edit3` for the complete patch.

---

## ⚡ Priority 2: Modern Effect Browser

### Current State
The app uses Audacity's QML-based plugin panels with nested vendor menus. No search, no categories, no favorites.

### Architecture

The effect browser is in `src/effects/effects_base/` (QML + C++). Key files:

| File | Purpose |
|------|---------|
| `src/effects/effects_base/view/EffectsPanel.qml` | Main effects panel UI |
| `src/effects/effects_base/view/EffectItem.qml` | Individual effect item |
| `src/effects/effects_base/view/EffectsView.qml` | Effects list view |
| `src/effects/effects_base/internal/EffectsProvider.cpp` | Effect data provider |

### Implementation Strategy

**Phase 2a — C++ Backend (use existing `PluginCategoryManager`)**

The `lib-plugin-curation` library (source in `~/HgeMusicStudio/module-source/`) provides:
- `PluginEntry` struct: internalName, category, displayName, sortOrder, visible, hgeCertified, starred
- `GetPluginsByCategory()` — grouped plugin lists
- `Search(query)` — fuzzy search across all plugins
- `GetStarredPlugins()` / `GetRecentPlugins()` — favorites & history
- `IsHgeCertified()` — badge checks

**Phase 2b — QML UI Overlay**

Create new QML components that replace the vendor submenu:

```
src/effects/effects_base/view/
├── HgePluginBrowser.qml          # Main browser panel (replaces old)
├── HgePluginCard.qml             # Plugin card with icon + name + badge
├── HgeCategoryList.qml           # Category sidebar
├── HgeSearchBar.qml              # Search with real-time filtering
├── HgeRecentPlugins.qml          # Recently used section
├── HgeFavoritesPanel.qml         # Starred/favorites section
└── HgePluginDetail.qml           # Detail panel when hovering
```

**Key UI States**:
1. Default — Category grid + search bar at top
2. Searching — Real-time filtered results as user types
3. Empty — "No plugins match your search" with suggestion
4. Detail — Plugin info on hover/click (name, vendor, category, version, HGE badge)

### Integration Points

1. Register `PluginCategoryManager` in the QML context:
```cpp
// In EffectsPanel.cpp or similar
QQmlEngine::setObjectOwnership(&categoryMgr, QQmlEngine::CppOwnership);
context->setContextProperty("HgePluginCategories", &categoryMgr);
```

2. Replace menu building in `EffectsProvider.cpp`:
```cpp
// Old: build vendor → plugin menu tree
// New: use PluginDisplayName + PluginCategoryManager for flat, categorized list
```

3. Connect search: `PluginCategoryManager::Search()` returns filtered list

---

## ⚡ Priority 3: Remove Audacity Feel

### What to Change

**A. Info.plist (Already Done ✓)**
- Bundle ID: `com.hgemusicstudio.HgeMusicStudio`
- Bundle Name: `Hge Music Studio`
- Bundle Icon: `AppIcon.icns` (custom HGE icon needed)

**B. Binary Strings (Need Source Rebuild)**
The binary contains many Audacity reference strings that users can see:
- "Audacity version:" — change to "HGE Music Studio version:"
- "Powered by Audacity" or similar branding
- Nyquist error messages
- Old preference dialog titles

**C. Remove Nyquist Clutter from Menus**
The 28 Nyquist scripts currently appear as individual menu items. Options:

**Option 1 — Hide from effect menu (recommended)**:
```cpp
// In NyquistEffectsModule or effects registration:
for (auto &effect : nyquistEffects) {
    if (NyquistCategories::isLegacy(effect)) {
        effect.setVisible(false);  // Keep loaded, hide from menu
    }
}
```

**Option 2 — Group into single "Legacy Nyquist" submenu**:
```cpp
// Create a single entry that opens a submenu
auto legacyGroup = std::make_shared<EffectGroup>("Legacy Effects");
for (auto &effect : nyquistEffects) {
    if (NyquistCategories::isLegacy(effect)) {
        legacyGroup->AddEffect(effect);
    }
}
```

**D. Resources to Replace**
| Resource | Action |
|----------|--------|
| `AppIcon.icns` | Replace with HGE logo |
| Splash screen | Replace with HGE branded version |
| About dialog | Change all text references |
| Default project template | Replace with HGE defaults |
| Error dialogs | Reword from Audacity → HGE |

---

## ⚡ Priority 4: Plugin Consistency System

### The Standard

Every bundled plugin must pass this checklist:

```
□ Display alias (TDR Nova → "TDR EQ")
□ Category assignment (EQ, Dynamics, etc.)
□ Architecture compatibility (arm64 ✅ / x86_64 ⚠️ / Universal ✅)
□ Binary validation (Mach-O header, code signing)
□ Cache registration (file hash, size, mtime)
□ Startup health check (loads without crash)
□ HGE Certified badge (if applicable)
□ Description (polished, non-technical)
□ Version string (clean format)
```

### Current Plugin Registry

| Plugin | Type | Alias | Category | Arch | Status |
|--------|------|-------|----------|------|--------|
| TDR Nova.vst | VST2 | TDR EQ | EQ | Universal | ✅ Validated |
| Saturation Knob.vst | VST2 | Saturation | Mastering | Universal | ✅ Validated |
| TDR Nova.vst3 | VST3 | TDR EQ | EQ | Universal | ⏳ VST3 rebuild |
| SSQ.vst3 | VST3 | SSQ | — | Universal | ⏳ VST3 rebuild |

### Adding New Plugins

```bash
# 1. Copy plugin into bundle
cp -R "/path/to/Plugin.vst" "HgeMusicStudio.app/Contents/plug-ins/VST2/"

# 2. Register alias in plugin-aliases.xml
# 3. Assign category in PluginCategoryManager
# 4. Validate architecture
file "HgeMusicStudio.app/Contents/plug-ins/VST2/Plugin.vst/Contents/MacOS/"*

# 5. Test launch
open HgeMusicStudio.app

# 6. Verify in plugin registry
cat ~/Library/Application\ Support/HgeMusicStudio/pluginregistry.cfg | grep Plugin
```

---

## ⚡ Priority 5: Safe Plugin Loading

### Crash Isolation Architecture

```
┌─────────────────────────────────────────────┐
│           Main Process                      │
│  ┌──────────────────────────────────────┐   │
│  │  PluginManagerModule                  │   │
│  │  ├── ValidateBinary() → arch check   │   │
│  │  ├── CalculateFileHash() → SHA-256   │   │
│  │  ├── ScanDirectory() → discover      │   │
│  │  ├── QuarantinePlugin() → isolate    │   │
│  │  └── SaveCache() → persist           │   │
│  └──────────────────────────────────────┘   │
│                                             │
│  ┌──────────────────────────────────────┐   │
│  │  Plugin Sandbox (future)              │   │
│  │  ├── Process isolation               │   │
│  │  ├── Timeout detection               │   │
│  │  ├── Crash recovery                  │   │
│  │  └── Memory monitoring               │   │
│  └──────────────────────────────────────┘   │
└─────────────────────────────────────────────┘
```

### What's Already Built

1. **Binary validation** — `PluginValidator::ValidateBinary()` checks Mach-O magic, arch, code signing
2. **Quarantine system** — Failed plugins isolated, not scanned again until cleared
3. **Hash-based change detection** — SHA-256 hash stored in cache, rescans only when changed
4. **Cache persistence** — `plugin-cache.xml` in app support dir

### What's Still Needed for Production

1. **Plugin timeout** — Kill plugin if it hangs > 30s during scan
2. **Duplicate detection** — Same plugin in VST2 + VST3 should register once
3. **Graceful degradation** — One bad plugin doesn't block others
4. **Incompatible binary handling** — Warn if plugin is x86_64-only on Apple Silicon
5. **Memory limit monitoring** — Don't let a plugin consume all RAM

---

## Timeline Estimate

| Task | Effort | Priority |
|------|--------|----------|
| VST3 rebuild (fix cmake + rebuild) | 2-4 hours | 🔴 P1 |
| Apply VST3_PATH patch | 15 min | 🔴 P1 |
| Rebuild Wrapper for VST3_PATH | 5 min | 🔴 P1 |
| Add PluginCategoryManager to build | 1-2 hours | 🟡 P2 |
| Create HGE effect browser QML | 4-8 hours | 🟡 P2 |
| Connect browser to backend | 2-4 hours | 🟡 P2 |
| Rebrand Info.plist + resources | 1 hour | 🟡 P2 |
| Hide Nyquist legacy from menus | 30 min | 🟡 P2 |
| Plugin consistency audit | 1-2 hours | 🟢 P3 |
| HGE Certified badge system | 2-3 hours | 🟢 P3 |
| Crash isolation architecture | 4-6 hours | 🟢 P3 |
| DMG install experience polish | 2-3 hours | 🟢 P3 |

---

## Key Files Reference

```
~/HgeMusicStudio/
├── HgeMusicStudio.app/          # Working app bundle (VST2 + LV2)
├── build-dmg.sh                 # DMG packaging script
├── rebuild-vst3.sh              # VST3 rebuild script
├── README.md                    # Architecture documentation
├── PHASE2_EXECUTION.md          # ← This document
├── module-source/               # Saved source code
│   ├── PluginManagerModule.h    # Plugin management singleton
│   ├── PluginManagerModule.cpp  # ~31KB implementation
│   ├── PluginCache.h/.cpp       # XML cache persistence
│   ├── PluginValidator.h/.cpp   # Binary validation
│   ├── PluginManagerPanel.h/.cpp # wxWidgets UI panel
│   ├── PluginDisplayName.h      # Display name aliasing
│   ├── VST3EffectsModule.cpp    # VST3_PATH patch
│   ├── Wrapper.c edits          # Launcher modifications
│   └── CMakeLists.txt edits     # Build integration
└── [future] HGEPluginBrowser.qml  # (to be created)
```
