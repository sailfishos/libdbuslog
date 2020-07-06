# -*- Mode: makefile-gmake -*-

.PHONY: clean distclean all debug release

#
# Required packages
#

PKGS = glib-2.0 gio-2.0 gio-unix-2.0 libglibutil

#
# Default target
#

all: debug release

#
# Sources
#

SRC = \
  dbuslog_category.c \
  dbuslog_message.c \
  dbuslog_util.c

#
# Directories
#

SRC_DIR = src
INCLUDE_DIR = include
BUILD_DIR = build
DEBUG_BUILD_DIR = $(BUILD_DIR)/debug
RELEASE_BUILD_DIR = $(BUILD_DIR)/release

#
# Library name
#

NAME = dbuslogcommon
LIB_NAME = lib$(NAME)
LIB = $(LIB_NAME).a

#
# Tools and flags
#

CC = $(CROSS_COMPILE)gcc
LD = $(CC)
WARNINGS = -Wall -Wno-unused-parameter
INCLUDES = -I$(INCLUDE_DIR)
BASE_FLAGS = -fPIC $(CFLAGS)
FULL_CFLAGS = $(BASE_FLAGS) $(DEFINES) $(WARNINGS) $(INCLUDES) -MMD -MP \
  $(shell pkg-config --cflags $(PKGS))
DEBUG_FLAGS = -g
RELEASE_FLAGS =
ARFLAGS = rc

ifndef KEEP_SYMBOLS
KEEP_SYMBOLS = 0
endif

ifneq ($(KEEP_SYMBOLS),0)
RELEASE_FLAGS += -g
endif

DEBUG_CFLAGS = $(FULL_CFLAGS) $(DEBUG_FLAGS) -DDEBUG
RELEASE_CFLAGS = $(FULL_CFLAGS) $(RELEASE_FLAGS) -O2

#
# Files
#

DEBUG_OBJS = $(SRC:%.c=$(DEBUG_BUILD_DIR)/%.o)
RELEASE_OBJS = $(SRC:%.c=$(RELEASE_BUILD_DIR)/%.o)

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

DEBUG_LIB = $(DEBUG_BUILD_DIR)/$(LIB)
RELEASE_LIB = $(RELEASE_BUILD_DIR)/$(LIB)

debug: $(DEBUG_LIB)

release: $(RELEASE_LIB)

disclean: clean

clean:
	rm -f *~ $(SRC_DIR)/*~ $(INCLUDE_DIR)/*~
	rm -fr $(BUILD_DIR)

$(DEBUG_BUILD_DIR):
	mkdir -p $@

$(RELEASE_BUILD_DIR):
	mkdir -p $@

$(DEBUG_BUILD_DIR)/%.o : $(SRC_DIR)/%.c
	$(CC) -c $(DEBUG_CFLAGS) -MT"$@" -MF"$(@:%.o=%.d)" $< -o $@

$(RELEASE_BUILD_DIR)/%.o : $(SRC_DIR)/%.c
	$(CC) -c $(RELEASE_CFLAGS) -MT"$@" -MF"$(@:%.o=%.d)" $< -o $@

$(DEBUG_LIB): $(DEBUG_BUILD_DIR) $(DEBUG_OBJS)
	$(AR) $(ARFLAGS) $@ $(DEBUG_OBJS)
	ranlib $@

$(RELEASE_LIB): $(RELEASE_BUILD_DIR) $(RELEASE_OBJS)
	$(AR) $(ARFLAGS) $@ $(RELEASE_OBJS)
	ranlib $@
