; power.s -- BASE^EXP by repeated multiplication. Program A for Part 1,
; written directly in T13 assembly.
;
; Same as the C:  r = 1; e = EXP; while (e != 0) { r = r * BASE; e = e - 1; }  result = r;
;
; Expected end state (17 executed instructions, including HALT):
;   M[0x0100] = 0x0040 (64)   R0 = 0x0040   R1 = 0x0008   R2 = 0x0000
;   FLAGS Z=1 N=0 C=0 (left by the last CMP)   PC = 0x0024   SP = 0x0400

        .equ BASE, 8            ; compute BASE^EXP = 8^2
        .equ EXP, 2

        .text
main:   MOV R0, #1              ; R0 = result, starts at 1
        MOV R1, #BASE           ; R1 = base
        MOV R2, #EXP            ; R2 = exponent left
loop:   CMP R2, #0              ; done when the exponent reaches 0
        JEQ done
        MUL R0, R1              ; result *= base
        SUB R2, #1              ; exponent -= 1
        JMP loop
done:   MOV [result], R0        ; store the answer in the Data word
        HALT

        .data
result: .word 0                 ; becomes 0x0040 = 64
