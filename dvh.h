#pragma once

#include <stdint.h>
#include <stdio.h>
#include "efs_err.h"
#include "fileslice.h"

#define VDNAMESIZE 8
#define VHMAGIC 0x0be5a941
#define NPARTAB 16
#define NVDIR 15
#define BFNAMESIZE 16
#define NPTYPES 16
#define BLKSIZ 512

enum partition_type_e {
	PT_VOLHDR = 0,
	PT_BSD = 4,
	PT_SYSV = 5,
	PT_VOLUME = 6,
	PT_EFS = 7,
	PT_XFS = 10,
};

struct dvh_dp_s {
	uint8_t  dp_skew;
	uint8_t  dp_gap1;
	uint8_t  dp_gap2;
	uint8_t  dp_spares_cyl;
	uint16_t dp_cyls;
	uint16_t dp_shd0;
	uint16_t dp_trks0;
	uint8_t  dp_ctq_depth;
	uint8_t  dp_cylshi;
	uint16_t dp_unused;
	uint16_t dp_secs;
	uint16_t dp_secbytes;
	uint16_t dp_interleave;
	int32_t  dp_flags;
	int32_t  dp_datarate;
	int32_t  dp_nretries;
	int32_t  dp_mspw;
	uint16_t dp_xgap1;
	uint16_t dp_xsync;
	uint16_t dp_xrdly;
	uint16_t dp_xgap2;
	uint16_t dp_xrgate;
	uint16_t dp_xwcont;
} __attribute__((packed));

struct dvh_vd_s {
	char vd_name[VDNAMESIZE];
	int32_t vd_lbn;
	int32_t vd_nbytes;
} __attribute__((packed));

struct dvh_pt_s {
	int32_t pt_nblks;
	int32_t pt_firstlbn;
	int32_t pt_type;
} __attribute__((packed));

struct dvh_s {
	uint32_t vh_magic;
	uint16_t vh_rootpt;
	uint16_t vh_swappt;
	char     vh_bootfile[BFNAMESIZE];
	struct dvh_dp_s vh_dp;
	struct dvh_vd_s vh_pd[NVDIR];
	struct dvh_pt_s vh_pt[NPARTAB];
	int32_t  vh_csum;
	int32_t  vh_fill;
} __attribute__((packed));

typedef struct dvh_ctx {
	FILE *f;
	struct dvh_s dvh;
} dvh_t;

extern efs_err_t dvh_open(dvh_t **ctx, const char *filename);
extern efs_err_t dvh_close(dvh_t *ctx);
extern fileslice_t *dvh_getParSlice(dvh_t *ctx, int parNum);
extern struct dvh_pt_s dvh_getParInfo(dvh_t *ctx, int parNum);
extern struct dvh_vd_s dvh_getFileInfo(dvh_t *ctx, int fileNum);
extern const char *dvh_getNameForType(unsigned parType);
