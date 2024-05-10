#include <string.h>
#include "progname.h"

#if defined(__MINGW32__) || defined(__sgi)
char *__progname = NULL;
#endif

#if defined(__MINGW32__)
const char dirsep = '\\';
#else
const char dirsep = '/';
#endif

void progname_init(int argc, char *argv[])
{
#if defined(__MINGW32__) || defined(__sgi)
	char *temp;
	__progname = strrchr(argv[0], dirsep);
	if (!__progname) __progname = argv[0];
	else __progname++;
	temp = strrchr(argv[0], '.');
	if (temp) *temp = '\0';
#endif
}
