# ember-gfx build and test.
#
#   make            build the library
#   make test       run every test that the machine supports
#   make demo       build the Ember demo programs (needs the Ember compiler)
#   make arm-check  compile every source for AArch64 (needs the GNU cross compiler)
#   make arm-test   run the headless tests and the Ember self-test on AArch64
#
# Set EMBER_REPO to the path of an Ember checkout (default ../ember).

CC      ?= cc
CFLAGS  ?= -std=c11 -O2 -Wall -Wextra -Werror -Iinclude -Isrc
EMBER_REPO ?= ../ember
EMBER   ?= env PYTHONPATH=$(EMBER_REPO) python3 -m ember

# AArch64 Linux. On an ARM64 host the native compiler and no emulator are used; elsewhere
# the GNU cross compiler and qemu-user run the programs.
ARM_CC      ?= aarch64-linux-gnu-gcc
ARM_AR      ?= aarch64-linux-gnu-ar
ARM_SYSROOT ?= /usr/aarch64-linux-gnu
ifeq ($(shell uname -m),aarch64)
  ARM_RUN :=
else
  ARM_RUN := qemu-aarch64 -L $(ARM_SYSROOT)
endif
ARM_CFLAGS := -std=c11 -O2 -Wall -Wextra -Werror -Iinclude -Isrc
EMBER_ARM := env EMBER_ARM64_CC=$(ARM_CC) PYTHONPATH=$(EMBER_REPO) python3 -m ember

# X11 support is enabled only when the X11 development files are installed.
X11_CFLAGS := $(shell pkg-config --cflags x11 xext xtst 2>/dev/null)
X11_LIBS   := $(shell pkg-config --libs x11 xext xtst 2>/dev/null)
ifneq ($(X11_LIBS),)
  DEFS += -DGFX_HAVE_X11
  X11_SRC := src/gfx_x11.c
  CFLAGS += $(X11_CFLAGS)
endif

CORE_SRC := src/gfx_core.c src/gfx_null.c
BUILD    := build

LIB      := $(BUILD)/libember_gfx.a
CORE_OBJ := $(CORE_SRC:src/%.c=$(BUILD)/%.o)
X11_OBJ  := $(X11_SRC:src/%.c=$(BUILD)/%.o)

all: $(LIB)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: src/%.c src/gfx_internal.h include/ember_gfx.h | $(BUILD)
	$(CC) $(CFLAGS) $(DEFS) -c $< -o $@

$(LIB): $(CORE_OBJ) $(X11_OBJ)
	ar rcs $@ $^

# ---- tests -------------------------------------------------------------------

$(BUILD)/test_core: tests/test_core.c $(CORE_OBJ) $(X11_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) $(DEFS) $< $(CORE_OBJ) $(X11_OBJ) $(X11_LIBS) -lm -o $@

$(BUILD)/test_x11: tests/test_x11.c $(CORE_OBJ) $(X11_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) $(DEFS) $< $(CORE_OBJ) $(X11_OBJ) $(X11_LIBS) -lm -o $@

# Items for `ember build --link`. The `=` form is needed for flags such as -lX11,
# because argparse would otherwise read them as options.
EMBER_LINK := $(foreach item,$(LIB) $(X11_LIBS) -lm -Iinclude $(DEFS),--link=$(item))

# The Ember self-test runs headless, so it needs no display.
$(BUILD)/selftest: demo/selftest.em $(LIB)
	$(EMBER) build demo/selftest.em -o $@ $(EMBER_LINK)

$(BUILD)/animate: demo/animate.em $(LIB)
	$(EMBER) build demo/animate.em -o $@ $(EMBER_LINK)

test: $(BUILD)/test_core $(BUILD)/selftest
	@echo "== test_core (headless reference model)"
	./$(BUILD)/test_core
	@echo "== selftest (Ember program, headless)"
	./$(BUILD)/selftest | diff - demo/selftest.expect && echo "selftest: output matches"
ifdef DISPLAY
	$(MAKE) test-x11 e2e-click
else
	@echo "== X11 tests skipped: no DISPLAY (run under xvfb-run to include them)"
endif

test-x11: $(BUILD)/test_x11
	@echo "== test_x11 (real X server, MIT-SHM path)"
	./$(BUILD)/test_x11
	@echo "== test_x11 (XPutImage fallback)"
	GFX_X11_NO_SHM=1 ./$(BUILD)/test_x11

# Run under: xvfb-run -s "-screen 0 800x600x16" make test-x11-16
test-x11-16: $(BUILD)/test_x11
	@echo "== test_x11 on a 16-bit display (format conversion path)"
	./$(BUILD)/test_x11

demo: $(BUILD)/selftest $(BUILD)/animate

# Mouse test. Linux version: an X11 program, driven by real pointer events in e2e-click.
$(BUILD)/click_test: demo/click_test.em $(LIB)
	$(EMBER) build demo/click_test.em -o $@ $(EMBER_LINK)

$(BUILD)/click_inject: tests/click_inject.c
	$(CC) $(CFLAGS) $< $(X11_LIBS) -o $@

click-test: $(BUILD)/click_test

# Windows version: a GUI .exe with mouse support (cross-compiled with MinGW).
click-test-exe: $(BUILD)/click_test.exe

$(BUILD)/click_test.exe: demo/click_test.em src/gfx_core.c src/gfx_null.c src/gfx_win32.c include/ember_gfx.h | $(BUILD)
	$(EMBER) build demo/click_test.em --target windows -o $@ \
	  --link=src/gfx_core.c --link=src/gfx_null.c --link=src/gfx_win32.c \
	  --link=-Iinclude --link=-Isrc

# Needs a display: run under xvfb-run.
e2e-click: $(BUILD)/click_test $(BUILD)/click_inject
	./tests/e2e_click.sh ./$(BUILD)/click_test ./$(BUILD)/click_inject

# Windows: compile-check the backend and core with MinGW (does not link or run).
win-check:
	x86_64-w64-mingw32-gcc -std=c11 -Wall -Wextra -Werror -Iinclude -Isrc \
	  -c src/gfx_core.c -o /dev/null
	x86_64-w64-mingw32-gcc -std=c11 -Wall -Wextra -Werror -Iinclude -Isrc \
	  -c src/gfx_win32.c -o /dev/null
	x86_64-w64-mingw32-gcc -std=c11 -Wall -Wextra -Werror -Iinclude -Isrc \
	  -c src/gfx_null.c -o /dev/null
	@echo "win-check: Windows backend compiles"

clean:
	rm -rf $(BUILD)

# ---- AArch64 ------------------------------------------------------------------
# The core and headless backend are tested on AArch64. The X11 backend is compile-checked
# only: running it needs AArch64 X11 libraries, which the cross toolchain does not include.

ARM_BUILD    := $(BUILD)/linux-arm64
ARM_CORE_OBJ := $(ARM_BUILD)/gfx_core.o $(ARM_BUILD)/gfx_null.o
ARM_LIB      := $(ARM_BUILD)/libember_gfx.a

$(ARM_BUILD):
	mkdir -p $(ARM_BUILD)

$(ARM_BUILD)/gfx_core.o: src/gfx_core.c src/gfx_internal.h include/ember_gfx.h | $(ARM_BUILD)
	$(ARM_CC) $(ARM_CFLAGS) -c $< -o $@

$(ARM_BUILD)/gfx_null.o: src/gfx_null.c src/gfx_internal.h include/ember_gfx.h | $(ARM_BUILD)
	$(ARM_CC) $(ARM_CFLAGS) -c $< -o $@

$(ARM_LIB): $(ARM_CORE_OBJ)
	$(ARM_AR) rcs $@ $^

arm-check: | $(ARM_BUILD)
	$(ARM_CC) $(ARM_CFLAGS) -c src/gfx_core.c -o /dev/null
	$(ARM_CC) $(ARM_CFLAGS) -c src/gfx_null.c -o /dev/null
ifneq ($(X11_LIBS),)
	$(ARM_CC) $(ARM_CFLAGS) -DGFX_HAVE_X11 $(X11_CFLAGS) -c src/gfx_x11.c -o /dev/null
	@echo "arm-check: AArch64 sources compile (including X11)"
else
	@echo "arm-check: AArch64 sources compile (X11 skipped: no X11 development files)"
endif

$(ARM_BUILD)/test_core: tests/test_core.c $(ARM_CORE_OBJ) | $(ARM_BUILD)
	$(ARM_CC) $(ARM_CFLAGS) $< $(ARM_CORE_OBJ) -lm -o $@

$(ARM_BUILD)/selftest: demo/selftest.em $(ARM_LIB)
	$(EMBER_ARM) build demo/selftest.em --target linux-arm64 -o $@ \
	  --link=$(ARM_LIB) --link=-Iinclude --link=-lm

arm-test: $(ARM_BUILD)/test_core $(ARM_BUILD)/selftest
	@echo "== test_core (AArch64)"
	$(ARM_RUN) ./$(ARM_BUILD)/test_core
	@echo "== selftest (AArch64, Ember --target linux-arm64)"
	$(ARM_RUN) ./$(ARM_BUILD)/selftest | diff - demo/selftest.expect && echo "selftest (AArch64): output matches"

.PHONY: all test test-x11 test-x11-16 demo win-check click-test click-test-exe e2e-click arm-check arm-test clean
