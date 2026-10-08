#ifndef _P2PORT_STL_CTYPE_H
#define _P2PORT_STL_CTYPE_H
#include <cctype>
#ifdef __cplusplus
static inline int isprintable(int c) { return isprint(c); }
#else
static inline int isprintable(int c) { return isprint(c); }
#endif
#endif
