#ifndef _IO_RBUF_H
#define _IO_RBUF_H

#include <stddef.h>
#include <stdint.h>
#include <sys/uio.h>

struct io_req {
	enum io_req_type {
		IO_REQ_PREAD,
		IO_REQ_PWRITE,
	}			type;
	int			fd;
	struct iovec 		iov;
	void			*buf_start;
	off_t			offs;
	int			errc;
	unsigned		ready : 1;
};

static inline
void io_req_restore_offs(struct io_req *r)
{
	intptr_t diff = r->iov.iov_base - r->buf_start;
	r->iov.iov_base	-= diff;
	r->iov.iov_len	+= diff;
	r->offs		-= diff;
}

static inline
void io_req_prep_pread(struct io_req *r, int fd,
		void *buf, size_t count, off_t offs)
{
	r->type = IO_REQ_PREAD;
	r->fd = fd;
	r->iov.iov_base = buf;
	r->iov.iov_len = count;
	r->buf_start = buf;
	r->offs = offs;
	r->errc = 0;
	r->ready = 0;
}

static inline
void io_req_prep_pwrite(struct io_req *r, int fd,
		void *buf, size_t count, off_t offs)
{
	io_req_prep_pread(r, fd, buf, count, offs);
	r->type = IO_REQ_PWRITE;
}

struct io_rbuf {
	struct io_req	*buf;
	size_t		head;
	size_t		tail;
	size_t		mask;
	unsigned	full : 1;
};

int io_rbuf_init(struct io_rbuf *rb, size_t cap);
void io_rbuf_destroy(struct io_rbuf *rb);

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
struct io_req *io_rbuf_push(struct io_rbuf *rb)
{
	if (io_rbuf_full(rb))
		return NULL;

	struct io_req *req = &rb->buf[rb->head];
	__io_rbuf_move_head(rb);
	return req;
}

static inline
struct io_req *io_rbuf_pop(struct io_rbuf *rb)
{
	if (io_rbuf_empty(rb))
		return NULL;

	struct io_req *req = &rb->buf[rb->tail];
	__io_rbuf_move_tail(rb);
	return req;
}

static inline
int io_rbuf_ready(struct io_rbuf *rb)
{
	if (io_rbuf_empty(rb))
		return 0;
	return rb->buf[rb->tail].ready;
}

#endif /* _IO_RBUF_H */
