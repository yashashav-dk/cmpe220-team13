/*
 * main.c -- the t13view command line.
 *
 *   t13view trace.trc [VIEW] [--html] [--name NAME] [-o out]
 *
 * VIEW is one of:
 *   --summary      end state and the words that changed (default)
 *   --waterfall    one row per executed instruction
 *   --grid         the memory block after loading and at the end
 *   --blocks       the memory block after every instruction
 *   --loaded       the executable as loaded, decoded
 *   --segments     Text / Data / Heap / Stack at load, each CALL, RET, HALT
 *   --peek EXPR    print NAME=value for EXPR = NAME (M[NAME]) or *NAME
 *
 * --html writes an HTML <figure> for the report instead of plain text;
 * --name sets the program name used in its captions.
 *
 * Exit 0: done. Exit 1: --peek could not evaluate EXPR. Exit 2: usage
 * error, or the trace cannot be read or is malformed (path:line: message).
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "view.h"

enum { TRACE_MAX = 16 << 20 };

static char text[TRACE_MAX + 1];
static vw_trace trace;

static int usage(void)
{
    fprintf(stderr, "usage: t13view trace.trc [--summary|--waterfall|--grid|--blocks|--loaded|--segments|"
                    "--peek EXPR] [--html] [--name NAME] [-o out]\n");
    return 2;
}

int main(int argc, char **argv)
{
    void (*view)(const vw_trace *, FILE *, vw_format) = vw_summary;
    const char *src = NULL, *out_path = NULL, *name = NULL, *peek = NULL;
    vw_format fmt = VW_TEXT;
    char err[160];
    FILE *f, *out = stdout;
    size_t n;
    int i, line, rc = 0, nviews = 0;

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "--summary") == 0) view = vw_summary, nviews++;
        else if (strcmp(a, "--waterfall") == 0) view = vw_waterfall, nviews++;
        else if (strcmp(a, "--grid") == 0) view = vw_grid, nviews++;
        else if (strcmp(a, "--blocks") == 0) view = vw_blocks, nviews++;
        else if (strcmp(a, "--loaded") == 0) view = vw_loaded, nviews++;
        else if (strcmp(a, "--segments") == 0) view = vw_segments, nviews++;
        else if (strcmp(a, "--peek") == 0 && i + 1 < argc) peek = argv[++i], nviews++;
        else if (strcmp(a, "--html") == 0) fmt = VW_HTML;
        else if (strcmp(a, "--name") == 0 && i + 1 < argc) name = argv[++i];
        else if (strcmp(a, "-o") == 0 && i + 1 < argc) out_path = argv[++i];
        else if (a[0] == '-' || src != NULL) return usage();
        else src = a;
    }
    if (src == NULL || nviews > 1)
        return usage();

    if ((f = fopen(src, "rb")) == NULL) {
        fprintf(stderr, "t13view: cannot open '%s': %s\n", src, strerror(errno));
        return 2;
    }
    n = fread(text, 1, TRACE_MAX + 1, f);
    if (ferror(f) || n > TRACE_MAX) {
        fprintf(stderr, "t13view: cannot read '%s' (error or larger than %d bytes)\n", src, TRACE_MAX);
        fclose(f);
        return 2;
    }
    fclose(f);
    if ((line = vw_read(&trace, text, n, err, sizeof err)) != 0) {
        fprintf(stderr, "%s:%d: error: %s\n", src, line, err);
        return 2;
    }
    snprintf(trace.name, sizeof trace.name, "%s", name ? name : "");

    if (out_path != NULL && (out = fopen(out_path, "wb")) == NULL) {
        fprintf(stderr, "t13view: cannot write '%s': %s\n", out_path, strerror(errno));
        vw_free(&trace);
        return 2;
    }
    if (peek != NULL) {
        if (vw_peek(&trace, peek, out, err, sizeof err) != 0) {
            fprintf(stderr, "t13view: %s\n", err);
            rc = 1;
        }
    } else {
        view(&trace, out, fmt);
    }
    if (out != stdout && fclose(out) != 0) {
        fprintf(stderr, "t13view: error writing '%s'\n", out_path);
        rc = 2;
    }
    vw_free(&trace);
    return rc;
}
