#!/bin/sh
# cxxprobe installer — downloads the latest release binary and verifies it.
#
#   curl -sSf https://algorithms-mathematics-society.github.io/cxxprobe/install.sh | sh
#
# Env vars:
#   CXXPROBE_INSTALL_DIR   where to install (default: $HOME/.local/bin)
#   CXXPROBE_VERSION       install a specific tag instead of latest, e.g. v0.4.2
set -eu

REPO="Algorithms-Mathematics-Society/cxxprobe"
INSTALL_DIR="${CXXPROBE_INSTALL_DIR:-$HOME/.local/bin}"
ASSET="cxxprobe-x86_64-linux"

say() { printf '%s\n' "$1"; }
err() {
    printf 'cxxprobe: error: %s\n' "$1" >&2
    exit 1
}

# ── platform check ───────────────────────────────────────────────────────────
# cxxprobe sandboxes via cgroup v2 + Linux user namespaces — Linux only.
os=$(uname -s)
arch=$(uname -m)
case "$os" in
    Linux) ;;
    *)
        err "cxxprobe is Linux-only (it needs cgroup v2 + Linux user namespaces). Detected: $os. On macOS or Windows, use WSL2 (Windows) or build from source in a Linux VM/container."
        ;;
esac
case "$arch" in
    x86_64 | amd64) ;;
    *)
        err "no prebuilt binary for architecture '$arch' yet (x86_64 only for now). Build from source instead — see https://algorithms-mathematics-society.github.io/cxxprobe/getting-started"
        ;;
esac

# ── resolve download URLs ─────────────────────────────────────────────────────
if [ -n "${CXXPROBE_VERSION:-}" ]; then
    base_url="https://github.com/$REPO/releases/download/$CXXPROBE_VERSION"
else
    base_url="https://github.com/$REPO/releases/latest/download"
fi
bin_url="$base_url/$ASSET"
sha_url="$base_url/$ASSET.sha256"

fetch() {
    if command -v curl >/dev/null 2>&1; then
        curl -fsSL "$1"
    elif command -v wget >/dev/null 2>&1; then
        wget -qO- "$1"
    else
        err "neither curl nor wget is available — install one and try again"
    fi
}

# ── download + verify ─────────────────────────────────────────────────────────
tmp_dir=$(mktemp -d)
trap 'rm -rf "$tmp_dir"' EXIT

say "Downloading $ASSET..."
fetch "$bin_url" >"$tmp_dir/$ASSET" || err "download failed: $bin_url"
fetch "$sha_url" >"$tmp_dir/$ASSET.sha256" 2>/dev/null || say "(no checksum published for this release, skipping verification)"

if [ -s "$tmp_dir/$ASSET.sha256" ]; then
    expected=$(tr -d ' \n' <"$tmp_dir/$ASSET.sha256")
    if command -v sha256sum >/dev/null 2>&1; then
        actual=$(sha256sum "$tmp_dir/$ASSET" | cut -d' ' -f1)
    elif command -v shasum >/dev/null 2>&1; then
        actual=$(shasum -a 256 "$tmp_dir/$ASSET" | cut -d' ' -f1)
    else
        say "(no sha256sum/shasum available, skipping verification)"
        actual="$expected"
    fi
    [ "$actual" = "$expected" ] || err "checksum mismatch — expected $expected, got $actual. Try again or report this."
    say "Checksum verified."
fi

# ── install ────────────────────────────────────────────────────────────────────
mkdir -p "$INSTALL_DIR"
chmod +x "$tmp_dir/$ASSET"
mv "$tmp_dir/$ASSET" "$INSTALL_DIR/cxxprobe"

say ""
say "cxxprobe installed to $INSTALL_DIR/cxxprobe"
"$INSTALL_DIR/cxxprobe" --version 2>/dev/null || true

case ":$PATH:" in
    *":$INSTALL_DIR:"*) ;;
    *)
        say ""
        say "$INSTALL_DIR isn't on your PATH. Add this to your shell profile:"
        say "  export PATH=\"$INSTALL_DIR:\$PATH\""
        ;;
esac

say ""
say "This binary is self-contained (no yaml-cpp/libstdc++ install needed)."
say "'cxxprobe new'/'cxxprobe test problem' still need a C++ compiler (g++ or"
say "clang++) on PATH to build solutions and checkers — 'cxxprobe run' alone"
say "doesn't."
say ""
say "One-time setup still required before it can sandbox anything — see:"
say "https://algorithms-mathematics-society.github.io/cxxprobe/getting-started#one-time-cgroup-delegation"
