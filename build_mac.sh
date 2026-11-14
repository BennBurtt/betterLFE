#!/bin/bash
# Builds dist/SubGuard.app on macOS (Apple Silicon or Intel).
set -euo pipefail
cd "$(dirname "$0")"

PYTHON="${PYTHON:-python3}"
if [ ! -d .venv ]; then
    "$PYTHON" -m venv .venv
fi
source .venv/bin/activate
python -m pip install --upgrade pip
python -m pip install -r requirements-dev.txt

python -m pytest tests -q
pyinstaller --noconfirm --clean SubGuard.spec

echo
echo "Built: $(pwd)/dist/SubGuard.app"
