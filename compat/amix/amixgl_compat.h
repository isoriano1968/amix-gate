/* amixgl_compat.h -- forced into every Mesa file built for AMIX */
#ifndef AMIXGL_COMPAT_H
#define AMIXGL_COMPAT_H

/* AMIX's sys/types.h has "enum boolean", which collides with Mesa's boolean */
#define boolean amix_sys_boolean
#include <sys/types.h>
#undef boolean
#include <sys/stat.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* POSIX.1b time, missing on AMIX (amixgl_compat.c, from gettimeofday) */
#ifndef AMIX_TIMESPEC_DEFINED
#define AMIX_TIMESPEC_DEFINED
struct timespec { time_t tv_sec; long tv_nsec; };
#endif
typedef int clockid_t;
#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1
#ifdef __cplusplus
extern "C" {
#endif
int clock_gettime(clockid_t, struct timespec *);
int nanosleep(const struct timespec *, struct timespec *);
#ifdef __cplusplus
}
#endif

#ifndef ETIMEDOUT
#define ETIMEDOUT 145
#endif

#ifdef __cplusplus
extern "C" {
#endif
int strcasecmp(const char *, const char *);
int strncasecmp(const char *, const char *, size_t);
char *strdup(const char *);
char *strndup(const char *, size_t);
char *strtok_r(char *, const char *, char **);
long long strtoll(const char *, char **, int);
unsigned long long strtoull(const char *, char **, int);
float strtof(const char *, char **);
int setenv(const char *, const char *, int);
#ifdef __cplusplus
}
#endif

#include <math.h>
#ifdef __cplusplus
extern "C" {
#endif
/* AMIX's libm is C89 + SVR4 float functions: the C99 ones Mesa wants */
double exp2(double);
double log2(double);
float exp2f(float);
float log2f(float);
float truncf(float);
double trunc(double);
float roundf(float);
double round(double);
float rintf(float);
long lrint(double);
long lrintf(float);
long long llrint(double);
long long llrintf(float);
float ldexpf(float, int);
float frexpf(float, int *);
float fmaxf(float, float);
float fminf(float, float);
double fmax(double, double);
double fmin(double, double);
float copysignf(float, float);
float nearbyintf(float);
double nearbyint(double);
float scalbnf(float, int);
float log1pf(float);
float expm1f(float);
float hypotf(float, float);
float cbrtf(float);
double rint(double);
double copysign(double, double);
#ifdef __cplusplus
}
static inline bool isnormal(double x) { return __builtin_isnormal(x); }
#else
#undef isnan
#define isnan(x) __builtin_isnan(x)
#define isinf(x) __builtin_isinf(x)
#define isfinite(x) __builtin_isfinite(x)
#define isnormal(x) __builtin_isnormal(x)
#define signbit(x) __builtin_signbit(x)
#define FP_NAN 0
#define FP_INFINITE 1
#define FP_ZERO 2
#define FP_SUBNORMAL 3
#define FP_NORMAL 4
#define fpclassify(x) __builtin_fpclassify(FP_NAN, FP_INFINITE, FP_NORMAL, FP_SUBNORMAL, FP_ZERO, x)
#endif
#ifndef INFINITY
#define INFINITY __builtin_inff()
#endif
#ifndef NAN
#define NAN __builtin_nanf("")
#endif
#include <limits.h>
#ifndef LLONG_MAX
#define LLONG_MAX 9223372036854775807LL
#define LLONG_MIN (-LLONG_MAX - 1LL)
#define ULLONG_MAX 18446744073709551615ULL
#endif
#ifndef RTLD_LOCAL
#define RTLD_LOCAL 0
#endif
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef M_PI_2
#define M_PI_2 1.57079632679489661923
#endif
#ifndef M_PI_4
#define M_PI_4 0.78539816339744830962
#endif
#ifndef M_LOG2E
#define M_LOG2E 1.4426950408889634074
#endif
#ifndef M_LN2
#define M_LN2 0.69314718055994530942
#endif
#ifndef M_SQRT2
#define M_SQRT2 1.41421356237309504880
#endif
#endif
