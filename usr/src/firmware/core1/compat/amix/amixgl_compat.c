/*
 * amixgl_compat.c -- what Mesa 18.3 needs from a C99/POSIX libc that AMIX's
 * (SVR4, 1991) does not have.  Declarations in amixgl_compat.h and pthread.h.
 */
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <pthread.h>

double hypot(double, double);
double cbrt(double);

/* pthread.h: one thread */
void *amix_pthread_tsd[AMIX_PTHREAD_KEYS];
unsigned amix_pthread_nkeys;

int pthread_mutexattr_init(pthread_mutexattr_t *a) { *a = 0; return 0; }
int pthread_mutexattr_destroy(pthread_mutexattr_t *a) { (void)a; return 0; }
int pthread_mutexattr_settype(pthread_mutexattr_t *a, int t) { *a = t; return 0; }

/* time */
int clock_gettime(clockid_t id, struct timespec *ts)
{
    struct timeval tv;

    (void)id;
    gettimeofday(&tv, NULL);
    ts->tv_sec = tv.tv_sec;
    ts->tv_nsec = tv.tv_usec * 1000L;
    return 0;
}

int nanosleep(const struct timespec *req, struct timespec *rem)
{
    if (rem)
        rem->tv_sec = rem->tv_nsec = 0;
    return usleep((unsigned long)req->tv_sec * 1000000UL + (unsigned long)(req->tv_nsec / 1000));
}

/* strings */
char *strndup(const char *s, size_t n)
{
    size_t k = 0;
    char *d;

    while (k < n && s[k])
        k++;
    if (!(d = malloc(k + 1)))
        return NULL;
    memcpy(d, s, k);
    d[k] = 0;
    return d;
}

char *strtok_r(char *s, const char *delim, char **save)
{
    char *e;

    if (!s)
        s = *save;
    s += strspn(s, delim);
    if (!*s) {
        *save = s;
        return NULL;
    }
    e = s + strcspn(s, delim);
    if (*e)
        *e++ = 0;
    *save = e;
    return s;
}

unsigned long long strtoull(const char *s, char **end, int base)
{
    unsigned long long v = 0;
    int d;

    while (*s == ' ' || *s == '\t' || *s == '\n')
        s++;
    if (*s == '+')
        s++;
    if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
        base = 16;
    } else if (base == 0)
        base = *s == '0' ? 8 : 10;
    for (;; s++) {
        if (*s >= '0' && *s <= '9')
            d = *s - '0';
        else if (*s >= 'a' && *s <= 'z')
            d = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'Z')
            d = *s - 'A' + 10;
        else
            break;
        if (d >= base)
            break;
        v = v * base + d;
    }
    if (end)
        *end = (char *)s;
    return v;
}

long long strtoll(const char *s, char **end, int base)
{
    while (*s == ' ' || *s == '\t' || *s == '\n')
        s++;
    if (*s == '-')
        return -(long long)strtoull(s + 1, end, base);
    return (long long)strtoull(s, end, base);
}

float strtof(const char *s, char **end)
{
    return (float)strtod(s, end);
}

int setenv(const char *name, const char *value, int overwrite)
{
    char *e;

    if (!overwrite && getenv(name))
        return 0;
    if (!(e = malloc(strlen(name) + strlen(value) + 2)))
        return -1;
    strcpy(e, name);
    strcat(e, "=");
    strcat(e, value);
    return putenv(e);
}

/* C99 math on top of AMIX's libm (C89 + SVR4 float versions) */
double exp2(double x) { return pow(2.0, x); }
double log2(double x) { return log(x) * 1.4426950408889634074; }
float exp2f(float x) { return (float)pow(2.0, x); }
float log2f(float x) { return (float)(log(x) * 1.4426950408889634074); }
double trunc(double x) { return x < 0.0 ? ceil(x) : floor(x); }
float truncf(float x) { return x < 0.0f ? ceilf(x) : floorf(x); }
double round(double x) { return x < 0.0 ? -floor(-x + 0.5) : floor(x + 0.5); }
float roundf(float x) { return x < 0.0f ? -floorf(-x + 0.5f) : floorf(x + 0.5f); }
float rintf(float x) { return (float)rint(x); }
long lrint(double x) { return (long)rint(x); }
long lrintf(float x) { return (long)rint(x); }
long long llrint(double x) { return (long long)rint(x); }
long long llrintf(float x) { return (long long)rint(x); }
double nearbyint(double x) { return rint(x); }
float nearbyintf(float x) { return (float)rint(x); }
float ldexpf(float x, int e) { return (float)ldexp(x, e); }
float frexpf(float x, int *e) { return (float)frexp(x, e); }
float scalbnf(float x, int e) { return (float)ldexp(x, e); }
float fmaxf(float a, float b) { return a != a ? b : b != b ? a : a > b ? a : b; }
float fminf(float a, float b) { return a != a ? b : b != b ? a : a < b ? a : b; }
double fmax(double a, double b) { return a != a ? b : b != b ? a : a > b ? a : b; }
double fmin(double a, double b) { return a != a ? b : b != b ? a : a < b ? a : b; }
float copysignf(float a, float b) { return (float)copysign(a, b); }
float log1pf(float x) { return (float)log(1.0 + x); }
float expm1f(float x) { return (float)(exp(x) - 1.0); }
float hypotf(float a, float b) { return (float)hypot(a, b); }
float cbrtf(float x) { return (float)cbrt(x); }

size_t strnlen(const char *s, size_t n)
{
    size_t k = 0;

    while (k < n && s[k])
        k++;
    return k;
}

int vasprintf(char **out, const char *fmt, va_list ap)
{
    va_list aq;
    int n;

    va_copy(aq, ap);
    n = vsnprintf(NULL, 0, fmt, aq);
    va_end(aq);
    if (n < 0 || !(*out = malloc((size_t)n + 1)))
        return -1;
    return vsnprintf(*out, (size_t)n + 1, fmt, ap);
}

/* threads are never created (pthread_create fails), so no signal masks */
int pthread_sigmask(int how, const void *set, void *old)
{
    (void)how; (void)set; (void)old;
    return 0;
}

/* Gallium's remote debugger (rbug) talks over sockets: not on AMIX */
int u_socket_init(void) { return 0; }
void u_socket_stop(void) { }
void u_socket_close(int s) { (void)s; }
int u_socket_accept(int s) { (void)s; return -1; }
int u_socket_send(int s, void *data, size_t size) { (void)s; (void)data; (void)size; return -1; }
int u_socket_peek(int s, void *data, size_t size) { (void)s; (void)data; (void)size; return -1; }
int u_socket_recv(int s, void *data, size_t size) { (void)s; (void)data; (void)size; return -1; }
int u_socket_connect(const char *host, int port) { (void)host; (void)port; return -1; }
int u_socket_listen_on_port(unsigned short port) { (void)port; return -1; }
void u_socket_block(int s, int block) { (void)s; (void)block; }
