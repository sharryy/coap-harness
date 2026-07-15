# Repo-level convenience targets.
.PHONY: clean clean-logs

# Remove diff-testing run artifacts: logs + KLEE output dirs.
clean: clean-logs
	rm -rf klee-diff-* klee-dbg-* klee-exp-* klee-*-check
	rm -rf libcoap-harness/klee-* freecoap-harness/klee-*

clean-logs:
	rm -f *.log
