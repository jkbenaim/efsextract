#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include "efs.h"
#include "efs_internal.h"
#include "err.h"
#include "hexdump.h"

char *_getfirstpathpart(char *name)
{
	char *ptr;
	char *newname;
	
	if (!name)
		return NULL;
	ptr = strchr(name, '/');
	if (!ptr)
		return name;
	newname = strdup(name);
	*(ptr - name + newname) = '\0';
	return newname;
}

struct _efs_file {
	struct efs_dinode dinode;
	unsigned numextents;
	struct efs_extent *exs;
	efs_t *ctx;
	efs_ino_t ino;
	unsigned pos;
	unsigned nbytes;
	bool eof;
	bool error;
};

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
		if (((ex->ex_offset + ex->ex_length) * BLKSIZ) < pos)
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
	size_t nbytes_in_extent;
	size_t start, end;
	start = BLKSIZ * ex.ex_offset;
	end = BLKSIZ * (ex.ex_offset + ex.ex_length);
	 /* assert position is within extent */
	 /* TODO */
}

size_t _efs_file_fread(
	void *ptr,
	size_t size,
	size_t nmemb,
	struct _efs_file *file
) {
	size_t out;
	struct efs_extent *ex;
	size_t nbytes;
	
	nbytes = size * nmemb;
	ex = _efs_find_extent(file->exs, file->numextents, file->pos);
	if (!ex) return 0;
	unsigned nbytes_this_extent;
	nbytes_this_extent = _efs_nbytes_this_extent(ex, file->pos, nbytes);
	
	memset(ptr, 0xab, nbytes);
	
	return 1;
}

struct _efs_file *_efs_file_open(efs_t *ctx, efs_ino_t ino)
{
	__label__ out_error, out_ok;
	struct _efs_file *out = NULL;
	if (!ctx) goto out_error;
	
	out = calloc(sizeof(struct _efs_file), 1);
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

int _efs_file_fclose(struct _efs_file *file)
{
	free(file->exs);
	free(file);
	return 0;
}

int _efs_file_fseek(struct _efs_file *file, long offset, int whence)
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

long _efs_file_ftell(struct _efs_file *file)
{
	return file->pos;
}

void _efs_file_rewind(struct _efs_file *file)
{
	(void)_efs_file_fseek(file, 0, SEEK_SET);
}

void _efs_file_clearerr(struct _efs_file *file)
{
	file->eof = false;
}

int _efs_file_feof(struct _efs_file *file)
{
	return file->eof;
}

int _efs_file_ferror(struct _efs_file *file)
{
	return file->error;
}

void _efs_read_dirblks(efs_t *ctx, efs_ino_t ino)
{
	size_t sRc;
	struct efs_dinode di;
	struct efs_dirblk dirblk;
	di = efs_get_inode(ctx, ino);
	//hexdump(&di, sizeof(di));
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
	
	struct _efs_file *file;
	file = _efs_file_open(ctx, ino);
	if (!file)
		errx(1, "in _efs_read_dirblks while opening directory");
	printf("file size: %u\n", file->nbytes);
	for (unsigned blk = 0; blk < (file->nbytes / BLKSIZ); blk++) {
		sRc = _efs_file_fread(&dirblk, sizeof(dirblk), 1, file);
		if (sRc != 1) errx(1, "while reading dirblk blk");
		hexdump(&dirblk, sizeof(dirblk));
		printf("PPL\n");
	}
	
	_efs_file_fclose(file);
	file = NULL;
}

efs_ino_t _efs_namei_aux(efs_t *ctx, char *name, efs_ino_t ino)
{
	char *firstpart;
	
	firstpart = _getfirstpathpart(name);
	printf("firstpart: '%s'\n", firstpart);
	if (firstpart[0] == '\0')
		return ino;
	_efs_read_dirblks(ctx, ino);
	return -1;
}

efs_ino_t efs_namei(efs_t *ctx, char *name)
{
	return _efs_namei_aux(ctx, name, EFS_BLK_ROOTINO);
}

__attribute__((weak))
int main(int argc, char *argv[])
{
	efs_err_t erc;
	efs_t *ctx;
	efs_ino_t ino;
	
	erc = efs_open(&ctx, "../testdisk.img", 7);
	if (erc != EFS_ERR_OK)
		errefs(1, erc, "couldn't open file");
	ino = efs_namei(ctx, "a");
	printf("inode: %u\n", ino);
	
	efs_close(ctx);
	ctx = NULL;
	return 0;
}