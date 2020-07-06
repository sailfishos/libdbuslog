# -*- Mode: makefile-gmake -*-

all:
%:
	@$(MAKE) -C test_logger $*
	@$(MAKE) -C test_util $@

clean: distclean
	rm -f coverage/*.gcov
	rm -fr coverage/results
