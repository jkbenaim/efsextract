#pragma once

#include <errno.h>
#include <string.h>

#if defined(__MINGW32__) || defined(__sgi)
#include <stdio.h>
#define warn(...) do {fprintf(stderr, __VA_ARGS__); fprintf(stderr, ": %s\n", strerror(errno));} while (0);
#define warnx(...) do {fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n");} while (0);
#define err(eval, ...) do {fprintf(stderr, __VA_ARGS__); fprintf(stderr, ": %s\n", strerror(errno)); exit(eval);} while (0);
#define errx(eval, ...) do {fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); exit(eval);} while (0);
#else
#include <err.h>
#endif
