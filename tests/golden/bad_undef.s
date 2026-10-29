; bad_undef.s -- error-path fixture: jumps to a label that does not exist.
        .text
main:   NOP
        JMP lop                 ; typo for "loop": t13as must exit 1 and write nothing
