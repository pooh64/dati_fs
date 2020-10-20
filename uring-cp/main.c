#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <error.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include "liburing.h"

#include "io_rbuf.h"
#include "common.h"

#define WQ_CAP 8
#define RQ_CAP 8
#define URING_IO_BLOCK (1024L * 128L)

#define err_display(errc, fmt, ...) error(0, (int) (errc), fmt, ##__VA_ARGS__)

struct uring_marena {
	uint8_t	*arena;
	void	**free_blocks;
	size_t	n_blocks;
	size_t	n_free;
	size_t	block_sz;
};

struct uring_context {
	struct io_uring		uring;
	struct io_rbuf		rq;
	struct io_rbuf		wq;
	struct uring_marena	ma;
	unsigned		rq_cap;
	unsigned		wq_cap;
};

static
void uring_marena_init(struct uring_marena *ma,
		size_t n_blocks, size_t block_sz)
{
	int rc = posix_memalign((void**) &ma->arena, block_sz, n_blocks * block_sz);
	release_assert(!rc);

	ma->free_blocks = xmalloc(sizeof(*ma->free_blocks) * n_blocks);

	for (size_t i = 0; i < n_blocks; ++i)
		ma->free_blocks[i] = &ma->arena[i * block_sz];

	ma->n_blocks = n_blocks;
	ma->n_free = n_blocks;
	ma->block_sz = block_sz;
}

static inline
size_t uring_marena_block_sz(struct uring_marena *ma)
{
	return ma->block_sz;
}

static
void uring_marena_destroy(struct uring_marena *ma)
{
	free(ma->arena);
	free(ma->free_blocks);
}

static
void *uring_marena_alloc(struct uring_marena *ma)
{
	release_assert(ma->n_free);
	void *buf = ma->free_blocks[--(ma->n_free)];
	printf("allocate: %p\n", buf);
	return buf; 
}

static
void uring_marena_free(struct uring_marena *ma, void *ptr)
{
	printf("free: %p\n", ptr);
	uintptr_t _ptr = (uintptr_t) ptr;
	release_assert(_ptr % ma->block_sz == 0);
	_ptr = _ptr / ma->block_sz;
	uintptr_t _beg = (uintptr_t) ma->arena / ma->block_sz;

	release_assert(_ptr >= _beg);
	release_assert((_ptr - _beg) < ma->n_blocks);

	release_assert(ma->n_free < ma->n_blocks);
	ma->free_blocks[(ma->n_free)++] = ptr;
}

static
int uring_context_init(struct uring_context *c, unsigned rq_cap,
		unsigned wq_cap, size_t io_block_sz)
{
	int rc;
	if ((rq_cap + wq_cap) < rq_cap)
		return -EOVERFLOW;
	c->rq_cap = rq_cap;
	c->wq_cap = wq_cap;

	if ((rc = io_uring_queue_init(rq_cap + wq_cap, &c->uring, 0)) < 0)
		return rc;

	io_rbuf_init(&c->rq, rq_cap);
	io_rbuf_init(&c->wq, wq_cap);
	uring_marena_init(&c->ma, wq_cap + rq_cap, io_block_sz);

	return 0;
}

static
void uring_context_destroy(struct uring_context *c)
{
	io_uring_queue_exit(&c->uring);

	io_rbuf_destroy(&c->rq);
	io_rbuf_destroy(&c->wq);

	uring_marena_destroy(&c->ma);
}

static inline
void __uring_context_queue(struct uring_context *c, struct io_req *req, int alloc)
{
	struct io_uring_sqe *sqe;
	struct io_req *sr = req;
	struct io_rbuf *q;
	int op;

	switch (req->type) {
		case IO_REQ_PREAD:
			op = IORING_OP_READV;
			q = &c->rq;
			break;
		case IO_REQ_PWRITE:
			op = IORING_OP_WRITEV;
			q = &c->wq;
			break;
		default:
			release_assert(!"wrong opcode");
	}

	release_assert(alloc && !io_rbuf_full(q));
	sqe = io_uring_get_sqe(&c->uring);
	release_assert(sqe);
	if (alloc) {
		sr = io_rbuf_push(q);
		*sr = *req;
	}
	io_req_make_aligned(sr, uring_marena_block_sz(&c->ma));
	io_uring_prep_rw(op, sqe, sr->fd, &sr->__submit_iov, 1,
			sr->__submit_offs);

	io_uring_sqe_set_data(sqe, sr);
}

static
void uring_context_req_queue(struct uring_context *c, struct io_req *req)
{
	__uring_context_queue(c, req, 1);
}

static
void uring_context_req_restart(struct uring_context *c, struct io_req *req)
{
	int32_t tmp = req->res;
	__uring_context_queue(c, req, 0);
	req->res = tmp;
}

static
int uring_context_submit(struct uring_context *c)
{
	return io_uring_submit(&c->uring);
}

static
int uring_context_req_wait(struct uring_context *c)
{
	int rc;
	struct io_uring_cqe *cqe;
	struct io_req *req;
	size_t io_sz = uring_marena_block_sz(&c->ma);

	int cqe_obtained = 0;
again:
	if (!cqe_obtained) {
		if ((rc = io_uring_wait_cqe(&c->uring, &cqe)) < 0)
			return rc;
		cqe_obtained = 1;
	} else if ((rc = io_uring_peek_cqe(&c->uring, &cqe)) < 0) {
		return rc;
	}

	req = io_uring_cqe_get_data(cqe);

	if (cqe->res < 0) {
		if (cqe->res == -EAGAIN) {
			release_assert(!"not tested");
			uring_context_req_restart(c, req);
			io_uring_cqe_seen(&c->uring, cqe);
			goto again;
		}
		return req->res = cqe->res;
	}

	int32_t res_round = roundup(cqe->res, io_sz);	
	if (res_round != req->__submit_iov.iov_len) {
		release_assert(0);
		req->__submit_iov.iov_base += res_round;
		req->__submit_iov.iov_len  -= res_round;
		req->__submit_offs	   += res_round;
		req->res		   += res_round;
		uring_context_req_restart(c, req);
		io_uring_cqe_seen(&c->uring, cqe);
		goto again;
	}

	req->res += cqe->res;
	io_uring_cqe_seen(&c->uring, cqe);
	req->ready = 1;
	return 0;
}

static
void copy_file_read(struct uring_context *c, int infd, off_t *in_offs, size_t copy_sz)
{
	release_assert(*in_offs < copy_sz);
	struct io_req req;
	size_t io_sz = uring_marena_block_sz(&c->ma);
	size_t len = (*in_offs + io_sz > copy_sz) ? copy_sz - *in_offs : io_sz;
	void *buf = uring_marena_alloc(&c->ma);
	printf("prep_read: buf=%p len=%8.8lu offs=%8.8lu\n", buf, len, *in_offs);
	io_req_prep_pread(&req, infd, buf, len, *in_offs);
	*in_offs += len;
	uring_context_req_queue(c, &req);
}

static
void copy_file_write(struct uring_context *c, int outfd, struct io_req *req_r)
{
	struct io_req req_new;
	io_req_prep_pwrite(&req_new, outfd, req_r->iov.iov_base,
			req_r->iov.iov_len, req_r->offs);
	uring_context_req_queue(c, &req_new);
}

static
int copy_file(struct uring_context *c, int infd, int outfd, size_t copy_sz)
{
	int rc;
	off_t in_offs = 0;

	for (int i = 0; i < c->rq_cap && in_offs < copy_sz; ++i)
		copy_file_read(c, infd, &in_offs, copy_sz);

	while (1) {
		if ((rc = uring_context_submit(c)) < 0)
			goto errout;
		if ((rc = uring_context_req_wait(c)) < 0) {
			if (rc == -EAGAIN) {
				fprintf(stderr, "req_wait needs restart\n");
				continue;
			}
			printf("%d\n", __LINE__);
			goto errout;
		}

		while (io_rbuf_ready(&c->wq)) {
			struct io_req *req = io_rbuf_pop(&c->wq);
			uring_marena_free(&c->ma, req->iov.iov_base);
			printf("successfull write: offs=%8.8lu\n", req->offs);
			if (req->iov.iov_len + req->offs >= copy_sz)
				goto completed;
		}
		while (io_rbuf_ready(&c->rq) && !io_rbuf_full(&c->wq)) {
			copy_file_write(c, outfd, io_rbuf_peek(&c->rq));
			io_rbuf_pop(&c->rq);
			if (in_offs < copy_sz)
				copy_file_read(c, infd, &in_offs, copy_sz);
		}
	}
completed:
	if ((rc = ftruncate(outfd, copy_sz)) < 0) {
		rc = -errno;
		goto errout;
	}
	return 0;
errout:
	return rc;
}

static int get_file_size(int fd, off_t *size)
{
	struct stat st;
	if (fstat(fd, &st) < 0)
		return -errno;
	if (S_ISREG(st.st_mode)) {
		*size = st.st_size;
		return 0;
	}
	return -EINVAL;
}

int main(int argc, char **argv)
{
	int rc;
	struct uring_context context;
	int infd, outfd;
	off_t copy_size;

	if (argc != 3) {
		fprintf(stderr, "usage: %s <infile> <outfile>\n", argv[0]);
		return 1;
	}

	if ((infd = open(argv[1], O_RDONLY | O_DIRECT)) < 0) {
		err_display(errno, "open infile");
		return 1;
	}

	if ((rc = get_file_size(infd, &copy_size)) < 0) {
		err_display(-rc, "get_file_size");
		return 1;
	}

	if ((outfd = open(argv[2], O_CREAT | O_WRONLY | O_TRUNC | O_DIRECT,
			0644)) < 0) {
		err_display(errno, "creat outfile");
		return 1;
	}

	if ((rc = fallocate(outfd, 0, 0, copy_size)) < 0) {
		err_display(errno, "fallocate");
		return 1;
	}

	if ((rc = uring_context_init(&context,
			RQ_CAP, WQ_CAP, URING_IO_BLOCK)) < 0) {
		err_display(-rc, "uring_context_init");
		return 1;
	}

	if ((rc = copy_file(&context, infd, outfd, copy_size)) < 0) {
		err_display(-rc, "copy_file");
		return 1;
	}

	close(infd);
	close(outfd);
	uring_context_destroy(&context);

	return 0;
}
