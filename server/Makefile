# -*- Mode: makefile-gmake -*-

.PHONY: clean distclean all debug release pkgconfig
.PHONY: common_debug common_release
.PHONY: common-install-dev install
.PHONY: dbus-install dbus-install-dev
.PHONY: gio-install gio-install-dev

#
# Required packages
#

PKGS = libdbusaccess libglibutil glib-2.0
GIO_PKGS = gio-2.0 gio-unix-2.0
DBUS_PKGS = gobject-2.0 dbus-1

#
# Default target
#

all: debug release pkgconfig

#
# Sources
#

SRC = \
  dbuslog_core.c \
  dbuslog_sender.c \
  dbuslog_server.c
DBUS_SRC = \
  dbuslog_server_dbus.c
GIO_SRC = \
  dbuslog_server_gio.c
GIO_GEN_SRC = \
  org.nemomobile.Logger.c

#
# Directories
#

SRC_DIR = src
INCLUDE_DIR = include
SPEC_DIR = ../spec
BUILD_DIR = build
GEN_DIR = $(BUILD_DIR)/gen
DEBUG_BUILD_DIR = $(BUILD_DIR)/debug
RELEASE_BUILD_DIR = $(BUILD_DIR)/release

DBUS_INCLUDE_DIR = $(INCLUDE_DIR)/dbus
DBUS_BUILD_DIR = $(BUILD_DIR)/dbus
DBUS_SRC_DIR = $(SRC_DIR)/dbus
DBUS_DEBUG_BUILD_DIR = $(DEBUG_BUILD_DIR)/dbus
DBUS_RELEASE_BUILD_DIR = $(RELEASE_BUILD_DIR)/dbus

GIO_INCLUDE_DIR = $(INCLUDE_DIR)/gio
GIO_BUILD_DIR = $(BUILD_DIR)/gio
GIO_GEN_DIR = $(GEN_DIR)/gio
GIO_SRC_DIR = $(SRC_DIR)/gio
GIO_DEBUG_BUILD_DIR = $(DEBUG_BUILD_DIR)/gio
GIO_RELEASE_BUILD_DIR = $(RELEASE_BUILD_DIR)/gio

#
# Library version
#

VERSION_MAJOR = $(shell head -1 ../VERSION | cut -f1 -d.)
VERSION_MINOR = $(shell head -1 ../VERSION | cut -f2 -d.)
VERSION_RELEASE = $(shell head -1 ../VERSION | cut -f3 -d.)

# Version for pkg-config
PCVERSION = $(VERSION_MAJOR).$(VERSION_MINOR).$(VERSION_RELEASE)

#
# Library name
#

MAP_FILE = libdbuslogserver.map

DBUS_NAME = dbuslogserver-dbus
DBUS_LIB_NAME = lib$(DBUS_NAME)
DBUS_LIB_DEV_SYMLINK = $(DBUS_LIB_NAME).so
DBUS_LIB_SYMLINK1 = $(DBUS_LIB_DEV_SYMLINK).$(VERSION_MAJOR)
DBUS_LIB_SYMLINK2 = $(DBUS_LIB_SYMLINK1).$(VERSION_MINOR)
DBUS_LIB_SONAME = $(DBUS_LIB_SYMLINK1)
DBUS_LIB = $(DBUS_LIB_SONAME).$(VERSION_MINOR).$(VERSION_RELEASE)
DBUS_EXPORTS = $(MAP_FILE)

GIO_NAME = dbuslogserver-gio
GIO_LIB_NAME = lib$(GIO_NAME)
GIO_LIB_DEV_SYMLINK = $(GIO_LIB_NAME).so
GIO_LIB_SYMLINK1 = $(GIO_LIB_DEV_SYMLINK).$(VERSION_MAJOR)
GIO_LIB_SYMLINK2 = $(GIO_LIB_SYMLINK1).$(VERSION_MINOR)
GIO_LIB_SONAME = $(GIO_LIB_SYMLINK1)
GIO_LIB = $(GIO_LIB_SONAME).$(VERSION_MINOR).$(VERSION_RELEASE)
GIO_EXPORTS = $(MAP_FILE)

#
# Common code
#

COMMON_DIR = ../common
COMMON_INCLUDE_DIR = $(COMMON_DIR)/include
COMMON_DEBUG_DIR = $(COMMON_DIR)/build/debug
COMMON_RELEASE_DIR = $(COMMON_DIR)/build/release
COMMON_LIB = dbuslogcommon

DEBUG_LIBS = -L$(COMMON_DEBUG_DIR) -l$(COMMON_LIB)
RELEASE_LIBS = -L$(COMMON_RELEASE_DIR) -l$(COMMON_LIB)

#
# Tools and flags
#

CC = $(CROSS_COMPILE)gcc
LD = $(CC)
WARNINGS = -Wall -Wno-unused-parameter
INCLUDES = -I$(INCLUDE_DIR) -I$(COMMON_INCLUDE_DIR)
DBUS_INCLUDES = -I$(SRC_DIR) -I$(DBUS_INCLUDE_DIR)
GIO_INCLUDES = -I$(SRC_DIR) -I$(GIO_INCLUDE_DIR) -I$(GIO_GEN_DIR)
BASE_FLAGS = -fPIC $(CFLAGS)
FULL_CFLAGS = $(BASE_FLAGS) $(DEFINES) $(WARNINGS) $(INCLUDES) -MMD -MP \
  $(shell pkg-config --cflags $(PKGS))
DBUS_FULL_CFLAGS = $(FULL_CFLAGS) $(DBUS_INCLUDES) \
  $(shell pkg-config --cflags $(DBUS_PKGS))
DBUS_LDFLAGS = $(BASE_FLAGS) -shared -Wl,-soname=$(DBUS_LIB_SONAME) \
  -Wl,--version-script=$(DBUS_EXPORTS) \
  $(shell pkg-config --libs $(DBUS_PKGS) $(PKGS))
GIO_FULL_CFLAGS = $(FULL_CFLAGS) $(GIO_INCLUDES) \
  $(shell pkg-config --cflags $(GIO_PKGS))
GIO_LDFLAGS = $(BASE_FLAGS) -shared -Wl,-soname=$(GIO_LIB_SONAME) \
  -Wl,--version-script=$(GIO_EXPORTS) \
   $(shell pkg-config --libs $(GIO_PKGS) $(PKGS))
DEBUG_FLAGS = -g
RELEASE_FLAGS =

KEEP_SYMBOLS ?= 0
ifneq ($(KEEP_SYMBOLS),0)
RELEASE_FLAGS += -g
endif

DEBUG_CFLAGS = $(FULL_CFLAGS) $(DEBUG_FLAGS) -DDEBUG
RELEASE_CFLAGS = $(FULL_CFLAGS) $(RELEASE_FLAGS) -O2
DBUS_DEBUG_CFLAGS = $(DBUS_FULL_CFLAGS) $(DEBUG_FLAGS)
DBUS_RELEASE_CFLAGS = $(DBUS_FULL_CFLAGS) $(RELEASE_FLAGS)
DBUS_DEBUG_LDFLAGS = $(DBUS_LDFLAGS) $(DEBUG_FLAGS)
DBUS_RELEASE_LDFLAGS = $(DBUS_LDFLAGS) $(RELEASE_FLAGS)
GIO_DEBUG_CFLAGS = $(GIO_FULL_CFLAGS) $(DEBUG_FLAGS)
GIO_RELEASE_CFLAGS = $(GIO_FULL_CFLAGS) $(RELEASE_FLAGS)
GIO_DEBUG_LDFLAGS = $(GIO_LDFLAGS) $(DEBUG_FLAGS)
GIO_RELEASE_LDFLAGS = $(GIO_LDFLAGS) $(RELEASE_FLAGS)

#
# Files
#

DEBUG_OBJS = \
  $(SRC:%.c=$(DEBUG_BUILD_DIR)/%.o)
RELEASE_OBJS = \
  $(SRC:%.c=$(RELEASE_BUILD_DIR)/%.o)

DBUS_PKGCONFIG = \
  $(BUILD_DIR)/$(DBUS_LIB_NAME).pc
DBUS_DEBUG_OBJS = \
  $(DBUS_SRC:%.c=$(DBUS_DEBUG_BUILD_DIR)/%.o)
DBUS_RELEASE_OBJS = \
  $(DBUS_SRC:%.c=$(DBUS_RELEASE_BUILD_DIR)/%.o)

GIO_PKGCONFIG = \
  $(BUILD_DIR)/$(GIO_LIB_NAME).pc
GIO_DEBUG_OBJS = \
  $(GIO_GEN_SRC:%.c=$(GIO_DEBUG_BUILD_DIR)/%.o) \
  $(GIO_SRC:%.c=$(GIO_DEBUG_BUILD_DIR)/%.o)
GIO_RELEASE_OBJS = \
  $(GIO_GEN_SRC:%.c=$(GIO_RELEASE_BUILD_DIR)/%.o) \
  $(GIO_SRC:%.c=$(GIO_RELEASE_BUILD_DIR)/%.o)
GIO_GEN_FILES = $(GIO_GEN_SRC:%=$(GIO_GEN_DIR)/%)

.PRECIOUS: $(GIO_GEN_FILES)

#
# Dependencies
#

DEPS = \
  $(DEBUG_OBJS:%.o=%.d) \
  $(RELEASE_OBJS:%.o=%.d) \
  $(DBUS_DEBUG_OBJS:%.o=%.d) \
  $(DBUS_RELEASE_OBJS:%.o=%.d) \
  $(GIO_DEBUG_OBJS:%.o=%.d) \
  $(GIO_RELEASE_OBJS:%.o=%.d)
ifneq ($(MAKECMDGOALS),clean)
ifneq ($(strip $(DEPS)),)
-include $(DEPS)
endif
endif

$(DEBUG_OBJS): | $(DEBUG_BUILD_DIR)
$(RELEASE_OBJS): | $(RELEASE_BUILD_DIR)

$(DBUS_DEBUG_OBJS): | $(DBUS_DEBUG_BUILD_DIR)
$(DBUS_RELEASE_OBJS): | $(DBUS_RELEASE_BUILD_DIR)
$(DBUS_PKGCONFIG): | $(BUILD_DIR)

$(GIO_GEN_FILES): | $(GIO_GEN_DIR)
$(GIO_DEBUG_OBJS): | $(GIO_DEBUG_BUILD_DIR)
$(GIO_RELEASE_OBJS): | $(GIO_RELEASE_BUILD_DIR)
$(GIO_PKGCONFIG): | $(BUILD_DIR)

#
# Rules
#

DBUS_DEBUG_LIB = $(DBUS_DEBUG_BUILD_DIR)/$(DBUS_LIB)
DBUS_RELEASE_LIB = $(DBUS_RELEASE_BUILD_DIR)/$(DBUS_LIB)
DBUS_DEBUG_LINK = $(DBUS_DEBUG_BUILD_DIR)/$(DBUS_LIB_SONAME)
DBUS_RELEASE_LINK = $(DBUS_RELEASE_BUILD_DIR)/$(DBUS_LIB_SONAME)

GIO_DEBUG_LIB = $(GIO_DEBUG_BUILD_DIR)/$(GIO_LIB)
GIO_RELEASE_LIB = $(GIO_RELEASE_BUILD_DIR)/$(GIO_LIB)
GIO_DEBUG_LINK = $(GIO_DEBUG_BUILD_DIR)/$(GIO_LIB_SONAME)
GIO_RELEASE_LINK = $(GIO_RELEASE_BUILD_DIR)/$(GIO_LIB_SONAME)

debug: dbus-debug gio-debug

release: dbus-release gio-release

pkgconfig: dbus-pkgconfig gio-pkgconfig

dbus-debug: common_debug $(DBUS_DEBUG_LIB) $(DBUS_DEBUG_LINK)

dbus-release: common_release $(DBUS_RELEASE_LIB) $(DBUS_RELEASE_LINK)

dbus-pkgconfig: $(DBUS_PKGCONFIG)

gio-debug: common_debug $(GIO_DEBUG_LIB) $(GIO_DEBUG_LINK)

gio-release: common_release $(GIO_RELEASE_LIB) $(GIO_RELEASE_LINK)

gio-pkgconfig: $(GIO_PKGCONFIG)

distclean: clean

clean:
	rm -f *~ $(SRC_DIR)/*~ $(INCLUDE_DIR)/*~
	rm -fr $(BUILD_DIR)

common_debug:
	make -C $(COMMON_DIR) debug

common_release:
	make -C $(COMMON_DIR) release

$(BUILD_DIR):
	mkdir -p $@

$(DBUS_DEBUG_BUILD_DIR):
	mkdir -p $@

$(DBUS_RELEASE_BUILD_DIR):
	mkdir -p $@

$(GIO_GEN_DIR):
	mkdir -p $@

$(GIO_DEBUG_BUILD_DIR):
	mkdir -p $@

$(GIO_RELEASE_BUILD_DIR):
	mkdir -p $@

$(DEBUG_BUILD_DIR):
	mkdir -p $@

$(RELEASE_BUILD_DIR):
	mkdir -p $@

$(DBUS_DEBUG_BUILD_DIR)/%.o : $(DBUS_SRC_DIR)/%.c
	$(CC) -c $(DBUS_DEBUG_CFLAGS) -MT"$@" -MF"$(@:%.o=%.d)" $< -o $@

$(DBUS_RELEASE_BUILD_DIR)/%.o : $(DBUS_SRC_DIR)/%.c
	$(CC) -c $(DBUS_RELEASE_CFLAGS) -MT"$@" -MF"$(@:%.o=%.d)" $< -o $@

$(GIO_GEN_DIR)/%.c: $(SPEC_DIR)/%.xml
	gdbus-codegen --generate-c-code $(@:%.c=%) $<

$(GIO_DEBUG_BUILD_DIR)/%.o : $(GIO_GEN_DIR)/%.c
	$(CC) -c -I. $(GIO_DEBUG_CFLAGS) -MT"$@" -MF"$(@:%.o=%.d)" $< -o $@

$(GIO_RELEASE_BUILD_DIR)/%.o : $(GIO_GEN_DIR)/%.c
	$(CC) -c -I. $(GIO_RELEASE_CFLAGS) -MT"$@" -MF"$(@:%.o=%.d)" $< -o $@

$(GIO_DEBUG_BUILD_DIR)/%.o : $(GIO_SRC_DIR)/%.c
	$(CC) -c $(GIO_DEBUG_CFLAGS) -MT"$@" -MF"$(@:%.o=%.d)" $< -o $@

$(GIO_RELEASE_BUILD_DIR)/%.o : $(GIO_SRC_DIR)/%.c
	$(CC) -c $(GIO_RELEASE_CFLAGS) -MT"$@" -MF"$(@:%.o=%.d)" $< -o $@

$(DEBUG_BUILD_DIR)/%.o : $(SRC_DIR)/%.c
	$(CC) -c $(DEBUG_CFLAGS) -MT"$@" -MF"$(@:%.o=%.d)" $< -o $@

$(RELEASE_BUILD_DIR)/%.o : $(SRC_DIR)/%.c
	$(CC) -c $(RELEASE_CFLAGS) -MT"$@" -MF"$(@:%.o=%.d)" $< -o $@

$(DBUS_DEBUG_LIB): $(DEBUG_OBJS) $(DBUS_DEBUG_OBJS)
	$(LD) $^ $(DEBUG_LIBS) $(DBUS_DEBUG_LDFLAGS) -o $@

$(DBUS_RELEASE_LIB): $(RELEASE_OBJS) $(DBUS_RELEASE_OBJS)
	$(LD) $^ $(RELEASE_LIBS) $(DBUS_RELEASE_LDFLAGS) -o $@
ifeq ($(KEEP_SYMBOLS),0)
	strip $@
endif

$(DBUS_DEBUG_BUILD_DIR)/$(DBUS_LIB_SYMLINK1): $(DBUS_DEBUG_BUILD_DIR)/$(DBUS_LIB_SYMLINK2)
	ln -sf $(DBUS_LIB_SYMLINK2) $@

$(DBUS_RELEASE_BUILD_DIR)/$(DBUS_LIB_SYMLINK1): $(DBUS_RELEASE_BUILD_DIR)/$(DBUS_LIB_SYMLINK2)
	ln -sf $(DBUS_LIB_SYMLINK2) $@

$(DBUS_DEBUG_BUILD_DIR)/$(DBUS_LIB_SYMLINK2): $(DBUS_DEBUG_LIB)
	ln -sf $(DBUS_LIB) $@

$(DBUS_RELEASE_BUILD_DIR)/$(DBUS_LIB_SYMLINK2): $(DBUS_RELEASE_LIB)
	ln -sf $(DBUS_LIB) $@

$(DBUS_PKGCONFIG): $(DBUS_LIB_NAME).pc.in
	sed -e 's/\[version\]/'$(PCVERSION)/g $< > $@

$(GIO_DEBUG_LIB): $(DEBUG_OBJS) $(GIO_DEBUG_OBJS)
	$(LD) $^ $(DEBUG_LIBS) $(GIO_DEBUG_LDFLAGS) -o $@

$(GIO_RELEASE_LIB): $(RELEASE_OBJS) $(GIO_RELEASE_OBJS)
	$(LD) $^ $(RELEASE_LIBS) $(GIO_RELEASE_LDFLAGS) -o $@
ifeq ($(KEEP_SYMBOLS),0)
	strip $@
endif

$(GIO_DEBUG_BUILD_DIR)/$(GIO_LIB_SYMLINK1): $(GIO_DEBUG_BUILD_DIR)/$(GIO_LIB_SYMLINK2)
	ln -sf $(GIO_LIB_SYMLINK2) $@

$(GIO_RELEASE_BUILD_DIR)/$(GIO_LIB_SYMLINK1): $(GIO_RELEASE_BUILD_DIR)/$(GIO_LIB_SYMLINK2)
	ln -sf $(GIO_LIB_SYMLINK2) $@

$(GIO_DEBUG_BUILD_DIR)/$(GIO_LIB_SYMLINK2): $(GIO_DEBUG_LIB)
	ln -sf $(GIO_LIB) $@

$(GIO_RELEASE_BUILD_DIR)/$(GIO_LIB_SYMLINK2): $(GIO_RELEASE_LIB)
	ln -sf $(GIO_LIB) $@

$(GIO_PKGCONFIG): $(GIO_LIB_NAME).pc.in
	sed -e 's/\[version\]/'$(PCVERSION)/g $< > $@

#
# Install
#

INSTALL = install
INSTALL_DIRS = $(INSTALL) -d
INSTALL_FILES = $(INSTALL) -m 644

INSTALL_LIB_DIR = $(DESTDIR)/usr/lib
INSTALL_PKGCONFIG_DIR = $(DESTDIR)/usr/lib/pkgconfig
INSTALL_INCLUDE_DIR = $(DESTDIR)/usr/include/dbuslogserver
DBUS_INSTALL_INCLUDE_DIR = $(INSTALL_INCLUDE_DIR)/dbus
GIO_INSTALL_INCLUDE_DIR = $(INSTALL_INCLUDE_DIR)/gio

INSTALL_ALIAS = $(INSTALL_LIB_DIR)/$(LIB_SHORTCUT)

install: dbus-install-dev gio-install-dev

common-install-dev: $(INSTALL_INCLUDE_DIR)
	$(INSTALL_FILES) $(INCLUDE_DIR)/*.h $(INSTALL_INCLUDE_DIR)
	$(INSTALL_FILES) $(COMMON_INCLUDE_DIR)/dbuslog_protocol.h $(INSTALL_INCLUDE_DIR)

dbus-install: $(INSTALL_LIB_DIR)
	$(INSTALL_FILES) $(DBUS_RELEASE_LIB) $(INSTALL_LIB_DIR)
	ln -sf $(DBUS_LIB) $(INSTALL_LIB_DIR)/$(DBUS_LIB_SYMLINK2)
	ln -sf $(DBUS_LIB_SYMLINK2) $(INSTALL_LIB_DIR)/$(DBUS_LIB_SYMLINK1)

dbus-install-dev: dbus-install common-install-dev $(DBUS_INSTALL_INCLUDE_DIR) $(INSTALL_PKGCONFIG_DIR)
	$(INSTALL_FILES) $(DBUS_INCLUDE_DIR)/*.h $(DBUS_INSTALL_INCLUDE_DIR)
	$(INSTALL_FILES) $(DBUS_PKGCONFIG) $(INSTALL_PKGCONFIG_DIR)
	ln -sf $(DBUS_LIB_SYMLINK1) $(INSTALL_LIB_DIR)/$(DBUS_LIB_DEV_SYMLINK)

gio-install: $(INSTALL_LIB_DIR)
	$(INSTALL_FILES) $(GIO_RELEASE_LIB) $(INSTALL_LIB_DIR)
	ln -sf $(GIO_LIB) $(INSTALL_LIB_DIR)/$(GIO_LIB_SYMLINK2)
	ln -sf $(GIO_LIB_SYMLINK2) $(INSTALL_LIB_DIR)/$(GIO_LIB_SYMLINK1)

gio-install-dev: gio-install common-install-dev $(GIO_INSTALL_INCLUDE_DIR) $(INSTALL_PKGCONFIG_DIR)
	$(INSTALL_FILES) $(GIO_INCLUDE_DIR)/*.h $(GIO_INSTALL_INCLUDE_DIR)
	$(INSTALL_FILES) $(GIO_PKGCONFIG) $(INSTALL_PKGCONFIG_DIR)
	ln -sf $(GIO_LIB_SYMLINK1) $(INSTALL_LIB_DIR)/$(GIO_LIB_DEV_SYMLINK)

$(INSTALL_LIB_DIR):
	$(INSTALL_DIRS) $@

$(INSTALL_INCLUDE_DIR):
	$(INSTALL_DIRS) $@

$(DBUS_INSTALL_INCLUDE_DIR):
	$(INSTALL_DIRS) $@

$(GIO_INSTALL_INCLUDE_DIR):
	$(INSTALL_DIRS) $@

$(INSTALL_PKGCONFIG_DIR):
	$(INSTALL_DIRS) $@
