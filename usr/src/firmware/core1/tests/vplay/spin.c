/* spin.c -- a fixed amount of integer work, timed: is the CPU free? */
#include <stdio.h>
#include <sys/time.h>
int main(void)
{
    struct timeval a, b;
    volatile unsigned long x = 1;
    unsigned long i;

    gettimeofday(&a, 0);
    for (i = 0; i < 40000000UL; i++)
        x = x * 1103515245UL + 12345UL;
    gettimeofday(&b, 0);
    printf("spin: %.2f s\n", (b.tv_sec - a.tv_sec) + (b.tv_usec - a.tv_usec) / 1e6);
    return 0;
}
