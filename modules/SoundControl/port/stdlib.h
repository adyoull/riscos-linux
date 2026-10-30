/* stdlib.h: what SoundControl uses (port.c). Copyright (c) 2026 Andrew Youll. MIT. */
#ifndef SC_STDLIB_H
#define SC_STDLIB_H
#include <stddef.h>
void *malloc (size_t n);
void free (void *p);
int atoi (const char *s);
#endif
