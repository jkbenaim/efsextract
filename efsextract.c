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
#include "progname.h"
#include "version.h"

static void tryhelp(void);
static void usage(void);

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

int qflag = 0;
int lflag = 0;
int Pflag = 0;
int Lflag = 0;

int main(int argc, char *argv[])
{
	char *filename = NULL;
	int rc;
	int parnum = -1;

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
	if (*argv != NULL) {
		printf("got filename\n");
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
		printf("a\n");
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
	
	efs_dir_t *dir;
	dir = efs_opendir(efs, "WhatsNew/");
	printf("%p\n", dir);
	efs_closedir(dir);
	
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

