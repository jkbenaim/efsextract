#pragma once
#include "efs.h"
extern int is_pd(efs_t *efs, const char *path);
extern void pdprint(efs_t *efs, const char *path);
extern int pdscan(efs_file_t *f);
