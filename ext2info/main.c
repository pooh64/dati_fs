#include <ext2fs/ext2fs.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "common.h"

#define EXT2_I_NBLOCKS(sb, i) ((i)->i_blocks / (2 << (sb)->s_log_block_size))

#define EXT2_I_FTYPE(i) ((i)->i_mode & (0xf000))
#define EXT2_DIRENT_NAME_LEN(d) ((d)->name_len & 0xff)

struct e2img {
	int fd;
	size_t blk_sz;
	struct ext2_super_block *sb;
};

static
ssize_t blk_read(int fd, size_t blk_sz, void *buf, size_t len, size_t off)
{
	ssize_t rc, orig;
	len *= blk_sz;
	off *= blk_sz;
	orig = len;

	while (len) {
		if ((rc = pread(fd, buf, len, off)) < 0)
			return -errno;
		if (!rc)
			break;
		len -= rc;
		off += rc;
		buf = (uint8_t*) buf + rc;
	}
	return (orig - len) / blk_sz;
}

static
ssize_t e2img_blk_read(struct e2img *fs, void *buf, blk64_t len, blk64_t off)
{
	ssize_t rc = blk_read(fs->fd, fs->blk_sz, buf, len, off);
	if (!(rc < 0) && rc != len)
		rc = -EIO;
	return rc;
}

/* dummy buffer cache */
static
int e2img_bcache_access(struct e2img *fs, blk64_t blkno, void **blk)
{
	ssize_t rc;
	*blk = xmemalign(fs->blk_sz, fs->blk_sz);
	if ((rc = e2img_blk_read(fs, *blk, 1, blkno)) < 0)
		goto errout;
	return 0;
errout:
	free(blk);
	return rc;
}

static
int e2img_bcache_release(struct e2img *fs, void *blk)
{
	free(blk);
	return 0;
}

static
int __init_super_block(struct e2img *fs)
{
	size_t boff, blen;
	uint8_t *buf;
	boff = SUPERBLOCK_OFFSET / fs->blk_sz;
	blen = div_rup(SUPERBLOCK_OFFSET + SUPERBLOCK_SIZE, fs->blk_sz) - boff;

	buf = xmemalign(fs->blk_sz, blen * fs->blk_sz);

	if (blk_read(fs->fd, fs->blk_sz, buf, blen, boff) != blen)
		return -EIO;

	fs->sb = xmalloc(sizeof(*fs->sb));

	memcpy(fs->sb, buf + SUPERBLOCK_OFFSET - boff * fs->blk_sz, SUPERBLOCK_SIZE);
	free(buf);

	release_assert(fs->sb->s_magic == EXT2_SUPER_MAGIC);
	return 0;
}

static
int e2img_open(struct e2img *fs, char const *path)
{
	int rc;
	struct stat st;

	if ((fs->fd = open(path, O_RDONLY)) < 0)
		return -errno;

	if (fstat(fs->fd, &st) < 0)
		return -errno;

	fs->blk_sz = st.st_blksize;

	rc = __init_super_block(fs);
	fs->blk_sz = EXT2_BLOCK_SIZE(fs->sb);
	return rc;
}

static
int e2img_close(struct e2img *fs)
{
	free(fs->sb);
	return close(fs->fd);
}

static
int e2img_read_group(struct e2img *fs, dgrp_t grpno, struct ext2_group_desc *grp)
{
	int rc;
	void *blk;
	blk_t desc_per_blk = EXT2_DESC_PER_BLOCK(fs->sb);
	blk_t blkno = fs->sb->s_first_data_block + 1 + grpno / desc_per_blk;
	ext2_off_t blkoff = (grpno % desc_per_blk) * sizeof(*grp);

	if ((rc = e2img_bcache_access(fs, blkno, &blk)) < 0)
		return rc;

	memcpy(grp, ptr_add(blk, blkoff), sizeof(*grp));
	e2img_bcache_release(fs, blk);
	return 0;
}

static
int e2img_read_inode(struct e2img *fs, ext2_ino_t ino, struct ext2_inode *inode)
{
	int rc;
	void *blk;
	struct ext2_group_desc grp;

	--ino;
	dgrp_t grpno = ino / EXT2_INODES_PER_GROUP(fs->sb);
	e2img_read_group(fs, grpno, &grp);

	blk_t blkno = grp.bg_inode_table +
		(ino % EXT2_INODES_PER_GROUP(fs->sb)) / EXT2_INODES_PER_BLOCK(fs->sb);

	ext2_off_t blkoff = (ino % EXT2_INODES_PER_BLOCK(fs->sb)) * EXT2_INODE_SIZE(fs->sb);

	if ((rc = e2img_bcache_access(fs, blkno, &blk)) < 0)
		return rc;

	memcpy(inode, ptr_add(blk, blkoff), EXT2_INODE_SIZE(fs->sb));
	e2img_bcache_release(fs, blk);
	return 0;
}

static
int e2img_inode_get_blkno(struct e2img *fs, struct ext2_inode *inode,
		blk64_t file_blkno, blk64_t *fs_blkno)
{
	if (file_blkno < EXT2_NDIR_BLOCKS)
		*fs_blkno = inode->i_block[file_blkno];
	else
		release_assert(0); /* TODO: Indirect blocks */
	return 0;
}

static
int e2img_iterate_dir(struct e2img *fs, struct ext2_inode *inode,
		int (*func)(struct ext2_dir_entry *dirent, void *priv), void *priv)
{
	ssize_t rc;
	blk64_t file_blkno, fs_blkno;
	ext2_off64_t fpos, fsize = EXT2_I_SIZE(inode);
	void *blk = NULL;

	file_blkno = 0;
	fpos = 0;
	if (!fsize)
		return 0;

fetch_blk:
	if (blk && (rc = e2img_bcache_release(fs, blk)) < 0) {
		blk = NULL;
		goto out;
	}
	file_blkno = fpos / fs->blk_sz;
	if ((rc = e2img_inode_get_blkno(fs, inode, file_blkno, &fs_blkno)) < 0)
		goto out;
	if ((rc = e2img_bcache_access(fs, fs_blkno, &blk)) < 0)
		goto out;

	rc = 0;
	while (fpos < fsize) {
		if (fpos / fs->blk_sz != file_blkno)
			goto fetch_blk;

		struct ext2_dir_entry *dirent = ptr_add(blk, fpos % fs->blk_sz);
		fpos += dirent->rec_len;
		if ((rc = func(dirent, priv)))
			break;
	}
out:
	if (blk)
		e2img_bcache_release(fs, blk);
	return rc;
}

static
int print_dirent_info(struct ext2_dir_entry *dirent, void *priv)
{
	(void) priv;
	printf("ino: %8.u name: %.*s\n", dirent->inode,
			EXT2_DIRENT_NAME_LEN(dirent), dirent->name);
	return 0;
}

struct dirent_cmp_data {
	char const *name;
	uint8_t name_len;
	ext2_ino_t ino;
};

static
int dirent_cmp(struct ext2_dir_entry *dirent, void *priv)
{
	struct dirent_cmp_data *s = priv;
#if 0
	printf("dirent_cmp: %.*s %.*s\n", s->name_len, s->name,
			EXT2_DIRENT_NAME_LEN(dirent), dirent->name);
#endif
	if (s->name_len != EXT2_DIRENT_NAME_LEN(dirent))
		return 0;
	int rc = memcmp(s->name, dirent->name, s->name_len);
	if (rc)
		return 0;
	s->ino = dirent->inode;

	return 1;
}

static
int e2img_path_lookup(struct e2img *fs, char const *path, ext2_ino_t *ino)
{
	int rc;
	struct dirent_cmp_data data;
	struct ext2_inode inode;

	if (path[0] != '/')
		return -ENOENT;
	data.ino = EXT2_ROOT_INO;

	while (1) {
		while (*path == '/')
			path++;
		if (*path == '\0')
			break;
		size_t len = 0;
		for (; path[len] != '/' && path[len] != '\0'; ++len) {
			if (len > EXT2_NAME_LEN)
				return -EINVAL;
		}
		data.name = path;
		data.name_len = len;
		path += len;

		if ((rc = e2img_read_inode(fs, data.ino, &inode)) < 0)
			return rc;
		if (!LINUX_S_ISDIR(inode.i_mode))
			return -ENOENT;

		if ((rc = e2img_iterate_dir(fs, &inode, dirent_cmp, &data)) < 0)
			return rc;
		if (!rc)
			return -ENOENT;
	}
	*ino = data.ino;
	return 0;
}

static
int get_strtoul(char const *str, unsigned long *val)
{
	errno = 0;
	char *eptr;
	*val = strtoul(str, &eptr, 0);
	if (errno)
		return -errno;
	if (*eptr)
		return -EINVAL;
	return 0;
}

int ext2info_print_file(struct e2img *fs, struct ext2_inode *inode)
{
	ssize_t rc = 0;
	void *blk = NULL;
	ssize_t file_sz = EXT2_I_SIZE(inode);
	for (ssize_t i = 0; i < file_sz; i += fs->blk_sz) {
		blk64_t blkno;
		if ((rc = e2img_inode_get_blkno(fs, inode, i / fs->blk_sz, &blkno)) < 0)
			goto out;
		if (blk && (rc = e2img_bcache_release(fs, blk)) < 0) {
			blk = NULL;
			goto out;
		}
		if ((rc = e2img_bcache_access(fs, blkno, &blk)) < 0)
			goto out;
		errno = 0;
		fwrite(blk, 1, min(fs->blk_sz, file_sz - i), stdout);
		if ((rc = -errno))
			goto out;
	}
out:
	if (blk)
		e2img_bcache_release(fs, blk);
	return rc;
}

int ext2info_process_ino(struct e2img *fs, ext2_ino_t ino)
{
	int rc;
	struct ext2_inode inode;
	if ((rc = e2img_read_inode(fs, ino, &inode)) < 0) {
		err_display(-rc, "e2img_read_inode");
		return rc;
	}

	if (LINUX_S_ISDIR(inode.i_mode)) {
		if ((rc = e2img_iterate_dir(fs, &inode, print_dirent_info, NULL)) < 0) {
			err_display(-rc, "e2img_iterate_dir");
			return rc;
		}
		return 0;
	}

	if ((rc = ext2info_print_file(fs, &inode)) < 0) {
		err_display(-rc, "ext2info_print_file");
		return rc;
	}
	return 0;
}

int main(int argc, char **argv)
{
	ssize_t rc;
	struct e2img img;
	char *imgpath = NULL;
	char *inopath = NULL;
	int ino_present = 0;
	ext2_ino_t ino;
	int c;
	opterr = 0;
	while ((c = getopt(argc, argv, "hf:i:p:")) != -1) switch (c) {
		case 'f':
			imgpath = optarg;
			break;
		case 'p':
			if (ino_present++) {
				fprintf(stderr, "ino already presented\n");
				return 1;
			}
			inopath = optarg;
			break;
		case 'i':
			if (ino_present++) {
				fprintf(stderr, "ino already presented\n");
				return 1;
			}
			unsigned long tmp;
			if ((rc = get_strtoul(optarg, &tmp)) < 0) {
				err_display(-rc, "wrong ino");
				return 1;
			}
			ino = tmp;
			break;
		case 'h':
		default:
			fprintf(stderr, "usage: %s "
				"-f <ext2-image> [-i <ino>|-p <path>]\n", argv[0]);
			return 1;
	}
	if (!imgpath) {
		fprintf(stderr, "no <ext2-image> presented\n");
		return 1;
	}
	if (!ino_present) {
		fprintf(stderr, "no ino presented\n");
		return 1;
	}

	if ((rc = e2img_open(&img, imgpath)) < 0) {
		err_display(-rc, "e2img_open");
		return 1;
	}

	if (inopath) {
		if ((rc = e2img_path_lookup(&img, inopath, &ino)) < 0) {
			err_display(-rc, "e2img_path_lookup");
			return 1;
		}
	}

	if (ext2info_process_ino(&img, ino) < 0)
		return 1;

	if ((rc = e2img_close(&img)) < 0) {
		err_display(-rc, "e2img_close");
		return 1;
	}
	return 0;
}
