/*
 * armgl_compat.c -- the OS calls Mesa needs on the Z3660's core 1 (bare
 * metal, newlib, one thread).  The platform supplies armgl_now_us().
 */
#include <errno.h>
#include <stdlib.h>
#include <pthread.h>

unsigned long long armgl_now_us(void);

int pthread_mutex_init(pthread_mutex_t *m, const pthread_mutexattr_t *a) { (void)a; *m = 0; return 0; }
int pthread_mutex_destroy(pthread_mutex_t *m) { (void)m; return 0; }
int pthread_mutex_lock(pthread_mutex_t *m) { (void)m; return 0; }
int pthread_mutex_trylock(pthread_mutex_t *m) { (void)m; return 0; }
int pthread_mutex_timedlock(pthread_mutex_t *m, const struct timespec *t) { (void)m; (void)t; return 0; }
int pthread_mutex_unlock(pthread_mutex_t *m) { (void)m; return 0; }
int pthread_mutexattr_init(pthread_mutexattr_t *a) { (void)a; return 0; }
int pthread_mutexattr_destroy(pthread_mutexattr_t *a) { (void)a; return 0; }
int pthread_mutexattr_settype(pthread_mutexattr_t *a, int t) { (void)a; (void)t; return 0; }
int pthread_cond_init(pthread_cond_t *c, const pthread_condattr_t *a) { (void)a; *c = 0; return 0; }
int pthread_cond_destroy(pthread_cond_t *c) { (void)c; return 0; }
int pthread_cond_signal(pthread_cond_t *c) { (void)c; return 0; }
int pthread_cond_broadcast(pthread_cond_t *c) { (void)c; return 0; }
int pthread_cond_wait(pthread_cond_t *c, pthread_mutex_t *m) { (void)c; (void)m; return 0; }
int pthread_cond_timedwait(pthread_cond_t *c, pthread_mutex_t *m, const struct timespec *t)
{ (void)c; (void)m; (void)t; return ETIMEDOUT; }
int pthread_once(pthread_once_t *o, void (*f)(void))
{
    if (!o->init_executed) {
        o->init_executed = 1;
        f();
    }
    return 0;
}
pthread_t pthread_self(void) { return 1; }
int pthread_equal(pthread_t a, pthread_t b) { return a == b; }
int pthread_attr_init(pthread_attr_t *a) { (void)a; return 0; }
int pthread_attr_destroy(pthread_attr_t *a) { (void)a; return 0; }
int pthread_attr_setdetachstate(pthread_attr_t *a, int s) { (void)a; (void)s; return 0; }
int pthread_create(pthread_t *t, const pthread_attr_t *a, void *(*f)(void *), void *p)
{ (void)t; (void)a; (void)f; (void)p; return EAGAIN; }
int pthread_join(pthread_t t, void **r) { (void)t; (void)r; return ESRCH; }
int pthread_detach(pthread_t t) { (void)t; return 0; }
void pthread_exit(void *r) { (void)r; for (;;) ; }

#define NKEYS 64
static void *tsd[NKEYS];
static unsigned nkeys;
int pthread_key_create(pthread_key_t *k, void (*d)(void *))
{
    (void)d;
    if (nkeys >= NKEYS)
        return EAGAIN;
    *k = nkeys++;
    return 0;
}
int pthread_key_delete(pthread_key_t k) { tsd[k] = 0; return 0; }
void *pthread_getspecific(pthread_key_t k) { return tsd[k]; }
int pthread_setspecific(pthread_key_t k, const void *v) { tsd[k] = (void *)v; return 0; }
int pthread_barrier_init(pthread_barrier_t *b, const pthread_barrierattr_t *a, unsigned n) { (void)a; *b = n; return 0; }
int pthread_barrier_destroy(pthread_barrier_t *b) { (void)b; return 0; }
int pthread_barrier_wait(pthread_barrier_t *b) { (void)b; return PTHREAD_BARRIER_SERIAL_THREAD; }
int pthread_sigmask(int how, const sigset_t *set, sigset_t *old) { (void)how; (void)set; (void)old; return 0; }

int sched_yield(void) { return 0; }

int clock_gettime(clockid_t id, struct timespec *ts)
{
    unsigned long long us = armgl_now_us();

    (void)id;
    ts->tv_sec = (time_t)(us / 1000000ULL);
    ts->tv_nsec = (long)(us % 1000000ULL) * 1000L;
    return 0;
}

int nanosleep(const struct timespec *req, struct timespec *rem)
{
    unsigned long long end = armgl_now_us() + (unsigned long long)req->tv_sec * 1000000ULL
                             + (unsigned long long)req->tv_nsec / 1000ULL;

    while (armgl_now_us() < end)
        ;
    if (rem)
        rem->tv_sec = rem->tv_nsec = 0;
    return 0;
}

/* Gallium's remote debugger (rbug) talks over sockets: not here */
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

/* POSIX bits Mesa's debug paths call: one user, no file system */
#include <sys/types.h>
uid_t getuid(void) { return 0; }
uid_t geteuid(void) { return 0; }
int mkdir(const char *path, mode_t mode) { (void)path; (void)mode; errno = ENOSYS; return -1; }
void sync(void) { }
