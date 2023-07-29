#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "hexdump.h"

void hexdump2(const void *buf, size_t siz, size_t off)
{
	const uint8_t *mybuf = buf;
	const char addrstr[] = "%08zx: ";
	while (siz) {
		unsigned numbytes;
		size_t i;
		numbytes = (siz>16)?16:siz;
		printf(addrstr, off);
		for (i = 0; i < numbytes; i++) {
			if (i & 1) {
				printf("%02x ", mybuf[i]);
			} else {
				printf("%02x", mybuf[i]);
			}
		}
		for (i = numbytes; i < 16; i++) {
			if (i & 1) {
				printf("   ");
			} else {
				printf("  ");
			}
		}
		printf(" ");
		for (i = 0; i < numbytes; i++) {
			putchar(isprint(mybuf[i])?mybuf[i]:'.');
		}
		printf("\n");
		siz -= numbytes;
		mybuf += numbytes;
		off += 16;
	}
}

void hexdump(const void *buf, size_t siz)
{
	hexdump2(buf, siz, 0);
}
