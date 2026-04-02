#!/usr/bin/env python3
import json
import os
import random
import time
import urllib.error
import urllib.request
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parent
SHARED = ROOT / "shared"
STATE = SHARED / "state.json"
PROPOSAL = SHARED / "proposal.json"
DECISION = SHARED / "decision.json"
DOCUMENT = SHARED / "document.md"
CONFIG = SHARED / "config.json"


N_MAX = 4
MISMATCH_PROB = 0.35
POLL_SEC = 0.2
DRAFT_ENDPOINT = os.getenv("DRAFT_ENDPOINT", "http://127.0.0.1:8091")
DRAFT_SLOT_ID = int(os.getenv("DRAFT_SLOT_ID", "0"))


def read_json(path: Path):
    if not path.exists():
        return None
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, obj: dict) -> None:
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(json.dumps(obj, ensure_ascii=True, indent=2), encoding="utf-8")
    tmp.replace(path)


def read_document_tail(max_chars: int = 160) -> str:
    if not DOCUMENT.exists():
        return ""
    text = DOCUMENT.read_text(encoding="utf-8").strip()
    if len(text) <= max_chars:
        return text
    return "..." + text[-max_chars:]


def http_post_json(endpoint: str, route: str, payload: dict[str, Any]) -> dict[str, Any]:
    req = urllib.request.Request(
        f"{endpoint}{route}",
        data=json.dumps(payload).encode("utf-8"),
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    with urllib.request.urlopen(req, timeout=30) as resp:
        return json.loads(resp.read().decode("utf-8"))


def tokenize(endpoint: str, text: str, add_special: bool) -> list[int]:
    res = http_post_json(
        endpoint,
        "/tokenize",
        {"content": text, "add_special": add_special, "parse_special": True},
    )
    return [int(x) for x in res.get("tokens", [])]


def detokenize(endpoint: str, token_ids: list[int]) -> str:
    if not token_ids:
        return ""
    res = http_post_json(endpoint, "/detokenize", {"tokens": token_ids})
    return str(res.get("content", ""))


def sample_many_draft(endpoint: str, prompt_token_ids: list[int], n_predict: int) -> list[int]:
    # Deterministic greedy settings for reproducible split verification.
    res = http_post_json(
        endpoint,
        "/completion",
        {
            "prompt": prompt_token_ids,
            "n_predict": n_predict,
            "id_slot": DRAFT_SLOT_ID,
            "cache_prompt": True,
            "temperature": 0.0,
            "top_k": 1,
            "top_p": 1.0,
            "min_p": 0.0,
            "repeat_penalty": 1.0,
            "presence_penalty": 0.0,
            "frequency_penalty": 0.0,
            "return_tokens": True,
            "stream": False,
        },
    )
    return [int(x) for x in res.get("tokens", [])]


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
    draft_prompt_ids_cache: list[int] | None = None

    while True:
        state = read_json(STATE)
        config = read_json(CONFIG)
        if state is None:
            print("[draft] missing state.json, run init_demo.py first")
            return
        if config is None:
            print("[draft] missing config.json, run init_demo.py first")
            return

        if state["done"]:
            print("[draft] done=true, exit")
            return

        mode = state.get("mode", "toy")
        round_id = int(state["round"])
        accepted_pos = int(state["accepted_pos"])
        n_max = int(config.get("n_max", N_MAX))
        id_last = accepted_pos

        if round_id != last_round_logged:
            doc_tail = read_document_tail()
            print(f"[draft] observe doc before round {round_id}: {doc_tail!r}")
            last_round_logged = round_id

        cur_proposal = read_json(PROPOSAL)
        if cur_proposal is not None and int(cur_proposal.get("round", -1)) == round_id:
            decision = read_json(DECISION)
            if decision is not None and int(decision.get("round", -1)) == round_id:
                doc_tail = read_document_tail()
                print(
                    f"[draft] round {round_id} decision: "
                    f"accepted_draft={decision['accepted_draft']} "
                    f"result={decision.get('result_tokens', [])} "
                    f"doc_tail={doc_tail!r}"
                )
            time.sleep(POLL_SEC)
            continue

        if mode == "toy":
            target_tokens = state["target_tokens"]
            id_last = target_tokens[accepted_pos]
            draft_tokens = make_draft(target_tokens, accepted_pos, n_max, rng)
            proposal = {
                "round": round_id,
                "accepted_pos": accepted_pos,
                "id_last": id_last,
                "draft_tokens": draft_tokens,
                "n_max": n_max,
            }
        else:
            prompt = str(config.get("prompt", ""))
            accepted_ids = [int(x) for x in state.get("accepted_token_ids", [])]
            try:
                if draft_prompt_ids_cache is None:
                    draft_prompt_ids_cache = tokenize(DRAFT_ENDPOINT, prompt, add_special=True)
                prompt_ids = draft_prompt_ids_cache + accepted_ids
                draft_ids = sample_many_draft(DRAFT_ENDPOINT, prompt_ids, n_predict=n_max)
                draft_text = detokenize(DRAFT_ENDPOINT, draft_ids)
            except (TimeoutError, urllib.error.URLError, json.JSONDecodeError, ValueError) as e:
                print(f"[draft] model call failed in round {round_id}: {e}")
                time.sleep(POLL_SEC)
                continue

            proposal = {
                "round": round_id,
                "accepted_pos": accepted_pos,
                "id_last": accepted_ids[-1] if accepted_ids else -1,
                "draft_token_ids": draft_ids,
                "draft_text": draft_text,
                "n_max": n_max,
            }

        write_json(PROPOSAL, proposal)
        if mode == "toy":
            print(
                f"[draft] round {round_id} proposal: "
                f"id_last={proposal['id_last']} draft={proposal['draft_tokens']}"
            )
        else:
            print(
                f"[draft] round {round_id} proposal: "
                f"id_last={proposal['id_last']} draft_ids={proposal['draft_token_ids']}"
            )
        time.sleep(POLL_SEC)


if __name__ == "__main__":
    main()
