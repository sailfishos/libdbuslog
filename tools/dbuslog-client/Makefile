# -*- Mode: makefile-gmake -*-

.PHONY: clean all debug release
.PHONY: common_debug common_release
.PHONY: client_debug client_release

#
# Executable
#

EXE = dbuslog-client

#
# Sources
#

SRC = main.c

#
# Required packages
#

PKGS = glib-2.0 gio-2.0 gio-unix-2.0 libglibutil

#
# Default target
#

all: debug release

#
# Directories
#

SRC_DIR = .
LIB_DIR = ../..
BUILD_DIR = build
COMMON_DIR = $(LIB_DIR)/common
CLIENT_DIR = $(LIB_DIR)/client
DEBUG_BUILD_DIR = $(BUILD_DIR)/debug
RELEASE_BUILD_DIR = $(BUILD_DIR)/release

#
# Tools and flags
#

CC = $(CROSS_COMPILE)gcc
LD = $(CC)
WARNINGS = -Wall
INCLUDES = -I$(LIB_DIR)/include -I$(LIB_DIR)/src -I$(COMMON_DIR)/include \
  -I$(CLIENT_DIR)/include
BASE_FLAGS = -fPIC
BASE_LDFLAGS = $(BASE_FLAGS) $(LDFLAGS)
BASE_CFLAGS = $(BASE_FLAGS) $(CFLAGS)
FULL_CFLAGS = $(BASE_CFLAGS) $(DEFINES) $(WARNINGS) $(INCLUDES) -MMD -MP \
  $(shell pkg-config --cflags $(PKGS))
FULL_LDFLAGS = $(BASE_LDFLAGS)
LIBS = $(shell pkg-config --libs $(PKGS))
DEBUG_FLAGS = -g
RELEASE_FLAGS =

DEBUG_LDFLAGS = $(FULL_LDFLAGS) $(DEBUG_FLAGS)
RELEASE_LDFLAGS = $(FULL_LDFLAGS) $(RELEASE_FLAGS)
DEBUG_CFLAGS = $(FULL_CFLAGS) $(DEBUG_FLAGS) -DDEBUG
RELEASE_CFLAGS = $(FULL_CFLAGS) $(RELEASE_FLAGS) -O2

#
# Files
#

DEBUG_OBJS = $(SRC:%.c=$(DEBUG_BUILD_DIR)/%.o)
RELEASE_OBJS = $(SRC:%.c=$(RELEASE_BUILD_DIR)/%.o)

#
# Libraries
#

CLIENT_LIB = dbuslogclient
CLIENT_DEBUG_DIR = $(CLIENT_DIR)/build/debug
CLIENT_RELEASE_DIR = $(CLIENT_DIR)/build/release
CLIENT_DEBUG_LIB = $(CLIENT_DEBUG_DIR)/lib$(CLIENT_LIB).a
CLIENT_RELEASE_LIB = $(CLIENT_RELEASE_DIR)/lib$(CLIENT_LIB).a
DEBUG_LIBS += -L$(CLIENT_DEBUG_DIR) -l$(CLIENT_LIB)
RELEASE_LIBS += -L$(CLIENT_RELEASE_DIR) -l$(CLIENT_LIB)
DEBUG_DEPS += $(CLIENT_DEBUG_LIB)
RELEASE_DEPS += $(CLIENT_RELEASE_LIB)

COMMON_LIB = dbuslogcommon
COMMON_DEBUG_DIR = $(COMMON_DIR)/build/debug
COMMON_RELEASE_DIR = $(COMMON_DIR)/build/release
COMMON_DEBUG_LIB = $(COMMON_DEBUG_DIR)/lib$(COMMON_LIB).a
COMMON_RELEASE_LIB = $(COMMON_RELEASE_DIR)/lib$(COMMON_LIB).a
DEBUG_LIBS += -L$(COMMON_DEBUG_DIR) -l$(COMMON_LIB)
RELEASE_LIBS += -L$(COMMON_RELEASE_DIR) -l$(COMMON_LIB)
DEBUG_DEPS += $(COMMON_DEBUG_LIB)
RELEASE_DEPS += $(COMMON_RELEASE_LIB)

#
# Dependencies
#

DEPS = $(DEBUG_OBJS:%.o=%.d) $(RELEASE_OBJS:%.o=%.d)
ifneq ($(MAKECMDGOALS),clean)
ifneq ($(strip $(DEPS)),)
-include $(DEPS)
endif
endif

$(DEBUG_OBJS): | $(DEBUG_BUILD_DIR)
$(RELEASE_OBJS): | $(RELEASE_BUILD_DIR)

#
# Rules
#

DEBUG_EXE = $(DEBUG_BUILD_DIR)/$(EXE)
RELEASE_EXE = $(RELEASE_BUILD_DIR)/$(EXE)

debug: common_debug client_debug $(DEBUG_EXE)

release: common_release client_release $(RELEASE_EXE)

clean:
	rm -f *~
	rm -fr $(BUILD_DIR)

common_debug:
	make -C $(COMMON_DIR) debug

common_release:
	make -C $(COMMON_DIR) release

client_debug:
	make -C $(CLIENT_DIR) debug

client_release:
	make -C $(CLIENT_DIR) release

$(DEBUG_BUILD_DIR):
	mkdir -p $@

$(RELEASE_BUILD_DIR):
	mkdir -p $@

$(DEBUG_BUILD_DIR)/%.o : $(SRC_DIR)/%.c
	$(CC) -c $(DEBUG_CFLAGS) -MT"$@" -MF"$(@:%.o=%.d)" $< -o $@

$(RELEASE_BUILD_DIR)/%.o : $(SRC_DIR)/%.c
	$(CC) -c $(RELEASE_CFLAGS) -MT"$@" -MF"$(@:%.o=%.d)" $< -o $@

$(DEBUG_EXE): $(DEBUG_OBJS) $(DEBUG_DEPS)
	$(LD) $(DEBUG_LDFLAGS) $(DEBUG_OBJS) $(DEBUG_LIBS) $(LIBS) -o $@

$(RELEASE_EXE): $(RELEASE_OBJS) $(RELEASE_DEPS)
	$(LD) $(RELEASE_LDFLAGS) $(RELEASE_OBJS) $(RELEASE_LIBS) $(LIBS) -o $@
ifeq ($(KEEP_SYMBOLS),0)
	strip $@
endif
