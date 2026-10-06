/* Windows: no hay execinfo.h. Las trazas de pila de los avisos de depuración
 * (heap, crash) se quedan vacías; el juego no depende de ellas. Solo se usa en
 * las builds de Windows (CMake añade este directorio únicamente con WIN32). */
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
static inline int backtrace(void** buffer, int size) { (void)buffer; (void)size; return 0; }
static inline char** backtrace_symbols(void* const* buffer, int size) { (void)buffer; (void)size; return 0; }
static inline void backtrace_symbols_fd(void* const* buffer, int size, int fd) { (void)buffer; (void)size; (void)fd; }
#ifdef __cplusplus
}
#endif
