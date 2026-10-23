# T13 calling convention and C → assembly recipe

Status: **v1, 2026-11-07**, written up from the frame layout in the team plan (Oct 29). Sai Karan owns this format. Used by `programs/fact.s` (Part 1, Program B) and `programs/recfact.s` (Part 2).

## 1. Registers

| Register | Role | Saved by |
|---|---|---|
| `R0` | return value inside a function; accumulator | caller |
| `R1`–`R3` | scratch | caller |
| `R4`–`R12` | general | callee (push in the prologue if used) |
| `FP` (`R13`) | frame pointer: base of the current frame | callee (prologue) |
| `SP` (`R14`) | stack pointer: lowest used stack word; starts at `0400` | — |
| `R15` | reserved | — |

## 2. Stack frame

One frame per active call, 4 words (8 bytes), addressed from `FP`. Offsets are always written by name (`.equ`), never as bare numbers.

| Name (`.equ`) | Address | Holds | Written by |
|---|---|---|---|
| `RETV` = 6 | `FP+6` | return value | callee, before the epilogue |
| `ARG` = 4 | `FP+4` | the argument | caller (`PUSH`) |
| `RA` = 2 | `FP+2` | return address | `CALL` |
| `OLDFP` = 0 | `FP+0` | the caller's `FP` | callee (`PUSH FP`) |

```
 high addresses
 0400  +----------------+  <- SP before main's first call
 03FE  | RETV           |  FP+6   \
 03FC  | ARG            |  FP+4    |  frame of the first call
 03FA  | RA             |  FP+2    |  (8-byte aligned: 03F8)
 03F8  | OLDFP          |  FP+0   /   <- FP, SP after the prologue
 03F6  | RETV           |          \
 03F4  | ARG            |           | frame of the next call
 ...                                /
 0280  lowest stack word: SP below this is a stack overflow
 low addresses
```

- SP starts at `0400` and each frame is 8 bytes, so every frame starts on an 8-byte boundary. A stack dump printed 4 words per row shows exactly one frame per row, with the columns `OLDFP RA ARG RETV`.
- `main` is the entry point, not a function: it has no frame, is never called, and ends with `HALT`.
- A function that needs more than `R0`–`R3` saves `R4`–`R12` with `PUSH` after the prologue. That makes its frame bigger than 4 words; none of our programs do it.

## 3. Call sequence

| Step | Caller | Callee | SP after (first call) |
|---|---|---|---|
| 1 | `SUB SP, #2` (return-value slot) | | `03FE` |
| 2 | `PUSH Rx` (argument) | | `03FC` |
| 3 | `CALL f` (pushes the return address) | | `03FA` |
| 4 | | `PUSH FP` · `MOV FP, SP` (prologue) | `03F8` |
| 5 | | body; result → `MOV [FP+RETV], R0` | `03F8` |
| 6 | | `MOV SP, FP` · `POP FP` · `RET` (epilogue) | `03FC` |
| 7 | `ADD SP, #2` (drop the argument) | | `03FE` |
| 8 | `POP R0` (the result) | | `0400` |

`PUSH` and `POP` are `SUB`/`ADD` on `SP`, so they change the flags: never put one between `CMP` and its conditional jump.

## 4. C → assembly recipe

Translation is by hand, one C statement at a time. The C line stays in the `.s` file as a comment above its instructions. Every `.c` program is also compiled natively by `tests/programs.sh`, and its printed result must equal what `t13sim` leaves in memory.

| C | T13 assembly |
|---|---|
| global `uint16_t g = 5;` | `.data` · `g: .word 5` |
| local or parameter | a scratch register (`R0`–`R3`); reload a parameter from `[FP+ARG]` after a call |
| `x = y;` (globals) | `MOV R1, [y]` · `MOV [x], R1` |
| `r = r * n;` | `MUL R0, R1` (operands in registers) |
| `if (k <= 1) A else B` | `CMP R1, #1` · `JGT else` · *A* · `JMP end` · `else:` *B* · `end:` |
| `while (n > 1) { A }` | `loop: CMP R1, #1` · `JLE done` · *A* · `JMP loop` · `done:` |
| `y = f(x);` | §3 steps 1–3, then 7–8: `SUB SP, #2` · `PUSH R1` · `CALL f` · `ADD SP, #2` · `POP R0` · `MOV [y], R0` |
| `return e;` | value in `R0` · `MOV [FP+RETV], R0` · epilogue |
| `p = malloc(2);` | bump allocator: `MOV R1, [heap_top]` · `MOV [p], R1` · `ADD R1, #2` · `MOV [heap_top], R1` (`heap_top` is a Data word that starts at `0180`) |
| `*p = v;` | `MOV R1, [p]` · `MOV [R1], R0` |
| `printf(...)` | not translated: the T13 has no I/O; the result stays in memory |

Conditions are unsigned (`docs/isa.md` §4.6): `<` is `JLT`, `<=` is `JLE`, `>` is `JGT`, `>=` is `JGE`. To leave a block when its condition is false, jump on the opposite condition.

## 5. Memory segments

| Segment | Addresses | Our programs keep there |
|---|---|---|
| Text | `0000`–`00FF` | code (`main` first, then the functions) |
| Data | `0100`–`017F` | globals; `heap_top` |
| Heap | `0180`–`027F` | `malloc` blocks, handed out upward by `heap_top` |
| Stack | `0280`–`03FF` | frames, growing down from `0400` |

The simulator stops on any write into Text and when `SP` drops below `0280`.
