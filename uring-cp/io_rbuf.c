#include <stdlib.h>
#include <errno.h>

#include "io_rbuf.h"

int io_rbuf_init(struct io_rbuf *rb, size_t cap)
{
	if (cap == 0 || (cap & (cap - 1)))
		return -EINVAL;

	if (!(rb->buf = malloc(sizeof(*rb->buf) * cap)))
		return -errno;

	rb->head = 0;
	rb->tail = 0;
	rb->mask = cap - 1;
	rb->full = 0;
	return 0;
}

void io_rbuf_destroy(struct io_rbuf *rb)
{
	free(rb->buf);
}
