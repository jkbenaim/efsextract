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
#include <sys/sysmacros.h>

#include "efs.h"
#include "endian.h"
#include "hexdump.h"
#include "noreturn.h"

extern char *__progname;
noreturn static void usage(void);

struct qent_s {
	struct qent_s *next;
	struct qent_s *prev;
	char *path;
	efs_ino_t ino;
};
struct qent_s *head = NULL;
struct qent_s *tail = NULL;

/* add to tail of queue */
int queue_enqueue(efs_ino_t ino, char *path, char *name)
{
	struct qent_s *q;
	int rc;
	q = calloc(1, sizeof(*q));
	if (!q) err(1, "in malloc");

	q->ino = ino;

	if (strlen(path) == 0) {
		q->path = strdup(name);
	} else {
		rc = asprintf(&(q->path), "%s/%s", path, name);
		if (rc == -1)
			errx(1, "in asprintf");
	}

	if (tail)
		tail->next = q;
	q->prev = tail;
	tail = q;
	if (!head)
		head = q;

	return 0;
}

struct qent_s *queue_dequeue(void)
{
	struct qent_s *out;
	out = head;

	if (head) {
		head = head->next;
		if (head)
			head->prev = NULL;
	}
	if (!head)
		tail = NULL;
	
	return out;
}

struct qent_s *queue_dequeue_lowest(void)
{
	struct qent_s *out = head;
	static size_t count = 0;
	
	if (!out) return NULL;

	for (struct qent_s *cur = head; cur; cur = cur->next) {
		if (cur->ino < out->ino)
			out = cur;
	}

	if (head == out) {
		head = out->next;
	}
	if (tail == out) {
		tail = out->prev;
	}
	if (out->next) {
		out->next->prev = out->prev;
	}
	if (out->prev) {
		out->prev->next = out->next;
	}
	out->next = out->prev = NULL;

	return out;
}

void print_ex(struct efs_extent_s ext)
{
	printf("%u %u %u %u\n",
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
		printf("%s\n", q->path);
	}
	printf("tail\n");
}

int qflag = 0;
int lflag = 0;
int Pflag = 0;

int main(int argc, char *argv[])
{
	char *filename = NULL;
	int rc;
	int parnum = -1;

	char buf[BLKSIZ * 2];
	
	while ((rc = getopt(argc, argv, "lp:Pq")) != -1)
		switch (rc) {
		case 'l':
			if (lflag != 0)
				usage();
			lflag = 1;
			break;
		case 'p':
			if (parnum != -1)
				usage();
			{
				char *ptr = NULL;
				parnum = strtol(optarg, &ptr, 10);
				if (*ptr)
					errx(1, "bad partition number '%s'", optarg);
			}
		case 'P':
			if (Pflag != 0)
				usage();
			Pflag = 1;
			break;
		case 'q':
			if (qflag != 0)
				usage();
			qflag = 1;
			break;
		default:
			usage();
		}
	argc -= optind;
	argv += optind;
	if (*argv != NULL)
		filename = *argv;
	else
		usage();
	if (parnum == -1)
		parnum = 7;

	if (Pflag && lflag)
		errx(1, "cannot combine P and l flags");
	
	efs_ctx_t *ctx = NULL;
	efs_err_t erc;
	erc = efs_open(&ctx, filename, parnum);
	if (erc != EFS_ERR_OK)
		errefs(1, erc, "couldn't open '%s'", filename);
	
	erc = efs_get_blocks(ctx, &buf, 0, 2);
	if (erc != EFS_ERR_OK)
		errefs(1, erc, "while reading blocks");

	struct efs_dinode_s inode;

	queue_enqueue(2, "", "");

	for (struct qent_s *qe = queue_dequeue_lowest(); qe; free(qe),qe = queue_dequeue_lowest()) {
		inode = efs_get_inode(ctx, qe->ino);
		if (inode.di_version) {
			errx(1, "bad inode version %u\n", inode.di_version);
		}
		if (!qflag && (strlen(qe->path) > 0)) {
			printf("%s\n", qe->path);
		}
		unsigned filetype = inode.di_mode & IFMT;
		switch (filetype) {
		case IFDIR:
			if (!lflag && strlen(qe->path)) {
				rc = mkdir(qe->path, 0755);
				if (rc == -1) err(1, "couldn't make directory '%s'", qe->path);
			}
			for (unsigned exnum = 0; exnum < inode.di_numextents; exnum++) {
				struct efs_dirblk_s *dirblks;
				struct efs_extent_s ex;
				ex = inode.di_u.di_extents[exnum];
				dirblks = calloc(ex.ex_length, BLKSIZ);
				if (!dirblks) err(1, "in malloc");
				rc = efs_get_blocks(ctx, dirblks, ex.ex_bn, ex.ex_length);
				if (rc != EFS_ERR_OK) errefs(1, ctx->err, "while reading dirblks");
				for (unsigned dirblknum = 0; dirblknum < ex.ex_length; dirblknum++) {
					struct efs_dirblk_s *dirblk;
					dirblk = &dirblks[dirblknum];
					if (be16toh(dirblk->magic) != EFS_DIRBLK_MAGIC) {
						errx(1, "bad dirblk magic");
					}
					for (int slot = 0; slot < dirblk->slots; slot++) {
						char name[EFS_MAXNAMELEN + 1] = {0,};
						off_t slotOffset = dirblk->space[slot] << 1;
						struct efs_dent_s *dent = (struct efs_dent_s *)((uint8_t *)dirblk + slotOffset);
						memcpy(name, dent->d_name, dent->d_namelen);
						name[dent->d_namelen] = '\0';
						//printf("slot %3d: %8xh '%s'\n", slot, be32toh(dent->l), name);
						if (!strcmp(".", name) || !strcmp("..", name)) {
							//printf("skipping '%s'\n", name);
						} else {
							queue_enqueue(be32toh(dent->l), qe->path, name);
						}
					}
				}
			}
			break;
		case IFREG:
			if (!lflag) {
				FILE *out = fopen(qe->path, "w");
				if (!out) warn("couldn't create file '%s'", qe->path);
				fclose(out);
			}
			break;
		case IFIFO:
			if (!lflag) {
				rc = mkfifo(qe->path, inode.di_mode & 0777);
				if (rc == -1) warn("couldn't create fifo '%s'", qe->path);
			}
			break;
		case IFCHR:
		case IFBLK:
			if (!lflag) {
				dev_t dev;
				mode_t mode;
				unsigned int major, minor;

				mode = inode.di_mode & 0777;
				if (filetype == IFCHR) {
					mode |= S_IFCHR;
				} else if (filetype == IFBLK) {
					mode |= S_IFBLK;
				}

				if (inode.di_u.di_dev.ndev != 0) {
					// use new
					major = (inode.di_u.di_dev.ndev & 0xFFFF0000) >> 16;
					minor = inode.di_u.di_dev.ndev & 0x0000FFFF;
				} else {
					// use old
					major = (inode.di_u.di_dev.odev & 0xFF00) >> 8;
					minor = inode.di_u.di_dev.odev & 0x00FF;
				}
				dev = makedev(major, minor);

				rc = mknod(qe->path, mode, dev);
				if (rc == -1) warn("couldn't create node '%s'", qe->path);
			}
			break;
		case IFLNK:
			if (!lflag) {
				char namebuf[EFS_MAXNAMELEN + 1];
				efs_err_t erc;
				char *buf;
				struct efs_extent_s ex;

				ex = inode.di_u.di_extents[0];

				buf = calloc(BLKSIZ, ex.ex_length);
				if (!buf) err(1, "in calloc");

				erc = efs_get_blocks(ctx, buf, ex.ex_bn, ex.ex_length);
				if (erc != EFS_ERR_OK) errefs(1, erc, "couldn't read blocks");

				memcpy(namebuf, buf, inode.di_size);
				namebuf[inode.di_size] = '\0';

				rc = symlink(namebuf, qe->path);
				if (rc == -1) warn("couldn't create symlink '%s'", qe->path);
				free(buf);
			}
			break;
		case IFSOCK:
			warnx("sockets not supported");
			break;
		default:
			errx(1, "weird file type");
			break;
		}
		if (Pflag && strlen(qe->path)) {
			rc = chmod(qe->path, inode.di_mode & 0777);
			if (rc == -1) err(1, "couldn't set permissions on '%s'\n", qe->path);
		}
		free(qe->path);
	}

	efs_close(ctx);

	return EXIT_SUCCESS;
}

noreturn static void usage(void)
{
	(void)fprintf(stderr, "usage: %s [-lPq] [-p #] file\n",
		__progname
	);
	exit(EXIT_FAILURE);
}

