#define FUSE_USE_VERSION 30

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>
#include <stddef.h>
#include <linux/limits.h>

#include "common.h"
#include "e2img.h"

/* FUSE options: show_help */
static struct options {
	int show_help;
	char *img_path;
} g_options;

struct e2img g_img;

#define OPTION(t, p) { t, offsetof(struct options, p), 1 }
static const struct fuse_opt g_option_spec[] = {
	OPTION("-h", show_help),
	OPTION("--help", show_help),
	OPTION("--img=%s", img_path),
	FUSE_OPT_END,
};
#undef OPTION

static void *e2fs_init(struct fuse_conn_info *conn, struct fuse_config *cfg)
{
	cfg->kernel_cache = 1; /* Data never changed externally */
	return NULL;
}

static int e2fs_obtain_ino(const char *path, struct fuse_file_info *fi, ext2_ino_t *ino)
{
	int rc;
	if (fi)
		*ino = fi->fh;
	else if ((rc = e2img_path_lookup(&g_img, path, ino)) < 0)
		return rc;
	return 0;
}

static int e2fs_getattr(const char *path, struct stat *stbuf,
			 struct fuse_file_info *fi)
{
	int rc;
	ext2_ino_t ino;
	struct ext2_inode inode;
	if ((rc = e2fs_obtain_ino(path, fi, &ino)) < 0)
		return rc;
	if ((rc = e2img_read_inode(&g_img, ino, &inode)) < 0)
		return rc;

	memset(stbuf, 0, sizeof(*stbuf));
	stbuf->st_mode  = inode.i_mode & ~0222;
	stbuf->st_nlink = inode.i_links_count;
	stbuf->st_uid   = inode.i_uid;
	stbuf->st_gid   = inode.i_gid;
	stbuf->st_size  = inode.i_size;
	return rc;
}

struct e2fs_apply_filler_info {
	fuse_fill_dir_t filler;
	void *buf;
};

static int e2fs_apply_filler(struct ext2_dir_entry *dirent, void *priv)
{
	struct e2fs_apply_filler_info *info = priv;

	char name[256];
	uint16_t len = ext2fs_dirent_name_len(dirent);
	memcpy(name, dirent->name, len);
	name[len] = 0;
	info->filler(info->buf, name, NULL, 0, 0);
	return 0;
}

static int e2fs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			 off_t offset, struct fuse_file_info *fi,
			 enum fuse_readdir_flags flags)
{
	int rc;
	ext2_ino_t ino;
	struct ext2_inode inode;
	if ((rc = e2fs_obtain_ino(path, fi, &ino)) < 0)
		return rc;
	if ((rc = e2img_read_inode(&g_img, ino, &inode)) < 0)
		return rc;

	if (!LINUX_S_ISDIR(inode.i_mode))
		return -ENOTDIR;

	struct e2fs_apply_filler_info info = {
		.filler = filler,
		.buf = buf,
	};

	if ((rc = e2img_iterate_dir(&g_img, &inode, e2fs_apply_filler, &info)) < 0)
		return rc;
	return 0;
}

static int e2fs_open(const char *path, struct fuse_file_info *fi)
{
	int rc;
	ext2_ino_t ino;
	struct ext2_inode inode;
	if ((rc = e2fs_obtain_ino(path, NULL, &ino)) < 0)
		return rc;
	if ((rc = e2img_read_inode(&g_img, ino, &inode)) < 0)
		return rc;

	if (!LINUX_S_ISREG(inode.i_mode))
		return -ENOENT;
	if ((fi->flags & O_ACCMODE) != O_RDONLY)
		return -EACCES;
	fi->fh = ino;
	return 0;
}

static int e2fs_opendir(const char *path, struct fuse_file_info *fi)
{
	int rc;
	ext2_ino_t ino;
	struct ext2_inode inode;
	if ((rc = e2fs_obtain_ino(path, NULL, &ino)) < 0)
		return rc;
	if ((rc = e2img_read_inode(&g_img, ino, &inode)) < 0)
		return rc;

	if (!LINUX_S_ISDIR(inode.i_mode))
		return -ENOENT;
	if ((fi->flags & O_ACCMODE) != O_RDONLY)
		return -EACCES;
	fi->fh = ino;
	return 0;
}

static int e2fs_read(const char *path, char *buf, size_t size, off_t offset,
		      struct fuse_file_info *fi)
{
	ssize_t rc = 0;
	ext2_ino_t ino;
	struct ext2_inode inode;
	if ((rc = e2fs_obtain_ino(path, fi, &ino)) < 0)
		return rc;
	if ((rc = e2img_read_inode(&g_img, ino, &inode)) < 0)
		return rc;

	void *blk = NULL;
	ext2_off64_t file_sz = EXT2_I_SIZE(&inode);
	size = min(size, file_sz - offset);
	for (ext2_off64_t i = offset; i < offset + size; i += g_img.blk_sz) {
		blk_t blkno;
		if ((rc = e2img_inode_get_blkno(&g_img, &inode, i / g_img.blk_sz, &blkno)) < 0)
			goto out;
		if (blk && (rc = e2img_bcache_release(&g_img, blk)) < 0) {
			blk = NULL;
			goto out;
		}
		if ((rc = e2img_bcache_access(&g_img, blkno, &blk)) < 0)
			goto out;
		memcpy(buf, blk, min(g_img.blk_sz, offset + size - i));
	}
out:
	if (blk)
		e2img_bcache_release(&g_img, blk);
	return rc ? rc : size;
}

static const struct fuse_operations hello_oper = {
	.init		= e2fs_init,
	.getattr	= e2fs_getattr,
	.readdir	= e2fs_readdir,
	.open		= e2fs_open,
	.opendir	= e2fs_opendir,
	.read		= e2fs_read,
};

static void show_help(const char *name)
{
	printf("usage: %s --img=<img> <mountpoint>\n", name);
}

int main(int argc, char **argv)
{
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
	if (fuse_opt_parse(&args, &g_options, g_option_spec, NULL) == -1)
		return 1;
	if (g_options.show_help) {
		show_help(argv[0]);
		args.argv[0][0] = '\0';
		return 1;
	}
	if (!g_options.img_path) {
		fprintf(stderr, "No --img=<img> specified\n");
		return 1;
	}
	int rc;
	if ((rc = e2img_open(&g_img, g_options.img_path)) < 0) {
		err_display(-rc, "e2img_open");
		return 1;
	}
	int ret = fuse_main(args.argc, args.argv, &hello_oper, NULL);
	fuse_opt_free_args(&args);
	if ((rc = e2img_close(&g_img)) < 0) {
		err_display(-rc, "e2img_close");
		return 1;
	}
	return ret;
}
