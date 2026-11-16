#ifndef _KUNIT_TEST_H
#define _KUNIT_TEST_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
typedef uint8_t u8; typedef uint32_t u32;
struct kunit { int fails; };
#define KUNIT_FAIL_(t,msg,...) do{ (t)->fails++; fprintf(stderr,"  FAIL: " msg "\n",##__VA_ARGS__);}while(0)
#define KUNIT_EXPECT_EQ(t,a,b)   do{ long _a=(long)(a),_b=(long)(b); if(_a!=_b) KUNIT_FAIL_(t,"EQ %ld != %ld",_a,_b);}while(0)
#define KUNIT_EXPECT_LT(t,a,b)   do{ long _a=(long)(a),_b=(long)(b); if(!(_a<_b)) KUNIT_FAIL_(t,"LT %ld !< %ld",_a,_b);}while(0)
#define KUNIT_EXPECT_FALSE(t,c)  do{ if(c) KUNIT_FAIL_(t,"EXPECT_FALSE");}while(0)
#define KUNIT_EXPECT_MEMEQ(t,a,b,n) do{ if(memcmp((a),(b),(n))) KUNIT_FAIL_(t,"MEMEQ");}while(0)
#define KUNIT_ASSERT_NOT_NULL(t,p) do{ if(!(p)){KUNIT_FAIL_(t,"NULL");return;} }while(0)
struct kunit_case { const char*name; void(*fn)(struct kunit*); };
struct kunit_suite { const char*name; struct kunit_case*test_cases; };
#define KUNIT_CASE(f) { #f, f }
#define kunit_test_suite(s)
#define MODULE_DESCRIPTION(x)
#define MODULE_LICENSE(x)
#endif
