#pragma once

#include <stdio.h>

typedef struct _fileslice_s {
	FILE *f;
	fpos_t base;
	fpos_t cur;
} fileslice_t;

extern fileslice_t *fsopen(FILE *f, size_t base, size_t size);
extern int fsclose(fileslice_t *fs);
extern int fsseek(fileslice_t *fs, long offset, int whence);
extern void fsrewind(fileslice_t *fs);
extern size_t fsread(void *ptr, size_t size, size_t nmemb, fileslice_t *fs);
