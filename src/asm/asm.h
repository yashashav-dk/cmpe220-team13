/*
 * asm.h -- types, limits and module APIs of t13as, the T13 assembler.
 *
 * Pipeline (one module per step):
 *
 *   source text --parse.c--> stmt per line --select.c--> isa_insn(s)
 *        --assemble.c (pass 1: addresses, symbols; pass 2: resolve, encode)-->
 *   asm_unit (chunks of words) --image.c--> .img   --listing.c--> .lst
 *
 * All bit layouts live in the shared codec (include/isa.h); this code only
 * picks (op, mode, ra, rb, ext). Fixed-size arrays, no malloc: the machine
 * has 1 KiB of memory, so programs are tiny.
 */
#ifndef T13_ASM_H
#define T13_ASM_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "isa.h"

#define ASM_MAX_LINE 256   /* bytes per source line, including the NUL */
#define ASM_MAX_LINES 1024 /* source lines per file                    */
#define ASM_MAX_SYMS 128   /* labels + .equ names                      */
#define ASM_NAME_LEN 32    /* bytes per name, including the NUL        */
#define ASM_MAX_OPDS 8     /* operands per line (.word takes up to 8)  */
#define ASM_MAX_CHUNKS 512 /* 1 KiB / 2 bytes per word                 */
#define ASM_MAX_ERRORS 32  /* error messages kept (more are counted)   */

/* Segments; bounds are in docs/isa.md section 6. */
typedef enum { SEG_TEXT = 0, SEG_DATA = 1 } asm_seg;

typedef enum {
    OPD_REG,         /* R1                                              */
    OPD_IMM,         /* #5, #N                                          */
    OPD_MEM_REG,     /* [R2]                                            */
    OPD_MEM_REG_OFF, /* [FP+4], [FP+ARG]                                */
    OPD_MEM_ABS,     /* [0x0100], [result]                              */
    OPD_VALUE        /* bare number or symbol: jump target, .word item  */
} opd_kind;

typedef struct {
    opd_kind kind;
    uint8_t reg;            /* REG, MEM_REG, MEM_REG_OFF                   */
    int is_sym;             /* 1: the value comes from sym, resolved later */
    uint16_t value;         /* number, already reduced to 16 bits          */
    char sym[ASM_NAME_LEN]; /* symbol name when is_sym                     */
} operand;

/* One parsed source line. */
typedef struct {
    int line;                 /* 1-based line number                          */
    char label[ASM_NAME_LEN]; /* "" if none                                   */
    char mnem[ASM_NAME_LEN];  /* upper-cased mnemonic or ".DIRECTIVE"; "" if none */
    operand opd[ASM_MAX_OPDS];
    int nopd;
    uint16_t addr; /* filled by pass 1 */
    asm_seg seg;   /* filled by pass 1 */
} stmt;

typedef struct {
    char name[ASM_NAME_LEN];
    uint16_t value;
    int line;     /* where it was defined     */
    int is_label; /* 1 label, 0 .equ constant */
} symbol;

typedef struct {
    symbol syms[ASM_MAX_SYMS];
    int count;
} symtab;

/* Words emitted by one machine instruction or one .word line. */
typedef struct {
    uint16_t addr;
    asm_seg seg;
    uint16_t words[ASM_MAX_OPDS];
    int nwords;
    int line;
    char comment[40]; /* disassembly, the data label, or ".word" */
} chunk;

/* Everything known about one assembled source file. */
typedef struct {
    char name[128];                      /* source path as given, for messages */
    char src[ASM_MAX_LINES][ASM_MAX_LINE]; /* lines as written (for the listing) */
    int line_addr[ASM_MAX_LINES];        /* address at that line, -1 if none   */
    int nlines;
    chunk chunks[ASM_MAX_CHUNKS];
    int nchunks;
    symtab syms;
    uint16_t entry; /* address of the first text chunk, else 0 */
    char errors[ASM_MAX_ERRORS][ASM_MAX_LINE]; /* "name:line: error: msg" */
    int nerrors; /* may exceed ASM_MAX_ERRORS (counted, not stored) */
} asm_unit;

/* parse.c */

/* Number in decimal (optionally negative) or 0x hex, the whole string.
 * 1 and *out set (negatives as two's complement); 0 if malformed or outside
 * -32768 .. 65535. */
int parse_number(const char *text, uint16_t *out);

/* Parse one source line (no newline). 1 on success; 0 with a message in err. */
int parse_line(const char *text, int line, stmt *out, char *err, size_t errcap);

/* R0-R15, FP, SP (any case). 1 and *reg set, or 0. */
int parse_is_register(const char *name, uint8_t *reg);

/* symtab.c */
void symtab_init(symtab *t);
/* 1 defined; 0 already defined; -1 table full. */
int symtab_define(symtab *t, const char *name, uint16_t value, int line, int is_label);
/* NULL if undefined. Names are case-sensitive. */
const symbol *symtab_find(const symtab *t, const char *name);

/* select.c */

/* Instructions for statement s (count 1 or 2), or -1 with a message in err. */
int select_insns(const stmt *s, isa_insn out[2], char *err, size_t errcap);
/* 1 if upper_name is a real or pseudo mnemonic. */
int select_is_mnemonic(const char *upper_name);

/* assemble.c */

/* Assemble source text; returns u->nerrors. */
int assemble_text(const char *name, const char *text, asm_unit *u);
/* Read and assemble a file; -1 if it cannot be read, else u->nerrors. */
int assemble_file(const char *path, asm_unit *u);

/* image.c, listing.c */
void write_image(const asm_unit *u, FILE *out);
void write_listing(const asm_unit *u, FILE *out);

#endif /* T13_ASM_H */
