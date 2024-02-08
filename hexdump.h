#ifndef _HEXDUMP_H
#define _HEXDUMP_H
#include <stdint.h>
#include <stddef.h>
extern void hexdump(const void *buf, size_t siz);
extern void hexdump2(const void *buf, size_t siz, size_t off);
#endif
