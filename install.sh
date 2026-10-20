#!/bin/sh
# Local AI Monitor installer.
#   curl -fsSL https://raw.githubusercontent.com/ms95ir-ctrl/local-ai-monitor/main/install.sh | sh
# With options:
#   curl -fsSL .../install.sh | sh -s -- --dry-run
#   curl -fsSL .../install.sh | sh -s -- --yes --with-fans
#   curl -fsSL .../install.sh | sh -s -- --uninstall
# Downloads the repo to ~/.local/share/local-ai-monitor and runs host/setup.py, which detects the machine
# (GPU, LLM server, board, fans) and installs the feed as a systemd service. Read host/setup.py first if you like.
set -eu

REPO="${LAM_REPO:-ms95ir-ctrl/local-ai-monitor}"
BRANCH="${LAM_BRANCH:-main}"
DEST="${LAM_HOME:-$HOME/.local/share/local-ai-monitor}"

command -v python3 >/dev/null 2>&1 || { echo "python3 is required" >&2; exit 1; }
command -v curl >/dev/null 2>&1 || command -v wget >/dev/null 2>&1 || { echo "curl or wget is required" >&2; exit 1; }
command -v tar >/dev/null 2>&1 || { echo "tar is required" >&2; exit 1; }

URL="https://codeload.github.com/$REPO/tar.gz/refs/heads/$BRANCH"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

echo "Downloading $REPO ($BRANCH)..."
if command -v curl >/dev/null 2>&1; then curl -fsSL "$URL" -o "$TMP/src.tgz"; else wget -qO "$TMP/src.tgz" "$URL"; fi
mkdir -p "$TMP/src"
tar xzf "$TMP/src.tgz" -C "$TMP/src" --strip-components=1

mkdir -p "$DEST"
rm -rf "$DEST/src"
mv "$TMP/src" "$DEST/src"

# stdin is the script when piped, so setup.py asks its questions on /dev/tty
exec python3 "$DEST/src/host/setup.py" "$@"
