# -*- Mode: makefile-gmake -*-

LIBDIR ?= usr/lib

all:
%:
	@$(MAKE) -C client $*
	@$(MAKE) -C server $*

debian/%.install: debian/%.install.in
	sed 's|@LIBDIR@|$(LIBDIR)|g' $< > $@

check:
	@$(MAKE) -C test test

clean:
	@$(MAKE) -C client $@
	@$(MAKE) -C server $@
	@$(MAKE) -C common $@
	@$(MAKE) -C tools/dbuslog-client $@
	rm -f *~ rpm/*~
	rm -fr $(BUILD_DIR) RPMS installroot
	rm -fr debian/tmp
	rm -fr debian/dbuslog-tools debian/libdbuslogserver-common-dev
	rm -fr debian/libdbuslogserver-dbus debian/libdbuslogserver-dbus-dev
	rm -fr debian/libdbuslogserver-dbus.install
	rm -fr debian/libdbuslogserver-dbus-dev.install
	rm -fr debian/libdbuslogserver-gio debian/libdbuslogserver-gio-dev
	rm -fr debian/libdbuslogserver-gio.install
	rm -fr debian/libdbuslogserver-gio-dev.install
	rm -f documentation.list debian/files debian/*.substvars
	rm -f debian/*.debhelper.log debian/*.debhelper debian/*~
