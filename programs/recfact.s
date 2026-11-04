; recfact.s -- recursive factorial, the Part 2 program: the C in
; programs/recfact.c, translated by hand. Each C line is kept as a comment
; above the instructions that implement it.
;
; Segments: Text = code; Data = n, result, heap_top; Heap = the block that
; malloc hands out (a bump allocator: heap_top starts at 0x0180);
; Stack = one 4-word frame per active call (docs/abi.md):
;
;   FP+6 RETV   return value, written by the callee
;   FP+4 ARG    k, pushed by the caller
;   FP+2 RA     return address, pushed by CALL
;   FP+0 OLDFP  caller's FP, pushed by the callee
;
; fact only uses scratch registers (R0, R1) and re-reads k from [FP+ARG]
; after the recursive call, so every frame is exactly 4 words.
;
; Expected end state: M[result] = 0x0180, M[0x0180] = 0x0078 (120),
; heap_top = 0x0182, SP = 0x0400.

        .equ OLDFP, 0           ; frame layout, offsets from FP
        .equ RA, 2
        .equ ARG, 4
        .equ RETV, 6

        .text
; int main(void) {
;     result = malloc(sizeof *result);
main:   MOV R1, [heap_top]      ; malloc(2), bump allocator:
        MOV [result], R1        ;   the block starts at heap_top
        ADD R1, #2
        MOV [heap_top], R1      ;   heap_top += 2
;     *result = fact(n);
        SUB SP, #2              ; reserve the return-value slot
        MOV R1, [n]
        PUSH R1                 ; argument k = n
        CALL fact
        ADD SP, #2              ; drop the argument
        POP R0                  ; R0 = fact(n)
        MOV R1, [result]
        MOV [R1], R0            ; store through the pointer, into the Heap
;     printf(...);              (not translated: no I/O)
;     return 0;
        HALT
; }

; uint16_t fact(uint16_t k) {
fact:   PUSH FP                 ; prologue: save the caller's FP
        MOV FP, SP              ; FP -> this frame
;     if (k <= 1)
        MOV R1, [FP+ARG]
        CMP R1, #1
        JGT recurse
;         return 1;
        MOV R0, #1
        JMP return
;     return k * fact(k - 1);
recurse: SUB R1, #1             ; k - 1
        SUB SP, #2              ; reserve the return-value slot
        PUSH R1                 ; argument k - 1
        CALL fact
        ADD SP, #2              ; drop the argument
        POP R0                  ; R0 = fact(k - 1)
        MOV R1, [FP+ARG]        ; re-read k (R1 is scratch)
        MUL R0, R1              ; k * fact(k - 1)
return: MOV [FP+RETV], R0       ; result into the caller's slot
        MOV SP, FP              ; epilogue
        POP FP
        RET
; }

        .data
n:        .word 5
result:   .word 0               ; the pointer malloc returned
heap_top: .word 0x0180          ; first free Heap byte
