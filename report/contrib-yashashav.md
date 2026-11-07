# Yashashav: contribution log
One bullet per merged PR, newest last. Reconcile at report time with `gh pr list --author yashashav-dk --state merged --json number,title,mergedAt`.

- `96b36d4` (Nov 2): `docs/isa.md` ISA spec; `include/isa_ops.def` opcode table; `include/isa.h` + `src/common/isa.c` encode/decode/disassemble; `tests/test_isa.c` (561 checks).
- PR #1 (Nov 3): `docs/image.md` image-format draft, `docs/asm.md` assembler manual, ISA v1.0 status.
- PR #2 (Nov 3): `t13as` assembler, `src/asm/` (parse, symtab, select, assemble, image, listing, main): two-pass, `.text .data .org .word .space .equ`, NOT/PUSH/POP, file:line errors; `programs/power.s` (8² = BASE^EXP by repeated MUL, Program A); tests: `tests/test_asm.c` (773 checks incl. disasm→asm round trip of all 53 op/mode pairs), `tests/test_assemble.c` (158), `tests/golden.sh` + `tests/golden/` (image and listing goldens, error path, exit codes); README run instructions.
