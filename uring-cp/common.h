#ifndef _COMMON_H
#define _COMMON_H

#include <stddef.h>

void *xmalloc(size_t sz);

void __release_assert(char const *file, int line, char const *desr);

#define release_assert(expr)	do {				\
	if (!(expr))						\
		__release_assert(__FILE__, __LINE__, #expr);	\
	} while (0)

#endif /* _COMMON_H */
