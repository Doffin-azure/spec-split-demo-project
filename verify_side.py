#!/usr/bin/env python3
import json
import os
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


POLL_SEC = 0.2
VERIFY_ENDPOINT = os.getenv("VERIFY_ENDPOINT", "http://127.0.0.1:8092")
VERIFY_SLOT_ID = int(os.getenv("VERIFY_SLOT_ID", "0"))


def read_json(path: Path):
    if not path.exists():
        return None
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, obj: dict) -> None:
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(json.dumps(obj, ensure_ascii=True, indent=2), encoding="utf-8")
    tmp.replace(path)


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


def sample_one_token(endpoint: str, prompt_token_ids: list[int]) -> int | None:
    # Deterministic single-step sampling to mirror sample-and-accept logic.
    res = http_post_json(
        endpoint,
        "/completion",
        {
            "prompt": prompt_token_ids,
            "n_predict": 1,
            "id_slot": VERIFY_SLOT_ID,
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
    toks = res.get("tokens", [])
    if not toks:
        return None
    return int(toks[0])


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


def verify_model_like_sample_and_accept_n(
    endpoint: str,
    prompt_ids: list[int],
    accepted_ids: list[int],
    draft_ids: list[int],
) -> tuple[list[int], int, bool]:
    """
    Token-id equivalent of common_sampler_sample_and_accept_n using repeated
    single-token target sampling.

    Returns: (result_ids, accepted_draft, exhausted)
    """
    result: list[int] = []
    accepted_draft = 0
    exhausted = False

    for i in range(len(draft_ids)):
        tok = sample_one_token(endpoint, prompt_ids + accepted_ids + draft_ids[:i])
        if tok is None:
            exhausted = True
            break
        result.append(tok)
        if tok != draft_ids[i]:
            accepted_draft = i
            return result, accepted_draft, exhausted

    # all draft matched -> sample one more from full draft prefix
    if len(draft_ids) > 0:
        accepted_draft = len(draft_ids)
        extra = sample_one_token(endpoint, prompt_ids + accepted_ids + draft_ids)
        if extra is None:
            exhausted = True
        else:
            result.append(extra)
    else:
        # no draft, still sample one token to advance generation
        extra = sample_one_token(endpoint, prompt_ids + accepted_ids)
        if extra is None:
            exhausted = True
        else:
            result.append(extra)
            accepted_draft = 0

    return result, accepted_draft, exhausted


def append_document_text(text: str):
    if not text:
        return
    with DOCUMENT.open("a", encoding="utf-8") as f:
        f.write(text)


def main() -> None:
    print("[verify] started")
    verify_prompt_ids_cache: list[int] | None = None

    while True:
        state = read_json(STATE)
        config = read_json(CONFIG)
        if state is None:
            print("[verify] missing state.json, run init_demo.py first")
            return
        if config is None:
            print("[verify] missing config.json, run init_demo.py first")
            return

        if state["done"]:
            print("[verify] done=true, exit")
            return

        proposal = read_json(PROPOSAL)
        if proposal is None:
            time.sleep(POLL_SEC)
            continue

        if int(proposal["round"]) != int(state["round"]):
            time.sleep(POLL_SEC)
            continue

        if int(proposal["accepted_pos"]) != int(state["accepted_pos"]):
            print("[verify] stale proposal ignored")
            time.sleep(POLL_SEC)
            continue

        mode = state.get("mode", "toy")
        exhausted = False

        if mode == "toy":
            target_tokens = state["target_tokens"]
            result_tokens, accepted_draft = verify_like_sample_and_accept_n(
                target_tokens=target_tokens,
                accepted_pos=state["accepted_pos"],
                draft_tokens=proposal["draft_tokens"],
            )
            result_text = (" ".join(result_tokens) + " ") if result_tokens else ""
            done = state["accepted_pos"] + len(result_tokens) >= len(target_tokens) - 1
            result_payload_tokens = result_tokens
            result_payload_token_ids: list[int] = []
            new_accepted_ids = state.get("accepted_token_ids", [])
        else:
            prompt = str(config.get("prompt", ""))
            n_max = int(config.get("n_max", proposal.get("n_max", 4)))
            max_output_tokens = int(config.get("max_output_tokens", 64))
            accepted_ids = [int(x) for x in state.get("accepted_token_ids", [])]
            draft_ids = [int(x) for x in proposal.get("draft_token_ids", [])][:n_max]

            try:
                if verify_prompt_ids_cache is None:
                    verify_prompt_ids_cache = tokenize(VERIFY_ENDPOINT, prompt, add_special=True)
                result_ids, accepted_draft, exhausted = verify_model_like_sample_and_accept_n(
                    endpoint=VERIFY_ENDPOINT,
                    prompt_ids=verify_prompt_ids_cache,
                    accepted_ids=accepted_ids,
                    draft_ids=draft_ids,
                )
                result_text = detokenize(VERIFY_ENDPOINT, result_ids)
            except (TimeoutError, urllib.error.URLError, json.JSONDecodeError, ValueError) as e:
                print(f"[verify] model call failed in round {state['round']}: {e}")
                time.sleep(POLL_SEC)
                continue

            new_accepted_ids = accepted_ids + result_ids
            done = exhausted or (len(new_accepted_ids) >= max_output_tokens)

            result_payload_token_ids = result_ids
            result_payload_tokens = []

        new_accepted_pos = int(state["accepted_pos"]) + (
            len(result_payload_tokens) if mode == "toy" else len(result_payload_token_ids)
        )

        append_document_text(result_text)

        decision = {
            "round": state["round"],
            "accepted_draft": accepted_draft,
            "result_tokens": result_payload_tokens,
            "result_token_ids": result_payload_token_ids,
            "result_text": result_text,
            "new_accepted_pos": new_accepted_pos,
            "done": done,
            "exhausted": exhausted,
        }
        write_json(DECISION, decision)

        next_state = dict(state)
        next_state["accepted_pos"] = new_accepted_pos
        next_state["round"] = int(state["round"]) + 1
        next_state["done"] = done
        if mode == "model":
            next_state["accepted_token_ids"] = new_accepted_ids
        write_json(STATE, next_state)

        try:
            PROPOSAL.unlink()
        except FileNotFoundError:
            pass

        if mode == "toy":
            print(
                f"[verify] round {state['round']} verified: "
                f"accepted_draft={accepted_draft}, emitted={result_payload_tokens}"
            )
        else:
            print(
                f"[verify] round {state['round']} verified: "
                f"accepted_draft={accepted_draft}, emitted_ids={result_payload_token_ids}"
            )

        if done:
            print("[verify] target stream fully emitted")
            return

        time.sleep(POLL_SEC)


if __name__ == "__main__":
    main()
