/*
 * image.c -- write an assembled unit as a T13 memory image (.img).
 *
 * This is the only code that knows the image format; the format itself is
 * specified in docs/image.md. Example:
 *
 *   T13IMG 1
 *   ENTRY 0000
 *   SYM main 0000
 *   SEG TEXT 0000
 *   0000: 1110 0005      ; MOV R1, #5
 *   0004: 0000           ; HALT
 */
#include <stdio.h>

#include "asm.h"

static const struct {
    const char *name;
    uint16_t base;
} SEG_NAMES[2] = {{"TEXT", 0x0000}, {"DATA", 0x0100}};

/* "1110 0005": the chunk's words, upper-case hex, one space apart. */
static void format_words(const chunk *c, char *buf, size_t cap)
{
    size_t used = 0;
    int k;

    buf[0] = '\0';
    for (k = 0; k < c->nwords && used < cap; k++) {
        int n = snprintf(buf + used, cap - used, k ? " %04X" : "%04X", (unsigned)c->words[k]);
        if (n < 0)
            return;
        used += (size_t)n;
    }
}

void write_image(const asm_unit *u, FILE *out)
{
    char words[ASM_MAX_OPDS * 5 + 1];
    int i, seg;

    fprintf(out, "T13IMG 1\n");
    fprintf(out, "ENTRY %04X\n", (unsigned)u->entry);
    for (i = 0; i < u->syms.count; i++)
        if (u->syms.syms[i].is_label)
            fprintf(out, "SYM %s %04X\n", u->syms.syms[i].name, (unsigned)u->syms.syms[i].value);

    /* Within a segment, source order is address order (.org only moves
     * forward), so chunks are printed in the order they were made. */
    for (seg = SEG_TEXT; seg <= SEG_DATA; seg++) {
        int printed_header = 0;
        for (i = 0; i < u->nchunks; i++) {
            const chunk *c = &u->chunks[i];
            if ((int)c->seg != seg)
                continue;
            if (!printed_header) {
                fprintf(out, "SEG %s %04X\n", SEG_NAMES[seg].name, (unsigned)SEG_NAMES[seg].base);
                printed_header = 1;
            }
            format_words(c, words, sizeof words);
            fprintf(out, "%04X: %-14s ; %s\n", (unsigned)c->addr, words, c->comment);
        }
    }
}
