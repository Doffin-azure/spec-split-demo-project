# Experiment Index

This file is the stable entry point for experiment history in `reference/spec-split-demo-project`.

## Recording Rule

Every experiment run should record all of the following:

- start timestamp
- end timestamp
- exact mode / binary path
- model pair
- prompt
- key runtime parameters
- summarized result
- pointer to raw timestamped logs

Raw logs should be archived under:

- `experiments/YYYY-MM-DD/`

and should use timestamped filenames so later runs do not overwrite earlier evidence.

## Records

- 2026-04-03: [EXPERIMENT_TIMING_2026-04-03.md](/C:/Users/JXZ/AndroidStudioProjects/MyApplication2/reference/spec-split-demo-project/EXPERIMENT_TIMING_2026-04-03.md)
- 2026-04-07: [EXPERIMENT_TIMING_2026-04-07.md](/C:/Users/JXZ/AndroidStudioProjects/MyApplication2/reference/spec-split-demo-project/EXPERIMENT_TIMING_2026-04-07.md)
- 2026-04-08: [EXPERIMENT_TIMING_2026-04-08.md](/C:/Users/JXZ/AndroidStudioProjects/MyApplication2/reference/spec-split-demo-project/EXPERIMENT_TIMING_2026-04-08.md)

## Android Status

The Android-device split path is now runnable and recorded.

- Earlier `2026-04-07` attempts included an install-policy block with `INSTALL_FAILED_USER_RESTRICTED`.
- A later `2026-04-07` run completed successfully with:
  - Android app as the local draft runtime
  - desktop `llama_cpp_spec_split` service as the verifier
  - timestamped summary and archived raw logs under `experiments/2026-04-07/`
