#ifndef _IO_RBUF_H
#define _IO_RBUF_H

#include <stddef.h>
#include <sys/uio.h>

struct io_req {
	struct iovec 		iov;
	enum io_req_type {
		IO_REQ_READ,
		IO_REQ_WRITE,
	}			type;
	unsigned		ready : 1;
};

struct io_rbuf {
	struct io_req	*buf;
	size_t		head;
	size_t		tail;
	size_t		mask;
	unsigned	full : 1;
};

static inline
int io_rbuf_full(struct io_rbuf *rb)
{
	return rb->full;
}

static inline
int io_rbuf_empty(struct io_rbuf *rb)
{
	return (!rb->full) && (rb->head == rb->tail);
}

static inline
void __io_rbuf_move_head(struct io_rbuf *rb)
{
	rb->head = (rb->head + 1) & rb->mask;
	if (rb->head == rb->tail)
		rb->full = 1;
}

static inline
void __io_rbuf_move_tail(struct io_rbuf *rb)
{
	if (rb->full)
		rb->full = 0;
	rb->tail = (rb->tail + 1) & rb->mask;
}

static inline
struct io_req *io_rbuf_put(struct io_rbuf *rb)
{
	if (io_rbuf_full(rb))
		return NULL;

	struct io_req *req = &rb->buf[rb->head];
	__io_rbuf_move_head(rb);
	return req;
}

static inline
struct io_req *io_rbuf_get(struct io_rbuf *rb)
{
	if (io_rbuf_empty(rb))
		return NULL;

	struct io_req *req = &rb->buf[rb->tail];
	__io_rbuf_move_tail(rb);
	return req;
}

static inline
struct io_req *io_rbuf_get_ready(struct io_rbuf *rb)
{
	if (!rb->buf[rb->tail].ready)
		return NULL;
	return io_rbuf_get(rb);
}

#endif /* _IO_RBUF_H */
