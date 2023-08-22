#define _GNU_SOURCE
#include <errno.h>
#include <inttypes.h>
#include <iso646.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#if defined(__linux)
#include <sys/sysmacros.h>
#endif

#include "dvh.h"
#include "efs.h"
#include "efs_internal.h"
#include "err.h"
#include "endian.h"
#include "hexdump.h"
#include "noreturn.h"
#include "progname.h"
#include "version.h"

noreturn static void tryhelp(void);
noreturn static void usage(void);

struct qent {
	struct qent *next;
	struct qent *prev;
	char *path;
	efs_ino_t ino;
};
struct qent *head = NULL;
struct qent *tail = NULL;

/* add to tail of queue */
int queue_enqueue(efs_ino_t ino, char *path, char *name)
{
	struct qent *q;
	int rc;
	q = calloc(1, sizeof(*q));
	if (!q) err(1, "in malloc");

	q->ino = ino;

	if (strlen(path) == 0) {
		q->path = strdup(name);
	} else {
		size_t stringsize = strlen(path) + strlen("/") + strlen(name) + 1;
		q->path = malloc(stringsize);
		if (!q->path)
			err(1, "in malloc");
		rc = snprintf(q->path, stringsize, "%s/%s", path, name);
		if (rc >= stringsize)
			errx(1, "in snprintf");
		q->path[stringsize - 1] = '\0';
	}

	if (tail)
		tail->next = q;
	q->prev = tail;
	tail = q;
	if (!head)
		head = q;

	return 0;
}

struct qent *queue_dequeue(void)
{
	struct qent *out;
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

struct qent *queue_dequeue_lowest(void)
{
	struct qent *out = head;
	
	if (!out) return NULL;

	for (struct qent *cur = head; cur; cur = cur->next) {
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

/*
void print_ex(struct efs_extent ext)
{
	printf("%u %u %u %u\n",
		ext.ex_magic,
		ext.ex_bn,
		ext.ex_length,
		ext.ex_offset
	);
}
*/

void print_queue(void)
{
	printf("head\n");
	for (struct qent *q = head; q; q=q->next) {
		printf("%s\n", q->path);
	}
	printf("tail\n");
}

int write_extent(efs_t *ctx, FILE *outf, struct efs_extent ex, size_t nbytes)
{
	size_t rc;
	efs_err_t erc;
	void *buf;
	buf = malloc(EFS_MAXEXTENTS * BLKSIZ);
	if (!buf) err(1, "in malloc");

	erc = efs_get_blocks(ctx, buf, ex.ex_bn, ex.ex_length);
	if (erc != EFS_ERR_OK)
		errefs(1, erc, "while reading extent");
	rc = fseek(outf, ex.ex_offset * BLKSIZ, SEEK_SET);
	if (rc == -1)
		err(1, "while seeking before writing extent");
	rc = fwrite(buf, nbytes, 1, outf);
	if (rc != 1)
		err(1, "while writing extent");
	

	free(buf);
	return 0;
}

size_t write_extents(efs_t *ctx, FILE *outf, struct efs_extent *exs, size_t nex, size_t filesize)
{
	size_t bytes_left = filesize;

	for (unsigned exnum = 0; (exnum < nex) && bytes_left; exnum++) {
		size_t nbytes;
		struct efs_extent ex;
		ex = exs[exnum];
		if (ex.ex_length > EFS_MAXEXTENTLEN)
			errx(1, "bad extent length");
		if (bytes_left >= (ex.ex_length * BLKSIZ)) {
			nbytes = ex.ex_length * BLKSIZ;
		} else {
			nbytes = bytes_left;
		}
		write_extent(ctx, outf, ex, nbytes);
		bytes_left -= nbytes;
	}
	return bytes_left;
}

int qflag = 0;
int lflag = 0;
int Pflag = 0;
int Lflag = 0;

int main(int argc, char *argv[])
{
	char *filename = NULL;
	int rc;
	int parnum = -1;

	char buf[BLKSIZ * 2];

	progname_init(argc, argv);
	
	while ((rc = getopt(argc, argv, "hLlp:PqV")) != -1)
		switch (rc) {
		case 'h':
			usage();
			break;
		case 'L':
			if (Lflag != 0) {
				warnx("multiple use of `-L'");
				tryhelp();
			}
			Lflag = 1;
			break;
		case 'l':
			if (lflag != 0) {
				warnx("multiple use of `-l'");
				tryhelp();
			}
			lflag = 1;
			break;
		case 'p':
			if (parnum != -1) {
				warnx("multiple use of `-p'");
				tryhelp();
			}
			{
				char *ptr = NULL;
				parnum = strtol(optarg, &ptr, 10);
				if (*ptr)
					errx(1, "bad partition number `%s'", optarg);
			}
			break;
		case 'P':
			if (Pflag != 0) {
				warnx("multiple use of `-P'");
				tryhelp();
			}
			Pflag = 1;
			break;
		case 'q':
			if (qflag != 0) {
				warnx("multiple use of `-q'");
				tryhelp();
			}
			qflag = 1;
			break;
		case 'V':
			fprintf(stderr, "%s\n", PROG_VERSION);
			exit(EXIT_SUCCESS);
			break;
		default:
			tryhelp();
		}
	argc -= optind;
	argv += optind;
	if (*argv != NULL)
		filename = *argv;
	else {
		warnx("must specify a file");
		tryhelp();
	}
	if (parnum == -1)
		parnum = 7;

	if (Pflag && lflag)
		errx(1, "cannot combine P and l flags");
	
	if (Lflag && (Pflag || lflag || qflag))
		errx(1, "cannot combine L flag with other flags");
		
	if (Lflag) {
		FILE *f;
		struct dvh_s dvh;
		f = fopen(filename, "rb");
		if (!f) err(1, "couldn't open '%s'", filename);
		rc = fread(&dvh, sizeof(dvh), 1, f);
		if (rc != 1)
			err(1, "couldn't read dvh");
		if (be32toh(dvh.vh_magic) != VHMAGIC)
			errx(1, "bad VH magic");
		printf("idx  start     size      type\n");
		printf("---  --------  --------  --------\n");
		for (unsigned i = 0; i < NPARTAB; i++) {
			struct dvh_pt_s pt;
			const char *typeName;
			pt = dvh_getPar(&dvh, i);
			typeName = dvh_getNameForType(pt.pt_type);
			if (pt.pt_nblks) {
				printf("%3u  %8d  %8d  ",
					i,
					pt.pt_firstlbn,
					pt.pt_nblks
				);
				if (typeName) {
					printf("%s\n", typeName);
				} else {
					printf("(%d)\n", pt.pt_type);
				}
			} else {
				//printf("%3u         -         -\n", i);
			}
		}
		printf("\n");
		printf("start     size      name\n");
		printf("--------  --------  --------\n");
		for (unsigned i = 0; i < NVDIR; i++) {
			struct dvh_vd_s vd;
			char name[VDNAMESIZE + 1];
			name[VDNAMESIZE] = '\0';
			vd = dvh_getFile(&dvh, i);
			memcpy(name, vd.vd_name, VDNAMESIZE);
			if (strlen(name) || vd.vd_lbn || vd.vd_nbytes)
				printf("%8d  %8d  %s\n",
					vd.vd_lbn,
					vd.vd_nbytes,
					name
				);
		}
			
		exit(0);
	}
	
	efs_t *ctx = NULL;
	efs_err_t erc;
	erc = efs_open(&ctx, filename, parnum);
	if (erc != EFS_ERR_OK)
		errefs(1, erc, "couldn't open '%s'", filename);
	
	erc = efs_get_blocks(ctx, &buf, 0, 2);
	if (erc != EFS_ERR_OK)
		errefs(1, erc, "while reading blocks");

	struct efs_dinode inode;

	queue_enqueue(2, "", "");

	for (struct qent *qe = queue_dequeue_lowest(); qe; free(qe),qe = queue_dequeue_lowest()) {
		__label__ nextfile;
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
#if defined(__MINGW32__)
				rc = mkdir(qe->path);
#else
				rc = mkdir(qe->path, 0755);
#endif
				//if (rc == -1) err(1, "couldn't make directory '%s'", qe->path);
			}
			for (unsigned exnum = 0; exnum < inode.di_numextents; exnum++) {
				struct efs_dirblk *dirblks;
				struct efs_extent ex;
				ex = inode.di_u.di_extents[exnum];
				dirblks = calloc(ex.ex_length, BLKSIZ);
				if (!dirblks) err(1, "in malloc");
				rc = efs_get_blocks(ctx, dirblks, ex.ex_bn, ex.ex_length);
				if (rc != EFS_ERR_OK) errefs(1, ctx->err, "while reading dirblks");
				for (unsigned dirblknum = 0; dirblknum < ex.ex_length; dirblknum++) {
					struct efs_dirblk *dirblk;
					dirblk = &dirblks[dirblknum];
					if (be16toh(dirblk->magic) != EFS_DIRBLK_MAGIC) {
						errx(1, "bad dirblk magic");
					}
					for (int slot = 0; slot < dirblk->slots; slot++) {
						char name[EFS_MAX_NAME + 1] = {0,};
						off_t slotOffset = dirblk->space[slot] << 1;
						struct efs_dent *dent = (struct efs_dent *)((uint8_t *)dirblk + slotOffset);
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
				uint8_t *buf = NULL;
				size_t bufsiz = EFS_MAXEXTENTLEN; // in blocks
				size_t bytes_left = inode.di_size;

				buf = malloc(BLKSIZ * bufsiz);
				if (!buf) err(1, "in malloc");
				
				/*
				 * Check if our destination file exists
				 * as a symlink, and if so, remove it.
				 */
#if !defined(__MINGW32__)
				struct stat sb = {0,};
				rc = lstat(qe->path, &sb);
				if ((rc == -1) && (errno != ENOENT)) {
					err(1, "couldn't stat output path '%s'", qe->path);
				} else if (((sb.st_mode) & IFMT) == IFLNK) {
					rc = unlink(qe->path);
					if (rc == -1)
						err(1, "couldn't remove '%s'", qe->path);
				}
#endif

				FILE *outf = fopen(qe->path, "wb");
				if (!outf) {
					warn("couldn't create file '%s'", qe->path);
					goto nextfile;
				}

				if (inode.di_numextents > EFS_DIRECTEXTENTS) {
					efs_err_t erc;
					struct efs_extent_ex;
					struct efs_extent *buf;
					unsigned num_indirect;

					num_indirect = inode.di_u.di_extents[0].ex_offset;

					buf = malloc(BLKSIZ * EFS_MAXEXTENTS);
					if (!buf) err(1, "in malloc");

					for (unsigned exnum = 0; exnum < num_indirect; exnum++) {
						struct efs_extent ex;
						size_t numextents;
						ex = inode.di_u.di_extents[exnum];
						erc = efs_get_blocks(ctx, buf, ex.ex_bn, ex.ex_length);
						if (erc != EFS_ERR_OK) errefs(1, erc, "while reading indirect extent");

						numextents = (BLKSIZ * ex.ex_length) / sizeof(ex);

						for (unsigned a = 0; a < numextents; a++) {
							buf[a] = efs_extenttoh(buf[a]);
						}

						bytes_left = write_extents(ctx, outf, buf, numextents, bytes_left);
					}
					free(buf);
				} else {
					// direct extents
					bytes_left = write_extents(ctx, outf, inode.di_u.di_extents, inode.di_numextents, bytes_left);
				}

				fclose(outf);
				free(buf);
			}
			break;
		case IFIFO:
#if !defined(__MINGW32__)
			if (!lflag) {
				rc = mkfifo(qe->path, inode.di_mode & 0777);
				if (rc == -1) warn("couldn't create fifo '%s'", qe->path);
			}
#endif
			break;
		case IFCHR:
		case IFBLK:
#if !defined(__APPLE__) && !defined(__MINGW32__)
			if (!lflag) {
				dev_t dev;
				mode_t mode;
				unsigned int major, minor;

				mode = inode.di_mode & 0777;
				if (filetype == IFCHR) {
					mode |= IFCHR;
				} else if (filetype == IFBLK) {
					mode |= IFBLK;
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
#endif
			break;
		case IFLNK:
#if !defined(__MINGW32__)
			if (!lflag) {
				char namebuf[EFS_MAX_NAME + 1];
				efs_err_t erc;
				char *buf;
				struct efs_extent ex;

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
#endif
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
nextfile:
		free(qe->path);
	}

	efs_close(ctx);

	return EXIT_SUCCESS;
}

noreturn static void tryhelp(void)
{
	(void)fprintf(stderr, "Try `%s -h' for more information.\n",
		__progname);
	exit(EXIT_FAILURE);
}

noreturn static void usage(void)
{
	(void)fprintf(stderr, 
"Usage: %s [OPTION] [FILE]\n"
"Extract files from the SGI CD image (or EFS file system) in FILE.\n"
"\n"
"  -h       print this help text\n"
"  -l       list files without extracting\n"
"  -L       list partitions and bootfiles\n"
"  -p NUM   use partition number (default: 7)\n"
"  -P       also extract with file permissions\n"
"  -q       do not show file listing while extracting\n"
"  -V       print program version\n"
"\n"
"Please report any bugs to <jkbenaim@gmail.com>.\n"
,		__progname
	);
	exit(EXIT_SUCCESS);
}

