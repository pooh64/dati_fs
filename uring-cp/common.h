#ifndef _COMMON_H
#define _COMMON_H

#include <stddef.h>
#include <stdint.h>
#include <error.h>

void *xmalloc(size_t sz);

void __release_assert(char const *file, int line, char const *desr);

#define release_assert(expr)	do {				\
	if (!(expr))						\
		__release_assert(__FILE__, __LINE__, #expr);	\
	} while (0)

#define err_display(errc, fmt, ...) error(0, (int) (errc), fmt, ##__VA_ARGS__)

#define ptr_add(ptr, val) ((void*) ((uint8_t*) (ptr) + (val)))

#endif /* _COMMON_H */
