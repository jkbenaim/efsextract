#define _GNU_SOURCE
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "asprintf.h"
#include "err.h"

#if defined(__sgi)
int vasprintf(char **strp, const char *fmt, va_list ap)
{
	__label__ out_error;
	int n;
	size_t size = 0;
	size_t bufsize = 0;

	/*
	 * IRIX's libc has a broken implementation of vsnprintf.
	 * The return value is supposed to be the number of characters
	 * that would actually have been written if the buffer was
	 * big enough. IRIX's version instead returns the number
	 * of characters actually written. This means we can't rely
	 * on vsnprintf to tell us how long a formatted string will
	 * be, and we simply have to choose a buffer large enough.
	 */
#if defined(__sgi)
	bufsize = 1024;
#endif

	*strp = malloc(bufsize);
	if (!*strp) goto out_error;

	n = vsnprintf(*strp, bufsize, fmt, ap);
	if (n < 0) goto out_error;

	size = (size_t) n + 1;

	if (size > bufsize) {
		*strp = realloc(*strp, size);
		if (!*strp) goto out_error;
		n = vsnprintf(*strp, bufsize, fmt, ap);
		if (n < 0) goto out_error;
	}

	return n;

out_error:
	free(*strp);
	*strp = NULL;
	return -1;
}

int asprintf(char **strp, const char *fmt, ...)
{
	int rc;
	va_list ap;
	va_start(ap, fmt);
	rc = vasprintf(strp, fmt, ap);
	va_end(ap);
	return rc;
}
#endif
