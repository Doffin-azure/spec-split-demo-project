# Spec Split Demo (File Bus)

This demo shows how to separate speculative drafting and verification into two independent processes.

- `draft_side.py` proposes draft tokens and writes `proposal.json`.
- `verify_side.py` reads proposal, verifies it, appends accepted output to `document.md`, and writes `decision.json`.

Communication is done through shared files:

- `shared/state.json`
- `shared/config.json`
- `shared/proposal.json`
- `shared/decision.json`
- `shared/document.md`

## Modes

### 1) `toy` mode (protocol-only)

- no model inference
- deterministic target stream
- used to validate round protocol quickly

```bash
cd /Users/doffin_azure/Code/Project/spec-split-demo-project
./setup_env.sh
./run_demo.sh
tail -f shared/verify.log
tail -f shared/draft.log
./clean_demo.sh
```

### 2) `model` mode (real dual-model test)

- draft side calls `DRAFT_ENDPOINT/completion`
- verify side calls `VERIFY_ENDPOINT/completion`
- verifier applies token-id `sample_and_accept_n` semantics:
  - compare sampled token against draft token id position-by-position
  - stop at first mismatch
  - if full draft matched, sample one extra token
- model-mode exchange payload includes `draft_token_ids` and `result_token_ids`

```bash
cd /Users/doffin_azure/Code/Project/spec-split-demo-project
./setup_env.sh
./run_model_demo.sh
tail -f shared/verify_server.log
tail -f shared/draft_server.log
tail -f shared/verify.log
tail -f shared/draft.log
./clean_demo.sh
```

### 3) `model-native-verify` mode (closer to llama.cpp core path)

- draft side uses `llama-server` (`/completion`)
- verify side uses native binary `llama-spec-split-verify`
- verify binary executes local `llama_decode + common_sampler_sample_and_accept_n`

```bash
cd /Users/doffin_azure/Code/Project/spec-split-demo-project
./run_model_demo_native.sh
tail -f shared/verify_native.log
tail -f shared/draft.log
./clean_demo.sh
```

### 4) `model-native-full` mode (draft + verify both native)

- draft side: native binary `llama-spec-split-draft`
- verify side: native binary `llama-spec-split-verify`
- both sides hold persistent local contexts and communicate only through shared protocol files

```bash
cd /Users/doffin_azure/Code/Project/spec-split-demo-project
./run_model_demo_native_full.sh
tail -f shared/verify_native.log
tail -f shared/draft_native.log
./clean_demo.sh
```

In this mode, draft-side rollback is done with `llama_memory_seq_rm(...)` against unconfirmed speculative tail tokens before the next round proposal.

## Experiment Records

- Timing experiment record: `EXPERIMENT_TIMING_2026-04-03.md`
- Upstream `llama.cpp` patch snapshots used by this project:
  - `upstream-patches/0001-examples-add-native-split-draft-verify-workers-for-s.patch`
  - `upstream-patches/0002-feat-add-stage-timing-instrumentation-and-split-vs-n.patch`

## Run with custom models

```bash
DRAFT_MODEL=/abs/path/draft.gguf \
VERIFY_MODEL=/abs/path/verify.gguf \
PROMPT="Write one concise paragraph explaining speculative decoding." \
N_MAX=4 MAX_OUTPUT_TOKENS=64 CTX_SIZE=512 \
./run_model_demo.sh
```

Optional slot tuning for stable KV reuse across rounds:

```bash
DRAFT_SLOT_ID=0 VERIFY_SLOT_ID=0 ./run_model_demo.sh
```

## Cross-machine split

When draft and verify are deployed on different machines, run the two servers independently and point workers to remote endpoints:

```bash
DRAFT_ENDPOINT=http://draft-host:8091 \
VERIFY_ENDPOINT=http://verify-host:8092 \
python -u draft_side.py

DRAFT_ENDPOINT=http://draft-host:8091 \
VERIFY_ENDPOINT=http://verify-host:8092 \
python -u verify_side.py
```

## Notes

- This is an engineering demo for split protocol and system integration.
- `toy` mode is a whitespace simulation for fast protocol checks.
- `model` mode uses token-id exchange through `/tokenize`, `/detokenize`, and `/completion` with deterministic greedy sampling (`temperature=0`, `top_k=1`).
- See `SEPARATION_PLAN.md` for split design details and remaining gap to native in-process performance parity.
