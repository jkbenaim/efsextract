#define _GNU_SOURCE
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "dvh.h"
#include "efs.h"
#include "endian.h"
#include "err.h"
#include "progname.h"
#include "queue.h"

#define ARRAY_SIZE(x) (sizeof(x)/sizeof(*x))


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
#if 0
	printf("efs_get_blocks(%p, %p, %zu, %zu);\n", ctx, buf, firstlbn, nblks);
#endif

	rc = fsseek(ctx->fs, BLKSIZ * firstlbn, SEEK_SET);
	if (rc == -1) {
		erc = EFS_ERR_READFAIL;
		goto out_error;
	}

#if 0
	printf("fsread(%p, %u, %lu, %p);\n", buf, BLKSIZ, nblks, ctx->fs);
#endif
	rc = fsread(buf, BLKSIZ, nblks, ctx->fs);
#if 0
	printf("fsread: returning %d\n", rc);
	//hexdump(buf, BLKSIZ * nblks);
#endif
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
	if (ino == -1) return -1;
	return efs_stati(ctx, ino, statbuf);
}

int efs_fstat(efs_file_t *file, struct efs_stat *statbuf)
{
	return efs_stati(file->ctx, file->ino, statbuf);
}

char *mkpath(char *path, char *name)
{
	int rc;
	char *out;

	if (!path || !name)
		return NULL;

	if (strlen(path) == 0) {
		out = strdup(name);
	} else {
		size_t stringsize;
		stringsize = strlen(path) + strlen("/") + strlen(name) + 1;
		out = malloc(stringsize);
		if (!out)
			err(1, "in malloc");

		rc = snprintf(out, stringsize, "%s/%s", path, name);
		if (rc >= stringsize)
			errx(1, "in snprintf");
		out[stringsize - 1] = '\0';
	}
	return out;
}

int efs_nftw(
	efs_t *efs,
	const char *dirpath,
	int (*fn)(const char *fpath, const struct efs_stat *sb)
) {
	__label__ out_error;
	int rc;
	queue_t q;
	struct qent_s *qe;

	q = queue_init();
	if (!q)
		return -1;
	queue_add_head(q, strdup(dirpath));

	while ((qe = queue_dequeue(q))) {
		efs_dir_t *dirp;
		queue_t dirq;
		struct efs_dirent *de;

		dirp = efs_opendir(efs, qe->path);
		if (!dirp)
			errx(1, "couldn't open directory: '%s'", qe->path);

		dirq = queue_init();
		if (!dirq)
			err(1, "in queue_init");

		while ((de = efs_readdir(dirp))) {
			__label__ nextfile;
			struct efs_stat sb;
			char *path;

			rc = efs_stati(efs, de->d_ino, &sb);
			if (rc == -1)
				err(1, "couldn't get stat for '%s'", de->d_name);
			path = mkpath(qe->path, de->d_name);
			if (!path)
				goto nextfile;
			if ((sb.st_mode & IFMT) == IFDIR) {
				if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) {
					goto nextfile;
				}
				queue_add_head(dirq, strdup(path));
			}

			if (fn) {
				rc = fn(path, &sb);
				if (rc != 0) {
					// TODO: stop walk
				}
			} else {
				printf("%s\n", path);
			}
nextfile:
			free(path);
		}
		efs_closedir(dirp);
		free(qe->path);
		free(qe);
		queue_add_queue_head(q, dirq);
	}
	queue_free(q);


	return 0;
out_error:
	return -1;
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
		case EFS_ERR_NOVH:
			return "volume header not found";
		case EFS_ERR_BADVH:
			return "volume header checksum failure";
		case EFS_ERR_SBMAGIC:
			return "superblock not found";
		case EFS_ERR_PARTYPE:
			return "unrecognized partition type";
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

void verrefs(int eval, efs_err_t e, const char *fmt, va_list args)
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

void errefs(int eval, efs_err_t e, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	verrefs(eval, e, fmt, ap);
	va_end(ap);
}

#define MAX(a,b) (a>b?a:b)
#define MIN(a,b) (a>b?b:a)

const char *_getfirstpathpart(const char *name);
struct efs_extent *_efs_get_extents(efs_t *ctx, struct efs_dinode *dinode);
struct efs_extent *_efs_find_extent(struct efs_extent *exs, unsigned numextents, size_t pos);
efs_ino_t _efs_nameiat(efs_t *ctx, efs_ino_t ino, const char *name);
efs_file_t *_efs_file_openi(efs_t *ctx, efs_ino_t ino);

struct efs_extent *_efs_get_extents(efs_t *ctx, struct efs_dinode *dinode)
{
	__label__ out_error, out_ok;
	efs_err_t erc;
	struct efs_extent *out = NULL;
	unsigned numextents = dinode->di_numextents;
	//printf("numextents:  %4x\n", numextents);

	out = calloc(numextents, sizeof(struct efs_extent));
	if (!out)
		goto out_error;

	if (numextents == 0)
		goto out_ok;

	if (numextents <= EFS_DIRECTEXTENTS) {
		/* direct extents */
		for (unsigned i = 0; i < numextents; i++) {
			out[i] = dinode->di_u.di_extents[i];
		}
	} else {
		/* indirect extents */
		void *buf = NULL;
		unsigned numindirect;
		unsigned nobbs;
		numindirect = efs_extent_get_offset(dinode->di_u.di_extents[0]);
		//printf("numindirect: %4x\n", numindirect);

		if (numindirect > EFS_DIRECTEXTENTS)
			errx(1, "invalid number of indirect extents (fs corrupt)");

		/* validate number of BBs is at or under limit */
		nobbs = 0;
		for (unsigned i = 0; i < numindirect; i++) {
			struct efs_extent ex;
			ex = dinode->di_u.di_extents[i];
			nobbs += ex.ex_length;
		}
		if (nobbs > EFS_MAXINDIRBBS) {
			goto out_error;
		}
		buf = calloc(nobbs, BLKSIZ);
		if (!buf) err(1, "in calloc");

		unsigned bbcursor = 0;

		for (unsigned i = 0; i < numindirect; i++) {
			struct efs_extent ex;
			ex = dinode->di_u.di_extents[i];
			//printf("bbcursor:    %4x (%d)\n", bbcursor, i);
			erc = efs_get_blocks(
				ctx,
				buf + (BLKSIZ * bbcursor),
				efs_extent_get_bn(ex),
				ex.ex_length
			);
			if (erc != EFS_ERR_OK)
				errefs(1, erc, "while fetching indir extent");
			//hexdump(buf, BLKSIZ * nobbs);
			bbcursor += ex.ex_length;
		}
		for (unsigned i = 0; i < numextents; i++) {
			struct efs_extent *exs;
			exs = (struct efs_extent *)buf;
			out[i] = exs[i];
		}
		free(buf);
	}

#if 0
	/* sort extents */
	int a(const struct efs_extent *a, const struct efs_extent *b) {
		unsigned ao = efs_extent_get_offset(*a);
		unsigned bo = efs_extent_get_offset(*b);
		if (ao < bo)
			return -1;
		if (ao > bo)
			return 1;
		return 0;
	}
	qsort(out, numextents, sizeof(*out), a);
#endif

#if 0
	/* print extent table */
	for (size_t i = 0; i < numextents; i++) {
		uint32_t offset, bn;

		offset = efs_extent_get_offset(out[i]);
		bn = efs_extent_get_bn(out[i]);

		printf("%4zx %2x  %6x  %2x  %6x\n",
			i,
			out[i].ex_magic,
			bn,
			out[i].ex_length,
			offset
		);
	}
#endif
#if 1
	/* assert that extents are in ascending order */
	unsigned last = efs_extent_get_offset(out[0]);
	for (size_t i = 1; i < numextents; i++) {
		unsigned suspect;
		suspect = efs_extent_get_offset(out[i]);
		if (last >= suspect) {
			errx(1, "unsorted extents (%zu: %x >= %x)", i, last, suspect);
		}
		last = suspect;
	}

#endif

	goto out_ok;

out_ok:
	return out;
out_error:
	free(out);
	out = NULL;
	return NULL;
}

struct efs_extent *_efs_find_extent(struct efs_extent *exs, unsigned numextents, size_t pos)
{
	for (unsigned i = 0; i < numextents; i++) {
		struct efs_extent *ex;
		ex = &exs[i];
		/* too low? */
		if ((efs_extent_get_offset(*ex) * BLKSIZ) > pos)
			continue;
		/* too high? */
		if (((efs_extent_get_offset(*ex) + ex->ex_length) * BLKSIZ) <= pos)
			continue;
		return ex;
	}
	return NULL;
}

unsigned _efs_nbytes_this_extent(
	struct efs_extent *ex,
	unsigned pos,
	unsigned nbytes
) {
	__label__ out_error;
	size_t start, end;
	start = BLKSIZ * efs_extent_get_offset(*ex);
	end = BLKSIZ * (efs_extent_get_offset(*ex) + ex->ex_length);

	/* assert position is within extent */
	if (pos < start)
		goto out_error;
	if (pos > end)
		goto out_error;

	/* move pos to be relative to start of extent */
	pos -= start;
	unsigned bytes_left_in_extent;
	bytes_left_in_extent = end - start - pos;
	return MIN(bytes_left_in_extent, nbytes);
out_error:
	return 0;
}

unsigned _efs_nbytes_firstbn(
	struct efs_extent *ex,
	unsigned pos
) {
	typeof(pos) blkOff, newpos;

	newpos = pos - (BLKSIZ * efs_extent_get_offset(*ex));
	blkOff = newpos / BLKSIZ;
	return blkOff;
}

size_t efs_fread_blocks(
	void *ptr,
	size_t lbn,
	size_t numblocks,
	efs_file_t *file
) {
	efs_err_t erc;
	struct efs_extent *ex;

	if (!ptr) {
		return 0;
	}

#if 0
	printf("efs_fread_blocks: ptr: %p, lbn: %zu, numblocks: %zu, file: %p\n",
		ptr,
		lbn,
		numblocks,
		file
	);
#endif
	if ((numblocks==1) && (file->blocknum == lbn)) {
		memcpy(ptr, file->blockbuf, BLKSIZ);
		return 1;
	}
	unsigned done = 0;
	while (done < numblocks) {
		unsigned offset_in_extent;
		unsigned blocks_this_extent;

		ex = _efs_find_extent(file->exs, file->numextents, lbn * BLKSIZ);
		if (!ex) errx(1, "in efs_fread_blocks");
		offset_in_extent = lbn - efs_extent_get_offset(*ex);
		blocks_this_extent = MIN(numblocks, ex->ex_length - offset_in_extent);
#if 0
		printf("blocks_this_extent: %u\n", blocks_this_extent);
#endif

#if 0
		printf("before: %p %u\n", ptr, blocks_this_extent);
		hexdump(ptr, blocks_this_extent * BLKSIZ);
#endif

		erc = efs_get_blocks(file->ctx, ptr, efs_extent_get_bn(*ex) + offset_in_extent, blocks_this_extent);
		if (erc) errefs(1, erc, "in efs_fread_blocks");
#if 0
		printf("after:\n");
		hexdump(ptr, blocks_this_extent * BLKSIZ);
#endif
		ptr += blocks_this_extent * BLKSIZ;
		done += blocks_this_extent;
	}
	if (numblocks == 1) {
		file->blocknum = lbn;
		memcpy(file->blockbuf, ptr - BLKSIZ, BLKSIZ);
	}

#if 0
	printf("efs_fread_blocks returning: %zu\n", numblocks);
#endif
	return numblocks;
}

size_t _efs_fread_aux(
	void *ptr,
	size_t size,
	efs_file_t *file
) {
#if 0
	printf("_efs_fread_aux: ptr: %16p, size: %8zx, file: %p\n",
		ptr,
		size,
		file
	);
#endif
	if (!size) return 0;

	/* start with a partial block? */
	if (file->pos % BLKSIZ) {
		unsigned start, len, blknum;
		size_t rc;
		//uint8_t buf[BLKSIZ];
		uint8_t *buf = calloc(BLKSIZ, 1);

		start = file->pos % BLKSIZ;
		len = MIN(size, (BLKSIZ - start));
		blknum = file->pos / BLKSIZ;

		rc = efs_fread_blocks(buf, blknum, 1, file);
		if (rc != 1) return 0;

		memcpy(ptr, &buf[start], len);
		file->pos += len;
		ptr += len;
		size -= len;
		free(buf);
		buf = NULL;
	}

	/* whole blocks? */
	if (!(file->pos % BLKSIZ) && (size >= BLKSIZ)) {
		unsigned startblk, nblks;
		size_t rc;

		startblk = file->pos / BLKSIZ;
		nblks = size / BLKSIZ;
#if 0
		printf("file->pos: %u, size: %lu\n", file->pos, size);
		printf("nblks: %u, startblk: %u\n", nblks, startblk);
#endif
		rc = efs_fread_blocks(ptr, startblk, nblks, file);
		if (rc != nblks) return 0;

		file->pos += nblks * BLKSIZ;
		ptr += nblks * BLKSIZ;
		size -= nblks * BLKSIZ;
	}

	/* end with a partial block? */
	if (!(file->pos % BLKSIZ) && size > 0 && size < BLKSIZ) {
		size_t rc;
		//uint8_t buf[BLKSIZ];
		uint8_t *buf = calloc(BLKSIZ, 1);
		unsigned blknum;

		blknum = file->pos / BLKSIZ;

		rc = efs_fread_blocks(buf, blknum, 1, file);
		if (rc != 1) return 0;

		memcpy(ptr, buf, size);
		file->pos += size;
		ptr += size;
		free(buf);
		buf = NULL;
	}

	return 1;
}

size_t efs_fread(
	void *ptr,
	size_t size,
	size_t nmemb,
	efs_file_t *file
) {
	size_t out = 0;

#if 0
	if (size > 0xc080c1a2bf5e9032ULL)
	printf("efs_fread: ptr: %16p, size: %8zx, nmemb: %8zx, file: %p\n",
		ptr,
		size,
		nmemb,
		file
	);
#endif

	for (int i = 0; i < nmemb; i++) {
		size_t rc;
		rc = _efs_fread_aux(ptr, size, file);
		ptr += size;
		if (rc == 1) out++;
		else break;
	}

#if 0
	printf("efs_fread: returning %zu\n", out);
#endif

	return out;
}

efs_file_t *efs_fopenat(
	efs_t *ctx,
	efs_dir_t *dirp,
	const char *path
) {
	__label__ out_ok, out_error;

	efs_ino_t ino;
	efs_file_t *out = NULL;

	ino = _efs_nameiat(ctx, dirp->ino, path);
	if (ino == (efs_ino_t)(-1)) {
		errno = ENOENT;
		goto out_error;
	}

	out = _efs_file_openi(ctx, ino);
	if (!out) goto out_error;

	goto out_ok;

out_ok:
	errno = 0;
	return out;
out_error:
	free(out);
	return NULL;
}

efs_file_t *efs_fopen(
	efs_t *ctx,
	const char *path
) {
	efs_dir_t dir;
	dir.ino = EFS_BLK_ROOTINO;
	return efs_fopenat(ctx, &dir, path);
}

efs_file_t *_efs_file_openi(efs_t *ctx, efs_ino_t ino)
{
	__label__ out_error, out_ok;
	efs_file_t *out = NULL;
	if (!ctx) goto out_error;

	out = calloc(sizeof(efs_file_t), 1);
	if (!out) goto out_error;

	out->ctx = ctx;
	out->ino = ino;
	out->pos = 0;
	out->eof = false;
	out->error = false;
	out->dinode = efs_get_inode(ctx, ino);
	out->nbytes = out->dinode.di_size;
	out->blocknum = -1;

	/* validate inode */
	if (out->dinode.di_version != 0)
		goto out_error;
	if (out->dinode.di_nlink == 0)
		goto out_error;

	switch (out->dinode.di_mode & IFMT) {
	case IFREG:
	case IFDIR:
	case IFLNK:
		break;
	default:
		goto out_error;
	}

	out->numextents = out->dinode.di_numextents;
	out->exs = _efs_get_extents(ctx, &(out->dinode));
	if (!out->exs)
		goto out_error;

	goto out_ok;

out_ok:
	return out;
out_error:
	free(out);
	return NULL;
}

int efs_fclose(efs_file_t *file)
{
	free(file->exs);
	free(file);
	return 0;
}

int efs_fseek(efs_file_t *file, long offset, int whence)
{
	long newpos;

	if (!file) {
		errno = EBADF;
		return -1;
	}

	switch (file->dinode.di_mode & IFMT) {
	  case IFIFO:
	  case IFSOCK:
		errno = ESPIPE;
		return -1;
	  default:
		break;
	}

	switch (whence) {
	  case SEEK_SET:
		newpos = 0;
		break;
	  case SEEK_CUR:
		newpos = file->pos;
		break;
	  case SEEK_END:
		newpos = file->dinode.di_size -1;
		break;
	  default:
		errno = EINVAL;
		return -1;
	}

	newpos += offset;
	if (offset < 0) {
		errno = EINVAL;
		return -1;
	}

	file->eof = false;
	file->pos = newpos;
	return 0;
}

long efs_ftell(efs_file_t *file)
{
	return file->pos;
}

void efs_rewind(efs_file_t *file)
{
	(void)efs_fseek(file, 0, SEEK_SET);
}

void efs_clearerr(efs_file_t *file)
{
	file->eof = false;
}

int efs_feof(efs_file_t *file)
{
	return file->eof;
}

int efs_ferror(efs_file_t *file)
{
	return file->error;
}

#define EFS_DIRENT_INCR	(100)

struct efs_dirent *_efs_read_dirblks(efs_t *ctx, efs_ino_t ino)
{
	struct efs_dirent *out = NULL, *old = NULL;
	size_t out_size = 0;
	size_t out_used = 0;
	size_t sRc;
	struct efs_dinode di;
	struct efs_dirblk dirblk;

	di = efs_get_inode(ctx, ino);
	if ((di.di_mode & IFMT) != IFDIR)
		return NULL;

	out_size = EFS_DIRENT_INCR;
	out = calloc(out_size, sizeof(struct efs_dirent));
	if (!out)
		return NULL;

#if 0
	struct efs_extent *exs;
	exs = _efs_get_extents(ctx, &di);
	if (exs) {
		printf("ino   ma  bn      ln  offset\n");
		printf("----  --  ------  --  ------\n");
		for (unsigned i = 0; i < di.di_numextents; i++) {
			printf("%4u  %2x  %6x  %2x  %6x\n",
				i,
				exs[i].ex_magic,
				exs[i].ex_bn,
				exs[i].ex_length,
				exs[i].ex_offset
			);
		}
	}
#endif

	efs_file_t *file;
	file = _efs_file_openi(ctx, ino);
	if (!file)
		errx(1, "in _efs_read_dirblks while opening directory");
	for (unsigned blk = 0; blk < (file->nbytes / BLKSIZ); blk++) {
		memset(&dirblk, 0xba, sizeof(dirblk));
		sRc = efs_fread(&dirblk, sizeof(dirblk), 1, file);
		if (sRc != 1) errx(1, "while reading dirblk blk");
		if (dirblk.magic != htobe16(EFS_DIRBLK_MAGIC)) {
			warnx("skipping block %u", blk);
			hexdump(&dirblk, BLKSIZ);
			continue;
		}
		for (unsigned slot = 0; slot < dirblk.slots; slot++) {
			unsigned slotOffset;
			struct efs_dent *dent;
			if (dirblk.space[slot] >= dirblk.firstused) {
				struct efs_dirent de;
				slotOffset = dirblk.space[slot] << 1;
				dent = (struct efs_dent *)((uint8_t *)(&dirblk) + slotOffset);
				memcpy(&de.d_name, dent->d_name, dent->d_namelen);
				de.d_name[dent->d_namelen] = '\0';
				de.d_ino = be32toh(dent->l);
				//printf("%8x  %s\n", de.d_ino, de.d_name);

				/* add dirent */
				if (out_size == out_used) {
					out_size += EFS_DIRENT_INCR;
					out = realloc(out, out_size * sizeof(struct efs_dirent));
					if (!out)
						return NULL;
				}

				out[out_used++] = de;
			}
		}
	}

	/* add one more dirent as a terminator */
	old = out;
	out = realloc(out, (out_used + 1) * sizeof(struct efs_dirent));
	if (!out)
		free(old);
	else
		out[out_used].d_ino = 0;

	efs_fclose(file);
	file = NULL;
	return out;
}

/* returns the leftmost path part in firstpart[],
 *
 */
int _efs_nextpath(
	const char *in,
	char *firstpart,
	const char **remaining
) {
	const char *t;
	off_t len;

	if (!in) {
		return -1;
	}

	/* name too large */
	if (strlen(in) > EFS_MAX_NAME) {
		return -1;
	}

	/* empty name */
	if (!strcmp("", in) || !strcmp("/", in)) {
		strcpy(firstpart, ".");
		*remaining = NULL;
		return 0;
	}

	/* no slashes */
	t = strchr(in, '/');
	if (!t) {
		strncpy(firstpart, in, EFS_MAX_NAME);
		*remaining = NULL;
		return 0;
	}

	len = t - in;
	if (len > EFS_MAX_NAME) {
		return -1;
	}

	strncpy(firstpart, in, len);
	firstpart[len] = '\0';
	*remaining = (const char *)(t + 1);
	return 0;
}

efs_ino_t _efs_nameiat(efs_t *ctx, efs_ino_t ino, const char *name)
{
	int rc;
	struct efs_dirent *dirents;
	char firstpart[EFS_MAX_NAME + 1];
	const char *remaining;

	/*
	printf("_efs_nameiat: ctx %p, name '%s', ino %u\n",
	       ctx, name, ino);
	*/

	dirents = _efs_read_dirblks(ctx, ino);
	if (!dirents)
		return -1;

	rc = _efs_nextpath(name, firstpart, &remaining);
	if (rc == -1) {
		free(dirents);
		return -1;
	}
	// printf("firstpart: '%s'\n", firstpart);
	// printf("remaining: '%s'\n", remaining);

	struct efs_dirent *de;
	efs_ino_t myino;
	for (de = dirents; (myino = de->d_ino); de++) {
		if (!strcmp(de->d_name, firstpart)) {
			// printf("found it at inode %x\n", de->d_ino);
			if (!remaining) {
				free(dirents);
				return myino;
			} else {
				free(dirents);
				return _efs_nameiat(ctx, myino, remaining);
			}
		}
	}

	free(dirents);

	return -1;
}

efs_ino_t efs_namei(efs_t *ctx, const char *name)
{
	return _efs_nameiat(ctx, EFS_BLK_ROOTINO, name);
}

