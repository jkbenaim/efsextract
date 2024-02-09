#pragma once
#include <dirent.h>
#include <time.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define BLKSIZ 512
#define EFS_BLK_SB	(1)
#define EFS_BLK_ROOTINO	(2)
#define EFS_OLDMAGIC	0x00072959L
#define EFS_NEWMAGIC	0x0007295AL
#define IS_EFS_OLDMAGIC(x) (x == EFS_OLDMAGIC)
#define IS_EFS_NEWMAGIC(x) (x == EFS_NEWMAGIC)
#define IS_EFS_MAGIC(x) (IS_EFS_OLDMAGIC(x) || IS_EFS_NEWMAGIC(x))

#define EFS_MAXLBSIZE

#define EFS_DIRECTEXTENTS 12
//#define EFS_MAXINDIRBBS 64
#define EFS_MAXINDIRBBS 128
#define EFS_MAXEXTENTS 32767
#define EFS_INODESPERPAGE (NBPC / sizeof(struct efs_dinode))
#define EFS_MAXEXTENTLEN (256 - 8)
#define EFS_EFSINOSHIFT	7

#define IFMT	0170000
#define IFIFO	0010000
#define IFCHR	0020000
#define IFDIR	0040000
#define IFBLK	0060000
#define IFREG	0100000
#define IFLNK	0120000
#define IFSOCK	0140000

#define ISMT	07000
#define ISUID	04000
#define ISGID	02000
#define ISVTX	01000

#define ARRAY_SIZE(x) (sizeof(x)/sizeof(*x))

typedef struct _fileslice_s {
	FILE *f;
	fpos_t base;
	fpos_t cur;
} fileslice_t;

typedef uint32_t efs_ino_t;

struct efs_sb {
	int32_t fs_size;
	int32_t fs_firstcg;
	int32_t fs_cgfsize;
	int16_t fs_cgisize;
	int16_t fs_sectors;
	int16_t fs_heads;
	int16_t fs_ncg;
	int16_t fs_dirty;
	int16_t __pad;
	int32_t fs_time;
	int32_t fs_magic;
	char fs_fname[6];
	char fs_fpack[6];
	int32_t fs_bmsize;
	int32_t fs_tfree;
	int32_t fs_tinode;
	int32_t fs_bmblock;
	int32_t fs_replsb;
	int32_t fs_lastialloc;
	char fs_spare[20];
	int32_t fs_checksum;

	/* followed by a bunch of in-memory-only fields */
	char __pad2[BLKSIZ-92];
} __attribute__((packed));

struct efs_extent {
	uint32_t ex_magic:8,
		 ex_bn:24,
		 ex_length:8,
		 ex_offset:24;
	// uint8_t ex_magic;
	// uint8_t ex_bn[3];
	// uint8_t ex_length;
	// uint8_t ex_offset[3];
} __attribute__((packed));

struct efs_edevs {
	uint16_t odev;
	uint32_t ndev;
} __attribute__((packed));

struct efs_dinode {
	uint16_t di_mode;
	int16_t  di_nlink;
	uint16_t di_uid;
	uint16_t di_gid;
	int32_t  di_size;
	int32_t  di_atime;
	int32_t  di_mtime;
	int32_t  di_ctime;
	uint32_t di_gen;
	int16_t  di_numextents;
	uint8_t  di_version;
	uint8_t  di_spare;
	union di_addr {
		struct efs_extent di_extents[EFS_DIRECTEXTENTS];
		struct efs_edevs  di_dev;
	} di_u;
} __attribute__((packed));

struct efs_dent {
	uint32_t l;
	uint8_t d_namelen;
	char d_name[];
} __attribute__((packed));

#define EFS_DIRBSHIFT	9
#define EFS_DIRBSIZE	(1<<EFS_DIRBSHIFT)
#define EFS_DIRBMASK	(EFS_DIRBSIZE-1)

#define EFS_DENTSIZE	(sizeof(struct efs_dent) - 3 + 1)
#define EFS_MAX_NAME	255
#define EFS_DIRBLK_HEADERSIZE	4

#define EFS_DIRBLK_MAGIC	0xbeef

struct efs_dirblk {
	uint16_t magic;
	uint8_t  firstused;
	uint8_t  slots;
	uint8_t space[EFS_DIRBSIZE - EFS_DIRBLK_HEADERSIZE];
} __attribute__((packed));

/*
 * Locations of the efs superblock, bitmap and root inode.
 */
#define	EFS_SUPERBB	(1)			/* bb # of the superblock */
#define	EFS_BITMAPBB	(2)			/* bb of the bitmap, pre 3.3*/
#define	EFS_SUPERBOFF	BBTOB(EFS_SUPERBB)	/* superblock byte offset */
#define	EFS_BITMAPBOFF	BBTOB(EFS_BITMAPBB)	/* bitmap byte offset */
#define	EFS_ROOTINO	((efs_ino_t)2)		/* where else... */

/*
 * Inode parameters.
 */
/* number of inodes per bb */
#define	BBSHIFT	9
#define BBSIZE (1<<BBSHIFT)
#define	BBMASK	(BBSIZE - 1)
#define	EFS_INOPBB	(1 << EFS_INOPBBSHIFT)
#define	EFS_INOPBBSHIFT	(BBSHIFT - EFS_EFSINOSHIFT)
#define	EFS_INOPBBMASK	(EFS_INOPBB - 1)

/*
 * Compute the number of inodes-per-cylinder-group (IPCG) and the number
 * of inodes-per-basic-block (INOPBB).
 */
#define	EFS_COMPUTE_IPCG(fs) \
	((short) ((fs)->fs_cgisize << EFS_INOPBBSHIFT))

/*
 * Layout macros.  These macro provide easy access to the layout by
 * translating between sectors, basic blocks, and inode numbers.
 * WARNING: The macro EFS_SETUP_SUPERB must be executed before most
 * of these macros!
 */

/* inode number to bb, relative to cylinder group */
#define	EFS_ITOCGBB(fs, i) \
	((size_t) (((i) >> EFS_INOPBBSHIFT) % (fs)->fs_cgisize))

/* inode number to offset from bb base */
#define	EFS_ITOO(fs, i) \
	((short) ((i) & EFS_INOPBBMASK))

/* inode number to cylinder group */
#define	EFS_ITOCG(fs, i) \
	((short) ((i) / EFS_COMPUTE_IPCG(fs)))

/* inode number to cylinder group inode number offset */
#define	EFS_ITOCGOFF(fs, i) \
	((short) ((i) % EFS_COMPUTE_IPCG(fs)))

/* inode number to disk bb number */
#define	EFS_ITOBB(fs, i) \
	((size_t) ((fs)->fs_firstcg + \
		    (EFS_ITOCG(fs, i) * (fs)->fs_cgfsize) + \
		    EFS_ITOCGBB(fs, i)))

/* bb to cylinder group number */
#define	EFS_BBTOCG(fs, bb) \
	((short) ((bb - (fs)->fs_firstcg) / (fs)->fs_cgfsize))

/* cylinder group number to disk bb of base of cg */
#define	EFS_CGIMIN(fs, cg) \
	((size_t) ((fs)->fs_firstcg + (cg) * (fs)->fs_cgfsize))

/* inode number to base inode number in its chunk */
#define	EFS_ITOCHUNKI(fs, cg, inum) \
	(((((inum) - (cg)->cg_firsti) / (fs)->fs_inopchunk) * \
	  (fs)->fs_inopchunk) + (cg)->cg_firsti)

struct efs_sb efstoh (struct efs_sb efs);
struct efs_dinode efs_dinodetoh(struct efs_dinode inode);

enum efs_fstype {
	EFS_FSTYPE_NONE = 0,
	EFS_FSTYPE_EFS,
	EFS_FSTYPE_VH,
};

typedef struct efs_ctx {
	fileslice_t *fs;
	struct efs_sb sb;
	size_t nblks;
	efs_ino_t ipcg;
} efs_t;

struct efs_dirent {
	efs_ino_t d_ino;
	char d_name[EFS_MAX_NAME + 1];
};

typedef struct efs_dir {

	/*
	 * readdir() returns a pointer to a dirent.
	 * XPG3 says that the data at the pointer can be overwritten
	 * by successive calls to readdir() on the same directory.
	 * Therefore, define a dirent here, and return a pointer to
	 * it for each readdir() call.
	 */
	struct efs_dirent *dirent;

	/* private */
	struct efs_dirent *_dirent_memobj;
	efs_ino_t ino;
} efs_dir_t;

typedef struct efs_file {
	struct efs_dinode dinode;
	unsigned numextents;
	struct efs_extent *exs;
	efs_t *ctx;
	efs_ino_t ino;
	unsigned pos;
	unsigned nbytes;
	bool eof;
	bool error;
	uint8_t blockbuf[BLKSIZ];
	int blocknum;	// set to -1 if blockbuf is not filled
} efs_file_t;

struct efs_stat {
	efs_ino_t st_ino;
	uint16_t st_mode;
	int16_t st_nlink;
	uint16_t st_uid;
	uint16_t st_gid;
	int32_t st_size;
	uint16_t st_major;
	uint16_t st_minor;
	struct timespec st_atimespec;
	struct timespec st_mtimespec;
	struct timespec st_ctimespec;
};

typedef enum {
	EFS_ERR_OK = 0,
	EFS_ERR_INVAL,
	EFS_ERR_NOENT,
	EFS_ERR_NOMEM,
	EFS_ERR_READFAIL,
	EFS_ERR_NOPAR,
	EFS_ERR_NOVH,
	EFS_ERR_BADVH,
	EFS_ERR_SBMAGIC,
	EFS_ERR_PARTYPE,
} efs_err_t;

struct qent_s {
	struct qent_s *next;
	struct qent_s *prev;
	char *path;
};
struct queue_s {
	struct qent_s *head;
	struct qent_s *tail;
};
typedef struct queue_s* queue_t;

#define VDNAMESIZE 8
#define VHMAGIC 0x0be5a941
#define NPARTAB 16
#define NVDIR 15
#define BFNAMESIZE 16
#define NPTYPES 16
#define BLKSIZ 512

enum partition_type_e {
	PT_VOLHDR = 0,
	PT_BSD = 4,
	PT_SYSV = 5,
	PT_VOLUME = 6,
	PT_EFS = 7,
	PT_XFS = 10,
};

struct dvh_dp_s {
	uint8_t  dp_skew;
	uint8_t  dp_gap1;
	uint8_t  dp_gap2;
	uint8_t  dp_spares_cyl;
	uint16_t dp_cyls;
	uint16_t dp_shd0;
	uint16_t dp_trks0;
	uint8_t  dp_ctq_depth;
	uint8_t  dp_cylshi;
	uint16_t dp_unused;
	uint16_t dp_secs;
	uint16_t dp_secbytes;
	uint16_t dp_interleave;
	int32_t  dp_flags;
	int32_t  dp_datarate;
	int32_t  dp_nretries;
	int32_t  dp_mspw;
	uint16_t dp_xgap1;
	uint16_t dp_xsync;
	uint16_t dp_xrdly;
	uint16_t dp_xgap2;
	uint16_t dp_xrgate;
	uint16_t dp_xwcont;
} __attribute__((packed));

struct dvh_vd_s {
	char vd_name[VDNAMESIZE];
	int32_t vd_lbn;
	int32_t vd_nbytes;
} __attribute__((packed));

struct dvh_pt_s {
	int32_t pt_nblks;
	int32_t pt_firstlbn;
	int32_t pt_type;
} __attribute__((packed));

struct dvh_s {
	uint32_t vh_magic;
	uint16_t vh_rootpt;
	uint16_t vh_swappt;
	char     vh_bootfile[BFNAMESIZE];
	struct dvh_dp_s vh_dp;
	struct dvh_vd_s vh_pd[NVDIR];
	struct dvh_pt_s vh_pt[NPARTAB];
	int32_t  vh_csum;
	int32_t  vh_fill;
} __attribute__((packed));

typedef struct dvh_ctx {
	FILE *f;
	struct dvh_s dvh;
} dvh_t;

extern efs_err_t dvh_open(dvh_t **ctx, const char *filename);
extern efs_err_t dvh_close(dvh_t *ctx);
extern fileslice_t *dvh_getParSlice(dvh_t *ctx, int parNum);
extern struct dvh_pt_s dvh_getParInfo(dvh_t *ctx, int parNum);
extern struct dvh_vd_s dvh_getFileInfo(dvh_t *ctx, int fileNum);
extern const char *dvh_getNameForType(unsigned parType);

extern fileslice_t *fsopen(FILE *f, size_t base, size_t size);
extern int fsclose(fileslice_t *fs);
extern int fsseek(fileslice_t *fs, long offset, int whence);
extern void fsrewind(fileslice_t *fs);
extern size_t fsread(void *ptr, size_t size, size_t nmemb, fileslice_t *fs);

extern queue_t queue_init(void);
extern void queue_free(queue_t q);
extern int queue_add_tail(queue_t q, char *path);
extern int queue_add_head(queue_t q, char *path);
extern int queue_add_queue_head(queue_t dst, queue_t src);
extern struct qent_s *queue_dequeue(queue_t q);

extern const char *efs_strerror(efs_err_t e);
extern void vwarnefs(efs_err_t e, const char *fmt, va_list args);
extern void verrefs(int eval, efs_err_t e, const char *fmt, va_list args);
extern void warnefs(efs_err_t e, const char *fmt, ...);
extern void errefs(int eval, efs_err_t e, const char *fmt, ...);

extern efs_file_t *efs_fopen(efs_t *ctx, const char *path);
extern int efs_fclose(efs_file_t *file);
extern size_t efs_fread(void *ptr, size_t size, size_t nmemb, efs_file_t *file);
extern int efs_fseek(efs_file_t *file, long offset, int whence);
extern long efs_ftell(efs_file_t *file);
extern void efs_rewind(efs_file_t *file);
extern void efs_clearerr(efs_file_t *file);
extern int efs_feof(efs_file_t *file);
extern int efs_ferror(efs_file_t *file);

extern int efs_stat(efs_t *ctx, const char *pathname, struct efs_stat *statbuf);
extern int efs_stati(efs_t *ctx, efs_ino_t ino, struct efs_stat *statbuf);
extern int efs_fstat(efs_file_t *file, struct efs_stat *statbuf);

extern efs_ino_t efs_find_entry(efs_t *efs, const char *name);
extern efs_dir_t *efs_opendir(efs_t *efs, const char *dirname);
extern int efs_closedir(efs_dir_t *dirp);
extern struct efs_dirent *efs_readdir(efs_dir_t *dirp);
extern void efs_rewinddir(efs_dir_t *dirp);

extern efs_err_t efs_get_blocks(efs_t *ctx, void *buf, size_t firstlbn, size_t nblks);
extern efs_err_t efs_open(efs_t **ctx, fileslice_t *f);
extern void efs_close(efs_t *ctx);
extern struct efs_dinode efs_get_inode(efs_t *ctx, unsigned ino);

extern efs_ino_t efs_namei(efs_t *ctx, const char *name);

extern struct efs_dirent *_efs_read_dirblks(efs_t *ctx, efs_ino_t ino);

extern uint32_t efs_extent_get_bn(struct efs_extent extent);
extern uint32_t efs_extent_get_offset(struct efs_extent extent);

extern char *mkpath(char *path, char *name);
extern int efs_nftw(
	efs_t *efs,
	const char *dirpath,
	int (*fn)(const char *fpath, const struct efs_stat *sb)
);
