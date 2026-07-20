/* Minimal C++ runtime support so cantcoap's bitcode runs under KLEE without
 * linking libstdc++/libc++. cantcoap uses only operator new/delete (no STL,
 * no exceptions, no RTTI), so malloc/free wrappers are enough. */
#include <stddef.h>
#include <stdlib.h>

void *operator new(size_t n)        { return malloc(n ? n : 1); }
void *operator new[](size_t n)      { return malloc(n ? n : 1); }
void  operator delete(void *p)             { free(p); }
void  operator delete[](void *p)           { free(p); }
void  operator delete(void *p, size_t)     { free(p); }
void  operator delete[](void *p, size_t)   { free(p); }
