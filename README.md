# CMPE 220 Team 13: T13 CPU toolchain

A small 16-bit CPU of our own design, with the C tools to assemble, run and print programs for it.

| Tool | Does | Owner | Status |
|---|---|---|---|
| `isa.c` (shared) | encode / decode / disassemble one instruction | Yashashav | **done**, tested |
| `t13as` | assembly → memory image + listing | Yashashav | **done**, tested |
| `t13sim` | load image → run → execution trace | Sai Karan | not started |
| `t13view` | trace → memory tables for the report | Manoj | not started |

## Build, test, run

Needs a C99 compiler and `make` (macOS clang or Linux gcc). From the repository root:

```sh
make             # builds build/t13as and the unit-test binaries
make test        # unit tests + golden-file tests; last line: golden: all passed
make programs    # assembles programs/*.s into build/programs/<name>.img and .lst
```

Assemble one program by hand:

```sh
build/t13as programs/power.s -o build/power.img -l build/power.lst
```

- `power.img` is the memory image the simulator loads ([`docs/image.md`](docs/image.md)).
- `power.lst` shows every source line next to its address and machine words.
- Assembler syntax, directives and error messages: [`docs/asm.md`](docs/asm.md). The ISA itself: [`docs/isa.md`](docs/isa.md).

## Programs

| File | What it computes | Expected result |
|---|---|---|
| `programs/power.s` | 8² = BASE^EXP by repeated multiplication (Program A, written in assembly) | `M[0x0100] = 0x0040` (64) after 17 instructions |

## Layout

```
docs/isa.md            ISA specification: encoding tables, execution rules, memory map
docs/asm.md            assembler manual: syntax, directives, listing, errors
docs/image.md          memory image (.img) format
include/isa_ops.def    opcode table, one line per operation (X-macro)
include/isa.h          codec API shared by all three tools
src/common/isa.c       codec implementation
src/asm/               t13as: parse.c symtab.c select.c assemble.c image.c listing.c main.c
src/sim/               t13sim  (Sai Karan)    planned
src/view/              t13view (Manoj)        planned
programs/              example programs
tests/                 unit tests (test_*.c), golden.sh, golden/ expected outputs
report/                report sources, contribution logs
```

The `Makefile` is a placeholder. It will be replaced when the full build and CI are set up; the target names above stay.

## Using the codec

```c
#include "isa.h"

uint8_t mem[4];
size_t len;
isa_insn in = { OP_MOV, 1, 1, 0, 5, 0 };   /* MOV R1, #5 */
isa_encode(&in, mem, sizeof mem, &len);    /* mem = 10 11 05 00, len = 4 */

isa_insn out;
char text[32];
isa_decode(mem, len, &out, &len);
isa_disasm(&out, text, sizeof text);       /* "MOV R1, #5" */
```

To add or change an instruction, edit its line in `include/isa_ops.def`, the matching rows in `src/asm/select.c` and the tables in `docs/isa.md`, then run `make test`.
