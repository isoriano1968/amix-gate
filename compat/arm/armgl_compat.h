/* armgl_compat.h -- forced into every Mesa file built for the Z3660's core 1
 * (bare metal, newlib): no OS, one thread */
#ifndef ARMGL_COMPAT_H
#define ARMGL_COMPAT_H
#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <limits.h>
/* newlib declares these only for some POSIX feature sets; armgl_compat.c
 * provides them (there is no OS) */
#ifdef __cplusplus
extern "C" {
#endif
int sched_yield(void);
int nanosleep(const struct timespec *, struct timespec *);
int clock_gettime(clockid_t, struct timespec *);
#ifdef __cplusplus
}
#endif
#ifndef LLONG_MAX
#define LLONG_MAX 9223372036854775807LL
#endif
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef RTLD_LOCAL
#define RTLD_LOCAL 0
#endif
#endif
