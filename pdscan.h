#pragma once
#include "efs.h"
int is_pd(efs_t *efs, const char *path);
void pdprint(efs_t *efs, const char *path);
int pdscan(efs_file_t *f);
