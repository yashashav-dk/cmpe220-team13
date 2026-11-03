/*
 * test_assemble.c -- tests for the two-pass assembler (assemble.c):
 * addresses, segments, directives, labels, pseudo-ops, errors and limits.
 *
 * Expected words come from the shared codec (see docs/isa.md tables), never
 * typed from memory. Run with `make test` from the repository root (the
 * power test reads programs/power.s).
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

static asm_unit U;
static char big[ASM_MAX_LINES * 8 + 4096]; /* generated sources for limit tests */

static int asm_str(const char *text)
{
    int n = assemble_text("t.s", text, &U);
    int i;
    for (i = 0; i < n && i < ASM_MAX_ERRORS; i++)
        printf("  (error: %s)\n", U.errors[i]);
    return n;
}

/* Like asm_str, but errors are expected: print nothing. */
static int asm_bad(const char *text)
{
    return assemble_text("t.s", text, &U);
}

static const chunk *at(unsigned addr)
{
    int i;
    for (i = 0; i < U.nchunks; i++)
        if (U.chunks[i].addr == addr)
            return &U.chunks[i];
    return NULL;
}

/* Chunk at addr holds exactly these words (n of them). */
static int words_at(unsigned addr, int n, unsigned w0, unsigned w1)
{
    const chunk *c = at(addr);
    if (c == NULL || c->nwords != n)
        return 0;
    return c->words[0] == w0 && (n < 2 || c->words[1] == w1);
}

static void expect_one_error(const char *text, const char *message)
{
    int n = asm_bad(text);
    CHECK(n == 1);
    if (n >= 1)
        CHECK_STR(U.errors[0], message);
    if (n != 1)
        printf("  (source: %s)\n", text);
}

static void test_halt(void)
{
    const symbol *s;

    CHECK(asm_str("main: HALT\n") == 0);
    CHECK(U.nchunks == 1);
    CHECK(words_at(0x0000, 1, 0x0000, 0));
    CHECK(U.chunks[0].seg == SEG_TEXT && U.chunks[0].line == 1);
    CHECK_STR(U.chunks[0].comment, "HALT");
    CHECK(U.entry == 0);
    s = symtab_find(&U.syms, "main");
    CHECK(s && s->value == 0 && s->is_label);
    CHECK(U.nlines == 1 && U.line_addr[0] == 0);
}

static void test_power_program(void)
{
    const symbol *s;
    int i, loop_line = -1;

    CHECK(assemble_file("programs/power.s", &U) == 0);
    CHECK(U.nchunks == 11);
    CHECK(words_at(0x0000, 2, 0x1100, 0x0001)); /* MOV R0, #1       */
    CHECK(words_at(0x0004, 2, 0x1110, 0x0008)); /* MOV R1, #BASE    */
    CHECK(words_at(0x0008, 2, 0x1120, 0x0002)); /* MOV R2, #EXP     */
    CHECK(words_at(0x000C, 2, 0xA120, 0x0000)); /* CMP R2, #0       */
    CHECK(words_at(0x0010, 2, 0xB100, 0x001E)); /* JEQ done         */
    CHECK(words_at(0x0014, 1, 0x4001, 0));      /* MUL R0, R1       */
    CHECK(words_at(0x0016, 2, 0x3120, 0x0001)); /* SUB R2, #1       */
    CHECK(words_at(0x001A, 2, 0xB000, 0x000C)); /* JMP loop         */
    CHECK(words_at(0x001E, 2, 0x1700, 0x0100)); /* MOV [result], R0 */
    CHECK(words_at(0x0022, 1, 0x0000, 0));      /* HALT             */
    CHECK(words_at(0x0100, 1, 0x0000, 0));      /* result: .word 0  */
    CHECK(at(0x0010) && strcmp(at(0x0010)->comment, "JEQ 0x001E") == 0);
    CHECK(at(0x001E) && strcmp(at(0x001E)->comment, "MOV [0x0100], R0") == 0);
    CHECK(at(0x0100) && strcmp(at(0x0100)->comment, "result") == 0 && at(0x0100)->seg == SEG_DATA);
    CHECK(U.entry == 0);

    s = symtab_find(&U.syms, "BASE");
    CHECK(s && s->value == 8 && !s->is_label);
    s = symtab_find(&U.syms, "EXP");
    CHECK(s && s->value == 2 && !s->is_label);
    CHECK(symtab_find(&U.syms, "main") && symtab_find(&U.syms, "main")->value == 0x0000);
    CHECK(symtab_find(&U.syms, "loop") && symtab_find(&U.syms, "loop")->value == 0x000C);
    CHECK(symtab_find(&U.syms, "done") && symtab_find(&U.syms, "done")->value == 0x001E);
    CHECK(symtab_find(&U.syms, "result") && symtab_find(&U.syms, "result")->value == 0x0100);

    for (i = 0; i < U.nlines; i++)
        if (strncmp(U.src[i], "loop:", 5) == 0)
            loop_line = i;
    CHECK(loop_line >= 0 && U.line_addr[loop_line] == 0x000C);
    CHECK(U.line_addr[0] == -1); /* comment line */
}

static void test_forward_and_backward_labels(void)
{
    CHECK(asm_str("JMP end\nx: NOP\nJMP x\nend: HALT\n") == 0);
    CHECK(words_at(0x0000, 2, 0xB000, 0x000A));
    CHECK(words_at(0x0004, 1, 0x0100, 0));
    CHECK(words_at(0x0006, 2, 0xB000, 0x0004));
    CHECK(words_at(0x000A, 1, 0x0000, 0));
}

static void test_equ_in_every_position(void)
{
    CHECK(asm_str(".equ ARG, 4\n.equ K, ARG\nMOV R3, [FP+ARG]\nMOV R1, #K\n.data\n.word ARG\n") == 0);
    CHECK(words_at(0x0000, 2, 0x133D, 0x0004)); /* MOV R3, [FP+4] */
    CHECK(words_at(0x0004, 2, 0x1110, 0x0004)); /* MOV R1, #4     */
    CHECK(words_at(0x0100, 1, 0x0004, 0));
    CHECK(U.line_addr[0] == -1 && U.line_addr[1] == -1);

    expect_one_error(".equ A, B\n.equ B, 1\n", "t.s:1: error: undefined symbol 'B'");
    expect_one_error(".equ 5, 1\n", "t.s:1: error: .equ needs NAME, value");
    expect_one_error(".equ N\n", "t.s:1: error: .equ needs NAME, value");
}

static void test_pseudo_ops_emit_two_chunks(void)
{
    CHECK(asm_str("PUSH R1\nPOP R1\n") == 0);
    CHECK(U.nchunks == 4);
    CHECK(words_at(0x0000, 2, 0x31E0, 0x0002)); /* SUB SP, #2   */
    CHECK(words_at(0x0004, 1, 0x14E1, 0));      /* MOV [SP], R1 */
    CHECK(words_at(0x0006, 1, 0x121E, 0));      /* MOV R1, [SP] */
    CHECK(words_at(0x0008, 2, 0x21E0, 0x0002)); /* ADD SP, #2   */
    CHECK(U.line_addr[0] == 0x0000 && U.line_addr[1] == 0x0006);
    CHECK(at(0x0004) && at(0x0004)->line == 1 && strcmp(at(0x0004)->comment, "MOV [SP], R1") == 0);
}

static void test_segments_and_org(void)
{
    CHECK(asm_str(".data\n.word 1\n.text\nNOP\n.data\n.word 2\n.text\n.org 0x10\nHALT\n") == 0);
    CHECK(words_at(0x0100, 1, 0x0001, 0));
    CHECK(words_at(0x0102, 1, 0x0002, 0));
    CHECK(words_at(0x0000, 1, 0x0100, 0));
    CHECK(words_at(0x0010, 1, 0x0000, 0));
    CHECK(U.entry == 0);

    CHECK(asm_str(".data\nv: .word 7\n.text\n.org 0x0008\nstart: HALT\n") == 0);
    CHECK(U.entry == 0x0008); /* the first text chunk, wherever it is */

    expect_one_error(".org 0x10\nNOP\n.org 0x0004\n", "t.s:3: error: .org cannot move backwards");
    expect_one_error(".org 0x0200\n", "t.s:1: error: .org 0x0200 is outside the text segment (0x0000-0x00FF)");
    expect_one_error(".data\n.org 0x0010\n", "t.s:2: error: .org 0x0010 is outside the data segment (0x0100-0x017F)");
    expect_one_error(".text 5\n", "t.s:1: error: .text takes no operands");
    expect_one_error(".foo\n", "t.s:1: error: unknown directive '.FOO'");
    expect_one_error(".word #5\n", "t.s:1: error: bad operand for .word");
    expect_one_error(".word\n", "t.s:1: error: .word needs at least one value");
}

static void test_space(void)
{
    CHECK(asm_str(".data\nbuf: .space 6\nend: .word 0\n") == 0);
    CHECK(symtab_find(&U.syms, "buf")->value == 0x0100);
    CHECK(symtab_find(&U.syms, "end")->value == 0x0106);
    CHECK(U.nchunks == 1 && words_at(0x0106, 1, 0, 0));
    CHECK(U.line_addr[1] == 0x0100);
}

/* Review focus 3: odd or out-of-memory addresses are caught when assembling. */
static void test_address_checks(void)
{
    expect_one_error("JMP 0x0101\n", "t.s:1: error: odd address 0x0101");
    expect_one_error("CALL 0x0400\n", "t.s:1: error: address 0x0400 is outside memory (0x0000-0x03FF)");
    expect_one_error("MOV R0, [0x0500]\n", "t.s:1: error: address 0x0500 is outside memory (0x0000-0x03FF)");
    expect_one_error("MOV [0x0103], R0\n", "t.s:1: error: odd address 0x0103");
    expect_one_error(".org 0x0005\n", "t.s:1: error: odd address 0x0005");
    expect_one_error(".space 3\n", "t.s:1: error: .space needs an even size > 0");
    expect_one_error(".equ T, 0x0011\nJMP T\n", "t.s:2: error: odd address 0x0011");
    CHECK(asm_str("MOV R0, [FP+3]\n") == 0); /* offsets are not addresses */
}

/* Review focus 5: names that collide with registers, mnemonics or each other. */
static void test_symbol_errors(void)
{
    expect_one_error("sp: NOP\n", "t.s:1: error: 'sp' is a register name");
    expect_one_error("R3: NOP\n", "t.s:1: error: 'R3' is a register name");
    expect_one_error("add: NOP\n", "t.s:1: error: 'add' is a mnemonic");
    expect_one_error(".equ PUSH, 1\n", "t.s:1: error: 'PUSH' is a mnemonic");
    expect_one_error("a: NOP\na: NOP\n", "t.s:2: error: duplicate symbol 'a' (first defined at line 1)");
    expect_one_error("a: NOP\n.equ a, 1\n", "t.s:2: error: duplicate symbol 'a' (first defined at line 1)");
    expect_one_error("JMP lop\n", "t.s:1: error: undefined symbol 'lop'");
    expect_one_error("MOV R0, [nowhere]\n", "t.s:1: error: undefined symbol 'nowhere'");
    CHECK(asm_str("Loop: NOP\nloop: NOP\n") == 0); /* case-sensitive */
}

/* Review focus 4: limits give a clear error, never an overrun. */
static void test_limits(void)
{
    size_t len;
    int i;

    big[0] = '\0';
    for (i = 0; i < 129; i++)
        strcat(big, "NOP\n");
    expect_one_error(big, "t.s:129: error: segment overflow: text is 0x0000-0x00FF");

    strcpy(big, ".data\n");
    for (i = 0; i < 65; i++)
        strcat(big, ".word 0\n");
    expect_one_error(big, "t.s:66: error: segment overflow: data is 0x0100-0x017F");

    memset(big, 'x', 300);
    strcpy(big + 300, "\n");
    big[0] = ';';
    expect_one_error(big, "t.s:1: error: line too long (max 255 characters)");

    len = 0;
    for (i = 0; i <= ASM_MAX_SYMS; i++)
        len += (size_t)sprintf(big + len, "l%d:\n", i);
    expect_one_error(big, "t.s:129: error: too many symbols (max 128)");

    big[0] = '\0';
    for (i = 0; i < ASM_MAX_LINES + 1; i++)
        strcat(big, ";\n");
    expect_one_error(big, "t.s:1025: error: too many lines (max 1024)");
}

/* Review focus 1: Windows line endings and a missing final newline. */
static void test_crlf_and_no_final_newline(void)
{
    CHECK(asm_str("NOP\r\nHALT") == 0);
    CHECK(U.nchunks == 2 && U.nlines == 2);
    CHECK_STR(U.src[0], "NOP");
    CHECK_STR(U.src[1], "HALT");
    CHECK(words_at(0x0002, 1, 0x0000, 0));
}

static void test_reports_all_errors(void)
{
    /* pass-1 errors stop pass 2, so 'lop' is not reported here */
    CHECK(asm_bad("MOVE R1\nJMP lop\nFOO\n") == 2);
    CHECK_STR(U.errors[0], "t.s:1: error: unknown mnemonic 'MOVE'");
    CHECK_STR(U.errors[1], "t.s:3: error: unknown mnemonic 'FOO'");

    CHECK(asm_bad("JMP lop\nMOV R0, [0x0101]\n") == 2);
    CHECK_STR(U.errors[0], "t.s:1: error: undefined symbol 'lop'");
    CHECK_STR(U.errors[1], "t.s:2: error: odd address 0x0101");

    CHECK(asm_bad("MOV R1, R16\n") == 1);
    CHECK_STR(U.errors[0], "t.s:1: error: bad register 'R16'");
}

static void test_assemble_file_missing(void)
{
    CHECK(assemble_file("no/such/file.s", &U) == -1);
}

int main(void)
{
    test_halt();
    test_power_program();
    test_forward_and_backward_labels();
    test_equ_in_every_position();
    test_pseudo_ops_emit_two_chunks();
    test_segments_and_org();
    test_space();
    test_address_checks();
    test_symbol_errors();
    test_limits();
    test_crlf_and_no_final_newline();
    test_reports_all_errors();
    test_assemble_file_missing();

    printf("%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
