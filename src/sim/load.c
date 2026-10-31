/*
 * load.c -- the loader: memory image (docs/image.md) -> RAM.
 *
 * The image is plain text, one record per line:
 *
 *     T13IMG 1
 *     ENTRY 0000
 *     SYM main 0000            (optional, kept for the trace)
 *     SEG TEXT 0000
 *     0000: 1100 0001 ; ...    (words to consecutive even addresses)
 *     SEG DATA 0100
 *     0100: 0000
 *
 * Every word is stored little-endian. Memory not covered by a record is 0.
 * Loading the same address twice, an odd address, or an address outside
 * the record's segment is an error.
 */
#include "sim.h"

#include <ctype.h>
#include <stdarg.h>
#include <string.h>

enum { LINE_MAX_LEN = 255 };

typedef enum { SEG_NONE, SEG_TEXT, SEG_DATA } seg_kind;

/* Inclusive address range of each loadable segment. */
static const struct {
    const char *name;
    uint16_t lo, hi;
} SEGS[] = {
    [SEG_NONE] = {"", 0, 0},
    [SEG_TEXT] = {"TEXT", SIM_TEXT_BASE, SIM_DATA_BASE - 1},
    [SEG_DATA] = {"DATA", SIM_DATA_BASE, SIM_HEAP_BASE - 1},
};

void sim_reset_registers(sim_cpu *cpu, uint16_t entry)
{
    memset(cpu->reg, 0, sizeof cpu->reg);
    cpu->reg[REG_SP] = SIM_SP_INIT;
    cpu->pc = entry;
    cpu->ir = 0;
    cpu->ir_ext = 0;
    cpu->z = cpu->n = cpu->c = 0;
    cpu->entry = entry;
}

static void set_err(char *err, size_t cap, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, cap, fmt, ap);
    va_end(ap);
}

/* Parse exactly 4 hex digits (either case) at s. Returns 0 on success. */
static int hex4(const char *s, size_t n, uint16_t *out)
{
    unsigned v = 0;
    size_t i;

    if (n != 4)
        return -1;
    for (i = 0; i < 4; i++) {
        int c = (unsigned char)s[i];
        if (!isxdigit(c))
            return -1;
        v = v * 16 + (unsigned)(isdigit(c) ? c - '0' : tolower(c) - 'a' + 10);
    }
    *out = (uint16_t)v;
    return 0;
}

/* Split `line` into whitespace-separated tokens (in place). */
static int tokenize(char *line, char **tok, int max)
{
    int n = 0;
    char *p = line;

    while (*p != '\0' && n < max) {
        while (*p == ' ' || *p == '\t')
            p++;
        if (*p == '\0')
            break;
        tok[n++] = p;
        while (*p != '\0' && *p != ' ' && *p != '\t')
            p++;
        if (*p != '\0')
            *p++ = '\0';
    }
    return n;
}

/* Copy one line out of the image, without the comment and the line end. */
static size_t next_line(const char *text, size_t len, size_t pos, char *line, int *too_long)
{
    size_t end = pos, n;
    const char *semi;

    while (end < len && text[end] != '\n')
        end++;
    n = end - pos;
    *too_long = n > LINE_MAX_LEN;
    if (n > LINE_MAX_LEN)
        n = LINE_MAX_LEN;
    memcpy(line, text + pos, n);
    line[n] = '\0';
    if ((semi = strchr(line, ';')) != NULL)
        line[semi - line] = '\0';
    n = strlen(line);
    while (n > 0 && (line[n - 1] == '\r' || line[n - 1] == ' ' || line[n - 1] == '\t'))
        line[--n] = '\0';
    return end < len ? end + 1 : end;
}

/* One "hhhh: hhhh hhhh ..." record into RAM. Returns 0 or sets err. */
static int load_record(sim_cpu *cpu, uint8_t *used, seg_kind seg, char *line, char *err, size_t cap)
{
    char *tok[64]; /* a 255-character line holds at most 51 tokens */
    int ntok, i;
    size_t alen;
    uint16_t addr, word;

    ntok = tokenize(line, tok, 64);
    alen = strlen(tok[0]);
    if (alen != 5 || tok[0][4] != ':' || hex4(tok[0], 4, &addr) != 0) {
        set_err(err, cap, "expected 'hhhh: hhhh ...'");
        return -1;
    }
    if (seg == SEG_NONE) {
        set_err(err, cap, "record before any SEG line");
        return -1;
    }
    if (ntok < 2) {
        set_err(err, cap, "record has no words");
        return -1;
    }
    for (i = 1; i < ntok; i++, addr = (uint16_t)(addr + 2)) {
        if (hex4(tok[i], strlen(tok[i]), &word) != 0) {
            set_err(err, cap, "bad word '%s'", tok[i]);
            return -1;
        }
        if (addr & 1u) {
            set_err(err, cap, "odd address 0x%04X", (unsigned)addr);
            return -1;
        }
        if (addr < SEGS[seg].lo || addr > SEGS[seg].hi) {
            set_err(err, cap, "address 0x%04X is outside %s (0x%04X-0x%04X)", (unsigned)addr,
                    SEGS[seg].name, (unsigned)SEGS[seg].lo, (unsigned)SEGS[seg].hi);
            return -1;
        }
        if (used[addr / 2]) {
            set_err(err, cap, "address 0x%04X loaded twice", (unsigned)addr);
            return -1;
        }
        used[addr / 2] = 1;
        cpu->mem[addr] = (uint8_t)(word & 0xFFu); /* little-endian */
        cpu->mem[addr + 1] = (uint8_t)(word >> 8);
    }
    return 0;
}

int sim_load(sim_cpu *cpu, const char *text, size_t len, char *err, size_t errcap)
{
    static uint8_t used[SIM_MEM_SIZE / 2]; /* words already loaded */
    char line[LINE_MAX_LEN + 1];
    char *tok[4];
    size_t pos = 0;
    int lineno = 0, ntok, too_long, state = 0; /* 0: header, 1: ENTRY, 2: body */
    seg_kind seg = SEG_NONE;
    uint16_t v;

    memset(cpu, 0, sizeof *cpu);
    memset(used, 0, sizeof used);
    sim_reset_registers(cpu, 0);
    err[0] = '\0';

    while (pos < len) {
        pos = next_line(text, len, pos, line, &too_long);
        lineno++;
        if (too_long) {
            set_err(err, errcap, "line too long (max %d characters)", LINE_MAX_LEN);
            return lineno;
        }
        if (line[0] == '\0') /* blank or comment-only line */
            continue;

        if (state == 0) {
            ntok = tokenize(line, tok, 4);
            if (ntok != 2 || strcmp(tok[0], "T13IMG") != 0 || strcmp(tok[1], "1") != 0) {
                set_err(err, errcap, "expected 'T13IMG 1'");
                return lineno;
            }
            state = 1;
        } else if (state == 1) {
            ntok = tokenize(line, tok, 4);
            if (ntok != 2 || strcmp(tok[0], "ENTRY") != 0 || hex4(tok[1], strlen(tok[1]), &v) != 0) {
                set_err(err, errcap, "expected 'ENTRY hhhh'");
                return lineno;
            }
            if ((v & 1u) || v > SEGS[SEG_TEXT].hi) {
                set_err(err, errcap, "ENTRY 0x%04X must be even and inside Text (0x0000-0x00FF)",
                        (unsigned)v);
                return lineno;
            }
            sim_reset_registers(cpu, v);
            state = 2;
        } else if (strncmp(line, "SYM", 3) == 0 && (line[3] == ' ' || line[3] == '\t')) {
            ntok = tokenize(line, tok, 4);
            if (ntok != 3 || strlen(tok[1]) > SIM_NAME_MAX || hex4(tok[2], strlen(tok[2]), &v) != 0) {
                set_err(err, errcap, "expected 'SYM name hhhh'");
                return lineno;
            }
            if (cpu->nsym >= SIM_MAX_SYMS) {
                set_err(err, errcap, "too many symbols (max %d)", SIM_MAX_SYMS);
                return lineno;
            }
            strcpy(cpu->sym[cpu->nsym].name, tok[1]);
            cpu->sym[cpu->nsym].addr = v;
            cpu->nsym++;
        } else if (strncmp(line, "SEG", 3) == 0 && (line[3] == ' ' || line[3] == '\t')) {
            ntok = tokenize(line, tok, 4);
            if (ntok != 3 || hex4(tok[2], strlen(tok[2]), &v) != 0) {
                set_err(err, errcap, "expected 'SEG TEXT|DATA hhhh'");
                return lineno;
            }
            if (strcmp(tok[1], "TEXT") == 0)
                seg = SEG_TEXT;
            else if (strcmp(tok[1], "DATA") == 0)
                seg = SEG_DATA;
            else {
                set_err(err, errcap, "unknown segment '%s'", tok[1]);
                return lineno;
            }
        } else if (load_record(cpu, used, seg, line, err, errcap) != 0) {
            return lineno;
        }
    }

    if (state == 0) {
        set_err(err, errcap, "empty image");
        return 1;
    }
    if (state == 1) {
        set_err(err, errcap, "expected 'ENTRY hhhh'");
        return lineno + 1;
    }
    return 0;
}
