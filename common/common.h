#ifndef _COMMON_H
#define _COMMON_H

#include <stddef.h>
#include <stdint.h>
#include <error.h>

#define div_rup(x, y) ({		\
	const typeof(y) __y = y;	\
	((x) + (__y - 1)) / __y;	\
})

#define min(x, y) ({                    \
        typeof(x) __x = (x);            \
        typeof(y) __y = (y);            \
        (__x < __y) ? __x : __y;  })

#define BUILD_BUG_ON_ZERO(e)	(sizeof(struct { int:-!!(e); }))
#define __same_type(a, b)	__builtin_types_compatible_p(typeof(a), typeof(b))
#define __must_be_array(a)	BUILD_BUG_ON_ZERO(__same_type((a), &(a)[0]))
#define ARRAY_SIZE(arr)		(sizeof(arr) / sizeof((arr)[0]) + __must_be_array(arr))

#define err_display(errc, fmt, ...) error(0, (int) (errc), fmt, ##__VA_ARGS__)

#define ptr_add(ptr, val) ((void*) ((uint8_t*) (ptr) + (val)))

#define release_assert(expr)	do {				\
	if (!(expr))						\
		__release_assert(__FILE__, __LINE__, #expr);	\
	} while (0)

void *xmalloc(size_t sz);
void *xmemalign(size_t align, size_t sz);

void __release_assert(char const *file, int line, char const *desr);

#endif /* _COMMON_H */
