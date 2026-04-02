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
DRAFT_NATIVE_BIN="${DRAFT_NATIVE_BIN:-$LLAMA_ROOT/build/bin/llama-spec-split-draft}"
VERIFY_NATIVE_BIN="${VERIFY_NATIVE_BIN:-$LLAMA_ROOT/build/bin/llama-spec-split-verify}"

DRAFT_MODEL="${DRAFT_MODEL:-/Users/doffin_azure/Code/Project/models-ms/qwen2.5-0.5b-instruct-q4_k_m.gguf}"
VERIFY_MODEL="${VERIFY_MODEL:-/Users/doffin_azure/Code/Project/models-ms/qwen2.5-1.5b-instruct-q4_k_m.gguf}"

CTX_SIZE="${CTX_SIZE:-512}"
N_MAX="${N_MAX:-4}"
MAX_OUTPUT_TOKENS="${MAX_OUTPUT_TOKENS:-64}"
PROMPT="${PROMPT:-Write one concise paragraph explaining speculative decoding.}"

if [[ ! -x "$DRAFT_NATIVE_BIN" ]]; then
  echo "missing native draft binary: $DRAFT_NATIVE_BIN"
  echo "build it with: cd $LLAMA_ROOT && cmake -S . -B build && cmake --build build --target llama-spec-split-draft -j 8"
  exit 1
fi

if [[ ! -x "$VERIFY_NATIVE_BIN" ]]; then
  echo "missing native verify binary: $VERIFY_NATIVE_BIN"
  echo "build it with: cd $LLAMA_ROOT && cmake -S . -B build && cmake --build build --target llama-spec-split-verify -j 8"
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

"$VERIFY_NATIVE_BIN" \
  --model "$VERIFY_MODEL" \
  --shared-dir "$ROOT/shared" \
  --prompt "$PROMPT" \
  --max-output-tokens "$MAX_OUTPUT_TOKENS" \
  --ctx-size "$CTX_SIZE" \
  --batch-size 512 \
  --ubatch-size 512 \
  --gpu-layers 99 \
  > shared/verify_native.log 2>&1 &
VERIFY_PID=$!

"$DRAFT_NATIVE_BIN" \
  --model "$DRAFT_MODEL" \
  --shared-dir "$ROOT/shared" \
  --prompt "$PROMPT" \
  --n-max "$N_MAX" \
  --ctx-size "$CTX_SIZE" \
  --batch-size 512 \
  --ubatch-size 512 \
  --gpu-layers 99 \
  > shared/draft_native.log 2>&1 &
DRAFT_PID=$!

echo "$VERIFY_PID" > shared/verify.pid
echo "$DRAFT_PID" > shared/draft.pid

echo "started native full model demo:"
echo "  verify native pid: $VERIFY_PID"
echo "  draft native  pid: $DRAFT_PID"
echo ""
echo "logs:"
echo "  tail -f shared/verify_native.log"
echo "  tail -f shared/draft_native.log"
echo ""
echo "stop:"
echo "  ./clean_demo.sh"
