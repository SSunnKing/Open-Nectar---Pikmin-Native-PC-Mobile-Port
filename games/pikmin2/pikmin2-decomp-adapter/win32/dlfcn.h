/* Windows: no hay dlfcn.h. Solo se usa dladdr() para poner nombre a los marcos
 * de pila de los avisos de depuración; aquí nunca resuelve (devuelve 0). */
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    const char* dli_fname;
    void* dli_fbase;
    const char* dli_sname;
    void* dli_saddr;
} Dl_info;
static inline int dladdr(const void* addr, Dl_info* info) { (void)addr; (void)info; return 0; }
#ifdef __cplusplus
}
#endif
