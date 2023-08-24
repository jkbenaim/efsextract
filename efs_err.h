#pragma once

#include "stdarg.h"

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

extern const char *efs_strerror(efs_err_t e);
extern void vwarnefs(efs_err_t e, const char *fmt, va_list args);
extern noreturn void verrefs(int eval, efs_err_t e, const char *fmt, va_list args);
extern void warnefs(efs_err_t e, const char *fmt, ...);
extern noreturn void errefs(int eval, efs_err_t e, const char *fmt, ...);