#define _GNU_SOURCE
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "dvh.h"
#include "efs.h"
#include "efs_internal.h"
#include "endian.h"
#include "err.h"
#include "progname.h"

#define ARRAY_SIZE(x) (sizeof(x)/sizeof(*x))

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

struct efs_sb efstoh(struct efs_sb efs)
{
	struct efs_sb out = {0,};

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

uint32_t efs_extent_get_bn(struct efs_extent extent)
{
	uint8_t buf[8];
	uint32_t out = 0;

	memcpy(buf, &extent, sizeof(buf));
	out = 256*256*buf[1] + 256*buf[2] + buf[3];
	return out;
}

uint32_t efs_extent_get_offset(struct efs_extent extent)
{
	uint8_t buf[8];
	uint32_t out = 0;

	memcpy(buf, &extent, sizeof(buf));
	out = 256*256*buf[5] + 256*buf[6] + buf[7];
	return out;
}

struct efs_dinode efs_dinodetoh(struct efs_dinode inode)
{
	size_t i;
	struct efs_dinode out = {0,};

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

	switch (out.di_mode & IFMT) {
	case IFCHR:
	case IFBLK:
		out.di_u.di_dev.ndev = be32toh(inode.di_u.di_dev.ndev);
		out.di_u.di_dev.odev = be16toh(inode.di_u.di_dev.odev);
		break;
	default:
		for (i = 0; i < EFS_DIRECTEXTENTS; i++) {
			out.di_u.di_extents[i] = inode.di_u.di_extents[i];
		}
		break;
	}

	return out;
}

efs_err_t efs_get_blocks(efs_t *ctx, void *buf, size_t firstlbn, size_t nblks)
{
	__label__ out_error, out_ok;
	int rc;
	efs_err_t erc;

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
	goto out_ok;

out_ok:
	return EFS_ERR_OK;
out_error:
	return erc;
}

efs_err_t efs_open(efs_t **ctx, fileslice_t *fs)
{
	__label__ out_error;
	efs_err_t erc;
	int rc;

	/* Allocate efs context */
	*ctx = calloc(1, sizeof(efs_t));
	if (!*ctx) {
		erc = EFS_ERR_NOMEM;
		goto out_error;
	}

	(*ctx)->fs = fs;

	/* Read superblock */
	rc = efs_get_blocks(*ctx, &(*ctx)->sb, 1, 1);
	if (rc != EFS_ERR_OK) {
		erc = rc;
		goto out_error;
	}
	
	/* Validate superblock magic */
	if (!IS_EFS_MAGIC(be32toh((*ctx)->sb.fs_magic))) {
		erc = EFS_ERR_SBMAGIC;
		goto out_error;
	}

	/* Convert superblock to native endianness */
	(*ctx)->sb = efstoh((*ctx)->sb);
	
	return EFS_ERR_OK;
out_error:
	if (*ctx) free(*ctx);
	*ctx = NULL;
	return erc;
}

void efs_close(efs_t *ctx)
{
	if (!ctx) return;
	fsclose(ctx->fs);
	free(ctx);
}

struct efs_ino_inf_s {
	size_t bb;
	unsigned slot;
};

size_t itobb(efs_t *ctx, efs_ino_t ino)
{
	return EFS_ITOBB(&ctx->sb, ino);
}

struct efs_ino_inf_s efs_get_inode_info(efs_t *ctx, efs_ino_t ino)
{
	struct efs_ino_inf_s out = {0,};

	out.bb = ctx->sb.fs_firstcg;

	out.bb = itobb(ctx, ino);
	out.slot = ino & EFS_INOPBBMASK;

	return out;
}

struct efs_dinode efs_get_inode(efs_t *ctx, unsigned ino)
{
	struct efs_dinode inodes[4];

	//printf("--> read inode %u\n", ino);


	struct efs_ino_inf_s info;
	info = efs_get_inode_info(ctx, ino);
	efs_get_blocks(ctx, &inodes, info.bb, 1);
	inodes[info.slot] = efs_dinodetoh(inodes[info.slot]);
	//hexdump(&inodes[off], sizeof(*inodes));
	return inodes[info.slot];
}

efs_ino_t efs_find_entry(efs_t *efs, const char *name)
{
	/* TODO */
	return 2;
}

/*
 * Directory functions.
 */
 
int compar(const void *a, const void *b)
{
	struct efs_dirent *de_a, *de_b;
	de_a = (struct efs_dirent *)a;
	de_b = (struct efs_dirent *)b;
	
	return strcmp(de_a->d_name, de_b->d_name);
}

efs_dir_t *efs_opendir(efs_t *efs, const char *dirname)
{
	__label__ out_ok, out_error;
	efs_dir_t *dirp;
	
	dirp = calloc(sizeof(*dirp), 1);
	if (!dirp) goto out_error;
	
	dirp->ino = efs_namei(efs, dirname);
	// printf("--ino: %u\n", dirp->ino);
	if (dirp->ino == -1) goto out_error;
	
	dirp->dirent = _efs_read_dirblks(efs, dirp->ino);
	if (!dirp->dirent)
		goto out_error;
	dirp->_dirent_memobj = dirp->dirent;
	
	size_t nel = 0;
	for (typeof(dirp->dirent) de = dirp->dirent; de->d_ino; de++) {
		nel++;
	}
	
	qsort(dirp->dirent, nel, sizeof(struct efs_dirent), compar);

	goto out_ok;
	
out_ok:
	return dirp;
out_error:
	free(dirp);
	return NULL;
}

int efs_closedir(efs_dir_t *dirp)
{
	free(dirp->_dirent_memobj);
	free(dirp);
	return 0;
}

struct efs_dirent *efs_readdir(efs_dir_t *dirp)
{
	if (dirp->dirent->d_ino)
		return dirp->dirent ++;
	else
		return NULL;
}

void efs_rewinddir(efs_dir_t *dirp)
{
	dirp->dirent = dirp->_dirent_memobj;
}

int efs_stati(efs_t *ctx, efs_ino_t ino, struct efs_stat *statbuf)
{
	struct efs_dinode dinode;
	dinode = efs_get_inode(ctx, ino);
	
	statbuf->st_ino = ino;
	statbuf->st_mode = dinode.di_mode;
	statbuf->st_nlink = dinode.di_nlink;
	statbuf->st_uid = dinode.di_uid;
	statbuf->st_gid = dinode.di_gid;
	statbuf->st_size = dinode.di_size;

	switch (statbuf->st_mode & IFMT) {
	case IFCHR:
	case IFBLK:
		statbuf->st_major = (dinode.di_u.di_dev.odev & 0xff00) >> 8;
		statbuf->st_minor = (dinode.di_u.di_dev.odev & 0x00ff);
		break;
	}
	
	statbuf->st_atimespec.tv_sec = dinode.di_atime;
	statbuf->st_atimespec.tv_nsec = 0;
	
	statbuf->st_mtimespec.tv_sec = dinode.di_mtime;
	statbuf->st_mtimespec.tv_nsec = 0;
	
	statbuf->st_ctimespec.tv_sec = dinode.di_ctime;
	statbuf->st_ctimespec.tv_nsec = 0;
	
	return 0;
}

int efs_stat(efs_t *ctx, const char *pathname, struct efs_stat *statbuf)
{
	efs_ino_t ino;
	ino = efs_namei(ctx, pathname);
	return efs_stati(ctx, ino, statbuf);
}

int efs_fstat(efs_file_t *file, struct efs_stat *statbuf)
{
	return efs_stati(file->ctx, file->ino, statbuf);
}


