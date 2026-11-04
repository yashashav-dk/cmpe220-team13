/*
 * test_view.c -- tests for t13view: the trace reader (read.c) and the
 * views (render.c).
 *
 * The reader is tested on the golden traces in tests/golden/ (written by
 * hand from docs/trace.md before the simulator existed) and on broken
 * traces for every check of docs/trace.md rule 11. Run from the
 * repository root with `make test`.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "view.h"

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

#define CHECK_EQ(got, want)                                              \
    do {                                                                 \
        long g_ = (long)(got), w_ = (long)(want);                        \
        checks++;                                                        \
        if (g_ != w_) {                                                  \
            failures++;                                                  \
            printf("FAIL %s:%d: %s = 0x%lX, want 0x%lX\n", __FILE__,     \
                   __LINE__, #got, g_, w_);                              \
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

/* out contains needle */
#define CHECK_HAS(out, needle)                                           \
    do {                                                                 \
        checks++;                                                        \
        if (strstr((out), (needle)) == NULL) {                           \
            failures++;                                                  \
            printf("FAIL %s:%d: output lacks \"%s\"\n", __FILE__,        \
                   __LINE__, (needle));                                  \
        }                                                                \
    } while (0)

static vw_trace T;
static char err[160];
static char file_buf[1 << 16];

static const char *slurp(const char *path)
{
    FILE *f = fopen(path, "rb");
    size_t n = 0;

    file_buf[0] = '\0';
    if (f == NULL) {
        printf("FAIL cannot open %s (run from the repository root)\n", path);
        failures++;
        return file_buf;
    }
    n = fread(file_buf, 1, sizeof file_buf - 1, f);
    file_buf[n] = '\0';
    fclose(f);
    return file_buf;
}

static int rd(const char *text)
{
    return vw_read(&T, text, strlen(text), err, sizeof err);
}

/* Capture what a renderer writes. */
static char out_buf[1 << 17];

static const char *render(void (*fn)(const vw_trace *, FILE *, vw_format), vw_format fmt)
{
    FILE *f = tmpfile();
    size_t n;

    out_buf[0] = '\0';
    if (f == NULL)
        return out_buf;
    fn(&T, f, fmt);
    rewind(f);
    n = fread(out_buf, 1, sizeof out_buf - 1, f);
    out_buf[n] = '\0';
    fclose(f);
    return out_buf;
}

static const char *peek(const char *expr, int *rc)
{
    FILE *f = tmpfile();
    size_t n;

    out_buf[0] = '\0';
    *rc = vw_peek(&T, expr, f, err, sizeof err);
    rewind(f);
    n = fread(out_buf, 1, sizeof out_buf - 1, f);
    out_buf[n] = '\0';
    fclose(f);
    return out_buf;
}

/* ---- reader ---------------------------------------------------------- */

static void test_read_power(void)
{
    const char *s = slurp("tests/golden/power.trc");
    const vw_step *st;
    uint16_t a = 0;

    CHECK_EQ(rd(s), 0);
    CHECK_EQ(T.entry, 0x0000);
    CHECK_EQ(T.nsym, 4);
    CHECK_STR(T.sym[2].name, "done");
    CHECK_EQ(T.sym[2].addr, 0x001E);
    CHECK_STR(vw_sym_at(&T, 0x0100), "result");
    CHECK(vw_sym_at(&T, 0x0102) == NULL);
    CHECK_EQ(vw_sym_find(&T, "loop", &a), 0);
    CHECK_EQ(a, 0x000C);
    CHECK(vw_sym_find(&T, "nope", &a) != 0);
    CHECK_EQ(vw_word(T.mem0, 0x0014), 0x4001);
    CHECK_EQ(vw_word(T.mem0, 0x0020), 0x0100);
    CHECK_EQ(vw_word(T.mem0, 0x0100), 0x0000);
    CHECK_EQ(T.state0.reg[REG_SP], 0x0400);
    CHECK_EQ(T.nsteps, 17);

    st = &T.step[0];
    CHECK_EQ(st->n, 1);
    CHECK_EQ(st->pc, 0x0000);
    CHECK_EQ(st->ir, 0x1100);
    CHECK_EQ(st->has_ext, 1);
    CHECK_EQ(st->ext, 0x0001);
    CHECK_STR(st->text, "MOV R0, #1");
    CHECK_EQ(st->after.reg[0], 0x0001);
    CHECK_EQ(st->nwrites, 0);

    st = &T.step[15];
    CHECK_EQ(st->nwrites, 1);
    CHECK_EQ(st->write[0].addr, 0x0100);
    CHECK_EQ(st->write[0].before, 0x0000);
    CHECK_EQ(st->write[0].after, 0x0040);
    CHECK_EQ(T.step[16].after.z, 1);
    CHECK_EQ(T.step[16].has_ext, 0);
    CHECK_EQ(T.end, VW_END_HALT);
    CHECK_EQ(T.end_pc, 0x0024);
    vw_free(&T);
}

static void test_read_crlf_and_comments(void)
{
    CHECK_EQ(rd("T13TRC 1\r\n; comment\r\nENTRY 0000\r\n\r\n"
                "REGS R0=0000 R1=0000 R2=0000 R3=0000 R4=0000 R5=0000 R6=0000 R7=0000 "
                "R8=0000 R9=0000 R10=0000 R11=0000 R12=0000 FP=0000 SP=0400 R15=0000\r\n"
                "FLAGS Z=0 N=0 C=0\r\nEND LIMIT PC=0000 STEPS=0\r\n"),
             0);
    CHECK_EQ(T.nsteps, 0);
    CHECK_EQ(T.end, VW_END_LIMIT);
    vw_free(&T);
}

#define HDR "T13TRC 1\nENTRY 0000\n"
#define REGS_A "REGS R0=0000 R1=0000 R2=0000 R3=0000 R4=0000 R5=0000 R6=0000 R7=0000 " \
               "R8=0000 R9=0000 R10=0000 R11=0000 R12=0000 FP=0000 SP=0400 R15=0000\n"
#define FLAGS0 "FLAGS Z=0 N=0 C=0\n"
#define START HDR REGS_A FLAGS0

static void test_read_errors(void)
{
    CHECK_EQ(rd("T13TRC 2\n"), 1);
    CHECK_STR(err, "expected 'T13TRC 1'");
    CHECK_EQ(rd("T13TRC 1\nSYM a 0000\n"), 2);
    CHECK_STR(err, "expected 'ENTRY hhhh'");
    CHECK_EQ(rd(HDR "LOAD 0008: 0000 0000 0000 0000 0000 0000 0000 0001\n"), 3);
    CHECK_STR(err, "LOAD address 0x0008 is not a multiple of 0x0010");
    CHECK_EQ(rd(HDR "LOAD 0000: 0000 0001\n"), 3);
    CHECK_STR(err, "expected 'LOAD hhhh:' and 8 words");
    CHECK_EQ(rd(HDR "REGS R0=0000\n"), 3);
    CHECK_STR(err, "expected 'REGS' and 16 registers R0 ... R15");
    CHECK_EQ(rd(HDR REGS_A "FLAGS Z=2 N=0 C=0\n"), 4);
    CHECK_STR(err, "expected 'FLAGS Z=b N=b C=b'");
    CHECK_EQ(rd(START "STEP 2 PC=0000 IR=0000 ; HALT\n" REGS_A FLAGS0 "END HALT PC=0002 STEPS=1\n"), 5);
    CHECK_STR(err, "STEP 2 after STEP 0: step numbers must count up by one");
    CHECK_EQ(rd(START "STEP 1 PC=0000 IR=1710 EXT=0100\n" REGS_A FLAGS0
                "MEM 0100: 0001 -> 0002\nEND HALT PC=0002 STEPS=1\n"),
             8);
    CHECK_STR(err, "MEM 0100: before-value 0001, but memory holds 0000");
    CHECK_EQ(rd(START "STEP 1 PC=0000 IR=0000 ; HALT\n" REGS_A FLAGS0 "END HALT PC=0002 STEPS=2\n"), 8);
    CHECK_STR(err, "END says STEPS=2, but the trace has 1 STEP blocks");
    CHECK_EQ(rd(START "STEP 1 PC=0000 IR=0000 ; HALT\n" REGS_A FLAGS0), 7);
    CHECK_STR(err, "missing END line");
    CHECK_EQ(rd(START "END HALT PC=0002 STEPS=0\nSTEP 1 PC=0000 IR=0000\n"), 6);
    CHECK_STR(err, "line after END");
    CHECK_EQ(rd(START "STEP 1 PC=0000 IR=0000 ; HALT\n" FLAGS0), 6);
    CHECK_STR(err, "expected 'REGS' and 16 registers R0 ... R15");
    CHECK_EQ(rd(START "END BOOM PC=0000 STEPS=0\n"), 5);
    CHECK_STR(err, "expected 'END HALT|LIMIT|ERROR PC=hhhh STEPS=n'");
    CHECK_EQ(rd(START "WHAT\n"), 5);
    CHECK_STR(err, "unknown record 'WHAT'");
}

static void test_read_error_end(void)
{
    CHECK_EQ(rd(START "END ERROR PC=0000 STEPS=0 divide by zero\n"), 0);
    CHECK_EQ(T.end, VW_END_ERROR);
    CHECK_STR(T.end_msg, "divide by zero");
    vw_free(&T);
}

/* ---- views ------------------------------------------------------------ */

static void test_summary(void)
{
    const char *o;
    CHECK_EQ(rd(slurp("tests/golden/power.trc")), 0);
    o = render(vw_summary, VW_TEXT);
    CHECK_HAS(o, "END HALT after 17 steps, PC=0024");
    CHECK_HAS(o, "R0=0040 R1=0008 R2=0000");
    CHECK_HAS(o, "FLAGS Z=1 N=0 C=0");
    CHECK_HAS(o, "0100 result     0000 -> 0040");
    vw_free(&T);
}

static void test_waterfall(void)
{
    const char *o;
    CHECK_EQ(rd(slurp("tests/golden/power.trc")), 0);
    o = render(vw_waterfall, VW_TEXT);
    /* Columns: only registers that change, the flags, and written words. */
    CHECK_HAS(o, "STEP  PC    INSTRUCTION           R0     R1     R2     Z N C  M[0100]");
    CHECK(strstr(o, " SP ") == NULL);
    CHECK_HAS(o, "   0  ----  (loaded)              0000   0000   0000   0 0 0  0000");
    CHECK_HAS(o, "   1  0000  MOV R0, #1            0001*  0000   0000   0 0 0  0000");
    CHECK_HAS(o, "  16  001E  MOV [0x0100], R0      0040   0008   0000   1 0 0  0040*");
    o = render(vw_waterfall, VW_HTML);
    CHECK_HAS(o, "<table class=\"t13 waterfall\">");
    CHECK_HAS(o, "<th class=\"mem\">0100<br>result</th>");
    CHECK_HAS(o, "<td class=\"chg\">0040</td>");
    vw_free(&T);
}

static void test_grid(void)
{
    const char *o;
    CHECK_EQ(rd(slurp("tests/golden/power.trc")), 0);
    o = render(vw_grid, VW_TEXT);
    CHECK_HAS(o, "Memory after loading (step 0)");
    CHECK_HAS(o, "Memory at the end (step 17, END HALT)");
    CHECK_HAS(o, "TEXT   0000  1100 0001 1110 0008 1120 0002 A120 0000");
    CHECK_HAS(o, "DATA   0100  0000 0000 0000 0000 0000 0000 0000 0000");
    CHECK_HAS(o, "DATA   0100  0040*0000 0000 0000 0000 0000 0000 0000");
    vw_free(&T);
}

static void test_blocks(void)
{
    const char *o;
    CHECK_EQ(rd(slurp("tests/golden/power.trc")), 0);
    o = render(vw_blocks, VW_TEXT);
    CHECK_HAS(o, "After step 16: MOV [0x0100], R0");
    CHECK_HAS(o, "After step 17: HALT");
    CHECK_HAS(o, "DATA   0100  0040*0000");
    vw_free(&T);
}

static void test_loaded(void)
{
    const char *o;
    CHECK_EQ(rd(slurp("tests/golden/power.trc")), 0);
    o = render(vw_loaded, VW_TEXT);
    CHECK_HAS(o, "0000  1100 0001  main    MOV R0, #1");
    CHECK_HAS(o, "000C  A120 0000  loop    CMP R2, #0");
    CHECK_HAS(o, "0022  0000" "               " "HALT"); /* words 9 + 2, label 6 + 2 */
    CHECK(strstr(o, "0024  0000") == NULL); /* code ends after the last instruction */
    CHECK_HAS(o, "0100  0000  result");
    vw_free(&T);
}

static void test_segments(void)
{
    const char *o;
    CHECK_EQ(rd(slurp("tests/golden/callret.trc")), 0);
    o = render(vw_segments, VW_TEXT);
    CHECK_HAS(o, "Snapshot 0: after loading");
    CHECK_HAS(o, "Snapshot 1: after step 1, CALL f");
    CHECK_HAS(o, "Snapshot 2: after step 2, RET");
    CHECK_HAS(o, "Snapshot 3: after step 3, HALT");
    CHECK_HAS(o, "Text   0000-0007  4 words of code");
    CHECK_HAS(o, "Heap   0180-027F  empty");
    CHECK_HAS(o, "Stack  ROW   +0 OLDFP  +2 RA   +4 ARG  +6 RETV");
    CHECK_HAS(o, "       03F8  0000      0000    0000    0004*   SP=03FE");
    o = render(vw_segments, VW_HTML);
    CHECK_HAS(o, "<figure class=\"t13fig snap\">");
    vw_free(&T);

    /* Shading is relative to the previous snapshot, not the previous step:
     * power.s changes R0 in step 13, long before the HALT snapshot. */
    CHECK_EQ(rd(slurp("tests/golden/power.trc")), 0);
    o = render(vw_segments, VW_HTML);
    CHECK_HAS(o, "<tr><td>R0</td><td class=\"chg\">0040</td></tr>");
    CHECK_HAS(o, "<tr><td>R2</td><td>0000</td></tr>");
    vw_free(&T);

    /* HTML: the return address in the RA column names the function it
     * returns into (entry or CALL target), not the nearest label (loop). */
    CHECK_EQ(rd("T13TRC 1\nENTRY 0000\nSYM main 0000\nSYM loop 0008\nSYM f 0010\n" REGS_A FLAGS0
                "STEP 1 PC=0008 IR=C000 EXT=0010\n"
                "REGS R0=0000 R1=0000 R2=0000 R3=0000 R4=0000 R5=0000 R6=0000 R7=0000 "
                "R8=0000 R9=0000 R10=0000 R11=0000 R12=0000 FP=0000 SP=03FA R15=0000\n" FLAGS0
                "MEM 03FA: 0000 -> 000C\nEND LIMIT PC=0010 STEPS=1\n"),
             0);
    o = render(vw_segments, VW_HTML);
    CHECK_HAS(o, "<small>main+C</small>");
    CHECK(strstr(o, "loop+4") == NULL);
    vw_free(&T);
}

static void test_peek(void)
{
    int rc;
    const char *o;

    CHECK_EQ(rd(slurp("tests/golden/power.trc")), 0);
    o = peek("result", &rc);
    CHECK_EQ(rc, 0);
    CHECK_STR(o, "result=64\n");
    o = peek("nope", &rc);
    CHECK_EQ(rc, -1);
    CHECK_STR(err, "no symbol 'nope'");
    vw_free(&T);

    /* *p follows a pointer: p = 0102 holds 0104 holds 120. */
    CHECK_EQ(rd(START "SYM p 0102\nSTEP 1 PC=0000 IR=0000 ; HALT\n" REGS_A FLAGS0
                "END HALT PC=0002 STEPS=1\n"),
             5); /* SYM after REGS is out of place */
    CHECK_EQ(rd(HDR "SYM p 0100\nLOAD 0100: 0102 0078 0000 0000 0000 0000 0000 0000\n" REGS_A FLAGS0
                "END HALT PC=0002 STEPS=0\n"),
             0);
    o = peek("*p", &rc);
    CHECK_EQ(rc, 0);
    CHECK_STR(o, "*p=120\n");
    vw_free(&T);
}

int main(void)
{
    test_read_power();
    test_read_crlf_and_comments();
    test_read_errors();
    test_read_error_end();
    test_summary();
    test_waterfall();
    test_grid();
    test_blocks();
    test_loaded();
    test_segments();
    test_peek();

    printf("%d checks, %d failed\n", checks, failures);
    return failures != 0;
}
