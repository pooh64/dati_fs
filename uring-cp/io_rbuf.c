#include <stdlib.h>
#include <errno.h>

#include "io_rbuf.h"
#include "common.h"

void io_rbuf_init(struct io_rbuf *rb, size_t cap)
{
	release_assert(cap != 0);
	release_assert(!(cap & (cap - 1)));

	rb->buf = xmalloc(sizeof(*rb->buf) * cap);

	rb->in = 0;
	rb->out = 0;
	rb->mask = cap - 1;
}

void io_rbuf_destroy(struct io_rbuf *rb)
{
	free(rb->buf);
}
