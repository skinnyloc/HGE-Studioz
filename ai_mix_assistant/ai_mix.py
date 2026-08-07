#!/usr/bin/env python3
"""
HGE Music Studio — AI Mix Assistant

Takes an audio file and a plain-English prompt, asks Claude to turn that into
a concrete mix/master plan (EQ bands, compression, warmth, target loudness),
and applies it with ffmpeg. Two auth modes:

  --mode personal   Uses the local `claude` CLI (your Claude Code subscription
                     login) via headless -p/--output-format json. No API key,
                     no per-request billing — this is for Van's own use only.

  --mode api        Uses the real Anthropic Messages API with a user-supplied
                     key (BYOK). This is the path for the sold DMG — every end
                     user brings their own key, billed to them. Client picks a
                     model (--model) and a thinking effort (--think), or just
                     picks --think and a sensible model is chosen for them.

Usage:
    python3 ai_mix.py --in song.wav --out song_mixed.wav \\
        --prompt "warm the vocal, tighten the low end, gentle glue compression" \\
        --mode personal

    python3 ai_mix.py --in song.wav --out song_mixed.wav \\
        --prompt "loud and punchy for streaming" \\
        --mode api --api-key sk-ant-... --think thinking
"""

import argparse
import json
import subprocess
import sys
from pathlib import Path

CONFIG_PATH = Path.home() / "Library/Application Support/HgeMusicStudio/ai_mix_config.json"

# --think presets for --mode api (client/sold-DMG path). Each bundles a
# sensible default model + thinking config so non-technical buyers get one
# simple dial instead of raw model IDs; --model still overrides the model
# choice if given explicitly.
THINK_PRESETS = {
    "fast":     {"model": "claude-haiku-4-5-20251001", "thinking_budget": 0,    "max_tokens": 1024},
    "balanced": {"model": "claude-sonnet-5",            "thinking_budget": 0,    "max_tokens": 1024},
    "thinking": {"model": "claude-sonnet-5",            "thinking_budget": 8000, "max_tokens": 10000},
}
DEFAULT_API_MODEL = THINK_PRESETS["balanced"]["model"]

# Manual override presets for the Mastering Settings panel in HgeAiChat. Any
# of these, when explicitly picked (not "auto"), overrides that one field of
# whatever plan Claude returns -- lets the user force an exact number/style
# instead of hoping the AI parses it correctly out of free text.
TRUE_PEAK_DEFAULT = -1.0

EQ_STYLE_PRESETS = {
    "off":     [],   # explicit "don't touch the EQ" (same as natural, clearer label)
    "natural": [],
    "bright":  [{"freq_hz": 4000,  "gain_db": 2.0,  "q": 1.2},
                {"freq_hz": 12000, "gain_db": 2.0,  "q": 0.8}],
    "warm":    [{"freq_hz": 250,   "gain_db": 1.5,  "q": 1.0},
                {"freq_hz": 10000, "gain_db": -1.5, "q": 0.8}],
}

COMPRESSION_PRESETS = {
    "off":    None,
    "light":  {"threshold_db": -20, "ratio": 2.0, "attack_ms": 15, "release_ms": 150, "makeup_db": 1.0},
    "medium": {"threshold_db": -18, "ratio": 3.0, "attack_ms": 10, "release_ms": 120, "makeup_db": 2.0},
    "heavy":  {"threshold_db": -16, "ratio": 5.0, "attack_ms": 5,  "release_ms": 80,  "makeup_db": 3.5},
}

SATURATION_PRESETS = {
    "none":   0.0,
    "subtle": 0.25,
    "medium": 0.5,
}

SYSTEM_PROMPT = """You are a mixing/mastering engineer assistant for HGE Music Studio,
built on hip-hop/trap engineering practice (PlatinumForge chain + mastering-target
knowledge). Given a loudness/dynamics analysis of an audio file and a plain-English
request, output ONLY a strict JSON object (no prose, no markdown fences).

Normal case — you have enough to make a real engineering decision:
{
  "type": "plan",
  "eq": [ { "freq_hz": 120, "gain_db": -2.5, "q": 1.0 }, ... up to 6 bands ],
  "compression": { "threshold_db": -18, "ratio": 3.0, "attack_ms": 20, "release_ms": 200, "makeup_db": 2.0 },
  "warmth": 0.3,
  "target_lufs": -14.0,
  "notes": "one short sentence explaining the reasoning"
}

Rare case — the request is genuinely ambiguous in a way that changes the outcome
(e.g. no destination/genre/reference given AND the ask is vague, like just "make it
better"): ask ONE short question instead of guessing:
{ "type": "question", "text": "Streaming, radio, or club? And is this trap/melodic/aggressive?" }
Prefer "plan" over "question" whenever you can make a reasonable default call —
only ask when guessing would genuinely go the wrong direction. "type" defaults to
"plan" if omitted.

Engineering knowledge to ground your decisions (hip-hop/trap vocal + master):

Mastering LUFS targets by destination (set target_lufs from context clues in the
request; default -14 if nothing suggests otherwise):
  streaming (Spotify/YouTube) -14 | Apple Music -14 to -16 | hip-hop/R&B feel -11 to -12
  | club/DJ -9 to -8 | SoundCloud -8 to -10 | radio -14 to -16 (leave true-peak room)
Don't crush dynamics to hit a loud number; a louder target should come from more
limiting headroom usage, not just cranking makeup gain.

Vocal chain order (cut before you boost, control before you color):
  1. mud cut ~200-400 Hz (-2 to -4 dB) is the #1 "amateur mix" tell — cut it whenever
     a vocal is described as muddy, boxy, or "not sitting on the beat"
  2. de-ess / harsh control 5-9 kHz, 2-5 dB, if brightness or air is being added
  3. compression: control comp 3:1-4:1 / 5-15ms attack / 3-6dB GR for "even, not
     squashed"; tighter/heavier (4:1+, more GR) for "punchy"/"in your face"/"aggressive";
     softer (2.5:1-3:1, ~3dB GR) for "smooth"/"breathing"/"melodic"
  4. presence +1 to +3 dB around 3-5 kHz for "upfront"/"intelligible"/"cutting through"
  5. air high-shelf +1 to +3 dB at 10-16 kHz for "polish"/"shine"/"expensive" (pair
     with de-ess so it doesn't turn harsh)
  6. warmth/saturation for "warm"/"analog"/"tape"/"vintage" asks — keep it light
     (0.2-0.4); heavier (0.5-0.7) only for explicit "saturated"/"driven"/"gritty"
  7. master-bus glue comp is gentler than a vocal comp: 1.5:1-2:1, slow attack,
     1-2 dB GR — cohesion, not squash

Rules:
- eq gain_db range -12..+12, freq_hz range 20..20000, q range 0.3..8
- compression.ratio range 1..20, threshold_db range -40..0
- warmth is 0.0 (none) to 1.0 (heavy analog saturation), use it sparingly
- If the request doesn't need EQ or compression, return an empty array/omit the
  relevant fields rather than inventing changes
- Base your choices on the provided analysis (current LUFS, peak, dynamic range)
  AND the engineering knowledge above, not just the prompt text alone
"""


def analyze_audio(path: str) -> dict:
    """Run ffmpeg loudnorm (analysis pass) + astats to characterize the input."""
    cmd = [
        "ffmpeg", "-hide_banner", "-nostats", "-i", path,
        "-af", "loudnorm=print_format=json,astats=metadata=1:reset=1",
        "-f", "null", "-",
    ]
    result = subprocess.run(cmd, capture_output=True, text=True)
    stderr = result.stderr

    # loudnorm prints a JSON blob at the end of stderr
    start = stderr.rfind("{")
    end = stderr.rfind("}")
    loudnorm_data = {}
    if start != -1 and end != -1:
        try:
            loudnorm_data = json.loads(stderr[start:end + 1])
        except json.JSONDecodeError:
            pass

    def grab(key):
        for line in stderr.splitlines():
            if key in line:
                try:
                    return float(line.split(":")[-1].strip().split()[0])
                except (ValueError, IndexError):
                    pass
        return None

    return {
        "integrated_lufs": float(loudnorm_data.get("input_i", "nan")) if loudnorm_data else None,
        "true_peak_dbtp": float(loudnorm_data.get("input_tp", "nan")) if loudnorm_data else None,
        "lra": float(loudnorm_data.get("input_lra", "nan")) if loudnorm_data else None,
        "rms_peak_db": grab("Peak level dB"),
        "rms_level_db": grab("RMS level dB"),
    }


def get_mix_plan(analysis: dict, prompt: str, mode: str, api_key: str = None,
                  model: str = None, think: str = "balanced", skill_plan: dict = None) -> dict:
    user_message = ""
    if skill_plan:
        user_message += (
            "The user has a saved mixing/mastering recipe they like, called "
            f"{skill_plan.get('_skill_name', 'their saved skill')!r}. Use it as your "
            f"starting point and match its style -- only deviate from it where "
            f"today's request below explicitly asks for something different:\n"
            f"{json.dumps(skill_plan)}\n\n"
        )
    user_message += (
        f"Audio analysis: {json.dumps(analysis)}\n\n"
        f"Request: {prompt}"
    )

    if mode == "personal":
        cmd = [
            "claude", "-p", user_message,
            "--output-format", "json",
            "--system-prompt", SYSTEM_PROMPT,
        ]
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
        if result.returncode != 0:
            raise RuntimeError(f"claude CLI failed: {result.stderr.strip()}")
        envelope = json.loads(result.stdout)
        raw_text = envelope.get("result", envelope) if isinstance(envelope, dict) else envelope
        return _extract_json(raw_text if isinstance(raw_text, str) else json.dumps(raw_text))

    elif mode == "api":
        if not api_key:
            raise RuntimeError("--api-key (or ai_mix_config.json anthropic_api_key) is required for --mode api")
        import urllib.request

        preset = THINK_PRESETS.get(think, THINK_PRESETS["balanced"])
        request = {
            "model": model or preset["model"],
            "max_tokens": preset["max_tokens"],
            "system": SYSTEM_PROMPT,
            "messages": [{"role": "user", "content": user_message}],
        }
        if preset["thinking_budget"] > 0:
            request["thinking"] = {"type": "enabled", "budget_tokens": preset["thinking_budget"]}

        body = json.dumps(request).encode()
        req = urllib.request.Request(
            "https://api.anthropic.com/v1/messages",
            data=body,
            headers={
                "x-api-key": api_key,
                "anthropic-version": "2023-06-01",
                "content-type": "application/json",
            },
        )
        with urllib.request.urlopen(req, timeout=60) as resp:
            data = json.loads(resp.read())
        raw_text = "".join(block.get("text", "") for block in data.get("content", []))
        return _extract_json(raw_text)

    else:
        raise ValueError(f"Unknown mode: {mode}")


def _extract_json(text: str) -> dict:
    """Claude's response should be pure JSON, but strip markdown fences defensively."""
    text = text.strip()
    if text.startswith("```"):
        text = text.split("```")[1]
        if text.startswith("json"):
            text = text[4:]
    return json.loads(text.strip())


def build_filter_chain(plan: dict) -> str:
    filters = []

    for band in plan.get("eq", []):
        freq = max(20, min(20000, band.get("freq_hz", 1000)))
        gain = max(-12, min(12, band.get("gain_db", 0)))
        q = max(0.3, min(8, band.get("q", 1.0)))
        if abs(gain) > 0.1:
            filters.append(f"equalizer=f={freq}:t=q:w={q}:g={gain}")

    comp = plan.get("compression")
    if comp:
        threshold = max(-40, min(0, comp.get("threshold_db", -18)))
        ratio = max(1, min(20, comp.get("ratio", 3)))
        attack = max(1, comp.get("attack_ms", 20))
        release = max(1, comp.get("release_ms", 200))
        makeup = max(0, min(24, comp.get("makeup_db", 0)))
        filters.append(
            f"acompressor=threshold={threshold}dB:ratio={ratio}:"
            f"attack={attack}:release={release}:makeup={makeup}dB"
        )

    warmth = plan.get("warmth", 0)
    if warmth and warmth > 0.02:
        amount = max(0, min(1, warmth))
        # aexciter: harmonic exciter for analog-style warmth/saturation
        filters.append(f"aexciter=amount={amount * 2:.2f}:drive={1 + amount * 2:.2f}")

    target_lufs = plan.get("target_lufs", -14.0)
    true_peak = plan.get("true_peak_dbtp", TRUE_PEAK_DEFAULT)
    filters.append(f"loudnorm=I={target_lufs}:TP={true_peak}:LRA=11")

    # Real limiter stage, not just loudnorm's own single-pass TP handling --
    # loudnorm can overshoot its TP target slightly in single-pass mode (this
    # is what let a requested -1.0 ceiling slip through as -1.5 before this
    # existed). alimiter's `limit` is linear (0..1), not dB, hence the
    # conversion; level=disabled so it only catches peaks, doesn't re-ride
    # the level loudnorm already set. Gated on plan["limiter"] (default on) so
    # the user can turn it off from the Mastering Settings panel.
    if plan.get("limiter", True):
        limit_linear = max(0.0625, min(1.0, 10 ** (true_peak / 20)))
        filters.append(f"alimiter=limit={limit_linear:.4f}:attack=5:release=50:level=false")

    return ",".join(filters)


def apply_plan(input_path: str, output_path: str, plan: dict) -> None:
    filter_chain = build_filter_chain(plan)
    cmd = ["ffmpeg", "-y", "-hide_banner", "-loglevel", "error",
           "-i", input_path, "-af", filter_chain, output_path]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"ffmpeg failed: {result.stderr.strip()}")


def load_config() -> dict:
    if CONFIG_PATH.exists():
        try:
            return json.loads(CONFIG_PATH.read_text())
        except json.JSONDecodeError:
            return {}
    return {}


def apply_overrides(plan: dict, args) -> None:
    """Force Mastering Settings panel picks onto Claude's plan in place.
    Anything not explicitly set on args is left exactly as the AI returned
    it -- this is a per-field override, not a wholesale replacement."""
    if args.target_lufs is not None:
        plan["target_lufs"] = args.target_lufs
    plan["true_peak_dbtp"] = args.true_peak if args.true_peak is not None else TRUE_PEAK_DEFAULT

    if args.eq_style is not None:
        plan["eq"] = EQ_STYLE_PRESETS[args.eq_style]

    if args.compression is not None:
        plan["compression"] = COMPRESSION_PRESETS[args.compression]

    if args.saturation is not None:
        plan["warmth"] = SATURATION_PRESETS[args.saturation]

    if args.limiter is not None:
        plan["limiter"] = (args.limiter == "on")


def main():
    parser = argparse.ArgumentParser(description="HGE Music Studio AI Mix Assistant")
    parser.add_argument("--in", dest="input_path", required=True, help="Input audio file")
    parser.add_argument("--out", dest="output_path", required=True, help="Output audio file")
    parser.add_argument("--prompt", default="",
                         help="Plain-English mix/master request. OPTIONAL: leave it blank "
                              "to master using only the manual settings below (no AI call).")
    parser.add_argument("--mode", choices=["personal", "api"], default=None,
                         help="Defaults to 'api' if a saved API key exists (client/BYOK "
                              "build), else 'personal' (Van's subscription build)")
    parser.add_argument("--api-key", default=None, help="Anthropic API key (--mode api)")
    parser.add_argument("--model", default=None, help="Override the Anthropic model id (--mode api)")
    parser.add_argument("--think", choices=list(THINK_PRESETS.keys()), default="balanced",
                         help="Thinking effort for --mode api: fast/balanced/thinking (default: balanced)")
    # Mastering Settings panel overrides (HgeAiChat). Any of these, when
    # given, wins over whatever Claude's plan says for that field -- "auto"
    # (or omitting the flag) leaves it to the AI.
    parser.add_argument("--target-lufs", type=float, default=None,
                         help="Force an exact target LUFS instead of letting the AI pick")
    parser.add_argument("--true-peak", type=float, default=None,
                         help="Force an exact true-peak ceiling in dBTP (default -1.0)")
    parser.add_argument("--eq-style", choices=list(EQ_STYLE_PRESETS.keys()), default=None,
                         help="Force an EQ preset instead of the AI's own eq bands")
    parser.add_argument("--compression", choices=list(COMPRESSION_PRESETS.keys()), default=None,
                         help="Force a compression preset instead of the AI's own settings")
    parser.add_argument("--saturation", choices=list(SATURATION_PRESETS.keys()), default=None,
                         help="Force a saturation/warmth preset instead of the AI's own value")
    parser.add_argument("--limiter", choices=["on", "off"], default=None,
                         help="Brickwall limiter after loudnorm: on (default, catches the "
                              "true-peak ceiling exactly) or off (loudnorm handles peaks alone)")
    parser.add_argument("--skill-file", default=None,
                         help="Path to a saved skill JSON (a previous plan) to use as a "
                              "style anchor -- Claude matches it unless today's prompt "
                              "asks for something different")
    args = parser.parse_args()

    config = load_config()
    api_key = args.api_key or config.get("anthropic_api_key")
    model = args.model or config.get("model")
    think = args.think or config.get("think", "balanced")
    # No --mode given: a saved key means this is a BYOK/client build, use it;
    # no key means this is Van's personal build, use the subscription CLI.
    mode = args.mode or ("api" if api_key else "personal")

    skill_plan = None
    if args.skill_file:
        try:
            skill_plan = json.loads(Path(args.skill_file).read_text())
        except (OSError, json.JSONDecodeError) as exc:
            print(f"Warning: couldn't load skill file {args.skill_file}: {exc}")

    print(f"Analyzing {args.input_path}...")
    before = analyze_audio(args.input_path)
    print(f"  Input:  LUFS={before['integrated_lufs']}  peak={before['true_peak_dbtp']}dBTP  LRA={before['lra']}")

    prompt = (args.prompt or "").strip()
    if not prompt:
        # No prompt given -- master straight from the manual settings, no AI
        # call at all (fast, free, deterministic). If a saved skill is loaded,
        # use that recipe as the base; otherwise a clean transparent master
        # (no EQ/comp/warmth, -14 LUFS) that apply_overrides then tweaks.
        print("No prompt given — mastering with your settings only (no AI call)...")
        if skill_plan:
            plan = dict(skill_plan)
            plan.pop("_skill_name", None)
        else:
            plan = {"eq": [], "compression": None, "warmth": 0.0,
                    "target_lufs": -14.0, "notes": "manual master (no prompt)"}
    else:
        print(f"Asking Claude ({mode} mode" + (f", think={think}" if mode == "api" else "") + ") for a mix plan...")
        plan = get_mix_plan(before, prompt, mode, api_key=api_key, model=model,
                             think=think, skill_plan=skill_plan)

        if plan.get("type") == "question":
            # Ambiguous request — the model wants one clarifying answer before
            # committing to a plan, instead of guessing. The caller (HgeAiChat's
            # dialog, or a human at the CLI) is expected to re-run with the
            # answer folded into --prompt. Distinct stdout prefix so the C++
            # dialog can tell this apart from a normal completed run.
            print(f"CLARIFY:{plan.get('text', 'Can you say more about what you want?')}")
            return

    apply_overrides(plan, args)
    print(f"  Plan: {json.dumps(plan, indent=2)}")
    # Machine-readable, single line -- lets the caller offer "save this as a
    # skill" without re-parsing the pretty-printed block above.
    print(f"PLAN_JSON:{json.dumps(plan)}")

    print(f"Applying with ffmpeg -> {args.output_path}...")
    apply_plan(args.input_path, args.output_path, plan)

    after = analyze_audio(args.output_path)
    print(f"  Output: LUFS={after['integrated_lufs']}  peak={after['true_peak_dbtp']}dBTP  LRA={after['lra']}")
    print("Done.")


if __name__ == "__main__":
    main()
