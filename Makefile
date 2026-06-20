CC ?= cc
AR ?= ar
PKG_CONFIG ?= pkg-config

CPPFLAGS ?=
CPPFLAGS += -D_POSIX_C_SOURCE=200809L -D_DARWIN_C_SOURCE
CFLAGS ?= -O2 -g
WARNINGS = -std=c11 -Wall -Wextra -Wpedantic -Werror
INCLUDES = -Iinclude -Ilibtui -Isrc
NCURSES_CFLAGS := $(shell $(PKG_CONFIG) --cflags ncursesw 2>/dev/null || $(PKG_CONFIG) --cflags ncurses 2>/dev/null)
NCURSES_LIBS := $(shell $(PKG_CONFIG) --libs ncursesw 2>/dev/null || $(PKG_CONFIG) --libs ncurses 2>/dev/null || echo -lncurses)

BUILD = build
LIBTUI_SOURCES = libtui/tui.c libtui/widgets.c libtui/render.c libtui/ncurses_backend.c
LIBTUI_OBJECTS = $(LIBTUI_SOURCES:%.c=$(BUILD)/%.o)
GIT_SOURCES = src/process.c src/git.c src/git_cleanup.c src/discovery.c
GIT_OBJECTS = $(GIT_SOURCES:%.c=$(BUILD)/%.o)
APP_SOURCES = src/main.c src/app_ui.c src/worktree_cleanup.c
APP_OBJECTS = $(APP_SOURCES:%.c=$(BUILD)/%.o)

.PHONY: all clean test asan

all: gitroam tui-demo

libtui.a: $(LIBTUI_OBJECTS)
	$(AR) rcs $@ $^

gitroam: libtui.a $(GIT_OBJECTS) $(APP_OBJECTS)
	$(CC) $(CFLAGS) $(WARNINGS) -o $@ $(APP_OBJECTS) $(GIT_OBJECTS) libtui.a $(NCURSES_LIBS)

tui-demo: libtui.a $(BUILD)/examples/tui_demo.o
	$(CC) $(CFLAGS) $(WARNINGS) -o $@ $(BUILD)/examples/tui_demo.o libtui.a $(NCURSES_LIBS)

$(BUILD)/libtui/%.o: libtui/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) $(INCLUDES) $(NCURSES_CFLAGS) -c $< -o $@

$(BUILD)/src/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) $(INCLUDES) -c $< -o $@

$(BUILD)/examples/%.o: examples/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) $(INCLUDES) -c $< -o $@

$(BUILD)/tests/%.o: tests/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) $(INCLUDES) -c $< -o $@

test: all test-tui test-git test-cleanup
	./test-tui
	./test-git
	./test-cleanup

test-tui: libtui.a $(BUILD)/tests/test_tui.o
	$(CC) $(CFLAGS) $(WARNINGS) -o $@ $(BUILD)/tests/test_tui.o \
		libtui.a $(NCURSES_LIBS)

test-git: $(GIT_OBJECTS) $(BUILD)/tests/test_git.o
	$(CC) $(CFLAGS) $(WARNINGS) -o $@ $(BUILD)/tests/test_git.o $(GIT_OBJECTS)

test-cleanup: $(GIT_OBJECTS) $(BUILD)/tests/test_cleanup.o
	$(CC) $(CFLAGS) $(WARNINGS) -o $@ $(BUILD)/tests/test_cleanup.o $(GIT_OBJECTS)

asan:
	$(MAKE) clean
	$(MAKE) CFLAGS="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer" test

clean:
	rm -rf $(BUILD) libtui.a gitroam tui-demo test-tui test-git test-cleanup
