# T13 execution trace (`.trc`): v1 draft

Status: **proposed 2026-11-05 for Sai Karan's approval** (`t13sim` writes it). Owner: Manoj. Once agreed, change it only by PR (§5).

| | |
|---|---|
| Producer | `t13sim` |
| Consumer | `t13view` |
| Encoding | plain ASCII text, LF line endings (readers also accept CRLF), one record per line |
| Comments | `;` to end of line; blank lines are ignored |

A trace is everything the viewer needs: memory as loaded, then after every instruction the full register state and each memory word written. `t13view` never reads the image or reruns the program.

## 1. Grammar

```
file   := "T13TRC 1" NL  "ENTRY " HEX4 NL  sym*  load*  state  step*  end
sym    := "SYM " name " " HEX4 NL                 ; copied from the image, same order
load   := "LOAD " HEX4 ":" (" " HEX4){8} NL       ; one 16-byte row of memory after loading
step   := "STEP " DEC " PC=" HEX4 " IR=" HEX4 [" EXT=" HEX4] [" ; " text] NL  state  mem*
state  := "REGS" (" " reg "=" HEX4){16} NL  "FLAGS Z=" BIT " N=" BIT " C=" BIT NL
reg    := R0 R1 R2 R3 R4 R5 R6 R7 R8 R9 R10 R11 R12 FP SP R15, in this order
mem    := "MEM " HEX4 ": " HEX4 " -> " HEX4 NL    ; one word written: address, before, after
end    := "END " ("HALT" | "LIMIT") " PC=" HEX4 " STEPS=" DEC NL
        | "END ERROR PC=" HEX4 " STEPS=" DEC " " text NL
HEX4   := exactly 4 hex digits
DEC    := decimal, no leading zeros
BIT    := "0" | "1"
```

Writer layouts (`printf`):

| Record | Format |
|---|---|
| `LOAD` | `"LOAD %04X:"`, then `" %04X"` for each of the 8 words |
| `STEP` | `"STEP %u PC=%04X IR=%04X"`, then `" EXT=%04X"` if the instruction has one, then `" ; %s"` with the `isa_disasm()` text |
| `REGS` | `"REGS R0=%04X R1=%04X ... R12=%04X FP=%04X SP=%04X R15=%04X"` |
| `FLAGS` | `"FLAGS Z=%u N=%u C=%u"` |
| `MEM` | `"MEM %04X: %04X -> %04X"` |
| `END` | `"END HALT PC=%04X STEPS=%u"`, `"END LIMIT PC=%04X STEPS=%u"`, or `"END ERROR PC=%04X STEPS=%u %s"` |

## 2. Rules

**Before step 1**

1. `ENTRY` and the `SYM` lines are the image's own, in the image's order.
2. `LOAD` rows show memory right after loading: 8 words per row, at addresses that are multiples of `0010`, in address order. Only rows with at least one non-zero word are written. Every word not shown is 0.
3. The first `REGS` / `FLAGS` pair is the state after loading: all registers 0 except `SP` = `0400`, flags 0 (`docs/image.md` §2).

**One block per executed instruction**

4. `STEP n` counts instructions from 1, with no gaps. `PC` is the address the instruction was fetched from; `IR` and `EXT` are the words fetched. The disassembly after `;` is for people; readers ignore it.
5. `REGS` and `FLAGS` are the full state *after* the instruction, written even when nothing changed.
6. One `MEM` line per 16-bit word the instruction wrote, in the order written, with its value before and after. An instruction that writes nothing has no `MEM` line. Reads are not traced.
7. `HALT` gets a `STEP` block like any other instruction.

**End of the file**

8. Exactly one `END` line, last. `STEPS` equals the number of `STEP` blocks.

   | `END` | When | `PC` |
   |---|---|---|
   | `HALT` | `HALT` executed | the PC register after `HALT` (the next address) |
   | `ERROR` | an instruction failed (`docs/isa.md` §4, §6) | the address of the failed instruction |
   | `LIMIT` | the simulator's step limit ran out | the address of the next instruction, not run |

9. An instruction that fails gets no `STEP` block, so the last state in the trace is the one before it. The rest of the `ERROR` line is the message: `END ERROR PC=0040 STEPS=12 divide by zero`.

**Readers**

10. Hex digits are upper case on output; readers accept either case.
11. `t13view` stops with an error naming the line if the header is wrong, a step number is skipped, a `MEM` before-value differs from the memory it has rebuilt, or `STEPS` does not match.

## 3. How `t13view` uses it

| Report view | Built from |
|---|---|
| Part 1 memory waterfall: one row per instruction, changed cells shaded | one row per `STEP`; cells from `LOAD` plus every `MEM` so far; shaded cells are this step's `MEM` lines |
| Full memory grid at the start and the end | `LOAD` rows; the same rows after the last step |
| Part 2 Text / Data / Heap / Stack snapshot at each `CALL`, `RET` and `HALT` | `STEP` blocks whose `IR` is `C000` (CALL), `D000` (RET) or `0000` (HALT) |
| Names such as `main` and `fact` on addresses and `CALL` targets | `SYM` lines |

There are no separate event lines for `CALL` or `RET`: the `IR` already says which instruction ran, and a second record could disagree with it.

## 4. Examples

**Example 1. `halt.s`: one instruction.** `HALT` is the word `0000`, so memory is all zero and there are no `LOAD` rows. Golden file: [`tests/golden/halt.trc`](../tests/golden/halt.trc).

```
T13TRC 1
ENTRY 0000
SYM main 0000
REGS R0=0000 R1=0000 R2=0000 R3=0000 R4=0000 R5=0000 R6=0000 R7=0000 R8=0000 R9=0000 R10=0000 R11=0000 R12=0000 FP=0000 SP=0400 R15=0000
FLAGS Z=0 N=0 C=0
STEP 1 PC=0000 IR=0000 ; HALT
REGS R0=0000 R1=0000 R2=0000 R3=0000 R4=0000 R5=0000 R6=0000 R7=0000 R8=0000 R9=0000 R10=0000 R11=0000 R12=0000 FP=0000 SP=0400 R15=0000
FLAGS Z=0 N=0 C=0
END HALT PC=0002 STEPS=1
```

**Example 2. A call and a return.** `CALL` moves `SP` from `0400` to `03FE` and pushes the return address `0004`, which shows as a `MEM` line. `RET` only reads it back, so it has no `MEM` line and `03FE` keeps `0004`.

Image:

```
T13IMG 1
ENTRY 0000
SYM main 0000
SYM f 0006
SEG TEXT 0000
0000: C000 0006      ; CALL 0x0006
0004: 0000           ; HALT
0006: D000           ; RET
```

Trace:

```
T13TRC 1
ENTRY 0000
SYM main 0000
SYM f 0006
LOAD 0000: C000 0006 0000 D000 0000 0000 0000 0000
REGS R0=0000 R1=0000 R2=0000 R3=0000 R4=0000 R5=0000 R6=0000 R7=0000 R8=0000 R9=0000 R10=0000 R11=0000 R12=0000 FP=0000 SP=0400 R15=0000
FLAGS Z=0 N=0 C=0
STEP 1 PC=0000 IR=C000 EXT=0006 ; CALL 0x0006
REGS R0=0000 R1=0000 R2=0000 R3=0000 R4=0000 R5=0000 R6=0000 R7=0000 R8=0000 R9=0000 R10=0000 R11=0000 R12=0000 FP=0000 SP=03FE R15=0000
FLAGS Z=0 N=0 C=0
MEM 03FE: 0000 -> 0004
STEP 2 PC=0006 IR=D000 ; RET
REGS R0=0000 R1=0000 R2=0000 R3=0000 R4=0000 R5=0000 R6=0000 R7=0000 R8=0000 R9=0000 R10=0000 R11=0000 R12=0000 FP=0000 SP=0400 R15=0000
FLAGS Z=0 N=0 C=0
STEP 3 PC=0004 IR=0000 ; HALT
REGS R0=0000 R1=0000 R2=0000 R3=0000 R4=0000 R5=0000 R6=0000 R7=0000 R8=0000 R9=0000 R10=0000 R11=0000 R12=0000 FP=0000 SP=0400 R15=0000
FLAGS Z=0 N=0 C=0
END HALT PC=0006 STEPS=3
```

**Example 3. `programs/power.s`: 8² in 17 steps.** Golden file: [`tests/golden/power.trc`](../tests/golden/power.trc). The start and the last two steps, where the only memory write happens:

```
T13TRC 1
ENTRY 0000
SYM main 0000
SYM loop 000C
SYM done 001E
SYM result 0100
LOAD 0000: 1100 0001 1110 0008 1120 0002 A120 0000
LOAD 0010: B100 001E 4001 3120 0001 B000 000C 1700
LOAD 0020: 0100 0000 0000 0000 0000 0000 0000 0000
REGS R0=0000 R1=0000 R2=0000 R3=0000 R4=0000 R5=0000 R6=0000 R7=0000 R8=0000 R9=0000 R10=0000 R11=0000 R12=0000 FP=0000 SP=0400 R15=0000
FLAGS Z=0 N=0 C=0
STEP 1 PC=0000 IR=1100 EXT=0001 ; MOV R0, #1
...
STEP 16 PC=001E IR=1700 EXT=0100 ; MOV [0x0100], R0
REGS R0=0040 R1=0008 R2=0000 R3=0000 R4=0000 R5=0000 R6=0000 R7=0000 R8=0000 R9=0000 R10=0000 R11=0000 R12=0000 FP=0000 SP=0400 R15=0000
FLAGS Z=1 N=0 C=0
MEM 0100: 0000 -> 0040
STEP 17 PC=0022 IR=0000 ; HALT
REGS R0=0040 R1=0008 R2=0000 R3=0000 R4=0000 R5=0000 R6=0000 R7=0000 R8=0000 R9=0000 R10=0000 R11=0000 R12=0000 FP=0000 SP=0400 R15=0000
FLAGS Z=1 N=0 C=0
END HALT PC=0024 STEPS=17
```

The end state is the one `docs/image.md` §3 expects: `M[0100]` = `0040`, `R0` = `0040`, `R1` = `0008`, `R2` = `0000`, Z N C = 1 0 0, `PC` = `0024`, `SP` = `0400`.

## 5. Changing this format

Edit this file, the trace writer in `src/sim/`, the reader in `src/view/` and `tests/golden/*.trc` in one PR, and tell the others in chat.
