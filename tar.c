#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cdio/iso9660.h>

#include "efs.h"
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
	int i;
	memcpy(buf, &blk, sizeof(buf));
	for (i = 0; i < sizeof(buf); i++) {
		sum += buf[i];
	}
	return sum;
}

int tar_create(const char *path)
{
	__label__ out_error;

	f = fopen(path, "wb");
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
	/* Pad up to a multiple of 4096 bytes. */
	long pos;
	uint8_t buf[4096];

	pos = ftell(f);
	if (pos & (long)(4096 - 1)) {
		size_t nbytes;
		nbytes = 4096 - (pos & (long)(4096 - 1));
		memset(buf, 0, sizeof(buf));
		fwrite(buf, nbytes, 1, f);
	}

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
	int retval;
	size_t filename_len;
	uint32_t sum;

	if (!filename) {
		retval = -3;
		goto out_error;
	}

	rc = efs_stat(efs, filename, &sb);
	if (rc == -1) {
		/* file not found */
		retval = -2;
		goto out_error;
	}

	filename_len = strlen(filename);
	if ((sb.st_mode & IFMT) == IFDIR) {
		/* For directories, we append a '/' to the filename. */
		filename_len++;
	}

	if (filename_len > (sizeof(blk.name) + sizeof(blk.nameprefix))) {
		/* file name too long for tar format */
		retval = -1;
		goto out_error;
	} else if (filename_len > sizeof(blk.name)) {
		/* long file name */
		char *split = strrchr(filename, '/');
		if (!split)
			errx(1, "while splitting tar filename: '%s'", filename);
		strncpy(blk.nameprefix, filename, split-filename);
		strcpy(blk.name, split + 1);
	} else {
		/* short file name */
		strncpy(blk.name, filename, sizeof(blk.name));
	}

	/*
	 * if we are writing a directory, then append a '/' to the end
	 * of the filename.
	 */
	if ((sb.st_mode & IFMT) == IFDIR) {
		rc = strlen(blk.name);
		blk.name[rc] = '/';
		blk.name[rc + 1] = '\0';
	}

	rc = snprintf(blk.mode, sizeof(blk.mode), "%06o ", sb.st_mode & 0777);
	rc = snprintf(blk.uid, sizeof(blk.uid), "%06o ", sb.st_uid);
	rc = snprintf(blk.gid, sizeof(blk.gid), "%06o ", sb.st_gid);
	if ((sb.st_mode & IFMT) == IFDIR) {
		rc = snprintf(blk.size, sizeof(blk.size), "%011o", 0);
	} else {
		rc = snprintf(blk.size, sizeof(blk.size), "%011o", sb.st_size);
	}
	rc = snprintf(blk.mtime, sizeof(blk.mtime), "%011o", (unsigned int)sb.st_mtimespec.tv_sec);

	/*
	 * actually, we want certain values to be space-terminated,
	 * not null-terminated...
	 */
	blk.size[sizeof(blk.size) - 1] = ' ';
	blk.mtime[sizeof(blk.mtime) - 1] = ' ';

	memset(blk.sum, ' ', sizeof(blk.sum));	/* we'll fix the checksum later */

	blk.type = tar_mode_lookup(sb.st_mode);

	if ((sb.st_mode & IFMT) == IFLNK) {
		efs_file_t *src;
		src = efs_fopen(efs, filename);
		if (!src) err(1, "couldn't open efs file '%s' as link", filename);
		efs_fread(blk.lnk, sb.st_size, 1, src);
		efs_fclose(src);
		/* also, set size to zero */
		rc = snprintf(blk.size, sizeof(blk.size), "%011o", 0);
	}

	memcpy(blk.magic, "ustar", sizeof(blk.magic));

	blk.ver[0] = blk.ver[1] = '0';

#if 0
	strncpy(blk.username, "root", sizeof(blk.username));
	strncpy(blk.groupname, "sys", sizeof(blk.groupname));
#else
	blk.username[0] = '\0';
	blk.groupname[0] = '\0';
#endif

	if (((sb.st_mode & IFMT) == IFCHR)
	  || ((sb.st_mode & IFMT) == IFBLK)) {
		rc = snprintf(blk.devmajor, sizeof(blk.devmajor), "%06o ", sb.st_major);
		rc = snprintf(blk.devminor, sizeof(blk.devminor), "%06o ", sb.st_minor);
	} else {
		memcpy(blk.devmajor, "000000 ", 8);
		memcpy(blk.devminor, "000000 ", 8);
	}

	/* calculate checksum */
	sum = 0;
	sum = tar_getsum(blk);
	rc = snprintf(blk.sum, sizeof(blk.sum), "%06o", sum);

	sz = fwrite(&blk, sizeof(blk), 1, f);
	if (sz != 1)
		err(1, "couldn't write to archive");

	if ((sb.st_mode & IFMT) == IFREG) {
		size_t tailBytes;
		efs_file_t *src;
		const size_t bufsiz = 512;
		size_t blockNum;
		uint8_t *buf;

		buf = calloc(bufsiz, 1);
		if (!buf) err(1, "calloc");
		tailBytes = (size_t)(sb.st_size) % bufsiz;
#if 0
		printf("sb.st_size: %d, tailBytes: %zu\n", sb.st_size, tailBytes);
#endif
		src = efs_fopen(efs, filename);
		if (!src) errx(1, "couldn't open efs file '%s' as regular file", filename);
		for (blockNum = 0; blockNum < (sb.st_size / bufsiz); blockNum++) {
			sz = efs_fread(buf, bufsiz, 1, src);
			if (sz != 1)
				errx(1, "couldn't read from source file '%s'", filename);
			sz = fwrite(buf, bufsiz, 1, f);
			if (sz != 1)
				err(1, "while writing to tar (main blocks)");
		}
		if (tailBytes) {
			sz = efs_fread(buf, tailBytes, 1, src);
			if (sz != 1)
				errx(1, "couldn't read from source file '%s'", filename);
			sz = fwrite(buf, tailBytes, 1, f);
			if (sz != 1)
				err(1, "while writing to tar (tail bytes)");
		}
		/* pad out to a multiple of 512 bytes */
		if (sb.st_size % 512) {
			size_t zeroesToWrite;
			zeroesToWrite = 512 - (sb.st_size % 512);
			memset(buf, 0, zeroesToWrite);
			sz = fwrite(buf, zeroesToWrite, 1, f);
			if (sz != 1)
				errx(1, "while writing to tar (padding)");
		}

		efs_fclose(src);
		free(buf);
	}

	return 0;
out_error:
	return retval;
}

int tar_emit_from_iso9660(iso9660_t *ctx, const char *filename)
{
	__label__ out_error;
	int rc;
	size_t sz;
	struct tarblk_s blk = {0,};
	iso9660_stat_t *st = NULL;
	int retval;
	uint32_t sum;

	if (!filename) {
		retval = -3;
		goto out_error;
	}

	st = iso9660_ifs_stat_translate(ctx, filename);
	if (!st) {
		/* file not found */
		retval = -2;
		goto out_error;
	}

	if (strlen(st->filename) > (sizeof(blk.name) + sizeof(blk.nameprefix))) {
		/* file name too long for tar format */
		retval = -1;
		goto out_error;
	} else if (strlen(filename) > sizeof(blk.name)) {
		/* long file name */
		char *split = strrchr(filename, '/');
		if (!split)
			errx(1, "while splitting tar filename: '%s'", filename);
		strncpy(blk.nameprefix, filename, split-filename);
		strcpy(blk.name, split + 1);
	} else {
		/* short file name */
		strncpy(blk.name, filename, sizeof(blk.name));
	}

	rc = snprintf(blk.mode, sizeof(blk.mode), "%07o", iso9660_get_posix_filemode(st));
	if (rc < 0) errx(1, "in snprintf");
	rc = snprintf(blk.uid, sizeof(blk.uid), "%07o", 0);
	if (rc < 0) errx(1, "in snprintf");
	rc = snprintf(blk.gid, sizeof(blk.gid), "%07o", 0);
	if (rc < 0) errx(1, "in snprintf");
	rc = snprintf(blk.size, sizeof(blk.size), "%011o", st->size);
	if (rc < 0) errx(1, "in snprintf");
	rc = snprintf(blk.mtime, sizeof(blk.mtime), "%011lo", mktime(&st->tm));
	if (rc < 0) errx(1, "in snprintf");

	memset(blk.sum, ' ', sizeof(blk.sum));	/* we'll fix the checksum later */

	switch (st->type) {
	case _STAT_FILE:
		blk.type = TAR_TYPE_REG;
		break;
	case _STAT_DIR:
		blk.type = TAR_TYPE_DIR;
		break;
	default:
		errx(1, "unsupported file type '%d' for file '%s'", st->type, filename);
		break;
	}

	memcpy(blk.magic, "ustar", sizeof(blk.magic));

	blk.ver[0] = blk.ver[1] = '0';

	blk.username[0] = '\0';
	blk.groupname[0] = '\0';

	/* calculate checksum */
	sum = 0;
	sum = tar_getsum(blk);
	rc = snprintf(blk.sum, sizeof(blk.sum), "%07o", sum);
	if (rc < 0) errx(1, "in snprintf");

	sz = fwrite(&blk, sizeof(blk), 1, f);
	if (sz != 1)
		err(1, "couldn't write to archive");

	if ((st->type == _STAT_FILE) && st->size) {
		size_t bufsiz = st->size + ISO_BLOCKSIZE;
		uint8_t *buf = calloc(bufsiz, 1);
		long int z;
		size_t numblks;

		if (!buf) err(1, "calloc");

		numblks = (st->size + ISO_BLOCKSIZE - 1) / ISO_BLOCKSIZE;
		z = iso9660_iso_seek_read(ctx, buf, st->lsn, numblks);
		if (!z) err(1, "couldn't read file from image: '%s'", filename);

		sz = fwrite(buf, st->size, 1, f);
		if (sz != 1)
			err(1, "while writing to tar (main blocks)");

		/* pad out to a multiple of 512 bytes */
		if (st->size % 512) {
			size_t zeroesToWrite;
			zeroesToWrite = 512 - (st->size % 512);
			memset(buf, 0, zeroesToWrite);
			sz = fwrite(buf, zeroesToWrite, 1, f);
			if (sz != 1)
				errx(1, "while writing to tar (padding)");
		}

		free(buf);
	}

	iso9660_stat_free(st);

	return 0;
out_error:
	iso9660_stat_free(st);
	return retval;
}
