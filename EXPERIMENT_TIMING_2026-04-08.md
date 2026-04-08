# Experiment Record: Draft-Side Sync Hot-Path Iterations (2026-04-08)

## Scope

This record captures iterative draft-side optimization work on the Android + desktop `llama_cpp_spec_split` lane.

Prompt and model pair stayed consistent with the existing split experiment:

- prompt: `Explain speculative decoding briefly.`
- draft model (Android): `Llama-3.2-1B-Instruct-Q4_K_M.gguf`
- target model (desktop): `Llama-3.2-3B-Instruct-Q4_K_M.gguf`

## Code Changes Tested

### Change Set A (kept)

File:

- `lib/src/main/cpp/ai_chat.cpp`

Changes:

1. removed duplicate sampler-history rebuild on the active-session sync path
2. stopped forcing per-step sequence-state snapshot capture (`llama_state_seq_get_data`) in `syncAndGenerateDraftRealTokenIds` / `syncPersistentDraftSession`
3. added lazy fallback rebuild for sessions whose sequence snapshot is marked stale

### Change Set B (reverted)

File:

- `lib/src/main/cpp/ai_chat.cpp`

Changes (rolled back):

- forced sampler alignment to `temp=0`, `top_k=1`

Result:

- this increased large-draft bursts and worsened draft-side wall-clock on device

### Change Set C (kept)

File:

- `lib/src/main/cpp/ai_chat.cpp`

Changes:

- removed O(prefix) full-text detokenize rebuild from the split draft sync hot path
- kept token-first state on sync and rebuild paths

## Run Records

### Run A: post Change Set A baseline

- run: `2026-04-08T10-22-52+08-00`
- status: completed
- output: [android_spec_split_app_output_2026-04-08T10-22-52+08-00.txt](/C:/Users/JXZ/AndroidStudioProjects/MyApplication2/reference/spec-split-demo-project/experiments/2026-04-08/android_spec_split_app_output_2026-04-08T10-22-52+08-00.txt)

Key metrics:

- `committedTokens=26`
- `totalMs=15681`
- `totalDraftFetchMs=13522`
- `totalRemoteProposeMs=2000`
- `overallTokensPerSecond=1.658`

### Run B: Change Set B (`temp=0/top_k=1`) experiment (reverted)

- run: `2026-04-08T10-26-16+08-00`
- status: completed
- output: [android_spec_split_app_output_2026-04-08T10-26-16+08-00.txt](/C:/Users/JXZ/AndroidStudioProjects/MyApplication2/reference/spec-split-demo-project/experiments/2026-04-08/android_spec_split_app_output_2026-04-08T10-26-16+08-00.txt)

Key metrics:

- `committedTokens=20`
- `totalMs=41024`
- `totalDraftFetchMs=37150`
- `totalRemoteProposeMs=3730`
- `overallTokensPerSecond=0.488`

Interpretation:

- deterministic sampler forcing on Android draft was harmful for this lane under current verifier behavior
- change was reverted

### Run C: after reverting Change Set B (A only)

- run: `2026-04-08T10-28-35+08-00`
- status: completed
- output: [android_spec_split_app_output_2026-04-08T10-28-35+08-00.txt](/C:/Users/JXZ/AndroidStudioProjects/MyApplication2/reference/spec-split-demo-project/experiments/2026-04-08/android_spec_split_app_output_2026-04-08T10-28-35+08-00.txt)

Key metrics:

- `committedTokens=64`
- `totalMs=38387`
- `totalDraftFetchMs=33517`
- `totalRemoteProposeMs=4703`
- `overallTokensPerSecond=1.667`

### Run D: Change Set C on top of A

- run: `2026-04-08T10-31-13+08-00`
- status: completed
- output: [android_spec_split_app_output_2026-04-08T10-31-13+08-00.txt](/C:/Users/JXZ/AndroidStudioProjects/MyApplication2/reference/spec-split-demo-project/experiments/2026-04-08/android_spec_split_app_output_2026-04-08T10-31-13+08-00.txt)

Key metrics:

- `committedTokens=64`
- `totalMs=37739`
- `totalDraftFetchMs=32942`
- `totalRemoteProposeMs=4633`
- `overallTokensPerSecond=1.696`

Direct C -> D delta:

- `totalMs`: `38387 -> 37739` (`-1.69%`)
- `totalDraftFetchMs`: `33517 -> 32942` (`-1.72%`)
- `overallTokensPerSecond`: `1.667 -> 1.696` (`+1.74%`)

## Draft-Side Issue Ledger (Current)

The following draft-side issues are now confirmed from code and experiment traces.

Resolved in this iteration:

1. per-step forced seq-state capture in split sync hot path (`llama_state_seq_get_data`) caused avoidable fixed overhead
2. duplicate sampler-history rebuild on active-session sync path
3. per-step O(prefix) assistant detokenize rebuild during authoritative sync

Still unresolved:

1. sampler-history rebuild is still O(prefix) each sync step (single rebuild remains)
2. proposal efficiency is low in many runs (`accepted/proposed` ratio unstable), causing wasted draft decode work
3. draft-side token budget is static (`DRAFT_MAX_TOKENS=16`) with no adaptive cap based on recent accept/reject behavior
4. occasional script-level runtime block (`no_device_output_captured`) still appears on some runs and needs receiver trigger hardening / retry policy

## Current Conclusion

- Draft-side hot-path cost has been reduced, but not yet to a level that can claim 50% draft-side optimization versus the earlier baseline.
- The next likely high-impact node is adaptive draft-step sizing tied to recent accept behavior, plus reducing remaining sampler-history rebuild costs.
