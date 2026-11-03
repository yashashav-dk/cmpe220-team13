/*
 * isa.c -- encoder / decoder / disassembler for the T13 16-bit ISA.
 *
 * Everything here is driven by the opcode table in include/isa_ops.def,
 * so adding or changing an operation means editing that one line.
 * See docs/isa.md for the specification and include/isa.h for the API.
 */
#include "isa.h"

#include <stdio.h>

/* One row of isa_ops.def. */
typedef struct {
    const char *name;
    uint8_t nmodes;
    uint16_t ext_mask; /* bit m set: mode m takes an EXT word */
    uint16_t ra_mask;  /* bit m set: mode m reads Ra          */
    uint16_t rb_mask;  /* bit m set: mode m reads Rb          */
    uint8_t flags;     /* 1 if the op updates Z N C           */
} op_info;

/* Indexed by op code; rows not in isa_ops.def (0xE, 0xF) stay zeroed. */
static const op_info OPS[16] = {
#define OP(code, name, nmodes, ext, ra, rb, flags) \
    [code] = {#name, (nmodes), (ext), (ra), (rb), (flags)},
#include "isa_ops.def"
#undef OP
};

/* The row for `op`, or NULL if op is reserved or out of range. */
static const op_info *lookup(unsigned op)
{
    if (op >= 16 || OPS[op].name == NULL)
        return NULL;
    return &OPS[op];
}

/* Status of the fields of an instruction word, without looking at EXT. */
static isa_status check_fields(unsigned op, unsigned mode, unsigned ra, unsigned rb)
{
    const op_info *info = lookup(op);
    uint16_t bit;

    if (info == NULL)
        return ISA_ERR_OPCODE;
    if (mode >= info->nmodes)
        return ISA_ERR_MODE;
    if (ra >= REG_COUNT || rb >= REG_COUNT)
        return ISA_ERR_REG;
    bit = (uint16_t)(1u << mode);
    if ((!(info->ra_mask & bit) && ra != 0) || (!(info->rb_mask & bit) && rb != 0))
        return ISA_ERR_FIELD;
    return ISA_OK;
}

int isa_needs_ext(unsigned op, unsigned mode)
{
    const op_info *info = lookup(op);

    if (info == NULL || mode >= info->nmodes)
        return -1;
    return (info->ext_mask >> mode) & 1u;
}

unsigned isa_length(uint16_t word)
{
    unsigned op = (word >> 12) & 0xFu;
    unsigned mode = (word >> 8) & 0xFu;
    unsigned ra = (word >> 4) & 0xFu;
    unsigned rb = word & 0xFu;

    if (check_fields(op, mode, ra, rb) != ISA_OK)
        return 0;
    return isa_needs_ext(op, mode) ? 4u : 2u;
}

isa_status isa_encode(const isa_insn *in, uint8_t *out, size_t cap, size_t *len)
{
    isa_status st = check_fields(in->op, in->mode, in->ra, in->rb);
    uint16_t word;
    size_t need;

    if (st != ISA_OK)
        return st;
    need = isa_needs_ext(in->op, in->mode) ? 4u : 2u;
    if (cap < need)
        return ISA_ERR_SPACE;

    word = (uint16_t)((in->op << 12) | (in->mode << 8) | (in->ra << 4) | in->rb);
    out[0] = (uint8_t)(word & 0xFFu); /* little-endian: low byte first */
    out[1] = (uint8_t)(word >> 8);
    if (need == 4) {
        out[2] = (uint8_t)(in->ext & 0xFFu);
        out[3] = (uint8_t)(in->ext >> 8);
    }
    *len = need;
    return ISA_OK;
}

isa_status isa_decode(const uint8_t *mem, size_t avail, isa_insn *out, size_t *len)
{
    uint16_t word;
    isa_status st;

    if (avail < 2)
        return ISA_ERR_TRUNC;
    word = (uint16_t)(mem[0] | (mem[1] << 8));
    out->op = (uint8_t)((word >> 12) & 0xFu);
    out->mode = (uint8_t)((word >> 8) & 0xFu);
    out->ra = (uint8_t)((word >> 4) & 0xFu);
    out->rb = (uint8_t)(word & 0xFu);
    out->ext = 0;
    out->has_ext = 0;

    st = check_fields(out->op, out->mode, out->ra, out->rb);
    if (st != ISA_OK)
        return st;

    if (isa_needs_ext(out->op, out->mode)) {
        if (avail < 4)
            return ISA_ERR_TRUNC;
        out->ext = (uint16_t)(mem[2] | (mem[3] << 8));
        out->has_ext = 1;
        *len = 4;
    } else {
        *len = 2;
    }
    return ISA_OK;
}

/* Register name as printed in listings: R13/R14 use their ABI names. */
static const char *reg(unsigned r)
{
    static const char *const names[REG_COUNT] = {
        "R0", "R1", "R2", "R3", "R4", "R5", "R6", "R7",
        "R8", "R9", "R10", "R11", "R12", "FP", "SP", "R15",
    };
    return r < REG_COUNT ? names[r] : "R?";
}

/*
 * Operand text for the four forms shared by MOV 0-3, the ALU ops, SHIFT
 * and CMP:  0 "Ra, Rb"   1 "Ra, #EXT"   2 "Ra, [Rb]"   3 "Ra, [Rb+EXT]".
 */
static int two_operand(const char *mn, unsigned form, const isa_insn *in, char *buf, size_t cap)
{
    switch (form) {
    case 0: return snprintf(buf, cap, "%s %s, %s", mn, reg(in->ra), reg(in->rb));
    case 1: return snprintf(buf, cap, "%s %s, #%u", mn, reg(in->ra), (unsigned)in->ext);
    case 2: return snprintf(buf, cap, "%s %s, [%s]", mn, reg(in->ra), reg(in->rb));
    case 3: return snprintf(buf, cap, "%s %s, [%s+%u]", mn, reg(in->ra), reg(in->rb), (unsigned)in->ext);
    default: return snprintf(buf, cap, "???");
    }
}

int isa_disasm(const isa_insn *in, char *buf, size_t cap)
{
    static const char *const jmp_names[] = {"JMP", "JEQ", "JNE", "JLT", "JGE", "JGT", "JLE"};
    const op_info *info = lookup(in->op);
    unsigned m = in->mode;

    if (info == NULL || m >= info->nmodes)
        return snprintf(buf, cap, "???");

    switch (in->op) {
    case OP_SYS:
        return snprintf(buf, cap, "%s", m == SYS_HALT ? "HALT" : "NOP");
    case OP_MOV:
        switch (m) {
        case 4: return snprintf(buf, cap, "MOV [%s], %s", reg(in->ra), reg(in->rb));
        case 5: return snprintf(buf, cap, "MOV [%s+%u], %s", reg(in->ra), (unsigned)in->ext, reg(in->rb));
        case 6: return snprintf(buf, cap, "MOV %s, [0x%04X]", reg(in->ra), (unsigned)in->ext);
        case 7: return snprintf(buf, cap, "MOV [0x%04X], %s", (unsigned)in->ext, reg(in->ra));
        default: return two_operand("MOV", m, in, buf, cap);
        }
    case OP_SHIFT:
        /* modes 0/1 shift left, 2/3 shift right; even = register, odd = #EXT */
        return two_operand(m < 2 ? "SHL" : "SHR", m & 1u, in, buf, cap);
    case OP_JMP:
        return snprintf(buf, cap, "%s 0x%04X", jmp_names[m], (unsigned)in->ext);
    case OP_CALL:
        return snprintf(buf, cap, "CALL 0x%04X", (unsigned)in->ext);
    case OP_RET:
        return snprintf(buf, cap, "RET");
    default: /* ADD .. XOR and CMP: forms 0-3 by mode */
        return two_operand(info->name, m, in, buf, cap);
    }
}

const char *isa_op_name(unsigned op)
{
    const op_info *info = lookup(op);
    return info ? info->name : "???";
}

int isa_sets_flags(unsigned op)
{
    const op_info *info = lookup(op);
    return info ? info->flags : 0;
}

const char *isa_strerror(isa_status st)
{
    switch (st) {
    case ISA_OK: return "ok";
    case ISA_ERR_OPCODE: return "illegal instruction (reserved opcode)";
    case ISA_ERR_MODE: return "illegal instruction (undefined mode for this opcode)";
    case ISA_ERR_REG: return "register number out of range (0-15)";
    case ISA_ERR_FIELD: return "illegal instruction (unused register field is not 0)";
    case ISA_ERR_TRUNC: return "instruction runs past the end of memory";
    case ISA_ERR_SPACE: return "output buffer too small";
    }
    return "unknown error";
}
