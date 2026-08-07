# SAVE PROGRESS — HGE Music Studio

Last updated: 2026-08-06, ~11:46pm — Settings button + Rename/Remove skills + MANUAL TWEAKS grouping

## DONE + VERIFIED (2026-08-06 ~11:46pm) — "add the settings to the chat bot"
This is the batch that timed out mid-flight AFTER the previous save below (the older
"~10:25pm Skills" section). Picked it back up and finished it. All in the in-app
`HgeAiChat` dialog (`Effect → HGE AI Mix Assistant...`):

1. **Settings... button + `HgeAiSettings` dialog** — where a sold/BYOK buyer pastes
   their own Anthropic API key + picks a thinking effort (Fast/Balanced/Thinking) +
   optional model override. Writes `~/Library/Application Support/HgeMusicStudio/
   ai_mix_config.json` (`{anthropic_api_key, model, think}`). `HgeAiChat` now runs
   `ai_mix.py` with NO `--mode` flag; `ai_mix.py:361` auto-picks `api` if a saved key
   exists else `personal` (Van's own build leaves the key blank → stays on his Claude
   Code subscription). Files: `HgeAiSettings.h/.cpp` (NEW).
2. **Settings block split into two labeled groups** exactly as Van asked:
   - "Mastering Settings" → Target LUFS (incl. -14 Streaming) + True Peak (incl. -1.0)
   - "MANUAL TWEAKS — set these yourself, or let the AI above fill them in" → EQ Style
     (Natural/Bright/Warm), Compression (Off/Light/Medium/Heavy), Saturation
     (None/Subtle/Medium). Each still defaults to "Auto" so the AI fills it unless set.
3. **Rename + Remove saved-skill buttons** (were missing — only "Save as Skill..."
   existed). Rename = `wxGetTextFromUser` → sanitize → `wxRenameFile` the `.json`,
   refresh dropdown, reselect. Remove = `AudacityMessageBox` yes/no confirm →
   `wxRemoveFile`, refresh, back to "(none)". Both guard on a real skill being selected
   (index > 0, "(none)" is index 0). New helper `SelectedSkillName()`.

The full flow Van wanted is now all present: pick LUFS/TP + manual tweaks (or leave
Auto) → one prompt does the master/pre-master → re-prompt to touch up the result
(existing chaining) → Save as Skill → later re-select / Rename / Remove that skill.

**Verified (real, not just compiled):** `rebuild-full.sh` exit 0, zero `error:` lines.
`nm mod-plugin-manager.so` shows 19 `HgeAiSettings` symbols + the 3 new handler symbols.
UTF-32-LE scan of the built `.so` confirms all 7 new strings present ("MANUAL TWEAKS",
"Rename Skill", "Remove Skill", "AI Mix Assistant Settings", "Anthropic API Key",
"Mastering Settings", "Save as Skill"). `/Applications/HgeMusicStudio.app` refreshed
(rm -rf + cp -R; the installed copy had been stale). Launched clean: process up,
`lsof` confirms `mod-plugin-manager.so` mapped into the live PID, no crash report.
**Not click-tested on screen** (computer-use still can't see this bundle) — Van should
click Settings, Rename, and Remove once to confirm the GUI feel.

**module-source sync DONE** (critical — rebuild wipes the live tree from here every
run): `HgeAiChat.cpp/.h`, `HgeAiSettings.cpp/.h`, and the module `CMakeLists.txt` all
copied into `module-source/`, and `rebuild-full.sh` got two new `add_file` lines for
`HgeAiSettings.h`/`.cpp`. If a future rebuild loses the Settings button, check those.

## Desktop / findability cleanup (same session)
- There were TWO Desktop symlinks both → `/Applications/HgeMusicStudio.app` ("HGE Music
  Studio" and "HGE Music Studio.app"). Removed the redundant non-.app one; kept
  "HGE Music Studio.app".
- Couldn't find it in Launchpad/Spotlight because (a) the real bundle is
  `HgeMusicStudio.app` (no spaces, so it lists as "HgeMusicStudio") and (b) the
  ad-hoc-signed, repeatedly-recopied bundle was never indexed. Ran `lsregister -f
  /Applications/HgeMusicStudio.app` to register it. Search "HgeMusicStudio" (one word).

---
## (earlier save below — kept for history)

Last updated: 2026-08-06, later same-day session (Van present, actively testing)

## THIS SESSION — in-app AI chat panel: built, broke, root-caused, fixed, verified
Van wanted the AI Mix Assistant as a real in-app dialog (`HgeAiChat`), not just the
file-based `ai_mix.py` CLI/`.command` launcher from the earlier session. Built it,
wired it into the Effect menu (`Effect → HGE AI Mix Assistant...`, `Ctrl+Shift+A`),
and it silently DIDN'T show up after the first few rebuilds. Two real, separate bugs,
both now fixed and verified (Van confirmed via his own screenshot that the menu item
is there and clickable):

1. **Module security gate stuck on "ask every time".** Audacity's `ModuleManager`
   tracks each `.so`'s enable status in `audacity.cfg` (`[Module]` section) keyed by
   filename, and resets it to `kModuleNew` (4) whenever the file's mtime doesn't match
   the recorded `[ModuleDateTime]` — which happens on every rebuild. `kModuleNew`
   means "ask the user once"; our custom modules (`mod-plugin-manager`,
   `mod-script-pipe`) aren't in Audacity's own hardcoded
   `autoEnabledModules()` allowlist (`ModuleSettings.cpp`) the way stock modules
   (`mod-flac`, `mod-ffmpeg`, etc.) are, so they kept getting re-asked and never
   stayed enabled across dev rebuilds. **Fix**: added `"mod-plugin-manager"` and
   `"mod-script-pipe"` to that allowlist — see the comment at that call site.
2. **Missing module ABI entry point (the real root cause, deeper than #1).**
   `mod-plugin-manager` never actually implemented `DEFINE_VERSION_CHECK` /
   `ModuleDispatch` (`extern "C"` exports every real Audacity module needs — see
   `modules/etc/mod-null/ModNullCallback.cpp` for the canonical minimal example).
   Without it, `ModuleManager::TryLoadModules` calls `HasDispatch()`, gets false, and
   explicitly `Unload()`s the module right after loading it — status ends at
   `kModuleFailed` (3), permanently skipped on every future launch without a code fix
   (the status alone can't be sed-edited around; `kModuleFailed` short-circuits before
   even attempting to load). This gap existed from whenever `mod-plugin-manager` was
   first created, not something broken this session — the menu item could still
   sometimes *appear* because the `static AttachedItem` global constructor runs at
   dlopen time regardless of the later dispatch-check rejection, so it partially
   "worked" in a fragile, unsupported way in earlier sessions. **Fix**: added the
   actual `DEFINE_VERSION_CHECK` + `ModuleDispatch()` block to the bottom of
   `HgeEffectModule.cpp`, wired to call the already-existing (but never actually
   invoked!) `HgeEffectModule::OnStartup()`/`OnShutdown()`.

Verification used for both, since GUI screenshot access wasn't available for most of
this (see "computer-use access" below): `lsof -p <pid> | grep modules` to prove the
.so is genuinely mapped into the *running* process, plus re-reading `audacity.cfg`'s
`[Module]` values to confirm status holds at `1` (not resetting), plus a crash-report
directory check after every relaunch. Van's own screenshot at the end confirmed the
menu item visually (Effect menu, right under HGE Effect Browser).

## THIS SESSION — AI chat panel is now a real (light) multi-turn conversation
Per Van's request ("skills baked in for every convo"), extended both the prompt
engineering and the dialog's behavior, using two of Van's own confirmed-legitimate
skill packages (checked their actual file contents first — clean, no override/
persona/jailbreak content in either):
- **PlatinumForge** (`~/Downloads/Skill Folder copy/PlatinumForge/skills/
  mix-master-engineer/references/*.md`) — distilled its LUFS-by-destination table,
  vocal chain order, and preset deltas into `ai_mix.py`'s `SYSTEM_PROMPT` (condensed,
  not pasted verbatim — this system prompt is billed per-token for BYOK/sold-DMG
  clients, so kept it tight).
- **brainstorming** (`~/Downloads/Skill Folder copy/brainstorming/SKILL.md`) — its
  actual mechanic (ask ONE question when it would change the outcome, default
  otherwise) is now real behavior, not just a comment: the JSON schema gained an
  optional `"type": "question"` shape; `ai_mix.py main()` detects it and prints
  `CLARIFY:<question>` instead of applying a plan; `HgeAiChat` detects that prefix,
  logs the question, keeps the already-exported track cached (`mPendingInputWav`) and
  a running `mHistory` string instead of re-exporting, clears the prompt box for the
  reply, and only actually applies/imports once a real plan comes back.

## THIS SESSION — MCP server for Claude to control the app: built, PARTIALLY working
Van asked for an MCP server so his own Claude (Code/Desktop) can control HGE Music
Studio directly. Built on Audacity's own `mod-script-pipe` (official scripting
bridge — same one Audacity's Python scripting library uses), which turned out to
ALSO have been silently broken by bug #1 above (it's in the same allowlist fix).

**Lives at**: `~/HgeMusicStudio/mcp-server/hge_daw_mcp.py`. Registered with Claude
Code: `claude mcp add hge-daw -- python3 ~/HgeMusicStudio/mcp-server/hge_daw_mcp.py`
(already run, added to this project's local config).

**Confirmed genuinely working**, real round-trips with real project data:
`GetInfo: Type=Tracks` and `GetInfo: Type=Selection` — multiple successful calls
early in testing, including reading Van's actual open project's real track.

**Confirmed BROKEN — hang the entire app, not just the command** (had to force-kill
`Audacity`/`Wrapper` and relaunch after hitting this): `SelectAll:` and
`GetInfo: Type=Commands` (any Format). Removed both from the MCP server's bundled
tool set entirely rather than ship something that freezes the app; `run_command`'s
docstring explicitly warns not to send them via the raw escape hatch either.

**Open problem, NOT resolved**: after a completely fresh reinstall+relaunch (no prior
bad commands sent to that instance at all), even the previously-proven-good
`GetInfo: Type=Tracks` query timed out twice in a row (30s each) against the fresh
process — while the process itself stayed healthy (low CPU, normal sleep state, no
crash). So the pipe bridge's reliability is inconsistent/flaky in a way not yet
root-caused — sometimes solid (multiple clean round-trips), sometimes silently
unresponsive on a fresh launch for reasons unclear (timing after startup? something
about repeated kill/relaunch cycles during heavy dev iteration? unknown). **Do not
trust this as production-ready** — it's a real, partially-proven mechanism, not a
finished feature. Next session should test with NO prior kill/relaunch churn (a
single clean launch, wait ~15-20s before the first command, see if that alone fixes
the flakiness) before building anything further on top of it.

## THIS SESSION — folder consolidation (Van asked to put it all in one place)
- `~/Downloads/apps/HGE Music Studio - Client Package/` → moved into
  `~/HgeMusicStudio/client-package/` (just 2 small docs, safe/instant move).
- `~/Downloads/apps/HGE MUSIC STUDIO/audacity-3.7.7` (1.1GB source+build tree) →
  moved into `~/HgeMusicStudio/audacity-3.7.7/` (same volume, so it was a fast
  rename). **`rebuild-full.sh`'s `AUDACITY_SRC` updated to match** — don't let this
  regress to the old path. Moving it broke the CMake cache as expected (`CMakeCache.txt`
  had the old absolute path baked in — classic "moved a configured CMake tree" error);
  fixed by deleting `audacity-3.7.7/build-vst3` and letting `rebuild-full.sh` do one
  full clean reconfigure+rebuild from the new location, which succeeded. **Everything
  now genuinely lives under `~/HgeMusicStudio/`** — home base, source tree, build
  scripts, module-source, JARVIS/SAVE_PROGRESS, client package, MCP server, all one
  tree. Nothing HGE-Music-Studio-related left in `~/Downloads/apps/` anymore.

## computer-use / screen access — didn't work all session, root cause unclear
Tried repeatedly (many `request_access` calls, different name variants: "HGE Music
Studio", "HgeMusicStudio", "Audacity", bundle ID) — always `"reason":"not_installed"`.
`mdls`/Spotlight metadata came back null for the app bundle even after `mdimport`,
suggesting Spotlight/LaunchServices genuinely hadn't indexed this fresh
adhoc-signed, repeatedly-replaced (`rm -rf` + `cp -R` every rebuild) bundle. Never
resolved — ended up doing all verification via `lsof`/config files/crash-report
checks instead, which is real evidence but not as good as an actual screenshot. Van's
own screenshot (pasted into chat) was the only actual visual confirmation all
session. If this matters next time: maybe try after leaving the freshly-copied bundle
untouched for a few minutes (undisturbed) before requesting access, or investigate
whether adhoc code signing is what's blocking LaunchServices resolution.

## Done and VERIFIED (actually run, not just compiled)
1. Rebuilt HGE Music Studio from a fresh Audacity-3.7.7 clone (old source tree was
   incomplete/corrupted). Full build succeeds: VST2+VST3+LV2+Nyquist, all bundled plugins
   preserved, custom HGE Effect Browser module compiles and loads. App launches clean.
2. Moved everything off `~/Documents` (iCloud risk) onto `~/Downloads/apps/`.
3. **AI Mix Assistant core engine** — `ai_mix_assistant/ai_mix.py`. Ran end-to-end on a
   real MP3: analyzed it (ffmpeg loudnorm/astats), asked Claude for a mix plan via the
   local `claude -p --output-format json` CLI (personal/subscription auth, no API key),
   applied the plan with ffmpeg (parametric EQ bands, compressor, exciter/warmth,
   loudnorm), and confirmed the output actually changed as intended: input was
   -11.5 LUFS / +0.62 dBTP (over 0dBFS, clipping risk) → output -14.5 LUFS / -1.46 dBTP
   (safe), full duration preserved, not a corrupt/truncated file.
4. `ai_mix.py --mode api` (BYOK path for the sold DMG) — code path validated: fails with
   a clear error if no key is given, same plan/apply logic as personal mode otherwise.
   NOT tested against the real Anthropic API (no key available in this session).
5. `AI Mix Assistant.command` — double-click launcher (Finder file-picker + prompt dialog
   via osascript, then runs ai_mix.py, personal mode). Shell syntax verified
   (`bash -n`), but the osascript dialogs themselves were NOT interactively click-tested
   (can't drive a GUI dialog from this session) — Van should double-click it once to
   confirm the file-picker/prompt flow feels right.

## Why the AI Mix Assistant is file-based, not live-in-app (superseded, see below)
[SUPERSEDED — kept for history.] Original plan was to drive it through
`mod-script-pipe` this same day; it hit a wall and got pivoted to file-based. The
*real* cause of that wall was found and fixed in the later session (same day): see
"MCP server for Claude to control the app" above — `mod-script-pipe` was silently
stuck at `kModuleNew` for the exact same reason `mod-plugin-manager` was (not in
Audacity's own `autoEnabledModules()` allowlist), not a `[ModulePath]` issue. It's
fixed now, the pipe files DO appear, and it DOES respond — but flakily (see the MCP
section above for exactly what's proven vs. still broken). The AI Mix Assistant
itself stays file-based regardless — that part was never the bottleneck, and the
in-app `HgeAiChat` dialog (built later the same day) already covers the "control it
from inside the app" use case Van actually wanted, independent of script-pipe.

## Sellable version — side folder
Client-facing package now lives inside the main project folder:
`~/HgeMusicStudio/client-package/` (moved off `~/Downloads/apps/...` this session —
see "folder consolidation" above).
- `README.md` — polished, customer-facing, ready to ship in the DMG as-is.
- `INTERNAL-NOTES.md` — Van-only, tracks what's still missing before it can actually sell.

Keep iterating (fixes, new plugins) in `~/HgeMusicStudio/` as normal — that's the dev
copy. When ready to cut a sellable build, rebuild from here and drop the DMG into the
Client Package folder next to the README.

## Not done — retail DMG packaging (task 6, partial)
The `--mode api` code exists but a real sellable DMG needs more than that:
- Buyer machines won't have `ffmpeg` or a full `python3` installed the way this dev
  machine does — need to either bundle a portable ffmpeg binary + freeze `ai_mix.py` with
  PyInstaller into a standalone executable, or document it as a "requires Homebrew"
  install step (worse UX, cheaper to ship).
- No Settings UI yet for pasting an Anthropic API key — right now it's a manual JSON file
  at `~/Library/Application Support/HgeMusicStudio/ai_mix_config.json`
  (`{"anthropic_api_key": "sk-..."}`). Fine for now, needs a real UI before selling.
- Haven't run `build-dmg.sh` with any of this bundled in yet.

## Branding — full sweep done and VERIFIED (2026-08-06, later session)
Custom logo (black/gold "HGE Studios" graffiti art) is now the app icon, startup splash,
and About dialog logo. Every reachable "Audacity" string found by two independent audits
(one automated agent sweep + manual follow-up passes) has been fixed across 45+ source
files — window titles, all Help/About/Preferences/error dialog text, the File>Quit menu
item, effect vendor/description strings, Timer Record, Mixer, Log window, crash reporter,
etc. Verified two ways:
1. Most of these strings are stored as **UTF-32 wide-char literals**, not plain ASCII —
   the standard `strings` command cannot see them at all (this caused a lot of false
   "still broken" alarms mid-session — don't trust plain `strings` output for this app,
   scan for both plain and UTF-32-LE/BE encoded byte sequences, e.g. via Python
   `phrase.encode('utf-32-le') in binary_bytes`).
2. Ran a UTF-32-aware sweep across every `.dylib`/`.so`/the main binary in the built app
   bundle confirming 12/12 sampled old strings gone and 12/12 new ones present.

**Critical fix**: the stock "check for updates" feature was ON by default and would
download + prompt-install the **real** Audacity over this app (`Install Audacity 4` promo,
`updates.audacityteam.org` calls every 12h). Disabled via
`-Daudacity_has_updates_check=OFF` in `rebuild-full.sh`'s CMAKE_FLAGS — don't remove that
flag. Also neutered `HelpSystem::ShowHelp()` (used by ~20 dialogs' Help buttons) so it no
longer opens manual.audacityteam.org / support.audacityteam.org.

**Black & gold theme**: track backgrounds, waveform color, selection highlight, envelope,
cursor line, and panel text recolored via `libraries/lib-theme/AllThemeResources.h`'s flat
`DEFINE_COLOUR` table (these are procedurally-drawn, safe to hand-edit). Button/toolbar
*icon bitmaps* are baked into a binary image cache (`LightThemeAsCeeCode.h` etc.) that
requires Audacity's own in-app "Output Sourcery" theme-export tool to regenerate — not
done, would need someone to click through Preferences → Theme in the actual GUI.

**Also fixed this session**: the rebuild script was silently corrupting the app's bundled
libraries on every 2nd+ incremental build (`mv`-ing `Audacity.app` → `HgeMusicStudio.app`
confused CMake's incremental tracking, so Contents/Frameworks stopped getting refreshed).
Fixed by changing that step to `cp` instead of `mv` — see the comment in `rebuild-full.sh`
Step 5b if this ever regresses (symptom: app crashes on launch with a dyld "Library not
loaded" error for some `libwx_*.dylib`).

App is installed at `/Applications/HgeMusicStudio.app` with a Desktop shortcut symlink.

## Reminders
- Every fix to the DAW itself must be made in `module-source/` (or `rebuild-full.sh`), not
  just the live build tree, or it's lost on the next rebuild.
- Don't touch `~/Documents` for this project, ever — Van's iCloud sync has hung on it
  before and did again this session (a `mv` timed out).
- Personal build = Claude Code subscription auth (local CLI, `--mode personal`). Sold DMG
  = user's own Anthropic API key (`--mode api`). Two different code paths, don't conflate.
- **NEVER send `SelectAll:` or `GetInfo: Type=Commands` over the script-pipe** — both
  confirmed to freeze the entire app (had to force-kill and relaunch). If testing the
  MCP server / script-pipe again, stick to `GetInfo: Type=Tracks`/`Type=Selection` and
  test cautiously (short timeout wrapper) before trusting any new command against a
  live instance Van might be actively using.
- After a rebuild, `audacity.cfg`'s `[Module]` status for `mod-plugin-manager` and
  `mod-script-pipe` should read `1`. If either shows `3` or `4` again, re-check
  `ModuleSettings.cpp`'s `autoEnabledModules()` list survived the rebuild (it's a
  `copy_overlay` in `rebuild-full.sh` — make sure that line didn't get lost) and that
  `HgeEffectModule.cpp` still has its `ModuleDispatch`/`DEFINE_VERSION_CHECK` block at
  the bottom (also easy to lose if that file gets regenerated from an older
  module-source copy).

## DONE (2026-08-06, ~10:15pm) — Mastering Settings panel + touch-up chaining + Skills
Van tested the AI chat panel on a real song ("MASTER THE TRACK -15 LUFS -1DB NO COMP
LIGHT LIMITER"). Diagnosed the real output (`ffmpeg loudnorm` analysis on the actual
temp files): landed at -14.67 LUFS / -1.50 dBTP. Close on LUFS (SYSTEM_PROMPT doesn't
tell Claude to lock onto an exact numeric LUFS if the user states one literally — it
was treated as a loose hint, not a hard number). True peak came out at -1.50 dBTP
instead of the requested -1.0 because `build_filter_chain()` hardcodes `TP=-1.5` —
there was no path for ANY requested true-peak value, from prompt or otherwise, to
actually reach that filter. Real gap, now being fixed properly with explicit manual
controls instead of relying on Claude to parse exact numbers out of free text:
- Target LUFS dropdown: Auto (AI decides) + -9/-11/-13/-14/-15/-16 hard picks
- True Peak dropdown: explicit dBTP ceiling, always user-controlled (not AI-guessed)
- EQ Style: Auto/Natural/Bright/Warm
- Compression: Auto/Off/Light/Medium/Heavy
- Saturation: Auto/None/Subtle/Medium
- A real dedicated limiter stage (ffmpeg `alimiter`) added after loudnorm, not just
  relying on loudnorm's own single-pass TP behavior (which is what let -1.5 slip
  through instead of hitting the actual requested ceiling precisely).
Manual picks override whatever Claude's plan says for that field; "Auto" leaves it to
the AI. Modeled on Van's other app's settings panel ("HGE Mixing & Mastering.app").

**Verified for real** (not just "should work"): ran the actual new `build_filter_chain`
against a real file with -15 LUFS / -1.0 dBTP forced. `ffmpeg loudnorm` measurement on
the output: **-1.00 dBTP exactly** (was -1.50 before this fix — confirms the `alimiter`
stage is doing its job), **-14.68 LUFS** (within the ±0.5 dB professional tolerance of
-15). Also unit-tested `apply_overrides()` directly: manual picks correctly replace the
AI's own eq/compression/warmth/target_lufs; leaving everything on Auto correctly leaves
the AI's own plan untouched except true_peak_dbtp (always explicit, defaults to -1.0).
Caught and fixed a real bug before shipping: `ffmpeg`'s `alimiter` filter's `level`
param is a boolean, not the string `"disabled"` I first wrote — checked
`ffmpeg -h filter=alimiter` against the real filter spec before trusting it.

## DONE (2026-08-06, ~10:20pm) — three more upgrades, same request batch
Van also asked for (a) the dialog showing a "Plug-in group ... was merged with a
previously defined group" popup every startup — fixable, so fixed it; (b) touch-up
chaining: after a mix completes, the next prompt should refine THAT result instead of
starting over from the raw track; (c) "Skills" — save a plan you like under a name,
recall it later so the AI matches that same recipe on a new track.

**(a) Menu-merge popup — root cause and fix.** Traced to
`libraries/lib-registries/Registry.cpp:194`. Both HGE menu items
(`HgeEffectBrowser`/`HgeAiChat`) were each registered via their own separate
`AttachedItem` with its own `Section("HGE", ...)` — functionally fine (Audacity's
registry merges same-named sections), but the merge itself triggers a user-visible
notice every launch. Fixed by combining both into ONE `AttachedItem` with a single
`Section("HGE", Command(...), Command(...))` in `HgeEffectModule.cpp` — no merge
happens because there's only ever one "HGE" section now. Cleaner pattern regardless.

**(b) Touch-up chaining.** `HgeAiChat`'s state model reworked: `mPendingInputWav` now
means "what the next Apply operates on" and gets set to the MIXED OUTPUT (not cleared)
after a successful run — so typing another prompt refines that result instead of
re-exporting the raw track. Only resets to fresh export if the track dropdown selection
changed since the last run (tracked via new `mLastTrackName`). Distinguished from the
existing clarify-answer chaining (which reuses the ORIGINAL unmixed export, not the
mixed result) via the existing `mAwaitingClarify` flag — three cases total in
`OnApply()` now: mid-clarify, touch-up, fresh.

**(c) Skills.** `ai_mix.py` now prints a machine-readable `PLAN_JSON:<json>` line
alongside every successful plan; `HgeAiChat` captures it (`mLastPlanJson`). New "Save
as Skill..." button prompts for a name, writes the tagged plan JSON to
`~/Library/Application Support/HgeMusicStudio/mix_skills/<name>.json` (sanitized
filename). New "Saved Skill:" dropdown lists everything in that folder; picking one and
hitting Apply passes `--skill-file <path>` to `ai_mix.py`, which folds the saved plan
into Claude's context as a style anchor ("match this unless today's prompt asks for
something different") — chosen over directly forcing the saved plan's exact values so
full fidelity is preserved (arbitrary EQ/comp numbers, not lossily bucketed into the
Mastering Settings panel's ~4-5 discrete presets) while still allowing prompt-driven
deviation on top of a loaded skill.

**Build status: DONE and VERIFIED.** 0 compile errors. Reinstalled, relaunched, no
crash report, `mod-plugin-manager.so` confirmed loaded via `lsof` on the live PID, and
all five new UI strings ("Mastering Settings", "Saved Skill", "Save as Skill", "Target
LUFS", "True Peak Limit") confirmed physically present in the built binary. Van
confirmed on his end too ("LOADED PERFECTLY") after seeing the menu-merge popup is
gone. Everything in this section (Mastering Settings panel + touch-up chaining +
Skills + menu-merge fix) is real, shipped, and running as of 2026-08-06 ~10:25pm.

**Not yet tested**: a full real click-through of touch-up chaining (mix → type a
follow-up → confirm it refines the mixed result rather than re-processing the raw
track) and Skills (save one, pick it on a different track, confirm the style carries
over). Both are new this session and only verified by code review + successful
compile/load — Van should try them for real before this counts as fully proven.

## MCP hang investigation — narrowed further, root cause still open (10:35pm)
Read `ScriptCommandRelay.cpp`/`ResponseQueue.cpp`/`CommandBuilder.cpp` end to end
(safe, static analysis, no live app risk). The worker-thread/main-thread handoff
(`AddPendingEvent` → `AppCommandEvent` → main thread handles it → `ResponseQueue`
condition-variable wakes the worker) is a standard, correctly-implemented
producer/consumer pattern — no bug found there.

Checked what `GetInfo: Type=Commands` actually does
(`src/commands/GetInfoCommand.cpp` `SendCommands()`): it only iterates
`PluginManager::PluginsOfType(PluginTypeEffect | PluginTypeAudacityCommand)` — the
VST/LV2/Nyquist/built-in-effect plugin list. It does **not** touch the
MenuRegistry/AttachedItem system our custom `HgeEffectBrowser`/`HgeAiChat` commands
use at all. So the earlier theory ("it hangs because it chokes on our new custom
commands") is probably wrong — more likely something in a specific bundled
third-party plugin's `GetCommandDefinition()` call is slow or hanging (TDR Nova,
Graillon, GSnap, ValhallaFreqEcho, SSQ, Saturation Knob are all in this build).
Good news: probably not something this session broke. Bad news: finding which
plugin needs bisection testing (disable-one-at-a-time), which is live-app-risk
territory, not something to rush at the end of a long session.

**Left for next time**: don't re-attempt `SelectAll:`/`GetInfo:Type=Commands` blind.
If picking this back up, do it FIRST thing in a session (fresh launch, no prior
churn), test the plugin-list theory by temporarily moving plugin dylibs out of
`Contents/plug-ins`/`Contents/modules` one at a time and retrying `GetInfo:
Type=Commands` with a hard timeout each time, to isolate which one (if any) is the
actual hang. `get_tracks`/`get_selection` remain the only MCP tools proven reliably
safe to use as-is.
