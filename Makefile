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
ASM_SRC := src/asm/parse.c src/asm/symtab.c src/asm/select.c src/asm/assemble.c \
           src/asm/image.c src/asm/listing.c
ASM_OBJ := $(ASM_SRC:%.c=$(BUILD)/%.o)

HEADERS := include/isa.h include/isa_ops.def src/asm/asm.h

TESTS := $(BUILD)/test_isa $(BUILD)/test_asm $(BUILD)/test_assemble
TOOLS := $(BUILD)/t13as

PROGRAMS    := $(wildcard programs/*.s)
PROGRAM_IMG := $(PROGRAMS:programs/%.s=$(BUILD)/programs/%.img)

.PHONY: all test clean t13as programs

all: $(TOOLS) $(TESTS)

t13as: $(BUILD)/t13as

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

$(BUILD)/t13as: src/asm/main.c $(ASM_OBJ) $(COMMON_OBJ) $(HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) src/asm/main.c $(ASM_OBJ) $(COMMON_OBJ) -o $@

# Assemble every programs/*.s into $(BUILD)/programs/<name>.img and .lst
programs: $(PROGRAM_IMG)

$(BUILD)/programs/%.img: programs/%.s $(BUILD)/t13as
	@mkdir -p $(dir $@)
	$(BUILD)/t13as $< -o $@ -l $(@:.img=.lst)

test: $(TESTS) $(TOOLS)
	@for t in $(TESTS); do echo "== $$t"; ./$$t || exit 1; done
	@echo "== tests/golden.sh"; sh tests/golden.sh $(BUILD)

clean:
	rm -rf $(BUILD)
