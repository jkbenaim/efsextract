#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include "dvh.h"
#include "efserr.h"
#include "endian.h"

int dvh_open(dvh_t **ctx, const char filename)
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
	
	
out_error:
	if (*ctx) free(*ctx);
	*ctx = NULL;
	return erc;
}

struct dvh_vd_s dvh_getFile(struct dvh_s *dvh, int fileNum)
{
	struct dvh_vd_s vd;
	memset(&vd, 0, sizeof(vd));
	
	if (fileNum >= NVDIR) return vd;
	if (fileNum < 0) return vd;
	
	memcpy(vd.vd_name, dvh->vh_pd[fileNum].vd_name, VDNAMESIZE);
	vd.vd_lbn = be32toh(dvh->vh_pd[fileNum].vd_lbn);
	vd.vd_nbytes = be32toh(dvh->vh_pd[fileNum].vd_nbytes);
	return vd;
}

struct dvh_pt_s dvh_getPar(struct dvh_s *dvh, int parNum)
{
	struct dvh_pt_s pt;
	memset(&pt, 0, sizeof(pt));

	if (dvh->vh_magic != be32toh(VHMAGIC))
		return pt;

	if (parNum >= NPARTAB) return pt;
	if (parNum < 0) return pt;

	pt.pt_nblks = be32toh(dvh->vh_pt[parNum].pt_nblks);
	pt.pt_firstlbn = be32toh(dvh->vh_pt[parNum].pt_firstlbn);
	pt.pt_type = be32toh(dvh->vh_pt[parNum].pt_type);
	return pt;
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