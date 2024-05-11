#pragma once

#if defined(__sgi)
#include <stdarg.h>
int vasprintf(char **strp, const char *fmt, va_list ap);
int asprintf(char **strp, const char *fmt, ...);
#endif
