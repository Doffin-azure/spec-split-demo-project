#!/usr/bin/env python3
import json
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parent
SHARED = ROOT / "shared"
STATE = SHARED / "state.json"
PROPOSAL = SHARED / "proposal.json"
DECISION = SHARED / "decision.json"
DOCUMENT = SHARED / "document.md"


POLL_SEC = 0.2


def read_json(path: Path):
    if not path.exists():
        return None
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, obj: dict) -> None:
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(json.dumps(obj, ensure_ascii=True, indent=2), encoding="utf-8")
    tmp.replace(path)


def verify_like_sample_and_accept_n(target_tokens, accepted_pos, draft_tokens):
    # Simulate common_sampler_sample_and_accept_n:
    # - accept matching draft prefix
    # - break on first mismatch and emit target token at mismatch position
    # - if full draft matched, emit one extra target token
    result = []
    i = 0
    while i < len(draft_tokens):
        pos = accepted_pos + i + 1
        if pos >= len(target_tokens):
            break
        sampled = target_tokens[pos]
        result.append(sampled)
        if draft_tokens[i] != sampled:
            break
        i += 1

    if i == len(draft_tokens):
        pos = accepted_pos + i + 1
        if pos < len(target_tokens):
            result.append(target_tokens[pos])

    accepted_draft = max(0, len(result) - 1)
    return result, accepted_draft


def append_document(tokens):
    if not tokens:
        return
    text = " ".join(tokens)
    with DOCUMENT.open("a", encoding="utf-8") as f:
        f.write(text + " ")


def main() -> None:
    print("[verify] started")

    while True:
        state = read_json(STATE)
        if state is None:
            print("[verify] missing state.json, run init_demo.py first")
            return

        if state["done"]:
            print("[verify] done=true, exit")
            return

        proposal = read_json(PROPOSAL)
        if proposal is None:
            time.sleep(POLL_SEC)
            continue

        if proposal["round"] != state["round"]:
            time.sleep(POLL_SEC)
            continue

        if proposal["accepted_pos"] != state["accepted_pos"]:
            print("[verify] stale proposal ignored")
            time.sleep(POLL_SEC)
            continue

        target_tokens = state["target_tokens"]
        result_tokens, accepted_draft = verify_like_sample_and_accept_n(
            target_tokens=target_tokens,
            accepted_pos=state["accepted_pos"],
            draft_tokens=proposal["draft_tokens"],
        )

        new_accepted_pos = state["accepted_pos"] + len(result_tokens)
        done = new_accepted_pos >= len(target_tokens) - 1

        append_document(result_tokens)

        decision = {
            "round": state["round"],
            "accepted_draft": accepted_draft,
            "result_tokens": result_tokens,
            "new_accepted_pos": new_accepted_pos,
            "done": done,
        }
        write_json(DECISION, decision)

        next_state = dict(state)
        next_state["accepted_pos"] = new_accepted_pos
        next_state["round"] = state["round"] + 1
        next_state["done"] = done
        write_json(STATE, next_state)

        try:
            PROPOSAL.unlink()
        except FileNotFoundError:
            pass

        print(
            f"[verify] round {state['round']} verified: "
            f"accepted_draft={accepted_draft}, emitted={result_tokens}"
        )

        if done:
            print("[verify] target stream fully emitted")
            return

        time.sleep(POLL_SEC)


if __name__ == "__main__":
    main()
