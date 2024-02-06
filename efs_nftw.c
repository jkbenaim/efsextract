#include <stdlib.h>
#include "efs.h"
#include "efs_internal.h"
#include "err.h"
#include "efs_nftw.h"
#include "queue.h"

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
