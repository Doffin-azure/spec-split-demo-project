# Experiment Record: Split vs Native Speculative (2026-04-03)

This repository is the project-of-record.
The actual native C++ instrumentation for this experiment was implemented in local `llama.cpp`, and is referenced here with commit hashes + patch snapshots.

## Model/Run Setup

- Draft model: `qwen2.5-0.5b-instruct-q4_k_m.gguf`
- Verify model: `qwen2.5-1.5b-instruct-q4_k_m.gguf`
- Prompt: `Explain speculative decoding briefly.`
- `n_max=4`, `max_output_tokens=24`, `ctx=512`
- Backend: Apple Metal

## Result Summary (one run, 7 rounds)

Split-native-full (`llama-spec-split-draft` + `llama-spec-split-verify`):
- Draft generation avg: `30.51 ms`
- Draft sync+tail prep avg: `1.95 + 11.94 ms`
- proposal -> verify communication avg: `82.52 ms`
- decision -> next draft communication avg: `160.48 ms`
- Verify decode avg: `6.22 ms` (warm rounds near `0.64 ms`)
- Verify sample avg: `47.19 ms`
- Verify rollback avg: `0.0024 ms`
- Reject rounds: `4`

Native baseline (`llama-speculative-simple`):
- Draft avg: `36.27 ms`
- Decode avg: `0.87 ms`
- Sample avg: `47.15 ms`
- Post/KV cleanup avg: `0.0019 ms`
- Reject rounds: `4`

Warm-round comparison (excluding round 0):
- Split proposal -> verify comm avg: `74.54 ms`
- Split decision -> draft comm avg: `160.48 ms`
- Split draft compute avg: `25.26 ms`
- Split verify decode avg: `0.635 ms`
- Split verify sample avg: `47.52 ms`
- Native draft compute avg: `36.27 ms`
- Native decode avg: `0.869 ms`
- Native sample avg: `47.15 ms`

## Interpretation

- Core speculative logic stages are aligned (verify sample, rollback/post are in the same scale).
- The dominant gap is split-process exchange overhead (file bus + polling latency), especially `decision -> next draft`.

## Upstream llama.cpp Mapping

Local `llama.cpp` commits used by this experiment:
- `0b003d65c`: add native split draft/verify workers
- `e4ad79a89`: add stage timing instrumentation + experiment record

Patch snapshots are vendored in this repository:
- `upstream-patches/0001-examples-add-native-split-draft-verify-workers-for-s.patch`
- `upstream-patches/0002-feat-add-stage-timing-instrumentation-and-split-vs-n.patch`

