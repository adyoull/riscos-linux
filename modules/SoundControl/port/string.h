/* string.h: what SoundControl uses (port.c). Copyright (c) 2026 Andrew Youll. MIT. */
#ifndef SC_STRING_H
#define SC_STRING_H
#include <stddef.h>
char *strncpy (char *d, const char *s, size_t n);
size_t strlen (const char *s);
void *memcpy (void *d, const void *s, size_t n);
void *memset (void *d, int c, size_t n);
#endif
