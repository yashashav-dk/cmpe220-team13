/*
 * test_sim.c -- tests for t13sim: the loader (load.c), the CPU (cpu.c)
 * and the trace writer (trace.c).
 *
 * Programs are poked into RAM as raw words, so these tests do not depend
 * on the assembler. Expected flags follow docs/isa.md section 4; expected
 * trace text follows docs/trace.md. Run with `make test`.
 */
#include <stdio.h>
#include <string.h>

#include "sim.h"

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

static sim_cpu cpu;
static sim_step_info info;
static char err[128];

/* ---- helpers ---------------------------------------------------------- */

static void reset(void)
{
    memset(&cpu, 0, sizeof cpu);
    sim_reset_registers(&cpu, 0);
}

static void poke(uint16_t addr, uint16_t word)
{
    cpu.mem[addr] = (uint8_t)(word & 0xFFu);
    cpu.mem[addr + 1] = (uint8_t)(word >> 8);
}

/* Put the program words[] at address 0 and reset the registers. */
static void program(const uint16_t *words, size_t n)
{
    size_t i;
    reset();
    for (i = 0; i < n; i++)
        poke((uint16_t)(2 * i), words[i]);
}

#define PROGRAM(...)                                                     \
    do {                                                                 \
        const uint16_t w_[] = {__VA_ARGS__};                             \
        program(w_, sizeof w_ / sizeof w_[0]);                           \
    } while (0)

static int load(const char *text)
{
    return sim_load(&cpu, text, strlen(text), err, sizeof err);
}

/* ---- loader ----------------------------------------------------------- */

static const char POWER_IMG[] =
    "T13IMG 1\n"
    "ENTRY 0000\n"
    "SYM main 0000\n"
    "SYM loop 000C\n"
    "SYM done 001E\n"
    "SYM result 0100\n"
    "SEG TEXT 0000\n"
    "0000: 1100 0001      ; MOV R0, #1\n"
    "0004: 1110 0008      ; MOV R1, #8\n"
    "0008: 1120 0002      ; MOV R2, #2\n"
    "000C: A120 0000      ; CMP R2, #0\n"
    "0010: B100 001E      ; JEQ 0x001E\n"
    "0014: 4001           ; MUL R0, R1\n"
    "0016: 3120 0001      ; SUB R2, #1\n"
    "001A: B000 000C      ; JMP 0x000C\n"
    "001E: 1700 0100      ; MOV [0x0100], R0\n"
    "0022: 0000           ; HALT\n"
    "SEG DATA 0100\n"
    "0100: 0000           ; result\n";

static void test_load_power(void)
{
    unsigned i;
    int nonzero = 0;

    CHECK_EQ(load(POWER_IMG), 0);
    CHECK_EQ(cpu.entry, 0x0000);
    CHECK_EQ(cpu.pc, 0x0000);
    CHECK_EQ(cpu.reg[REG_SP], 0x0400);
    CHECK_EQ(cpu.reg[0], 0);
    CHECK_EQ(cpu.z | cpu.n | cpu.c, 0);
    /* little-endian: 1100 0001 -> bytes 00 11 01 00 */
    CHECK_EQ(cpu.mem[0], 0x00);
    CHECK_EQ(cpu.mem[1], 0x11);
    CHECK_EQ(cpu.mem[2], 0x01);
    CHECK_EQ(cpu.mem[3], 0x00);
    CHECK_EQ(sim_word(&cpu, 0x0014), 0x4001);
    CHECK_EQ(sim_word(&cpu, 0x0020), 0x0100);
    CHECK_EQ(sim_word(&cpu, 0x0100), 0x0000);
    for (i = 0x0024; i < SIM_MEM_SIZE; i++)
        nonzero |= cpu.mem[i];
    CHECK_EQ(nonzero, 0);
    CHECK_EQ(cpu.nsym, 4);
    CHECK_STR(cpu.sym[1].name, "loop");
    CHECK_EQ(cpu.sym[1].addr, 0x000C);
    CHECK_STR(cpu.sym[3].name, "result");
    CHECK_EQ(cpu.sym[3].addr, 0x0100);
}

static void test_load_lenient_input(void)
{
    /* CRLF, comments, blank lines and lower-case hex are all accepted. */
    CHECK_EQ(load("T13IMG 1\r\n; a comment\r\n\r\nENTRY 0004\r\n"
                  "SEG TEXT 0000\r\n0004: 11a0 00ff ; MOV R10, #255\r\n"),
             0);
    CHECK_EQ(cpu.entry, 0x0004);
    CHECK_EQ(cpu.pc, 0x0004);
    CHECK_EQ(sim_word(&cpu, 0x0004), 0x11A0);
    CHECK_EQ(sim_word(&cpu, 0x0006), 0x00FF);
    CHECK_EQ(cpu.nsym, 0);
}

static void test_load_resets_old_state(void)
{
    reset();
    cpu.mem[0x200] = 0x55;
    cpu.reg[3] = 7;
    CHECK_EQ(load("T13IMG 1\nENTRY 0000\nSEG TEXT 0000\n0000: 0000\n"), 0);
    CHECK_EQ(cpu.mem[0x200], 0);
    CHECK_EQ(cpu.reg[3], 0);
}

static void test_load_errors(void)
{
    CHECK_EQ(load(""), 1);
    CHECK_STR(err, "empty image");
    CHECK_EQ(load("T13IMG 2\nENTRY 0000\n"), 1);
    CHECK_STR(err, "expected 'T13IMG 1'");
    CHECK_EQ(load("T13IMG 1\nSEG TEXT 0000\n"), 2);
    CHECK_STR(err, "expected 'ENTRY hhhh'");
    CHECK_EQ(load("T13IMG 1\nENTRY 0001\n"), 2);
    CHECK_STR(err, "ENTRY 0x0001 must be even and inside Text (0x0000-0x00FF)");
    CHECK_EQ(load("T13IMG 1\nENTRY 0000\n0000: 0000\n"), 3);
    CHECK_STR(err, "record before any SEG line");
    CHECK_EQ(load("T13IMG 1\nENTRY 0000\nSEG BSS 0000\n"), 3);
    CHECK_STR(err, "unknown segment 'BSS'");
    CHECK_EQ(load("T13IMG 1\nENTRY 0000\nSEG TEXT 0000\n0001: 0000\n"), 4);
    CHECK_STR(err, "odd address 0x0001");
    CHECK_EQ(load("T13IMG 1\nENTRY 0000\nSEG TEXT 0000\n0000: 0000\n0000: 0100\n"), 5);
    CHECK_STR(err, "address 0x0000 loaded twice");
    CHECK_EQ(load("T13IMG 1\nENTRY 0000\nSEG TEXT 0000\n00FE: 0000 0000\n"), 4);
    CHECK_STR(err, "address 0x0100 is outside TEXT (0x0000-0x00FF)");
    CHECK_EQ(load("T13IMG 1\nENTRY 0000\nSEG DATA 0100\n0180: 0000\n"), 4);
    CHECK_STR(err, "address 0x0180 is outside DATA (0x0100-0x017F)");
    CHECK_EQ(load("T13IMG 1\nENTRY 0000\nSEG TEXT 0000\n0000: 12G4\n"), 4);
    CHECK_STR(err, "bad word '12G4'");
    CHECK_EQ(load("T13IMG 1\nENTRY 0000\nSEG TEXT 0000\n0000:\n"), 4);
    CHECK_STR(err, "record has no words");
    CHECK_EQ(load("T13IMG 1\nENTRY 0000\nSEG TEXT 0000\nhello\n"), 4);
    CHECK_STR(err, "expected 'hhhh: hhhh ...'");
    CHECK_EQ(load("T13IMG 1\nENTRY 0000\nSYM 0000\n"), 3);
    CHECK_STR(err, "expected 'SYM name hhhh'");
}

/* ---- CPU: data movement ---------------------------------------------- */

static void test_mov_forms(void)
{
    PROGRAM(0x1110, 0x0005,  /* 0000 MOV R1, #5         */
            0x1021,          /* 0004 MOV R2, R1         */
            0x1730, 0x0100,  /* 0006 MOV [0x0100], R3   */
            0x1640, 0x0100,  /* 000A MOV R4, [0x0100]   */
            0x1452,          /* 000E MOV [R5], R2       */
            0x1265,          /* 0010 MOV R6, [R5]       */
            0x1557, 0x0004,  /* 0012 MOV [R5+4], R7     */
            0x1385, 0x0004); /* 0016 MOV R8, [R5+4]     */
    cpu.reg[3] = 0xBEEF;
    cpu.reg[5] = 0x0102;
    cpu.reg[7] = 0x1234;
    cpu.z = 1; /* MOV must not touch FLAGS */

    CHECK_EQ(sim_step(&cpu, &info), SIM_RUNNING);
    CHECK_EQ(cpu.reg[1], 5);
    CHECK_EQ(cpu.pc, 0x0004);
    CHECK_EQ(info.pc, 0x0000);
    CHECK_EQ(info.ir, 0x1110);
    CHECK_EQ(info.has_ext, 1);
    CHECK_EQ(info.ext, 0x0005);
    CHECK_EQ(info.nwrites, 0);

    CHECK_EQ(sim_step(&cpu, &info), SIM_RUNNING);
    CHECK_EQ(cpu.reg[2], 5);
    CHECK_EQ(info.has_ext, 0);
    CHECK_EQ(cpu.pc, 0x0006);

    CHECK_EQ(sim_step(&cpu, &info), SIM_RUNNING);
    CHECK_EQ(sim_word(&cpu, 0x0100), 0xBEEF);
    CHECK_EQ(info.nwrites, 1);
    CHECK_EQ(info.write[0].addr, 0x0100);
    CHECK_EQ(info.write[0].before, 0x0000);
    CHECK_EQ(info.write[0].after, 0xBEEF);

    CHECK_EQ(sim_step(&cpu, &info), SIM_RUNNING);
    CHECK_EQ(cpu.reg[4], 0xBEEF);
    CHECK_EQ(info.nwrites, 0);

    CHECK_EQ(sim_step(&cpu, &info), SIM_RUNNING);
    CHECK_EQ(sim_word(&cpu, 0x0102), 5);

    CHECK_EQ(sim_step(&cpu, &info), SIM_RUNNING);
    CHECK_EQ(cpu.reg[6], 5);

    CHECK_EQ(sim_step(&cpu, &info), SIM_RUNNING);
    CHECK_EQ(sim_word(&cpu, 0x0106), 0x1234);

    CHECK_EQ(sim_step(&cpu, &info), SIM_RUNNING);
    CHECK_EQ(cpu.reg[8], 0x1234);
    CHECK_EQ(cpu.pc, 0x001A);
    CHECK_EQ(cpu.z, 1);
}

static void test_offset_wraps(void)
{
    /* [Rb+k] is computed modulo 2^16, so k = 0xFFFE means Rb - 2. */
    PROGRAM(0x1312, 0xFFFE); /* MOV R1, [R2+65534] */
    cpu.reg[2] = 0x0104;
    poke(0x0102, 0x4321);
    CHECK_EQ(sim_step(&cpu, &info), SIM_RUNNING);
    CHECK_EQ(cpu.reg[1], 0x4321);
}

/* ---- CPU: ALU and flags ---------------------------------------------- */

/* Run one ALU instruction `word` with EXT `ext` on R1 = a (and R2 = b). */
static void alu(uint16_t word, uint16_t ext, uint16_t a, uint16_t b)
{
    PROGRAM(word, ext);
    cpu.reg[1] = a;
    cpu.reg[2] = b;
    CHECK_EQ(sim_step(&cpu, &info), SIM_RUNNING);
}

#define FLAGS(zv, nv, cv)                                                \
    do {                                                                 \
        CHECK_EQ(cpu.z, (zv));                                           \
        CHECK_EQ(cpu.n, (nv));                                           \
        CHECK_EQ(cpu.c, (cv));                                           \
    } while (0)

static void test_alu_flags(void)
{
    alu(0x2110, 1, 0xFFFF, 0);      /* ADD R1, #1: carry out, zero */
    CHECK_EQ(cpu.reg[1], 0x0000);
    FLAGS(1, 0, 1);
    alu(0x2012, 0, 0x7FFF, 1);      /* ADD R1, R2 */
    CHECK_EQ(cpu.reg[1], 0x8000);
    FLAGS(0, 1, 0);
    alu(0x3110, 2, 1, 0);           /* SUB R1, #2: borrow */
    CHECK_EQ(cpu.reg[1], 0xFFFF);
    FLAGS(0, 1, 1);
    alu(0x3012, 0, 5, 5);           /* SUB R1, R2: zero, no borrow */
    FLAGS(1, 0, 0);
    alu(0x4110, 0x0100, 0x0100, 0); /* MUL R1, #256: overflow */
    CHECK_EQ(cpu.reg[1], 0x0000);
    FLAGS(1, 0, 1);
    alu(0x4110, 8, 5040, 0);        /* MUL: 7! * 8 = 40320 fits unsigned */
    CHECK_EQ(cpu.reg[1], 0x9D80);
    FLAGS(0, 1, 0);
    alu(0x5110, 2, 7, 0);           /* DIV R1, #2 */
    CHECK_EQ(cpu.reg[1], 3);
    FLAGS(0, 0, 0);
    alu(0x6110, 0x00FF, 0x1234, 0); /* AND */
    CHECK_EQ(cpu.reg[1], 0x0034);
    alu(0x7012, 0, 0x1200, 0x0034); /* OR R1, R2 */
    CHECK_EQ(cpu.reg[1], 0x1234);
    alu(0x8110, 0xFFFF, 0x00FF, 0); /* XOR R1, #0xFFFF = NOT */
    CHECK_EQ(cpu.reg[1], 0xFF00);
    FLAGS(0, 1, 0);
}

static void test_alu_memory_source(void)
{
    PROGRAM(0x2212,          /* ADD R1, [R2]   */
            0x231D, 0x0004); /* ADD R1, [FP+4] */
    cpu.reg[1] = 1;
    cpu.reg[2] = 0x0100;
    cpu.reg[REG_FP] = 0x03F8;
    poke(0x0100, 10);
    poke(0x03FC, 100);
    CHECK_EQ(sim_step(&cpu, &info), SIM_RUNNING);
    CHECK_EQ(cpu.reg[1], 11);
    CHECK_EQ(sim_step(&cpu, &info), SIM_RUNNING);
    CHECK_EQ(cpu.reg[1], 111);
}

static void test_shift(void)
{
    alu(0x9110, 1, 0x8001, 0); /* SHL R1, #1: bit 15 goes to C */
    CHECK_EQ(cpu.reg[1], 0x0002);
    FLAGS(0, 0, 1);
    alu(0x9310, 1, 0x0003, 0); /* SHR R1, #1: bit 0 goes to C */
    CHECK_EQ(cpu.reg[1], 0x0001);
    FLAGS(0, 0, 1);
    alu(0x9110, 0, 0x8001, 0); /* count 0: unchanged, C = 0 */
    CHECK_EQ(cpu.reg[1], 0x8001);
    FLAGS(0, 1, 0);
    alu(0x9012, 0, 0x0001, 17); /* SHL R1, R2: count is R2 & 15 = 1 */
    CHECK_EQ(cpu.reg[1], 0x0002);
    alu(0x9212, 0, 0x8000, 15); /* SHR R1, R2 by 15 */
    CHECK_EQ(cpu.reg[1], 0x0001);
    FLAGS(0, 0, 0);
}

static void test_cmp(void)
{
    alu(0xA012, 0, 3, 5); /* CMP R1, R2: 3 < 5 */
    CHECK_EQ(cpu.reg[1], 3); /* Ra unchanged */
    FLAGS(0, 1, 1);
    alu(0xA110, 0x9D80, 0x9D80, 0); /* CMP R1, #40320 */
    FLAGS(1, 0, 0);
    alu(0xA110, 1, 0x9D80, 0); /* unsigned: 40320 > 1 */
    FLAGS(0, 1, 0);
}

/* ---- CPU: control flow ----------------------------------------------- */

/* After CMP R1, R2 with R1 = a and R2 = b, does JMP mode `cond` jump? */
static int jumps(unsigned cond, uint16_t a, uint16_t b)
{
    PROGRAM(0xA012, (uint16_t)(0xB000 | (cond << 8)), 0x0040);
    cpu.reg[1] = a;
    cpu.reg[2] = b;
    sim_step(&cpu, &info);
    CHECK_EQ(sim_step(&cpu, &info), SIM_RUNNING);
    return cpu.pc == 0x0040;
}

static void test_jumps(void)
{
    CHECK(jumps(JMP_ALWAYS, 1, 2));
    CHECK(jumps(JMP_EQ, 4, 4));
    CHECK(!jumps(JMP_EQ, 4, 5));
    CHECK(jumps(JMP_NE, 4, 5));
    CHECK(!jumps(JMP_NE, 4, 4));
    CHECK(jumps(JMP_LT, 1, 0x9D80));  /* unsigned compare */
    CHECK(!jumps(JMP_LT, 0x9D80, 1));
    CHECK(!jumps(JMP_LT, 4, 4));
    CHECK(jumps(JMP_GE, 4, 4));
    CHECK(jumps(JMP_GE, 0x9D80, 1));
    CHECK(!jumps(JMP_GE, 1, 2));
    CHECK(jumps(JMP_GT, 5, 4));
    CHECK(!jumps(JMP_GT, 4, 4));
    CHECK(!jumps(JMP_GT, 3, 4));
    CHECK(jumps(JMP_LE, 4, 4));
    CHECK(jumps(JMP_LE, 3, 4));
    CHECK(!jumps(JMP_LE, 5, 4));
    /* A jump not taken falls through to the next instruction. */
    jumps(JMP_EQ, 1, 2);
    CHECK_EQ(cpu.pc, 0x0006);
}

static void test_call_ret(void)
{
    PROGRAM(0xC000, 0x0006, /* 0000 CALL 0x0006 */
            0x0000,         /* 0004 HALT        */
            0xD000);        /* 0006 RET         */
    CHECK_EQ(sim_step(&cpu, &info), SIM_RUNNING);
    CHECK_EQ(cpu.pc, 0x0006);
    CHECK_EQ(cpu.reg[REG_SP], 0x03FE);
    CHECK_EQ(sim_word(&cpu, 0x03FE), 0x0004); /* address after the CALL */
    CHECK_EQ(info.nwrites, 1);
    CHECK_EQ(info.write[0].addr, 0x03FE);
    CHECK_EQ(info.write[0].after, 0x0004);
    CHECK_EQ(sim_step(&cpu, &info), SIM_RUNNING);
    CHECK_EQ(cpu.pc, 0x0004);
    CHECK_EQ(cpu.reg[REG_SP], 0x0400);
    CHECK_EQ(info.nwrites, 0);
    CHECK_EQ(sim_word(&cpu, 0x03FE), 0x0004); /* RET only reads */
    CHECK_EQ(sim_step(&cpu, &info), SIM_HALTED);
    CHECK_EQ(cpu.pc, 0x0006); /* PC after HALT is the next address */
}

static void test_halt_nop(void)
{
    PROGRAM(0x0100, 0x0000); /* NOP; HALT */
    CHECK_EQ(sim_step(&cpu, &info), SIM_RUNNING);
    CHECK_EQ(cpu.pc, 2);
    CHECK_EQ(sim_step(&cpu, &info), SIM_HALTED);
    CHECK_EQ(info.pc, 2);
    CHECK_EQ(cpu.pc, 4);
}

/* ---- CPU: faults leave the machine unchanged ------------------------- */

static sim_cpu before;

/* Step once, expect SIM_FAULT with message `msg` and no state change. */
static void fault(const char *msg)
{
    memcpy(&before, &cpu, sizeof cpu);
    CHECK_EQ(sim_step(&cpu, &info), SIM_FAULT);
    CHECK_STR(info.msg, msg);
    CHECK(memcmp(&before, &cpu, sizeof cpu) == 0);
}

static void test_faults(void)
{
    PROGRAM(0xE000);
    fault("illegal instruction E000 (reserved opcode)");
    CHECK_EQ(info.pc, 0x0000);

    PROGRAM(0x0200);
    fault("illegal instruction 0200 (undefined mode)");

    PROGRAM(0x0010);
    fault("illegal instruction 0010 (unused register field is not 0)");

    PROGRAM(0x5110, 0x0000); /* DIV R1, #0 */
    cpu.reg[1] = 9;
    fault("divide by zero");

    PROGRAM(0x1710, 0x0040); /* MOV [0x0040], R1 */
    fault("write into Text at 0x0040");

    PROGRAM(0x1610, 0x0101); /* MOV R1, [0x0101] */
    fault("odd address 0x0101");

    PROGRAM(0x1312, 0x0002); /* MOV R1, [R2+2] with R2 = 03FE */
    cpu.reg[2] = 0x03FE;
    fault("address 0x0400 is outside memory (0x0000-0x03FF)");

    PROGRAM(0xD000); /* RET with an empty stack reads M[0400] */
    fault("address 0x0400 is outside memory (0x0000-0x03FF)");

    PROGRAM(0x31E0, 0x0002); /* SUB SP, #2 at the stack limit */
    cpu.reg[REG_SP] = 0x0280;
    fault("stack overflow: SP 0x027E is below 0x0280");

    PROGRAM(0xC000, 0x0000); /* CALL at the stack limit */
    cpu.reg[REG_SP] = 0x0280;
    fault("stack overflow: SP 0x027E is below 0x0280");

    reset();
    cpu.pc = 0x0001;
    fault("fetch from odd address 0x0001");

    reset();
    cpu.pc = 0x0400;
    fault("fetch from 0x0400, outside memory (0x0000-0x03FF)");

    reset();
    cpu.pc = 0x03FE;
    poke(0x03FE, 0x1110); /* MOV R1, #k, but EXT would be at 0400 */
    fault("illegal instruction 1110 (runs past the end of memory)");
}

/* ---- trace writer ---------------------------------------------------- */

/* Run `img` with sim_run and return the whole trace text. */
static const char *run_trace(const char *img, unsigned max_steps, sim_result *res)
{
    static char buf[16384];
    FILE *f = tmpfile();
    size_t n;

    buf[0] = '\0';
    if (f == NULL || load(img) != 0) {
        CHECK(f != NULL);
        return buf;
    }
    sim_run(&cpu, f, max_steps, res);
    rewind(f);
    n = fread(buf, 1, sizeof buf - 1, f);
    buf[n] = '\0';
    fclose(f);
    return buf;
}

#define REGS0 "REGS R0=0000 R1=0000 R2=0000 R3=0000 R4=0000 R5=0000 R6=0000 R7=0000 " \
              "R8=0000 R9=0000 R10=0000 R11=0000 R12=0000 FP=0000 SP=0400 R15=0000\n"

static void test_trace_halt(void)
{
    sim_result res;
    const char *t = run_trace("T13IMG 1\nENTRY 0000\nSYM main 0000\nSEG TEXT 0000\n"
                              "0000: 0000           ; HALT\n",
                              100, &res);
    CHECK_STR(t, "T13TRC 1\nENTRY 0000\nSYM main 0000\n" REGS0 "FLAGS Z=0 N=0 C=0\n"
                 "STEP 1 PC=0000 IR=0000 ; HALT\n" REGS0 "FLAGS Z=0 N=0 C=0\n"
                 "END HALT PC=0002 STEPS=1\n");
    CHECK_EQ(res.end, SIM_END_HALT);
    CHECK_EQ(res.steps, 1);
    CHECK_EQ(res.pc, 0x0002);
}

static void test_trace_limit(void)
{
    sim_result res;
    const char *t = run_trace("T13IMG 1\nENTRY 0000\nSEG TEXT 0000\n0000: B000 0000\n", 3, &res);
    CHECK(strstr(t, "LOAD 0000: B000 0000 0000 0000 0000 0000 0000 0000\n") != NULL);
    CHECK(strstr(t, "STEP 3 PC=0000 IR=B000 EXT=0000 ; JMP 0x0000\n") != NULL);
    CHECK(strstr(t, "STEP 4") == NULL);
    CHECK(strstr(t, "\nEND LIMIT PC=0000 STEPS=3\n") != NULL);
    CHECK_EQ(res.end, SIM_END_LIMIT);
    CHECK_EQ(res.steps, 3);
}

static void test_trace_error(void)
{
    sim_result res;
    const char *t = run_trace("T13IMG 1\nENTRY 0000\nSEG TEXT 0000\n"
                              "0000: 1110 0009 5110 0000\n",
                              100, &res);
    /* The failed instruction gets no STEP block. */
    CHECK(strstr(t, "STEP 1 PC=0000 IR=1110 EXT=0009 ; MOV R1, #9\n") != NULL);
    CHECK(strstr(t, "STEP 2") == NULL);
    CHECK(strstr(t, "\nEND ERROR PC=0004 STEPS=1 divide by zero\n") != NULL);
    CHECK_EQ(res.end, SIM_END_ERROR);
    CHECK_STR(res.msg, "divide by zero");
    CHECK_EQ(res.pc, 0x0004);
}

static void test_trace_mem_line(void)
{
    sim_result res;
    const char *t = run_trace("T13IMG 1\nENTRY 0000\nSEG TEXT 0000\n"
                              "0000: 1110 ABCD 1710 0100 0000\n",
                              100, &res);
    CHECK(strstr(t, "STEP 2 PC=0004 IR=1710 EXT=0100 ; MOV [0x0100], R1\n") != NULL);
    CHECK(strstr(t, "FLAGS Z=0 N=0 C=0\nMEM 0100: 0000 -> ABCD\nSTEP 3") != NULL);
}

int main(void)
{
    test_load_power();
    test_load_lenient_input();
    test_load_resets_old_state();
    test_load_errors();
    test_mov_forms();
    test_offset_wraps();
    test_alu_flags();
    test_alu_memory_source();
    test_shift();
    test_cmp();
    test_jumps();
    test_call_ret();
    test_halt_nop();
    test_faults();
    test_trace_halt();
    test_trace_limit();
    test_trace_error();
    test_trace_mem_line();

    printf("%d checks, %d failed\n", checks, failures);
    return failures != 0;
}
