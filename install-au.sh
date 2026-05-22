#!/bin/bash
# ==========================================================================
# HGE Music Studio — AU Component Installer
# ==========================================================================
# Installs all bundled plugins as Audio Unit components so they appear
# as clean one-click entries under Effect → AU.
#
# Usage:
#   ./install-au.sh              # Install all available AU components
#   ./install-au.sh --user       # Install to ~/Library only (no sudo)
#   ./install-au.sh --system     # Install to /Library (requires sudo)
#   ./install-au.sh --list       # List currently installed AU components
#   ./install-au.sh --verify     # Verify all AU components are valid
#
# ==========================================================================

set -euo pipefail

MODE="${1:---user}"
HGE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pkg)"
HGE_APP="$HGE_DIR/HgeMusicStudio.app"

# ─── Configuration ─────────────────────────────────────────────────────────

# Source PKG locations
PKG_TDR_NOVA="$HOME/Desktop/TDR Nova.pkg"
PKG_TDR_KOTELNIKOV="$HOME/Downloads/TDR Kotelnikov.pkg"

# System AU install location (requires sudo)
SYSTEM_AU_DIR="/Library/Audio/Plug-Ins/Components"

# User AU install location (no sudo needed)
USER_AU_DIR="$HOME/Library/Audio/Plug-Ins/Components"

# ─── Functions ─────────────────────────────────────────────────────────────

usage() {
  echo "Usage: $0 [--user|--system|--list|--verify]"
  exit 1
}

extract_au_from_pkg() {
  local pkg_path="$1"
  local plugin_name="$2"
  local target_dir="$3"

  if [ ! -f "$pkg_path" ]; then
    echo "  ⚠️  PKG not found: $pkg_path"
    return 1
  fi

  echo "  📦 Extracting $plugin_name from PKG..."

  local tmp_dir="/tmp/hge-au-$$"
  rm -rf "$tmp_dir"
  mkdir -p "$tmp_dir"

  # Expand PKG
  pkgutil --expand "$pkg_path" "$tmp_dir/expanded" 2>/dev/null

  # Find AU sub-package
  local au_pkg=""
  for f in "$tmp_dir/expanded"/*.pkg; do
    if echo "$f" | grep -qi "AU"; then
      au_pkg="$f"
      break
    fi
  done

  if [ -z "$au_pkg" ]; then
    echo "  ❌ No AU sub-package found in $pkg_path"
    rm -rf "$tmp_dir"
    return 1
  fi

  # Extract Payload
  mkdir -p "$tmp_dir/payload"
  cd "$tmp_dir/payload"
  cat "$au_pkg/Payload" | gunzip -dc 2>/dev/null | cpio -id 2>/dev/null || {
    # Try without gzip (some PKGs use raw cpio)
    cat "$au_pkg/Payload" | cpio -id 2>/dev/null
  }

  # Check if we got a Contents/ directory
  if [ ! -d "$tmp_dir/payload/Contents" ]; then
    echo "  ❌ No Contents/ found in payload"
    rm -rf "$tmp_dir"
    return 1
  fi

  # Create .component bundle
  local component_path="$target_dir/$plugin_name.component"
  rm -rf "$component_path"
  mkdir -p "$component_path"
  cp -R "$tmp_dir/payload/Contents" "$component_path/"

  rm -rf "$tmp_dir"
  echo "  ✅ Installed: $component_path"
  return 0
}

verify_au_component() {
  local name="$1"
  if auval -a 2>/dev/null | grep -qi "$name"; then
    echo "  ✅ $name — registered and valid"
    return 0
  else
    echo "  ⚠️  $name — NOT registered (may need app restart)"
    return 1
  fi
}

# ─── Main ──────────────────────────────────────────────────────────────────

echo "╔══════════════════════════════════════════════════════════════╗"
echo "║     HGE Music Studio — AU Component Installer               ║"
echo "╚══════════════════════════════════════════════════════════════╝"
echo ""

case "$MODE" in
  --list|-l)
    echo "System AU components:"
    ls "$SYSTEM_AU_DIR" 2>/dev/null | sed 's/^/  • /' || echo "  (none)"
    echo ""
    echo "User AU components:"
    ls "$USER_AU_DIR" 2>/dev/null | sed 's/^/  • /' || echo "  (none)"
    echo ""
    echo "Registered AU effects:"
    auval -a 2>/dev/null | grep "aufx" | sed 's/^/  /' | head -40
    exit 0
    ;;

  --verify|-v)
    echo "Verifying AU components..."
    echo ""
    for comp in "TDR Nova" "TDR Kotelnikov" "SSQ" "Saturation Knob"; do
      verify_au_component "$comp"
    done
    exit 0
    ;;

  --system|-s)
    TARGET_DIR="$SYSTEM_AU_DIR"
    echo "  Target: $TARGET_DIR (system-wide, requires sudo)"
    sudo mkdir -p "$TARGET_DIR"
    ;;
  --user|-u|*)
    TARGET_DIR="$USER_AU_DIR"
    echo "  Target: $TARGET_DIR (user-level, no sudo needed)"
    mkdir -p "$TARGET_DIR"
    ;;
esac

echo ""

# ── Install TDR Nova ──────────────────────────────────────────────────────
echo "── TDR Nova (EQ) ─────────────────────────────────────────────"
if [ -d "$TARGET_DIR/TDR Nova.component" ]; then
  echo "  ✅ Already installed"
else
  extract_au_from_pkg "$PKG_TDR_NOVA" "TDR Nova" "$TARGET_DIR" || {
    # Fallback: copy from user AU if available
    if [ -d "$USER_AU_DIR/TDR Nova.component" ]; then
      cp -R "$USER_AU_DIR/TDR Nova.component" "$TARGET_DIR/"
      echo "  ✅ Copied from user AU directory"
    else
      echo "  ⚠️  Could not install TDR Nova AU"
    fi
  }
fi

# ── Install TDR Kotelnikov ────────────────────────────────────────────────
echo ""
echo "── TDR Kotelnikov (Dynamics) ─────────────────────────────────"
if [ -d "$TARGET_DIR/TDR Kotelnikov.component" ]; then
  echo "  ✅ Already installed"
else
  extract_au_from_pkg "$PKG_TDR_KOTELNIKOV" "TDR Kotelnikov" "$TARGET_DIR" || {
    if [ -d "$USER_AU_DIR/TDR Kotelnikov.component" ]; then
      cp -R "$USER_AU_DIR/TDR Kotelnikov.component" "$TARGET_DIR/"
      echo "  ✅ Copied from user AU directory"
    else
      echo "  ⚠️  Could not install TDR Kotelnikov AU"
    fi
  }
fi

# ── Verify SSQ and Saturation Knob ────────────────────────────────────────
echo ""
echo "── SSQ (EQ) ──────────────────────────────────────────────────"
if [ -d "$SYSTEM_AU_DIR/SSQ.component" ]; then
  echo "  ✅ Already installed (system)"
elif [ -d "$USER_AU_DIR/SSQ.component" ]; then
  echo "  ✅ Already installed (user)"
else
  echo "  ⚠️  SSQ not found — may need reinstall from original PKG"
fi

echo ""
echo "── Saturation Knob (Mastering) ───────────────────────────────"
if [ -d "$SYSTEM_AU_DIR/Saturation Knob.component" ]; then
  echo "  ✅ Already installed (system)"
elif [ -d "$USER_AU_DIR/Saturation Knob.component" ]; then
  echo "  ✅ Already installed (user)"
else
  echo "  ⚠️  Saturation Knob not found — may need reinstall from original PKG"
fi

# ── Register with system ──────────────────────────────────────────────────
echo ""
echo "── Registering with Audio Component Manager ──────────────────"
echo "  Running auval to verify registration..."
auval -a 2>/dev/null | grep -E "TDR Nova|TDR Kotelnikov|SSQ|Saturation Knob" | sed 's/^/  /'

echo ""
echo "╔══════════════════════════════════════════════════════════════╗"
echo "║  ✅ AU components installed                                 ║"
echo "╚══════════════════════════════════════════════════════════════╝"
echo ""
echo "  📋 Next step:"
echo "     1. Open HGE Music Studio"
echo "     2. Go to Effect → AU → [plugin name]"
echo "     3. All AU plugins show as clean one-click entries"
echo ""
echo "  🔧 To reset plugin registry (forces clean rescan):"
echo "     rm ~/Library/Application\\ Support/HgeMusicStudio/pluginregistry.cfg"
echo ""
