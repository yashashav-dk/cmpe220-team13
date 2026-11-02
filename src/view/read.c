/*
 * read.c -- the trace reader: .trc text (docs/trace.md) -> vw_trace.
 *
 * Besides parsing, it checks what docs/trace.md rule 11 asks for: the
 * header, step numbers counting up by one, every MEM before-value against
 * the memory rebuilt from LOAD rows and earlier MEM lines, and the END
 * line's STEPS against the number of STEP blocks. Any problem stops the
 * reader with the number of the bad line.
 */
#include "view.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

enum { LINE_MAX_LEN = 511 };

/* Where we are in the grammar. */
typedef enum {
    S_HEADER,     /* expect T13TRC 1                          */
    S_ENTRY,      /* expect ENTRY                             */
    S_SYMS,       /* SYM* LOAD* then the first REGS           */
    S_LOADS,      /* LOAD* then the first REGS                */
    S_STEP_REGS,  /* REGS right after a STEP line             */
    S_FLAGS,      /* FLAGS of the state just read             */
    S_BODY,       /* STEP, MEM or END                         */
    S_DONE        /* after END: nothing more                  */
} rd_state;

static const char *const REG_NAMES[REG_COUNT] = {
    "R0", "R1", "R2", "R3", "R4", "R5", "R6", "R7",
    "R8", "R9", "R10", "R11", "R12", "FP", "SP", "R15",
};

uint16_t vw_word(const uint8_t *mem, uint16_t addr)
{
    return (uint16_t)(mem[addr] | (mem[addr + 1] << 8));
}

const char *vw_sym_at(const vw_trace *t, uint16_t addr)
{
    unsigned i;
    for (i = 0; i < t->nsym; i++)
        if (t->sym[i].addr == addr)
            return t->sym[i].name;
    return NULL;
}

int vw_sym_find(const vw_trace *t, const char *name, uint16_t *addr)
{
    unsigned i;
    for (i = 0; i < t->nsym; i++)
        if (strcmp(t->sym[i].name, name) == 0) {
            *addr = t->sym[i].addr;
            return 0;
        }
    return -1;
}

void vw_free(vw_trace *t)
{
    free(t->step);
    t->step = NULL;
    t->nsteps = 0;
}

static void set_err(char *err, size_t cap, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, cap, fmt, ap);
    va_end(ap);
}

/* Exactly 4 hex digits, either case. */
static int hex4(const char *s, uint16_t *out)
{
    unsigned v = 0;
    int i;

    for (i = 0; i < 4; i++) {
        int c = (unsigned char)s[i];
        if (!isxdigit(c))
            return -1;
        v = v * 16 + (unsigned)(isdigit(c) ? c - '0' : tolower(c) - 'a' + 10);
    }
    if (s[4] != '\0')
        return -1;
    *out = (uint16_t)v;
    return 0;
}

/* "KEY=hhhh" with the expected key. */
static int keyhex(const char *tok, const char *key, uint16_t *out)
{
    size_t k = strlen(key);
    return strncmp(tok, key, k) == 0 && tok[k] == '=' ? hex4(tok + k + 1, out) : -1;
}

/* Decimal without leading zeros, as docs/trace.md DEC. */
static int dec(const char *s, unsigned *out)
{
    unsigned long v = 0;
    const char *p = s;

    if (*p == '\0' || (*p == '0' && p[1] != '\0'))
        return -1;
    for (; *p; p++) {
        if (!isdigit((unsigned char)*p))
            return -1;
        v = v * 10 + (unsigned long)(*p - '0');
        if (v > 100000000ul)
            return -1;
    }
    *out = (unsigned)v;
    return 0;
}

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

/* REGS line into st. Returns 0 or -1. */
static int parse_regs(char **tok, int ntok, vw_state *st)
{
    int r;
    if (ntok != 1 + REG_COUNT || strcmp(tok[0], "REGS") != 0)
        return -1;
    for (r = 0; r < REG_COUNT; r++)
        if (keyhex(tok[1 + r], REG_NAMES[r], &st->reg[r]) != 0)
            return -1;
    return 0;
}

static int bit(const char *tok, const char *key, uint8_t *out)
{
    size_t k = strlen(key);
    if (strncmp(tok, key, k) != 0 || tok[k] != '=' || (tok[k + 1] != '0' && tok[k + 1] != '1') ||
        tok[k + 2] != '\0')
        return -1;
    *out = (uint8_t)(tok[k + 1] - '0');
    return 0;
}

static int parse_flags(char **tok, int ntok, vw_state *st)
{
    if (ntok != 4 || strcmp(tok[0], "FLAGS") != 0)
        return -1;
    return bit(tok[1], "Z", &st->z) | bit(tok[2], "N", &st->n) | bit(tok[3], "C", &st->c);
}

/* Disassembly of a STEP from its IR and EXT words. */
static void step_text(vw_step *s)
{
    uint8_t b[4];
    isa_insn in;
    size_t len;

    b[0] = (uint8_t)(s->ir & 0xFFu);
    b[1] = (uint8_t)(s->ir >> 8);
    b[2] = (uint8_t)(s->ext & 0xFFu);
    b[3] = (uint8_t)(s->ext >> 8);
    if (isa_decode(b, s->has_ext ? 4 : 2, &in, &len) == ISA_OK)
        isa_disasm(&in, s->text, sizeof s->text);
    else
        snprintf(s->text, sizeof s->text, "???");
}

int vw_read(vw_trace *t, const char *text, size_t len, char *err, size_t errcap)
{
    static uint8_t mem[VW_MEM_SIZE]; /* memory rebuilt as we go */
    char line[LINE_MAX_LEN + 1];
    char *tok[24];
    size_t pos = 0, cap = 0;
    int lineno = 0, ntok;
    rd_state state = S_HEADER;
    vw_state *pending = NULL; /* state whose FLAGS line comes next */
    uint16_t v, w;
    unsigned u;

    memset(t, 0, sizeof *t);
    err[0] = '\0';

    while (pos < len) {
        size_t end = pos, n;
        char *semi;

        while (end < len && text[end] != '\n')
            end++;
        n = end - pos;
        lineno++;
        if (n > LINE_MAX_LEN) {
            set_err(err, errcap, "line too long (max %d characters)", LINE_MAX_LEN);
            goto fail;
        }
        memcpy(line, text + pos, n);
        line[n] = '\0';
        pos = end < len ? end + 1 : end;
        if ((semi = strchr(line, ';')) != NULL)
            *semi = '\0';
        ntok = tokenize(line, tok, 24);
        if (ntok > 0) {
            size_t k = strlen(tok[ntok - 1]);
            if (k > 0 && tok[ntok - 1][k - 1] == '\r')
                tok[ntok - 1][k - 1] = '\0';
            if (tok[ntok - 1][0] == '\0')
                ntok--;
        }
        if (ntok == 0)
            continue; /* blank or comment-only */

        if (state == S_DONE) {
            set_err(err, errcap, "line after END");
            goto fail;
        }
        if (state == S_HEADER) {
            if (ntok != 2 || strcmp(tok[0], "T13TRC") != 0 || strcmp(tok[1], "1") != 0) {
                set_err(err, errcap, "expected 'T13TRC 1'");
                goto fail;
            }
            state = S_ENTRY;
            continue;
        }
        if (state == S_ENTRY) {
            if (ntok != 2 || strcmp(tok[0], "ENTRY") != 0 || hex4(tok[1], &t->entry) != 0) {
                set_err(err, errcap, "expected 'ENTRY hhhh'");
                goto fail;
            }
            state = S_SYMS;
            continue;
        }
        if (state == S_STEP_REGS) {
            if (parse_regs(tok, ntok, pending) != 0) {
                set_err(err, errcap, "expected 'REGS' and 16 registers R0 ... R15");
                goto fail;
            }
            state = S_FLAGS;
            continue;
        }
        if (state == S_FLAGS) {
            if (parse_flags(tok, ntok, pending) != 0) {
                set_err(err, errcap, "expected 'FLAGS Z=b N=b C=b'");
                goto fail;
            }
            state = S_BODY;
            continue;
        }

        if (strcmp(tok[0], "SYM") == 0) {
            if (state != S_SYMS) {
                set_err(err, errcap, "SYM line out of place (SYM lines follow ENTRY)");
                goto fail;
            }
            if (ntok != 3 || strlen(tok[1]) > VW_NAME_MAX || hex4(tok[2], &v) != 0) {
                set_err(err, errcap, "expected 'SYM name hhhh'");
                goto fail;
            }
            if (t->nsym >= VW_MAX_SYMS) {
                set_err(err, errcap, "too many symbols (max %d)", VW_MAX_SYMS);
                goto fail;
            }
            strcpy(t->sym[t->nsym].name, tok[1]);
            t->sym[t->nsym++].addr = v;
        } else if (strcmp(tok[0], "LOAD") == 0) {
            int i;
            if (state != S_SYMS && state != S_LOADS) {
                set_err(err, errcap, "LOAD line out of place (LOAD rows come before the first REGS)");
                goto fail;
            }
            if (ntok != 10 || strlen(tok[1]) != 5 || tok[1][4] != ':') {
                set_err(err, errcap, "expected 'LOAD hhhh:' and 8 words");
                goto fail;
            }
            tok[1][4] = '\0';
            if (hex4(tok[1], &v) != 0 || v >= VW_MEM_SIZE) {
                set_err(err, errcap, "expected 'LOAD hhhh:' and 8 words");
                goto fail;
            }
            if (v % 16u != 0) {
                set_err(err, errcap, "LOAD address 0x%04X is not a multiple of 0x0010", (unsigned)v);
                goto fail;
            }
            for (i = 0; i < 8; i++) {
                if (hex4(tok[2 + i], &w) != 0) {
                    set_err(err, errcap, "expected 'LOAD hhhh:' and 8 words");
                    goto fail;
                }
                t->mem0[v + 2 * i] = (uint8_t)(w & 0xFFu);
                t->mem0[v + 2 * i + 1] = (uint8_t)(w >> 8);
            }
            state = S_LOADS;
        } else if (state == S_SYMS || state == S_LOADS) {
            /* the state after loading */
            if (parse_regs(tok, ntok, &t->state0) != 0) {
                set_err(err, errcap, "expected 'REGS' and 16 registers R0 ... R15");
                goto fail;
            }
            memcpy(mem, t->mem0, sizeof mem);
            pending = &t->state0;
            state = S_FLAGS;
        } else if (strcmp(tok[0], "STEP") == 0) {
            vw_step *s;
            if (ntok < 4 || dec(tok[1], &u) != 0) {
                set_err(err, errcap, "expected 'STEP n PC=hhhh IR=hhhh [EXT=hhhh]'");
                goto fail;
            }
            if (u != t->nsteps + 1) {
                set_err(err, errcap, "STEP %u after STEP %u: step numbers must count up by one", u,
                        t->nsteps);
                goto fail;
            }
            if (t->nsteps == cap) {
                vw_step *grown;
                cap = cap ? cap * 2 : 64;
                grown = realloc(t->step, cap * sizeof *grown);
                if (grown == NULL) {
                    set_err(err, errcap, "out of memory");
                    goto fail;
                }
                t->step = grown;
            }
            s = &t->step[t->nsteps];
            memset(s, 0, sizeof *s);
            s->n = u;
            if (keyhex(tok[2], "PC", &s->pc) != 0 || keyhex(tok[3], "IR", &s->ir) != 0 || ntok > 5 ||
                (ntok == 5 && keyhex(tok[4], "EXT", &s->ext) != 0)) {
                set_err(err, errcap, "expected 'STEP n PC=hhhh IR=hhhh [EXT=hhhh]'");
                goto fail;
            }
            s->has_ext = ntok == 5;
            step_text(s);
            t->nsteps++;
            pending = &s->after;
            state = S_STEP_REGS;
        } else if (strcmp(tok[0], "MEM") == 0) {
            vw_step *s;
            uint16_t before, after;
            if (t->nsteps == 0) {
                set_err(err, errcap, "MEM line before any STEP");
                goto fail;
            }
            s = &t->step[t->nsteps - 1];
            if (ntok != 5 || strlen(tok[1]) != 5 || tok[1][4] != ':' || strcmp(tok[3], "->") != 0) {
                set_err(err, errcap, "expected 'MEM hhhh: hhhh -> hhhh'");
                goto fail;
            }
            tok[1][4] = '\0';
            if (hex4(tok[1], &v) != 0 || hex4(tok[2], &before) != 0 || hex4(tok[4], &after) != 0) {
                set_err(err, errcap, "expected 'MEM hhhh: hhhh -> hhhh'");
                goto fail;
            }
            if ((v & 1u) || v >= VW_MEM_SIZE) {
                set_err(err, errcap, "MEM address 0x%04X is odd or outside memory", (unsigned)v);
                goto fail;
            }
            if (vw_word(mem, v) != before) {
                set_err(err, errcap, "MEM %04X: before-value %04X, but memory holds %04X", (unsigned)v,
                        (unsigned)before, (unsigned)vw_word(mem, v));
                goto fail;
            }
            if (s->nwrites >= VW_MAX_WRITES) {
                set_err(err, errcap, "more than %d MEM lines in one step", VW_MAX_WRITES);
                goto fail;
            }
            s->write[s->nwrites].addr = v;
            s->write[s->nwrites].before = before;
            s->write[s->nwrites].after = after;
            s->nwrites++;
            mem[v] = (uint8_t)(after & 0xFFu);
            mem[v + 1] = (uint8_t)(after >> 8);
        } else if (strcmp(tok[0], "END") == 0) {
            int ok = ntok >= 4 && keyhex(tok[2], "PC", &t->end_pc) == 0 &&
                     strncmp(tok[3], "STEPS=", 6) == 0 && dec(tok[3] + 6, &u) == 0;
            if (ok && strcmp(tok[1], "HALT") == 0 && ntok == 4)
                t->end = VW_END_HALT;
            else if (ok && strcmp(tok[1], "LIMIT") == 0 && ntok == 4)
                t->end = VW_END_LIMIT;
            else if (ok && strcmp(tok[1], "ERROR") == 0 && ntok > 4) {
                int i;
                t->end = VW_END_ERROR;
                for (i = 4; i < ntok; i++) {
                    if (i > 4)
                        strncat(t->end_msg, " ", sizeof t->end_msg - strlen(t->end_msg) - 1);
                    strncat(t->end_msg, tok[i], sizeof t->end_msg - strlen(t->end_msg) - 1);
                }
            } else {
                set_err(err, errcap, "expected 'END HALT|LIMIT|ERROR PC=hhhh STEPS=n'");
                goto fail;
            }
            if (u != t->nsteps) {
                set_err(err, errcap, "END says STEPS=%u, but the trace has %u STEP blocks", u, t->nsteps);
                goto fail;
            }
            state = S_DONE;
        } else if (strcmp(tok[0], "REGS") == 0 || strcmp(tok[0], "FLAGS") == 0) {
            set_err(err, errcap, "%s line out of place", tok[0]);
            goto fail;
        } else {
            set_err(err, errcap, "unknown record '%s'", tok[0]);
            goto fail;
        }
    }

    if (state == S_HEADER) {
        set_err(err, errcap, "empty trace");
        lineno = 1;
        goto fail;
    }
    if (state != S_DONE) {
        set_err(err, errcap, "missing END line");
        goto fail;
    }
    return 0;

fail:
    vw_free(t);
    return lineno > 0 ? lineno : 1;
}
