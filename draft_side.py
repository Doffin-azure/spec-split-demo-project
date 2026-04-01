#!/usr/bin/env python3
import json
import random
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parent
SHARED = ROOT / "shared"
STATE = SHARED / "state.json"
PROPOSAL = SHARED / "proposal.json"
DECISION = SHARED / "decision.json"
DOCUMENT = SHARED / "document.md"


N_MAX = 4
MISMATCH_PROB = 0.35
POLL_SEC = 0.2


def read_json(path: Path):
    if not path.exists():
        return None
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, obj: dict) -> None:
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(json.dumps(obj, ensure_ascii=True, indent=2), encoding="utf-8")
    tmp.replace(path)


def read_document_tail(max_chars: int = 120) -> str:
    if not DOCUMENT.exists():
        return ""
    text = DOCUMENT.read_text(encoding="utf-8").strip()
    if len(text) <= max_chars:
        return text
    return "..." + text[-max_chars:]


def make_draft(target_tokens, accepted_pos: int, n_max: int, rng: random.Random):
    start = accepted_pos + 1
    end = min(start + n_max, len(target_tokens))
    draft = target_tokens[start:end]
    if not draft:
        return draft

    if rng.random() < MISMATCH_PROB:
        i = rng.randrange(len(draft))
        # deterministic "wrong" token for easier debugging
        draft[i] = "<WRONG>"
    return draft


def main() -> None:
    rng = random.Random(1234)
    print("[draft] started")
    last_round_logged = -1

    while True:
        state = read_json(STATE)
        if state is None:
            print("[draft] missing state.json, run init_demo.py first")
            return

        if state["done"]:
            print("[draft] done=true, exit")
            return

        round_id = state["round"]
        accepted_pos = state["accepted_pos"]
        target_tokens = state["target_tokens"]
        id_last = target_tokens[accepted_pos]

        if round_id != last_round_logged:
            doc_tail = read_document_tail()
            print(f"[draft] observe doc before round {round_id}: {doc_tail!r}")
            last_round_logged = round_id

        cur_proposal = read_json(PROPOSAL)
        if cur_proposal is not None and cur_proposal.get("round") == round_id:
            # wait for verifier decision
            decision = read_json(DECISION)
            if decision is not None and decision.get("round") == round_id:
                doc_tail = read_document_tail()
                print(
                    f"[draft] round {round_id} decision: "
                    f"accepted_draft={decision['accepted_draft']} "
                    f"result={decision['result_tokens']} "
                    f"doc_tail={doc_tail!r}"
                )
            time.sleep(POLL_SEC)
            continue

        draft_tokens = make_draft(target_tokens, accepted_pos, N_MAX, rng)
        proposal = {
            "round": round_id,
            "accepted_pos": accepted_pos,
            "id_last": id_last,
            "draft_tokens": draft_tokens,
            "n_max": N_MAX,
        }
        write_json(PROPOSAL, proposal)
        print(
            f"[draft] round {round_id} proposal: "
            f"id_last={id_last} draft={draft_tokens}"
        )
        time.sleep(POLL_SEC)


if __name__ == "__main__":
    main()
