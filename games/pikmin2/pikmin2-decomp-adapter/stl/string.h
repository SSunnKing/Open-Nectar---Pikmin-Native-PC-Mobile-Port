#ifndef _P2PORT_STL_STRING_H
#define _P2PORT_STL_STRING_H
#include <cstring>
#include <string>

/* Macros de utilidad de include/stl/string.h del decomp */
#ifndef IS_SAME_STRING
#define IS_SAME_STRING(a, b)        (strcmp(a, b) == 0)
#endif
#ifndef IS_SAME_STRING_N
#define IS_SAME_STRING_N(a, b, n)   (strncmp(a, b, n) == 0)
#endif
#ifndef IS_SAME_STRING_PREFIX
#define IS_SAME_STRING_PREFIX(a, b) (IS_SAME_STRING_N(a, b, sizeof(b) - 1))
#endif

#endif
