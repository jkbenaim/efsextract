#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include "dvh.h"
#include "efs_err.h"
#include "endian.h"

#define ARRAY_SIZE(x) (sizeof(x)/sizeof(*x))

void _dvh_ntoh(struct dvh_s *dvh);

efs_err_t dvh_open(dvh_t **ctx, const char *filename)
{
	__label__ out_error;
	int rc;
	efs_err_t erc;
	struct dvh_s dvh;
	
	/* Allocate dvh context */
	*ctx = calloc(1, sizeof(dvh_t));
	if (!*ctx) {
		erc = EFS_ERR_NOMEM;
		goto out_error;
	}
	
	/* Open file */
	(*ctx)->f = fopen(filename, "rb");
	if (!(*ctx)->f) {
		erc = EFS_ERR_NOENT;
		goto out_error;
	}
	
	/* Read volume header */
	rc = fread(&dvh, sizeof(dvh), 1, (*ctx)->f);
	if (rc != 1) {
		erc = EFS_ERR_READFAIL;
		goto out_error;
	}
	
	/* Validate volume header magic */
	if (be32toh(dvh.vh_magic) != VHMAGIC) {
		erc = EFS_ERR_NOVH;
		goto out_error;
	}

	/* Validate volume header checksum */
	uint32_t sum = 0;
	uint32_t words[128];
	memcpy(&words, &dvh, sizeof(words));
	for (size_t i = 0; i < ARRAY_SIZE(words); i++) {
		sum += be32toh(words[i]);
	}
	if (sum != 0) {
		erc = EFS_ERR_BADVH;
		goto out_error;
	}

	/* Swap dvh to native endiannes */
	_dvh_ntoh(&dvh);

	/* Store dvh in context */
	(*ctx)->dvh = dvh;
	
	return EFS_ERR_OK;
	
out_error:
	if (*ctx) free(*ctx);
	*ctx = NULL;
	return erc;
}

efs_err_t dvh_close(dvh_t *ctx)
{
	if (ctx) {
		fclose(ctx->f);
		free(ctx);
	}
	
	return EFS_ERR_OK;
}

void _dvh_ntoh(struct dvh_s *dvh)
{
	dvh->vh_magic = be32toh(dvh->vh_magic);
	dvh->vh_rootpt = be16toh(dvh->vh_rootpt);
	dvh->vh_swappt = be16toh(dvh->vh_swappt);
	dvh->vh_csum = be16toh(dvh->vh_csum);
	dvh->vh_fill = be16toh(dvh->vh_fill);

	/* vh_dp */
	dvh->vh_dp.dp_cyls = be16toh(dvh->vh_dp.dp_cyls);
	dvh->vh_dp.dp_shd0 = be16toh(dvh->vh_dp.dp_shd0);
	dvh->vh_dp.dp_trks0 = be16toh(dvh->vh_dp.dp_trks0);
	dvh->vh_dp.dp_unused = be16toh(dvh->vh_dp.dp_unused);
	dvh->vh_dp.dp_secs = be16toh(dvh->vh_dp.dp_secs);
	dvh->vh_dp.dp_secbytes = be16toh(dvh->vh_dp.dp_secbytes);
	dvh->vh_dp.dp_interleave = be16toh(dvh->vh_dp.dp_interleave);
	dvh->vh_dp.dp_xgap1 = be16toh(dvh->vh_dp.dp_xgap1);
	dvh->vh_dp.dp_xsync = be16toh(dvh->vh_dp.dp_xsync);
	dvh->vh_dp.dp_xrdly = be16toh(dvh->vh_dp.dp_xrdly);
	dvh->vh_dp.dp_xgap2 = be16toh(dvh->vh_dp.dp_xgap2);
	dvh->vh_dp.dp_xrgate = be16toh(dvh->vh_dp.dp_xrgate);
	dvh->vh_dp.dp_xwcont = be16toh(dvh->vh_dp.dp_xwcont);

	/* vh_pd */
	for (unsigned i = 0; i < NVDIR; i++) {
		dvh->vh_pd[i].vd_lbn = be32toh(dvh->vh_pd[i].vd_lbn);
		dvh->vh_pd[i].vd_nbytes = be32toh(dvh->vh_pd[i].vd_nbytes);
	}

	/* vh_pt */
	for (unsigned i = 0; i < NPARTAB; i++) {
		dvh->vh_pt[i].pt_nblks = be32toh(dvh->vh_pt[i].pt_nblks);
		dvh->vh_pt[i].pt_firstlbn = be32toh(dvh->vh_pt[i].pt_firstlbn);
		dvh->vh_pt[i].pt_type = be32toh(dvh->vh_pt[i].pt_type);
	}
}

fileslice_t *dvh_getParSlice(dvh_t *ctx, int parNum)
{
	__label__ out_error;
	struct dvh_pt_s pt;
	fileslice_t *fs = NULL;
	
	pt = dvh_getParInfo(ctx, parNum);
	if (pt.pt_nblks == 0)
		goto out_error;
	
	fs = fsopen(ctx->f, BLKSIZ * pt.pt_firstlbn, BLKSIZ * pt.pt_nblks);
	return fs;

out_error:
	free(fs);
	return NULL;
}

struct dvh_pt_s dvh_getParInfo(dvh_t *ctx, int parNum)
{
	struct dvh_pt_s pt;
	memset(&pt, 0, sizeof(pt));

	if (ctx->dvh.vh_magic != VHMAGIC)
		return pt;

	if (parNum >= NPARTAB) return pt;
	if (parNum < 0) return pt;

	return ctx->dvh.vh_pt[parNum];
}

struct dvh_vd_s dvh_getFileInfo(dvh_t *ctx, int fileNum)
{
	struct dvh_vd_s vd;
	memset(&vd, 0, sizeof(vd));
	
	if (fileNum >= NVDIR) return vd;
	if (fileNum < 0) return vd;

	return ctx->dvh.vh_pd[fileNum];
}

const char *dvh_getNameForType(unsigned parType)
{
	switch (parType) {
	case PT_VOLHDR:
		return "volhdr";
		break;
	case PT_BSD:
		return "bsd";
		break;
	case PT_SYSV:
		return "sysv";
		break;
	case PT_VOLUME:
		return "volume";
		break;
	case PT_EFS:
		return "efs";
		break;
	case PT_XFS:
		return "xfs";
		break;
	default:
		return NULL;
		break;
	}
}
