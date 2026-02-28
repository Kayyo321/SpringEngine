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

LIB_FILES := $(shell find $(LIBDIR) -type f \( -name '*.a' -o -name '*.so' -o -name '*.dylib' \) 2>/dev/null)
LDLIBS += $(LIB_FILES)

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
