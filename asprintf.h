#pragma once

#ifndef vasprintf
#include <stdarg.h>
int vasprintf(char **strp, const char *fmt, va_list ap);
#endif

#ifndef asprintf
int asprintf(char **strp, const char *fmt, ...);
#endif
