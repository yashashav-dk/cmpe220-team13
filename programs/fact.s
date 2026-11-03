; fact.s -- 8! by a loop inside a function. Program B for Part 1: the C in
; programs/fact.c, translated by hand. Each C line is kept as a comment
; above the instructions that implement it.
;
; Calling convention (docs/abi.md): the caller reserves the return-value
; slot and pushes the argument; the callee builds a 4-word frame with
; PUSH FP / MOV FP, SP and leaves its result in [FP+RETV].
;
; Expected end state (57 executed instructions, including HALT):
;   M[result] = M[0x0100] = 0x9D80 (40320)   R0 = 0x9D80   SP = 0x0400

        .equ OLDFP, 0           ; frame layout, offsets from FP
        .equ RA, 2
        .equ ARG, 4
        .equ RETV, 6

        .text
; int main(void) {
;     result = fact(8);
main:   SUB SP, #2              ; reserve the return-value slot
        MOV R1, #8
        PUSH R1                 ; argument n = 8
        CALL fact
        ADD SP, #2              ; drop the argument
        POP R0                  ; R0 = fact(8)
        MOV [result], R0
;     printf(...);              (not translated: no I/O)
;     return 0;
        HALT
; }

; uint16_t fact(uint16_t n) {
fact:   PUSH FP                 ; prologue: save the caller's FP
        MOV FP, SP              ; FP -> this frame
;     uint16_t r = 1;
        MOV R0, #1              ; r lives in R0
        MOV R1, [FP+ARG]        ; n lives in R1
;     while (n > 1) {
loop:   CMP R1, #1
        JLE done                ; unsigned n <= 1: leave the loop
;         r = r * n;
        MUL R0, R1
;         n = n - 1;
        SUB R1, #1
        JMP loop
;     }
;     return r;
done:   MOV [FP+RETV], R0       ; result into the caller's slot
        MOV SP, FP              ; epilogue
        POP FP
        RET
; }

        .data
result: .word 0                 ; becomes 0x9D80 = 40320
