#define _GNU_SOURCE
#include <err.h>
#include <inttypes.h>
#include <iso646.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "efs.h"
#include "endian.h"
#include "hexdump.h"
#include "noreturn.h"

extern char *__progname;
noreturn static void usage(void);

struct qent_s {
	struct qent_s *next;
	struct efs_extent_s ex;
	char *path;
};
struct qent_s *head = NULL;
struct qent_s *tail = NULL;

int queue_add(struct efs_extent_s ex, char *path, char *name)
{
	struct qent_s *q;
	int rc;
	if (ex.ex_magic != 0) return -1;
	q = calloc(1, sizeof(*head));
	if (!q) err(1, "in malloc");
	q->next = NULL;
	q->ex = ex;
	if (strlen(path) == 0) {
		q->path = strdup(name);
	} else {
		rc = asprintf(&(q->path), "%s/%s", path, name);
		if (rc == -1)
			errx(1, "in asprintf");
	}
	if (!head) {
		head = q;
	} else {
		tail->next = q;
	}
	tail = q;
	return 0;
}

struct qent_s *queue_dequeue(void)
{
	struct qent_s *out;
	out = head;
	if (head)
		head = head->next;
	if (!head)
		tail = NULL;
	return out;
}

void print_ex(struct efs_extent_s ext)
{
	printf("\t%u %u %u %u ",
		ext.ex_magic,
		ext.ex_bn,
		ext.ex_length,
		ext.ex_offset
	);
}

void print_queue(void)
{
	printf("head\n");
	for (struct qent_s *q = head; q; q=q->next) {
		print_ex(q->ex);
		printf("%s\n", q->path);
	}
	printf("tail\n");
}

int main(int argc, char *argv[])
{
	char *filename = NULL;
	int rc;

	char buf[BLKSIZ * 2];
	
	while ((rc = getopt(argc, argv, "f:")) != -1)
		switch (rc) {
		case 'f':
			if (filename)
				usage();
			filename = optarg;
			break;
		default:
			usage();
		}
	argc -= optind;
	argv += optind;
	if (not filename)
		usage();
	if (*argv != NULL)
		usage();
	
	efs_ctx_t *ctx;
	efs_err_t erc;
	erc = efs_open(&ctx, filename);
	if (erc != EFS_ERR_OK)
		errefs(1, erc, "couldn't open '%s'", filename);
	
	erc = efs_get_blocks(ctx, &buf, 0, 2);
	if (erc != EFS_ERR_OK) {
		errefs(1, erc, "while reading blocks");
	}

	struct efs_dinode_s inode;

	struct efs_extent_s rootinoex;
	rootinoex.ex_magic = 0;
	rootinoex.ex_bn = 2;
	rootinoex.ex_length = 1;
	rootinoex.ex_offset = 0;

	queue_add(rootinoex, "", "");
	print_queue();

	for (struct qent_s *qe = queue_dequeue(); qe; free(qe),qe = queue_dequeue()) {
		inode = efs_get_inode(ctx, qe->ex.ex_bn);
		//hexdump(&inode, sizeof(inode));

		switch (inode.di_mode & IFMT) {
		case IFDIR:
			
			printf("dir: '%s'\n", qe->path);
			if (strlen(qe->path)) {
				//rc = mkdir(qe->path, 0755);
				//if (rc == -1) err(1, "couldn't make directory '%s'", qe->path);
			}
			for (unsigned i = 0; i < inode.di_numextents; i++) {
				uint8_t buf[BLKSIZ];
				const struct efs_dirblk_s *dirblk;
				struct efs_extent_s ex;
				ex = inode.di_u.di_extents[i];
				rc = efs_get_blocks(ctx, &buf, ex.ex_bn, 1);
				if (rc != EFS_ERR_OK) errefs(1, ctx->err, "while reading dirblk");
				dirblk = (struct efs_dirblk_s *) &buf;
				if (be16toh(dirblk->magic) != EFS_DIRBLK_MAGIC) {
					errx(1, "bad dirblk magic");
				}
				//printf("dir firstused: %x\n", dirblk->firstused);
				//printf("dir slots: %x\n", dirblk->slots);
				for (int slot = 2; slot < dirblk->slots; slot++) {
					char name[EFS_MAXNAMELEN + 1] = {0,};
					off_t slotOffset = dirblk->space[slot] * 2;
					struct efs_dent_s *dent = (struct efs_dent_s *)((uint8_t *)dirblk + slotOffset);
					memcpy(name, dent->d_name, dent->d_namelen);
					name[dent->d_namelen] = '\0';
					//printf("slot %3d: %8xh '%s'\n", slot, be32toh(dent->l), name);
					struct efs_extent_s ex;
					ex.ex_magic = 0;
					ex.ex_bn = be32toh(dent->l);
					ex.ex_length = 1;
					ex.ex_offset = 0;
					queue_add(ex, qe->path, name);
				}

			}
			break;
		case IFREG:
			printf("reg: '%s'\n", qe->path);
			break;
		case IFIFO:
			printf("fifo: '%s'\n", qe->path);
			break;
		case IFCHR:
			printf("chr: '%s'\n", qe->path);
			break;
		case IFBLK:
			printf("blk: '%s'\n", qe->path);
			break;
		case IFLNK:
			printf("lnk: '%s'\n", qe->path);
			break;
		case IFSOCK:
			printf("sock: '%s'\n", qe->path);
			break;
		default:
			printf("weird file type\n");
			break;
		}
		free(qe->path);
		//print_queue();
	}

	efs_close(ctx);

	return EXIT_SUCCESS;
}

noreturn static void usage(void)
{
	(void)fprintf(stderr, "usage: %s -f file\n",
		__progname
	);
	exit(EXIT_FAILURE);
}

