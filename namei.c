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
		numindirect = dinode->di_u.di_extents[0].ex_offset;
		
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
		
		for (unsigned i = 0; i < numindirect; i++) {
			struct efs_extent ex;
			unsigned offset;
			ex = dinode->di_u.di_extents[i];
			if (i == 0)
				offset = 0;
			else
				offset = ex.ex_offset;
			erc = efs_get_blocks(
				ctx,
				buf + (BLKSIZ * offset),
				ex.ex_bn,
				ex.ex_length
			);
			if (erc != EFS_ERR_OK)
				errefs(1, erc, "while fetching indir extent");
		}
		for (unsigned i = 0; i < numextents; i++) {
			struct efs_extent *exs;
			exs = (struct efs_extent *)buf;
			out[i] = efs_extenttoh(exs[i]);
		}
		free(buf);
	}
	
	/* assert that extents are in ascending order */
	unsigned last = out[0].ex_offset;
	for (size_t i = 1; i < numextents; i++) {
		if (last >= out[i].ex_offset) {
			errx(1, "unsorted extents");
		}
		last = out[i].ex_offset;
	}
	
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
		if ((ex->ex_offset * BLKSIZ) > pos)
			continue;
		/* too high? */
		if (((ex->ex_offset + ex->ex_length) * BLKSIZ) <= pos)
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
	start = BLKSIZ * ex->ex_offset;
	end = BLKSIZ * (ex->ex_offset + ex->ex_length);
	
	/* assert position is within extent */
	if (pos < start)
		goto out_error;
	if (pos > end)
		goto out_error;
	
	/* move pos to be relative to start of extent */
	pos -= start;
	unsigned bytes_left_in_extent;
	bytes_left_in_extent = end - start - pos;
#define MIN(a, b) (a>b?b:a)
	return MIN(bytes_left_in_extent, nbytes);
#undef MIN
out_error:
	return 0;
}

unsigned _efs_nbytes_firstbn(
	struct efs_extent *ex,
	unsigned pos
) {
	
	unsigned blkOff;
	
	typeof(pos) newpos;
	newpos = pos - (BLKSIZ * ex->ex_offset);
	blkOff = newpos / BLKSIZ;
	return blkOff;
}

size_t efs_fread(
	void *ptr,
	size_t size,
	size_t nmemb,
	efs_file_t *file
) {
	efs_err_t erc;
	size_t out;
	struct efs_extent *ex;
	size_t nbytes;
	uint8_t buf[BLKSIZ];
	
	nbytes = size * nmemb;
	ex = _efs_find_extent(file->exs, file->numextents, file->pos);
	if (!ex) return 0;
	unsigned nbytes_this_extent;
	nbytes_this_extent = _efs_nbytes_this_extent(ex, file->pos, nbytes);
	
	//printf("file->pos: %u\n", file->pos);
	memset(ptr, 0xab, nbytes);
	//printf("nbytes_this_extent: %u\n", nbytes_this_extent);
	
	while (nbytes_this_extent > 0) {
		unsigned nbytes_this_read;
#define MIN(a,b) (a>b?b:a)
		nbytes_this_read = MIN(BLKSIZ, nbytes_this_extent);
#undef MIN
		//printf("nbytes_this_read: %u\n", nbytes_this_read);
		//printf("nbytes_this_extent: %u\n", nbytes_this_extent);
		unsigned firstbn = _efs_nbytes_firstbn(ex, file->pos);
		erc = efs_get_blocks(
			file->ctx,
			buf,
			ex->ex_bn + firstbn,
			1
		);
		if (erc != EFS_ERR_OK)
			errefs(1, erc, "in _efs_file_fread");
		memcpy(ptr, buf, nbytes_this_read);
		//printf("ok\n");
		ptr += nbytes_this_read;
		nbytes_this_extent -= nbytes_this_read;
	}
	
	file->pos += nbytes;
	
	/* this return value is truly wrong */
	out = 1;
	return out;
}

efs_file_t *efs_fopenat(
	efs_t *ctx,
	efs_dir_t *dirp,
	const char *path,
	const char *mode
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
		      const char *path,
		      const char *mode
) {
	efs_dir_t dir;
	dir.ino = EFS_BLK_ROOTINO;
	return efs_fopenat(ctx, &dir, path, mode);
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
	
	/* validate inode */
	if (out->dinode.di_version != 0)
		goto out_error;
	if (out->dinode.di_nlink == 0)
		goto out_error;
	
	switch (out->dinode.di_mode & IFMT) {
	case IFREG:
	case IFDIR:
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
	struct efs_dirent *out = NULL;
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
#endif
	
	efs_file_t *file;
	file = _efs_file_openi(ctx, ino);
	if (!file)
		errx(1, "in _efs_read_dirblks while opening directory");
	for (unsigned blk = 0; blk < (file->nbytes / BLKSIZ); blk++) {
		memset(&dirblk, 0xab, sizeof(dirblk));
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
				de.d_ino = dent->l;
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
	out = realloc(out, (out_used + 1) * sizeof(struct efs_dirent));
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
	
	printf("_efs_nameiat: ctx %p, name '%s', ino %u\n",
	       ctx, name, ino);
	
	dirents = _efs_read_dirblks(ctx, ino);
	if (!dirents)
		return -1;
	
	rc = _efs_nextpath(name, firstpart, &remaining);
	if (rc == -1)
		return -1;
	printf("firstpart: '%s'\n", firstpart);
	printf("remaining: '%s'\n", remaining);

	struct efs_dirent *de;
	for (de = dirents; de->d_ino; de++) {
		efs_ino_t got = be32toh(de->d_ino);
		if (!strcmp(de->d_name, firstpart)) {
			printf("found it at inode %x\n", got);
			if (!remaining)
				return got;
			else
				return _efs_nameiat(ctx, got, remaining);
		}
	}
	
	free(dirents);
	
	return -1;
}

efs_ino_t efs_namei(efs_t *ctx, const char *name)
{
	return _efs_nameiat(ctx, EFS_BLK_ROOTINO, name);
}

