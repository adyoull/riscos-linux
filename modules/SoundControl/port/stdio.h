/* stdio.h: what SoundControl uses (port.c). Copyright (c) 2026 Andrew Youll. MIT. */
#ifndef SC_STDIO_H
#define SC_STDIO_H
#include <stddef.h>
int sprintf (char *buf, const char *fmt, ...);
int sscanf (const char *s, const char *fmt, ...);
#endif
