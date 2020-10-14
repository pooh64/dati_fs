#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <error.h>
#include <assert.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include "liburing.h"

#include "io_rbuf.h"

#define WQ_CAP 8
#define RQ_CAP 8
#define IO_BUF_SZ (64)

#define err_display(errc, fmt, ...) error(0, (int) (errc), fmt, ##__VA_ARGS__)

struct uring_context {
	struct io_uring	uring;
	struct io_rbuf	rq;
	struct io_rbuf	wq;
	unsigned	rq_cap;
	unsigned	wq_cap;
};

static
int uring_context_init(struct uring_context *c, unsigned rq_cap,
		unsigned wq_cap)
{
	int rc;
	if ((rq_cap + wq_cap) < rq_cap)
		return -EOVERFLOW;
	c->rq_cap = rq_cap;
	c->wq_cap = wq_cap;

	if ((rc = io_uring_queue_init(rq_cap + wq_cap, &c->uring, 0)) < 0)
		goto errout;

	if ((rc = io_rbuf_init(&c->rq, rq_cap)) < 0)
		goto err_free_uring;

	if ((rc = io_rbuf_init(&c->wq, wq_cap)) < 0)
		goto err_free_rq;

	return 0;

err_free_rq:
	io_rbuf_destroy(&c->rq);
err_free_uring:
	io_uring_queue_exit(&c->uring);
errout:
	return rc;
}

static
void uring_context_destroy(struct uring_context *c)
{
	io_uring_queue_exit(&c->uring);

	struct io_req *req;

	while ((req = io_rbuf_pop(&c->rq)))
		free(req->buf_start);

	while ((req = io_rbuf_pop(&c->wq)))
		free(req->buf_start);

	io_rbuf_destroy(&c->rq);
	io_rbuf_destroy(&c->wq);
}

static inline
int __uring_context_queue(struct uring_context *c, struct io_req *req, int alloc)
{
	struct io_uring_sqe *sqe;
	struct io_req *sr = req;

	switch (req->type) {
		case IO_REQ_PREAD:
			if (alloc && io_rbuf_full(&c->rq))
				return -ENOBUFS;
			if (!(sqe = io_uring_get_sqe(&c->uring)))
				return -ENOBUFS;
			if (alloc) {
				sr = io_rbuf_push(&c->rq);
				*sr = *req;
			}
			io_uring_prep_readv(sqe, sr->fd, &sr->iov, 1, sr->offs);
			break;
		case IO_REQ_PWRITE:
			if (alloc && io_rbuf_full(&c->wq))
				return -ENOBUFS;
			if (!(sqe = io_uring_get_sqe(&c->uring)))
				return -ENOBUFS;
			if (alloc) {
				sr = io_rbuf_push(&c->wq);
				*sr = *req;
			}
			io_uring_prep_writev(sqe, sr->fd, &sr->iov, 1, sr->offs);
			break;
		default:
			return -EINVAL;
	}
	io_uring_sqe_set_data(sqe, sr);
	return 0;
}

static
int uring_context_req_queue(struct uring_context *c, struct io_req *req)
{
	return __uring_context_queue(c, req, 1);
}

static
int uring_context_req_restart(struct uring_context *c, struct io_req *req)
{
	return __uring_context_queue(c, req, 0);
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

	int cqe_obtained = 0;
again:
	if (!cqe_obtained) {
		if ((rc = io_uring_wait_cqe(&c->uring, &cqe)) < 0)
			return rc;
		cqe_obtained = 1;
	} else {
		if ((rc = io_uring_peek_cqe(&c->uring, &cqe)) < 0) {
			if (rc == -EAGAIN)
				return rc;
			return rc;
		}
	}

	req = io_uring_cqe_get_data(cqe);

	if (cqe->res < 0) {
		if (cqe->res == -EAGAIN) {
			assert(!"0_0 - not tested branch");
			if ((rc = uring_context_req_restart(c, req)) < 0)
				return req->errc = rc;
			io_uring_cqe_seen(&c->uring, cqe);
			goto again;
		}
		return req->errc = cqe->res;
	}
	
	if (cqe->res != req->iov.iov_len) {
		req->iov.iov_base += cqe->res;
		req->iov.iov_len  -= cqe->res;
		req->offs	  += cqe->res;
		if ((rc = uring_context_req_restart(c, req)) < 0) {
			return req->errc = rc;
		}
		io_uring_cqe_seen(&c->uring, cqe);
		goto again;
	}

	io_uring_cqe_seen(&c->uring, cqe);
	io_req_restore_offs(req);
	req->ready = 1;
	return 0;
}

static
int copy_file_read(struct uring_context *c, int infd, off_t *in_offs, size_t copy_sz)
{
	int rc;
	struct io_req req;
	size_t len = (*in_offs + IO_BUF_SZ > copy_sz) ?
		copy_sz - *in_offs : IO_BUF_SZ;
	void *buf = malloc(len);
	if (!buf)
		return -errno;
	io_req_prep_pread(&req, infd, buf, len, *in_offs);
	*in_offs += len;
	if ((rc = uring_context_req_queue(c, &req)) < 0) {
		free(buf);
		return rc;
	}
	return 0;
}

static
int copy_file_write(struct uring_context *c, int outfd, struct io_req *req_r)
{
	struct io_req req_new;
	io_req_prep_pwrite(&req_new, outfd, req_r->iov.iov_base,
			req_r->iov.iov_len, req_r->offs);
	return uring_context_req_queue(c, &req_new);
}

static
int copy_file(struct uring_context *c, int infd, int outfd, size_t copy_sz)
{
	int rc;
	off_t in_offs = 0;

	for (int i = 0; i < c->rq_cap && in_offs < copy_sz; ++i) {
		if ((rc = copy_file_read(c, infd, &in_offs, copy_sz)) < 0)
			goto errout;
	}

	while (1) {
		if ((rc = uring_context_submit(c)) < 0)
			goto errout;
		if ((rc = uring_context_req_wait(c)) < 0)
			goto errout;

		while (io_rbuf_ready(&c->wq)) {
			struct io_req *req = io_rbuf_pop(&c->wq);
			free(req->buf_start);
			printf("successfull write: offs=%8.8lu\n", req->offs);
			if (req->iov.iov_len + req->offs >= copy_sz)
				goto completed;
		}
		while (io_rbuf_ready(&c->rq)) {
			rc = copy_file_write(c, outfd, io_rbuf_peek(&c->rq));
			if (rc < 0) {
				if (rc == -ENOBUFS)	/* currently no free space */
					break;
			}
			io_rbuf_pop(&c->rq);
			if (in_offs < copy_sz) {
				if ((rc = copy_file_read(c, infd, &in_offs, copy_sz)) < 0)
					goto errout;
			}
		}
	}
completed:
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

	if ((infd = open(argv[1], O_RDONLY)) < 0) {
		err_display(errno, "open infile");
		return 1;
	}

	if ((rc = get_file_size(infd, &copy_size)) < 0) {
		err_display(-rc, "get_file_size");
		return 1;
	}

	if ((outfd = open(argv[2], O_CREAT | O_WRONLY | O_TRUNC,
			0644)) < 0) {
		err_display(errno, "creat outfile");
		return 1;
	}

	if ((rc = uring_context_init(&context, RQ_CAP, WQ_CAP)) < 0) {
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
