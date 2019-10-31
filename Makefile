#
# TyrQuake Makefile (tested under Linux and MinGW/Msys)
#
# By default, all executables will be built and placed in the ./bin
# subdirectory. If you want to build just one, type e.g. "make bin/tyr-quake".
#
# ============================================================================
# User configurable options here:
# ============================================================================

BUILD_DIR        ?= build
BIN_DIR          ?= bin
DOC_DIR          ?= doc
DIST_DIR         ?= dist
DEBUG            ?= N# Compile with debug info
OPTIMIZED_CFLAGS ?= Y# Enable compiler optimisations (if DEBUG != Y)
#USE_X86_ASM      ?= $(I386_GUESS)
USE_X86_ASM		 := N
USE_SDL          ?= Y# New (experimental) SDL video/sound/input targets
SYMBOLS_ON	     ?= N
LOCALBASE        ?= /usr/local
QBASEDIR         ?= .# Default basedir for quake data files (Linux/BSD only)
TARGET_OS        ?= i686-pc-serenity
TARGET_UNIX      ?= $(if $(filter UNIX,$(TARGET_OS)),$(HOST_UNIX),)

# ============================================================================

.PHONY:	default clean docs

# ============================================================================

TYR_RELEASE := v0.65
TYR_GIT := $(shell git describe --dirty 2> /dev/null)
TYR_VERSION := $(if $(TYR_GIT),$(TYR_GIT),$(TYR_RELEASE))
TYR_VERSION_NUM ?= $(patsubst v%,%,$(TYR_VERSION))

# Ensure the build directory exists
$(shell if [ ! -d "$(BUILD_DIR)" ]; then mkdir -p "$(BUILD_DIR)"; fi)

# Create/update the build version file
# Any source files which use TYR_VERSION will depend on this
BUILD_VERSION_FILE = $(BUILD_DIR)/.build_version
$(shell \
	if [ ! -f "$(BUILD_VERSION_FILE)" ] || \
	   [ "`cat $(BUILD_VERSION_FILE)`" != "$(TYR_VERSION)" ]; then \
		printf '%s' "$(TYR_VERSION)" > "$(BUILD_VERSION_FILE)"; \
	fi)

# Create/update the build version time file
# Any source files which use TYR_VERSION_TIME will depend on this
TYR_VERSION_TIME := $(shell \
	if [ -n "$(TYR_GIT)" ] && [ -z "$$(git status -s -uno)" ]; then \
		git show -s --pretty='format:%at'; \
	else \
		date +%s; \
	fi)
BUILD_VERSION_TIME_FILE = $(BUILD_DIR)/.build_version_time
$(shell \
	if [ ! -f "$(BUILD_VERSION_TIME_FILE)" ] || \
	   [ "`cat $(BUILD_VERSION_TIME_FILE)`" != "$(TYR_VERSION_TIME)" ]; then \
		printf '%s' "$(TYR_VERSION_TIME)" > "$(BUILD_VERSION_TIME_FILE)"; \
	fi)

# ---------------------------------------
# Attempt detection of the build host OS
# ---------------------------------------

SYSNAME := $(shell uname -s)
TOPDIR := $(shell pwd)

ifneq (,$(findstring MINGW32,$(SYSNAME)))
HOST_OS = WIN32
else
ifneq (,$(findstring MINGW64,$(SYSNAME)))
HOST_OS = WIN64
else
ifneq (,$(findstring $(SYSNAME),FreeBSD NetBSD))
HOST_OS = UNIX
HOST_UNIX = bsd
else
ifneq (,$(findstring $(SYSNAME),OpenBSD))
HOST_OS = UNIX
HOST_UNIX = openbsd
else
ifneq (,$(findstring $(SYSNAME),Darwin))
HOST_OS = UNIX
HOST_UNIX = darwin
OSX_VERSION_MAJOR := $(shell sw_vers -productVersion | cut -d. -f1)
OSX_VERSION_MINOR := $(shell sw_vers -productVersion | cut -d. -f2)
else
ifneq (,$(findstring $(SYSNAME),Linux))
HOST_OS = UNIX
HOST_UNIX = linux
else
$(error OS type not detected.)
endif
endif
endif
endif
endif
endif

# --------------------------------------------------------------------
# Setup driver options, choosing sensible defaults based on target OS
# --------------------------------------------------------------------

# USE_SDL -> shortcut to select all SDL targets
ifeq ($(USE_SDL),Y)
VID_TARGET ?= sdl
IN_TARGET ?= sdl
SND_TARGET ?= sdl
endif

ifeq ($(TARGET_OS),UNIX)
EXT =
ifeq ($(TARGET_UNIX),darwin)
VID_TARGET ?= sdl
IN_TARGET ?= sdl
SND_TARGET ?= sdl
CD_TARGET ?= null
SNAPSHOT_TARGET = $(DIST_DIR)/tyrquake-$(TYR_VERSION_NUM)-osx.dmg
else
# All other unix can default to X11
VID_TARGET ?= x11
IN_TARGET ?= x11
endif
ifeq ($(TARGET_UNIX),bsd)
CD_TARGET ?= bsd
SND_TARGET ?= oss
endif
ifeq ($(TARGET_UNIX),openbsd)
CD_TARGET ?= bsd
SND_TARGET ?= sndio
endif
ifeq ($(TARGET_UNIX),linux)
CD_TARGET ?= linux
SND_TARGET ?= oss
endif
endif

ifneq (,$(findstring $(TARGET_OS),WIN32 WIN64))
EXT = .exe
CD_TARGET ?= win
VID_TARGET ?= win
IN_TARGET ?= win
SND_TARGET ?= win
ifneq (,$(findstring $(TARGET_OS),WIN64))
SNAPSHOT_TARGET = $(DIST_DIR)/tyrquake-$(TYR_VERSION_NUM)-win64.zip
else
SNAPSHOT_TARGET = $(DIST_DIR)/tyrquake-$(TYR_VERSION_NUM)-win32.zip
endif
endif

# --------------------------------------------------------------
# Executable file extension and possible cross-compiler options
# --------------------------------------------------------------

ifeq ($(TARGET_OS),WIN32)
EXT = .exe
ifneq ($(HOST_OS),WIN32)
TARGET ?= $(MINGW32_CROSS_GUESS)
CC = $(TARGET)-g++
STRIP = $(TARGET)-strip
WINDRES = $(TARGET)-windres
endif
else
ifeq ($(TARGET_OS),WIN64)
EXT=.exe
ifneq ($(HOST_OS),WIN64)
TARGET ?= $(MINGW64_CROSS_GUESS)
CC = $(TARGET)-g++
STRIP = $(TARGET)-strip
WINDRES = $(TARGET)-windres
endif
else
CC = i686-pc-serenity-gcc
EXT=
endif
endif

# For generating html/text documentation
GROFF ?= groff

# Now that defaults are set, if we are using SDL set the CFLAGS/LFLAGS
ifneq (,$(filter sdl,$(VID_TARGET) $(SND_TARGET) $(IN_TARGET)))
SDL_CFLAGS_DEFAULT := $(shell sdl2-config --cflags)
SDL_LFLAGS_DEFAULT := $(shell sdl2-config --libs)
SDL_CFLAGS ?= $(SDL_CFLAGS_DEFAULT)
SDL_LFLAGS := -L../../SDL2/SDL-master-serenity -lm -lc -lgui -lcore -ldraw -lSDL2
endif

# ============================================================================
# Helper functions
# ============================================================================

# ---------------------------------------------------
# Remove duplicates from a list, preserving ordering
# ---------------------------------------------------
# (I wonder if Make optimises the tail recursion here?)

filter-dups = $(if $(1),$(firstword $(1)) $(call filter-dups,$(filter-out $(firstword $(1)),$(1))),)

# ----------------------------------------------
# Try to guess the location of X11 includes/libs
# ----------------------------------------------

# $(1) - header file to search for
# $(2) - library name to search for
# $(3) - list of directories to search in
IGNORE_DIRS = /usr $(LOCALBASE)
find-localbase = $(shell \
	for DIR in $(IGNORE_DIRS); do \
            if [ -e $$DIR/include/$(1) ] && \
		[ -e $$DIR/lib/lib$(2).a ] || \
		[ -e $$DIR/lib/lib$(2).la ] || \
		[ -e $$DIR/lib/lib$(2).dylib ]; then exit 0; fi; \
	done; \
	for DIR in $(3); do \
            if [ -e $$DIR/include/$(1) ] && \
		[ -e $$DIR/lib/lib$(2).a ] || \
		[ -e $$DIR/lib/lib$(2).la ] || \
		[ -e $$DIR/lib/lib$(2).dylib ]; then echo $$DIR; exit 0; fi; \
	done )

X11DIRS = /usr/X11R7 /usr/local/X11R7 /usr/X11R6 /usr/local/X11R6 /opt/X11 /opt/local
X11BASE_GUESS := $(call find-localbase,X11/Xlib.h,X11,$(X11DIRS))
X11BASE ?= $(X11BASE_GUESS)

OGLDIRS = /opt/local /opt/X11
OGLBASE_GUESS := $(call find-localbase,GL/GL.h,GL,$(OGLDIRS))
OGLBASE ?= $(OGLBASE_GUESS)

# ------------------------------------------------------------------------
# Try to guess the MinGW cross compiler executables
# - I've seen i386-mingw32msvc, i586-mingw32msvc (Debian) and now
#   i486-mingw32 (Arch).
# ------------------------------------------------------------------------

MINGW32_CROSS_GUESS := $(shell \
	if which i486-mingw32-gcc > /dev/null 2>&1; then \
		echo i486-mingw32; \
	elif which i586-mingw32msvc-gcc > /dev/null 2>&1; then \
		echo i586-mingw32msvc; \
	else \
		echo i386-mingw32msvc; \
	fi)

MINGW64_CROSS_GUESS := $(shell \
	if which x86_64-w64-mingw32-gcc > /dev/null 2>&1; then \
		echo x86_64-w64-mingw32; \
	else \
		echo x86_64-w64-mingw32; \
	fi)

# --------------------------------
# GCC version and option checking
# --------------------------------

cc-version = $(shell sh $(TOPDIR)/scripts/gcc-version \
              $(if $(1), $(1), $(CC)))

cc-option = $(shell if $(CC) $(CFLAGS) -Werror $(1) -S -o /dev/null -xc /dev/null \
             > /dev/null 2>&1; then echo "$(1)"; else echo "$(2)"; fi ;)

cc-i386 = $(if $(subst __i386,,$(shell echo __i386 | $(CC) -E -xc - | tail -n 1)),Y,N)

libdir-check = $(shell if [ -d "$(1)" ]; then printf -- '-L%s' "$(1)"; fi)

GCC_VERSION := $(call cc-version,)
I386_GUESS  := $(call cc-i386)

# -------------------------
# Special include/lib dirs
# -------------------------
DX_INC    = $(TOPDIR)/wine-dx

# ---------------------------------------
# Define some build variables
# ---------------------------------------

STRIP   ?= strip
WINDRES ?= windres

CFLAGS ?=
CFLAGS := $(CFLAGS) -Wall -Wno-trigraphs -Wwrite-strings
CFLAGS += $(call cc-option,-std=c++17)
CFLAGS += -fno-exceptions

ifeq ($(G4),Y)
# Playing with PPC OSX GCC Flags
CFLAGS += -mcpu=7450 -mtune=7450

# This stuff is enabled by Apple's "-fast" option, but you can't
# override any of the options if you use "-fast" and some are
# problematic (see below)
CFLAGS += -O3
CFLAGS += -falign-loops-max-skip=15
CFLAGS += -falign-jumps-max-skip=15
CFLAGS += -falign-loops=16
CFLAGS += -falign-jumps=16
CFLAGS += -falign-functions=16
CFLAGS += -malign-natural
CFLAGS += -ffast-math
CFLAGS += -fstrict-aliasing
CFLAGS += -funroll-loops
#CFLAGS += -ftree-loop-linear # This one generates bad code, at least in Mod_CalculateAliasModelBounds()
#CFLAGS += -ftree-loop-memset # This one ICE's on Con_DrawInput()
CFLAGS += -fsched-interblock
CFLAGS += -fgcse-sm

# Not part of "-fast"
CFLAGS += -ftree-vectorize
endif

ifeq ($(DEBUG),Y)
CFLAGS += -g
else
ifeq ($(OPTIMIZED_CFLAGS),Y)
CFLAGS += -O2
# -funit-at-a-time is buggy for MinGW GCC > 3.2
# I'm assuming it's fixed for MinGW GCC >= 4.0 when that comes about
CFLAGS += $(shell if [ $(GCC_VERSION) -lt 0400 ] ;\
		then echo $(call cc-option,-fno-unit-at-a-time,); fi ;)
CFLAGS += $(call cc-option,-fweb,)
CFLAGS += $(call cc-option,-frename-registers,)
# Enable some math optimisations, but not -funsafe-math-optimisations,
# which is causing problems with the software renderer currently.
CFLAGS += $(call cc-option,-fno-math-errno)
CFLAGS += $(call cc-option,-ffinite-math-only)
CFLAGS += $(call cc-option,-fno-signaling-nans)
endif
endif

# --------------------------------------------------------------------------
#  Each binary needs to build it's own object files in separate directories
#  due to the {NQ,QW}_HACK ifdefs still present in the common files.
# --------------------------------------------------------------------------

# (sw = software renderer, gl = OpenGL renderer, sv = server)
NQSWDIR	= $(BUILD_DIR)/nqsw
NQGLDIR	= $(BUILD_DIR)/nqgl
QWSWDIR	= $(BUILD_DIR)/qwsw
QWGLDIR	= $(BUILD_DIR)/qwgl
QWSVDIR	= $(BUILD_DIR)/qwsv

APPS =	quake$(EXT) 
		#glquake$(EXT) \
		#qwcl$(EXT) glqwcl$(EXT) \
		#qwsv$(EXT)

default:	all

all:	$(patsubst %,$(BIN_DIR)/%,$(APPS))

# To make warnings more obvious, be less verbose as default
# Use 'make V=1' to see the full commands
ifdef V
  quiet =
else
  quiet = quiet_
endif

quiet_cmd_mkdir = '  MKDIR    $(@D)'
      cmd_mkdir = mkdir -p $(@D)

define do_mkdir
	@if [ ! -d $(@D) ]; then \
		echo $($(quiet)cmd_mkdir); \
		$(cmd_mkdir); \
	fi
endef

quiet_cmd_cp = '  CP       $@'
      cmd_cp = cp $< $@

define do_cp
	$(do_mkdir)
	@echo $($(quiet)cmd_cp)
	@$(cmd_cp)
endef

quiet_cmd_rsync = '  RSYNC    $@'
      cmd_rsync = rsync $(1)

define do_rsync
	$(do_mkdir)
	@echo $(call $(quiet)cmd_rsync,$(1))
	@$(call cmd_rsync,$(1))
endef

# cmd_fixdep => Turn all pre-requisites into targets with no commands, to
# prevent errors when moving files around between builds (e.g. from NQ or QW
# dirs to the common dir.)
# Add dependency on build version time if needed.
cmd_fixdep = \
	cp $(@D)/.$(@F).d $(@D)/.$(@F).d.tmp && \
	sed -e 's/\#.*//' -e 's/^[^:]*: *//' -e 's/ *\\$$//' -e '/^$$/ d' \
	    -e 's/$$/ :/' < $(@D)/.$(@F).d.tmp >> $(@D)/.$(@F).d && \
	rm -f $(@D)/.$(@F).d.tmp

cmd_cc_dep_c = \
	$(CC) -MM -MT $@ $(CPPFLAGS) -o $(@D)/.$(@F).d $< && \
	$(cmd_fixdep)

quiet_cmd_cc_o_c = '  CC       $@'
      cmd_cc_o_c = $(CC) -c $(CPPFLAGS) $(CFLAGS) -o $@ $<

define do_cc_o_c
	@$(do_mkdir);
	@echo $($(quiet)cmd_cc_o_c);
	@$(cmd_cc_dep_c);
	@$(cmd_cc_o_c);
endef

cmd_cc_dep_m = $(cmd_cc_dep_c)

quiet_cmd_cc_o_m = '  CC       $@'
      cmd_cc_o_m = $(CC) -c $(CPPFLAGS) $(CFLAGS) -o $@ $<

define do_cc_o_m
	@$(do_mkdir);
	@echo $($(quiet)cmd_cc_o_m);
	@$(cmd_cc_dep_m);
	@$(cmd_cc_o_m);
endef

cmd_cc_dep_rc = \
	$(CC) -x c-header -MM -MT $@ $(CPPFLAGS) -o $(@D)/.$(@F).d $< ; \
	$(cmd_fixdep)

quiet_cmd_windres_res_rc = '  WINDRES  $@'
      cmd_windres_res_rc = $(WINDRES) -I $(<D) -I ./build/icons -i $< -O coff -o $@

define do_windres_res_rc
	@$(do_mkdir);
	@$(cmd_cc_dep_rc);
	@echo $($(quiet)cmd_windres_res_rc);
	@$(cmd_windres_res_rc);
endef

quiet_cmd_cc_link = '  LINK     $@'
      cmd_cc_link = i686-pc-serenity-g++ -o $@ $^ $(1)

define do_cc_link
	@$(do_mkdir);
	@echo $($(quiet)cmd_cc_link);
	@$(call cmd_cc_link,$(1))
endef

quiet_cmd_strip = '  STRIP    $(1)'
      cmd_strip = $(STRIP) $(1)

ifeq ($(SYMBOLS_ON),Y)
do_strip=
else
ifeq ($(SYMBOLS_ON),)
do_strip=
else
define do_strip
	@echo $(call $(quiet)cmd_strip,$(1));
	@$(call cmd_strip,$(1));
endef
endif
endif

git_date = $(shell git log -1 --date=short --format="%ad" -- $< 2>/dev/null)
doc_version = $(git_date) $(TYR_VERSION)

quiet_cmd_man2man = '  MAN2MAN  $@'
      cmd_man2man = \
	sed -e 's/TYR_VERSION/$(doc_version)/' < $< > $(@D)/.$(@F).tmp && \
	mv $(@D)/.$(@F).tmp $@

define do_man2man
	@$(do_mkdir)
	@echo $(if $(quiet),$(quiet_cmd_man2man),"$(cmd_man2man)");
	@$(cmd_man2man);
endef

# The sed/awk magic is a little ugly, but I wanted something that
# would work across Linux/BSD/Msys/Darwin
quiet_cmd_man2txt = '  MAN2TXT  $@'
      cmd_man2txt = \
	$(GROFF) -man -Tascii $< | cat -v | \
	sed -e 's/\^\[\[\([0-9]\)\{1,2\}[a-z]//g' \
	    -e 's/.\^H//g' | \
	awk '{ gsub("$$", "\r"); print $$0;}' - > $(@D)/.$(@F).tmp && \
	mv $(@D)/.$(@F).tmp $@
 loud_cmd_man2txt = \
	$(subst ",\",$(subst $$,\$$,$(subst "\r","\\r",$(cmd_man2txt))))

define do_man2txt
	@$(do_mkdir)
	@echo $(if $(quiet),$(quiet_cmd_man2txt),"$(loud_cmd_man2txt)");
	@$(cmd_man2txt);
endef

quiet_cmd_man2html = '  MAN2HTML $@'
      cmd_man2html = \
	$(GROFF) -man -Thtml $< > $(@D)/.$(@F).tmp && \
	mv $(@D)/.$(@F).tmp $@

define do_man2html
	@$(do_mkdir)
	@echo $(if $(quiet),$(quiet_cmd_man2html),"$(cmd_man2html)");
	@$(cmd_man2html);
endef

# cmd_zip ~ takes a leading path to be stripped from the archive members
#           (basically simulates tar's -C option).
# $(1) - leading path to strip from files
NOTHING:=
SPACE:=$(NOTHING) $(NOTHING)
quiet_cmd_zip = '  ZIP      $@'
      cmd_zip = ( \
	cd $(1) && \
	zip -q $(subst $(SPACE),/,$(patsubst %,..,$(subst /, ,$(1))))/$@ \
		$(patsubst $(1)/%,%,$^) )

# $@ - the archive file
# $^ - files to be added
# $(1) - leading path to strip from files
define do_zip
	@$(do_mkdir)
	@echo $(if $(quiet),$(quiet_cmd_zip),"$(call cmd_zip,$(1))")
	@$(call cmd_zip,$(1))
endef

DEPFILES := \
	$(wildcard \
		$(BUILD_DIR)/*/.*.d \
		$(BUILD_DIR)/*/*/.*.d \
	)

ifneq ($(DEPFILES),)
-include $(DEPFILES)
endif

install:
	mkdir -p $(DESTDIR)/bin
	cp bin/quake $(DESTDIR)/bin
	mkdir -p $(DESTDIR)/res/icons/16x16
	cp quake.png $(DESTDIR)/res/icons/16x16/

# ----------------------------------------------------------------------------
# Build rule generation
# ----------------------------------------------------------------------------
# Because I want to do nasty things like build for multiple architectures
# with a single make invocation (and not resort to recursion) I generate the
# rules for building using defines.

#  define rulesets for building object files
#  $(1) - the build directory
#  $(2) - CPPFLAGS
#  $(3) - CFLAGS
define NQCL_RULES
$(1)/buildinfo.o: $(BUILD_VERSION_FILE) $(BUILD_VERSION_TIME_FILE)
$(1)/%.o:	CPPFLAGS = $(2)
$(1)/%.o:	CFLAGS += $(3)
$(1)/%.o:	common/%.S	; $$(do_cc_o_c)
$(1)/%.o:	NQ/%.S		; $$(do_cc_o_c)
$(1)/%.o:	common/%.c	; $$(do_cc_o_c)
$(1)/%.o:	NQ/%.c		; $$(do_cc_o_c)
$(1)/%.res:	common/%.rc	; $$(do_windres_res_rc)
$(1)/%.res:	NQ/%.rc	$(BUILD_DIR)/icons/tyrquake.ico	; $$(do_windres_res_rc)
endef
define QWCL_RULES
$(1)/buildinfo.o: $(BUILD_VERSION_FILE) $(BUILD_VERSION_TIME_FILE)
$(1)/%.o:	CPPFLAGS = $(2)
$(1)/%.o:	CFLAGS += $(3)
$(1)/%.o:	common/%.S	; $$(do_cc_o_c)
$(1)/%.o:	QW/client/%.S	; $$(do_cc_o_c)
$(1)/%.o:	QW/common/%.S	; $$(do_cc_o_c)
$(1)/%.o:	common/%.c	; $$(do_cc_o_c)
$(1)/%.o:	QW/client/%.c	; $$(do_cc_o_c)
$(1)/%.o:	QW/common/%.c	; $$(do_cc_o_c)
$(1)/%.res:	common/%.rc	; $$(do_windres_res_rc)
$(1)/%.res:	QW/client/%.rc $(BUILD_DIR)/icons/tyrquake.ico	; $$(do_windres_res_rc)
endef
define QWSV_RULES
$(1)/buildinfo.o: $(BUILD_VERSION_FILE) $(BUILD_VERSION_TIME_FILE)
$(1)/%.o:	CPPFLAGS = $(2)
$(1)/%.o:	CFLAGS += $(3)
$(1)/%.o:	QW/server/%.S	; $$(do_cc_o_c)
$(1)/%.o:	QW/common/%.S	; $$(do_cc_o_c)
$(1)/%.o:	common/%.S	; $$(do_cc_o_c)
$(1)/%.o:	QW/server/%.c	; $$(do_cc_o_c)
$(1)/%.o:	QW/common/%.c	; $$(do_cc_o_c)
$(1)/%.o:	common/%.c	; $$(do_cc_o_c)
endef

# Another level of indirection
# Generate five sets of rules for the five target build directories
# $(1) - build sub-directory
# $(2) - extra cflags for arch
define TARGET_RULES
$(eval $(call NQCL_RULES,$$(BUILD_DIR)/$(1)/nqsw,$$(ALL_NQSW_CPPFLAGS),$(2)))
$(eval $(call NQCL_RULES,$$(BUILD_DIR)/$(1)/nqgl,$$(ALL_NQGL_CPPFLAGS),$(2)))
$(eval $(call QWCL_RULES,$$(BUILD_DIR)/$(1)/qwsw,$$(ALL_QWSW_CPPFLAGS),$(2)))
$(eval $(call QWCL_RULES,$$(BUILD_DIR)/$(1)/qwgl,$$(ALL_QWGL_CPPFLAGS),$(2)))
$(eval $(call QWSV_RULES,$$(BUILD_DIR)/$(1)/qwsv,$$(ALL_QWSV_CPPFLAGS),$(2)))
endef

# Standard build rules
$(eval $(call NQCL_RULES,$$(BUILD_DIR)/nqsw,$$(ALL_NQSW_CPPFLAGS),))
$(eval $(call NQCL_RULES,$$(BUILD_DIR)/nqgl,$$(ALL_NQGL_CPPFLAGS),))
$(eval $(call QWCL_RULES,$$(BUILD_DIR)/qwsw,$$(ALL_QWSW_CPPFLAGS),))
$(eval $(call QWCL_RULES,$$(BUILD_DIR)/qwgl,$$(ALL_QWGL_CPPFLAGS),))
$(eval $(call QWSV_RULES,$$(BUILD_DIR)/qwsv,$$(ALL_QWSV_CPPFLAGS),))

ifeq ($(OSX_VERSION_MAJOR).$(OSX_VERSION_MINOR),10.5)

# On OSX 10.5 we build universal binaries for four archs
# NOTE: This requires an older or patched version of SDL2 for now
$(eval $(call TARGET_RULES,osx-intel-32,-arch i386 -mmacosx-version-min=10.5))
$(eval $(call TARGET_RULES,osx-intel-64,-arch x86_64 -mmacosx-version-min=10.5))
$(eval $(call TARGET_RULES,osx-ppc-32,-arch ppc -mmacosx-version-min=10.5))
$(eval $(call TARGET_RULES,osx-ppc-64,-arch ppc64 -mmacosx-version-min=10.5))

else

# On modern OSX, we only build for intel-64
#  (support for everything else is now removed from the XCode libraries/utilties!)
$(eval $(call TARGET_RULES,osx-intel-64,-arch x86_64 -mmacosx-version-min=10.6))

endif

# Win32 and Win64 builds ?? - cross compiler...
$(eval $(call TARGET_RULES,win32,))
$(eval $(call TARGET_RULES,win64,))

# ----------------------------------
# Output the build Options: Output
# ----------------------------------
$(info Compile Options:)
$(info .        DEBUG = $(DEBUG))
$(info .    TARGET_OS = $(TARGET_OS))
$(info .  TARGET_UNIX = $(TARGET_UNIX))
$(info .  USE_X86_ASM = $(USE_X86_ASM))
$(info .    CD_TARGET = $(CD_TARGET))
$(info .   SND_TARGET = $(SND_TARGET))
$(info .   VID_TARGET = $(VID_TARGET))
$(info .    IN_TARGET = $(IN_TARGET))

# ============================================================================
# Object Files, libraries and options
# ============================================================================
#
# Provide a set of makefile variables to which we can attach lists of object
# files, libraries to link against, preprocessor and linker options, etc. The
# prefixes tell us to which targets the variables apply:
#
# Shared lists:
#  COMMON_ - objects common to all five targets
#  CL_     - objects common to the four client targets (NQ, QW, SW & GL)
#  SV_     - objects common to the three server targets (NQ SW&GL, QWSV)
#  NQCL_   - objects common to the NQ client targets (SW & GL)
#  QWCL_   - objects common to the QW client targets (SW & GL)
#  QW_     - objects common to the QW targets (CL & SV)
#  SW_     - objects common to the software rendering clients
#  GL_     - objects common to the OpenGL rendering clients
#
# Target specific lists:
#  NQSW_
#  NQGL_
#  QWSW_
#  QWGL_
#  QWSV_
#
# The suffix describes where the list is used
#  _OBJS     - list of object files used as dependency and for linker
#  _LIBS     - list of libs to pass to the linker
#  _CPPFLAGS - C preprocessor flags, e.g. include directories or defines
#  _LFLAGS   - linker flags, e.g. library directories
#
# Then we have configuration options which will alter the content of the lists:
# - VID_TARGET  - video driver
# - IN_TARGET   - input driver (usually tied to video driver)
# - CD_TARGET   - cd audio driver
# - SND_TARGET  - sound driver
# - USE_X86_ASM - conditionally replace C code with assembly
#
#
# 1. Set up lists of object files which aren't affected by config options
# 2. Go through the various config options and append to the appropriate lists
#
#
# TODO: Think about different groupings?
#       e.g. SW_RENDER_OBJS, GL_RENDER_OBJS, NET_DRIVER_OBJS...
#
# --------------------------------------------------------------------------
# Object File Lists - Static wrt. config options
# --------------------------------------------------------------------------

COMMON_OBJS := \
	buildinfo.o	\
	cmd.o		\
	common.o	\
	crc.o		\
	cvar.o		\
	mathlib.o	\
	model.o		\
	rb_tree.o	\
	shell.o		\
	sys_serenity.o  \
	zone.o

CL_OBJS := \
	alias_model.o	\
	cd_common.o	\
	cd_null.o \
	cl_demo.o	\
	cl_input.o	\
	cl_main.o	\
	cl_parse.o	\
	cl_tent.o	\
	console.o	\
	developer.o	\
	keys.o		\
	menu.o		\
	pcx.o		\
	r_lerp.o	\
	r_efrag.o	\
	r_light.o	\
	r_model.o	\
	r_part.o	\
	sbar.o		\
	screen.o	\
	snd_dma.o	\
	snd_mem.o	\
	snd_mix.o	\
	sprite_model.o	\
	vid_mode.o	\
	view.o		\
	wad.o

SV_OBJS := \
	pr_cmds.o	\
	pr_edict.o	\
	pr_exec.o	\
	sv_main.o	\
	sv_move.o	\
	sv_phys.o	\
	sv_user.o

NQCL_OBJS := \
	chase.o		\
	host.o		\
	host_cmd.o	\
	net_common.o	\
	net_none.o \
	net_dgrm.o	\
	net_loop.o	\
	net_main.o	\
	world.o

QWCL_OBJS := \
	cl_cam.o	\
	cl_ents.o	\
	cl_pred.o	\
	skin.o

QW_OBJS := \
	md4.o		\
	net_chan.o	\
	pmove.o		\
	pmovetst.o

SW_OBJS := \
	d_edge.o	\
	d_fill.o	\
	d_init.o	\
	d_modech.o	\
	d_part.o	\
	d_polyse.o	\
	d_scan.o	\
	d_sky.o		\
	d_sprite.o	\
	d_surf.o	\
	d_vars.o	\
	draw.o		\
	r_aclip.o	\
	r_alias.o	\
	r_bsp.o		\
	r_draw.o	\
	r_edge.o	\
	r_main.o	\
	r_misc.o	\
	r_sky.o		\
	r_sprite.o	\
	r_surf.o	\
	r_vars.o

GL_OBJS := \
	drawhulls.o	\
	gl_draw.o	\
	gl_extensions.o	\
	gl_fog.o	\
	gl_mesh.o	\
	gl_model.o	\
	gl_rmain.o	\
	gl_rmisc.o	\
	gl_rsurf.o	\
	gl_sky.o	\
	gl_textures.o	\
	gl_warp.o	\
	qpic.o		\
	tga.o

NQSW_OBJS :=

NQGL_OBJS :=

QWSW_OBJS :=

QWGL_OBJS := \
	gl_ngraph.o

QWSV_OBJS := \
	sv_ccmds.o	\
	sv_ents.o	\
	sv_init.o	\
	sv_nchan.o	\
	sv_send.o	\
	world.o

# ----------------------------------------------------------------------------
# Start off the CPPFLAGS, config independent stuff
# ----------------------------------------------------------------------------

# Defines
COMMON_CPPFLAGS += -DTYR_VERSION=$(TYR_VERSION_NUM)
COMMON_CPPFLAGS += -DTYR_VERSION_TIME=$(TYR_VERSION_TIME)LL
COMMON_CPPFLAGS += -DQBASEDIR="$(QBASEDIR)"
NQCL_CPPFLAGS   += -DNQ_HACK
QW_CPPFLAGS     += -DQW_HACK
QWSV_CPPFLAGS   += -DSERVERONLY
GL_CPPFLAGS     += -DGLQUAKE
ifeq ($(DEBUG),Y)
COMMON_CPPFLAGS += -DDEBUG
else
COMMON_CPPFLAGS += -DNDEBUG
endif

# Includes
COMMON_CPPFLAGS += -iquote $(TOPDIR)/include
COMMON_CPPFLAGS += $(if $(LOCALBASE),-idirafter $(LOCALBASE)/include,)
COMMON_LFLAGS   += $(if $(LOCALBASE),$(call libdir-check,$(LOCALBASE)/lib,),)
NQCL_CPPFLAGS   += -iquote $(TOPDIR)/NQ
QW_CPPFLAGS     += -iquote $(TOPDIR)/QW/client
QWSV_CPPFLAGS   += -iquote $(TOPDIR)/QW/server

# If we are on OSX (Darwin) and this is not the GLX target, we will link with
# "-framework OpenGL" instead. Otherwise, link with libGL as normal
# => if ($TARGET_OS == UNIX && $TARGET_UNIX == darwin && $VID_TARGET != x11)...
ifeq ($(filter-out UNIX,$(TARGET_OS))$(filter-out darwin,$(TARGET_UNIX))$(filter x11,$(VID_TARGET)),)
GL_CPPFLAGS     += -DAPPLE_OPENGL
GL_CPPFLAGS     += -DGL_SILENCE_DEPRECATION
GL_LFLAGS       += -framework OpenGL
else
GL_CPPFLAGS     += $(if $(OGLBASE),-idirafter $(OGLBASE)/include,)
GL_LFLAGS       += $(if $(OGLBASE),$(call libdir-check,$(OGLBASE)/lib,),)
endif

# ----------------------------------------------------------------------------
# Add objects depending whether using x86 assembly
# ----------------------------------------------------------------------------

ifeq ($(USE_X86_ASM),Y)
COMMON_CPPFLAGS += -DUSE_X86_ASM
COMMON_OBJS += sys_wina.o math.o
CL_OBJS   += snd_mixa.o
COMMON_OBJS += modela.o
SW_OBJS   += d_draw.o d_draw16.o d_parta.o d_polysa.o d_scana.o d_spr8.o \
	     d_varsa.o r_aclipa.o r_aliasa.o r_drawa.o r_edgea.o r_varsa.o \
	     surf8.o surf16.o
else
SW_OBJS += nonintel.o
endif

# ----------------------------------------------------------------------------
# Quick sanity check to make sure the lists have no overlap
# ----------------------------------------------------------------------------
dups-only = $(if $(1),$(if $(filter $(firstword $(1)),$(wordlist 2,$(words $(1)),$(1))),$(firstword $(1)),) $(call dups-only,$(filter-out $(firstword $(1)),$(1))),)
ALL_OBJS := $(COMMON_OBJS) $(CL_OBJS) $(SV_OBJS) $(NQCL_OBJS) $(QWCL_OBJS) \
	    $(QW_OBJS) $(SW_OBJS) $(GL_OBJS) $(NQSW_OBJS) $(NQGL_OBJS) \
	    $(QWSW_OBJS) $(QWGL_OBJS) $(QWSV_OBJS))
MSG_DUP = WARNING: Duplicate words detected in group
DUPS := $(strip $(call dups-only,$(ALL_OBJS)))
DUMMY := $(if $(DUPS),$(warning $(MSG_DUP): $(DUPS)),)

# ----------------------------------------------------------------------------
# Target OS Options
# ----------------------------------------------------------------------------

ifneq (,$(findstring $(TARGET_OS),WIN32 WIN64))
COMMON_CPPFLAGS += -DWIN32_LEAN_AND_MEAN
COMMON_OBJS += net_wins.o sys_win.o
CL_OBJS     += winquake.res
NQCL_OBJS   += conproc.o net_win.o
COMMON_LIBS += ws2_32 winmm dxguid
GL_LIBS     += opengl32
ifeq ($(DEBUG),Y)
CL_LFLAGS += -mconsole
else
CL_LFLAGS += -mwindows
endif
QWSV_LFLAGS += -mconsole
endif
ifeq ($(TARGET_OS),UNIX)
COMMON_CPPFLAGS += -DELF
COMMON_OBJS += net_udp.o sys_unix.o
COMMON_LIBS += m
NQCL_OBJS   += net_bsd.o

# FIXME - stupid hack
ifeq ($(APP_BUNDLE),Y)
COMMON_CPPFLAGS += -Dmain=SDL_main
endif

# If we are on OSX (Darwin) and this is not the GLX target, we will link with
# "-framework OpenGL" instead. Otherwise, link with libGL as normal
# => if ($TARGET_UNIX != darwin || $VID_TARGET == x11)...
ifneq ($(filter-out darwin,$(TARGET_UNIX))$(filter x11,$(VID_TARGET)),)
GL_LIBS     += GL
endif
endif

# ----------------------------------------------------------------------------
# Driver Options
#   NOTE: there is some duplication that may happen here, e.g. adding common
#         libs/objs multiple times. We will strip out duplicates later.
# ----------------------------------------------------------------------------

# ----------------
# 1. Video driver
# ----------------

ifeq ($(VID_TARGET),x11)
CL_CPPFLAGS += -DX11
CL_OBJS += x11_core.o
SW_OBJS += vid_x.o
GL_OBJS += vid_glx.o
CL_LIBS += X11 Xext Xxf86vm
ifneq ($(X11BASE),)
CL_CPPFLAGS += -idirafter $(X11BASE)/include
CL_LFLAGS += $(call libdir-check,$(X11BASE)/lib)
endif
endif
ifeq ($(VID_TARGET),win)
CL_CPPFLAGS += -idirafter $(DX_INC)
SW_OBJS += vid_win.o
GL_OBJS += vid_wgl.o
CL_LIBS += gdi32
GL_LIBS += comctl32
endif
ifeq ($(VID_TARGET),sdl)
SW_OBJS += vid_sdl.o sdl_common.o
GL_OBJS += vid_sgl.o sdl_common.o
CL_CPPFLAGS += $(SDL_CFLAGS)
CL_LFLAGS += $(SDL_LFLAGS)
endif

# ----------------
# 2. Input driver
# ----------------
# TODO: is it worth allowing input and video to be specified separately?
#       they can be pretty tightly bound...

ifeq ($(IN_TARGET),x11)
CL_OBJS += x11_core.o in_x11.o
CL_LIBS += X11
ifneq ($(X11BASE),)
CL_LFLAGS += $(call libdir-check,$(X11BASE)/lib)
endif
endif
ifeq ($(IN_TARGET),win)
CL_OBJS += in_win.o
endif
ifeq ($(IN_TARGET),sdl)
CL_OBJS += in_sdl.o sdl_common.o
endif

# ----------------
# 3. CD driver
# ----------------

ifeq ($(CD_TARGET),null)
CL_OBJS += cd_null.o
endif
ifeq ($(CD_TARGET),linux)
CL_OBJS += cd_linux.o
endif
ifeq ($(CD_TARGET),bsd)
CL_OBJS += cd_bsd.o
endif
ifeq ($(CD_TARGET),win)
CL_OBJS += cd_win.o
endif

# ----------------
# 4. Sound driver
# ----------------

ifeq ($(SND_TARGET),null)
CL_OBJS += snd_null.o
endif
ifeq ($(SND_TARGET),win)
CL_CPPFLAGS += -idirafter $(DX_INC)
CL_OBJS += snd_win.o
# FIXME - direct sound libs?
endif
ifeq ($(SND_TARGET),oss)
CL_OBJS += snd_oss.o
endif
ifeq ($(SND_TARGET),sndio)
CL_OBJS += snd_sndio.o
CL_LIBS += sndio
endif
ifeq ($(SND_TARGET),sdl)
CL_OBJS += snd_sdl.o sdl_common.o
CL_CPPFLAGS += $(SDL_CFLAGS)
CL_LFLAGS += $(SDL_LFLAGS)
endif

# ----------------------------------------------------------------------------
# Combining the lists
# ----------------------------------------------------------------------------

nqsw-list = $(COMMON_$(1)) $(CL_$(1)) $(SV_$(1)) $(NQCL_$(1)) $(SW_$(1)) $(NQSW_$(1))
nqgl-list = $(COMMON_$(1)) $(CL_$(1)) $(SV_$(1)) $(NQCL_$(1)) $(GL_$(1)) $(NQGL_$(1))
qwsw-list = $(COMMON_$(1)) $(CL_$(1)) $(QW_$(1)) $(QWCL_$(1)) $(SW_$(1)) $(QWSW_$(1))
qwgl-list = $(COMMON_$(1)) $(CL_$(1)) $(QW_$(1)) $(QWCL_$(1)) $(GL_$(1)) $(QWGL_$(1))
qwsv-list = $(COMMON_$(1)) $(SV_$(1)) $(QW_$(1)) $(QWSV_$(1))

ALL_NQSW_CPPFLAGS := $(call nqsw-list,CPPFLAGS)
ALL_NQGL_CPPFLAGS := $(call nqgl-list,CPPFLAGS)
ALL_QWSW_CPPFLAGS := $(call qwsw-list,CPPFLAGS)
ALL_QWGL_CPPFLAGS := $(call qwgl-list,CPPFLAGS)
ALL_QWSV_CPPFLAGS := $(call qwsv-list,CPPFLAGS)

ALL_NQSW_OBJS := $(sort $(call nqsw-list,OBJS))
ALL_NQGL_OBJS := $(sort $(call nqgl-list,OBJS))
ALL_QWSW_OBJS := $(sort $(call qwsw-list,OBJS))
ALL_QWGL_OBJS := $(sort $(call qwgl-list,OBJS))
ALL_QWSV_OBJS := $(sort $(call qwsv-list,OBJS))

ALL_NQSW_LIBS := $(call filter-dups,$(call nqsw-list,LIBS))
ALL_NQGL_LIBS := $(call filter-dups,$(call nqgl-list,LIBS))
ALL_QWSW_LIBS := $(call filter-dups,$(call qwsw-list,LIBS))
ALL_QWGL_LIBS := $(call filter-dups,$(call qwgl-list,LIBS))
ALL_QWSV_LIBS := $(call filter-dups,$(call qwsv-list,LIBS))

ALL_NQSW_LFLAGS := $(call nqsw-list,LFLAGS)
ALL_NQGL_LFLAGS := $(call nqgl-list,LFLAGS)
ALL_QWSW_LFLAGS := $(call qwsw-list,LFLAGS)
ALL_QWGL_LFLAGS := $(call qwgl-list,LFLAGS)
ALL_QWSV_LFLAGS := $(call qwsv-list,LFLAGS)

ALL_NQSW_LFLAGS += $(patsubst %,-l%,$(ALL_NQSW_LIBS))
ALL_NQGL_LFLAGS += $(patsubst %,-l%,$(ALL_NQGL_LIBS))
ALL_QWSW_LFLAGS += $(patsubst %,-l%,$(ALL_QWSW_LIBS))
ALL_QWGL_LFLAGS += $(patsubst %,-l%,$(ALL_QWGL_LIBS))
ALL_QWSV_LFLAGS += $(patsubst %,-l%,$(ALL_QWSV_LIBS))

# ============================================================================
# Build Rules
# ============================================================================

$(BIN_DIR)/quake$(EXT):	$(patsubst %,$(NQSWDIR)/%,$(ALL_NQSW_OBJS))
	$(call do_cc_link,$(ALL_NQSW_LFLAGS))
	$(call do_strip,$@)

# Build man pages, text and html docs from source
$(DOC_DIR)/%.6:		man/%.6	$(BUILD_VERSION_FILE)	; $(do_man2man)
$(DOC_DIR)/%.txt:	$(DOC_DIR)/%.6		; $(do_man2txt)
$(DOC_DIR)/%.html:	$(DOC_DIR)/%.6		; $(do_man2html)

# ----------------------------------------------------------------------------
# Documentation
# ----------------------------------------------------------------------------
SRC_DOCS = tyrquake.6
MAN_DOCS  = $(patsubst %.6,$(DOC_DIR)/%.6,$(SRC_DOCS))
HTML_DOCS = $(patsubst %.6,$(DOC_DIR)/%.html,$(SRC_DOCS))
TEXT_DOCS = $(patsubst %.6,$(DOC_DIR)/%.txt,$(SRC_DOCS))

docs:	$(MAN_DOCS) $(HTML_DOCS) $(TEXT_DOCS)

# ----------------------------------------------------------------------------
# Very basic clean target
# ----------------------------------------------------------------------------

# Main clean function...
clean:
	@rm -rf $(BUILD_DIR)
	@rm -rf $(BIN_DIR)
	@rm -rf $(DOC_DIR)
	@rm -rf $(DIST_DIR)
	@find . \( -name '*~' -o -name '#*#' -o -name '*.o' -o -name '*.res' \) -exec rm -f {} +

# ----------------------------------------------------------------------------
# Windows ICO file
# ----------------------------------------------------------------------------

quiet_cmd_resize = '  RESIZE   $@'
      cmd_resize = convert $< -resize $(1)x$(2) -background white $@

define do_resize
	$(do_mkdir)
	@echo $(call $(quiet)cmd_resize,$(1),$(2))
	@$(call cmd_resize,$(1),$(2))
endef

quiet_cmd_ico = '  ICO      $@'
      cmd_ico = convert $^ $@

define do_ico
	$(do_mkdir)
	@echo $($(quiet)cmd_ico)
	@$(cmd_ico)
endef

$(BUILD_DIR)/icons/tyrquake_%.png: icons/tyrquake-1024x1024.png; $(call do_resize,$*,$*)
$(BUILD_DIR)/icons/tyrquake_%.bmp: icons/tyrquake-1024x1024.png; $(call do_resize,$*,$*)

WINDOWS_ICON_SOURCE_FILES = \
	$(BUILD_DIR)/icons/tyrquake_256.png \
	$(BUILD_DIR)/icons/tyrquake_48.bmp  \
	$(BUILD_DIR)/icons/tyrquake_32.bmp  \
	$(BUILD_DIR)/icons/tyrquake_16.bmp

.SECONDARY: $(WINDOWS_ICON_SOURCE_FILES)

$(BUILD_DIR)/icons/tyrquake.ico: $(WINDOWS_ICON_SOURCE_FILES)
	$(do_ico)

# ----------------------------------------------------------------------------
# OSX Packaging Tools (WIP)
# ----------------------------------------------------------------------------

PLIST    ?= /usr/libexec/PlistBuddy
IBTOOL   ?= ibtool
ICONUTIL ?= iconutil
LIPO     ?= lipo
HDIUTIL  ?= hdiutil

quiet_cmd_plist_set = '  PLIST    $@ $(1)'
      cmd_plist_set = $(PLIST) -c "Set :$(1) $(2)" $(@D)/.$(@F).tmp

define do_plist_set
	@echo $(call $(quiet)cmd_plist_set,$(1),$(2));
	@$(call cmd_plist_set,$(1),$(2))
endef

quiet_cmd_ibtool_nib = '  IBTOOL   $@'
      cmd_ibtool_nib = $(IBTOOL) --compile $@ $(<D)

# NOTE: ibtool seems to launch an agent process to do it's compiling and this
#       agent hangs around after ibtool has finished.  Unfortunately make
#       detects that there is still a sub-process running, so doesn't exit
#       until the agent times out (30-seconds).  The timeout used to be
#       settable on the command line, but that option has been removed now.
#       Oh well, just be patient then...
define do_ibtool_nib
	$(do_mkdir)
	@echo $($(quiet)cmd_ibtool_nib)
	@$(cmd_ibtool_nib)
endef

quiet_cmd_iconutil_icns = '  ICONUTIL $@'
      cmd_iconutil_icns = $(ICONUTIL) -c icns -o $@ $(<D)

define do_iconutil_icns
	$(do_mkdir)
	@echo $($(quiet)cmd_iconutil_icns)
	@$(cmd_iconutil_icns)
endef

quiet_cmd_hdiutil = '  HDIUTIL  $@'
      cmd_hdiutil = $(HDIUTIL) create $@ -srcfolder $(1) -ov -volname $(2)

define do_hdiutil
	$(do_mkdir)
	@echo $(call $(quiet)cmd_hdiutil,$(1),$(2))
	@$(call cmd_hdiutil,$(1),$(2)) $(if $(quiet),>/dev/null,)
endef

quiet_cmd_lipo = '  LIPO     $@'
      cmd_lipo = lipo -create $^ -output $@

define do_lipo
	$(do_mkdir)
	@echo $($(quiet)cmd_lipo)
	@$(cmd_lipo)
endef

quiet_cmd_sips = '  SIPS     $@'
      cmd_sips = sips -z $(1) $(1) $< --out $@ >/dev/null

define do_sips
	$(do_mkdir)
	@echo $(call $(quiet)cmd_sips,$(1))
	@$(call cmd_sips,$(1))
endef
