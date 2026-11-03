# Team 13 toolchain build.
# Placeholder until the repo/CI owner replaces it. Assembler rules added by
# Yashashav: please keep the target names (all, test, t13as, clean) and the
# BUILD variable when this file is replaced.
# Portable across macOS clang and Linux gcc.

CC      ?= cc
CFLAGS  ?= -std=c99 -Wall -Wextra -Wpedantic -Werror -O2
CPPFLAGS += -Iinclude -Isrc/asm
BUILD   ?= build

COMMON_SRC := src/common/isa.c
COMMON_OBJ := $(COMMON_SRC:%.c=$(BUILD)/%.o)

# Assembler library: every src/asm/*.c except the CLI (main.c).
ASM_SRC := src/asm/parse.c src/asm/symtab.c src/asm/select.c src/asm/assemble.c
ASM_OBJ := $(ASM_SRC:%.c=$(BUILD)/%.o)

HEADERS := include/isa.h include/isa_ops.def src/asm/asm.h

TESTS := $(BUILD)/test_isa $(BUILD)/test_asm $(BUILD)/test_assemble

.PHONY: all test clean

all: $(TESTS)

$(BUILD)/%.o: %.c $(HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/test_isa: tests/test_isa.c $(COMMON_OBJ) $(HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_isa.c $(COMMON_OBJ) -o $@

$(BUILD)/test_asm: tests/test_asm.c $(ASM_OBJ) $(COMMON_OBJ) $(HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_asm.c $(ASM_OBJ) $(COMMON_OBJ) -o $@

$(BUILD)/test_assemble: tests/test_assemble.c $(ASM_OBJ) $(COMMON_OBJ) $(HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_assemble.c $(ASM_OBJ) $(COMMON_OBJ) -o $@

test: $(TESTS)
	@for t in $(TESTS); do echo "== $$t"; ./$$t || exit 1; done

clean:
	rm -rf $(BUILD)
