/*
 * pthread.h for AMIX: AMIX has no threads.  Mesa's OSMesa runs in one
 * thread, so mutexes and condition variables do nothing, once runs once and
 * thread-specific data is a plain variable.  Creating a thread fails.
 */
#ifndef AMIX_PTHREAD_H
#define AMIX_PTHREAD_H
#include <time.h>
#include <errno.h>

typedef int pthread_t;
typedef int pthread_attr_t;
typedef int pthread_mutex_t;
typedef int pthread_mutexattr_t;
typedef int pthread_cond_t;
typedef int pthread_condattr_t;
typedef int pthread_once_t;
typedef unsigned pthread_key_t;

#define PTHREAD_MUTEX_INITIALIZER 0
#define PTHREAD_COND_INITIALIZER 0
#define PTHREAD_ONCE_INIT 0
#define PTHREAD_MUTEX_RECURSIVE 1
#define PTHREAD_MUTEX_NORMAL 0
#define PTHREAD_CREATE_JOINABLE 0
#define PTHREAD_CREATE_DETACHED 1

#ifndef AMIX_TIMESPEC_DEFINED
#define AMIX_TIMESPEC_DEFINED
#endif

static inline int pthread_mutex_init(pthread_mutex_t *m, const pthread_mutexattr_t *a) { (void)a; *m = 0; return 0; }
static inline int pthread_mutex_destroy(pthread_mutex_t *m) { (void)m; return 0; }
static inline int pthread_mutex_lock(pthread_mutex_t *m) { (void)m; return 0; }
static inline int pthread_mutex_trylock(pthread_mutex_t *m) { (void)m; return 0; }
static inline int pthread_mutex_unlock(pthread_mutex_t *m) { (void)m; return 0; }
/* not inline: Mesa's threads_posix.h declares these weak */
#ifdef __cplusplus
extern "C" {
#endif
int pthread_mutexattr_init(pthread_mutexattr_t *a);
int pthread_mutexattr_destroy(pthread_mutexattr_t *a);
int pthread_mutexattr_settype(pthread_mutexattr_t *a, int t);
#ifdef __cplusplus
}
#endif
static inline int pthread_mutex_timedlock(pthread_mutex_t *m, const struct timespec *t) { (void)m; (void)t; return 0; }
typedef int pthread_barrier_t;
typedef int pthread_barrierattr_t;
#define PTHREAD_BARRIER_SERIAL_THREAD (-1)
static inline int pthread_barrier_init(pthread_barrier_t *b, const pthread_barrierattr_t *a, unsigned n) { (void)a; *b = (int)n; return 0; }
static inline int pthread_barrier_destroy(pthread_barrier_t *b) { (void)b; return 0; }
static inline int pthread_barrier_wait(pthread_barrier_t *b) { (void)b; return PTHREAD_BARRIER_SERIAL_THREAD; }
static inline int pthread_cond_init(pthread_cond_t *c, const pthread_condattr_t *a) { (void)a; *c = 0; return 0; }
static inline int pthread_cond_destroy(pthread_cond_t *c) { (void)c; return 0; }
static inline int pthread_cond_signal(pthread_cond_t *c) { (void)c; return 0; }
static inline int pthread_cond_broadcast(pthread_cond_t *c) { (void)c; return 0; }
static inline int pthread_cond_wait(pthread_cond_t *c, pthread_mutex_t *m) { (void)c; (void)m; return 0; }
static inline int pthread_cond_timedwait(pthread_cond_t *c, pthread_mutex_t *m, const struct timespec *t)
{ (void)c; (void)m; (void)t; return ETIMEDOUT; }
static inline int pthread_once(pthread_once_t *o, void (*f)(void)) { if (!*o) { *o = 1; f(); } return 0; }
static inline pthread_t pthread_self(void) { return 1; }
static inline int pthread_equal(pthread_t a, pthread_t b) { return a == b; }
static inline int pthread_attr_init(pthread_attr_t *a) { *a = 0; return 0; }
static inline int pthread_attr_destroy(pthread_attr_t *a) { (void)a; return 0; }
static inline int pthread_attr_setdetachstate(pthread_attr_t *a, int s) { *a = s; return 0; }
static inline int pthread_create(pthread_t *t, const pthread_attr_t *a, void *(*f)(void *), void *p)
{ (void)t; (void)a; (void)f; (void)p; return EAGAIN; }
static inline int pthread_join(pthread_t t, void **r) { (void)t; (void)r; return ESRCH; }
static inline int pthread_detach(pthread_t t) { (void)t; return 0; }
static inline void pthread_exit(void *r) { (void)r; for (;;) ; }

/* thread-specific data: one thread, so a table of plain values */
#define AMIX_PTHREAD_KEYS 64
extern void *amix_pthread_tsd[AMIX_PTHREAD_KEYS];
extern unsigned amix_pthread_nkeys;
static inline int pthread_key_create(pthread_key_t *k, void (*d)(void *))
{ (void)d; if (amix_pthread_nkeys >= AMIX_PTHREAD_KEYS) return EAGAIN; *k = amix_pthread_nkeys++; return 0; }
static inline int pthread_key_delete(pthread_key_t k) { amix_pthread_tsd[k] = 0; return 0; }
static inline void *pthread_getspecific(pthread_key_t k) { return amix_pthread_tsd[k]; }
static inline int pthread_setspecific(pthread_key_t k, const void *v) { amix_pthread_tsd[k] = (void *)v; return 0; }
#endif
