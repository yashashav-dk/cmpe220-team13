/*
 * main.c -- the t13sim command line.
 *
 *   t13sim image.img [-o out.trc] [-n max_steps]
 *
 * Loads the image, runs it until HALT, an error or max_steps instructions
 * (default 10000), and writes the execution trace (docs/trace.md). `-o -`
 * writes the trace to stdout. A one-line summary goes to stdout (stderr
 * when the trace itself goes to stdout).
 *
 * Exit 0: the program halted. Exit 1: it stopped with an error or ran out
 * of steps; the trace is still written and says why. Exit 2: usage error,
 * or the image cannot be read or loaded (no trace is written).
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sim.h"

enum { IMAGE_MAX = 1 << 20, DEFAULT_STEPS = 10000 };

static sim_cpu cpu;
static char image[IMAGE_MAX + 1];

static int usage(void)
{
    fprintf(stderr, "usage: t13sim image.img [-o out.trc] [-n max_steps]\n");
    return 2;
}

/* Default trace name: x.img -> x.trc, anything else -> name.trc. */
static void default_trace_name(const char *img, char *out, size_t cap)
{
    size_t n = strlen(img);

    if (n > 4 && strcmp(img + n - 4, ".img") == 0)
        snprintf(out, cap, "%.*s.trc", (int)(n - 4), img);
    else
        snprintf(out, cap, "%s.trc", img);
}

/* Read the whole file into image[]. Returns its length, or -1. */
static long read_image(const char *path)
{
    FILE *f = fopen(path, "rb");
    size_t n;

    if (f == NULL) {
        fprintf(stderr, "t13sim: cannot open '%s': %s\n", path, strerror(errno));
        return -1;
    }
    n = fread(image, 1, IMAGE_MAX + 1, f);
    if (ferror(f) || n > IMAGE_MAX) {
        fprintf(stderr, "t13sim: cannot read '%s' (error or larger than %d bytes)\n", path, IMAGE_MAX);
        fclose(f);
        return -1;
    }
    fclose(f);
    image[n] = '\0';
    return (long)n;
}

int main(int argc, char **argv)
{
    const char *src = NULL, *out_path = NULL;
    char dflt[1024], err[128];
    unsigned long max_steps = DEFAULT_STEPS;
    sim_result res;
    FILE *out, *info;
    long len;
    int i, line;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            out_path = argv[++i];
        } else if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            char *end;
            errno = 0;
            max_steps = strtoul(argv[++i], &end, 10);
            if (*argv[i] == '\0' || *end != '\0' || errno != 0 || max_steps == 0 || max_steps > 10000000ul)
                return usage();
        } else if (argv[i][0] == '-' || src != NULL) {
            return usage();
        } else {
            src = argv[i];
        }
    }
    if (src == NULL)
        return usage();
    if (out_path == NULL) {
        default_trace_name(src, dflt, sizeof dflt);
        out_path = dflt;
    }

    if ((len = read_image(src)) < 0)
        return 2;
    if ((line = sim_load(&cpu, image, (size_t)len, err, sizeof err)) != 0) {
        fprintf(stderr, "%s:%d: error: %s\n", src, line, err);
        return 2;
    }

    /* Binary mode: the trace is LF text on every OS (docs/trace.md). */
    if (strcmp(out_path, "-") == 0) {
        out = stdout;
        info = stderr;
    } else if ((out = fopen(out_path, "wb")) == NULL) {
        fprintf(stderr, "t13sim: cannot write '%s': %s\n", out_path, strerror(errno));
        return 2;
    } else {
        info = stdout;
    }

    sim_run(&cpu, out, (unsigned)max_steps, &res);

    if (out != stdout && fclose(out) != 0) {
        fprintf(stderr, "t13sim: error writing '%s'\n", out_path);
        return 2;
    }
    switch (res.end) {
    case SIM_END_HALT:
        fprintf(info, "%s: HALT after %u steps, PC=%04X\n", src, res.steps, (unsigned)res.pc);
        return 0;
    case SIM_END_LIMIT:
        fprintf(info, "%s: stopped at the step limit (%u steps), PC=%04X\n", src, res.steps,
                (unsigned)res.pc);
        return 1;
    case SIM_END_ERROR:
        fprintf(stderr, "%s: error at PC=%04X after %u steps: %s\n", src, (unsigned)res.pc,
                res.steps, res.msg);
        return 1;
    }
    return 1;
}
