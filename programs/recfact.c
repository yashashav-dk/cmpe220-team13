/*
 * recfact.c -- recursive factorial, the Part 2 program: main and one
 * recursive function. Translated by hand into T13 assembly
 * (programs/recfact.s), one C line at a time.
 *
 * It uses all four segments of the program layout:
 *   Text   the code of main and fact
 *   Data   the globals n and result
 *   Heap   the block that malloc returns; the answer is stored there
 *   Stack  one 4-word frame per active call of fact
 *
 * Native check (tests/programs.sh): compiled with the host C compiler,
 * its output must equal what t13sim leaves in memory. The printf is the
 * only line not translated (the T13 has no I/O).
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

uint16_t n = 5;    /* Data: 0x0100 */
uint16_t *result;  /* Data: 0x0102, points into the Heap */

uint16_t fact(uint16_t k)
{
    if (k <= 1)
        return 1;
    return (uint16_t)(k * fact((uint16_t)(k - 1)));
}

int main(void)
{
    result = malloc(sizeof *result);
    if (result == NULL)
        return 1;
    *result = fact(n);
    printf("*result=%u\n", (unsigned)*result);
    free(result);
    return 0;
}
