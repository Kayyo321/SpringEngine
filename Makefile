CC ?= cc
CFLAGS ?= -Wall -Wextra -Wpedantic -Werror -std=c11
CPPFLAGS ?=
LDFLAGS ?=

SRCDIR := src
LIBDIR := lib
OBJDIR := obj
BINDIR := bin
TARGET ?= $(BINDIR)/springengine
TEST_TARGET := $(BINDIR)/springengine-test

LUA_DIR := $(LIBDIR)/lua-5.4.6
LUA_INCLUDE_DIR := $(LUA_DIR)/src
LUA_STATIC_LIB := $(LUA_DIR)/lib/liblua.a
TOMLC17_DIR := $(LIBDIR)/tomlc17
TOMLC17_INCLUDE_DIR := $(TOMLC17_DIR)/include
TOMLC17_STATIC_LIB := $(TOMLC17_DIR)/lib/libtomlc17.a

ALL_SRC := $(shell find $(SRCDIR) -type f -name '*.c')
TEST_SRC := $(shell find $(SRCDIR)/testing -type f -name '*.c' 2>/dev/null)
SRC := $(filter-out $(TEST_SRC),$(ALL_SRC))
ifneq (,$(filter test,$(MAKECMDGOALS)))
SRC += $(TEST_SRC)
TARGET := $(TEST_TARGET)
endif
OBJ := $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SRC))

SRC_INCLUDE_DIRS := $(shell find $(SRCDIR) -type d 2>/dev/null)
LIB_INCLUDE_DIRS := \
	$(LIBDIR)/raylib/include \
	$(LIBDIR)/lua-5.4.6/src \
	$(LIBDIR)/tomlc17/include

CPPFLAGS += $(addprefix -I,$(SRC_INCLUDE_DIRS))
CPPFLAGS += $(addprefix -I,$(LIB_INCLUDE_DIRS))

ifeq ($(wildcard $(LUA_STATIC_LIB)),)
$(error Missing Lua static library at $(LUA_STATIC_LIB). Build it first with: cd $(LUA_DIR) && make clean && make macosx && mkdir -p lib && cp -f src/*.a lib/)
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

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
LDFLAGS += -framework Cocoa -framework IOKit -framework CoreVideo -framework CoreAudio -framework AudioToolbox -framework CoreFoundation -framework AppKit
LDLIBS += -lm
endif

.PHONY: all clean fclean re test

all: $(TARGET)

$(TARGET): $(OBJ) $(TOMLC17_STATIC_LIB) | $(BINDIR)
	$(CC) $(OBJ) $(LDFLAGS) $(LDLIBS) -o $@

$(TOMLC17_STATIC_LIB):
	@$(MAKE) -C $(TOMLC17_DIR) clean install prefix=./

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BINDIR):
	@mkdir -p $@

clean:
	@find . -type f -name '*.o' -delete
	@find $(OBJDIR) -type d -empty -delete 2>/dev/null || true
	@rm -rf logs

fclean: clean
	@rm -f $(BINDIR)/springengine $(TEST_TARGET)

re: fclean all

test: CPPFLAGS += -DTESTING
test: re
	./$(TARGET)
