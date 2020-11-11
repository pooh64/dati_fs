#include <stdlib.h>
#include <stdio.h>

#include "common.h"

void *xmalloc(size_t sz)
{
	void *rc = malloc(sz);
	release_assert(rc);
	return rc;
}

void *xmemalign(size_t align, size_t sz)
{
	void *buf;
	int rc = posix_memalign(&buf, align, sz);
	release_assert(!rc);
	return buf;
}

void __release_assert(char const *file, int line, char const *descr)
{
	fprintf(stderr, "Assertion failed:\n%s:%d: %s\n", file, line, descr);
	abort();
}
