SHELL := /bin/bash

.PHONY: setup run stop logs init

setup:
	./setup_env.sh

init:
	. .venv/bin/activate && python init_demo.py

run:
	./run_demo.sh

stop:
	./clean_demo.sh

logs:
	@echo "verify log:"
	@tail -n 50 shared/verify.log || true
	@echo ""
	@echo "draft log:"
	@tail -n 50 shared/draft.log || true
