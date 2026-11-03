/*
 * parse.c -- turn one line of T13 assembly into a stmt.
 *
 *   [label:]  [mnemonic  operand, operand, ...]  [; comment]
 *
 * Only syntax is checked here: numbers, registers, brackets, names. Whether
 * an instruction accepts those operands is select.c's job, and symbols are
 * resolved by assemble.c. The grammar is in docs/asm.md section 3.
 */
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "asm.h"

/* Write a message into err and return 0, so callers can `return fail(...)`. */
static int fail(char *err, size_t errcap, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, errcap, fmt, ap);
    va_end(ap);
    return 0;
}

/* Strip leading and trailing blanks in place; returns the new start. */
static char *trim(char *s)
{
    char *end;

    while (isspace((unsigned char)*s))
        s++;
    end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1]))
        *--end = '\0';
    return s;
}

/* Case-insensitive string equality (strcasecmp is not C99). */
static int same_ci(const char *a, const char *b)
{
    while (*a && *b) {
        if (toupper((unsigned char)*a) != toupper((unsigned char)*b))
            return 0;
        a++;
        b++;
    }
    return *a == *b;
}

/* [A-Za-z_][A-Za-z0-9_]*, shorter than ASM_NAME_LEN. */
static int is_ident(const char *s)
{
    size_t i;

    if (!isalpha((unsigned char)s[0]) && s[0] != '_')
        return 0;
    for (i = 1; s[i]; i++)
        if (!isalnum((unsigned char)s[i]) && s[i] != '_')
            return 0;
    return i < ASM_NAME_LEN;
}

/* "R" followed by digits only, e.g. R16: meant as a register but invalid. */
static int looks_like_register(const char *s)
{
    size_t i;

    if (toupper((unsigned char)s[0]) != 'R' || s[1] == '\0')
        return 0;
    for (i = 1; s[i]; i++)
        if (!isdigit((unsigned char)s[i]))
            return 0;
    return 1;
}

/* 1 ok, 0 malformed, -1 well formed but outside -32768 .. 65535. */
static int number_status(const char *text, uint16_t *out)
{
    const char *p = text;
    unsigned long v = 0;
    int neg = 0, base = 10, digits = 0;

    if (*p == '-') {
        neg = 1;
        p++;
    }
    if (!neg && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        base = 16;
        p += 2;
    }
    for (; *p; p++) {
        int d;
        if (isdigit((unsigned char)*p))
            d = *p - '0';
        else if (base == 16 && isxdigit((unsigned char)*p))
            d = toupper((unsigned char)*p) - 'A' + 10;
        else
            return 0;
        if (v <= 0xFFFFFul) /* stop growing once clearly out of range */
            v = v * (unsigned long)base + (unsigned long)d;
        digits++;
    }
    if (digits == 0)
        return 0;
    if (neg ? v > 32768ul : v > 65535ul)
        return -1;
    *out = (uint16_t)(neg ? (0x10000ul - v) & 0xFFFFul : v);
    return 1;
}

int parse_number(const char *text, uint16_t *out)
{
    return number_status(text, out) == 1;
}

int parse_is_register(const char *name, uint8_t *reg)
{
    const char *d = name + 1;

    if (same_ci(name, "FP")) {
        *reg = REG_FP;
        return 1;
    }
    if (same_ci(name, "SP")) {
        *reg = REG_SP;
        return 1;
    }
    if (toupper((unsigned char)name[0]) != 'R' || !isdigit((unsigned char)d[0]))
        return 0;
    if (d[1] == '\0') { /* R0 .. R9 */
        *reg = (uint8_t)(d[0] - '0');
        return 1;
    }
    if (d[0] == '1' && d[1] >= '0' && d[1] <= '5' && d[2] == '\0') { /* R10 .. R15 */
        *reg = (uint8_t)(10 + d[1] - '0');
        return 1;
    }
    return 0;
}

/* A number or a symbol name. `whole` is the full operand, for messages. */
static int parse_value(const char *text, const char *whole, operand *o, char *err, size_t errcap)
{
    uint8_t r;
    int st;

    if (text[0] == '\0')
        return fail(err, errcap, "bad number ''");
    if (isdigit((unsigned char)text[0]) || text[0] == '-') {
        st = number_status(text, &o->value);
        if (st == 1)
            return 1;
        if (st < 0)
            return fail(err, errcap, "constant out of range '%s'", text);
        return fail(err, errcap, "bad number '%s'", text);
    }
    if (is_ident(text) && !parse_is_register(text, &r)) {
        o->is_sym = 1;
        strcpy(o->sym, text); /* fits: is_ident checked the length */
        return 1;
    }
    return fail(err, errcap, "bad operand '%s'", whole);
}

/* One trimmed, non-empty operand. */
static int parse_operand(char *text, operand *o, char *err, size_t errcap)
{
    char inner[ASM_MAX_LINE];
    size_t n = strlen(text), i, j;
    char *plus;

    memset(o, 0, sizeof *o);

    if (text[0] == '#') { /* #k */
        o->kind = OPD_IMM;
        return parse_value(trim(text + 1), text, o, err, errcap);
    }

    if (text[0] == '[') { /* [Rb], [Rb+k], [addr] */
        if (text[n - 1] != ']')
            return fail(err, errcap, "expected ']'");
        for (i = 1, j = 0; i + 1 < n; i++) /* copy the inside without blanks */
            if (!isspace((unsigned char)text[i]))
                inner[j++] = text[i];
        inner[j] = '\0';

        plus = strchr(inner, '+');
        if (plus != NULL) {
            *plus = '\0';
            if (!parse_is_register(inner, &o->reg))
                return fail(err, errcap, "bad register '%s'", inner);
            o->kind = OPD_MEM_REG_OFF;
            return parse_value(plus + 1, text, o, err, errcap);
        }
        if (parse_is_register(inner, &o->reg)) {
            o->kind = OPD_MEM_REG;
            return 1;
        }
        if (looks_like_register(inner))
            return fail(err, errcap, "bad register '%s'", inner);
        o->kind = OPD_MEM_ABS;
        return parse_value(inner, text, o, err, errcap);
    }

    if (strpbrk(text, " \t") != NULL)
        return fail(err, errcap, "bad operand '%s'", text);
    if (parse_is_register(text, &o->reg)) {
        o->kind = OPD_REG;
        return 1;
    }
    if (looks_like_register(text))
        return fail(err, errcap, "bad register '%s'", text);
    o->kind = OPD_VALUE; /* jump target, .word item, .equ/.org value */
    return parse_value(text, text, o, err, errcap);
}

/* Split "a, [b+c], d" at top-level commas and parse each operand. */
static int parse_operands(char *rest, stmt *out, char *err, size_t errcap)
{
    char *start = rest, *c;
    int depth = 0;

    for (c = rest;; c++) {
        if (*c == '[')
            depth++;
        else if (*c == ']')
            depth--;
        if ((*c == ',' && depth <= 0) || *c == '\0') {
            char was = *c;
            char *t;

            *c = '\0';
            t = trim(start);
            if (*t == '\0')
                return fail(err, errcap, "empty operand");
            if (out->nopd == ASM_MAX_OPDS)
                return fail(err, errcap, "too many operands (max %d)", ASM_MAX_OPDS);
            if (!parse_operand(t, &out->opd[out->nopd], err, errcap))
                return 0;
            out->nopd++;
            if (was == '\0')
                return 1;
            start = c + 1;
        }
    }
}

int parse_line(const char *text, int line, stmt *out, char *err, size_t errcap)
{
    char buf[ASM_MAX_LINE];
    char *p, *q, *semi;
    size_t i, len;

    memset(out, 0, sizeof *out);
    out->line = line;
    strncpy(buf, text, sizeof buf - 1);
    buf[sizeof buf - 1] = '\0';
    semi = strchr(buf, ';');
    if (semi != NULL)
        *semi = '\0';

    p = buf;
    while (isspace((unsigned char)*p))
        p++;

    /* Label: the first token, if it ends in ':'. */
    for (q = p; *q && !isspace((unsigned char)*q) && *q != ':'; q++)
        ;
    if (*q == ':') {
        *q = '\0';
        if (!is_ident(p))
            return fail(err, errcap, "bad label '%s'", p);
        strcpy(out->label, p);
        p = q + 1;
        while (isspace((unsigned char)*p))
            p++;
        for (q = p; *q && !isspace((unsigned char)*q) && *q != ':'; q++)
            ;
        if (*q == ':')
            return fail(err, errcap, "only one label per line");
    }
    if (*p == '\0')
        return 1;

    /* Mnemonic or .directive, upper-cased. */
    for (q = p; *q && !isspace((unsigned char)*q); q++)
        ;
    len = (size_t)(q - p);
    if (len >= ASM_NAME_LEN)
        return fail(err, errcap, "bad mnemonic '%.*s'", (int)len, p);
    for (i = 0; i < len; i++) {
        unsigned char ch = (unsigned char)p[i];
        if (!(isalnum(ch) || (i == 0 && ch == '.')))
            return fail(err, errcap, "bad mnemonic '%.*s'", (int)len, p);
        out->mnem[i] = (char)toupper(ch);
    }
    out->mnem[len] = '\0';

    p = trim(q);
    if (*p == '\0')
        return 1;
    return parse_operands(p, out, err, errcap);
}
