# -*- Mode: makefile-gmake -*-

all:
%:
	@$(MAKE) -C client $*
	@$(MAKE) -C server $*

clean:
	@$(MAKE) -C client $@
	@$(MAKE) -C server $@
	@$(MAKE) -C common $@
	rm -f *~ rpm/*~
	rm -fr $(BUILD_DIR) RPMS installroot
	rm -fr debian/tmp
	rm -fr debian/libdbuslogserver debian/libdbuslogserver-dev
	rm -fr debian/libdbuslogclient debian/libdbuslogclient-dev
	rm -f documentation.list debian/files debian/*.substvars
	rm -f debian/*.debhelper.log debian/*.debhelper debian/*~
