/*
 * fact.c -- 8! by a loop inside a function. Program B for Part 1: written
 * in C, then translated by hand into T13 assembly (programs/fact.s), one
 * C line at a time.
 *
 * The T13 machine is 16-bit and unsigned, so every value here is a
 * uint16_t: 8! = 40320 = 0x9D80 fits.
 *
 * Native check (tests/programs.sh): this file is compiled with the host
 * C compiler and its output must equal what t13sim leaves in memory.
 * The printf is the only line not translated (the T13 has no I/O).
 */
#include <stdint.h>
#include <stdio.h>

uint16_t result; /* Data: 0x0100 */

uint16_t fact(uint16_t n)
{
    uint16_t r = 1;

    while (n > 1) {
        r = (uint16_t)(r * n);
        n = (uint16_t)(n - 1);
    }
    return r;
}

int main(void)
{
    result = fact(8);
    printf("result=%u\n", (unsigned)result);
    return 0;
}
