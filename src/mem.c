/* Allocation wrappers (DLL: mem_calloc 0x1000f300, mem_free). */
#include <stdlib.h>
#include "ivx.h"

void *mem_calloc(int n, int size)
{
    return calloc((size_t)n, (size_t)size);
}

void mem_free(void *p)
{
    free(p);
}
