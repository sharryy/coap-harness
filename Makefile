.PHONY: clean clean-logs

# Remove run artifacts: logs + KLEE output dirs.
clean: clean-logs
	rm -rf libcoap/klee-* freecoap/klee-* differential/klee-* cantcoap/cc-exp-*

clean-logs:
	rm -f *.log differential/*.log
