/*
 * trace.c -- run a loaded program and write its execution trace.
 *
 * Format: docs/trace.md. In short:
 *
 *     T13TRC 1 / ENTRY / SYM*          header, copied from the image
 *     LOAD hhhh: 8 words               memory right after loading
 *     REGS ... / FLAGS ...             state after loading
 *     STEP n PC= IR= [EXT=] ; text     one block per executed instruction:
 *       REGS / FLAGS / MEM*            state after it, each word it wrote
 *     END HALT|LIMIT|ERROR ...         exactly one, last
 */
#include "sim.h"

#include <string.h>

static const char *const REG_NAMES[REG_COUNT] = {
    "R0", "R1", "R2", "R3", "R4", "R5", "R6", "R7",
    "R8", "R9", "R10", "R11", "R12", "FP", "SP", "R15",
};

static void write_state(FILE *out, const sim_cpu *cpu)
{
    unsigned r;

    fputs("REGS", out);
    for (r = 0; r < REG_COUNT; r++)
        fprintf(out, " %s=%04X", REG_NAMES[r], (unsigned)cpu->reg[r]);
    fprintf(out, "\nFLAGS Z=%u N=%u C=%u\n", (unsigned)cpu->z, (unsigned)cpu->n, (unsigned)cpu->c);
}

/* Header, SYM lines, LOAD rows (16-byte rows with a non-zero word), state. */
static void write_header(FILE *out, const sim_cpu *cpu)
{
    unsigned i, row, w;

    fprintf(out, "T13TRC 1\nENTRY %04X\n", (unsigned)cpu->entry);
    for (i = 0; i < cpu->nsym; i++)
        fprintf(out, "SYM %s %04X\n", cpu->sym[i].name, (unsigned)cpu->sym[i].addr);
    for (row = 0; row < SIM_MEM_SIZE; row += 16) {
        int nonzero = 0;
        for (i = 0; i < 16; i++)
            nonzero |= cpu->mem[row + i];
        if (!nonzero)
            continue;
        fprintf(out, "LOAD %04X:", row);
        for (w = 0; w < 16; w += 2)
            fprintf(out, " %04X", (unsigned)sim_word(cpu, (uint16_t)(row + w)));
        fputc('\n', out);
    }
    write_state(out, cpu);
}

static void write_step(FILE *out, unsigned n, const sim_step_info *info, const sim_cpu *cpu)
{
    char text[48];
    unsigned i;

    fprintf(out, "STEP %u PC=%04X IR=%04X", n, (unsigned)info->pc, (unsigned)info->ir);
    if (info->has_ext)
        fprintf(out, " EXT=%04X", (unsigned)info->ext);
    isa_disasm(&info->insn, text, sizeof text);
    fprintf(out, " ; %s\n", text);
    write_state(out, cpu);
    for (i = 0; i < info->nwrites; i++)
        fprintf(out, "MEM %04X: %04X -> %04X\n", (unsigned)info->write[i].addr,
                (unsigned)info->write[i].before, (unsigned)info->write[i].after);
}

void sim_run(sim_cpu *cpu, FILE *out, unsigned max_steps, sim_result *res)
{
    sim_step_info info;
    sim_status st = SIM_RUNNING;

    memset(res, 0, sizeof *res);
    if (out != NULL)
        write_header(out, cpu);

    while (res->steps < max_steps) {
        st = sim_step(cpu, &info);
        if (st == SIM_FAULT)
            break;
        res->steps++;
        if (out != NULL)
            write_step(out, res->steps, &info, cpu);
        if (st == SIM_HALTED)
            break;
    }

    if (st == SIM_FAULT) {
        res->end = SIM_END_ERROR;
        res->pc = info.pc; /* the failed instruction */
        memcpy(res->msg, info.msg, sizeof res->msg);
    } else if (st == SIM_HALTED) {
        res->end = SIM_END_HALT;
        res->pc = cpu->pc; /* the address after HALT */
    } else {
        res->end = SIM_END_LIMIT;
        res->pc = cpu->pc; /* the next instruction, not run */
    }

    if (out == NULL)
        return;
    switch (res->end) {
    case SIM_END_HALT: fprintf(out, "END HALT PC=%04X STEPS=%u\n", (unsigned)res->pc, res->steps); break;
    case SIM_END_LIMIT: fprintf(out, "END LIMIT PC=%04X STEPS=%u\n", (unsigned)res->pc, res->steps); break;
    case SIM_END_ERROR:
        fprintf(out, "END ERROR PC=%04X STEPS=%u %s\n", (unsigned)res->pc, res->steps, res->msg);
        break;
    }
}
