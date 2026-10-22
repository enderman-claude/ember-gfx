# ember-gfx build and test.
#
#   make            build the library
#   make test       run every test that the machine supports
#   make demo       build the Ember demo programs (needs the Ember compiler)
#
# Set EMBER_REPO to the path of an Ember checkout (default ../ember).

CC      ?= cc
CFLAGS  ?= -std=c11 -O2 -Wall -Wextra -Werror -Iinclude -Isrc
EMBER_REPO ?= ../ember
EMBER   ?= env PYTHONPATH=$(EMBER_REPO) python3 -m ember

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

.PHONY: all test test-x11 test-x11-16 demo win-check click-test click-test-exe e2e-click clean
