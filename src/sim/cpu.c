/*
 * cpu.c -- the T13 CPU: one instruction per sim_step().
 *
 * The code follows the CPU diagram in the report, block by block:
 *
 *   RAM / bus      mem_read(), mem_check_write(): 16-bit little-endian words,
 *                  even addresses below 0400, no writes into Text.
 *   control unit   sim_step(): fetch -> decode -> execute -> store.
 *   ALU            alu(): ADD SUB MUL DIV AND OR XOR SHL SHR, sets Z N C.
 *   registers      cpu->reg[0..15], PC, IR, FLAGS.
 *
 * An instruction either completes or faults. Every check runs before
 * anything is committed, so a fault leaves the machine exactly as it was
 * and the trace's last state is the one before the failed instruction
 * (docs/trace.md rule 9). Rules: docs/isa.md sections 4-6.
 */
#include "sim.h"

#include <stdarg.h>
#include <string.h>

/* ---- RAM and bus ------------------------------------------------------ */

uint16_t sim_word(const sim_cpu *cpu, uint16_t addr)
{
    return (uint16_t)(cpu->mem[addr] | (cpu->mem[addr + 1] << 8));
}

static sim_status fault(sim_step_info *info, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(info->msg, sizeof info->msg, fmt, ap);
    va_end(ap);
    return SIM_FAULT;
}

/* 0 if a data word may be read at addr; otherwise writes the reason. */
static int mem_check(uint16_t addr, sim_step_info *info)
{
    if (addr & 1u) {
        fault(info, "odd address 0x%04X", (unsigned)addr);
        return -1;
    }
    if (addr >= SIM_MEM_SIZE) {
        fault(info, "address 0x%04X is outside memory (0x0000-0x03FF)", (unsigned)addr);
        return -1;
    }
    return 0;
}

static int mem_read(const sim_cpu *cpu, uint16_t addr, uint16_t *val, sim_step_info *info)
{
    if (mem_check(addr, info) != 0)
        return -1;
    *val = sim_word(cpu, addr);
    return 0;
}

static int mem_check_write(uint16_t addr, sim_step_info *info)
{
    if (mem_check(addr, info) != 0)
        return -1;
    if (addr < SIM_DATA_BASE) {
        fault(info, "write into Text at 0x%04X", (unsigned)addr);
        return -1;
    }
    return 0;
}

/* ---- ALU -------------------------------------------------------------- */

typedef enum { ALU_ADD, ALU_SUB, ALU_MUL, ALU_DIV, ALU_AND, ALU_OR, ALU_XOR, ALU_SHL, ALU_SHR } alu_fn;

typedef struct {
    uint16_t r;    /* result      */
    uint8_t z, n, c;
} alu_out;

/* Compute a <fn> s and its flags (docs/isa.md section 4, rule 4).
 * The caller has already refused DIV by zero. */
static alu_out alu(alu_fn fn, uint16_t a, uint16_t s)
{
    alu_out o = {0, 0, 0, 0};
    uint32_t wide;
    unsigned count = s & 15u;

    switch (fn) {
    case ALU_ADD:
        wide = (uint32_t)a + s;
        o.r = (uint16_t)wide;
        o.c = wide > 0xFFFFu;          /* carry out of bit 15 */
        break;
    case ALU_SUB:
        o.r = (uint16_t)(a - s);
        o.c = a < s;                   /* borrow (unsigned a < s) */
        break;
    case ALU_MUL:
        wide = (uint32_t)a * s;
        o.r = (uint16_t)wide;
        o.c = wide > 0xFFFFu;          /* product does not fit */
        break;
    case ALU_DIV: o.r = (uint16_t)(a / s); break;
    case ALU_AND: o.r = a & s; break;
    case ALU_OR: o.r = a | s; break;
    case ALU_XOR: o.r = a ^ s; break;
    case ALU_SHL:
        o.r = (uint16_t)(a << count);
        o.c = count ? (a >> (16 - count)) & 1u : 0; /* last bit shifted out */
        break;
    case ALU_SHR:
        o.r = (uint16_t)(a >> count);
        o.c = count ? (a >> (count - 1)) & 1u : 0;
        break;
    }
    o.z = o.r == 0;
    o.n = (o.r >> 15) & 1u;
    return o;
}

/* Does JMP mode `cond` jump, given the flags? Conditions are unsigned. */
static int condition(unsigned cond, const sim_cpu *cpu)
{
    switch (cond) {
    case JMP_ALWAYS: return 1;
    case JMP_EQ: return cpu->z;
    case JMP_NE: return !cpu->z;
    case JMP_LT: return cpu->c;
    case JMP_GE: return !cpu->c;
    case JMP_GT: return !cpu->c && !cpu->z;
    case JMP_LE: return cpu->c || cpu->z;
    }
    return 0;
}

/* Why isa_decode() refused a word, in the words of an error message. */
static const char *decode_reason(isa_status st)
{
    switch (st) {
    case ISA_ERR_OPCODE: return "reserved opcode";
    case ISA_ERR_MODE: return "undefined mode";
    case ISA_ERR_FIELD: return "unused register field is not 0";
    case ISA_ERR_TRUNC: return "runs past the end of memory";
    default: return isa_strerror(st);
    }
}

/* ---- control unit ----------------------------------------------------- */

/*
 * Source operand of an ALU / CMP / SHIFT instruction, forms 0-3:
 * Rb, #EXT, [Rb], [Rb+EXT]. SHIFT has no memory forms: its modes 0/2 are
 * "by Rb" and 1/3 are "by #EXT".
 */
static int source(const sim_cpu *cpu, const isa_insn *in, unsigned form, uint16_t *s,
                  sim_step_info *info)
{
    switch (form) {
    case 0: *s = cpu->reg[in->rb]; return 0;
    case 1: *s = in->ext; return 0;
    case 2: return mem_read(cpu, cpu->reg[in->rb], s, info);
    default: return mem_read(cpu, (uint16_t)(cpu->reg[in->rb] + in->ext), s, info);
    }
}

sim_status sim_step(sim_cpu *cpu, sim_step_info *info)
{
    isa_insn in;
    isa_status st;
    size_t len;
    uint16_t pc = cpu->pc;
    uint16_t reg[REG_COUNT];       /* register file after this instruction */
    uint16_t next_pc, s = 0, addr = 0, val = 0;
    int store = 0, halted = 0, set_flags = 0;
    alu_out o = {0, 0, 0, 0};

    memset(info, 0, sizeof *info);
    info->pc = pc;

    /* FETCH: IR <- M[PC]; EXT <- M[PC+2] if the word needs one. */
    if (pc & 1u)
        return fault(info, "fetch from odd address 0x%04X", (unsigned)pc);
    if (pc >= SIM_MEM_SIZE)
        return fault(info, "fetch from 0x%04X, outside memory (0x0000-0x03FF)", (unsigned)pc);
    info->ir = sim_word(cpu, pc);

    /* DECODE */
    st = isa_decode(cpu->mem + pc, (size_t)(SIM_MEM_SIZE - pc), &in, &len);
    if (st != ISA_OK)
        return fault(info, "illegal instruction %04X (%s)", (unsigned)info->ir, decode_reason(st));
    info->insn = in;
    info->has_ext = in.has_ext;
    info->ext = in.ext;
    next_pc = (uint16_t)(pc + len); /* PC moves on before execute */

    /* EXECUTE: work on copies; nothing is committed until every check passed. */
    memcpy(reg, cpu->reg, sizeof reg);

    switch (in.op) {
    case OP_SYS:
        halted = in.mode == SYS_HALT;
        break;

    case OP_MOV:
        switch (in.mode) {
        case 0: reg[in.ra] = cpu->reg[in.rb]; break;
        case 1: reg[in.ra] = in.ext; break;
        case 2:
        case 3:
            if (source(cpu, &in, in.mode, &reg[in.ra], info) != 0)
                return SIM_FAULT;
            break;
        case 4: store = 1; addr = cpu->reg[in.ra]; val = cpu->reg[in.rb]; break;
        case 5: store = 1; addr = (uint16_t)(cpu->reg[in.ra] + in.ext); val = cpu->reg[in.rb]; break;
        case 6:
            if (mem_read(cpu, in.ext, &reg[in.ra], info) != 0)
                return SIM_FAULT;
            break;
        case 7: store = 1; addr = in.ext; val = cpu->reg[in.ra]; break;
        }
        break;

    case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV:
    case OP_AND: case OP_OR: case OP_XOR:
        if (source(cpu, &in, in.mode, &s, info) != 0)
            return SIM_FAULT;
        if (in.op == OP_DIV && s == 0)
            return fault(info, "divide by zero");
        o = alu((alu_fn)(ALU_ADD + (in.op - OP_ADD)), cpu->reg[in.ra], s);
        reg[in.ra] = o.r;
        set_flags = 1;
        break;

    case OP_SHIFT:
        source(cpu, &in, in.mode & 1u, &s, info); /* register or #EXT: cannot fail */
        o = alu(in.mode < 2 ? ALU_SHL : ALU_SHR, cpu->reg[in.ra], s);
        reg[in.ra] = o.r;
        set_flags = 1;
        break;

    case OP_CMP:
        source(cpu, &in, in.mode, &s, info); /* modes 0 and 1 only: cannot fail */
        o = alu(ALU_SUB, cpu->reg[in.ra], s); /* flags only; Ra unchanged */
        set_flags = 1;
        break;

    case OP_JMP:
        if (condition(in.mode, cpu))
            next_pc = in.ext;
        break;

    case OP_CALL: /* SP <- SP-2; M[SP] <- PC (already the next address); PC <- EXT */
        reg[REG_SP] = (uint16_t)(cpu->reg[REG_SP] - 2);
        store = 1;
        addr = reg[REG_SP];
        val = next_pc;
        next_pc = in.ext;
        break;

    case OP_RET: /* PC <- M[SP]; SP <- SP+2 */
        if (mem_read(cpu, cpu->reg[REG_SP], &next_pc, info) != 0)
            return SIM_FAULT;
        reg[REG_SP] = (uint16_t)(cpu->reg[REG_SP] + 2);
        break;
    }

    /* Checks on the results, still before committing. */
    if (reg[REG_SP] != cpu->reg[REG_SP] && reg[REG_SP] < SIM_STACK_BASE)
        return fault(info, "stack overflow: SP 0x%04X is below 0x%04X", (unsigned)reg[REG_SP],
                     (unsigned)SIM_STACK_BASE);
    if (store && mem_check_write(addr, info) != 0)
        return SIM_FAULT;

    /* STORE: commit registers, flags, memory and PC. */
    if (store) {
        info->nwrites = 1;
        info->write[0].addr = addr;
        info->write[0].before = sim_word(cpu, addr);
        info->write[0].after = val;
        cpu->mem[addr] = (uint8_t)(val & 0xFFu);
        cpu->mem[addr + 1] = (uint8_t)(val >> 8);
    }
    memcpy(cpu->reg, reg, sizeof reg);
    if (set_flags) {
        cpu->z = o.z;
        cpu->n = o.n;
        cpu->c = o.c;
    }
    cpu->ir = info->ir;
    cpu->ir_ext = in.ext;
    cpu->pc = next_pc;
    return halted ? SIM_HALTED : SIM_RUNNING;
}
