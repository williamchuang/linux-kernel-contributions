#ifndef _STUB_KERNEL_H
#define _STUB_KERNEL_H
#include <stddef.h>
#include <stdint.h>
#define likely(x)   (x)
#define unlikely(x) (x)
#define fallthrough __attribute__((__fallthrough__))
#define pr_debug(...) do {} while (0)
#define pr_err(...)   do {} while (0)
#define pr_warn(...)  do {} while (0)
#endif
