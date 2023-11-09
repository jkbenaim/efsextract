#include <stdlib.h>
#include <stdio.h>
#include "fileslice.h"


fileslice_t *fsopen(FILE *f, size_t base, size_t size)
{
	__label__ out_error;
	fileslice_t *fs;
	fpos_t old_pos;
	int rc;

	fs = calloc(1, sizeof(fileslice_t));
	if (!fs) goto out_error;

	/* Save old position */
	rc = fgetpos(f, &old_pos);
	if (rc == -1) goto out_error;

	/* Seek to base address */
	rc = fseek(f, base, SEEK_SET);
	if (rc == -1) goto out_error;

	/* Save as base pos */
	rc = fgetpos(f, &fs->base);
	if (rc == -1) goto out_error;

	/* Restore old position */
	rc = fsetpos(f, &old_pos);
	if (rc == -1) goto out_error;

	fs->f = f;
	fs->cur = fs->base;

	return fs;

out_error:
	free(fs);
	return NULL;
}

int fsclose(fileslice_t *fs)
{
	free(fs);
	return 0;
}

size_t fsread(void *ptr, size_t size, size_t nmemb, fileslice_t *fs)
{
	__label__ out_error;
	size_t rc, rc2;
	fpos_t old_pos;

	/* Save old position */
	rc = fgetpos(fs->f, &old_pos);
	if (rc == -1) goto out_error;

	/* Restore cursor */
	rc = fsetpos(fs->f, &fs->cur);
	if (rc == -1) goto out_error;

	/* Do the read */
	rc2 = fread(ptr, size, nmemb, fs->f);

	/* Save cursor */
	rc = fgetpos(fs->f, &fs->cur);
	if (rc == -1) goto out_error;

	/* Restore old position */
	rc = fsetpos(fs->f, &old_pos);
	if (rc == -1) goto out_error;

	return rc2;

out_error:
	return 0;
}

int fsseek(fileslice_t *fs, long offset, int whence)
{
	__label__ out_error;
	int rc;
	fpos_t old_pos;
	
#if 0
	printf("fseek(%p, %lu, %d\n", fs, offset, whence);
#endif

	if ((whence != SEEK_SET) && (whence != SEEK_CUR)) {
		/* SEEK_END is not supported */
		goto out_error;
	}

	/* Save old position */
	rc = fgetpos(fs->f, &old_pos);
	if (rc == -1) goto out_error;

#if 0
	printf("fsread: pos before %lu\n", ftell(fs->f));
#endif

	/* Handle whence */
	if (whence == SEEK_SET) {
		rc = fsetpos(fs->f, &fs->base);
	} else if (whence == SEEK_CUR) {
		rc = fsetpos(fs->f, &fs->cur);
	} else {
		goto out_error;
	}
	if (rc == -1) goto out_error;

	/* Seek */
	rc = fseek(fs->f, offset, SEEK_CUR);
	if (rc == -1) goto out_error;

	/* Save cursor */
	rc = fgetpos(fs->f, &fs->cur);
	if (rc == -1) goto out_error;


	/* Restore old position */
	rc = fsetpos(fs->f, &old_pos);
	if (rc == -1) goto out_error;

	return 0;

out_error:
	return -1;
}

void fsrewind(fileslice_t *fs)
{
	fsseek(fs, 0, SEEK_SET);
}
