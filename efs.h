#pragma once
#include "efs_internal.h"

struct efs_dirent_s {
	efs_ino_t d_ino;
	char d_name[EFS_MAX_NAME + 1];
};

typedef struct _efs_dir_s {
	/*
	 * readdir() returns a pointer to a dirent.
	 * XPG3 says that the data at the pointer can be overwritten
	 * by successive calls to readdir() on the same directory.
	 * Therefore, define a dirent here, and return a pointer to
	 * it for each readdir() call.
	 */
	struct efs_dirent_s dirent;
} efs_dir_t;

typedef struct _efs_file_s {
} efs_file_t;

extern efs_dir_t *efs_opendir(efs_t *efs, const char *dirname);
extern int efs_closedir(efs_dir_t *dirp);
extern struct efs_dirent_s *efs_readdir(efs_dir_t *dirp);
extern void efs_rewinddir(efs_dir_t *dirp);

