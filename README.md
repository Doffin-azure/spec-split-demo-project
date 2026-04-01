# Spec Split Demo (File Bus)

This demo shows how to separate speculative drafting and verification into two independent processes.

- `draft_side.py` only proposes draft tokens and writes a proposal file.
- `verify_side.py` reads the proposal, verifies it against a target stream, appends accepted output to a document, and writes a decision file.

Communication is done through shared files:

- `shared/state.json`
- `shared/proposal.json`
- `shared/decision.json`
- `shared/document.md`

## Project layout

- `init_demo.py`: initialize shared state and target stream
- `draft_side.py`: draft worker (proposal producer)
- `verify_side.py`: verify worker (accept/reject + document writer)
- `shared/`: file-bus directory

## 1) Setup

```bash
cd spec-split-demo-project
./setup_env.sh
source .venv/bin/activate
```

## 2) Run verifier (terminal A)

```bash
python3 verify_side.py
```

## 3) Run drafter (terminal B)

```bash
python3 draft_side.py
```

The two processes will advance round by round until `done=true`.

## One-command run

```bash
./run_demo.sh
tail -f shared/verify.log
tail -f shared/draft.log
./clean_demo.sh
```

## GitHub publish

```bash
git init
git add .
git commit -m "feat: initial spec split demo"
# then add your own remote:
# git remote add origin https://github.com/<you>/spec-split-demo-project.git
# git push -u origin main
```

## Notes

- This is a protocol demo, not real model inference.
- The verifier simulates `sample_and_accept_n` behavior:
  - accept matching draft prefix
  - stop at first mismatch and emit target token
  - if full draft matches, emit one extra target token
- `document.md` acts as the shared "manuscript" written by verifier and readable by drafter/others.
