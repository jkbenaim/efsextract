#define _GNU_SOURCE
#include <endian.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "dvh.h"
#include "efs.h"

extern char *__progname;

/*
struct efs_s {
	int32_t fs_size;
	int32_t fs_firstcg;
	int32_t fs_cgfsize;
	int16_t fs_cgisize;
	int16_t fs_sectors;
	int16_t fs_heads;
	int16_t fs_ncg;
	int16_t fs_dirty;
	int16_t __pad;
	int32_t fs_time;
	int32_t fs_magic;
	char fs_fname[6];
	char fs_fpack[6];
	int32_t fs_bmsize;
	int32_t fs_tfree;
	int32_t fs_tinode;
	int32_t fs_bmblock;
	int32_t fs_replsb;
	int32_t fs_lastialloc;
	char fs_spare[20];
	int32_t fs_checksum;

} __attribute__((packed));
*/

struct efs_sb_s efstoh (struct efs_sb_s efs)
{
	struct efs_sb_s out = {0,};

	out.fs_size = be32toh(efs.fs_size);
	out.fs_firstcg = be32toh(efs.fs_firstcg);
	out.fs_cgfsize = be32toh(efs.fs_cgfsize);
	out.fs_cgisize = be16toh(efs.fs_cgisize);
	out.fs_sectors = be16toh(efs.fs_sectors);
	out.fs_heads = be16toh(efs.fs_heads);
	out.fs_ncg = be16toh(efs.fs_ncg);
	out.fs_dirty = be16toh(efs.fs_dirty);
	out.__pad = be16toh(efs.__pad);
	out.fs_time = be32toh(efs.fs_time);
	out.fs_magic = be32toh(efs.fs_magic);
	memcpy(&out.fs_fname, &efs.fs_fname, sizeof(efs.fs_fname));
	memcpy(&out.fs_fpack, &efs.fs_fpack, sizeof(efs.fs_fpack));
	out.fs_bmsize = be32toh(efs.fs_bmsize);
	out.fs_tfree = be32toh(efs.fs_tfree);
	out.fs_tinode = be32toh(efs.fs_tinode);
	out.fs_bmblock = be32toh(efs.fs_bmblock);
	out.fs_replsb = be32toh(efs.fs_replsb);
	out.fs_lastialloc = be32toh(efs.fs_lastialloc);
	memcpy(&out.fs_spare, &efs.fs_spare, sizeof(efs.fs_spare));
	out.fs_checksum = be32toh(efs.fs_checksum);

	return out;
}

/*
struct efs_extent_s {
	uint32_t ex_magic:8,
		 ex_bn:24,
		 ex_length:8,
		 ex_offset:24;
} __attribute__((packed));

struct efs_edevs_s {
	uint16_t odev;
	uint32_t ndev;
} __attribute__((packed));

struct efs_dinode_s {
	uint16_t di_mode;
	int16_t  di_nlink;
	uint16_t di_uid;
	uint16_t di_gid;
	int32_t  di_size;
	int32_t  di_atime;
	int32_t  di_mtime;
	int32_t  di_ctime;
	uint32_t di_gen;
	int16_t  di_numextents;
	uint8_t  di_version;
	uint8_t  di_spare;
	union di_addr {
		struct efs_extent_s di_extents[EFS_DIRECTEXTENTS];
		struct efs_edevs_s  di_dev;
	} di_u;
};
*/
/*
struct efs_extent_s {
	uint32_t ex_magic:8,
		 ex_bn:24,
		 ex_length:8,
		 ex_offset:24;
} __attribute__((packed));
*/

uint32_t be24toh(uint32_t x)
{
#if __BYTE_ORDER == __LITTLE_ENDIAN
	return be32toh(x<<8);
#else
	return x & 0x00ffffff;
#endif
}


struct efs_dinode_s efs_dinodetoh(struct efs_dinode_s inode)
{
	size_t i;
	struct efs_dinode_s out = {0,};

	out.di_mode = be16toh(inode.di_mode);
	out.di_nlink = be16toh(inode.di_nlink);
	out.di_uid = be16toh(inode.di_uid);
	out.di_gid = be16toh(inode.di_gid);
	out.di_size = be32toh(inode.di_size);
	out.di_atime = be32toh(inode.di_atime);
	out.di_mtime = be32toh(inode.di_mtime);
	out.di_ctime = be32toh(inode.di_ctime);
	out.di_gen = be32toh(inode.di_gen);
	out.di_numextents = be16toh(inode.di_numextents);
	out.di_version = inode.di_version;
	out.di_spare = inode.di_spare;

	//printf("numextents: %u\n", out.di_numextents);

	if (out.di_numextents > EFS_DIRECTEXTENTS) return out;

	/* TODO */
	for (i = 0; i < out.di_numextents; i++) {
		out.di_u.di_extents[i].ex_magic = inode.di_u.di_extents[i].ex_magic;
		out.di_u.di_extents[i].ex_bn = be24toh(inode.di_u.di_extents[i].ex_bn);
		out.di_u.di_extents[i].ex_length = inode.di_u.di_extents[i].ex_length;
		out.di_u.di_extents[i].ex_offset = be24toh(inode.di_u.di_extents[i].ex_offset);
	}

	return out;
}

struct efs_extent_s efs_extenttoh(struct efs_extent_s extent)
{
	struct efs_extent_s out;
	out.ex_magic = extent.ex_magic;
	out.ex_bn = be24toh(extent.ex_bn);
	out.ex_length = extent.ex_length;
	out.ex_offset = be24toh(extent.ex_offset);
	return out;
}

const char *efs_strerror(efs_err_t e)
{
	switch (e) {
	case EFS_ERR_OK:
		return "no error";
	case EFS_ERR_INVAL:
		return "invalid argument";
	case EFS_ERR_NOENT:
		return "no such file or directory";
	case EFS_ERR_NOMEM:
		return "out of memory";
	case EFS_ERR_READFAIL:
		return "read error";
	case EFS_ERR_NOPAR:
		return "requested partition not found";
	default:
		return "";
	}
}

void vwarnefs(efs_err_t e, const char *fmt, va_list args)
{
	fprintf(stderr, "%s: ", __progname);
	if (fmt) {
		vfprintf(stderr, fmt, args);
		fprintf(stderr, ": ");
	}
	fprintf(stderr, "%s\n", efs_strerror(e));
}

noreturn void verrefs(int eval, efs_err_t e, const char *fmt, va_list args)
{
	vwarnefs(e, fmt, args);
	exit(eval);
}

void warnefs(efs_err_t e, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vwarnefs(e, fmt, ap);
	va_end(ap);
}

noreturn void errefs(int eval, efs_err_t e, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	verrefs(eval, e, fmt, ap);
	va_end(ap);
}

fileslice_t *efs_get_slice_for_par(efs_ctx_t *ctx, int parNum)
{
	__label__ out_error;
	int rc;
	fileslice_t *fs;
	struct dvh_s dvh;
	struct dvh_pt_s pt;

	rc = fseek(ctx->f, 0, SEEK_SET);
	if (rc == -1) goto out_error;

	rc = fread(&dvh, sizeof(dvh), 1, ctx->f);
	if (rc != 1) goto out_error;

	pt = dvh_getPar(&dvh, parNum);
	if (pt.pt_nblks == 0) goto out_error;
	//printf("pt.pt_nblks = %u\n", pt.pt_nblks);
	//printf("pt.pt_firstlbn = %u\n", pt.pt_firstlbn);

	fs = fsopen(ctx->f, BLKSIZ * pt.pt_firstlbn, BLKSIZ * pt.pt_nblks);
	return fs;

out_error:
	return NULL;
}

efs_err_t efs_get_blocks(efs_ctx_t *ctx, void *buf, size_t firstlbn, size_t nblks)
{
	__label__ out_error;
	int rc;
	efs_err_t erc;

	//printf("getting blocks %lu:%lu\n", firstlbn, nblks);

	rc = fsseek(ctx->fs, BLKSIZ * firstlbn, SEEK_SET);
	if (rc == -1) {
		erc = EFS_ERR_READFAIL;
		goto out_error;
	}

	//printf("fsread(%p, %u, %lu, %p);\n", buf, BLKSIZ, nblks, ctx->fs);
	rc = fsread(buf, BLKSIZ, nblks, ctx->fs);
	if (rc != nblks) {
		erc = EFS_ERR_READFAIL;
		goto out_error;
	}

	return EFS_ERR_OK;
out_error:
	return erc;
}

efs_err_t efs_open(efs_ctx_t **ctx, char *filename)
{
	__label__ out_error;
	efs_err_t erc;
	int rc;
	struct dvh_s dvh;

	*ctx = calloc(1, sizeof(efs_ctx_t));
	if (!*ctx) {
		erc = EFS_ERR_NOMEM;
		goto out_error;
	}
	(*ctx)->err = EFS_ERR_OK;

	(*ctx)->f = fopen(filename, "r");
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

	/* Open par7 slice */
	(*ctx)->fs = efs_get_slice_for_par(*ctx, 7);
	if (!((*ctx)->fs)) {
		erc = EFS_ERR_NOPAR;
		goto out_error;
	}

	/* Read superblock */
	rc = efs_get_blocks(*ctx, &(*ctx)->sb, 1, 1);
	if (rc != EFS_ERR_OK) {
		erc = rc;
		goto out_error;
	}
	(*ctx)->sb = efstoh((*ctx)->sb);

	return (*ctx)->err = EFS_ERR_OK;
out_error:
	if (*ctx) free(*ctx);
	*ctx = NULL;
	return erc;
}

void efs_close(efs_ctx_t *ctx)
{
	if (!ctx) return;
	fsclose(ctx->fs);
	fclose(ctx->f);
	free(ctx);
}

struct efs_dinode_s efs_get_inode(efs_ctx_t *ctx, unsigned ino)
{
	unsigned bb;
	unsigned off;
	struct efs_dinode_s inodes[4];

	//printf("--> read inode %u\n", ino);

	bb = EFS_ITOBB(&ctx->sb, ino);
	off = EFS_ITOO(&ctx->sb, ino);
	efs_get_blocks(ctx, &inodes, bb, 1);
	inodes[off] = efs_dinodetoh(inodes[off]);
	//hexdump(&inodes[off], sizeof(*inodes));
	return inodes[off];
}
