/*
 * assemble.c -- the two passes of t13as.
 *
 * Split:  the text is cut into lines (LF or CRLF; a last line without a
 *         newline counts) and kept as written, for the listing.
 * Pass 1: parse every line; give each label the address its segment's
 *         location counter has reached; run the directives; size every
 *         instruction with select_insns() (2 or 4 bytes each). Symbols may
 *         still be undefined here.
 * Pass 2: only if pass 1 found no errors. Resolve symbols, check absolute
 *         addresses, select and encode each instruction with the shared
 *         codec, and record the words as chunks.
 *
 * Every error is "name:line: error: message"; all of them are collected
 * (the first ASM_MAX_ERRORS are kept) so the user sees them in one run.
 * Directives and segment bounds: docs/asm.md sections 4 and 6.
 */
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "asm.h"

#define MEM_SIZE 0x0400u

/* Segment bounds: [base, end). docs/isa.md section 6. */
static const struct {
    const char *name;
    uint16_t base, end;
} SEGS[2] = {
    {"text", 0x0000, 0x0100},
    {"data", 0x0100, 0x0180},
};

/* Parsed lines, shared by both passes; ok[i] is 0 if line i had a parse error. */
static stmt stmts[ASM_MAX_LINES];
static int ok[ASM_MAX_LINES];

static void error(asm_unit *u, int line, const char *fmt, ...)
{
    if (u->nerrors < ASM_MAX_ERRORS) {
        /* Format into a local buffer: u->name and u->errors live in the
         * same struct, and gcc rejects snprintf from one into the other. */
        char msg[ASM_MAX_LINE];
        int n = snprintf(msg, sizeof msg, "%s:%d: error: ", u->name, line);
        va_list ap;
        if (n > 0 && n < (int)sizeof msg) {
            va_start(ap, fmt);
            vsnprintf(msg + n, sizeof msg - (size_t)n, fmt, ap);
            va_end(ap);
        }
        memcpy(u->errors[u->nerrors], msg, sizeof msg);
    }
    u->nerrors++;
}

/* ---- split -------------------------------------------------------------- */

static void split_lines(asm_unit *u, const char *text)
{
    const char *p = text;

    while (*p != '\0') {
        const char *end = strchr(p, '\n');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        size_t keep = len;

        if (keep > 0 && p[keep - 1] == '\r')
            keep--;
        if (u->nlines == ASM_MAX_LINES) {
            error(u, u->nlines + 1, "too many lines (max %d)", ASM_MAX_LINES);
            return;
        }
        if (keep > ASM_MAX_LINE - 1) {
            error(u, u->nlines + 1, "line too long (max %d characters)", ASM_MAX_LINE - 1);
            keep = ASM_MAX_LINE - 1;
            ok[u->nlines] = -1; /* not parsed */
        }
        memcpy(u->src[u->nlines], p, keep);
        u->src[u->nlines][keep] = '\0';
        u->nlines++;
        if (end == NULL)
            break;
        p = end + 1;
    }
}

/* ---- pass 1 -------------------------------------------------------------- */

/* Value of a .org/.space/.equ operand: a number or an already-defined symbol. */
static int const_value(asm_unit *u, const stmt *s, const operand *o, uint16_t *v)
{
    if (o->is_sym) {
        const symbol *sym = symtab_find(&u->syms, o->sym);
        if (sym == NULL) {
            error(u, s->line, "undefined symbol '%s'", o->sym);
            return 0;
        }
        *v = sym->value;
        return 1;
    }
    *v = o->value;
    return 1;
}

/* Define a label or .equ name, rejecting register and mnemonic names. */
static void define(asm_unit *u, const char *name, uint16_t value, int line, int is_label)
{
    char upper[ASM_NAME_LEN];
    const symbol *old;
    uint8_t reg;
    size_t i;

    for (i = 0; name[i] && i < ASM_NAME_LEN - 1; i++)
        upper[i] = (char)toupper((unsigned char)name[i]);
    upper[i] = '\0';

    if (parse_is_register(name, &reg)) {
        error(u, line, "'%s' is a register name", name);
        return;
    }
    if (select_is_mnemonic(upper)) {
        error(u, line, "'%s' is a mnemonic", name);
        return;
    }
    old = symtab_find(&u->syms, name);
    if (old != NULL) {
        error(u, line, "duplicate symbol '%s' (first defined at line %d)", name, old->line);
        return;
    }
    if (symtab_define(&u->syms, name, value, line, is_label) < 0)
        error(u, line, "too many symbols (max %d)", ASM_MAX_SYMS);
}

/* Reserve n bytes at the current counter of seg; 0 (and one error per
 * segment) if they do not fit. */
static int reserve(asm_unit *u, uint16_t loc[2], asm_seg seg, unsigned n, int line, int overflowed[2])
{
    if ((unsigned)loc[seg] + n > SEGS[seg].end) {
        if (!overflowed[seg])
            error(u, line, "segment overflow: %s is 0x%04X-0x%04X", SEGS[seg].name, SEGS[seg].base,
                  SEGS[seg].end - 1u);
        overflowed[seg] = 1;
        return 0;
    }
    loc[seg] = (uint16_t)(loc[seg] + n);
    return 1;
}

/* One directive in pass 1. */
static void directive(asm_unit *u, stmt *s, int i, asm_seg *cur, uint16_t loc[2], int overflowed[2])
{
    const char *d = s->mnem;
    uint16_t v;
    int k;

    if (strcmp(d, ".TEXT") == 0 || strcmp(d, ".DATA") == 0) {
        if (s->nopd != 0)
            error(u, s->line, "%s takes no operands", d[1] == 'T' ? ".text" : ".data");
        else
            *cur = d[1] == 'T' ? SEG_TEXT : SEG_DATA;
        return;
    }
    if (strcmp(d, ".EQU") == 0) {
        if (s->nopd != 2 || s->opd[0].kind != OPD_VALUE || !s->opd[0].is_sym || s->opd[1].kind != OPD_VALUE) {
            error(u, s->line, ".equ needs NAME, value");
            return;
        }
        if (const_value(u, s, &s->opd[1], &v))
            define(u, s->opd[0].sym, v, s->line, 0);
        return;
    }
    if (strcmp(d, ".ORG") == 0) {
        if (s->nopd != 1 || s->opd[0].kind != OPD_VALUE) {
            error(u, s->line, ".org needs one address");
            return;
        }
        if (!const_value(u, s, &s->opd[0], &v))
            return;
        if (v & 1u)
            error(u, s->line, "odd address 0x%04X", v);
        else if (v < SEGS[*cur].base || v >= SEGS[*cur].end)
            error(u, s->line, ".org 0x%04X is outside the %s segment (0x%04X-0x%04X)", v, SEGS[*cur].name,
                  SEGS[*cur].base, SEGS[*cur].end - 1u);
        else if (v < loc[*cur])
            error(u, s->line, ".org cannot move backwards");
        else
            loc[*cur] = v;
        return;
    }
    if (strcmp(d, ".WORD") == 0) {
        if (s->nopd == 0) {
            error(u, s->line, ".word needs at least one value");
            return;
        }
        for (k = 0; k < s->nopd; k++)
            if (s->opd[k].kind != OPD_VALUE) {
                error(u, s->line, "bad operand for .word");
                return;
            }
        u->line_addr[i] = loc[*cur];
        reserve(u, loc, *cur, 2u * (unsigned)s->nopd, s->line, overflowed);
        return;
    }
    if (strcmp(d, ".SPACE") == 0) {
        if (s->nopd != 1 || s->opd[0].kind != OPD_VALUE) {
            error(u, s->line, ".space needs an even size > 0");
            return;
        }
        if (!const_value(u, s, &s->opd[0], &v))
            return;
        if (v == 0 || (v & 1u)) {
            error(u, s->line, ".space needs an even size > 0");
            return;
        }
        u->line_addr[i] = loc[*cur];
        reserve(u, loc, *cur, v, s->line, overflowed);
        return;
    }
    error(u, s->line, "unknown directive '%s'", d);
}

static void pass1(asm_unit *u)
{
    uint16_t loc[2] = {0x0000, 0x0100};
    int overflowed[2] = {0, 0};
    asm_seg cur = SEG_TEXT;
    char err[ASM_MAX_LINE];
    int i;

    for (i = 0; i < u->nlines; i++) {
        stmt *s = &stmts[i];

        u->line_addr[i] = -1;
        if (ok[i] < 0) { /* line too long: already reported */
            ok[i] = 0;
            continue;
        }
        ok[i] = parse_line(u->src[i], i + 1, s, err, sizeof err);
        if (!ok[i]) {
            error(u, i + 1, "%s", err);
            continue;
        }
        s->seg = cur;
        s->addr = loc[cur];
        if (s->label[0] != '\0') {
            define(u, s->label, loc[cur], s->line, 1);
            u->line_addr[i] = loc[cur];
        }
        if (s->mnem[0] == '\0')
            continue;

        if (s->mnem[0] == '.') {
            directive(u, s, i, &cur, loc, overflowed);
        } else {
            isa_insn in[2];
            int n = select_insns(s, in, err, sizeof err), k;
            unsigned size = 0;

            if (n < 0) {
                error(u, s->line, "%s", err);
                continue;
            }
            for (k = 0; k < n; k++)
                size += isa_needs_ext(in[k].op, in[k].mode) ? 4u : 2u;
            u->line_addr[i] = loc[cur];
            reserve(u, loc, cur, size, s->line, overflowed);
        }
    }
}

/* ---- pass 2 -------------------------------------------------------------- */

static int check_address(asm_unit *u, int line, uint16_t a)
{
    if (a & 1u) {
        error(u, line, "odd address 0x%04X", a);
        return 0;
    }
    if (a >= MEM_SIZE) {
        error(u, line, "address 0x%04X is outside memory (0x0000-0x%04X)", a, MEM_SIZE - 1u);
        return 0;
    }
    return 1;
}

/* Replace symbol operands with their values; 0 if any is undefined. */
static int resolve(asm_unit *u, stmt *s)
{
    int k, good = 1;

    for (k = 0; k < s->nopd; k++) {
        operand *o = &s->opd[k];
        const symbol *sym;
        if (!o->is_sym)
            continue;
        sym = symtab_find(&u->syms, o->sym);
        if (sym == NULL) {
            error(u, s->line, "undefined symbol '%s'", o->sym);
            good = 0;
        } else {
            o->value = sym->value;
        }
    }
    return good;
}

static chunk *new_chunk(asm_unit *u, const stmt *s, uint16_t addr)
{
    chunk *c;

    if (u->nchunks == ASM_MAX_CHUNKS) { /* cannot happen: 1 KiB holds 512 words */
        error(u, s->line, "internal: too many chunks");
        return NULL;
    }
    c = &u->chunks[u->nchunks++];
    memset(c, 0, sizeof *c);
    c->addr = addr;
    c->seg = s->seg;
    c->line = s->line;
    return c;
}

static void emit_word_line(asm_unit *u, const stmt *s)
{
    chunk *c = new_chunk(u, s, s->addr);
    int k;

    if (c == NULL)
        return;
    for (k = 0; k < s->nopd; k++)
        c->words[k] = s->opd[k].value;
    c->nwords = s->nopd;
    snprintf(c->comment, sizeof c->comment, "%s", s->label[0] ? s->label : ".word");
}

static void emit_instruction(asm_unit *u, const stmt *s)
{
    isa_insn in[2];
    char err[ASM_MAX_LINE];
    uint16_t addr = s->addr;
    int n, k, bad = 0;

    /* Absolute addresses: [addr] operands, jump and call targets. */
    for (k = 0; k < s->nopd; k++)
        if (s->opd[k].kind == OPD_MEM_ABS || s->opd[k].kind == OPD_VALUE)
            bad |= !check_address(u, s->line, s->opd[k].value);
    if (bad)
        return;

    n = select_insns(s, in, err, sizeof err);
    if (n < 0) {
        error(u, s->line, "%s", err);
        return;
    }
    for (k = 0; k < n; k++) {
        uint8_t bytes[4];
        size_t len = 0;
        isa_status st = isa_encode(&in[k], bytes, sizeof bytes, &len);
        chunk *c;

        if (st != ISA_OK) {
            error(u, s->line, "internal: %s", isa_strerror(st));
            return;
        }
        c = new_chunk(u, s, addr);
        if (c == NULL)
            return;
        c->words[0] = (uint16_t)(bytes[0] | (bytes[1] << 8));
        c->nwords = 1;
        if (len == 4) {
            c->words[1] = (uint16_t)(bytes[2] | (bytes[3] << 8));
            c->nwords = 2;
        }
        isa_disasm(&in[k], c->comment, sizeof c->comment);
        addr = (uint16_t)(addr + len);
    }
}

static void pass2(asm_unit *u)
{
    int i, have_entry = 0;

    for (i = 0; i < u->nlines; i++) {
        stmt *s = &stmts[i];

        if (!ok[i] || s->mnem[0] == '\0')
            continue;
        if (strcmp(s->mnem, ".WORD") == 0) {
            if (resolve(u, s))
                emit_word_line(u, s);
        } else if (s->mnem[0] != '.') {
            if (resolve(u, s))
                emit_instruction(u, s);
        }
    }
    for (i = 0; i < u->nchunks && !have_entry; i++)
        if (u->chunks[i].seg == SEG_TEXT) {
            u->entry = u->chunks[i].addr;
            have_entry = 1;
        }
}

/* ---- entry points ---------------------------------------------------------- */

static void start_unit(const char *name, asm_unit *u)
{
    memset(u, 0, sizeof *u);
    memset(ok, 0, sizeof ok);
    snprintf(u->name, sizeof u->name, "%s", name);
    symtab_init(&u->syms);
}

int assemble_text(const char *name, const char *text, asm_unit *u)
{
    start_unit(name, u);

    /* Windows editors may start a UTF-8 file with a byte-order mark. */
    if ((unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF)
        text += 3;

    split_lines(u, text);
    pass1(u);
    if (u->nerrors == 0)
        pass2(u);
    return u->nerrors;
}

int assemble_file(const char *path, asm_unit *u)
{
    /* Room for ASM_MAX_LINES full lines with CRLF endings. */
    static char text[ASM_MAX_LINES * (ASM_MAX_LINE + 1) + 1];
    FILE *f = fopen(path, "rb");
    size_t n, i;
    int more, line = 1;

    if (f == NULL)
        return -1;
    n = fread(text, 1, sizeof text - 1, f);
    if (ferror(f)) { /* e.g. the path is a directory */
        fclose(f);
        return -1;
    }
    more = (fgetc(f) != EOF);
    fclose(f);
    text[n] = '\0';

    /* Refuse input that would otherwise be cut short without a word. */
    for (i = 0; i < n; i++) {
        if (text[i] == '\0') {
            start_unit(path, u);
            error(u, line, "NUL byte in source (is this a binary file?)");
            return u->nerrors;
        }
        if (text[i] == '\n')
            line++;
    }
    if (more) {
        start_unit(path, u);
        error(u, line, "source file too large (max %lu bytes)", (unsigned long)(sizeof text - 1));
        return u->nerrors;
    }
    return assemble_text(path, text, u);
}
