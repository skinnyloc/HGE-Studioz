# HGE Music Studio — JARVIS Brain (continuity doc, READ FIRST)

## What this project is
Van's own DAW, forked from Audacity 3.7.7 (Muse Studio-style Qt/wx build), rebranded as
"HGE Music Studio." Self-contained bundled plugins (VST2+VST3), a custom in-app "HGE
Effect Browser," and (in progress) an AI Mix Assistant that maps a text prompt to plugin
parameter values via Claude.

## Canonical locations (as of 2026-08-06, later session — CONSOLIDATED, single tree now)
Everything lives under `~/HgeMusicStudio/` now — Van asked for this to stop being
scattered across `~/Downloads/apps/...` too, so it got moved:
- **Home base / git repo / scripts**: `~/HgeMusicStudio/` — `rebuild-full.sh` is the one
  script to run. `module-source/` holds every patch/custom file; it is the single source
  of truth. The live build tree gets fully repopulated from `module-source/` on every run.
- **Audacity source + build tree**: `~/HgeMusicStudio/audacity-3.7.7` (moved here this
  session from `~/Downloads/apps/HGE MUSIC STUDIO/audacity-3.7.7` — if you see the old
  path anywhere, it's stale; `rebuild-full.sh`'s `AUDACITY_SRC` was updated to match).
- **Built app**: `~/HgeMusicStudio/HgeMusicStudio.app` (rebuild-full.sh installs it here;
  copy to `/Applications/HgeMusicStudio.app` — `rm -rf` + `cp -R`, never `mv`/symlink —
  to actually update the installed/launchable copy).
- **Client/sellable package**: `~/HgeMusicStudio/client-package/` (moved here this
  session too — was `~/Downloads/apps/HGE Music Studio - Client Package/`).
- **MCP server** (new this session): `~/HgeMusicStudio/mcp-server/hge_daw_mcp.py` —
  see "MCP server" section below.
- **DO NOT use `~/Documents/...` for anything in this project.** Van's iCloud Documents
  sync has been flaky/deadlocked before (see memory `project_icloud_storage_full.md`) —
  a plain `mv` out of Documents timed out mid-session on 2026-08-06. Everything must live
  under `~/HgeMusicStudio/`.

## Build requirements discovered this session
- Needs **Conan 2.x**, not 1.x (the old README/scripts said 1.x — wrong for this actual
  Audacity-3.7.7 tag). Conan 2 lives in an isolated venv: `~/.venvs/conan2`, symlinked as
  `~/.local/bin/conan`. Don't `pip install --break-system-packages` into the homebrew
  Python — use a venv.
- `cmake --build` must NOT pass `--target Audacity` — that only builds the main app and
  silently skips every module (mod-mp3, mod-script-pipe, mod-plugin-manager, etc). Build
  with no `--target` (default `all`).
- The actual CMake project/executable target is named `Audacity`, not `HgeMusicStudio`.
  Renaming that through the whole build system is not worth it — `rebuild-full.sh` builds
  `Audacity.app`, then renames the outer bundle folder to `HgeMusicStudio.app` and patches
  `Info.plist` (CFBundleName/DisplayName/Identifier). The `Wrapper` launcher is told via
  `-DAUDACITY_BUNDLE_EXECUTABLE=\"Audacity\"` to exec the real (unrenamed) binary.
- `audacity_module(...)` in this Audacity version is a **positional** macro:
  `audacity_module( <name> "<SOURCES>" "<LIBRARIES>" "" "" )` — NOT the keyword-style
  `NAME/SOURCES/IMPORT_TARGETS/...` syntax that was in the old module-source files.
- CMake's `source_group` requires every module SOURCES file to live **inside** the
  module's own directory — no `../../../libraries/...` relative paths. That's why
  `PluginCategoryManager.{h,cpp}` and `PluginDisplayName.{h,cpp}` are local copies inside
  `modules/plugin-manager/mod-plugin-manager/`, not shared library files.
- A module needing `src/*.h` headers (e.g. `CommonCommandFlags.h` for `AlwaysEnabledFlag`)
  must link the `Audacity` target itself as a library (see how stock `mod-cloud-audiocom`
  does it) — that's where the `src` include path comes from.
- This wxWidgets build has **no `wxSearchCtrl`** (`wx/searchctrl.h` doesn't exist) — the
  effect browser's search box uses a plain `wxTextCtrl` + `SetHint()` instead.
- `PluginDescriptor` has no `GetName()` — use `GetSymbol().Translation()` (display name)
  or `.Internal()` (untranslated). `CommandID`/`PluginID` are `TaggedIdentifier` wrappers —
  need `.GET()` to get the raw `wxString`.
- Opening/applying an effect programmatically: use `EffectUI::DoEffect(commandId, project,
  flags)` from `src/DoEffect.h` — this is what the stock Effects menu itself calls. Don't
  hand-roll `EffectManager::GetEffect()->ShowInterface()` (`ShowInterface` doesn't exist
  on `EffectPlugin` in this version).

## Current state (end of 2026-08-06, later session)
- Full rebuild succeeds cleanly: VST2+VST3+LV2+Nyquist, all bundled plugins present
  (TDR Nova, Saturation Knob, Graillon, GSnap, ValhallaFreqEcho, SSQ), the custom HGE
  Effect Browser + AI Chat module (`mod-plugin-manager.so`) compiles, links, AND —
  as of this session — actually **loads and stays loaded** (see the two-bug fix below;
  earlier sessions' "it compiles and loads" claims were only half true). App launches
  without crashing.
- `mod-script-pipe` now loads too (was hit by the same bug as mod-plugin-manager, not a
  separate dead end as previously written here) and its pipe files do appear and do
  respond to at least some commands — but see "MCP server" below, it's flaky, not fully
  trustworthy yet.

## Two root-caused module-loading bugs (fixed this session — READ IF THE AI CHAT MENU
## ITEM OR ANY CUSTOM MODULE EVER "DISAPPEARS" AGAIN AFTER A REBUILD)
1. `ModuleSettings.cpp`'s `autoEnabledModules()` is Audacity's own hardcoded allowlist
   of module names it trusts without asking. Our custom modules weren't on it, so every
   rebuild (which changes the .so's mtime) reset their status to "ask the user" —
   and since nothing was actually driving that ask-dialog headlessly, they just never
   got enabled. Fix: added `"mod-plugin-manager"` and `"mod-script-pipe"` to that list.
2. `mod-plugin-manager` never implemented the required module ABI exports
   (`GetVersionString`/`ModuleDispatch` via `DEFINE_VERSION_CHECK` + a real
   `ModuleDispatch()` — see `modules/etc/mod-null/ModNullCallback.cpp` for the minimal
   canonical pattern). Without it, `HasDispatch()` returns false and Audacity explicitly
   unloads the module right after loading it. This was missing since the module was
   first created, not something broken this session. Fix: added the block to the bottom
   of `HgeEffectModule.cpp`, wired to the already-written (but never-actually-called!)
   `OnStartup()`/`OnShutdown()`.

Both are `copy_overlay`'d from `module-source/` in `rebuild-full.sh` now — if a future
rebuild seems to "lose" the fix, check those two lines are still in the script and the
corresponding module-source files still have the fix.

## AI Mix Assistant — TWO ways to use it now
1. **File-based** (original, still works): `ai_mix_assistant/ai_mix.py` /
   `AI Mix Assistant.command` double-click launcher. Verified end-to-end on a real file
   (see SAVE_PROGRESS.md for the numbers).
2. **In-app chat panel** (built this session, NOW ACTUALLY WORKING after the module
   fixes above): `Effect → HGE AI Mix Assistant...` / `Ctrl+Shift+A` inside the app
   itself (`HgeAiChat.h/.cpp`). Pick a track from a dropdown, type a prompt, hit Apply
   — exports that track, runs `ai_mix.py` on it, imports the result as a new track.
   Now genuinely multi-turn: if the prompt is too vague, the AI asks ONE clarifying
   question (via `ai_mix.py`'s new `"type":"question"` JSON shape) instead of guessing,
   the dialog shows it and waits for a reply instead of re-exporting from scratch.

Both share the same `ai_mix.py` engine underneath — same two auth modes as before
(personal = Claude Code subscription via local CLI, sold DMG = BYOK Anthropic API key),
same `SYSTEM_PROMPT`, now grounded in distilled PlatinumForge mixing/mastering
knowledge (LUFS-by-destination, vocal chain order, preset deltas — condensed to stay
token-cheap for BYOK clients, not pasted verbatim from the skill files).

**Optional prompt + EQ Off + Limiter toggle (2026-08-07 ~1am) — DONE + VERIFIED, see
SAVE_PROGRESS.md top.** Prompt box is now OPTIONAL — blank + Apply masters from the
settings panel only and `ai_mix.py` skips the AI call entirely (deterministic/free).
Added EQ "Off (no EQ)" and a Limiter On/Off dropdown (default On). ai_mix.py: `--prompt`
optional, `--limiter on|off`, EQ_STYLE_PRESETS["off"]. NOTE: app runs ai_mix.py from the
DEV PATH (`~/HgeMusicStudio/ai_mix_assistant/ai_mix.py`), not a bundled copy — .py edits
are live for Van's build instantly; bundling it is a sellable-build task.

**Settings button + Rename/Remove skills + MANUAL TWEAKS grouping (2026-08-06
~11:46pm) — DONE and VERIFIED, see SAVE_PROGRESS.md top section.** This was the batch
that timed out mid-edit; finished it. In-app `HgeAiChat` now has: a "Settings..."
button opening `HgeAiSettings` (buyer pastes their own Anthropic API key + thinking
effort → `ai_mix_config.json`; `HgeAiChat` runs `ai_mix.py` with NO `--mode`, which
auto-picks api-vs-personal from whether a key is saved); the settings split into
"Mastering Settings" (LUFS+TruePeak) vs "MANUAL TWEAKS — set these yourself, or let
the AI above fill them in" (EQ/Compression/Saturation); and Rename + Remove buttons on
saved skills (rename/delete the `.json`). New files `HgeAiSettings.h/.cpp` — synced to
`module-source/` and registered in `rebuild-full.sh` (two `add_file` lines). Verified
via nm symbols + UTF-32 string scan + live `lsof` module-load + clean launch; NOT
click-tested on screen.

**Mastering Settings panel, touch-up chaining, and Skills (later same-day session,
~10:25pm) — DONE and VERIFIED, see SAVE_PROGRESS.md for the full writeup.** Short
version: the dialog now has Target LUFS / True Peak / EQ Style / Compression /
Saturation dropdowns (each "Auto" by default, any pick overrides just that one field
of whatever Claude returns); a real `alimiter` limiter stage was added so True Peak is
now hit exactly (verified -1.0 dBTP measured, was silently -1.5 before); after a
successful mix the next prompt touches up THAT result instead of re-processing the raw
track; and a "Save as Skill..." button persists a named recipe
(`~/Library/Application Support/HgeMusicStudio/mix_skills/*.json`) that a "Saved
Skill:" dropdown can recall later to anchor the AI's style on a different track. Also
fixed a "Plug-in group ... was merged" popup that showed on every startup (two
`AttachedItem`s both declaring `Section("HGE", ...)` — combined into one).

## MCP server (new this session) — Claude can control the app directly, PARTIALLY
`~/HgeMusicStudio/mcp-server/hge_daw_mcp.py` — wraps Audacity's own `mod-script-pipe`
protocol (the same one its official Python scripting library uses) as MCP tools:
`get_tracks`, `get_selection`, `get_clips`, `get_labels`, `import_audio`,
`export_audio`, `save_project`, `select_time`, `apply_effect`, `undo`, `redo`, and a
raw `run_command` escape hatch. Registered with Claude Code already
(`claude mcp add hge-daw -- python3 ~/HgeMusicStudio/mcp-server/hge_daw_mcp.py`).

**Status is genuinely mixed, not a clean win** — see SAVE_PROGRESS.md's MCP section
for the full detail, but the short version: `get_tracks`/`get_selection` proved
solidly working (multiple real round-trips with real project data). `SelectAll:` and
`GetInfo: Type=Commands` HANG THE ENTIRE APP — confirmed, had to force-kill and
relaunch — removed from the bundled tools, documented as forbidden in `run_command`'s
docstring. Then, on a totally fresh relaunch with no prior bad commands sent, even
`GetInfo: Type=Tracks` (previously solid) timed out twice in a row for reasons not
yet understood, while the app process itself stayed healthy (not crashed, low CPU).
**Don't treat this as done** — it's a real, partly-proven mechanism that needs another
pass of careful, patient testing (not mid-heavy-rebuild-churn) before trusting it.

See `SAVE_PROGRESS.md` for exact next steps / where this was paused.

## ⭐ SELLABLE PRODUCT ROADMAP — see SAVE_PROGRESS.md top ("SELLABLE PRODUCT ROADMAP")
Van's plan to sell/distribute HGE Music Studio through hgediscord.com. Phased, BUILD
IN ORDER, gated on Van signing off his personal build first (Phase 0). Phases:
0) prove personal build (add plugins + click-test AI) → 1) multi-provider AI keys
(Claude+DeepSeek+OpenAI, incl. Van's own DeepSeek backup) → 2) in-app "Check for
Updates" + "About HGE Studio" button → hgediscord.com → 3) gated landing/download page
on the site (members download free + updates; recommend paid-members-only MVP since the
site only has subscription PayPal today) → 4) promo/ads. Three decisions still owed by
Van: access model, which providers, personal default provider order. Site repo =
`~/Downloads/HGE Community Hub/` (its own JARVIS_BRAIN.md/MEMORY.md — read first; auth
can't be tested on localhost, CORS prod-only; PayPal only per [[feedback_no_stripe]]).
**In progress (2026-08-06 night):** Phase-3 landing page being built in the Hub repo in
parallel while Van sources plugins + tests — LOCAL only, not deployed.

## Known gotchas
- Van is on a very slow/flaky pattern where `~/Documents` operations can hang — never read,
  write, or `mv` into Documents for this project.
- Don't run bare `pip install` against the homebrew-managed Python — it's externally
  managed (PEP 668) and will refuse; use a venv.
- `rebuild-full.sh` fully re-syncs everything from `module-source/` every run — if you fix
  a bug directly in the live build tree, you MUST mirror the same fix into the matching
  `module-source/...` file or it gets silently overwritten next build.
