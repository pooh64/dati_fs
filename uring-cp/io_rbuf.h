#ifndef _IO_RBUF_H
#define _IO_RBUF_H

#include <stddef.h>
#include <stdint.h>
#include <sys/uio.h>
#include "common.h"

/* i/o request */
struct io_req {
	enum io_req_type {
		IO_REQ_PREAD,
		IO_REQ_PWRITE,
	}			type;
	int			fd;
	struct {
		struct iovec	iov;
		off_t		offs;
		struct iovec	__submit_iov;
		off_t		__submit_offs;
	};
	int32_t			res;
	unsigned		ready : 1;
};

static inline
void __io_req_prep_prw(struct io_req *r, enum io_req_type op,
		int fd, void *buf, size_t count, off_t offs)
{
	r->type = op;
	r->fd = fd;
	r->iov.iov_base = buf;
	r->iov.iov_len = count;
	r->offs = offs;
	r->res = 0;
	r->ready = 0;
}

#define roundup(x, y) ({			\
	const typeof(y) __y = (y);		\
	(((x) + (__y - 1)) / __y) * __y;	\
})

static inline
void io_req_make_aligned(struct io_req *r, size_t align)
{
	release_assert((uintptr_t) r->iov.iov_base % align == 0);
	release_assert(r->offs % align == 0);
	r->__submit_iov.iov_base = r->iov.iov_base;
	r->__submit_iov.iov_len = roundup(r->iov.iov_len, align);
	r->__submit_offs = r->offs;
}

static inline
void io_req_prep_pread(struct io_req *r,
		int fd, void *buf, size_t count, off_t offs)
{
	__io_req_prep_prw(r, IO_REQ_PREAD, fd, buf, count, offs);
}

static inline
void io_req_prep_pwrite(struct io_req *r,
		int fd, void *buf, size_t count, off_t offs)
{
	__io_req_prep_prw(r, IO_REQ_PWRITE, fd, buf, count, offs);
}


/* Circular buffer */
struct io_rbuf {
	struct io_req	*buf;
	size_t		in;
	size_t		out;
	size_t		mask;
};

void io_rbuf_init(struct io_rbuf *rb, size_t cap);
void io_rbuf_destroy(struct io_rbuf *rb);

static inline
size_t io_rbuf_len(struct io_rbuf *rb)
{
	return rb->in - rb->out;
}

static inline
int io_rbuf_full(struct io_rbuf *rb)
{
	return io_rbuf_len(rb) > rb->mask;
}

static inline
int io_rbuf_empty(struct io_rbuf *rb)
{
	return rb->in == rb->out;
}

static inline
struct io_req *io_rbuf_push(struct io_rbuf *rb)
{
	release_assert(!io_rbuf_full(rb));

	return &rb->buf[rb->in++ & rb->mask];
}

static inline
struct io_req *io_rbuf_pop(struct io_rbuf *rb)
{
	release_assert(!io_rbuf_empty(rb));

	return &rb->buf[rb->out++ & rb->mask];
}

static inline
struct io_req *io_rbuf_peek(struct io_rbuf *rb)
{
	release_assert(!io_rbuf_empty(rb));

	return &rb->buf[rb->out & rb->mask];
}

static inline
int io_rbuf_ready(struct io_rbuf *rb)
{
	if (io_rbuf_empty(rb))
		return 0;

	return rb->buf[rb->out & rb->mask].ready;
}

#endif /* _IO_RBUF_H */
