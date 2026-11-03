/*
 * main.c -- the t13as command line.
 *
 *   t13as source.s [-o out.img] [-l out.lst]
 *
 * Exit 0: assembled and written. Exit 1: errors in the source (all printed
 * to stderr; no output file is written). Exit 2: usage or file problems.
 * See docs/asm.md section 1.
 */
#include <stdio.h>
#include <string.h>

#include "asm.h"

static asm_unit unit; /* large; keep it off the stack */

static int usage(void)
{
    fprintf(stderr, "usage: t13as source.s [-o out.img] [-l out.lst]\n");
    return 2;
}

/* Default image name: source.s -> source.img, anything else -> name.img. */
static void default_image_name(const char *src, char *out, size_t cap)
{
    size_t n = strlen(src);

    if (n > 2 && strcmp(src + n - 2, ".s") == 0)
        snprintf(out, cap, "%.*s.img", (int)(n - 2), src);
    else
        snprintf(out, cap, "%s.img", src);
}

static int write_file(const char *path, void (*writer)(const asm_unit *, FILE *))
{
    FILE *f = fopen(path, "w");

    if (f == NULL) {
        fprintf(stderr, "t13as: cannot write %s\n", path);
        return 0;
    }
    writer(&unit, f);
    if (fclose(f) != 0) {
        fprintf(stderr, "t13as: cannot write %s\n", path);
        return 0;
    }
    return 1;
}

int main(int argc, char **argv)
{
    const char *src = NULL, *img = NULL, *lst = NULL;
    char img_default[512];
    int i, n;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "-l") == 0) {
            if (i + 1 >= argc)
                return usage();
            if (argv[i][1] == 'o')
                img = argv[++i];
            else
                lst = argv[++i];
        } else if (argv[i][0] == '-' || src != NULL) {
            return usage();
        } else {
            src = argv[i];
        }
    }
    if (src == NULL)
        return usage();
    if (img == NULL) {
        default_image_name(src, img_default, sizeof img_default);
        img = img_default;
    }

    n = assemble_file(src, &unit);
    if (n < 0) {
        fprintf(stderr, "t13as: cannot read %s\n", src);
        return 2;
    }
    if (n > 0) {
        for (i = 0; i < n && i < ASM_MAX_ERRORS; i++)
            fprintf(stderr, "%s\n", unit.errors[i]);
        if (n > ASM_MAX_ERRORS)
            fprintf(stderr, "t13as: %d errors (first %d shown)\n", n, ASM_MAX_ERRORS);
        return 1;
    }

    if (!write_file(img, write_image))
        return 2;
    if (lst != NULL && !write_file(lst, write_listing))
        return 2;
    return 0;
}
