#!/bin/sh
# cxxprobe installer — downloads the latest release binary, verifies it, and
# installs it.
#
#   curl -sSf https://algorithms-mathematics-society.github.io/cxxprobe/install.sh | sh
#
# Env vars:
#   CXXPROBE_INSTALL_DIR    where to install (default: $HOME/.local/bin)
#   CXXPROBE_VERSION        install a specific tag instead of latest, e.g. v0.4.2
#   CXXPROBE_NO_MODIFY_PATH don't touch shell rc files — just print the export line
#   NO_COLOR                disable colored/styled output
set -eu

REPO="Algorithms-Mathematics-Society/cxxprobe"
INSTALL_DIR="${CXXPROBE_INSTALL_DIR:-$HOME/.local/bin}"
ASSET="cxxprobe-x86_64-linux"
CGROUP_ROOT="/sys/fs/cgroup/cxxprobe"

# ── styling ───────────────────────────────────────────────────────────────────
# Real ANSI bytes in a variable, not a printf format string — portable across
# /bin/sh implementations without relying on echo -e / printf %b quirks.
# Disabled for non-interactive stdout (piped/redirected) or NO_COLOR.
if [ -t 1 ] && [ -z "${NO_COLOR:-}" ] && [ "${TERM:-dumb}" != "dumb" ]; then
    BOLD=$(printf '\033[1m') DIM=$(printf '\033[2m')
    RED=$(printf '\033[31m') GREEN=$(printf '\033[32m')
    YELLOW=$(printf '\033[33m') CYAN=$(printf '\033[36m')
    RESET=$(printf '\033[0m')
    INTERACTIVE=1
else
    BOLD='' DIM='' RED='' GREEN='' YELLOW='' CYAN='' RESET=''
    INTERACTIVE=0
fi

title() { printf '\n%s%s%s\n' "$BOLD" "$1" "$RESET"; }
info() { printf '%s\n' "$1"; }
ok() { printf '  %s%s%s %s\n' "$GREEN" '✓' "$RESET" "$1"; }
warn() { printf '  %s%s%s %s\n' "$YELLOW" '!' "$RESET" "$1"; }
cmd_line() { printf '  %s%s%s\n' "$CYAN" "$1" "$RESET"; }
die() {
    printf '%s%s%s %s\n' "$RED" 'error:' "$RESET" "$1" >&2
    exit 1
}

printf '%s%s cxxprobe installer%s\n' "$BOLD" "$CYAN" "$RESET"
printf '%s%s%s\n' "$DIM" '────────────────────────────────────────' "$RESET"

# ── platform check ───────────────────────────────────────────────────────────
# cxxprobe sandboxes via cgroup v2 + Linux user namespaces — Linux only.
os=$(uname -s)
arch=$(uname -m)
case "$os" in
    Linux) ;;
    *)
        die "cxxprobe is Linux-only (it needs cgroup v2 + Linux user namespaces). Detected: $os. On macOS or Windows, use WSL2 (Windows) or build from source in a Linux VM/container."
        ;;
esac
case "$arch" in
    x86_64 | amd64) arch_label="x86_64" ;;
    *)
        die "no prebuilt binary for architecture '$arch' yet (x86_64 only for now). Build from source instead — see https://algorithms-mathematics-society.github.io/cxxprobe/getting-started"
        ;;
esac
ok "Platform: Linux ${arch_label}"

# ── resolve download URLs ─────────────────────────────────────────────────────
if [ -n "${CXXPROBE_VERSION:-}" ]; then
    base_url="https://github.com/$REPO/releases/download/$CXXPROBE_VERSION"
    version_label="$CXXPROBE_VERSION"
else
    base_url="https://github.com/$REPO/releases/latest/download"
    version_label="latest"
fi
bin_url="$base_url/$ASSET"
sha_url="$base_url/$ASSET.sha256"

have() { command -v "$1" >/dev/null 2>&1; }

# ── PATH setup ────────────────────────────────────────────────────────────────
# A child process (this script) can't change its parent shell's already-live
# environment — "automatic" here means appending an idempotent, clearly
# marked line to the right rc file for future sessions, detected from $SHELL.
RC_FILE=""
RELOAD_CMD=""
PATH_ALREADY_SET=0

add_to_path_permanently() {
    shell_name=$(basename "${SHELL:-sh}")
    case "$shell_name" in
        fish)
            RC_FILE="$HOME/.config/fish/config.fish"
            export_line="fish_add_path $INSTALL_DIR"
            RELOAD_CMD="source $RC_FILE"
            ;;
        zsh)
            RC_FILE="$HOME/.zshrc"
            export_line="export PATH=\"$INSTALL_DIR:\$PATH\""
            RELOAD_CMD=". $RC_FILE"
            ;;
        bash)
            RC_FILE="$HOME/.bashrc"
            export_line="export PATH=\"$INSTALL_DIR:\$PATH\""
            RELOAD_CMD=". $RC_FILE"
            ;;
        *)
            RC_FILE="$HOME/.profile"
            export_line="export PATH=\"$INSTALL_DIR:\$PATH\""
            RELOAD_CMD=". $RC_FILE"
            ;;
    esac

    if [ -f "$RC_FILE" ] && grep -qF "$INSTALL_DIR" "$RC_FILE" 2>/dev/null; then
        PATH_ALREADY_SET=1
        return
    fi

    mkdir -p "$(dirname "$RC_FILE")"
    printf '\n# added by cxxprobe installer\n%s\n' "$export_line" >>"$RC_FILE"
}

# Downloads $1 to file $2. Shows curl/wget's own progress bar when the
# terminal can render one; stays quiet otherwise (e.g. inside CI logs).
fetch_to_file() {
    if have curl; then
        if [ "$INTERACTIVE" = 1 ]; then
            curl -fL --progress-bar "$1" -o "$2"
        else
            curl -fsSL "$1" -o "$2"
        fi
    elif have wget; then
        if [ "$INTERACTIVE" = 1 ]; then
            wget -q --show-progress "$1" -O "$2"
        else
            wget -q "$1" -O "$2"
        fi
    else
        die "neither curl nor wget is available — install one and try again"
    fi
}

fetch_quiet() {
    if have curl; then
        curl -fsSL "$1"
    elif have wget; then
        wget -qO- "$1"
    else
        die "neither curl nor wget is available — install one and try again"
    fi
}

# ── download + verify ─────────────────────────────────────────────────────────
tmp_dir=$(mktemp -d)
trap 'rm -rf "$tmp_dir"' EXIT

title "Downloading ($version_label)"
fetch_to_file "$bin_url" "$tmp_dir/$ASSET" || die "download failed: $bin_url"
ok "Fetched $ASSET"

if fetch_quiet "$sha_url" >"$tmp_dir/$ASSET.sha256" 2>/dev/null && [ -s "$tmp_dir/$ASSET.sha256" ]; then
    expected=$(tr -d ' \n' <"$tmp_dir/$ASSET.sha256")
    if have sha256sum; then
        actual=$(sha256sum "$tmp_dir/$ASSET" | cut -d' ' -f1)
    elif have shasum; then
        actual=$(shasum -a 256 "$tmp_dir/$ASSET" | cut -d' ' -f1)
    else
        actual="$expected"
        warn "no sha256sum/shasum available — skipped checksum verification"
    fi
    if [ "$actual" != "$expected" ]; then
        die "checksum mismatch — expected $expected, got $actual. Try again or report this."
    fi
    [ "$actual" = "$expected" ] && [ -n "$actual" ] && ok "Checksum verified"
else
    warn "no checksum published for this release — skipped verification"
fi

# ── install ────────────────────────────────────────────────────────────────────
mkdir -p "$INSTALL_DIR"
chmod +x "$tmp_dir/$ASSET"
mv "$tmp_dir/$ASSET" "$INSTALL_DIR/cxxprobe"
ok "Installed to $INSTALL_DIR/cxxprobe"

installed_version=$("$INSTALL_DIR/cxxprobe" --version 2>/dev/null || printf 'cxxprobe (version unknown)')

title "Done — $installed_version"

path_ok=1
case ":$PATH:" in
    *":$INSTALL_DIR:"*) ;;
    *) path_ok=0 ;;
esac

cgroup_ready=1
[ -d "$CGROUP_ROOT" ] && [ -w "$CGROUP_ROOT" ] || cgroup_ready=0

if [ "$path_ok" = 1 ]; then
    : # already on PATH, nothing to do
elif [ -n "${CXXPROBE_NO_MODIFY_PATH:-}" ]; then
    warn "$INSTALL_DIR isn't on your PATH. Add this to your shell profile:"
    cmd_line "export PATH=\"$INSTALL_DIR:\$PATH\""
else
    add_to_path_permanently
    if [ "$PATH_ALREADY_SET" = 1 ]; then
        ok "PATH already configured in $RC_FILE"
    else
        ok "Added $INSTALL_DIR to PATH in $RC_FILE"
        warn "open a new terminal, or run this to use cxxprobe right now:"
        cmd_line "$RELOAD_CMD"
    fi
fi

if [ "$cgroup_ready" = 1 ]; then
    ok "cgroup delegation already set up"
else
    warn "one-time setup still needed before cxxprobe can sandbox anything:"
    cmd_line "echo \"+memory +cpu +pids\" | sudo tee /sys/fs/cgroup/cgroup.subtree_control"
    cmd_line "sudo mkdir -p $CGROUP_ROOT"
    cmd_line "sudo chown -R \"\$(whoami):\$(whoami)\" $CGROUP_ROOT"
    info "  ${DIM}(one-time per boot — /sys/fs/cgroup resets on reboot)${RESET}"
fi

info ""
info "${DIM}'cxxprobe new'/'cxxprobe test problem' also need a C++ compiler"
info "(g++ or clang++) on PATH to build solutions and checkers.${RESET}"
info ""
info "Docs: https://algorithms-mathematics-society.github.io/cxxprobe/"
info "Start here: https://algorithms-mathematics-society.github.io/cxxprobe/guides/build-a-contest"
