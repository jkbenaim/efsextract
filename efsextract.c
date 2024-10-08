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
#include <sys/types.h>

#include <cdio/iso9660.h>
#include <cdio/logging.h>

#ifndef __MINGW32__
#include <sys/sysmacros.h>
#endif

#include "efs.h"
#include "endian.h"
#include "err.h"
#include "hexdump.h"
#include "pdscan.h"
#include "progname.h"
#include "tar.h"
#include "version.h"

int qflag = 0;
int lflag = 0;
int Lflag = 0;
int Wflag = 0;
int Xflag = 0;
char *outfile = NULL;
efs_t *efs;

static void tryhelp(void);
static void usage(void);

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
	size_t blockNum;

	rc = efs_stat(efs, path, &sb);
	if (rc == -1)
		err(1, "couldn't get stat for '%s'", path);

	src = efs_fopen(efs, path);
	if (!src)
		errx(1, "couldn't open efs file '%s'", path);

	dst = fopen(path, "wb");
	if (!dst)
		err(1, "couldn't open destination file '%s'", path);

	for (blockNum = 0; blockNum < (sb.st_size / 512); blockNum++) {
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

	rc = chmod(path, sb.st_mode & 0777);
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
#ifdef __MINGW32__
		rc = mkdir(path);
#else
		rc = mkdir(path, sb.st_mode & 0777);
#endif
		if ((rc == -1) && (errno != EEXIST))
			err(1, "couldn't make directory '%s'", path);
		break;
	case IFREG:
		emit_regfile(efs, path);
		break;
	case IFIFO:
#ifndef __MINGW32__
		rc = mkfifo(path, sb.st_mode & 0777);
		if (rc == -1)
			warn("couldn't create fifo '%s'", path);
#else
		warnx("extracting fifos not supported");
#endif
		break;
	case IFCHR:
#ifndef __MINGW32__
		rc = mknod(path, S_IFCHR | (sb.st_mode & 0777), makedev(sb.st_major, sb.st_minor));
		if (rc == -1)
			warn("couldn't create character special '%s'", path);
#else
		warnx("extracting character specials not supported");
#endif
		break;
	case IFBLK:
#ifndef __MINGW32__
		rc = mknod(path, S_IFBLK | (sb.st_mode & 0777), makedev(sb.st_major, sb.st_minor));
		if (rc == -1)
			warn("couldn't create block special '%s'", path);
#else
		warnx("extracting block specials not supported");
#endif
		break;
	case IFLNK:
#ifndef __MINGW32__
	{
		__label__ done;
		char *buf = NULL;
		efs_file_t *f = NULL;
		size_t sz;

		buf = malloc(sb.st_size + 1);
		if (!buf)
			err(1, "in malloc");
		f = efs_fopen(efs, path);
		if (!f) {
			warnx("couldn't open efs symlink '%s'", path);
			goto done;
		}
		sz = efs_fread(buf, sb.st_size, 1, f);
		if (sz != 1) {
			warnx("couldn't read efs symlink '%s'", path);
			goto done;
		}
		buf[sb.st_size] = '\0';
		rc = symlink(buf, path);
		if (rc == -1)
			warn("couldn't create symlink '%s'", path);
done:
		if (f)
			efs_fclose(f);
		free(buf);
	}
#else
		warnx("extracting symlinks not supported");
#endif
		break;
	case IFSOCK:
		warnx("extracting sockets not supported");
		break;
	default:
		break;
	}
}

/*
 * This function is used as a callback for a later
 * invocation of efs_nftw().
 */
int efs_nftw_callback(const char *fpath, const struct efs_stat *sb) {
	/* extern: efs, outfile */
	int rc;

	if (Wflag) {
		pdprint(efs, fpath);
		return 0;
	}
	if (!qflag) {
		printf("%s\n", fpath);
	}
	if (!lflag) {
		if (outfile) {
			rc = tar_emit(efs, fpath);
			if (rc == -1)
				errx(1, "while writing to tar (emit failure): %d", rc);
		} else {
			emit_file(efs, fpath);
		}
	}

	return 0;
}

int main(int argc, char *argv[])
{
	char *filename = NULL;
	int rc;
	int parnum = -1;
	efs_err_t erc;
	dvh_t *dvh;
	fileslice_t *par;

	progname_init(argc, argv);

	while ((rc = getopt(argc, argv, "hLlo:p:qVWX")) != -1)
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
		case 'q':
			if (qflag != 0) {
				warnx("multiple use of `-q'");
				tryhelp();
			}
			qflag = 1;
			break;
		case 'V':
			fprintf(stderr, "%s\n", PROG_EMBLEM);
			exit(EXIT_SUCCESS);
			break;
		case 'W':
			if (Wflag != 0) {
				warnx("multiple use of `-W'");
				tryhelp();
			}
			Wflag = 1;
			break;
		case 'X':
			if (Xflag != 0) {
				warnx("multiple use of `-X'");
				tryhelp();
			}
			Xflag = 1;
			break;
		default:
			tryhelp();
		}
	argc -= optind;
	argv += optind;

	/* -L flag: cannot be combined with other flags */
	if (Lflag && (lflag || qflag || Wflag))
		errx(1, "cannot combine -L flag with other flags");
	
	/* -X flag: can be combined with -q flag, but nothing else */
	if (Xflag && (lflag || Lflag || Wflag))
		errx(1, "cannot combine -X flag with other flags");
	if (Xflag && outfile)
		errx(1, "cannot combine -X flag with -o");
	
	/* grab filename as first un-flagged argument */
	if (*argv != NULL) {
		filename = *argv;
	} else {
		warnx("must specify a file");
		tryhelp();
	}

	if (parnum == -1)
		parnum = 7;

	if (Lflag) {
		efs_err_t erc;
		dvh_t *ctx = NULL;
		unsigned i;
		erc = dvh_open(&ctx, filename);
		if (erc != EFS_ERR_OK)
			errefs(1, erc, "couldn't open dvh in '%s'", filename);
		printf("idx  start     size      type\n");
		printf("---  --------  --------  --------\n");
		for (i = 0; i < NPARTAB; i++) {
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
#if 0
				printf("%3u         -         -\n", i);
#endif
			}
		}
		printf("\n");
		printf("start     size      name\n");
		printf("--------  --------  --------\n");
		for (i = 0; i < NVDIR; i++) {
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

	erc = dvh_open(&dvh, filename);
	if ((erc != EFS_ERR_OK) && !outfile) {
		errx(1, "couldn't find volume header in '%s'", filename);
	} else if (erc != EFS_ERR_OK) {
		/* is it iso9660? */
		CdioList_t *a;
		CdioListNode_t *b;
		iso9660_t *ctx;
		queue_t q, dirq;
		int rc;
		struct qent_s *qe;

		cdio_loglevel_default = CDIO_LOG_ERROR;

		rc = tar_create(outfile);
		if (rc) {
			errx(1, "while creating tar");
		}

		q = queue_init();
		queue_add_head(q, strdup(""));

		ctx = iso9660_open(filename);
		if (!ctx)
			errx(1, "couldn't open '%s'", filename);

		while ((qe = queue_dequeue(q))) {
			a = iso9660_ifs_readdir(ctx, qe->path);
			dirq = queue_init();
			if (a) {
				_CDIO_LIST_FOREACH(b, a) {
					iso9660_stat_t *st;
					char *path;

					st = (iso9660_stat_t *) _cdio_list_node_data(b);
					path = mkpath(qe->path, st->filename);
					if (strcmp(st->filename, ".") && strcmp(st->filename, "..")) {
						if (!qflag && path) {
							printf("%s\n", path);
						}
						switch (st->type) {
						case _STAT_DIR:
							queue_add_head(dirq, path);
							break;
						case _STAT_FILE:
							tar_emit_from_iso9660(ctx, path);
							free(path);
							break;
						default:
							free(path);
							break;
						}
					} else {
						free(path);
					}
				}
				iso9660_filelist_free(a);
				a = NULL;
			}
			free(qe->path);
			free(qe);
			queue_add_queue_head(q, dirq);
		}
		queue_free(q);

		iso9660_close(ctx);
		tar_close();
		ctx = NULL;
		return 0;
	} /* end iso9660 branch */

	if (Xflag) {
		int fileNum;
		struct dvh_vd_s vd;

		for (fileNum = 0; fileNum < NVDIR; fileNum++) {
			vd = dvh_getFileInfo(dvh, fileNum);
			if (vd.vd_lbn != 0) {
				void *filedata;
				char filename[VDNAMESIZE + 1];
				FILE *f = NULL;

				strncpy(filename, vd.vd_name, VDNAMESIZE);
				filename[VDNAMESIZE] = '\0';

				filedata = dvh_readFile(dvh, fileNum);
				if (filedata)
					f = fopen(filename, "wb");

				if (f) {
					size_t sRc;
					sRc = fwrite(filedata, vd.vd_nbytes, 1, f);
					fclose(f);
					if (sRc != 1) {
						unlink(filename);
						err(1, "while writing file `%s'", filename);
					} else if (!qflag) {
						printf("%s\n", filename);
					}
				}
				free(filedata);
			}
		}
		efs_close(efs);
		dvh_close(dvh);
		return EXIT_SUCCESS;
	}

	par = dvh_getParSlice(dvh, parnum);
	if (!par)
		errx(1, "couldn't get par slice %u", parnum);
	erc = efs_open(&efs, par);
	if (erc != EFS_ERR_OK)
		errefs(1, erc, "couldn't open efs in '%s'", filename);

	if (outfile) {
		rc = tar_create(outfile);
		if (rc) err(1, "couldn't create archive '%s'", outfile);
	}

        if (Wflag) {
                printf("   %-30s  %s\n\n", "Name", "Description");
        }
	efs_nftw(efs, "", efs_nftw_callback);

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
"  -L       list partitions and bootfiles from the volume header\n"
"  -o ARCHIVE\n"
"           create a tar archive instead of extracting\n"
"  -p NUM   use partition number (default: 7)\n"
"  -q       do not show file listing while extracting\n"
"  -V       print program version\n"
"  -W       scan image for packages and list them\n"
"  -X       extract bootfiles from the volume headers\n"
"\n"
"Please report any bugs to <jkbenaim@gmail.com>.\n"
,		__progname
	);
	exit(EXIT_SUCCESS);
}

