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
#include "endian.h"
#include "err.h"
#include "hexdump.h"
#include "progname.h"
#include "tar.h"
#include "version.h"

int qflag = 0;
int lflag = 0;
int Pflag = 0;
int Lflag = 0;

static void tryhelp(void);
static void usage(void);

struct qent {
	struct qent *next;
	struct qent *prev;
	char *path;
};
struct qent *head = NULL;
struct qent *tail = NULL;

char *mkpath(char *path, char *name)
{
	int rc;
	char *out;

	if (strlen(path) == 0) {
		out = strdup(name);
	} else {
		size_t stringsize = strlen(path) + strlen("/") + strlen(name) + 1;
		out = malloc(stringsize);
		if (!out) err(1, "in malloc");

		rc = snprintf(out, stringsize, "%s/%s", path, name);
		if (rc >= stringsize)
			errx(1, "in snprintf");
		out[stringsize - 1] = '\0';
	}
	return out;
}

/* add to tail of queue */
int queue_enqueue(char *path)
{
	struct qent *q;

	q = calloc(1, sizeof(*q));
	if (!q) err(1, "in malloc");

	q->path = path;

	if (tail)
		tail->next = q;
	q->prev = tail;
	tail = q;
	if (!head)
		head = q;

	return 0;
}

/* add to head of queue */
int queue_push(char *path)
{
	struct qent *q;

	q = calloc(1, sizeof(*q));
	if (!q) err(1, "in malloc");

	q->path = path;

	if (head)
		head->prev = q;
	q->next = head;
	head = q;
	if (!tail)
		tail = q;

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

void mode2str(char *str, uint16_t mode)
{
	strcpy(str, "----------");
	
	switch (mode & IFMT) {
	case IFIFO:
		str[0] = 'p';
		break;
	case IFCHR:
		str[0] = 'c';
		break;
	case IFDIR:
		str[0] = 'd';
		break;
	case IFBLK:
		str[0] = 'b';
		break;
	case IFREG:
		str[0] = '-';
		break;
	case IFLNK:
		str[0] = 'l';
		break;
	case IFSOCK:
		str[0] = 's';
		break;
	default:
		str[0] = '?';
		break;
	}
	
	if (mode & 0400) str[1] = 'r';
	if (mode & 0200) str[2] = 'w';
	if (mode & 0100) str[3] = 'x';
	if (mode & 0040) str[4] = 'r';
	if (mode & 0020) str[5] = 'w';
	if (mode & 0010) str[6] = 'x';
	if (mode & 0004) str[7] = 'r';
	if (mode & 0002) str[8] = 'w';
	if (mode & 0001) str[9] = 'x';
	str[10] = '\0';
}

int mode2color(uint16_t mode)
{
	switch (mode & IFMT) {
	case IFIFO:
		return 33;
	case IFCHR:
		return 33;
	case IFDIR:
		return 34;
	case IFBLK:
		return 33;
	case IFREG:
		if (mode & 0111)
			return 32;
		else
			return -1;
	case IFLNK:
		return 36;
	case IFSOCK:
		return 35;
	default:
		return -1;
	}
}

void emit_regfile(efs_t *efs, const char *path)
{
	struct efs_stat sb;
	int rc;
	FILE *dst;
	efs_file_t *src;
	char blk[BLKSIZ];
	size_t sz;
	size_t bytesLeft;

	rc = efs_stat(efs, path, &sb);
	if (rc == -1)
		err(1, "couldn't get stat for '%s'", path);

	src = efs_fopen(efs, path);
	if (!src)
		errx(1, "couldn't open efs file '%s'", path);

	dst = fopen(path, "w");
	if (!dst)
		err(1, "couldn't open destination file '%s'", path);

	for (size_t blockNum = 0; blockNum < (sb.st_size / 512); blockNum++) {
		sz = efs_fread(blk, BLKSIZ, 1, src);
		if (sz != 1)
			err(1, "couldn't read from source file '%s'", path);
		sz = fwrite(blk, BLKSIZ, 1, dst);
		if (sz != 1)
			err(1, "couldn't write to destination file '%s'", path);
	}
	bytesLeft = sb.st_size & (BLKSIZ - 1);
	if (bytesLeft) {
		sz = efs_fread(blk, bytesLeft, 1, src);
		if (sz != 1)
			err(1, "couldn't read from source file '%s'", path);
		sz = fwrite(blk, bytesLeft, 1, dst);
		if (sz != 1)
			err(1, "couldn't write to destination file '%s'", path);
	}

	efs_fclose(src);
	fclose(dst);

	unsigned mask;
	if (Pflag) {
		mask = 07777;
	} else {
		mask = 0777;
	}

	rc = chmod(path, sb.st_mode & mask);
	if (rc == -1)
		err(1, "couldn't set permissions on '%s'", path);
}

void emit_file(efs_t *efs, const char *path)
{
	struct efs_stat sb;
	int rc;

	rc = efs_stat(efs, path, &sb);
	if (rc == -1)
		err(1, "couldn't get stat for '%s'", path);

	switch (sb.st_mode & IFMT) {
	case IFDIR:
		rc = mkdir(path, sb.st_mode & 0777);
		if (rc == -1)
			err(1, "couldn't make directory '%s'", path);
		break;
	case IFREG:
		emit_regfile(efs, path);
		break;
	case IFIFO:
		break;
	case IFCHR:
		break;
	case IFBLK:
		break;
	case IFLNK:
		break;
	case IFSOCK:
		break;
	default:
		break;
	}
}

void emit_file_tar(efs_t *efs, const char *path)
{
	struct efs_stat sb;
	int rc;

	rc = efs_stat(efs, path, &sb);
	if (rc == -1)
		err(1, "couldn't get stat for '%s'", path);

	switch (sb.st_mode & IFMT) {
	case IFDIR:
		break;
	case IFREG:
		break;
	case IFIFO:
		break;
	case IFCHR:
		break;
	case IFBLK:
		break;
	case IFLNK:
		break;
	case IFSOCK:
		break;
	default:
		break;
	}
}

int main(int argc, char *argv[])
{
	char *filename = NULL;
	char *outfile = NULL;
	int rc;
	int parnum = -1;

	progname_init(argc, argv);
	
	while ((rc = getopt(argc, argv, "hLlo:p:PqV")) != -1)
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
		case 'o':
			if (outfile) {
				warnx("multiple use of `-o'");
				tryhelp();
			}
			outfile = optarg;
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
	if (*argv != NULL) {
		// printf("got filename\n");
		filename = *argv;
	} else {
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
		efs_err_t erc;
		dvh_t *ctx = NULL;
		erc = dvh_open(&ctx, filename);
		if (erc != EFS_ERR_OK)
			errefs(1, erc, "couldn't open dvh in '%s'", filename);
		printf("idx  start     size      type\n");
		printf("---  --------  --------  --------\n");
		for (unsigned i = 0; i < NPARTAB; i++) {
			struct dvh_pt_s pt;
			const char *typeName;
			pt = dvh_getParInfo(ctx, i);
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
			vd = dvh_getFileInfo(ctx, i);
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
	
	efs_err_t erc;
	dvh_t *dvh;
	fileslice_t *par;
	efs_t *efs;
	
	erc = dvh_open(&dvh, filename);
	if (erc != EFS_ERR_OK)
		errefs(1, erc, "couldn't open dvh in '%s'", filename);
	
	par = dvh_getParSlice(dvh, parnum);
	if (!par)
		errx(1, "couldn't get par slice %u", parnum);
	erc = efs_open(&efs, par);
	if (erc != EFS_ERR_OK)
		errefs(1, erc, "couldn't open efs in '%s'", filename);

	queue_enqueue(strdup(""));

	if (outfile) {
		rc = tar_create(outfile);
		if (rc) err(1, "couldn't create archive '%s'", outfile);
	}

	struct qent *q;
	while ((q = queue_dequeue())) {
		efs_dir_t *dirp;
		dirp = efs_opendir(efs, q->path);
		if (!dirp) errx(1, "couldn't open directory: '%s'", q->path);
		struct efs_dirent *de;
		while ((de = efs_readdir(dirp))) {
			struct efs_stat sb;
			char *path;
			rc = efs_stati(efs, de->d_ino, &sb);
			if (rc == -1)
				err(1, "couldn't get stat for '%s'", de->d_name);
			path = mkpath(q->path, de->d_name);
			if (!path) goto nextfile;
			if ((sb.st_mode & IFMT) == IFDIR) {
				if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) {
					goto nextfile;
				}
				queue_push(strdup(path));
			}
			if (!qflag)
				printf("%s\n", path);
			if (!lflag) {
				if (outfile) {
					tar_emit(efs, path);
				} else {
					emit_file(efs, path);
				}
			}
nextfile:
			free(path);
		}
		efs_closedir(dirp);
		free(q->path);
		free(q);
	}

	if (outfile) {
		rc = tar_close();
		if (rc) err(1, "couldn't close archive '%s'", outfile);
	}
	
	efs_close(efs);
	dvh_close(dvh);

	return EXIT_SUCCESS;
}

static void tryhelp(void)
{
	(void)fprintf(stderr, "Try `%s -h' for more information.\n",
		__progname);
	exit(EXIT_FAILURE);
}

static void usage(void)
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

