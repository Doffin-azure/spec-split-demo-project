#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

if [[ ! -d ".venv" ]]; then
  python3 -m venv .venv
fi
source .venv/bin/activate

LLAMA_ROOT_DEFAULT="$(cd "$ROOT/../llama.cpp" && pwd)"
LLAMA_ROOT="${LLAMA_ROOT:-$LLAMA_ROOT_DEFAULT}"
SERVER_BIN="${SERVER_BIN:-$LLAMA_ROOT/build/bin/llama-server}"

DRAFT_MODEL="${DRAFT_MODEL:-/Users/doffin_azure/Code/Project/models-ms/qwen2.5-0.5b-instruct-q4_k_m.gguf}"
VERIFY_MODEL="${VERIFY_MODEL:-/Users/doffin_azure/Code/Project/models-ms/qwen2.5-1.5b-instruct-q4_k_m.gguf}"

DRAFT_PORT="${DRAFT_PORT:-8091}"
VERIFY_PORT="${VERIFY_PORT:-8092}"
CTX_SIZE="${CTX_SIZE:-512}"
N_MAX="${N_MAX:-4}"
MAX_OUTPUT_TOKENS="${MAX_OUTPUT_TOKENS:-64}"
PROMPT="${PROMPT:-Write one concise paragraph explaining speculative decoding.}"

if [[ ! -x "$SERVER_BIN" ]]; then
  echo "missing server binary: $SERVER_BIN"
  echo "build it with: cd $LLAMA_ROOT && cmake --build build --target llama-server -j 8"
  exit 1
fi

if [[ ! -f "$DRAFT_MODEL" ]]; then
  echo "missing draft model: $DRAFT_MODEL"
  exit 1
fi

if [[ ! -f "$VERIFY_MODEL" ]]; then
  echo "missing verify model: $VERIFY_MODEL"
  exit 1
fi

python init_demo.py \
  --mode model \
  --prompt "$PROMPT" \
  --n-max "$N_MAX" \
  --max-output-tokens "$MAX_OUTPUT_TOKENS"

"$SERVER_BIN" -m "$DRAFT_MODEL" --port "$DRAFT_PORT" -ngl 99 -c "$CTX_SIZE" --no-webui \
  > shared/draft_server.log 2>&1 &
DRAFT_SERVER_PID=$!

"$SERVER_BIN" -m "$VERIFY_MODEL" --port "$VERIFY_PORT" -ngl 99 -c "$CTX_SIZE" --no-webui \
  > shared/verify_server.log 2>&1 &
VERIFY_SERVER_PID=$!

wait_health () {
  local url="$1"
  local name="$2"
  local n=0
  until curl -fsS "$url/health" >/dev/null 2>&1; do
    n=$((n + 1))
    if [[ $n -gt 120 ]]; then
      echo "timeout waiting for $name health at $url"
      exit 1
    fi
    sleep 1
  done
}

wait_health "http://127.0.0.1:${DRAFT_PORT}" "draft server"
wait_health "http://127.0.0.1:${VERIFY_PORT}" "verify server"

DRAFT_ENDPOINT="http://127.0.0.1:${DRAFT_PORT}" \
VERIFY_ENDPOINT="http://127.0.0.1:${VERIFY_PORT}" \
python -u verify_side.py > shared/verify.log 2>&1 &
VERIFY_PID=$!

DRAFT_ENDPOINT="http://127.0.0.1:${DRAFT_PORT}" \
VERIFY_ENDPOINT="http://127.0.0.1:${VERIFY_PORT}" \
python -u draft_side.py > shared/draft.log 2>&1 &
DRAFT_PID=$!

echo "$VERIFY_PID" > shared/verify.pid
echo "$DRAFT_PID" > shared/draft.pid
echo "$DRAFT_SERVER_PID" > shared/draft_server.pid
echo "$VERIFY_SERVER_PID" > shared/verify_server.pid

echo "started model demo:"
echo "  draft server  pid: $DRAFT_SERVER_PID (port $DRAFT_PORT)"
echo "  verify server pid: $VERIFY_SERVER_PID (port $VERIFY_PORT)"
echo "  verify proc   pid: $VERIFY_PID"
echo "  draft proc    pid: $DRAFT_PID"
echo ""
echo "logs:"
echo "  tail -f shared/verify_server.log"
echo "  tail -f shared/draft_server.log"
echo "  tail -f shared/verify.log"
echo "  tail -f shared/draft.log"
echo ""
echo "stop:"
echo "  ./clean_demo.sh"
