/*
 * test_isa.c -- unit tests for the shared codec (src/common/isa.c).
 *
 * Plain C, no framework: every CHECK that fails prints file:line and the
 * expression, and the program exits non-zero if anything failed.
 * Run with `make test`.
 */
#include <stdio.h>
#include <string.h>

#include "isa.h"

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

static isa_insn mk(unsigned op, unsigned mode, unsigned ra, unsigned rb, unsigned ext)
{
    isa_insn in;
    in.op = (uint8_t)op;
    in.mode = (uint8_t)mode;
    in.ra = (uint8_t)ra;
    in.rb = (uint8_t)rb;
    in.ext = (uint16_t)ext;
    in.has_ext = 0;
    return in;
}

/* Encodings written out by hand from docs/isa.md. */
static void test_known_encodings(void)
{
    uint8_t b[4];
    size_t len = 0;
    isa_insn in;

    in = mk(OP_ADD, 0, 1, 2, 0); /* ADD R1, R2 -> 0x2012 */
    CHECK(isa_encode(&in, b, sizeof b, &len) == ISA_OK);
    CHECK(len == 2);
    CHECK(b[0] == 0x12 && b[1] == 0x20); /* little-endian */

    in = mk(OP_MOV, 1, 1, 0, 5); /* MOV R1, #5 -> 0x1110 0x0005 */
    CHECK(isa_encode(&in, b, sizeof b, &len) == ISA_OK);
    CHECK(len == 4);
    CHECK(b[0] == 0x10 && b[1] == 0x11 && b[2] == 0x05 && b[3] == 0x00);

    in = mk(OP_SYS, SYS_HALT, 0, 0, 0); /* HALT -> 0x0000 */
    CHECK(isa_encode(&in, b, sizeof b, &len) == ISA_OK);
    CHECK(len == 2 && b[0] == 0x00 && b[1] == 0x00);

    in = mk(OP_JMP, JMP_LE, 0, 0, 0x0010); /* JLE 0x0010 -> 0xB600 0x0010 */
    CHECK(isa_encode(&in, b, sizeof b, &len) == ISA_OK);
    CHECK(len == 4);
    CHECK(b[0] == 0x00 && b[1] == 0xB6 && b[2] == 0x10 && b[3] == 0x00);

    in = mk(OP_SUB, 1, REG_SP, 0, 2); /* SUB SP, #2 -> 0x31E0 0x0002 */
    CHECK(isa_encode(&in, b, sizeof b, &len) == ISA_OK);
    CHECK(b[0] == 0xE0 && b[1] == 0x31 && b[2] == 0x02 && b[3] == 0x00);
}

/* Which (op, mode) pairs take an EXT word: exactly the list in docs/isa.md. */
static void test_needs_ext_table(void)
{
    unsigned op, m;

    CHECK(isa_needs_ext(OP_SYS, SYS_HALT) == 0);
    CHECK(isa_needs_ext(OP_SYS, SYS_NOP) == 0);
    CHECK(isa_needs_ext(OP_SYS, 2) == -1);

    for (m = 0; m < 8; m++)
        CHECK(isa_needs_ext(OP_MOV, m) == (m == 1 || m == 3 || m >= 5));
    CHECK(isa_needs_ext(OP_MOV, 8) == -1);

    for (op = OP_ADD; op <= OP_XOR; op++) {
        for (m = 0; m < 4; m++)
            CHECK(isa_needs_ext(op, m) == (m == 1 || m == 3));
        CHECK(isa_needs_ext(op, 4) == -1);
    }

    for (m = 0; m < 4; m++)
        CHECK(isa_needs_ext(OP_SHIFT, m) == (m == 1 || m == 3));
    CHECK(isa_needs_ext(OP_SHIFT, 4) == -1);

    CHECK(isa_needs_ext(OP_CMP, 0) == 0);
    CHECK(isa_needs_ext(OP_CMP, 1) == 1);
    CHECK(isa_needs_ext(OP_CMP, 2) == -1);

    for (m = 0; m < 7; m++)
        CHECK(isa_needs_ext(OP_JMP, m) == 1);
    CHECK(isa_needs_ext(OP_JMP, 7) == -1);

    CHECK(isa_needs_ext(OP_CALL, 0) == 1);
    CHECK(isa_needs_ext(OP_CALL, 1) == -1);
    CHECK(isa_needs_ext(OP_RET, 0) == 0);
    CHECK(isa_needs_ext(OP_RET, 1) == -1);

    CHECK(isa_needs_ext(0xE, 0) == -1);
    CHECK(isa_needs_ext(0xF, 0) == -1);
    CHECK(isa_needs_ext(16, 0) == -1);
}

static void test_length(void)
{
    CHECK(isa_length(0x2012) == 2); /* ADD R1, R2   */
    CHECK(isa_length(0x1110) == 4); /* MOV R1, #EXT */
    CHECK(isa_length(0x0000) == 2); /* HALT         */
    CHECK(isa_length(0xD000) == 2); /* RET          */
    CHECK(isa_length(0xC000) == 4); /* CALL EXT     */
    CHECK(isa_length(0xE000) == 0); /* reserved     */
    CHECK(isa_length(0x0200) == 0); /* SYS mode 2   */
}

/* Every legal (op, mode) survives encode -> decode unchanged. */
static void test_round_trip_all(void)
{
    unsigned op, m;
    int legal = 0;

    for (op = 0; op < 16; op++) {
        for (m = 0; m < 16; m++) {
            int ext = isa_needs_ext(op, m);
            isa_insn in, out;
            uint8_t b[4];
            size_t elen = 0, dlen = 0;
            int ok;

            if (ext < 0)
                continue;
            legal++;

            /* Set Ra and Rb to non-zero values, clearing whichever one this
             * mode does not read (the encoder rejects it with ISA_ERR_FIELD). */
            in = mk(op, m, 7, 0, ext ? 0xBEEF : 0);
            if (isa_encode(&in, b, sizeof b, &elen) == ISA_ERR_FIELD)
                in.ra = 0;
            in.rb = 11;
            if (isa_encode(&in, b, sizeof b, &elen) == ISA_ERR_FIELD)
                in.rb = 0;

            ok = isa_encode(&in, b, sizeof b, &elen) == ISA_OK;
            CHECK(ok);
            if (!ok)
                continue;
            CHECK(elen == (ext ? 4u : 2u));
            CHECK(isa_decode(b, elen, &out, &dlen) == ISA_OK);
            CHECK(dlen == elen);
            CHECK(out.op == in.op && out.mode == in.mode);
            CHECK(out.ra == in.ra && out.rb == in.rb);
            CHECK(out.has_ext == (ext ? 1 : 0));
            if (ext)
                CHECK(out.ext == 0xBEEF);
        }
    }
    /* SYS 2 + MOV 8 + 7 ALU ops x 4 + SHIFT 4 + CMP 2 + JMP 7 + CALL 1 + RET 1 */
    CHECK(legal == 2 + 8 + 28 + 4 + 2 + 7 + 1 + 1);
}

static void test_decode_rejects_reserved_opcodes(void)
{
    uint8_t e[4] = {0x00, 0xE0, 0x00, 0x00};
    uint8_t f[4] = {0x00, 0xF0, 0x00, 0x00};
    isa_insn out;
    size_t len = 0;

    CHECK(isa_decode(e, sizeof e, &out, &len) == ISA_ERR_OPCODE);
    CHECK(isa_decode(f, sizeof f, &out, &len) == ISA_ERR_OPCODE);
}

static void test_decode_rejects_bad_modes(void)
{
    uint8_t sys2[2] = {0x00, 0x02}; /* SYS mode 2 */
    uint8_t mov8[4] = {0x00, 0x18, 0, 0};
    uint8_t jmp7[4] = {0x00, 0xB7, 0, 0};
    uint8_t ret1[2] = {0x00, 0xD1};
    uint8_t cmp2[2] = {0x12, 0xA2};
    isa_insn out;
    size_t len = 0;

    CHECK(isa_decode(sys2, sizeof sys2, &out, &len) == ISA_ERR_MODE);
    CHECK(isa_decode(mov8, sizeof mov8, &out, &len) == ISA_ERR_MODE);
    CHECK(isa_decode(jmp7, sizeof jmp7, &out, &len) == ISA_ERR_MODE);
    CHECK(isa_decode(ret1, sizeof ret1, &out, &len) == ISA_ERR_MODE);
    CHECK(isa_decode(cmp2, sizeof cmp2, &out, &len) == ISA_ERR_MODE);
}

/* Register fields a mode does not read must be zero. */
static void test_unused_fields_must_be_zero(void)
{
    uint8_t halt_ra[2] = {0x10, 0x00};       /* HALT with Ra = 1         */
    uint8_t movi_rb[4] = {0x13, 0x11, 5, 0}; /* MOV R1, #5 with Rb = 3   */
    uint8_t jmp_ra[4] = {0x20, 0xB0, 0, 0};  /* JMP with Ra = 2          */
    uint8_t shli_rb[4] = {0x11, 0x91, 1, 0}; /* SHL R1, #1 with Rb = 1   */
    uint8_t movst_ok[4] = {0x10, 0x17, 0, 1}; /* MOV [0x0100], R1: Ra used */
    isa_insn out, in;
    uint8_t b[4];
    size_t len = 0;

    CHECK(isa_decode(halt_ra, sizeof halt_ra, &out, &len) == ISA_ERR_FIELD);
    CHECK(isa_decode(movi_rb, sizeof movi_rb, &out, &len) == ISA_ERR_FIELD);
    CHECK(isa_decode(jmp_ra, sizeof jmp_ra, &out, &len) == ISA_ERR_FIELD);
    CHECK(isa_decode(shli_rb, sizeof shli_rb, &out, &len) == ISA_ERR_FIELD);
    CHECK(isa_decode(movst_ok, sizeof movst_ok, &out, &len) == ISA_OK);

    in = mk(OP_RET, 0, 0, 4, 0);
    CHECK(isa_encode(&in, b, sizeof b, &len) == ISA_ERR_FIELD);
    in = mk(OP_CMP, 1, 2, 2, 0);
    CHECK(isa_encode(&in, b, sizeof b, &len) == ISA_ERR_FIELD);
}

static void test_truncated_input(void)
{
    uint8_t movi[4] = {0x10, 0x11, 0x05, 0x00};
    isa_insn out;
    size_t len = 0;

    CHECK(isa_decode(movi, 0, &out, &len) == ISA_ERR_TRUNC);
    CHECK(isa_decode(movi, 1, &out, &len) == ISA_ERR_TRUNC);
    CHECK(isa_decode(movi, 2, &out, &len) == ISA_ERR_TRUNC); /* EXT missing */
    CHECK(isa_decode(movi, 3, &out, &len) == ISA_ERR_TRUNC);
    CHECK(isa_decode(movi, 4, &out, &len) == ISA_OK);
}

static void test_encode_errors(void)
{
    isa_insn in;
    uint8_t b[4];
    size_t len = 0;

    in = mk(OP_ADD, 0, 16, 1, 0);
    CHECK(isa_encode(&in, b, sizeof b, &len) == ISA_ERR_REG);
    in = mk(OP_ADD, 0, 1, 16, 0);
    CHECK(isa_encode(&in, b, sizeof b, &len) == ISA_ERR_REG);
    in = mk(0xE, 0, 0, 0, 0);
    CHECK(isa_encode(&in, b, sizeof b, &len) == ISA_ERR_OPCODE);
    in = mk(OP_ADD, 4, 1, 2, 0);
    CHECK(isa_encode(&in, b, sizeof b, &len) == ISA_ERR_MODE);
    in = mk(OP_MOV, 1, 1, 0, 5); /* needs 4 bytes */
    CHECK(isa_encode(&in, b, 3, &len) == ISA_ERR_SPACE);
    in = mk(OP_ADD, 0, 1, 2, 0); /* needs 2 bytes */
    CHECK(isa_encode(&in, b, 1, &len) == ISA_ERR_SPACE);
}

static void dis(unsigned op, unsigned mode, unsigned ra, unsigned rb, unsigned ext, const char *want)
{
    char buf[64];
    isa_insn in = mk(op, mode, ra, rb, ext);
    in.has_ext = (uint8_t)(isa_needs_ext(op, mode) == 1);
    isa_disasm(&in, buf, sizeof buf);
    CHECK_STR(buf, want);
}

/* One case per operand form; register 13/14 print as FP/SP. */
static void test_disasm(void)
{
    dis(OP_SYS, SYS_HALT, 0, 0, 0, "HALT");
    dis(OP_SYS, SYS_NOP, 0, 0, 0, "NOP");

    dis(OP_MOV, 0, 1, 2, 0, "MOV R1, R2");
    dis(OP_MOV, 1, 1, 0, 5, "MOV R1, #5");
    dis(OP_MOV, 2, 0, 2, 0, "MOV R0, [R2]");
    dis(OP_MOV, 3, 3, REG_FP, 4, "MOV R3, [FP+4]");
    dis(OP_MOV, 4, REG_SP, 1, 0, "MOV [SP], R1");
    dis(OP_MOV, 5, REG_FP, 0, 6, "MOV [FP+6], R0");
    dis(OP_MOV, 6, 0, 0, 0x0100, "MOV R0, [0x0100]");
    dis(OP_MOV, 7, 0, 0, 0x0102, "MOV [0x0102], R0");
    dis(OP_MOV, 0, REG_FP, REG_SP, 0, "MOV FP, SP");

    dis(OP_ADD, 0, 1, 2, 0, "ADD R1, R2");
    dis(OP_SUB, 1, REG_SP, 0, 2, "SUB SP, #2");
    dis(OP_MUL, 2, 0, 2, 0, "MUL R0, [R2]");
    dis(OP_DIV, 3, 0, 2, 2, "DIV R0, [R2+2]");
    dis(OP_AND, 0, 4, 5, 0, "AND R4, R5");
    dis(OP_OR, 1, 4, 0, 0x8000, "OR R4, #32768");
    dis(OP_XOR, 1, 1, 0, 0xFFFF, "XOR R1, #65535");

    dis(OP_SHIFT, SHIFT_SHL_R, 1, 2, 0, "SHL R1, R2");
    dis(OP_SHIFT, SHIFT_SHL_I, 1, 0, 1, "SHL R1, #1");
    dis(OP_SHIFT, SHIFT_SHR_R, 1, 2, 0, "SHR R1, R2");
    dis(OP_SHIFT, SHIFT_SHR_I, 1, 0, 3, "SHR R1, #3");

    dis(OP_CMP, 0, 1, 2, 0, "CMP R1, R2");
    dis(OP_CMP, 1, 1, 0, 0, "CMP R1, #0");

    dis(OP_JMP, JMP_ALWAYS, 0, 0, 0x0010, "JMP 0x0010");
    dis(OP_JMP, JMP_EQ, 0, 0, 0x0010, "JEQ 0x0010");
    dis(OP_JMP, JMP_NE, 0, 0, 0x0010, "JNE 0x0010");
    dis(OP_JMP, JMP_LT, 0, 0, 0x0010, "JLT 0x0010");
    dis(OP_JMP, JMP_GE, 0, 0, 0x0010, "JGE 0x0010");
    dis(OP_JMP, JMP_GT, 0, 0, 0x0010, "JGT 0x0010");
    dis(OP_JMP, JMP_LE, 0, 0, 0x00FE, "JLE 0x00FE");

    dis(OP_CALL, 0, 0, 0, 0x0040, "CALL 0x0040");
    dis(OP_RET, 0, 0, 0, 0, "RET");

    dis(0xE, 0, 0, 0, 0, "???");
}

static void test_names(void)
{
    CHECK_STR(isa_op_name(OP_SYS), "SYS");
    CHECK_STR(isa_op_name(OP_SHIFT), "SHIFT");
    CHECK_STR(isa_op_name(OP_RET), "RET");
    CHECK_STR(isa_op_name(0xE), "???");
    CHECK(isa_sets_flags(OP_ADD) == 1);
    CHECK(isa_sets_flags(OP_CMP) == 1);
    CHECK(isa_sets_flags(OP_MOV) == 0);
    CHECK(isa_sets_flags(OP_JMP) == 0);
    CHECK(isa_sets_flags(0xF) == 0);
    CHECK(strlen(isa_strerror(ISA_ERR_FIELD)) > 0);
}

int main(void)
{
    test_known_encodings();
    test_needs_ext_table();
    test_length();
    test_round_trip_all();
    test_decode_rejects_reserved_opcodes();
    test_decode_rejects_bad_modes();
    test_unused_fields_must_be_zero();
    test_truncated_input();
    test_encode_errors();
    test_disasm();
    test_names();

    printf("%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
