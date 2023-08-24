#include <stdio.h>
#include <stdlib.h>
#include "efs_err.h"
#include "progname.h"

const char *efs_strerror(efs_err_t e)
{
	switch (e) {
		case EFS_ERR_OK:
			return "no error";
		case EFS_ERR_INVAL:
			return "invalid argument";
		case EFS_ERR_NOENT:
			return "no such file or directory";
		case EFS_ERR_NOMEM:
			return "out of memory";
		case EFS_ERR_READFAIL:
			return "read error";
		case EFS_ERR_NOPAR:
			return "requested partition not found";
		case EFS_ERR_NOVH:
			return "volume header not found";
		case EFS_ERR_BADVH:
			return "volume header checksum failure";
		case EFS_ERR_SBMAGIC:
			return "superblock not found";
		case EFS_ERR_PARTYPE:
			return "unrecognized partition type";
		default:
			return "";
	}
}

void vwarnefs(efs_err_t e, const char *fmt, va_list args)
{
	fprintf(stderr, "%s: ", __progname);
	if (fmt) {
		vfprintf(stderr, fmt, args);
		fprintf(stderr, ": ");
	}
	fprintf(stderr, "%s\n", efs_strerror(e));
}

void verrefs(int eval, efs_err_t e, const char *fmt, va_list args)
{
	vwarnefs(e, fmt, args);
	exit(eval);
}

void warnefs(efs_err_t e, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vwarnefs(e, fmt, ap);
	va_end(ap);
}

void errefs(int eval, efs_err_t e, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	verrefs(eval, e, fmt, ap);
	va_end(ap);
}
