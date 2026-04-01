#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

python3 -m venv .venv
source .venv/bin/activate

python -m pip install --upgrade pip

# Optional but useful if system cmake is unavailable.
python -m pip install cmake

chmod +x init_demo.py draft_side.py verify_side.py run_demo.sh clean_demo.sh || true

echo "environment ready"
echo "activate with: source .venv/bin/activate"
echo "run demo with: ./run_demo.sh"

