#!/usr/bin/env python3
import argparse
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parent
SHARED = ROOT / "shared"
STATE = SHARED / "state.json"
PROPOSAL = SHARED / "proposal.json"
DECISION = SHARED / "decision.json"
DOCUMENT = SHARED / "document.md"
CONFIG = SHARED / "config.json"


def write_json(path: Path, obj: dict) -> None:
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(json.dumps(obj, ensure_ascii=True, indent=2), encoding="utf-8")
    tmp.replace(path)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--mode", choices=["toy", "model"], default="toy")
    parser.add_argument(
        "--target-text",
        default="I like distributed speculative decoding where draft and verify are split.",
    )
    parser.add_argument(
        "--prompt",
        default="Write one concise paragraph explaining speculative decoding.",
    )
    parser.add_argument("--max-output-tokens", type=int, default=64)
    parser.add_argument("--n-max", type=int, default=4)
    args = parser.parse_args()

    SHARED.mkdir(parents=True, exist_ok=True)

    state = {"mode": args.mode, "round": 0, "accepted_pos": 0, "done": False}
    config = {
        "mode": args.mode,
        "n_max": args.n_max,
        "max_output_tokens": args.max_output_tokens,
        "prompt": args.prompt,
    }
    if args.mode == "toy":
        target_tokens = ["<BOS>"] + args.target_text.split(" ")
        state["target_tokens"] = target_tokens
        config["target_text"] = args.target_text
    else:
        # In model mode we keep accepted token-ids in verifier vocabulary space.
        # This allows round-to-round token-id exchange instead of whitespace text chunks.
        state["accepted_token_ids"] = []

    write_json(STATE, state)
    write_json(CONFIG, config)
    if PROPOSAL.exists():
        PROPOSAL.unlink()
    if DECISION.exists():
        DECISION.unlink()
    DOCUMENT.write_text("# Shared Document\n\n", encoding="utf-8")

    print(f"Initialized demo (mode={args.mode})")
    if args.mode == "toy":
        print(f"- target tokens: {len(target_tokens) - 1}")
    else:
        print(f"- prompt: {args.prompt}")
        print(f"- max output tokens: {args.max_output_tokens}")
    print(f"- shared dir: {SHARED}")


if __name__ == "__main__":
    main()
