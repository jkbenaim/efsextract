#pragma once
#include "efs.h"

struct tarblk_s {
        char name[100];
        char mode[8];
        char uid[8];
        char gid[8];
        char size[12];
        char mtime[12];
        char sum[8];
        char type;
        char lnk[100];
        char magic[6];  // ustar\0
        char ver[2];    // "00"
        char username[32];
        char groupname[32];
        char devmajor[8];
        char devminor[8];
        char nameprefix[155];
	char __pad[12];	// 512 bytes total
} __attribute__((packed));

enum tar_type_e {
        TAR_TYPE_REG = 0,
        TAR_TYPE_LINK = 1,
        TAR_TYPE_SYM = 2,
        TAR_TYPE_CHAR = 3,
        TAR_TYPE_BLOCK = 4,
        TAR_TYPE_DIR = 5,
        TAR_TYPE_FIFO = 6,
};

extern int tar_create(const char *path);
extern int tar_close(void);
extern int tar_emit(efs_t *efs, const char *filename);
