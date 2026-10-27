/*
 * select.c -- pick the machine instruction for a parsed statement.
 *
 * FORMS below lists every (mnemonic, operand kinds) pair the ISA accepts and
 * the (OP, MODE) it maps to: one row per line of docs/isa.md Tables 1-4.
 * The operand fields are then filled by one rule for every row:
 *
 *   - registers fill Ra first, then Rb, in operand order
 *     (so "MOV [0x0102], R5" puts R5 in Ra, as mode 7 requires);
 *   - EXT gets the value of whichever operand carries one
 *     (#k, the offset of [Rb+k], the address of [addr], or a jump target).
 *
 * Symbol operands still hold 0 here; assemble.c resolves them before
 * calling select_insns() a second time in pass 2.
 * NOT, PUSH and POP are pseudo-instructions, expanded in select_pseudo().
 */
#include <stdio.h>
#include <string.h>

#include "asm.h"

typedef struct {
    const char *mnem;
    int nopd;
    opd_kind kinds[2];
    uint8_t op;
    uint8_t mode;
} form;

/* ADD .. XOR all take the same four source forms (docs/isa.md Table 3). */
#define ALU_FORMS(name, op)                                  \
    {name, 2, {OPD_REG, OPD_REG}, op, 0},                    \
    {name, 2, {OPD_REG, OPD_IMM}, op, 1},                    \
    {name, 2, {OPD_REG, OPD_MEM_REG}, op, 2},                \
    {name, 2, {OPD_REG, OPD_MEM_REG_OFF}, op, 3}

#define NO_OPERANDS {OPD_REG, OPD_REG} /* ignored when nopd is 0 */

static const form FORMS[] = {
    {"HALT", 0, NO_OPERANDS, OP_SYS, SYS_HALT},
    {"NOP", 0, NO_OPERANDS, OP_SYS, SYS_NOP},

    {"MOV", 2, {OPD_REG, OPD_REG}, OP_MOV, 0},         /* Ra <- Rb       */
    {"MOV", 2, {OPD_REG, OPD_IMM}, OP_MOV, 1},         /* Ra <- #k       */
    {"MOV", 2, {OPD_REG, OPD_MEM_REG}, OP_MOV, 2},     /* Ra <- [Rb]     */
    {"MOV", 2, {OPD_REG, OPD_MEM_REG_OFF}, OP_MOV, 3}, /* Ra <- [Rb+k]   */
    {"MOV", 2, {OPD_MEM_REG, OPD_REG}, OP_MOV, 4},     /* [Ra] <- Rb     */
    {"MOV", 2, {OPD_MEM_REG_OFF, OPD_REG}, OP_MOV, 5}, /* [Ra+k] <- Rb   */
    {"MOV", 2, {OPD_REG, OPD_MEM_ABS}, OP_MOV, 6},     /* Ra <- [addr]   */
    {"MOV", 2, {OPD_MEM_ABS, OPD_REG}, OP_MOV, 7},     /* [addr] <- Ra   */

    ALU_FORMS("ADD", OP_ADD),
    ALU_FORMS("SUB", OP_SUB),
    ALU_FORMS("MUL", OP_MUL),
    ALU_FORMS("DIV", OP_DIV),
    ALU_FORMS("AND", OP_AND),
    ALU_FORMS("OR", OP_OR),
    ALU_FORMS("XOR", OP_XOR),

    {"SHL", 2, {OPD_REG, OPD_REG}, OP_SHIFT, SHIFT_SHL_R},
    {"SHL", 2, {OPD_REG, OPD_IMM}, OP_SHIFT, SHIFT_SHL_I},
    {"SHR", 2, {OPD_REG, OPD_REG}, OP_SHIFT, SHIFT_SHR_R},
    {"SHR", 2, {OPD_REG, OPD_IMM}, OP_SHIFT, SHIFT_SHR_I},

    {"CMP", 2, {OPD_REG, OPD_REG}, OP_CMP, 0},
    {"CMP", 2, {OPD_REG, OPD_IMM}, OP_CMP, 1},

    {"JMP", 1, {OPD_VALUE, OPD_VALUE}, OP_JMP, JMP_ALWAYS},
    {"JEQ", 1, {OPD_VALUE, OPD_VALUE}, OP_JMP, JMP_EQ},
    {"JNE", 1, {OPD_VALUE, OPD_VALUE}, OP_JMP, JMP_NE},
    {"JLT", 1, {OPD_VALUE, OPD_VALUE}, OP_JMP, JMP_LT},
    {"JGE", 1, {OPD_VALUE, OPD_VALUE}, OP_JMP, JMP_GE},
    {"JGT", 1, {OPD_VALUE, OPD_VALUE}, OP_JMP, JMP_GT},
    {"JLE", 1, {OPD_VALUE, OPD_VALUE}, OP_JMP, JMP_LE},

    {"CALL", 1, {OPD_VALUE, OPD_VALUE}, OP_CALL, 0},
    {"RET", 0, NO_OPERANDS, OP_RET, 0},
};

#define NFORMS (sizeof FORMS / sizeof FORMS[0])

static const char *const PSEUDOS[] = {"NOT", "PUSH", "POP"};

static int has_register(opd_kind k)
{
    return k == OPD_REG || k == OPD_MEM_REG || k == OPD_MEM_REG_OFF;
}

static int has_value(opd_kind k)
{
    return k == OPD_IMM || k == OPD_MEM_REG_OFF || k == OPD_MEM_ABS || k == OPD_VALUE;
}

/* Build one instruction from (op, mode) and the statement's operands. */
static void fill(const stmt *s, uint8_t op, uint8_t mode, isa_insn *out)
{
    int i, nregs = 0;

    memset(out, 0, sizeof *out);
    out->op = op;
    out->mode = mode;
    for (i = 0; i < s->nopd; i++) {
        const operand *o = &s->opd[i];
        if (has_register(o->kind)) {
            if (nregs++ == 0)
                out->ra = o->reg;
            else
                out->rb = o->reg;
        }
        if (has_value(o->kind))
            out->ext = o->value;
    }
    out->has_ext = (uint8_t)(isa_needs_ext(op, mode) == 1);
}

/* A hand-built instruction for the pseudo-op expansions. */
static isa_insn insn(uint8_t op, uint8_t mode, uint8_t ra, uint8_t rb, uint16_t ext)
{
    isa_insn in;
    in.op = op;
    in.mode = mode;
    in.ra = ra;
    in.rb = rb;
    in.ext = ext;
    in.has_ext = (uint8_t)(isa_needs_ext(op, mode) == 1);
    return in;
}

/* NOT, PUSH, POP (docs/isa.md section 7). Count, or -1 with err. */
static int select_pseudo(const stmt *s, isa_insn out[2], char *err, size_t errcap)
{
    uint8_t r;

    if (s->nopd != 1 || s->opd[0].kind != OPD_REG) {
        snprintf(err, errcap, "bad operands for '%s'", s->mnem);
        return -1;
    }
    r = s->opd[0].reg;
    if (strcmp(s->mnem, "NOT") == 0) { /* XOR Ra, #0xFFFF */
        out[0] = insn(OP_XOR, 1, r, 0, 0xFFFF);
        return 1;
    }
    if (strcmp(s->mnem, "PUSH") == 0) { /* SUB SP, #2 ; MOV [SP], Ra */
        out[0] = insn(OP_SUB, 1, REG_SP, 0, 2);
        out[1] = insn(OP_MOV, 4, REG_SP, r, 0);
        return 2;
    }
    /* POP: MOV Ra, [SP] ; ADD SP, #2 */
    out[0] = insn(OP_MOV, 2, r, REG_SP, 0);
    out[1] = insn(OP_ADD, 1, REG_SP, 0, 2);
    return 2;
}

static int is_pseudo(const char *name)
{
    size_t i;
    for (i = 0; i < sizeof PSEUDOS / sizeof PSEUDOS[0]; i++)
        if (strcmp(name, PSEUDOS[i]) == 0)
            return 1;
    return 0;
}

int select_is_mnemonic(const char *upper_name)
{
    size_t i;

    if (is_pseudo(upper_name))
        return 1;
    for (i = 0; i < NFORMS; i++)
        if (strcmp(upper_name, FORMS[i].mnem) == 0)
            return 1;
    return 0;
}

int select_insns(const stmt *s, isa_insn out[2], char *err, size_t errcap)
{
    size_t i;
    int known = 0;

    if (is_pseudo(s->mnem))
        return select_pseudo(s, out, err, errcap);

    for (i = 0; i < NFORMS; i++) {
        const form *f = &FORMS[i];
        int k, match;

        if (strcmp(s->mnem, f->mnem) != 0)
            continue;
        known = 1;
        match = (s->nopd == f->nopd);
        for (k = 0; match && k < f->nopd; k++)
            match = (s->opd[k].kind == f->kinds[k]);
        if (match) {
            fill(s, f->op, f->mode, &out[0]);
            return 1;
        }
    }
    if (known)
        snprintf(err, errcap, "bad operands for '%s'", s->mnem);
    else
        snprintf(err, errcap, "unknown mnemonic '%s'", s->mnem);
    return -1;
}
