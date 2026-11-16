#ifndef KUNIT_SHIM_H
#define KUNIT_SHIM_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
typedef uint8_t u8; typedef uint32_t u32;
struct kunit { int fails; };
static struct kunit *CUR;
#define KUNIT_FAIL_(t,msg,...) do{ (t)->fails++; fprintf(stderr,"FAIL: " msg "\n",##__VA_ARGS__);}while(0)
#define KUNIT_EXPECT_EQ(t,a,b)   do{ long _a=(long)(a),_b=(long)(b); if(_a!=_b) KUNIT_FAIL_(t,"EQ %ld != %ld",_a,_b);}while(0)
#define KUNIT_EXPECT_LT(t,a,b)   do{ long _a=(long)(a),_b=(long)(b); if(!(_a<_b)) KUNIT_FAIL_(t,"LT %ld !< %ld",_a,_b);}while(0)
#define KUNIT_EXPECT_FALSE(t,c)  do{ if(c) KUNIT_FAIL_(t,"EXPECT_FALSE");}while(0)
#define KUNIT_EXPECT_MEMEQ(t,a,b,n) do{ if(memcmp((a),(b),(n))) KUNIT_FAIL_(t,"MEMEQ");}while(0)
#define KUNIT_ASSERT_NOT_NULL(t,p) do{ if(!(p)){KUNIT_FAIL_(t,"NULL");return;} }while(0)
#define kmalloc(n,f) malloc(n)
#define kzalloc(n,f) calloc(1,n)
#define kfree(p) free(p)
#define GFP_KERNEL 0
#endif
