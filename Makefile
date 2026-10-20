# Team 13 toolchain build. Portable across macOS clang and Linux gcc.
#
#   make           build the three tools and the unit-test binaries
#   make test      unit tests, golden-file tests, every program end to end
#   make run       assemble, simulate and print every programs/*.s
#                  (build/programs/*.img .lst .trc, build/views/*.txt)
#   make check     test + run: the one command for the grader
#   make report    the two report PDFs (needs Python 3 with WeasyPrint)
#   make clean     remove build/
#
# Keep the target names (all, test, t13as, t13sim, t13view, clean) and the
# BUILD variable: CI and the README use them.

CC      ?= cc
CFLAGS  ?= -std=c99 -Wall -Wextra -Wpedantic -Werror -O2
CPPFLAGS += -Iinclude -Isrc/asm -Isrc/sim -Isrc/view
BUILD   ?= build
PYTHON  ?= python3

COMMON_SRC := src/common/isa.c
COMMON_OBJ := $(COMMON_SRC:%.c=$(BUILD)/%.o)

# Assembler library: every src/asm/*.c except the CLI (main.c).
ASM_SRC := src/asm/parse.c src/asm/symtab.c src/asm/select.c src/asm/assemble.c \
           src/asm/image.c src/asm/listing.c
ASM_OBJ := $(ASM_SRC:%.c=$(BUILD)/%.o)

# Simulator library: every src/sim/*.c except the CLI (main.c).
SIM_SRC := src/sim/load.c src/sim/cpu.c src/sim/trace.c
SIM_OBJ := $(SIM_SRC:%.c=$(BUILD)/%.o)

# Viewer library: every src/view/*.c except the CLI (main.c).
VIEW_SRC := src/view/read.c src/view/render.c
VIEW_OBJ := $(VIEW_SRC:%.c=$(BUILD)/%.o)

HEADERS := include/isa.h include/isa_ops.def src/asm/asm.h src/sim/sim.h src/view/view.h

TESTS := $(BUILD)/test_isa $(BUILD)/test_asm $(BUILD)/test_assemble $(BUILD)/test_sim $(BUILD)/test_view
TOOLS := $(BUILD)/t13as $(BUILD)/t13sim $(BUILD)/t13view

PROGRAMS    := $(wildcard programs/*.s)
PROGRAM_IMG := $(PROGRAMS:programs/%.s=$(BUILD)/programs/%.img)
VIEWS       := summary waterfall grid loaded segments

.PHONY: all test check run views programs report clean t13as t13sim t13view

all: $(TOOLS) $(TESTS)

t13as: $(BUILD)/t13as
t13sim: $(BUILD)/t13sim
t13view: $(BUILD)/t13view

$(BUILD)/%.o: %.c $(HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

# ---- tools ------------------------------------------------------------------

$(BUILD)/t13as: src/asm/main.c $(ASM_OBJ) $(COMMON_OBJ) $(HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) src/asm/main.c $(ASM_OBJ) $(COMMON_OBJ) -o $@

$(BUILD)/t13sim: src/sim/main.c $(SIM_OBJ) $(COMMON_OBJ) $(HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) src/sim/main.c $(SIM_OBJ) $(COMMON_OBJ) -o $@

$(BUILD)/t13view: src/view/main.c $(VIEW_OBJ) $(COMMON_OBJ) $(HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) src/view/main.c $(VIEW_OBJ) $(COMMON_OBJ) -o $@

# ---- unit tests -------------------------------------------------------------

$(BUILD)/test_isa: tests/test_isa.c $(COMMON_OBJ) $(HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_isa.c $(COMMON_OBJ) -o $@

$(BUILD)/test_asm: tests/test_asm.c $(ASM_OBJ) $(COMMON_OBJ) $(HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_asm.c $(ASM_OBJ) $(COMMON_OBJ) -o $@

$(BUILD)/test_assemble: tests/test_assemble.c $(ASM_OBJ) $(COMMON_OBJ) $(HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_assemble.c $(ASM_OBJ) $(COMMON_OBJ) -o $@

$(BUILD)/test_sim: tests/test_sim.c $(SIM_OBJ) $(COMMON_OBJ) $(HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_sim.c $(SIM_OBJ) $(COMMON_OBJ) -o $@

$(BUILD)/test_view: tests/test_view.c $(VIEW_OBJ) $(COMMON_OBJ) $(HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_view.c $(VIEW_OBJ) $(COMMON_OBJ) -o $@

test: $(TESTS) $(TOOLS)
	@for t in $(TESTS); do echo "== $$t"; $$t || exit 1; done
	@echo "== tests/golden.sh"; sh tests/golden.sh $(BUILD)
	@echo "== tests/programs.sh"; CC="$(CC)" sh tests/programs.sh $(BUILD)

check: test run

# ---- programs ---------------------------------------------------------------

# Assemble every programs/*.s into $(BUILD)/programs/<name>.img and .lst
programs: $(PROGRAM_IMG)

$(BUILD)/programs/%.img: programs/%.s $(BUILD)/t13as
	@mkdir -p $(dir $@)
	$(BUILD)/t13as $< -o $@ -l $(@:.img=.lst)

# Simulate every program and print its views into $(BUILD)/views/.
run: $(TOOLS) $(PROGRAM_IMG)
	@mkdir -p $(BUILD)/views
	@for img in $(PROGRAM_IMG); do \
	    n=$$(basename $$img .img); \
	    $(BUILD)/t13sim $$img -o $(BUILD)/programs/$$n.trc || exit 1; \
	    for v in $(VIEWS); do \
	        $(BUILD)/t13view $(BUILD)/programs/$$n.trc --$$v -o $(BUILD)/views/$$n.$$v.txt || exit 1; \
	    done; \
	done
	@echo "views: $(BUILD)/views/<program>.<view>.txt"

views: run

# ---- report -----------------------------------------------------------------

report: run
	$(PYTHON) report/build.py --build $(BUILD)

clean:
	rm -rf $(BUILD)
