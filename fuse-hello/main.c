#define FUSE_USE_VERSION 30

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <assert.h>
#include <stddef.h>

const char *g_file_name = "hello";
const char *g_file_path = "/hello";
const char *g_hello_str = "hello!!";

/* FUSE options: show_help */
static struct options {
	int show_help;
} g_options;

#define OPTION(t, p) { t, offsetof(struct options, p), 1 }
static const struct fuse_opt g_option_spec[] = {
	OPTION("-h", show_help),
	OPTION("--help", show_help),
	FUSE_OPT_END,
};
#undef OPTION

static void *hello_init(struct fuse_conn_info *conn, struct fuse_config *cfg)
{
	cfg->kernel_cache = 1; /* Data never changed externally */
	return NULL;
}

static int hello_getattr(const char *path, struct stat *stbuf,
			 struct fuse_file_info *fi)
{
	int res = 0;
	memset(stbuf, 0, sizeof(struct stat));
	if (!strcmp(path, "/")) {
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
	} else if (!strcmp(path, g_file_path)) {
		stbuf->st_mode = S_IFREG | 0444;
		stbuf->st_nlink = 1;
		stbuf->st_size = strlen(g_hello_str);
	} else {
		res = -ENOENT;
	}
	return res;
}

static int hello_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			 off_t offset, struct fuse_file_info *fi,
			 enum fuse_readdir_flags flags)
{
	if (strcmp(path, "/"))
		return -ENOENT;
	filler(buf, ".", NULL, 0, 0);
	filler(buf, "..", NULL, 0, 0);
	filler(buf, g_file_name, NULL, 0, 0);
	return 0;
}

static int hello_open(const char *path, struct fuse_file_info *fi)
{
	if (strcmp(path, g_file_path))
		return -ENOENT;
	if ((fi->flags & O_ACCMODE) != O_RDONLY) /* ro/wo/rw */
		return -EACCES;
	return 0;
}

static int hello_read(const char *path, char *buf, size_t size, off_t offset,
		      struct fuse_file_info *fi)
{
	size_t len;
	if (strcmp(path, g_file_path))
		return -ENOENT;
	len = strlen(g_hello_str);
	if (offset < len) {
		if (offset + size > len)
			size = len - offset;
		memcpy(buf, g_hello_str + offset, size);
	} else {
		size = 0;
	}
	return size;
}

static const struct fuse_operations hello_oper = {
	.init		= hello_init,
	.getattr	= hello_getattr,
	.readdir	= hello_readdir,
	.open		= hello_open,
	.read		= hello_read,
};

static void show_help(const char *name)
{
	printf("usage: %s <mountpoint>\n", name);
}

int main(int argc, char **argv)
{
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
	if (fuse_opt_parse(&args, &g_options, g_option_spec, NULL) == -1)
		return 1;
	if (g_options.show_help) {
		show_help(argv[0]);
		assert(fuse_opt_add_arg(&args, "--help") == 0);
		args.argv[0][0] = '\0';
	}
	int ret = fuse_main(args.argc, args.argv, &hello_oper, NULL);
	fuse_opt_free_args(&args);
	return ret;
}
