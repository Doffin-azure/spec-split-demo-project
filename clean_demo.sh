#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

stop_pid_file () {
  local f="$1"
  if [[ -f "$f" ]]; then
    local pid
    pid="$(cat "$f" || true)"
    if [[ -n "${pid:-}" ]] && kill -0 "$pid" 2>/dev/null; then
      kill "$pid" || true
    fi
    rm -f "$f"
  fi
}

stop_pid_file "shared/verify.pid"
stop_pid_file "shared/draft.pid"
stop_pid_file "shared/verify_server.pid"
stop_pid_file "shared/draft_server.pid"

echo "stopped demo processes (if running)."
