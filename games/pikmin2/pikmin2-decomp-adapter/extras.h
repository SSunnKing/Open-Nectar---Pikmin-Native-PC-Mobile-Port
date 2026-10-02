#ifndef _EXTRAS_H
#define _EXTRAS_H
#include <strings.h>
#ifdef __cplusplus
extern "C" {
#endif
static inline int stricmp(const char* a, const char* b) { return strcasecmp(a, b); }
#ifdef __cplusplus
}
#endif
#endif
