# `t13as`: T13 assembler manual

Status: **v1 draft, 2026-11-03.** The instruction forms are the ones in `docs/isa.md` Tables 1–4, written exactly as `isa_disasm()` prints them.

## 1. Command line

```
t13as source.s [-o out.img] [-l out.lst]
```

| Option | Meaning | Default |
|---|---|---|
| `-o out.img` | memory image to write (`docs/image.md`) | source name with `.s` replaced by `.img` |
| `-l out.lst` | listing to write (§7) | no listing |

| Exit | Meaning |
|---|---|
| 0 | assembled; outputs written |
| 1 | source errors, all printed to stderr; **no output file is written** |
| 2 | usage error, or the source file cannot be read |

## 2. Lines

```
[label:]  [mnemonic operands]  [; comment]
```

- One instruction or directive per line. Label-only, comment-only and blank lines are fine.
- A label takes the address where the next word in the current segment would go.
- Mnemonics, directives, register names and hex digits are **case-insensitive**. Labels and `.equ` names are **case-sensitive**.
- A label may not be a register name (`SP`, `R3`) or a mnemonic (`ADD`, `PUSH`).

## 3. Operands

```
operand    := register | '#' value | '[' register ']' | '[' register '+' value ']' | '[' value ']' | value
register   := 'R0' .. 'R15' | 'FP' (= R13) | 'SP' (= R14)
value      := number | identifier
number     := ['-'] decimal | '0x' hex           ; must fit -32768 .. 65535; negatives stored as two's complement
identifier := [A-Za-z_][A-Za-z0-9_]*             ; a label or an .equ name
```

| Form | Meaning | Example |
|---|---|---|
| `Rb` | register | `ADD R1, R2` |
| `#k` | constant | `MOV R1, #5`, `MOV R1, #BASE` |
| `[Rb]` | memory at a register | `MOV R0, [R2]` |
| `[Rb+k]` | memory at register + offset | `MOV R3, [FP+ARG]` |
| `[addr]` | memory at an absolute address | `MOV [result], R0` |
| `addr` | jump or call target | `JNE loop`, `CALL fact` |

Spaces inside `[...]` are ignored. Which forms each mnemonic accepts: `docs/isa.md` Tables 1, 3 and 4.

## 4. Directives

| Directive | Effect |
|---|---|
| `.text` | switch to the Text segment (`0000`–`00FF`). A file starts in `.text`. |
| `.data` | switch to the Data segment (`0100`–`017F`). Each segment keeps its own location counter. |
| `.org v` | move the current segment's counter forward to `v` (even, inside the segment, never backwards) |
| `.word v[, v ...]` | 1–8 words, each a number or a symbol |
| `.space n` | reserve `n` bytes (even, > 0). Emits no record; the loader leaves them 0. |
| `.equ NAME, v` | define a constant; `v` is a number or an already-defined symbol |

## 5. Pseudo-instructions

| Pseudo | Expands to | Note |
|---|---|---|
| `NOT Ra` | `XOR Ra, #0xFFFF` | sets flags |
| `PUSH Ra` | `SUB SP, #2` then `MOV [SP], Ra` | changes flags: never between `CMP` and a conditional jump |
| `POP Ra` | `MOV Ra, [SP]` then `ADD SP, #2` | changes flags, same caution |

## 6. Errors

Format on stderr: `path:line: error: message`. Every error in the file is reported, then `t13as` exits 1.

| Checked | Example message |
|---|---|
| unknown mnemonic or wrong operands | `unknown mnemonic 'MOVE'` · `bad operands for 'ADD'` |
| registers, numbers, brackets | `bad register 'R16'` · `constant out of range '70000'` · `expected ']'` |
| symbols | `undefined symbol 'lop'` · `duplicate symbol 'loop' (first defined at line 4)` · `'SP' is a register name` |
| addresses | `odd address 0x0101` · `address 0x0500 is outside memory (0x0000-0x03FF)` · `.org cannot move backwards` |
| segment size | `segment overflow: text is 0x0000-0x00FF` · `segment overflow: data is 0x0100-0x017F` |
| limits | `line too long (max 255 characters)` · `too many lines (max 1024)` · `too many symbols (max 128)` |

## 7. Listing (`-l`)

Excerpt from `programs/power.s` below. Every source line appears once, as written. The address column is filled for lines that have a label or emit or reserve memory. At most 3 words per row; longer `.word` lines continue on the next row. A symbol table follows.

```
ADDR  WORDS           SOURCE
0000  1100 0001       main:   MOV R0, #1              ; R0 = result, starts at 1
000C  A120 0000       loop:   CMP R2, #0              ; done when exponent reaches 0
0014  4001                    MUL R0, R1              ; result *= base

SYMBOLS
BASE         0008
EXP          0002
main         0000
```

## 8. Limits

255 characters per line · 1024 lines · 128 symbols · 8 values per `.word` · 32-character names.
