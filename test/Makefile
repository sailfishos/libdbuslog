# -*- Mode: makefile-gmake -*-

all:
%:
	@$(MAKE) -C test_logger $*

clean:
	@$(MAKE) -C test_logger $@
	rm -f coverage/*.gcov
	rm -fr coverage/results
