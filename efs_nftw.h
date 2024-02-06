#pragma once
#include "efs_internal.h"

char *mkpath(char *path, char *name);
int efs_nftw(
	efs_t *efs,
	const char *dirpath,
	int (*fn)(const char *fpath, const struct efs_stat *sb)
);
