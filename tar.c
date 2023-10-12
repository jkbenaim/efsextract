#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "efs.h"
#include "efs_internal.h"
#include "err.h"
#include "tar.h"

FILE *f;

char tar_mode_lookup(uint16_t mode)
{
	switch (mode & IFMT) {
	case IFREG:
		return '0';
	case IFLNK:
		return '2';
	case IFCHR:
		return '3';
	case IFBLK:
		return '4';
	case IFDIR:
		return '5';
	case IFIFO:
		return '6';
	default:
		return '\0';
	}
}

uint32_t tar_getsum(struct tarblk_s blk)
{
	uint32_t sum = 0;
	uint8_t buf[512];
	memcpy(buf, &blk, sizeof(buf));
	for (int i = 0; i < sizeof(buf); i++) {
		sum += buf[i];
	}
	return sum;
}

int tar_create(const char *path)
{
	__label__ out_error;

	f = fopen(path, "w");
	if (!f)
		goto out_error;

	return 0;

out_error:
	if (f)
		fclose(f);
	return -1;
}

int tar_close(void)
{
	fclose(f);
	return 0;
}

int tar_emit(efs_t *efs, const char *filename)
{
	__label__ out_error;
	int rc;
	size_t sz;
	struct tarblk_s blk = {0,};
	struct efs_stat sb = {0,};

	if (strlen(filename) > sizeof(blk.name))
		goto out_error;
	
	strncpy(blk.name, filename, sizeof(blk.name));

	rc = efs_stat(efs, filename, &sb);
	if (rc == -1)
		goto out_error;
	
	rc = snprintf(blk.mode, sizeof(blk.mode), "%07o", sb.st_mode & 0777);

	rc = snprintf(blk.uid, sizeof(blk.uid), "%07o", sb.st_uid);
	rc = snprintf(blk.gid, sizeof(blk.gid), "%07o", sb.st_gid);
	rc = snprintf(blk.size, sizeof(blk.size), "%011o", sb.st_size);
	rc = snprintf(blk.mtime, sizeof(blk.mtime), "%011o", (unsigned int)sb.st_mtimespec.tv_sec);

	memset(blk.sum, ' ', sizeof(blk.sum));	// we'll fix the checksum later

	blk.type = tar_mode_lookup(sb.st_mode);

	if ((sb.st_mode & IFMT) == IFLNK) {
		efs_file_t *src;
		src = efs_fopen(efs, filename);
		if (!src) err(1, "couldn't open efs file '%s'", filename);
		efs_fread(blk.lnk, sb.st_size, 1, src);
		efs_fclose(src);
		// also, set size to zero
		rc = snprintf(blk.size, sizeof(blk.size), "%011o", 0);
	}
	
	memcpy(blk.magic, "ustar", sizeof(blk.magic));

	blk.ver[0] = blk.ver[1] = '0';

	blk.username[0] = '\0';
	blk.groupname[0] = '\0';
	
	if (((sb.st_mode & IFMT) == IFCHR)
	  || ((sb.st_mode & IFMT) == IFBLK)) {
		rc = snprintf(blk.devmajor, sizeof(blk.devmajor), "%07o", sb.st_major);
		rc = snprintf(blk.devminor, sizeof(blk.devminor), "%07o", sb.st_minor);
	}
	
	blk.nameprefix[0] = '\0';

	// calculate checksum
	uint32_t sum = 0;
	sum = tar_getsum(blk);
	rc = snprintf(blk.sum, sizeof(blk.sum), "%07o", sum);

	sz = fwrite(&blk, sizeof(blk), 1, f);
	if (sz != 1)
		err(1, "couldn't write to archive");
	
	if ((sb.st_mode & IFMT) == IFREG) {
		efs_file_t *src;
		uint8_t buf[512] = {0,};
		src = efs_fopen(efs, filename);
		if (!src) err(1, "couldn't open efs file '%s'", filename);
		while ((sz = efs_fread(buf, sizeof(buf), 1, src))) {
			fwrite(buf, sizeof(buf), 1, f);
		}
		
		efs_fclose(src);
	}

	return 0;
out_error:
	return -1;
}
