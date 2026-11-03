/*
 * test_asm.c -- unit tests for the assembler front end:
 * parse.c (numbers, registers, lines, operands) and symtab.c.
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

    printf("%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
