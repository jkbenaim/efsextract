#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include "dvh.h"
#include "endian.h"

struct dvh_pt_s dvh_getPar(struct dvh_s *dvh, int parNum)
{
	struct dvh_pt_s pt;
	memset(&pt, 0, sizeof(pt));

	if (dvh->vh_magic != be32toh(VHMAGIC))
		return pt;

	if (parNum >= NPARTAB) return pt;

	pt.pt_nblks = be32toh(dvh->vh_pt[parNum].pt_nblks);
	pt.pt_firstlbn = be32toh(dvh->vh_pt[parNum].pt_firstlbn);
	pt.pt_type = be32toh(dvh->vh_pt[parNum].pt_type);
	return pt;
}
