#ifndef _SHIM_SLAB
#define _SHIM_SLAB
#include <stdlib.h>
#define kmalloc(n,f) malloc(n)
#define kzalloc(n,f) calloc(1,n)
#define kfree(p) free(p)
#define GFP_KERNEL 0
#endif
