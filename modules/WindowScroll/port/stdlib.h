#ifndef WS_STDLIB_H
#define WS_STDLIB_H
#include <stddef.h>
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
void *malloc (size_t n);          /* from the RMA */
void free (void *p);
void exit (int status) __attribute__((noreturn));
int atexit (void (*fn) (void));
#endif
