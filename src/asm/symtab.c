/*
 * symtab.c -- the assembler's symbol table: labels and .equ constants.
 *
 * A fixed array searched linearly. Programs for a 1 KiB machine have a few
 * dozen symbols at most, so nothing faster is needed.
 */
#include <string.h>

#include "asm.h"

void symtab_init(symtab *t)
{
    memset(t, 0, sizeof *t);
}

int symtab_define(symtab *t, const char *name, uint16_t value, int line, int is_label)
{
    symbol *s;

    if (symtab_find(t, name) != NULL)
        return 0;
    if (t->count >= ASM_MAX_SYMS)
        return -1;

    s = &t->syms[t->count++];
    strncpy(s->name, name, ASM_NAME_LEN - 1);
    s->name[ASM_NAME_LEN - 1] = '\0';
    s->value = value;
    s->line = line;
    s->is_label = is_label;
    return 1;
}

const symbol *symtab_find(const symtab *t, const char *name)
{
    int i;

    for (i = 0; i < t->count; i++)
        if (strcmp(t->syms[i].name, name) == 0)
            return &t->syms[i];
    return NULL;
}
