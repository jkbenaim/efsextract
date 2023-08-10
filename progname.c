#include <string.h>
#include "progname.h"

#ifdef __MINGW32__
char *__progname = NULL;
#endif

void progname_init(int argc, char *argv[])
{
#ifdef __MINGW32__
	char *temp;
	__progname = strrchr(argv[0], '\\');
	if (!__progname) __progname = argv[0];
	else __progname++;
	temp = strrchr(argv[0], '.');
	if (temp) *temp = '\0';
#endif
}
