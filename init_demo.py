#!/usr/bin/env python3
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parent
SHARED = ROOT / "shared"
STATE = SHARED / "state.json"
PROPOSAL = SHARED / "proposal.json"
DECISION = SHARED / "decision.json"
DOCUMENT = SHARED / "document.md"


def write_json(path: Path, obj: dict) -> None:
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(json.dumps(obj, ensure_ascii=True, indent=2), encoding="utf-8")
    tmp.replace(path)


def main() -> None:
    SHARED.mkdir(parents=True, exist_ok=True)

    target_text = "I like distributed speculative decoding where draft and verify are split."
    target_tokens = ["<BOS>"] + target_text.split(" ")

    state = {
        "round": 0,
        "accepted_pos": 0,
        "target_tokens": target_tokens,
        "done": False,
    }

    write_json(STATE, state)
    if PROPOSAL.exists():
        PROPOSAL.unlink()
    if DECISION.exists():
        DECISION.unlink()
    DOCUMENT.write_text("# Shared Document\n\n", encoding="utf-8")

    print("Initialized demo")
    print(f"- target tokens: {len(target_tokens) - 1}")
    print(f"- shared dir: {SHARED}")


if __name__ == "__main__":
    main()
