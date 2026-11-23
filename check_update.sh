#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

ENV_FILE="$SCRIPT_DIR/.env"
if [[ ! -f "$ENV_FILE" ]]; then
    echo "Error: .env file not found at $ENV_FILE" >&2
    exit 1
fi

# Load .env configuration
# shellcheck disable=SC1090
source "$ENV_FILE"

: "${VANILLA_DIR:?VANILLA_DIR must be set in .env}"
: "${PREVIOUS_VERSION:?PREVIOUS_VERSION must be set in .env}"
: "${NEW_VERSION:?NEW_VERSION must be set in .env}"
PAMT_PATH="${PAMT_PATH:-$HOME/.local/share/Steam/steamapps/common/Crimson Desert/0012/0.pamt}"
FILTER="${FILTER:-inputmap*.xml}"

PREVIOUS_DIR="${VANILLA_DIR}/${PREVIOUS_VERSION}"
NEW_DIR="${VANILLA_DIR}/${NEW_VERSION}"

# Resolve Python binary from local venv (checks python/.venv and .venv)
if [[ -x "$SCRIPT_DIR/python/.venv/bin/python" ]]; then
    PYTHON_BIN="$SCRIPT_DIR/python/.venv/bin/python"
elif [[ -x "$SCRIPT_DIR/.venv/bin/python" ]]; then
    PYTHON_BIN="$SCRIPT_DIR/.venv/bin/python"
elif [[ -n "${VIRTUAL_ENV:-}" && -x "$VIRTUAL_ENV/bin/python" ]]; then
    PYTHON_BIN="$VIRTUAL_ENV/bin/python"
else
    echo "Warning: No local venv found, falling back to system python..." >&2
    PYTHON_BIN="python"
fi

echo "=== 1. Unpacking '${FILTER}' for version ${NEW_VERSION} ==="
echo "PAMT:   $PAMT_PATH"
echo "Target: $NEW_DIR"
echo "Python: $PYTHON_BIN"
echo ""

if [[ ! -f "$PAMT_PATH" ]]; then
    echo "Error: PAMT file not found: $PAMT_PATH" >&2
    exit 1
fi

"$PYTHON_BIN" "$SCRIPT_DIR/python/paz_unpack.py" "$PAMT_PATH" \
    --filter "$FILTER" \
    --output "$NEW_DIR"

echo ""
echo "=== 2. Comparing ${PREVIOUS_VERSION} with ${NEW_VERSION} ==="

if [[ ! -d "$PREVIOUS_DIR" ]]; then
    echo "Error: Previous version directory does not exist: $PREVIOUS_DIR" >&2
    exit 1
fi

if diff -rq "$PREVIOUS_DIR" "$NEW_DIR" >/dev/null 2>&1; then
    echo "✅ No content changes between ${PREVIOUS_VERSION} and ${NEW_VERSION}!"
    echo "   The files are identical – the mod can remain unchanged."
    exit 0
else
    echo "--- Detailed diff (diff -ru) ---"
    diff -ru --color=auto "$PREVIOUS_DIR" "$NEW_DIR" || true
    echo ""
    echo "⚠️  Changes detected between ${PREVIOUS_VERSION} and ${NEW_VERSION}:"
    diff -rq "$PREVIOUS_DIR" "$NEW_DIR" || true
    exit 2
fi
