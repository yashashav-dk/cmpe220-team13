/*
 * listing.c -- write the assembler listing (.lst): every source line next
 * to its address and the words it produced, then the symbol table.
 *
 *   ADDR  WORDS           SOURCE
 *   0000  1100 0001       main:   MOV R0, #1
 *   0100  0001 0002 0003  w: .word 1,2,3,4
 *   0106  0004                                <- continuation row
 *
 * At most 3 words per row, so the source column always starts at the same
 * place. Rows are right-trimmed. Format: docs/asm.md section 7.
 */
#include <stdio.h>
#include <string.h>

#include "asm.h"

#define WORDS_PER_ROW 3

/* Print one row: address (or blanks), up to 3 words, source text. */
static void row(FILE *out, int addr, const uint16_t *w, int nw, const char *src)
{
    char line[ASM_MAX_LINE + 64];
    char words[WORDS_PER_ROW * 5 + 1];
    size_t used = 0, len;
    int k;

    words[0] = '\0';
    for (k = 0; k < nw; k++) {
        int n = snprintf(words + used, sizeof words - used, k ? " %04X" : "%04X", (unsigned)w[k]);
        if (n < 0 || (size_t)n >= sizeof words - used)
            break; /* cannot happen for 3 words of 4 hex digits; never step past the buffer */
        used += (size_t)n;
    }

    if (addr >= 0)
        snprintf(line, sizeof line, "%04X  %-14s  %s", (unsigned)addr, words, src);
    else
        snprintf(line, sizeof line, "%-4s  %-14s  %s", "", words, src);

    len = strlen(line);
    while (len > 0 && line[len - 1] == ' ')
        line[--len] = '\0';
    fprintf(out, "%s\n", line);
}

void write_listing(const asm_unit *u, FILE *out)
{
    uint16_t w[ASM_MAX_OPDS * 2];
    int i, c, k;

    fprintf(out, "%-4s  %-14s  %s\n", "ADDR", "WORDS", "SOURCE");

    for (i = 0; i < u->nlines; i++) {
        int nw = 0, addr = u->line_addr[i];

        /* All words this line produced, in address order. */
        for (c = 0; c < u->nchunks; c++)
            if (u->chunks[c].line == i + 1)
                for (k = 0; k < u->chunks[c].nwords && nw < (int)(sizeof w / sizeof w[0]); k++)
                    w[nw++] = u->chunks[c].words[k];

        row(out, addr, w, nw < WORDS_PER_ROW ? nw : WORDS_PER_ROW, u->src[i]);
        for (k = WORDS_PER_ROW; k < nw; k += WORDS_PER_ROW) /* continuation rows */
            row(out, addr + 2 * k, w + k, nw - k < WORDS_PER_ROW ? nw - k : WORDS_PER_ROW, "");
    }

    fprintf(out, "\nSYMBOLS\n");
    for (i = 0; i < u->syms.count; i++)
        fprintf(out, "%-12s %04X\n", u->syms.syms[i].name, (unsigned)u->syms.syms[i].value);
}
