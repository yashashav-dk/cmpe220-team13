# CMPE 220 Team 13: T13 CPU toolchain

A small 16-bit CPU of our own design, with the C tools to assemble, run and print programs for it.

| Tool | Does | Owner | Status |
|---|---|---|---|
| `isa.c` (shared) | encode / decode / disassemble one instruction | Yashashav | **done**, tested |
| `t13as` | assembly → memory image + listing | Yashashav | not started |
| `t13sim` | load image → run → execution trace | Sai Karan | not started |
| `t13view` | trace → memory tables for the report | Manoj | not started |

## Build and test

Needs a C99 compiler and `make` (macOS clang or Linux gcc).

```sh
make test
```

Expected last line: `561 checks, 0 failed`.

CI ([`.github/workflows/ci.yml`](.github/workflows/ci.yml)) runs `make` and `make test` on every pull request and every push to `main`, on Linux with gcc and clang and on macOS with clang.

## Layout

```
docs/isa.md          ISA specification: encoding tables, execution rules, memory map
docs/trace.md        execution trace format: what t13sim writes and t13view reads
include/isa_ops.def  opcode table, one line per operation (X-macro)
include/isa.h        codec API shared by all three tools
src/common/isa.c     codec implementation
src/asm/             t13as   (Yashashav)   planned
src/sim/             t13sim  (Sai Karan)   planned
src/view/            t13view (Manoj)       planned
programs/            example programs, .s and .c   planned
report/              report sources                planned
tests/               unit tests
tests/golden/        expected outputs, including sample traces (halt.trc, power.trc)
.github/workflows/   CI
```

The `Makefile` is a placeholder that builds the codec and its tests. It will be replaced when the full build and CI are set up.

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

To add or change an instruction, edit its line in `include/isa_ops.def` and the tables in `docs/isa.md`, then run `make test`.
