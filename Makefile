# Team 13 toolchain build.
# Placeholder until the repo/CI owner replaces it: builds the shared codec
# and runs its unit tests. Portable across macOS clang and Linux gcc.

CC      ?= cc
CFLAGS  ?= -std=c99 -Wall -Wextra -Wpedantic -Werror -O2
CPPFLAGS += -Iinclude
BUILD   := build

COMMON_SRC := src/common/isa.c
COMMON_OBJ := $(COMMON_SRC:%.c=$(BUILD)/%.o)
HEADERS    := include/isa.h include/isa_ops.def

TESTS := $(BUILD)/test_isa

.PHONY: all test clean

all: $(TESTS)

$(BUILD)/%.o: %.c $(HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/test_isa: tests/test_isa.c $(COMMON_OBJ) $(HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_isa.c $(COMMON_OBJ) -o $@

test: $(TESTS)
	@for t in $(TESTS); do echo "== $$t"; ./$$t || exit 1; done

clean:
	rm -rf $(BUILD)
