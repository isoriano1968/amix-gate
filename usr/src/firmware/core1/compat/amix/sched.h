/* sched.h for AMIX: only sched_yield, for Mesa's thread wrappers */
#ifndef AMIX_SCHED_H
#define AMIX_SCHED_H
static inline int sched_yield(void) { return 0; }
#endif
