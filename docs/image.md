# T13 memory image (`.img`): v1 draft

Status: **proposed 2026-11-03 for Sai Karan's approval** (he owns this format). Written up from the sample in the team plan.

| | |
|---|---|
| Producer | `t13as` (only `src/asm/image.c` writes it) |
| Consumers | `t13sim` loader (required lines); `t13view` (`SYM` lines, optional) |
| Encoding | plain ASCII text, LF line endings (loaders also accept CRLF), one record per line |
| Comments | `;` to end of line; blank lines are ignored |

## 1. Grammar

```
file    := "T13IMG 1" NL  "ENTRY " HEX4 NL  sym*  segment+
sym     := "SYM " name " " HEX4 NL           ; a label and its address; loaders MAY ignore
segment := "SEG " ("TEXT" | "DATA") " " HEX4 NL  record+
record  := HEX4 ":" (" " HEX4)+ [" ; " text] NL
HEX4    := exactly 4 hex digits
```

Writer layout: `"%04X: %-14s ; %s"` (address, words joined by one space, disassembly or data label).

## 2. Loader rules

1. A record's words go to consecutive even byte addresses starting at its `HEX4` address. Each 16-bit word is stored **little-endian**: `0000: 1110 0005` puts bytes `10 11 05 00` at `0000`–`0003`.
2. Memory not covered by any record is `00`. Loading the same address twice is an error.
3. Every address is even and below `0400`. TEXT records lie in `0000`–`00FF`, DATA records in `0100`–`017F` (`docs/isa.md` §6).
4. A `SEG` line appears only for a segment that has records. Either segment may be absent.
5. After loading: all registers 0, `SP` = `0400`, `FLAGS` = 0, `PC` = `ENTRY` (`docs/isa.md` §5).
6. Hex digits are upper case on output. Loaders accept either case.

## 3. Examples

**Example 1. `halt.s`: one instruction.** Use this for the first loader test.

```
T13IMG 1
ENTRY 0000
SYM main 0000
SEG TEXT 0000
0000: 0000           ; HALT
```

**Example 2. `programs/power.s`: 8² by repeated multiplication.**

```
T13IMG 1
ENTRY 0000
SYM main 0000
SYM loop 000C
SYM done 001E
SYM result 0100
SEG TEXT 0000
0000: 1100 0001      ; MOV R0, #1
0004: 1110 0008      ; MOV R1, #8
0008: 1120 0002      ; MOV R2, #2
000C: A120 0000      ; CMP R2, #0
0010: B100 001E      ; JEQ 0x001E
0014: 4001           ; MUL R0, R1
0016: 3120 0001      ; SUB R2, #1
001A: B000 000C      ; JMP 0x000C
001E: 1700 0100      ; MOV [0x0100], R0
0022: 0000           ; HALT
SEG DATA 0100
0100: 0000           ; result
```

Expected state after it runs. Use this to check the simulator:

| Executed instructions | `M[0100]` | `R0` | `R1` | `R2` | Z N C | `PC` | `SP` |
|---|---|---|---|---|---|---|---|
| 17 (including HALT) | `0040` (64) | `0040` | `0008` | `0000` | 1 0 0 | `0024` | `0400` |

## 4. Changing this format

Edit this file, `src/asm/image.c` and `tests/golden/*.img` in one PR. Nothing else reads or writes images.
