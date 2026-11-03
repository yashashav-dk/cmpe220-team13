/*
 * test_asm.c -- unit tests for the assembler front end:
 * parse.c (numbers, registers, lines, operands), symtab.c and select.c.
 *
 * Same plain-C style as test_isa.c. Run with `make test`.
 */
#include <stdio.h>
#include <string.h>

#include "asm.h"

static int checks = 0;
static int failures = 0;

#define CHECK(cond)                                                      \
    do {                                                                 \
        checks++;                                                        \
        if (!(cond)) {                                                   \
            failures++;                                                  \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
        }                                                                \
    } while (0)

#define CHECK_STR(got, want)                                             \
    do {                                                                 \
        checks++;                                                        \
        if (strcmp((got), (want)) != 0) {                                \
            failures++;                                                  \
            printf("FAIL %s:%d: got \"%s\", want \"%s\"\n", __FILE__,    \
                   __LINE__, (got), (want));                             \
        }                                                                \
    } while (0)

static stmt S;
static char err[128];
#define PARSE(txt) parse_line((txt), 1, &S, err, sizeof err)

/* ---- parse_number ---------------------------------------------------- */

static int num_ok(const char *text, unsigned want)
{
    uint16_t v = 0;
    return parse_number(text, &v) == 1 && v == want;
}

static int num_bad(const char *text)
{
    uint16_t v = 0;
    return parse_number(text, &v) == 0;
}

static void test_parse_number(void)
{
    CHECK(num_ok("5", 5));
    CHECK(num_ok("0", 0));
    CHECK(num_ok("0x1F", 31));
    CHECK(num_ok("0X1f", 31));
    CHECK(num_ok("-1", 0xFFFF));
    CHECK(num_ok("-32768", 0x8000));
    CHECK(num_ok("65535", 65535));
    CHECK(num_ok("0xFFFF", 0xFFFF));
    CHECK(num_bad("65536"));
    CHECK(num_bad("0x10000"));
    CHECK(num_bad("-32769"));
    CHECK(num_bad("12abc"));
    CHECK(num_bad(""));
    CHECK(num_bad("0x"));
    CHECK(num_bad("--1"));
    CHECK(num_bad("-"));
    CHECK(num_bad(" 5"));
    CHECK(num_bad("5 "));
    CHECK(num_bad("+5"));
    CHECK(num_bad("99999999999999999999"));
}

/* ---- parse_is_register ------------------------------------------------ */

static void test_parse_register(void)
{
    uint8_t r = 99;
    CHECK(parse_is_register("R0", &r) == 1 && r == 0);
    CHECK(parse_is_register("r15", &r) == 1 && r == 15);
    CHECK(parse_is_register("R9", &r) == 1 && r == 9);
    CHECK(parse_is_register("FP", &r) == 1 && r == 13);
    CHECK(parse_is_register("sp", &r) == 1 && r == 14);
    CHECK(parse_is_register("R16", &r) == 0);
    CHECK(parse_is_register("R", &r) == 0);
    CHECK(parse_is_register("R01", &r) == 0);
    CHECK(parse_is_register("PC", &r) == 0);
    CHECK(parse_is_register("FPX", &r) == 0);
    CHECK(parse_is_register("", &r) == 0);
}

/* ---- parse_line: structure -------------------------------------------- */

static void test_parse_blank_and_comment(void)
{
    const char *lines[] = {"", "   ", "; hi", "\t; x"};
    size_t i;
    for (i = 0; i < sizeof lines / sizeof lines[0]; i++) {
        CHECK(PARSE(lines[i]) == 1);
        CHECK_STR(S.label, "");
        CHECK_STR(S.mnem, "");
        CHECK(S.nopd == 0);
    }
    CHECK(parse_line("NOP", 7, &S, err, sizeof err) == 1 && S.line == 7);
}

static void test_parse_label_only(void)
{
    CHECK(PARSE("loop:") == 1);
    CHECK_STR(S.label, "loop");
    CHECK_STR(S.mnem, "");
    CHECK(S.nopd == 0);

    CHECK(PARSE("loop: ADD R0, R1") == 1);
    CHECK_STR(S.label, "loop");
    CHECK_STR(S.mnem, "ADD");
    CHECK(S.nopd == 2);

    CHECK(PARSE("done:HALT") == 1);
    CHECK_STR(S.label, "done");
    CHECK_STR(S.mnem, "HALT");

    CHECK(PARSE("_x1:") == 1);
    CHECK_STR(S.label, "_x1");

    CHECK(PARSE("a: b: NOP") == 0);
    CHECK(PARSE("1x:") == 0);
    CHECK_STR(err, "bad label '1x'");
}

static void test_parse_case_and_spacing(void)
{
    const char *lines[] = {"mov r1,#5", "  MOV   R1 ,  #5  ; c"};
    size_t i;
    for (i = 0; i < 2; i++) {
        CHECK(PARSE(lines[i]) == 1);
        CHECK_STR(S.mnem, "MOV");
        CHECK(S.nopd == 2);
        CHECK(S.opd[0].kind == OPD_REG && S.opd[0].reg == 1);
        CHECK(S.opd[1].kind == OPD_IMM && S.opd[1].value == 5 && S.opd[1].is_sym == 0);
    }
}

/* ---- parse_line: operand forms ---------------------------------------- */

/* Parse "NOP <text>" and return operand 0. */
static const operand *opd(const char *text)
{
    char line[ASM_MAX_LINE];
    snprintf(line, sizeof line, "NOP %s", text);
    if (PARSE(line) != 1 || S.nopd != 1) {
        printf("  (operand '%s' did not parse: %s)\n", text, err);
        return NULL;
    }
    return &S.opd[0];
}

static void test_parse_operand_forms(void)
{
    const operand *o;

    o = opd("R2");
    CHECK(o && o->kind == OPD_REG && o->reg == 2);
    o = opd("#0x10");
    CHECK(o && o->kind == OPD_IMM && o->value == 16 && !o->is_sym);
    o = opd("#N");
    CHECK(o && o->kind == OPD_IMM && o->is_sym && strcmp(o->sym, "N") == 0);
    o = opd("#-1");
    CHECK(o && o->kind == OPD_IMM && o->value == 0xFFFF);
    o = opd("[R2]");
    CHECK(o && o->kind == OPD_MEM_REG && o->reg == 2);
    o = opd("[FP+4]");
    CHECK(o && o->kind == OPD_MEM_REG_OFF && o->reg == 13 && o->value == 4 && !o->is_sym);
    o = opd("[ FP + ARG ]");
    CHECK(o && o->kind == OPD_MEM_REG_OFF && o->reg == 13 && o->is_sym && strcmp(o->sym, "ARG") == 0);
    o = opd("[0x0100]");
    CHECK(o && o->kind == OPD_MEM_ABS && o->value == 0x100 && !o->is_sym);
    o = opd("[result]");
    CHECK(o && o->kind == OPD_MEM_ABS && o->is_sym && strcmp(o->sym, "result") == 0);
    o = opd("loop");
    CHECK(o && o->kind == OPD_VALUE && o->is_sym && strcmp(o->sym, "loop") == 0);
    o = opd("0x0010");
    CHECK(o && o->kind == OPD_VALUE && o->value == 16 && !o->is_sym);
}

static void test_parse_directives(void)
{
    CHECK(PARSE(".word 1, 2, -1, sym") == 1);
    CHECK_STR(S.mnem, ".WORD");
    CHECK(S.nopd == 4);
    CHECK(S.opd[0].kind == OPD_VALUE && S.opd[0].value == 1);
    CHECK(S.opd[2].value == 0xFFFF);
    CHECK(S.opd[3].is_sym && strcmp(S.opd[3].sym, "sym") == 0);

    CHECK(PARSE(".equ N, 8") == 1);
    CHECK_STR(S.mnem, ".EQU");
    CHECK(S.nopd == 2);
    CHECK(S.opd[0].is_sym && strcmp(S.opd[0].sym, "N") == 0);
    CHECK(S.opd[1].value == 8 && !S.opd[1].is_sym);

    CHECK(PARSE(".text") == 1);
    CHECK_STR(S.mnem, ".TEXT");
    CHECK(S.nopd == 0);

    CHECK(PARSE(".org 0x10") == 1);
    CHECK(S.nopd == 1 && S.opd[0].value == 0x10);

    CHECK(PARSE(".word") == 1); /* rejected later, by the assembler */
    CHECK(S.nopd == 0);

    CHECK(PARSE(".word 1,2,3,4,5,6,7,8") == 1 && S.nopd == 8);
    CHECK(PARSE(".word 1,2,3,4,5,6,7,8,9") == 0);
    CHECK_STR(err, "too many operands (max 8)");
}

static void expect_error(const char *line, const char *message)
{
    int ok = PARSE(line);
    CHECK(ok == 0);
    if (ok == 0)
        CHECK_STR(err, message);
    else
        printf("  (line '%s' parsed but should not)\n", line);
}

static void test_parse_errors(void)
{
    expect_error("MOV R1, R16", "bad register 'R16'");
    expect_error("MOV R1, [R2", "expected ']'");
    expect_error("MOV R1, #", "bad number ''");
    expect_error("MOV R1, #70000", "constant out of range '70000'");
    expect_error("ADD R1,, R2", "empty operand");
    expect_error("ADD R1, R2 R3", "bad operand 'R2 R3'");
    expect_error("MOV R1, [R2+]", "bad number ''");
    expect_error("MOV R1, #12abc", "bad number '12abc'");
    expect_error("MOV R1, [R2+R3]", "bad operand '[R2+R3]'");
    expect_error("MOV R1, [0x10+4]", "bad register '0x10'");
    expect_error("JMP lo$p", "bad operand 'lo$p'");
}

/* ---- symtab ------------------------------------------------------------ */

static void test_symtab(void)
{
    static symtab t;
    const symbol *s;
    char name[ASM_NAME_LEN];
    int i;

    symtab_init(&t);
    CHECK(symtab_find(&t, "loop") == NULL);
    CHECK(symtab_define(&t, "loop", 0x0C, 4, 1) == 1);
    s = symtab_find(&t, "loop");
    CHECK(s && s->value == 0x0C && s->line == 4 && s->is_label == 1);
    CHECK(symtab_define(&t, "loop", 0x10, 9, 1) == 0);
    CHECK(symtab_find(&t, "loop")->value == 0x0C); /* first definition kept */
    CHECK(symtab_find(&t, "Loop") == NULL);        /* case-sensitive */
    CHECK(symtab_define(&t, "N", 8, 1, 0) == 1);
    CHECK(symtab_find(&t, "N")->is_label == 0);
    CHECK(t.count == 2);

    symtab_init(&t);
    for (i = 0; i < ASM_MAX_SYMS; i++) {
        snprintf(name, sizeof name, "s%d", i);
        CHECK(symtab_define(&t, name, (uint16_t)i, i + 1, 1) == 1);
    }
    CHECK(symtab_define(&t, "one_too_many", 0, 999, 1) == -1);
    CHECK(t.count == ASM_MAX_SYMS);
}

/* ---- select.c ---------------------------------------------------------- */

/* parse_line + select_insns; returns the instruction count or -1. */
static int sel(const char *line, isa_insn out[2])
{
    if (PARSE(line) != 1) {
        printf("  (line '%s' did not parse: %s)\n", line, err);
        return -1;
    }
    return select_insns(&S, out, err, sizeof err);
}

static int fields_are(const isa_insn *in, unsigned op, unsigned mode, unsigned ra, unsigned rb, unsigned ext)
{
    return in->op == op && in->mode == mode && in->ra == ra && in->rb == rb && in->ext == ext;
}

/* One case per row of docs/isa.md Tables 1, 3 and 4 (and every ALU op). */
static void test_select_table(void)
{
    static const struct {
        const char *line;
        unsigned op, mode, ra, rb, ext;
    } rows[] = {
        {"HALT", OP_SYS, SYS_HALT, 0, 0, 0},
        {"NOP", OP_SYS, SYS_NOP, 0, 0, 0},
        {"MOV R1, R2", OP_MOV, 0, 1, 2, 0},
        {"MOV R1, #5", OP_MOV, 1, 1, 0, 5},
        {"MOV R0, [R2]", OP_MOV, 2, 0, 2, 0},
        {"MOV R3, [FP+4]", OP_MOV, 3, 3, 13, 4},
        {"MOV [SP], R1", OP_MOV, 4, 14, 1, 0},
        {"MOV [FP+6], R0", OP_MOV, 5, 13, 0, 6},
        {"MOV R0, [0x0100]", OP_MOV, 6, 0, 0, 0x100},
        {"MOV [0x0102], R5", OP_MOV, 7, 5, 0, 0x102},
        {"ADD R1, R2", OP_ADD, 0, 1, 2, 0},
        {"ADD R1, #1", OP_ADD, 1, 1, 0, 1},
        {"ADD R0, [R2]", OP_ADD, 2, 0, 2, 0},
        {"ADD R0, [FP+4]", OP_ADD, 3, 0, 13, 4},
        {"SUB SP, #2", OP_SUB, 1, 14, 0, 2},
        {"MUL R0, [R2]", OP_MUL, 2, 0, 2, 0},
        {"DIV R0, #2", OP_DIV, 1, 0, 0, 2},
        {"AND R1, #255", OP_AND, 1, 1, 0, 255},
        {"OR R1, R2", OP_OR, 0, 1, 2, 0},
        {"XOR R1, [R3+2]", OP_XOR, 3, 1, 3, 2},
        {"SHL R1, R2", OP_SHIFT, SHIFT_SHL_R, 1, 2, 0},
        {"SHL R1, #1", OP_SHIFT, SHIFT_SHL_I, 1, 0, 1},
        {"SHR R1, R2", OP_SHIFT, SHIFT_SHR_R, 1, 2, 0},
        {"SHR R1, #3", OP_SHIFT, SHIFT_SHR_I, 1, 0, 3},
        {"CMP R1, R2", OP_CMP, 0, 1, 2, 0},
        {"CMP R1, #0", OP_CMP, 1, 1, 0, 0},
        {"JMP 0x0010", OP_JMP, JMP_ALWAYS, 0, 0, 0x10},
        {"JEQ 0x0010", OP_JMP, JMP_EQ, 0, 0, 0x10},
        {"JNE 0x0010", OP_JMP, JMP_NE, 0, 0, 0x10},
        {"JLT 0x0010", OP_JMP, JMP_LT, 0, 0, 0x10},
        {"JGE 0x0010", OP_JMP, JMP_GE, 0, 0, 0x10},
        {"JGT 0x0010", OP_JMP, JMP_GT, 0, 0, 0x10},
        {"JLE 0x0010", OP_JMP, JMP_LE, 0, 0, 0x10},
        {"CALL 0x0040", OP_CALL, 0, 0, 0, 0x40},
        {"RET", OP_RET, 0, 0, 0, 0},
        {"jle loop", OP_JMP, JMP_LE, 0, 0, 0},           /* symbol: value 0 until pass 2 */
        {"MOV R3, [FP+ARG]", OP_MOV, 3, 3, 13, 0},
    };
    size_t i;
    isa_insn out[2];

    for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        int n = sel(rows[i].line, out);
        CHECK(n == 1);
        if (n == 1 && !fields_are(&out[0], rows[i].op, rows[i].mode, rows[i].ra, rows[i].rb, rows[i].ext)) {
            CHECK(!"fields");
            printf("  '%s' -> op %u mode %u ra %u rb %u ext 0x%04X\n", rows[i].line, (unsigned)out[0].op,
                   (unsigned)out[0].mode, (unsigned)out[0].ra, (unsigned)out[0].rb, (unsigned)out[0].ext);
        }
    }
}

static void test_select_pseudo(void)
{
    isa_insn out[2];

    CHECK(sel("NOT R1", out) == 1);
    CHECK(fields_are(&out[0], OP_XOR, 1, 1, 0, 0xFFFF));

    CHECK(sel("PUSH R1", out) == 2);
    CHECK(fields_are(&out[0], OP_SUB, 1, REG_SP, 0, 2));
    CHECK(fields_are(&out[1], OP_MOV, 4, REG_SP, 1, 0));

    CHECK(sel("POP R3", out) == 2);
    CHECK(fields_are(&out[0], OP_MOV, 2, 3, REG_SP, 0));
    CHECK(fields_are(&out[1], OP_ADD, 1, REG_SP, 0, 2));

    CHECK(sel("push fp", out) == 2);
    CHECK(fields_are(&out[1], OP_MOV, 4, REG_SP, REG_FP, 0));
}

static void expect_select_error(const char *line, const char *message)
{
    isa_insn out[2];
    int n = sel(line, out);
    CHECK(n == -1);
    if (n == -1)
        CHECK_STR(err, message);
    else
        printf("  (line '%s' selected but should not)\n", line);
}

static void test_select_errors(void)
{
    expect_select_error("MOVE R1, R2", "unknown mnemonic 'MOVE'");
    expect_select_error("ADD #1, R1", "bad operands for 'ADD'");
    expect_select_error("MOV [R1], #5", "bad operands for 'MOV'");
    expect_select_error("JMP R1", "bad operands for 'JMP'");
    expect_select_error("HALT R1", "bad operands for 'HALT'");
    expect_select_error("RET 0", "bad operands for 'RET'");
    expect_select_error("CMP R1, 5", "bad operands for 'CMP'");
    expect_select_error("SHL R1, [R2]", "bad operands for 'SHL'");
    expect_select_error("PUSH #1", "bad operands for 'PUSH'");
    expect_select_error("NOT", "bad operands for 'NOT'");
    expect_select_error("ADD R1", "bad operands for 'ADD'");
    expect_select_error("ADD R1, R2, R3", "bad operands for 'ADD'");
    expect_select_error(".word 1", "unknown mnemonic '.WORD'");
}

static void test_select_is_mnemonic(void)
{
    CHECK(select_is_mnemonic("ADD") == 1);
    CHECK(select_is_mnemonic("JEQ") == 1);
    CHECK(select_is_mnemonic("PUSH") == 1);
    CHECK(select_is_mnemonic("HALT") == 1);
    CHECK(select_is_mnemonic("SHIFT") == 0); /* the op name, not a mnemonic */
    CHECK(select_is_mnemonic("LOOP") == 0);
    CHECK(select_is_mnemonic(".WORD") == 0);
}

/*
 * The property: everything isa_disasm() prints assembles back to the same
 * instruction. Covers all 53 legal (op, mode) pairs, twice: once with
 * R7/R11, once with FP/SP so the register aliases are exercised.
 */
static void test_disasm_round_trip_all(void)
{
    static const uint8_t regs[2][2] = {{7, 11}, {REG_FP, REG_SP}};
    unsigned op, m, pass;
    int legal = 0;

    for (pass = 0; pass < 2; pass++) {
        for (op = 0; op < 16; op++) {
            for (m = 0; m < 16; m++) {
                isa_insn in, out[2];
                uint8_t a[4], b[4];
                size_t alen = 0, blen = 0;
                char text[64];
                int ext = isa_needs_ext(op, m);

                if (ext < 0)
                    continue;
                if (pass == 0)
                    legal++;

                memset(&in, 0, sizeof in);
                in.op = (uint8_t)op;
                in.mode = (uint8_t)m;
                in.ra = regs[pass][0];
                in.ext = (uint16_t)(ext ? 0x0102 : 0);
                if (isa_encode(&in, a, sizeof a, &alen) == ISA_ERR_FIELD)
                    in.ra = 0;
                in.rb = regs[pass][1];
                if (isa_encode(&in, a, sizeof a, &alen) == ISA_ERR_FIELD)
                    in.rb = 0;
                CHECK(isa_encode(&in, a, sizeof a, &alen) == ISA_OK);
                in.has_ext = (uint8_t)ext;
                isa_disasm(&in, text, sizeof text);

                if (sel(text, out) != 1) {
                    CHECK(!"round trip selects one instruction");
                    printf("  '%s': %s\n", text, err);
                    continue;
                }
                CHECK(fields_are(&out[0], in.op, in.mode, in.ra, in.rb, in.ext));
                CHECK(isa_encode(&out[0], b, sizeof b, &blen) == ISA_OK);
                CHECK(alen == blen && memcmp(a, b, alen) == 0);
            }
        }
    }
    CHECK(legal == 2 + 8 + 28 + 4 + 2 + 7 + 1 + 1);

    {   /* what the disassembler prints for NOT */
        isa_insn out[2];
        CHECK(sel("XOR R1, #65535", out) == 1 && out[0].ext == 0xFFFF);
    }
}

int main(void)
{
    test_parse_number();
    test_parse_register();
    test_parse_blank_and_comment();
    test_parse_label_only();
    test_parse_case_and_spacing();
    test_parse_operand_forms();
    test_parse_directives();
    test_parse_errors();
    test_symtab();
    test_select_table();
    test_select_pseudo();
    test_select_errors();
    test_select_is_mnemonic();
    test_disasm_round_trip_all();

    printf("%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
