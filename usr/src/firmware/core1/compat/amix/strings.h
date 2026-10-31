#ifndef AMIX_STRINGS_H
#define AMIX_STRINGS_H
#include <string.h>
#ifdef __cplusplus
extern "C" {
#endif
int strcasecmp(const char *, const char *);
int strncasecmp(const char *, const char *, size_t);
#ifdef __cplusplus
}
#endif
static inline int ffs_amix_unused(void) { return 0; }
#endif
