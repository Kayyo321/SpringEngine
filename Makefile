CC ?= cc
CFLAGS ?= -Wall -Wextra -Wpedantic -std=c11
CPPFLAGS ?=
LDFLAGS ?=

SRCDIR := src
LIBDIR := lib
OBJDIR := obj
BINDIR := bin
TARGET ?= $(BINDIR)/springengine

SRC := $(shell find $(SRCDIR) -type f -name '*.c')
OBJ := $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SRC))

INCLUDE_DIRS := $(shell find $(SRCDIR) $(LIBDIR) -type d 2>/dev/null)
CPPFLAGS += $(addprefix -I,$(INCLUDE_DIRS))

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
LDLIBS += $(LIB_FILES)

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
LDFLAGS += -framework Cocoa -framework IOKit -framework CoreVideo -framework CoreAudio -framework AudioToolbox -framework CoreFoundation -framework AppKit
LDLIBS += -lm
endif

.PHONY: all clean fclean re

all: $(TARGET)

$(TARGET): $(OBJ) | $(BINDIR)
	$(CC) $(OBJ) $(LDFLAGS) $(LDLIBS) -o $@

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BINDIR):
	@mkdir -p $@

clean:
	@find . -type f -name '*.o' -delete
	@find $(OBJDIR) -type d -empty -delete 2>/dev/null || true

fclean: clean
	@rm -f $(TARGET)

re: fclean all
