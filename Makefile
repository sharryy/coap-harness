# Repo-level convenience targets.
.PHONY: clean clean-logs

# Remove diff-testing run artifacts: logs + KLEE output dirs.
clean: clean-logs
	rm -rf libcoap/klee-* freecoap/klee-* differential/klee-*

clean-logs:
	rm -f *.log differential/*.log
