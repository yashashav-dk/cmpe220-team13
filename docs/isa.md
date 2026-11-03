# T13 ISA — specification (draft for team review)

Status: **draft v0.1, 2026-11-02.** Follows the ISA proposal in the team plan (Oct 29). Open points are listed in §9. Once the team agrees, change it only by PR.

Machine-readable source of truth: [`include/isa_ops.def`](../include/isa_ops.def). Shared codec: [`include/isa.h`](../include/isa.h), [`src/common/isa.c`](../src/common/isa.c). Every example encoding below was produced by that codec, not typed by hand.

## 1. At a glance

| Item | Value |
|---|---|
| Word size | 16 bits, unsigned arithmetic |
| Instruction | 1 word, plus 1 extension word (EXT) in some modes → 2 or 4 bytes |
| Registers | 16 GPRs `R0`–`R15` (`R13` = `FP`, `R14` = `SP`) · special: `PC`, `IR`, `FLAGS` (Z N C) |
| Memory | 1 KiB, byte-addressed, little-endian, words on even addresses |
| Operations | 14 opcodes (`0`–`D`); `E`, `F` reserved (illegal instruction) |
| Cycle | fetch → decode → execute → store |

## 2. Instruction word

```
 15    12 11     8 7      4 3      0
+--------+--------+--------+--------+
|   OP   |  MODE  |   Ra   |   Rb   |   then EXT (16 bits) if the mode needs one
+--------+--------+--------+--------+
```

- `OP` + `MODE` together are the instructor's 8-bit opcode: `OP` picks the operation, `MODE` picks the variant (addressing mode, shift direction or jump condition).
- Every field is one hex digit, so a word in a memory dump decodes by eye: `2012` = ADD (2), mode 0, R1, R2.
- `EXT` is a constant, an offset or an absolute address. It follows the instruction word at `address + 2`.
- Byte order: little-endian. `MOV R1, #5` is the words `1110 0005`, stored as bytes `10 11 05 00`.
- A register field that the mode does not use **must be 0**. Anything else is an illegal instruction.

## 3. Encoding table

`a` = Ra field, `b` = Rb field, `xxxx` = EXT word. `M[e]` = the 16-bit word at byte address `e`.

**Table 1. SYS, MOV, CALL, RET**

| OP | MODE | Assembly | Word(s) | Operation | Example | Hex |
|---|---|---|---|---|---|---|
| 0 | 0 | `HALT` | `0000` | stop the machine | `HALT` | `0000` |
| 0 | 1 | `NOP` | `0100` | no operation | `NOP` | `0100` |
| 1 | 0 | `MOV Ra, Rb` | `10ab` | Ra ← Rb | `MOV R1, R2` | `1012` |
| 1 | 1 | `MOV Ra, #k` | `11a0 xxxx` | Ra ← k | `MOV R1, #5` | `1110 0005` |
| 1 | 2 | `MOV Ra, [Rb]` | `12ab` | Ra ← M[Rb] (load) | `MOV R0, [R2]` | `1202` |
| 1 | 3 | `MOV Ra, [Rb+k]` | `13ab xxxx` | Ra ← M[Rb+k] (load) | `MOV R3, [FP+4]` | `133D 0004` |
| 1 | 4 | `MOV [Ra], Rb` | `14ab` | M[Ra] ← Rb (store) | `MOV [SP], R1` | `14E1` |
| 1 | 5 | `MOV [Ra+k], Rb` | `15ab xxxx` | M[Ra+k] ← Rb (store) | `MOV [FP+6], R0` | `15D0 0006` |
| 1 | 6 | `MOV Ra, [addr]` | `16a0 xxxx` | Ra ← M[addr] (load) | `MOV R0, [0x0100]` | `1600 0100` |
| 1 | 7 | `MOV [addr], Ra` | `17a0 xxxx` | M[addr] ← Ra (store) | `MOV [0x0102], R0` | `1700 0102` |
| C | 0 | `CALL addr` | `C000 xxxx` | SP ← SP−2; M[SP] ← PC; PC ← addr | `CALL 0x0040` | `C000 0040` |
| D | 0 | `RET` | `D000` | PC ← M[SP]; SP ← SP+2 | `RET` | `D000` |

**Table 2. Arithmetic and logic (OP 2–8): Ra ← Ra *op* s, sets Z N C**

| OP | Mnemonic | Operation |
|---|---|---|
| 2 | `ADD` | Ra ← Ra + s |
| 3 | `SUB` | Ra ← Ra − s |
| 4 | `MUL` | Ra ← low 16 bits of Ra × s |
| 5 | `DIV` | Ra ← Ra ÷ s (unsigned quotient) |
| 6 | `AND` | Ra ← Ra AND s |
| 7 | `OR` | Ra ← Ra OR s |
| 8 | `XOR` | Ra ← Ra XOR s |

**Table 3. Source operand `s` of OP 2–8** (`o` = the OP digit)

| MODE | Assembly | Source `s` | Word(s) |
|---|---|---|---|
| 0 | `ADD Ra, Rb` | Rb | `o0ab` |
| 1 | `ADD Ra, #k` | k (EXT) | `o1a0 xxxx` |
| 2 | `ADD Ra, [Rb]` | M[Rb] | `o2ab` |
| 3 | `ADD Ra, [Rb+k]` | M[Rb+k] | `o3ab xxxx` |

Examples: `ADD R1, R2` = `2012` · `ADD R1, #1` = `2110 0001` · `ADD R0, [R2]` = `2202` · `ADD R0, [FP+4]` = `230D 0004` · `SUB SP, #2` = `31E0 0002` · `MUL R0, R1` = `4001` · `DIV R0, #2` = `5100 0002` · `AND R1, #255` = `6110 00FF` · `OR R1, R2` = `7012` · `XOR R1, #65535` = `8110 FFFF`.

**Table 4. SHIFT, CMP, JMP**

| OP | MODE | Assembly | Word(s) | Operation | Example | Hex |
|---|---|---|---|---|---|---|
| 9 | 0 | `SHL Ra, Rb` | `90ab` | Ra ← Ra << (Rb & 15) | `SHL R1, R2` | `9012` |
| 9 | 1 | `SHL Ra, #n` | `91a0 xxxx` | Ra ← Ra << (n & 15) | `SHL R1, #1` | `9110 0001` |
| 9 | 2 | `SHR Ra, Rb` | `92ab` | Ra ← Ra >> (Rb & 15), logical | `SHR R1, R2` | `9212` |
| 9 | 3 | `SHR Ra, #n` | `93a0 xxxx` | Ra ← Ra >> (n & 15), logical | `SHR R1, #1` | `9310 0001` |
| A | 0 | `CMP Ra, Rb` | `A0ab` | flags of Ra − Rb; Ra unchanged | `CMP R1, R2` | `A012` |
| A | 1 | `CMP Ra, #k` | `A1a0 xxxx` | flags of Ra − k; Ra unchanged | `CMP R1, #0` | `A110 0000` |
| B | 0 | `JMP addr` | `B000 xxxx` | PC ← addr | `JMP 0x0010` | `B000 0010` |
| B | 1 | `JEQ addr` | `B100 xxxx` | if Z: PC ← addr | `JEQ 0x0010` | `B100 0010` |
| B | 2 | `JNE addr` | `B200 xxxx` | if not Z | `JNE 0x0010` | `B200 0010` |
| B | 3 | `JLT addr` | `B300 xxxx` | if C (borrow) | `JLT 0x0010` | `B300 0010` |
| B | 4 | `JGE addr` | `B400 xxxx` | if not C | `JGE 0x0010` | `B400 0010` |
| B | 5 | `JGT addr` | `B500 xxxx` | if not C and not Z | `JGT 0x0010` | `B500 0010` |
| B | 6 | `JLE addr` | `B600 xxxx` | if C or Z | `JLE 0x0010` | `B600 0010` |
| E–F | — | — | — | reserved: illegal instruction | — | — |

Any `MODE` not listed for an `OP` is an illegal instruction.

## 4. Execution rules (for `t13sim`)

1. **Fetch:** IR ← M[PC]. If `isa_length(IR)` is 4, also fetch EXT ← M[PC+2]. Then PC ← PC + length, before execute. So `CALL` pushes the address of the instruction after it.
2. **Decode:** `isa_decode()`. Any error is an illegal instruction: the simulator stops and reports PC and the word.
3. **Memory operands** are 16-bit words. The effective address is computed modulo 2¹⁶ and must be even and below `0x0400`, or the simulator stops.
4. **Flags** (only OP 2–A change them):

   | Flag | Meaning |
   |---|---|
   | Z | result == 0 |
   | N | bit 15 of the result |
   | C | ADD: carry out of bit 15 · SUB, CMP: borrow (Ra < s, unsigned) · MUL: product > `0xFFFF` · DIV, AND, OR, XOR: 0 · SHL/SHR: last bit shifted out (0 if count is 0) |

5. **DIV by zero** stops the simulator with an error.
6. **Jump conditions are unsigned** (they read Z and C; N is informational). So `8! = 40320 = 0x9D80` compares correctly.

## 5. Registers

| Register | Role | Saved by |
|---|---|---|
| `R0` | accumulator (the class design's "R0") | caller |
| `R1`–`R3` | scratch | caller |
| `R4`–`R12` | general | callee |
| `R13` = `FP` | frame pointer | callee (prologue) |
| `R14` = `SP` | stack pointer, starts at `0x0400`, grows down | — |
| `R15` | reserved | — |
| `PC` | program counter (special) | — |
| `IR` | instruction register: word + EXT (special) | — |
| `FLAGS` | Z N C (special) | — |

Listings print `R13` and `R14` as `FP` and `SP`. The full calling convention (frame layout, prologue, epilogue) goes in `docs/abi.md`.

## 6. Memory map (1 KiB)

| Segment | Addresses | Holds | Grows |
|---|---|---|---|
| Text | `0000`–`00FF` | code | — |
| Data | `0100`–`017F` | globals and statics | — |
| Heap | `0180`–`027F` | `malloc`-style blocks (bump pointer kept in Data) | up from `0180` |
| Stack | `0280`–`03FF` | frames | down from SP = `0400` |

The simulator stops with an error if anything writes into Text or SP drops below `0280`.

## 7. Pseudo-instructions (expanded by `t13as`)

| Pseudo | Expands to | Note |
|---|---|---|
| `NOT Ra` | `XOR Ra, #0xFFFF` | sets flags |
| `PUSH Ra` | `SUB SP, #2` · `MOV [SP], Ra` | **changes flags**: never between `CMP` and a conditional jump |
| `POP Ra` | `MOV Ra, [SP]` · `ADD SP, #2` | **changes flags**, same caution |

## 8. Assembly syntax

- One instruction per line. `;` starts a comment. `label:` defines a label.
- Registers: `R0`–`R15`, `FP`, `SP`.
- Constants: `#5`, `#0x10`. Offsets: `[FP+4]`. Absolute addresses and jump targets: `0x0100` or a label.
- `isa_disasm()` prints exactly this syntax. Directives (`.text`, `.data`, `.word`, `.equ`) are specified with the assembler.

## 9. Open points

1. **XOR vs NOT.** The Oct 21 board (Canvas class notes, "20261021 ISA Design 01") crossed out XOR (with NOR, NEG and PRC) and kept `OR / AND / NOT`. This draft keeps the team plan's XOR at OP 8, with `NOT` as a pseudo-op. The alternative is OP 8 = `NOT Ra` (mode 0 only, `80a0`). Team to decide.
2. **I/O.** The Oct 21 board crossed out INPUT and OUTPUT (with PUSH, POP, INC, DEC). The Oct 28 board ("20261028 Input Output 01") lists "I/O special via instruction: Input, Output" under the ISA table. This draft has no I/O: results stay in a Data word. If the instructor wants I/O, OP `E` takes it (`E0a0 xxxx` = `IN Ra, port`, `E1a0 xxxx` = `OUT port, Ra`). On the list of questions for the instructor.
3. **Memory printout format** (waterfall vs full block per step) is not confirmed with the instructor yet.

**Why a 4-bit MODE field? (board homework, Oct 21: "can we reserve 1 bit instead of 2 bits to distinguish address modes?")**
One bit gives only 2 variants (register / constant). The board lists 4 ALU operand forms (`r, c` · `r, r` · `r, [r]` · `r, [r]+offset`), which needs 2 bits. MOV needs 8 forms (3 bits). A full nibble costs nothing in a 16-bit word and keeps every field on a hex digit.
