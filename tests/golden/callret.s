; callret.s -- one CALL and one RET (docs/trace.md, Example 2).
        .text
main:   CALL f
        HALT
f:      RET
