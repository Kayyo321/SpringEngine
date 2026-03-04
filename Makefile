CC ?= cc
AR ?= ar
RANLIB ?= ranlib
CFLAGS ?= -Wall -Wextra -Wpedantic -Werror -std=c11
CPPFLAGS ?=
LDFLAGS ?=

CPPFLAGS += -D_POSIX_C_SOURCE=200809L

HOST_UNAME_S := $(shell uname -s)
TARGET_OS ?= $(HOST_UNAME_S)
STATIC_LINK ?= 0
RELEASE ?= 0
RELEASE_FLAGS ?= -O3 -DNDEBUG -ffunction-sections -fdata-sections
TARGET_ARCH_CFLAGS ?=
TARGET_STATIC_LDFLAGS ?=

SRCDIR := src
LIBDIR := lib
OBJDIR := obj
BINDIR := bin
TARGET ?= $(BINDIR)/springengine
RELEASE_DIR := $(BINDIR)/release
DEBUG_TARGET := $(BINDIR)/springengine-debug
TEST_TARGET := $(BINDIR)/springengine-test
RUN_PROJECT ?= ./example-project/
LSAN_SUPPRESSIONS_FILE ?= .lsan-suppressions.txt

BUILD_OBJDIR := $(OBJDIR)

ifeq ($(RELEASE),1)
CFLAGS += $(RELEASE_FLAGS)
endif

CFLAGS += $(TARGET_ARCH_CFLAGS)
LDFLAGS += $(TARGET_ARCH_CFLAGS)

ifeq ($(STATIC_LINK),1)
LDFLAGS += $(TARGET_STATIC_LDFLAGS)
endif

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

ifeq ($(RELEASE),1)
LUA_STATIC_LIB := $(LUA_DIR)/src/liblua.a
TOMLC17_STATIC_LIB := $(TOMLC17_DIR)/src/libtomlc17.a
endif

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

ifeq ($(TARGET_OS),Linux)
ifneq ($(STATIC_LINK),1)
RAYLIB_CFLAGS := $(shell pkg-config --cflags raylib 2>/dev/null)
RAYLIB_LIBS := $(shell pkg-config --libs raylib 2>/dev/null)
SDL2_LIBS := $(shell pkg-config --libs sdl2 2>/dev/null)
endif
CPPFLAGS += $(RAYLIB_CFLAGS)
endif

ifeq ($(wildcard $(TOMLC17_DIR)/include/tomlc17.h),)
$(error Missing tomlc17 headers at $(TOMLC17_DIR)/include/tomlc17.h. Ensure lib/tomlc17 is present.)
endif

ifeq ($(STATIC_LINK),1)
LIB_FILES := $(shell find $(LIBDIR) -type f -name '*.a' 2>/dev/null | sort)
else
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
endif
LIB_FILES := $(filter-out $(LUA_STATIC_LIB),$(LIB_FILES))
LIB_FILES := $(filter-out $(TOMLC17_STATIC_LIB),$(LIB_FILES))
LIB_FILES := $(filter-out $(LUA_DIR)/src/liblua.a,$(LIB_FILES))
LIB_FILES := $(filter-out $(TOMLC17_DIR)/src/libtomlc17.a,$(LIB_FILES))
LDLIBS += $(LIB_FILES)
LDLIBS += $(LUA_STATIC_LIB)
LDLIBS += $(TOMLC17_STATIC_LIB)

ifeq ($(TARGET_OS),Linux)
LDLIBS += $(RAYLIB_LIBS)
LDLIBS += $(SDL2_LIBS)
LDLIBS += -lm -lpthread -ldl -lrt -lX11
endif

ifeq ($(TARGET_OS),Darwin)
LDFLAGS += -framework Cocoa -framework IOKit -framework CoreVideo -framework CoreAudio -framework AudioToolbox -framework CoreFoundation -framework AppKit
LDLIBS += -lm
endif

ifeq ($(TARGET_OS),Windows)
CPPFLAGS += -Dlstat=stat
LDLIBS += -lopengl32 -lgdi32 -lwinmm
endif

LUA_MAKE_TARGET ?= posix
ifeq ($(TARGET_OS),Linux)
LUA_MAKE_TARGET := linux
endif
ifeq ($(TARGET_OS),Darwin)
LUA_MAKE_TARGET := macosx
endif
ifeq ($(TARGET_OS),Windows)
LUA_MAKE_TARGET := generic
endif

RELEASE_TARGETS ?= linux-x86_64 linux-i686 windows-x86_64 windows-i686 macos-x86_64 macos-arm64
CC_LINUX_X86_64 ?= x86_64-linux-gnu-gcc
AR_LINUX_X86_64 ?= x86_64-linux-gnu-ar
RANLIB_LINUX_X86_64 ?= x86_64-linux-gnu-ranlib
CC_LINUX_I686 ?= i686-linux-gnu-gcc
AR_LINUX_I686 ?= i686-linux-gnu-ar
RANLIB_LINUX_I686 ?= i686-linux-gnu-ranlib
CC_WINDOWS_X86_64 ?= x86_64-w64-mingw32-gcc
AR_WINDOWS_X86_64 ?= x86_64-w64-mingw32-ar
RANLIB_WINDOWS_X86_64 ?= x86_64-w64-mingw32-ranlib
CC_WINDOWS_I686 ?= i686-w64-mingw32-gcc
AR_WINDOWS_I686 ?= i686-w64-mingw32-ar
RANLIB_WINDOWS_I686 ?= i686-w64-mingw32-ranlib
CC_MACOS_X86_64 ?= clang
AR_MACOS_X86_64 ?= ar
RANLIB_MACOS_X86_64 ?= ranlib
CC_MACOS_ARM64 ?= clang
AR_MACOS_ARM64 ?= ar
RANLIB_MACOS_ARM64 ?= ranlib

RAYLIB_PLATFORM_OS ?=
ifeq ($(TARGET_OS),Linux)
RAYLIB_PLATFORM_OS := LINUX
endif
ifeq ($(TARGET_OS),Darwin)
RAYLIB_PLATFORM_OS := OSX
endif
ifeq ($(TARGET_OS),Windows)
RAYLIB_PLATFORM_OS := WINDOWS
endif

RAYLIB_PLATFORM ?= PLATFORM_DESKTOP
RAYLIB_SDL_INCLUDE_PATH ?=
RAYLIB_SDL_LIBRARIES ?=
RAYLIB_BACKEND_ARGS :=
ifeq ($(RAYLIB_PLATFORM),PLATFORM_DESKTOP_SDL)
ifneq ($(strip $(RAYLIB_SDL_INCLUDE_PATH)),)
RAYLIB_BACKEND_ARGS += SDL_INCLUDE_PATH="$(RAYLIB_SDL_INCLUDE_PATH)"
endif
ifneq ($(strip $(RAYLIB_SDL_LIBRARIES)),)
RAYLIB_BACKEND_ARGS += SDL_LIBRARIES="$(RAYLIB_SDL_LIBRARIES)"
endif
endif

.PHONY: all debug clean clean-test fclean re test check-allocators prepare-lua prepare-tomlc17 prepare-raylib check-raylib check-toolchain release release-all release-linux-x86_64 release-linux-i686 release-windows-x86_64 release-windows-i686 release-macos-x86_64 release-macos-arm64
.NOTPARALLEL: all

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

ifneq (,$(filter release,$(MAKECMDGOALS)))
all:
	@true
else
all: check-allocators prepare-lua prepare-tomlc17 prepare-raylib check-raylib $(TARGET)
endif

debug: all

run-debug: debug
	./$(DEBUG_TARGET) --run $(RUN_PROJECT)

run-debug-symbols: debug
	ASAN_OPTIONS='$(ASAN_OPTIONS_BASE)' LSAN_OPTIONS='$(LSAN_OPTIONS_BASE)' ./$(DEBUG_TARGET) --run $(RUN_PROJECT)

run-debug-symbols-sdl-suppressed: debug
	ASAN_OPTIONS='$(ASAN_OPTIONS_BASE)' LSAN_OPTIONS='$(LSAN_OPTIONS_WITH_SUPPRESSIONS)' ./$(DEBUG_TARGET) --run $(RUN_PROJECT)

$(TARGET): $(OBJ) $(LUA_STATIC_LIB) $(TOMLC17_STATIC_LIB) | $(BINDIR)
	@mkdir -p $(dir $@)
	$(CC) $(OBJ) $(LDFLAGS) $(LDLIBS) -o $@

prepare-lua:
	@if [ "$(RELEASE)" = "1" ]; then \
		echo "Building Lua static library for $(TARGET_OS) with $(CC)..."; \
		$(MAKE) -C $(LUA_DIR)/src clean CC="$(CC)" AR="$(AR) rcu" RANLIB="$(RANLIB)"; \
		$(MAKE) -C $(LUA_DIR)/src $(LUA_MAKE_TARGET) CC="$(CC)" AR="$(AR) rcu" RANLIB="$(RANLIB)" MYCFLAGS="$(TARGET_ARCH_CFLAGS)"; \
	else \
		if [ ! -f "$(LUA_STATIC_LIB)" ]; then \
			echo "Missing Lua static library at $(LUA_STATIC_LIB). Build it first with: cd $(LUA_DIR) && make clean && make $(LUA_MAKE_TARGET) && mkdir -p lib && cp -f src/liblua.a lib/liblua.a"; \
			exit 1; \
		fi; \
	fi

check-raylib:
ifeq ($(STATIC_LINK),1)
	@if [ ! -f "$(LIBDIR)/raylib/lib/libraylib.a" ]; then \
		echo "Missing static raylib at $(LIBDIR)/raylib/lib/libraylib.a. Build it first (example: ./rebuild_libs.sh)."; \
		exit 1; \
	fi
else
ifeq ($(TARGET_OS),Linux)
	@if [ -z "$(RAYLIB_LIBS)" ] && ! find $(LIBDIR)/raylib -type f \( -name 'libraylib.a' -o -name 'libraylib.so' -o -name 'libraylib.so.*' \) | grep -q .; then \
		echo "Missing raylib link flags on Linux. Install raylib development package (pkg-config module 'raylib') or place a raylib library under $(LIBDIR)/raylib."; \
		exit 1; \
	fi
endif
endif

prepare-tomlc17:
	@if [ "$(RELEASE)" = "1" ]; then \
		echo "Building tomlc17 static library for $(TARGET_OS) with $(CC)..."; \
		$(MAKE) -C $(TOMLC17_DIR)/src clean CC="$(CC)" AR="$(AR)"; \
		$(MAKE) -C $(TOMLC17_DIR)/src libtomlc17.a CC="$(CC)" AR="$(AR)" CFLAGS="-std=c17 -fpic -Wmissing-declarations -Wall -Wextra -MMD -O3 -DNDEBUG $(TARGET_ARCH_CFLAGS)"; \
	elif [ ! -f "$(TOMLC17_STATIC_LIB)" ]; then \
		$(MAKE) -C $(TOMLC17_DIR)/src clean CC="$(CC)" AR="$(AR)"; \
		$(MAKE) -C $(TOMLC17_DIR)/src libtomlc17.a CC="$(CC)" AR="$(AR)" CFLAGS="-std=c17 -fpic -Wmissing-declarations -Wall -Wextra -MMD -O3 -DNDEBUG $(TARGET_ARCH_CFLAGS)"; \
		mkdir -p $(TOMLC17_DIR)/lib; \
		cp -f $(TOMLC17_DIR)/src/libtomlc17.a $(TOMLC17_STATIC_LIB); \
	fi

prepare-raylib:
	@if [ "$(RELEASE)" = "1" ]; then \
		echo "Building raylib static library for $(TARGET_OS) with $(CC)..."; \
		rm -f $(LIBDIR)/raylib/lib/libraylib.a; \
		$(MAKE) -C $(LIBDIR)/raylib/src clean; \
		$(MAKE) -C $(LIBDIR)/raylib/src RAYLIB_LIBTYPE=STATIC RAYLIB_RELEASE_PATH=../lib CC="$(CC)" AR="$(AR)" PLATFORM="$(RAYLIB_PLATFORM)" PLATFORM_OS="$(RAYLIB_PLATFORM_OS)" CUSTOM_CFLAGS="$(TARGET_ARCH_CFLAGS)" $(RAYLIB_BACKEND_ARGS); \
	fi

$(TOMLC17_STATIC_LIB):
	@$(MAKE) -C $(TOMLC17_DIR)/src clean CC="$(CC)" AR="$(AR)"
	@$(MAKE) -C $(TOMLC17_DIR)/src libtomlc17.a CC="$(CC)" AR="$(AR)" CFLAGS="-std=c17 -fpic -Wmissing-declarations -Wall -Wextra -MMD -O3 -DNDEBUG $(TARGET_ARCH_CFLAGS)"
	@mkdir -p $(TOMLC17_DIR)/lib
	@cp -f $(TOMLC17_DIR)/src/libtomlc17.a $(TOMLC17_STATIC_LIB)

check-toolchain:
	@command -v "$(CC)" >/dev/null 2>&1 || { echo "Compiler not found: $(CC)"; exit 1; }
	@command -v "$(AR)" >/dev/null 2>&1 || { echo "Archiver not found: $(AR)"; exit 1; }
	@command -v "$(RANLIB)" >/dev/null 2>&1 || { echo "Ranlib not found: $(RANLIB)"; exit 1; }

release: release-all

release-all:
	@fails=0; \
	for target in $(addprefix release-,$(RELEASE_TARGETS)); do \
		echo "[release-all] Building $$target"; \
		if $(MAKE) $$target; then \
			echo "[release-all] OK: $$target"; \
		else \
			echo "[release-all] FAIL: $$target"; \
			fails=$$((fails+1)); \
		fi; \
	done; \
	if [ $$fails -ne 0 ]; then \
		echo "[release-all] Completed with $$fails failing target(s)."; \
		exit 2; \
	fi; \
	echo "[release-all] All targets built successfully."

release-linux-x86_64:
	@$(MAKE) check-toolchain all \
		RELEASE=1 STATIC_LINK=1 TARGET_OS=Linux \
		CC="$(CC_LINUX_X86_64)" AR="$(AR_LINUX_X86_64)" RANLIB="$(RANLIB_LINUX_X86_64)" \
		TARGET_ARCH_CFLAGS="-m64" TARGET_STATIC_LDFLAGS="-static" \
		TARGET="$(RELEASE_DIR)/linux-x86_64/springengine-linux-64" BUILD_OBJDIR="$(OBJDIR)/release/linux-x86_64"

release-linux-i686:
	@$(MAKE) check-toolchain all \
		RELEASE=1 STATIC_LINK=1 TARGET_OS=Linux \
		CC="$(CC_LINUX_I686)" AR="$(AR_LINUX_I686)" RANLIB="$(RANLIB_LINUX_I686)" \
		TARGET_ARCH_CFLAGS="-m32" TARGET_STATIC_LDFLAGS="-static" \
		TARGET="$(RELEASE_DIR)/linux-i686/springengine-linux-32" BUILD_OBJDIR="$(OBJDIR)/release/linux-i686"

release-windows-x86_64:
	@$(MAKE) check-toolchain all \
		RELEASE=1 STATIC_LINK=1 TARGET_OS=Windows \
		CC="$(CC_WINDOWS_X86_64)" AR="$(AR_WINDOWS_X86_64)" RANLIB="$(RANLIB_WINDOWS_X86_64)" \
		TARGET_ARCH_CFLAGS="-m64" TARGET_STATIC_LDFLAGS="-static -static-libgcc" \
		TARGET="$(RELEASE_DIR)/windows-x86_64/springengine-win-64.exe" BUILD_OBJDIR="$(OBJDIR)/release/windows-x86_64"

release-windows-i686:
	@$(MAKE) check-toolchain all \
		RELEASE=1 STATIC_LINK=1 TARGET_OS=Windows \
		CC="$(CC_WINDOWS_I686)" AR="$(AR_WINDOWS_I686)" RANLIB="$(RANLIB_WINDOWS_I686)" \
		TARGET_ARCH_CFLAGS="-m32" TARGET_STATIC_LDFLAGS="-static -static-libgcc" \
		TARGET="$(RELEASE_DIR)/windows-i686/springengine-win-32.exe" BUILD_OBJDIR="$(OBJDIR)/release/windows-i686"

release-macos-x86_64:
	@$(MAKE) check-toolchain all \
		RELEASE=1 STATIC_LINK=1 TARGET_OS=Darwin \
		CC="$(CC_MACOS_X86_64)" AR="$(AR_MACOS_X86_64)" RANLIB="$(RANLIB_MACOS_X86_64)" \
		TARGET_ARCH_CFLAGS="-arch x86_64" TARGET_STATIC_LDFLAGS="" \
		TARGET="$(RELEASE_DIR)/macos-x86_64/springengine-macos-64" BUILD_OBJDIR="$(OBJDIR)/release/macos-x86_64"

release-macos-arm64:
	@$(MAKE) check-toolchain all \
		RELEASE=1 STATIC_LINK=1 TARGET_OS=Darwin \
		CC="$(CC_MACOS_ARM64)" AR="$(AR_MACOS_ARM64)" RANLIB="$(RANLIB_MACOS_ARM64)" \
		TARGET_ARCH_CFLAGS="-arch arm64" TARGET_STATIC_LDFLAGS="" \
		TARGET="$(RELEASE_DIR)/macos-arm64/springengine-macos-arm64" BUILD_OBJDIR="$(OBJDIR)/release/macos-arm64"

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
	@rm -rf $(RELEASE_DIR)

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
