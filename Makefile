# -*- Mode: makefile-gmake -*-

all:
%:
	@$(MAKE) -C client $*
	@$(MAKE) -C server $*

check:
	@$(MAKE) -C test test

clean:
	@$(MAKE) -C client $@
	@$(MAKE) -C server $@
	@$(MAKE) -C common $@
	rm -f *~ rpm/*~
	rm -fr $(BUILD_DIR) RPMS installroot
	rm -fr debian/tmp
	rm -fr debian/dbuslog-tools debian/libdbuslogserver-common-dev
	rm -fr debian/libdbuslogserver-dbus debian/libdbuslogserver-dbus-dev
	rm -fr debian/libdbuslogserver-gio debian/libdbuslogserver-gio-dev
	rm -f documentation.list debian/files debian/*.substvars
	rm -f debian/*.debhelper.log debian/*.debhelper debian/*~
