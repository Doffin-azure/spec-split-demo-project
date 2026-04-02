# Split Speculative Decoding Plan (Current Record)

## 1) Goal

Build a low-cost split demo that keeps the `llama.cpp` speculative control flow conceptually intact while allowing draft-side and verify-side to run as independent processes (and later, on different machines).

## 2) Current architecture

- `draft_side.py`: proposal producer
- `verify_side.py`: acceptance authority + manuscript writer
- file bus in `shared/`:
  - `state.json`
  - `config.json`
  - `proposal.json`
  - `decision.json`
  - `document.md`

There are two modes:

- `toy`: protocol-only simulation using fixed target stream
- `model`: real dual-model testing via two `llama-server` endpoints

## 3) Information ownership split

### Verify-side (authoritative)

- round progression (`round`)
- accepted cursor (`accepted_pos`)
- accepted token-id history (`accepted_token_ids`, model mode)
- done condition (`done`)
- accepted tokens appended to `document.md`
- decision result (`accepted_draft`, `result_token_ids`, `result_text`, `new_accepted_pos`)

### Draft-side

- per-round draft proposal (`draft_token_ids`, model mode)
- draft generation from its own model endpoint

### Shared transport data

- request-like: `round`, `accepted_pos`, `id_last`, `n_max`
- response-like:
  - toy mode: `draft_tokens`
  - model mode: `draft_token_ids`, `draft_text`
- decision-like:
  - toy mode: `result_tokens`
  - model mode: `result_token_ids`, `result_text`
  - common: `accepted_draft`, `new_accepted_pos`, `done`

## 4) How this maps to `llama.cpp` design

- mirrors `draft -> verify -> accept/reject -> next round`
- verify-side remains the final authority for accepted output
- no KV sharing between sides (same as native conceptual separation of draft/target contexts)

## 5) Current limitations

- toy mode remains whitespace simulation by design
- model mode now uses token-id exchange and verify-side token-id acceptance loop
- verifier reproduces `sample_and_accept_n` semantics but uses repeated 1-token endpoint calls (functionally aligned, not throughput-aligned with in-process batched decode)
- no direct KV-level synchronization protocol between machines

## 6) Immediate next step to increase fidelity

Implement a long-lived target-side slot/session API that evaluates `[id_last + draft...]` in one pass per round (to match native batching and reduce network overhead), while preserving token-id protocol.

## 7) Draft rollback status

- Previous gap:
  - draft side used stateless HTTP completions and rebuilt from accepted text/token history each round
  - no explicit KV rollback call comparable to native `llama_memory_seq_rm(...)`
- Current implementation:
  - native draft worker (`llama-spec-split-draft`) keeps persistent local context
  - before each round, it aligns to authoritative accepted sequence and rolls back speculative tail with:
    - `llama_memory_seq_rm(llama_get_memory(ctx), 0, target_seq_len, -1)`
  - then it continues drafting from the rolled-back state
