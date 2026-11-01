/*
 * view.h -- t13view: execution trace (docs/trace.md) -> memory tables.
 *
 *   read.c    trace reader: parses and checks a .trc file into a vw_trace
 *   render.c  the views: summary, memory waterfall, memory grid, full
 *             block after every step, decoded executable, four-segment
 *             snapshots; plain text or HTML
 *   main.c    the command line
 *
 * The viewer never reads the image or reruns the program: everything it
 * prints comes from the trace.
 */
#ifndef T13_VIEW_H
#define T13_VIEW_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "isa.h"

enum {
    VW_MEM_SIZE = 0x0400,
    VW_TEXT_BASE = 0x0000,
    VW_DATA_BASE = 0x0100,
    VW_HEAP_BASE = 0x0180,
    VW_STACK_BASE = 0x0280,
    VW_MAX_SYMS = 128,
    VW_NAME_MAX = 32,
    VW_MAX_WRITES = 8,      /* MEM lines accepted per step       */
    VW_MSG_MAX = 128
};

typedef struct {
    char name[VW_NAME_MAX + 1];
    uint16_t addr;
} vw_sym;

/* Register file and flags at one point of the run. */
typedef struct {
    uint16_t reg[REG_COUNT];
    uint8_t z, n, c;
} vw_state;

/* One STEP block. */
typedef struct {
    unsigned n;              /* step number, from 1                    */
    uint16_t pc, ir, ext;
    uint8_t has_ext;
    char text[48];           /* disassembly after ';' (may be empty)   */
    vw_state after;          /* REGS / FLAGS after the instruction     */
    unsigned nwrites;
    struct {
        uint16_t addr, before, after;
    } write[VW_MAX_WRITES];
} vw_step;

typedef enum { VW_END_HALT, VW_END_LIMIT, VW_END_ERROR } vw_end;

typedef struct {
    char name[64];              /* program name for captions; set by the caller */
    uint16_t entry;
    vw_sym sym[VW_MAX_SYMS];
    unsigned nsym;
    uint8_t mem0[VW_MEM_SIZE];  /* memory right after loading (LOAD rows) */
    vw_state state0;            /* registers after loading                */
    vw_step *step;              /* step[0] is STEP 1                      */
    unsigned nsteps;
    vw_end end;
    uint16_t end_pc;
    char end_msg[VW_MSG_MAX];
} vw_trace;

/* ---- read.c ---------------------------------------------------------- */

/* Parse the trace in text[0..len). Returns 0, or the 1-based number of
 * the bad line with the message in err. On success free with vw_free(). */
int vw_read(vw_trace *t, const char *text, size_t len, char *err, size_t errcap);
void vw_free(vw_trace *t);

/* Word at an even address of a byte image. */
uint16_t vw_word(const uint8_t *mem, uint16_t addr);

/* The symbol at exactly addr, or NULL. */
const char *vw_sym_at(const vw_trace *t, uint16_t addr);

/* Address of a symbol by name; returns 0 if found. */
int vw_sym_find(const vw_trace *t, const char *name, uint16_t *addr);

/* ---- render.c -------------------------------------------------------- */

typedef enum { VW_TEXT, VW_HTML } vw_format;

void vw_summary(const vw_trace *t, FILE *out, vw_format fmt);
void vw_waterfall(const vw_trace *t, FILE *out, vw_format fmt);
void vw_grid(const vw_trace *t, FILE *out, vw_format fmt);
void vw_blocks(const vw_trace *t, FILE *out, vw_format fmt);
void vw_loaded(const vw_trace *t, FILE *out, vw_format fmt);
void vw_segments(const vw_trace *t, FILE *out, vw_format fmt);

/* "NAME" -> final M[NAME]; "*NAME" -> final M[M[NAME]]. Writes
 * "NAME=<decimal>" to out. Returns 0, or -1 with a message in err. */
int vw_peek(const vw_trace *t, const char *expr, FILE *out, char *err, size_t errcap);

#endif /* T13_VIEW_H */
