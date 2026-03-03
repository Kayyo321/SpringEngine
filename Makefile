CC ?= cc
CFLAGS ?= -Wall -Wextra -Wpedantic -Werror -std=c11
CPPFLAGS ?=
LDFLAGS ?=

CPPFLAGS += -D_POSIX_C_SOURCE=200809L

UNAME_S := $(shell uname -s)

SRCDIR := src
LIBDIR := lib
OBJDIR := obj
BINDIR := bin
TARGET ?= $(BINDIR)/springengine
DEBUG_TARGET := $(BINDIR)/springengine-debug
TEST_TARGET := $(BINDIR)/springengine-test
RUN_PROJECT ?= ./example-project/
LSAN_SUPPRESSIONS_FILE ?= .lsan-suppressions.txt

BUILD_OBJDIR := $(OBJDIR)

ifneq (,$(filter debug,$(MAKECMDGOALS)))
CPPFLAGS += -DDebug
CFLAGS += -O0 -g -fsanitize=address -fno-omit-frame-pointer
LDFLAGS += -fsanitize=address
TARGET := $(DEBUG_TARGET)
BUILD_OBJDIR := $(OBJDIR)/debug
endif

LUA_DIR := $(LIBDIR)/lua-5.4.6
LUA_INCLUDE_DIR := $(LUA_DIR)/src
LUA_STATIC_LIB := $(LUA_DIR)/lib/liblua.a
TOMLC17_DIR := $(LIBDIR)/tomlc17
TOMLC17_INCLUDE_DIR := $(TOMLC17_DIR)/include
TOMLC17_STATIC_LIB := $(TOMLC17_DIR)/lib/libtomlc17.a

ALL_SRC := $(shell find $(SRCDIR) -type f -name '*.c')
ROOT_SRC := $(wildcard tarheader.c)
TEST_SRC := $(shell find $(SRCDIR)/testing -type f -name '*.c' 2>/dev/null)
SRC := $(filter-out $(TEST_SRC),$(ALL_SRC) $(ROOT_SRC))
ifneq (,$(filter test,$(MAKECMDGOALS)))
SRC += $(TEST_SRC)
TARGET := $(TEST_TARGET)
BUILD_OBJDIR := $(OBJDIR)/test
endif
OBJ_SRC := $(patsubst $(SRCDIR)/%.c,$(BUILD_OBJDIR)/%.o,$(filter $(SRCDIR)/%,$(SRC)))
OBJ_ROOT := $(patsubst %.c,$(BUILD_OBJDIR)/%.o,$(filter-out $(SRCDIR)/%,$(SRC)))
OBJ := $(OBJ_SRC) $(OBJ_ROOT)
DEP := $(OBJ:.o=.d)

SRC_INCLUDE_DIRS := $(shell find $(SRCDIR) -type d 2>/dev/null)
LIB_INCLUDE_DIRS := \
	$(LIBDIR)/raylib/src \
	$(LIBDIR)/lua-5.4.6/src \
	$(LIBDIR)/tomlc17/include

CPPFLAGS += $(addprefix -I,$(SRC_INCLUDE_DIRS))
CPPFLAGS += $(addprefix -I,$(LIB_INCLUDE_DIRS))

ifeq ($(UNAME_S),Linux)
RAYLIB_CFLAGS := $(shell pkg-config --cflags raylib 2>/dev/null)
RAYLIB_LIBS := $(shell pkg-config --libs raylib 2>/dev/null)
SDL2_LIBS := $(shell pkg-config --libs sdl2 2>/dev/null)
CPPFLAGS += $(RAYLIB_CFLAGS)
endif

ifeq ($(wildcard $(TOMLC17_DIR)/include/tomlc17.h),)
$(error Missing tomlc17 headers at $(TOMLC17_DIR)/include/tomlc17.h. Ensure lib/tomlc17 is present.)
endif

LIB_FILES := $(shell \
	find $(LIBDIR) -type f \( -name '*.a' -o -name '*.dylib' -o -name '*.dylib.*' -o -name '*.so' -o -name '*.so.*' \) 2>/dev/null | sort | \
	awk '{ \
		path=$$0; \
		base=path; sub(/^.*\//, "", base); \
		name=base; sub(/^lib/, "", name); \
		if (name ~ /\.a$$/) { \
			type=1; sub(/\.a$$/, "", name); \
		} else if (name ~ /\.so(\..*)?$$/) { \
			type=2; sub(/\.so(\..*)?$$/, "", name); \
		} else if (name ~ /\.dylib$$/) { \
			type=3; sub(/\.dylib$$/, "", name); sub(/\.[0-9][0-9.]*$$/, "", name); \
		} else { \
			next; \
		} \
		if (!(name in best_type) || type < best_type[name]) { \
			best_type[name]=type; best_path[name]=path; \
		} \
	} END { \
		for (name in best_path) print best_path[name]; \
	}' | sort)
LIB_FILES := $(filter-out $(LUA_STATIC_LIB),$(LIB_FILES))
LIB_FILES := $(filter-out $(TOMLC17_STATIC_LIB),$(LIB_FILES))
LDLIBS += $(LIB_FILES)
LDLIBS += $(LUA_STATIC_LIB)
LDLIBS += $(TOMLC17_STATIC_LIB)

ifeq ($(UNAME_S),Linux)
LDLIBS += $(RAYLIB_LIBS)
LDLIBS += $(SDL2_LIBS)
LDLIBS += -lm
endif

ifeq ($(UNAME_S),Darwin)
LDFLAGS += -framework Cocoa -framework IOKit -framework CoreVideo -framework CoreAudio -framework AudioToolbox -framework CoreFoundation -framework AppKit
LDLIBS += -lm
endif

.PHONY: all debug clean clean-test fclean re test check-allocators prepare-lua check-raylib

-include $(DEP)

ASAN_SYMBOLIZER := $(shell command -v llvm-symbolizer 2>/dev/null)
ifeq ($(ASAN_SYMBOLIZER),)
ASAN_SYMBOLIZER := $(shell command -v addr2line 2>/dev/null)
endif

ASAN_OPTIONS_BASE := symbolize=1:abort_on_error=0:detect_leaks=1
ifneq ($(ASAN_SYMBOLIZER),)
ASAN_OPTIONS_BASE := $(ASAN_OPTIONS_BASE):external_symbolizer_path=$(ASAN_SYMBOLIZER)
endif
LSAN_OPTIONS_BASE := report_objects=1
LSAN_OPTIONS_WITH_SUPPRESSIONS := $(LSAN_OPTIONS_BASE)
ifneq ($(wildcard $(LSAN_SUPPRESSIONS_FILE)),)
LSAN_OPTIONS_WITH_SUPPRESSIONS := $(LSAN_OPTIONS_WITH_SUPPRESSIONS):suppressions=$(abspath $(LSAN_SUPPRESSIONS_FILE)):print_suppressions=1
endif

all: check-allocators prepare-lua check-raylib $(TARGET)

debug: all

run-debug: debug
	./$(DEBUG_TARGET) --run $(RUN_PROJECT)

run-debug-symbols: debug
	ASAN_OPTIONS='$(ASAN_OPTIONS_BASE)' LSAN_OPTIONS='$(LSAN_OPTIONS_BASE)' ./$(DEBUG_TARGET) --run $(RUN_PROJECT)

run-debug-symbols-sdl-suppressed: debug
	ASAN_OPTIONS='$(ASAN_OPTIONS_BASE)' LSAN_OPTIONS='$(LSAN_OPTIONS_WITH_SUPPRESSIONS)' ./$(DEBUG_TARGET) --run $(RUN_PROJECT)

$(TARGET): $(OBJ) $(LUA_STATIC_LIB) $(TOMLC17_STATIC_LIB) | $(BINDIR)
	$(CC) $(OBJ) $(LDFLAGS) $(LDLIBS) -o $@

prepare-lua:
ifeq ($(UNAME_S),Linux)
	@if [ ! -f "$(LUA_STATIC_LIB)" ] || file "$(LUA_STATIC_LIB)" | grep -q 'Mach-O'; then \
		echo "Building Linux Lua static library..."; \
		$(MAKE) -C $(LUA_DIR)/src clean linux; \
		mkdir -p $(LUA_DIR)/lib; \
		cp -f $(LUA_DIR)/src/liblua.a $(LUA_STATIC_LIB); \
	fi
else
	@if [ ! -f "$(LUA_STATIC_LIB)" ]; then \
		echo "Missing Lua static library at $(LUA_STATIC_LIB). Build it first with: cd $(LUA_DIR) && make clean && make macosx && mkdir -p lib && cp -f src/*.a lib/"; \
		exit 1; \
	fi
endif

check-raylib:
ifeq ($(UNAME_S),Linux)
	@if [ -z "$(RAYLIB_LIBS)" ] && ! find $(LIBDIR)/raylib -type f \( -name 'libraylib.a' -o -name 'libraylib.so' -o -name 'libraylib.so.*' \) | grep -q .; then \
		echo "Missing raylib link flags on Linux. Install raylib development package (pkg-config module 'raylib') or place a raylib library under $(LIBDIR)/raylib."; \
		exit 1; \
	fi
endif

$(TOMLC17_STATIC_LIB):
	@$(MAKE) -C $(TOMLC17_DIR) clean install prefix=./

$(BUILD_OBJDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD_OBJDIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(BINDIR):
	@mkdir -p $@

clean:
	@find . -type f -name '*.o' -delete
	@find . -type f -name '*.d' -delete
	@find $(OBJDIR) -type d -empty -delete 2>/dev/null || true
	@rm -rf logs

clean-test:
	@rm -rf $(OBJDIR)/test
	@rm -f $(TEST_TARGET)

fclean: clean
	@rm -f $(BINDIR)/springengine $(DEBUG_TARGET) $(TEST_TARGET)

re: fclean all

test: CPPFLAGS += -DTesting
test: clean-test all
	./$(TARGET)

check-allocators:
	@matches=$$(find $(SRCDIR) -type f -name '*.c' ! -path '$(SRCDIR)/common.c' -exec grep -nEw '(malloc|realloc|free)[[:space:]]*[(]' {} + || true); \
	if [ -n "$$matches" ]; then \
		echo "Error: direct malloc/realloc/free usage is forbidden outside src/common.c"; \
		echo "$$matches"; \
		exit 1; \
	fi
