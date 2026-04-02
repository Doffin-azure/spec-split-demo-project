#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

if [[ ! -d ".venv" ]]; then
  python3 -m venv .venv
fi

source .venv/bin/activate

python init_demo.py

python -u verify_side.py > shared/verify.log 2>&1 &
VERIFY_PID=$!

python -u draft_side.py > shared/draft.log 2>&1 &
DRAFT_PID=$!

echo "$VERIFY_PID" > shared/verify.pid
echo "$DRAFT_PID" > shared/draft.pid

echo "started:"
echo "  verify pid: $VERIFY_PID"
echo "  draft  pid: $DRAFT_PID"
echo ""
echo "logs:"
echo "  tail -f shared/verify.log"
echo "  tail -f shared/draft.log"
echo ""
echo "stop:"
echo "  ./clean_demo.sh"
