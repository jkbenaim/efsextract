#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "efs.h"
#include "efs_internal.h"
#include "endian.h"
#include "err.h"
#include "hexdump.h"

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

