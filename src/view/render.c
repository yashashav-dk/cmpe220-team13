/*
 * render.c -- the views t13view prints from a trace.
 *
 *   summary    end state: END line, registers, flags, words that changed
 *   waterfall  one row per executed instruction: the registers and memory
 *              words that change during the run, changed cells marked
 *   grid       the program's memory block after loading and at the end
 *   blocks     the same block again after every instruction
 *   loaded     the executable as loaded: Text decoded, Data with labels
 *   segments   Text / Data / Heap / Stack at load, at every CALL and RET,
 *              and at HALT; the stack 4 words per row, one frame per row
 *
 * Text output marks a changed cell with '*' after it. HTML output is a
 * <figure> per view (class "chg" on changed cells) for the report.
 */
#include "view.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

static const char *const REG_NAMES[REG_COUNT] = {
    "R0", "R1", "R2", "R3", "R4", "R5", "R6", "R7",
    "R8", "R9", "R10", "R11", "R12", "FP", "SP", "R15",
};

/* ---- helpers ------------------------------------------------------------ */

/* Text output: lines are built in a buffer and written without trailing
 * spaces. */
typedef struct {
    char buf[1024];
    size_t len;
} line_buf;

static void lb_printf(line_buf *lb, const char *fmt, ...)
{
    va_list ap;
    int n;

    if (lb->len >= sizeof lb->buf - 1)
        return;
    va_start(ap, fmt);
    n = vsnprintf(lb->buf + lb->len, sizeof lb->buf - lb->len, fmt, ap);
    va_end(ap);
    if (n > 0)
        lb->len += (size_t)n < sizeof lb->buf - lb->len ? (size_t)n : sizeof lb->buf - lb->len - 1;
}

static void lb_emit(line_buf *lb, FILE *out)
{
    while (lb->len > 0 && lb->buf[lb->len - 1] == ' ')
        lb->len--;
    lb->buf[lb->len] = '\0';
    fprintf(out, "%s\n", lb->buf);
    lb->len = 0;
}

static void html_esc(FILE *out, const char *s)
{
    for (; *s; s++) {
        switch (*s) {
        case '<': fputs("&lt;", out); break;
        case '>': fputs("&gt;", out); break;
        case '&': fputs("&amp;", out); break;
        case '"': fputs("&quot;", out); break;
        default: fputc(*s, out);
        }
    }
}

/* "<name>: " caption prefix, or nothing. */
static void caption_start(const vw_trace *t, FILE *out)
{
    fputs("<figcaption>", out);
    if (t->name[0] != '\0') {
        fputs("<code>", out);
        html_esc(out, t->name);
        fputs("</code>: ", out);
    }
}

static const vw_state *state_at(const vw_trace *t, unsigned k)
{
    return k == 0 ? &t->state0 : &t->step[k - 1].after;
}

/* Apply the MEM lines of step k (1-based) to mem. */
static void apply_step(const vw_trace *t, unsigned k, uint8_t *mem)
{
    const vw_step *s = &t->step[k - 1];
    unsigned i;
    for (i = 0; i < s->nwrites; i++) {
        mem[s->write[i].addr] = (uint8_t)(s->write[i].after & 0xFFu);
        mem[s->write[i].addr + 1] = (uint8_t)(s->write[i].after >> 8);
    }
}

/* Memory after the last step. */
static void final_mem(const vw_trace *t, uint8_t *mem)
{
    unsigned k;
    memcpy(mem, t->mem0, VW_MEM_SIZE);
    for (k = 1; k <= t->nsteps; k++)
        apply_step(t, k, mem);
}

/* written[a/2] = 1 if any step writes the word at a. */
static void written_words(const vw_trace *t, uint8_t *written)
{
    unsigned k, i;
    memset(written, 0, VW_MEM_SIZE / 2);
    for (k = 0; k < t->nsteps; k++)
        for (i = 0; i < t->step[k].nwrites; i++)
            written[t->step[k].write[i].addr / 2] = 1;
}

static const char *seg_name(uint16_t addr)
{
    if (addr < VW_DATA_BASE)
        return "TEXT";
    if (addr < VW_HEAP_BASE)
        return "DATA";
    if (addr < VW_STACK_BASE)
        return "HEAP";
    return "STACK";
}

/* Length of the instruction whose first word is w (2 if illegal). */
static unsigned insn_len(uint16_t w)
{
    unsigned n = isa_length(w);
    return n ? n : 2;
}

/* First address past the code: the end of the last executed instruction or
 * of the last non-zero Text word, whichever is later. */
static uint16_t code_end(const vw_trace *t)
{
    unsigned end = 0, k, a;

    for (k = 0; k < t->nsteps; k++) {
        unsigned e = t->step[k].pc + insn_len(t->step[k].ir);
        if (t->step[k].pc < VW_DATA_BASE && e > end)
            end = e;
    }
    for (a = 0; a < VW_DATA_BASE; a += 2)
        if (vw_word(t->mem0, (uint16_t)a) != 0 && a + 2 > end)
            end = a + 2;
    return (uint16_t)(end > VW_DATA_BASE ? VW_DATA_BASE : end);
}

/* Nearest symbol at or below addr inside Text, as "name" or "name+hh". */
static void code_ref(const vw_trace *t, uint16_t addr, char *buf, size_t cap)
{
    const vw_sym *best = NULL;
    unsigned i;

    buf[0] = '\0';
    if (addr >= VW_DATA_BASE)
        return;
    for (i = 0; i < t->nsym; i++)
        if (t->sym[i].addr <= addr && t->sym[i].addr < VW_DATA_BASE &&
            (best == NULL || t->sym[i].addr > best->addr))
            best = &t->sym[i];
    if (best == NULL)
        return;
    if (best->addr == addr)
        snprintf(buf, cap, "%s", best->name);
    else
        snprintf(buf, cap, "%s+%X", best->name, (unsigned)(addr - best->addr));
}

static const char *end_name(vw_end e)
{
    return e == VW_END_HALT ? "HALT" : e == VW_END_LIMIT ? "LIMIT" : "ERROR";
}

/* ---- summary ------------------------------------------------------------ */

void vw_summary(const vw_trace *t, FILE *out, vw_format fmt)
{
    static uint8_t mem[VW_MEM_SIZE];
    const vw_state *st = state_at(t, t->nsteps);
    char line[256];
    unsigned a, r;
    size_t n = 0;

    final_mem(t, mem);
    if (fmt == VW_HTML)
        fputs("<pre class=\"t13\">", out);

#define OUT_LINE()                                                                 \
    do {                                                                           \
        if (fmt == VW_HTML)                                                        \
            html_esc(out, line), fputc('\n', out);                                 \
        else                                                                       \
            fprintf(out, "%s\n", line);                                            \
    } while (0)

    if (t->end == VW_END_ERROR)
        snprintf(line, sizeof line, "END ERROR after %u steps, PC=%04X: %s", t->nsteps,
                 (unsigned)t->end_pc, t->end_msg);
    else
        snprintf(line, sizeof line, "END %s after %u steps, PC=%04X", end_name(t->end), t->nsteps,
                 (unsigned)t->end_pc);
    OUT_LINE();

    n = (size_t)snprintf(line, sizeof line, "REGS");
    for (r = 0; r < REG_COUNT; r++)
        n += (size_t)snprintf(line + n, sizeof line - n, " %s=%04X", REG_NAMES[r], (unsigned)st->reg[r]);
    OUT_LINE();
    snprintf(line, sizeof line, "FLAGS Z=%u N=%u C=%u", (unsigned)st->z, (unsigned)st->n, (unsigned)st->c);
    OUT_LINE();

    snprintf(line, sizeof line, "Memory words changed (after loading -> at the end):");
    OUT_LINE();
    n = 0;
    for (a = 0; a < VW_MEM_SIZE; a += 2) {
        uint16_t w0 = vw_word(t->mem0, (uint16_t)a), w1 = vw_word(mem, (uint16_t)a);
        const char *lbl = vw_sym_at(t, (uint16_t)a);
        if (w0 == w1)
            continue;
        snprintf(line, sizeof line, "  %04X %-10s %04X -> %04X  (%u)", a, lbl ? lbl : seg_name((uint16_t)a),
                 (unsigned)w0, (unsigned)w1, (unsigned)w1);
        OUT_LINE();
        n++;
    }
    if (n == 0) {
        snprintf(line, sizeof line, "  none");
        OUT_LINE();
    }
#undef OUT_LINE
    if (fmt == VW_HTML)
        fputs("</pre>\n", out);
}

/* ---- waterfall ---------------------------------------------------------- */

void vw_waterfall(const vw_trace *t, FILE *out, vw_format fmt)
{
    static uint8_t mem[VW_MEM_SIZE], prev[VW_MEM_SIZE], written[VW_MEM_SIZE / 2];
    uint16_t cols[VW_MEM_SIZE / 2];
    int reg_used[REG_COUNT] = {0};
    unsigned ncols = 0, k, r, a, i;
    line_buf lb = {{0}, 0};

    /* Columns: registers that ever change, and words that are ever written. */
    for (k = 1; k <= t->nsteps; k++)
        for (r = 0; r < REG_COUNT; r++)
            if (state_at(t, k)->reg[r] != state_at(t, k - 1)->reg[r])
                reg_used[r] = 1;
    written_words(t, written);
    for (a = 0; a < VW_MEM_SIZE; a += 2)
        if (written[a / 2])
            cols[ncols++] = (uint16_t)a;

    if (fmt == VW_HTML) {
        fputs("<figure class=\"t13fig\">\n<table class=\"t13 waterfall\">\n<thead><tr>"
              "<th>Step</th><th>PC</th><th>Instruction</th>",
              out);
        for (r = 0; r < REG_COUNT; r++)
            if (reg_used[r])
                fprintf(out, "<th>%s</th>", REG_NAMES[r]);
        fputs("<th>Z</th><th>N</th><th>C</th>", out);
        for (i = 0; i < ncols; i++) {
            const char *lbl = vw_sym_at(t, cols[i]);
            fprintf(out, "<th class=\"mem\">%04X", (unsigned)cols[i]);
            if (lbl) {
                fputs("<br>", out);
                html_esc(out, lbl);
            }
            fputs("</th>", out);
        }
        fputs("</tr></thead>\n<tbody>\n", out);
    } else {
        lb_printf(&lb, "%4s  %-4s  %-20s  ", "STEP", "PC", "INSTRUCTION");
        for (r = 0; r < REG_COUNT; r++)
            if (reg_used[r])
                lb_printf(&lb, "%-7s", REG_NAMES[r]);
        lb_printf(&lb, "Z N C  ");
        for (i = 0; i < ncols; i++)
            lb_printf(&lb, "M[%04X] ", (unsigned)cols[i]);
        lb_emit(&lb, out);
    }

    memcpy(mem, t->mem0, sizeof mem);
    memcpy(prev, mem, sizeof prev);
    for (k = 0; k <= t->nsteps; k++) {
        const vw_state *st = state_at(t, k), *ps = state_at(t, k ? k - 1 : 0);
        if (k > 0)
            apply_step(t, k, mem);

        if (fmt == VW_HTML) {
            fprintf(out, "<tr><td>%u</td>", k);
            if (k == 0)
                fputs("<td>&ndash;</td><td class=\"ins\">(loaded)</td>", out);
            else {
                fprintf(out, "<td>%04X</td><td class=\"ins\">", (unsigned)t->step[k - 1].pc);
                html_esc(out, t->step[k - 1].text);
                fputs("</td>", out);
            }
            for (r = 0; r < REG_COUNT; r++)
                if (reg_used[r])
                    fprintf(out, "<td%s>%04X</td>", k && st->reg[r] != ps->reg[r] ? " class=\"chg\"" : "",
                            (unsigned)st->reg[r]);
            fprintf(out, "<td%s>%u</td><td%s>%u</td><td%s>%u</td>",
                    k && st->z != ps->z ? " class=\"chg\"" : "", (unsigned)st->z,
                    k && st->n != ps->n ? " class=\"chg\"" : "", (unsigned)st->n,
                    k && st->c != ps->c ? " class=\"chg\"" : "", (unsigned)st->c);
            for (i = 0; i < ncols; i++) {
                uint16_t w = vw_word(mem, cols[i]);
                fprintf(out, "<td%s>%04X</td>", w != vw_word(prev, cols[i]) ? " class=\"chg\"" : "",
                        (unsigned)w);
            }
            fputs("</tr>\n", out);
        } else {
            if (k == 0)
                lb_printf(&lb, "%4u  %-4s  %-20s  ", k, "----", "(loaded)");
            else
                lb_printf(&lb, "%4u  %04X  %-20s  ", k, (unsigned)t->step[k - 1].pc, t->step[k - 1].text);
            for (r = 0; r < REG_COUNT; r++)
                if (reg_used[r])
                    lb_printf(&lb, "%04X%c  ", (unsigned)st->reg[r], k && st->reg[r] != ps->reg[r] ? '*' : ' ');
            lb_printf(&lb, "%u %u %u  ", (unsigned)st->z, (unsigned)st->n, (unsigned)st->c);
            for (i = 0; i < ncols; i++) {
                uint16_t w = vw_word(mem, cols[i]);
                lb_printf(&lb, "%04X%c   ", (unsigned)w, w != vw_word(prev, cols[i]) ? '*' : ' ');
            }
            lb_emit(&lb, out);
        }
        memcpy(prev, mem, sizeof prev);
    }

    if (fmt == VW_HTML) {
        fputs("</tbody>\n</table>\n", out);
        caption_start(t, out);
        fputs("memory waterfall, one row per executed instruction (row 0: after loading). "
              "Columns: registers and memory words that change; shaded = changed by that row's "
              "instruction.</figcaption>\n</figure>\n",
              out);
    }
}

/* ---- memory grid and blocks --------------------------------------------- */

/* Rows (16 bytes) of the program's memory block: non-zero after loading or
 * at the end, written during the run, or holding a symbol. */
static unsigned block_rows(const vw_trace *t, const uint8_t *final, uint16_t *rows)
{
    static uint8_t written[VW_MEM_SIZE / 2];
    unsigned n = 0, row, i;

    written_words(t, written);
    for (row = 0; row < VW_MEM_SIZE; row += 16) {
        int keep = 0;
        for (i = 0; i < 16; i++)
            keep |= t->mem0[row + i] | final[row + i];
        for (i = 0; i < 16; i += 2)
            keep |= written[(row + i) / 2] | (vw_sym_at(t, (uint16_t)(row + i)) != NULL);
        if (keep)
            rows[n++] = (uint16_t)row;
    }
    return n;
}

/* One grid of `rows`, marking words that differ from `ref` (NULL: none). */
static void grid_rows(const vw_trace *t, FILE *out, vw_format fmt, const uint8_t *mem, const uint8_t *ref,
                      const uint16_t *rows, unsigned nrows)
{
    line_buf lb = {{0}, 0};
    unsigned i, w;

    if (fmt == VW_HTML) {
        fputs("<table class=\"t13 grid\">\n<thead><tr><th>Seg</th><th>Addr</th>", out);
        for (w = 0; w < 16; w += 2)
            fprintf(out, "<th>+%X</th>", w);
        fputs("</tr></thead>\n<tbody>\n", out);
        for (i = 0; i < nrows; i++) {
            fprintf(out, "<tr class=\"%s\"><td>%s</td><td>%04X</td>", seg_name(rows[i]), seg_name(rows[i]),
                    (unsigned)rows[i]);
            for (w = 0; w < 16; w += 2) {
                uint16_t a = (uint16_t)(rows[i] + w), v = vw_word(mem, a);
                const char *lbl = vw_sym_at(t, a);
                int chg = ref != NULL && v != vw_word(ref, a);
                fprintf(out, "<td%s>%04X", chg ? " class=\"chg\"" : "", (unsigned)v);
                if (lbl) {
                    fputs("<br><small>", out);
                    html_esc(out, lbl);
                    fputs("</small>", out);
                }
                fputs("</td>", out);
            }
            fputs("</tr>\n", out);
        }
        fputs("</tbody>\n</table>\n", out);
        return;
    }

    lb_printf(&lb, "%-5s  %-4s  ", "SEG", "ADDR");
    for (w = 0; w < 16; w += 2)
        lb_printf(&lb, "+%-4X", w);
    lb_emit(&lb, out);
    for (i = 0; i < nrows; i++) {
        lb_printf(&lb, "%-5s  %04X  ", seg_name(rows[i]), (unsigned)rows[i]);
        for (w = 0; w < 16; w += 2) {
            uint16_t a = (uint16_t)(rows[i] + w), v = vw_word(mem, a);
            lb_printf(&lb, "%04X%c", (unsigned)v, ref != NULL && v != vw_word(ref, a) ? '*' : ' ');
        }
        lb_emit(&lb, out);
    }
}

void vw_grid(const vw_trace *t, FILE *out, vw_format fmt)
{
    static uint8_t final[VW_MEM_SIZE];
    uint16_t rows[VW_MEM_SIZE / 16];
    unsigned nrows;

    final_mem(t, final);
    nrows = block_rows(t, final, rows);

    if (fmt == VW_HTML) {
        fputs("<figure class=\"t13fig\">\n", out);
        grid_rows(t, out, fmt, t->mem0, NULL, rows, nrows);
        caption_start(t, out);
        fputs("memory after loading (step 0); rows with code, data or stack use.</figcaption>\n</figure>\n", out);
        fputs("<figure class=\"t13fig\">\n", out);
        grid_rows(t, out, fmt, final, t->mem0, rows, nrows);
        caption_start(t, out);
        fprintf(out, "memory at the end (step %u, END %s); shaded = changed since loading.</figcaption>\n</figure>\n",
                t->nsteps, end_name(t->end));
        return;
    }
    fprintf(out, "Memory after loading (step 0)\n");
    grid_rows(t, out, fmt, t->mem0, NULL, rows, nrows);
    fprintf(out, "\nMemory at the end (step %u, END %s)\n", t->nsteps, end_name(t->end));
    grid_rows(t, out, fmt, final, t->mem0, rows, nrows);
}

void vw_blocks(const vw_trace *t, FILE *out, vw_format fmt)
{
    static uint8_t final[VW_MEM_SIZE], mem[VW_MEM_SIZE], prev[VW_MEM_SIZE];
    uint16_t rows[VW_MEM_SIZE / 16];
    unsigned nrows, k;

    final_mem(t, final);
    nrows = block_rows(t, final, rows);
    memcpy(mem, t->mem0, sizeof mem);

    if (fmt == VW_HTML)
        fputs("<figure class=\"t13fig\">\n<div class=\"blocks\">\n", out);
    for (k = 1; k <= t->nsteps; k++) {
        memcpy(prev, mem, sizeof prev);
        apply_step(t, k, mem);
        if (fmt == VW_HTML) {
            fprintf(out, "<div class=\"block\"><div class=\"bhead\">Step %u: <code>", k);
            html_esc(out, t->step[k - 1].text);
            fputs("</code></div>\n", out);
            grid_rows(t, out, fmt, mem, prev, rows, nrows);
            fputs("</div>\n", out);
        } else {
            fprintf(out, "%sAfter step %u: %s\n", k > 1 ? "\n" : "", k, t->step[k - 1].text);
            grid_rows(t, out, fmt, mem, prev, rows, nrows);
        }
    }
    if (fmt == VW_HTML) {
        fputs("</div>\n", out);
        caption_start(t, out);
        fputs("the memory block after every executed instruction; shaded = written by that "
              "instruction.</figcaption>\n</figure>\n",
              out);
    }
}

/* ---- loaded executable -------------------------------------------------- */

/* Last Data address worth showing (+2), or VW_DATA_BASE if none. */
static uint16_t data_end(const vw_trace *t, uint16_t lo, uint16_t hi)
{
    static uint8_t written[VW_MEM_SIZE / 2];
    unsigned a, end = lo;

    written_words(t, written);
    for (a = lo; a < hi; a += 2)
        if (vw_word(t->mem0, (uint16_t)a) != 0 || written[a / 2] || vw_sym_at(t, (uint16_t)a))
            end = a + 2;
    return (uint16_t)end;
}

static int label_width(const vw_trace *t, uint16_t lo, uint16_t hi)
{
    unsigned i;
    int w = 6;
    for (i = 0; i < t->nsym; i++)
        if (t->sym[i].addr >= lo && t->sym[i].addr < hi && (int)strlen(t->sym[i].name) > w)
            w = (int)strlen(t->sym[i].name);
    return w;
}

void vw_loaded(const vw_trace *t, FILE *out, vw_format fmt)
{
    uint16_t end = code_end(t), dend = data_end(t, VW_DATA_BASE, VW_HEAP_BASE);
    int lw = label_width(t, 0, VW_HEAP_BASE);
    line_buf lb = {{0}, 0};
    unsigned a;

    if (fmt == VW_HTML)
        fputs("<figure class=\"t13fig\">\n<table class=\"t13 loaded\">\n<thead><tr><th>Seg</th><th>Addr</th>"
              "<th>Words</th><th>Label</th><th>Decoded</th></tr></thead>\n<tbody>\n",
              out);
    else
        fprintf(out, "Executable loaded into memory (ENTRY %04X)\n\nTEXT   %04X-%04X  code, decoded\n"
                     "ADDR  WORDS      %-*s  INSTRUCTION\n",
                (unsigned)t->entry, 0u, (unsigned)(end ? end - 1 : 0), lw, "LABEL");

    for (a = 0; a < end;) {
        uint8_t b[4] = {0, 0, 0, 0};
        isa_insn in;
        size_t len = 2;
        char text[64], words[16], ref[48] = "";
        const char *lbl = vw_sym_at(t, (uint16_t)a);
        uint16_t w = vw_word(t->mem0, (uint16_t)a);

        memcpy(b, t->mem0 + a, a + 4 <= VW_MEM_SIZE ? 4 : 2);
        if (isa_decode(b, 4, &in, &len) == ISA_OK) {
            isa_disasm(&in, text, sizeof text);
            if (in.has_ext && (in.op == OP_JMP || in.op == OP_CALL || (in.op == OP_MOV && in.mode >= 6))) {
                const char *s = vw_sym_at(t, in.ext);
                if (s)
                    snprintf(ref, sizeof ref, "%s", s);
            }
        } else {
            snprintf(text, sizeof text, ".word 0x%04X", (unsigned)w);
            len = 2;
        }
        if (len == 4)
            snprintf(words, sizeof words, "%04X %04X", (unsigned)w, (unsigned)vw_word(t->mem0, (uint16_t)(a + 2)));
        else
            snprintf(words, sizeof words, "%04X", (unsigned)w);

        if (fmt == VW_HTML) {
            fprintf(out, "<tr class=\"TEXT\"><td>%s</td><td>%04X</td><td>%s</td><td>", a == 0 ? "TEXT" : "", a, words);
            if (lbl)
                html_esc(out, lbl);
            fputs("</td><td class=\"ins\">", out);
            html_esc(out, text);
            if (ref[0]) {
                fputs(" <span class=\"ref\">&rarr; ", out);
                html_esc(out, ref);
                fputs("</span>", out);
            }
            fputs("</td></tr>\n", out);
        } else {
            lb_printf(&lb, "%04X  %-9s  %-*s  %s", a, words, lw, lbl ? lbl : "", text);
            if (ref[0])
                lb_printf(&lb, "   -> %s", ref);
            lb_emit(&lb, out);
        }
        a += (unsigned)len;
    }

    if (fmt != VW_HTML)
        fprintf(out, "\nDATA   0100-017F  globals\nADDR  WORD  %-*s  VALUE\n", lw, "LABEL");
    for (a = VW_DATA_BASE; a < dend; a += 2) {
        const char *lbl = vw_sym_at(t, (uint16_t)a);
        uint16_t w = vw_word(t->mem0, (uint16_t)a);
        if (fmt == VW_HTML) {
            fprintf(out, "<tr class=\"DATA\"><td>%s</td><td>%04X</td><td>%04X</td><td>", a == VW_DATA_BASE ? "DATA" : "",
                    a, (unsigned)w);
            if (lbl)
                html_esc(out, lbl);
            fprintf(out, "</td><td class=\"ins\">.word %u</td></tr>\n", (unsigned)w);
        } else {
            lb_printf(&lb, "%04X  %04X  %-*s  %u", a, (unsigned)w, lw, lbl ? lbl : "", (unsigned)w);
            lb_emit(&lb, out);
        }
    }
    if (fmt == VW_HTML) {
        fprintf(out, "<tr class=\"HEAP\"><td>HEAP</td><td>0180&ndash;027F</td><td colspan=\"3\">all 0 at load"
                     "</td></tr>\n<tr class=\"STACK\"><td>STACK</td><td>0280&ndash;03FF</td><td colspan=\"3\">"
                     "all 0 at load; SP = %04X</td></tr>\n</tbody>\n</table>\n",
                (unsigned)t->state0.reg[REG_SP]);
        caption_start(t, out);
        fprintf(out, "executable as loaded into memory, decoded (ENTRY %04X, PC = ENTRY, SP = %04X).</figcaption>\n"
                     "</figure>\n",
                (unsigned)t->entry, (unsigned)t->state0.reg[REG_SP]);
    } else {
        fprintf(out, "\nHEAP   0180-027F  all 0 at load\nSTACK  0280-03FF  all 0 at load, SP=%04X\n",
                (unsigned)t->state0.reg[REG_SP]);
    }
}

/* ---- four-segment snapshots --------------------------------------------- */

static int is_snapshot(const vw_step *s)
{
    return s->ir == 0xC000 || s->ir == 0xD000 || s->ir == 0x0000;
}

/* "CALL fact", "RET", "HALT" */
static void event_name(const vw_trace *t, const vw_step *s, char *buf, size_t cap)
{
    if (s->ir == 0xC000) {
        const char *lbl = vw_sym_at(t, s->ext);
        if (lbl)
            snprintf(buf, cap, "CALL %s", lbl);
        else
            snprintf(buf, cap, "CALL 0x%04X", (unsigned)s->ext);
    } else {
        snprintf(buf, cap, "%s", s->text);
    }
}

typedef struct {
    uint16_t text_end;
    uint16_t data[VW_MEM_SIZE / 2];
    unsigned ndata;
    uint16_t heap_lo, heap_hi;   /* words heap_lo .. heap_hi-2 shown */
    uint16_t stack_lo;           /* lowest stack row shown (8-aligned) */
    int stack_used;
} seg_layout;

static void layout(const vw_trace *t, seg_layout *L)
{
    static uint8_t written[VW_MEM_SIZE / 2];
    unsigned a, k;
    uint16_t lowest = t->state0.reg[REG_SP];

    written_words(t, written);
    memset(L, 0, sizeof *L);
    L->text_end = code_end(t);
    for (a = VW_DATA_BASE; a < VW_HEAP_BASE; a += 2)
        if (vw_word(t->mem0, (uint16_t)a) != 0 || written[a / 2] || vw_sym_at(t, (uint16_t)a))
            L->data[L->ndata++] = (uint16_t)a;
    L->heap_lo = VW_HEAP_BASE;
    L->heap_hi = data_end(t, VW_HEAP_BASE, VW_STACK_BASE);
    for (k = 1; k <= t->nsteps; k++)
        if (t->step[k - 1].after.reg[REG_SP] < lowest)
            lowest = t->step[k - 1].after.reg[REG_SP];
    for (a = VW_STACK_BASE; a < VW_MEM_SIZE; a += 2)
        if (written[a / 2] && a < lowest)
            lowest = (uint16_t)a;
    if (lowest < VW_MEM_SIZE) {
        L->stack_used = 1;
        L->stack_lo = (uint16_t)(lowest < VW_STACK_BASE ? VW_STACK_BASE : lowest & ~7u);
    }
}

static void snapshot(const vw_trace *t, FILE *out, vw_format fmt, const seg_layout *L, unsigned idx, unsigned k,
                     const uint8_t *mem, const uint8_t *prev)
{
    const vw_state *st = state_at(t, k);
    char ev[64] = "", ref[48];
    line_buf lb = {{0}, 0};
    unsigned i, r;
    uint16_t row, sp = st->reg[REG_SP], fp = st->reg[REG_FP];
    static const char *const SLOT[4] = {"+0 OLDFP", "+2 RA", "+4 ARG", "+6 RETV"};

    if (k > 0)
        event_name(t, &t->step[k - 1], ev, sizeof ev);

    if (fmt == VW_HTML) {
        fputs("<figure class=\"t13fig snap\">\n<div class=\"segs\">\n", out);
        /* registers */
        fputs("<table class=\"t13 regs\"><thead><tr><th colspan=\"2\">Registers</th></tr></thead><tbody>\n", out);
        fprintf(out, "<tr><td>PC</td><td>%04X</td></tr>\n", (unsigned)(k == 0 ? t->entry : (k < t->nsteps ? t->step[k].pc : t->end_pc)));
        for (r = 0; r < 4; r++)
            fprintf(out, "<tr><td>%s</td><td%s>%04X</td></tr>\n", REG_NAMES[r],
                    k && st->reg[r] != state_at(t, k - 1)->reg[r] ? " class=\"chg\"" : "", (unsigned)st->reg[r]);
        fprintf(out, "<tr><td>FP</td><td>%04X</td></tr>\n<tr><td>SP</td><td>%04X</td></tr>\n</tbody></table>\n",
                (unsigned)fp, (unsigned)sp);
        /* Text, Data, Heap */
        fputs("<table class=\"t13 segtab\"><thead><tr><th>Seg</th><th>Addr</th><th>Label</th><th>Value</th></tr>"
              "</thead><tbody>\n",
              out);
        fprintf(out, "<tr class=\"TEXT\"><td>Text</td><td>0000&ndash;%04X</td><td colspan=\"2\">%u words of code"
                     "</td></tr>\n",
                (unsigned)(L->text_end ? L->text_end - 1 : 0), (unsigned)L->text_end / 2);
        if (L->ndata == 0)
            fputs("<tr class=\"DATA\"><td>Data</td><td>0100&ndash;017F</td><td colspan=\"2\">empty</td></tr>\n", out);
        for (i = 0; i < L->ndata; i++) {
            const char *lbl = vw_sym_at(t, L->data[i]);
            uint16_t w = vw_word(mem, L->data[i]);
            fprintf(out, "<tr class=\"DATA\"><td>%s</td><td>%04X</td><td>", i == 0 ? "Data" : "", (unsigned)L->data[i]);
            if (lbl)
                html_esc(out, lbl);
            fprintf(out, "</td><td%s>%04X</td></tr>\n", w != vw_word(prev, L->data[i]) ? " class=\"chg\"" : "",
                    (unsigned)w);
        }
        if (L->heap_hi <= L->heap_lo)
            fputs("<tr class=\"HEAP\"><td>Heap</td><td>0180&ndash;027F</td><td colspan=\"2\">empty</td></tr>\n", out);
        for (row = L->heap_lo; row < L->heap_hi; row = (uint16_t)(row + 2)) {
            uint16_t w = vw_word(mem, row);
            fprintf(out, "<tr class=\"HEAP\"><td>%s</td><td>%04X</td><td></td><td%s>%04X</td></tr>\n",
                    row == L->heap_lo ? "Heap" : "", (unsigned)row,
                    w != vw_word(prev, row) ? " class=\"chg\"" : "", (unsigned)w);
        }
        fputs("</tbody></table>\n", out);
        /* Stack */
        fputs("<table class=\"t13 stack\"><thead><tr><th>Stack</th>", out);
        for (i = 0; i < 4; i++)
            fprintf(out, "<th>%s</th>", SLOT[i]);
        fputs("<th></th></tr></thead><tbody>\n", out);
        if (!L->stack_used)
            fputs("<tr><td colspan=\"6\">empty (SP = 0400)</td></tr>\n", out);
        else
            for (row = 0x03F8; row >= L->stack_lo && row >= VW_STACK_BASE; row = (uint16_t)(row - 8)) {
                int free_row = row + 8 <= sp;
                fprintf(out, "<tr%s><td>%04X</td>", free_row ? " class=\"free\"" : "", (unsigned)row);
                for (i = 0; i < 4; i++) {
                    uint16_t a = (uint16_t)(row + 2 * i), w = vw_word(mem, a);
                    int chg = w != vw_word(prev, a);
                    const char *cls = chg ? (a == sp ? "chg sp" : "chg") : (a == sp ? "sp" : "");
                    fprintf(out, "<td%s%s%s>%04X", cls[0] ? " class=\"" : "", cls, cls[0] ? "\"" : "", (unsigned)w);
                    if (i == 1 && w != 0) {
                        code_ref(t, w, ref, sizeof ref);
                        if (ref[0]) {
                            fputs("<br><small>", out);
                            html_esc(out, ref);
                            fputs("</small>", out);
                        }
                    }
                    fputs("</td>", out);
                }
                fputs("<td class=\"note\">", out);
                if (fp == row)
                    fputs("&larr; FP ", out);
                if (sp >= row && sp < row + 8)
                    fprintf(out, "&larr; SP=%04X", (unsigned)sp);
                if (free_row)
                    fputs("free (below SP)", out);
                fputs("</td></tr>\n", out);
            }
        fputs("</tbody></table>\n</div>\n", out);
        caption_start(t, out);
        if (k == 0)
            fprintf(out, "snapshot %u, after loading: Text / Data / Heap / Stack.</figcaption>\n</figure>\n", idx);
        else {
            fprintf(out, "snapshot %u, after step %u <code>", idx, k);
            html_esc(out, ev);
            fputs("</code>: Text / Data / Heap / Stack; shaded = changed since the previous snapshot.</figcaption>\n"
                  "</figure>\n",
                  out);
        }
        return;
    }

    if (k == 0)
        fprintf(out, "Snapshot %u: after loading\n", idx);
    else
        fprintf(out, "Snapshot %u: after step %u, %s\n", idx, k, ev);
    lb_printf(&lb, "  Regs   SP=%04X FP=%04X", (unsigned)sp, (unsigned)fp);
    for (r = 0; r < 4; r++)
        lb_printf(&lb, " %s=%04X", REG_NAMES[r], (unsigned)st->reg[r]);
    lb_emit(&lb, out);
    fprintf(out, "  Text   0000-%04X  %u words of code\n", (unsigned)(L->text_end ? L->text_end - 1 : 0),
            (unsigned)L->text_end / 2);
    if (L->ndata == 0)
        fprintf(out, "  Data   0100-017F  empty\n");
    for (i = 0; i < L->ndata; i++) {
        const char *lbl = vw_sym_at(t, L->data[i]);
        uint16_t w = vw_word(mem, L->data[i]);
        lb_printf(&lb, "  %-5s  %04X  %-10s %04X%c", i == 0 ? "Data" : "", (unsigned)L->data[i], lbl ? lbl : "",
                  (unsigned)w, w != vw_word(prev, L->data[i]) ? '*' : ' ');
        lb_emit(&lb, out);
    }
    if (L->heap_hi <= L->heap_lo)
        fprintf(out, "  Heap   0180-027F  empty\n");
    for (row = L->heap_lo; row < L->heap_hi; row = (uint16_t)(row + 2)) {
        uint16_t w = vw_word(mem, row);
        lb_printf(&lb, "  %-5s  %04X  %-10s %04X%c", row == L->heap_lo ? "Heap" : "", (unsigned)row, "", (unsigned)w,
                  w != vw_word(prev, row) ? '*' : ' ');
        lb_emit(&lb, out);
    }
    if (!L->stack_used) {
        fprintf(out, "  Stack  0280-03FF  empty (SP=%04X)\n", (unsigned)sp);
        return;
    }
    fprintf(out, "  Stack  ROW   +0 OLDFP  +2 RA   +4 ARG  +6 RETV\n");
    for (row = 0x03F8; row >= L->stack_lo && row >= VW_STACK_BASE; row = (uint16_t)(row - 8)) {
        static const int width[4] = {10, 8, 8, 8};
        lb_printf(&lb, "         %04X  ", (unsigned)row);
        for (i = 0; i < 4; i++) {
            uint16_t a = (uint16_t)(row + 2 * i), w = vw_word(mem, a);
            lb_printf(&lb, "%04X%c%*s", (unsigned)w, w != vw_word(prev, a) ? '*' : ' ', width[i] - 5, "");
        }
        if (fp == row)
            lb_printf(&lb, "FP=%04X ", (unsigned)fp);
        if (sp >= row && sp < row + 8)
            lb_printf(&lb, "SP=%04X", (unsigned)sp);
        if (row + 8 <= sp)
            lb_printf(&lb, "(free: below SP)");
        lb_emit(&lb, out);
    }
}

void vw_segments(const vw_trace *t, FILE *out, vw_format fmt)
{
    static uint8_t mem[VW_MEM_SIZE], prev[VW_MEM_SIZE];
    seg_layout L;
    unsigned k, idx = 0;

    layout(t, &L);
    memcpy(mem, t->mem0, sizeof mem);
    memcpy(prev, mem, sizeof prev);
    snapshot(t, out, fmt, &L, idx++, 0, mem, prev);
    for (k = 1; k <= t->nsteps; k++) {
        apply_step(t, k, mem);
        if (!is_snapshot(&t->step[k - 1]))
            continue;
        if (fmt != VW_HTML)
            fputc('\n', out);
        snapshot(t, out, fmt, &L, idx++, k, mem, prev);
        memcpy(prev, mem, sizeof prev);
    }
}

/* ---- peek --------------------------------------------------------------- */

int vw_peek(const vw_trace *t, const char *expr, FILE *out, char *err, size_t errcap)
{
    static uint8_t mem[VW_MEM_SIZE];
    int deref = expr[0] == '*';
    const char *name = expr + deref;
    uint16_t addr, v;

    if (vw_sym_find(t, name, &addr) != 0) {
        snprintf(err, errcap, "no symbol '%s'", name);
        return -1;
    }
    if ((addr & 1u) || addr >= VW_MEM_SIZE) {
        snprintf(err, errcap, "symbol '%s' (0x%04X) is not a word address", name, (unsigned)addr);
        return -1;
    }
    final_mem(t, mem);
    v = vw_word(mem, addr);
    if (deref) {
        if ((v & 1u) || v >= VW_MEM_SIZE) {
            snprintf(err, errcap, "pointer %s = 0x%04X is odd or outside memory", name, (unsigned)v);
            return -1;
        }
        v = vw_word(mem, v);
    }
    fprintf(out, "%s=%u\n", expr, (unsigned)v);
    return 0;
}
