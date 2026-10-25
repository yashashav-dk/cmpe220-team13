/*
 * sim.h -- t13sim: the T13 CPU simulator.
 *
 * The simulator is split the way the CPU diagram in the report is drawn:
 *
 *   load.c   loader: reads a memory image (docs/image.md) into RAM and
 *            resets the registers.
 *   cpu.c    the CPU: fetch -> decode -> execute -> store, one instruction
 *            per sim_step() call. Control unit, ALU and register file live
 *            here (docs/isa.md sections 2-6).
 *   trace.c  trace writer: everything t13view needs (docs/trace.md).
 *   main.c   the command line.
 *
 * Nothing in here knows about the assembler; the only shared code is the
 * instruction codec in include/isa.h.
 */
#ifndef T13_SIM_H
#define T13_SIM_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "isa.h"

/* Memory map (docs/isa.md section 6). */
enum {
    SIM_MEM_SIZE = 0x0400,    /* 1 KiB of byte-addressed RAM            */
    SIM_TEXT_BASE = 0x0000,   /* Text  0000-00FF: code, never written   */
    SIM_DATA_BASE = 0x0100,   /* Data  0100-017F: globals and statics   */
    SIM_HEAP_BASE = 0x0180,   /* Heap  0180-027F: grows up              */
    SIM_STACK_BASE = 0x0280,  /* Stack 0280-03FF: grows down            */
    SIM_SP_INIT = 0x0400      /* SP after reset: the stack is empty     */
};

enum {
    SIM_MAX_SYMS = 128,       /* same limit as the assembler            */
    SIM_NAME_MAX = 32,        /* longest symbol name                    */
    SIM_MSG_MAX = 96          /* longest error message                  */
};

/* One SYM line of the image: a label and its address. */
typedef struct {
    char name[SIM_NAME_MAX + 1];
    uint16_t addr;
} sim_sym;

/* The whole machine: RAM plus the registers of the CPU diagram. */
typedef struct {
    uint8_t mem[SIM_MEM_SIZE]; /* RAM, little-endian 16-bit words        */
    uint16_t reg[REG_COUNT];   /* R0-R15 (R13 = FP, R14 = SP)             */
    uint16_t pc;               /* program counter                         */
    uint16_t ir;               /* instruction register: last word fetched */
    uint16_t ir_ext;           /* ... and its EXT word, if it had one     */
    uint8_t z, n, c;           /* FLAGS                                   */
    uint16_t entry;            /* ENTRY of the image                      */
    sim_sym sym[SIM_MAX_SYMS]; /* SYM lines, in image order               */
    unsigned nsym;
} sim_cpu;

/* What one instruction did: the input to one STEP block of the trace. */
typedef struct {
    uint16_t pc;              /* address it was fetched from        */
    uint16_t ir;              /* instruction word                   */
    uint16_t ext;             /* EXT word (valid if has_ext)        */
    uint8_t has_ext;
    isa_insn insn;            /* decoded form                       */
    unsigned nwrites;         /* memory words written (0 or 1)      */
    struct {
        uint16_t addr, before, after;
    } write[2];
    char msg[SIM_MSG_MAX];    /* error text when sim_step faults    */
} sim_step_info;

typedef enum {
    SIM_RUNNING = 0, /* instruction done, machine still running         */
    SIM_HALTED,      /* HALT executed                                   */
    SIM_FAULT        /* instruction failed; machine state is unchanged */
} sim_status;

/* ---- load.c ---------------------------------------------------------- */

/* Reset `cpu` and load the image in text[0..len). Returns 0 on success.
 * On error returns the 1-based line number of the bad line (or 1 for an
 * empty file) and puts the message in err. */
int sim_load(sim_cpu *cpu, const char *text, size_t len, char *err, size_t errcap);

/* Registers after loading: all 0, SP = 0400, FLAGS = 0, PC = entry. */
void sim_reset_registers(sim_cpu *cpu, uint16_t entry);

/* ---- cpu.c ----------------------------------------------------------- */

/* Run one instruction. On SIM_FAULT, info->msg says why, info->pc is the
 * address of the failed instruction, and `cpu` is exactly as before. */
sim_status sim_step(sim_cpu *cpu, sim_step_info *info);

/* The 16-bit word at an even address below SIM_MEM_SIZE. */
uint16_t sim_word(const sim_cpu *cpu, uint16_t addr);

/* ---- trace.c --------------------------------------------------------- */

typedef enum { SIM_END_HALT, SIM_END_LIMIT, SIM_END_ERROR } sim_end;

/* Summary of a whole run. */
typedef struct {
    sim_end end;
    unsigned steps;          /* instructions executed (STEP blocks)  */
    uint16_t pc;             /* PC as in the END line                 */
    char msg[SIM_MSG_MAX];   /* error text when end == SIM_END_ERROR  */
} sim_result;

/* Run from the current state until HALT, an error, or max_steps
 * instructions, writing the trace to `out` (NULL: no trace). */
void sim_run(sim_cpu *cpu, FILE *out, unsigned max_steps, sim_result *res);

#endif /* T13_SIM_H */
