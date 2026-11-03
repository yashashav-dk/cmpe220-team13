/*
 * isa.h -- encoder / decoder / disassembler for the T13 16-bit ISA.
 *
 * Instruction word (one 16-bit word, stored little-endian):
 *
 *     15    12 11     8 7      4 3      0
 *     +-------+--------+--------+--------+
 *     |  OP   |  MODE  |   Ra   |   Rb   |   [+ one EXT word in some modes]
 *     +-------+--------+--------+--------+
 *
 * Every field sits on a hex digit, so 0x2012 reads as ADD(2), mode 0,
 * R1, R2. The full specification is docs/isa.md; the opcode table is
 * include/isa_ops.def.
 *
 * This file is the only code shared by t13as, t13sim and t13view.
 * Pseudo-instructions (NOT, PUSH, POP) are the assembler's job, not ours.
 */
#ifndef T13_ISA_H
#define T13_ISA_H

#include <stddef.h>
#include <stdint.h>

/* Operation codes, generated from isa_ops.def: OP_SYS = 0x0 ... OP_RET = 0xD. */
typedef enum {
#define OP(code, name, nmodes, ext, ra, rb, flags) OP_##name = (code),
#include "isa_ops.def"
#undef OP
} isa_op;

/* Register numbers with a fixed role in the calling convention. */
enum {
    REG_ACC = 0,  /* R0: accumulator                         */
    REG_FP = 13,  /* R13: frame pointer                      */
    REG_SP = 14,  /* R14: stack pointer                      */
    REG_COUNT = 16
};

/* Modes of SYS. */
enum { SYS_HALT = 0, SYS_NOP = 1 };

/* Modes of SHIFT. */
enum { SHIFT_SHL_R = 0, SHIFT_SHL_I = 1, SHIFT_SHR_R = 2, SHIFT_SHR_I = 3 };

/* Modes (conditions) of JMP. */
enum { JMP_ALWAYS = 0, JMP_EQ = 1, JMP_NE = 2, JMP_LT = 3, JMP_GE = 4, JMP_GT = 5, JMP_LE = 6 };

/* One decoded instruction. ext is meaningful only when has_ext is 1. */
typedef struct {
    uint8_t op;      /* 0..15 */
    uint8_t mode;    /* 0..15 */
    uint8_t ra;      /* 0..15 */
    uint8_t rb;      /* 0..15 */
    uint16_t ext;    /* constant, offset or address */
    uint8_t has_ext; /* 1 if an EXT word follows the instruction word */
} isa_insn;

typedef enum {
    ISA_OK = 0,
    ISA_ERR_OPCODE, /* OP is 0xE/0xF (reserved) or out of range      */
    ISA_ERR_MODE,   /* MODE is not defined for this OP               */
    ISA_ERR_REG,    /* a register number is above 15                 */
    ISA_ERR_FIELD,  /* a register field this mode ignores is not 0   */
    ISA_ERR_TRUNC,  /* not enough bytes for the word or its EXT word */
    ISA_ERR_SPACE   /* output buffer too small                       */
} isa_status;

/* 1 if (op, mode) is legal and takes an EXT word, 0 if legal without one,
 * -1 if (op, mode) is illegal. */
int isa_needs_ext(unsigned op, unsigned mode);

/* Instruction length in bytes from the first word alone: 2, 4, or 0 if the
 * word is illegal. Lets the simulator know whether to fetch an EXT word. */
unsigned isa_length(uint16_t word);

/* Validate `in` and write it as little-endian bytes into out[0..cap).
 * has_ext in `in` is ignored: the (op, mode) pair decides it.
 * On ISA_OK, *len is 2 or 4. */
isa_status isa_encode(const isa_insn *in, uint8_t *out, size_t cap, size_t *len);

/* Decode the instruction at mem[0..avail). On ISA_OK, *len is 2 or 4. */
isa_status isa_decode(const uint8_t *mem, size_t avail, isa_insn *out, size_t *len);

/* Write the assembly text of `in` into buf, e.g. "ADD R1, R2" or
 * "MOV R3, [FP+4]". Returns what snprintf returns (chars needed, or < 0). */
int isa_disasm(const isa_insn *in, char *buf, size_t cap);

/* "ADD", "MOV", ... for a legal op code; "???" otherwise. */
const char *isa_op_name(unsigned op);

/* 1 if the op updates FLAGS. */
int isa_sets_flags(unsigned op);

/* Human-readable text for a status code. */
const char *isa_strerror(isa_status st);

#endif /* T13_ISA_H */
