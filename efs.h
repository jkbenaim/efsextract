#pragma once
#include <stdbool.h>
#include "efs_internal.h"

struct efs_dirent {
	efs_ino_t d_ino;
	char d_name[EFS_MAX_NAME + 1];
};

typedef struct efs_dir {
	/*
	 * readdir() returns a pointer to a dirent.
	 * XPG3 says that the data at the pointer can be overwritten
	 * by successive calls to readdir() on the same directory.
	 * Therefore, define a dirent here, and return a pointer to
	 * it for each readdir() call.
	 */
	struct efs_dirent dirent;
} efs_dir_t;

typedef struct efs_file {
	struct efs_dinode dinode;
	unsigned numextents;
	struct efs_extent *exs;
	efs_t *ctx;
	efs_ino_t ino;
	unsigned pos;
	unsigned nbytes;
	bool eof;
	bool error;
} efs_file_t;

struct efs_stat {
	efs_ino_t st_ino;
	uint16_t st_mode;
	int16_t st_nlink;
	uint16_t st_uid;
	uint16_t st_gid;
};

extern efs_ino_t efs_find_entry(efs_t *efs, const char *name);
extern efs_dir_t *efs_opendir(efs_t *efs, const char *dirname);
extern int efs_closedir(efs_dir_t *dirp);
extern struct efs_dirent *efs_readdir(efs_dir_t *dirp);
extern void efs_rewinddir(efs_dir_t *dirp);

