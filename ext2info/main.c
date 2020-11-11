#include <ext2fs/ext2fs.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "common.h"

struct e2img {
	int fd;
	size_t blk_sz;	//
	struct ext2_super_block *sb;

	uint8_t *tmp_blk;
};

//
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
int __init_super_block(struct e2img *fs)
{
	size_t boff, blen;	//
	uint8_t *buf;
	boff = SUPERBLOCK_OFFSET / fs->blk_sz;
	blen = div_rup(SUPERBLOCK_OFFSET + SUPERBLOCK_SIZE, fs->blk_sz) - boff;

	buf = xmemalign(fs->blk_sz, blen * fs->blk_sz);

	if (blk_read(fs->fd, fs->blk_sz, buf, blen, boff) != blen)
		return -EIO;

	fs->sb = xmalloc(sizeof(*fs->sb));

	memcpy(fs->sb, buf + SUPERBLOCK_OFFSET - boff * fs->blk_sz, SUPERBLOCK_SIZE);

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
	fs->tmp_blk = xmalloc(fs->blk_sz);
	
	return rc;
}


static
int e2img_read_group(struct e2img *fs, dgrp_t grpno, struct ext2_group_desc *grp)
{
	int rc;
	blk_t desc_per_blk = EXT2_DESC_PER_BLOCK(fs->sb);
	blk_t blkno = fs->sb->s_first_data_block + 1 + grpno / desc_per_blk;

	ext2_off_t blkoff = (grpno % desc_per_blk) * sizeof(*grp);

	if ((rc = blk_read(fs->fd, fs->blk_sz, fs->tmp_blk, 1, blkno)) < 0)
		return rc;

	memcpy(grp, fs->tmp_blk + blkoff, sizeof(*grp));
	return 0;
}

static
int e2img_read_inode(struct e2img *fs, ext2_ino_t ino, struct ext2_inode *inode)
{
	int rc;
	struct ext2_group_desc grp;
	--ino;

	dgrp_t grpno = ino / EXT2_INODES_PER_GROUP(fs->sb);
	e2img_read_group(fs, grpno, &grp);

	blk_t blkno = grp.bg_inode_table +
		(ino % EXT2_INODES_PER_GROUP(fs->sb)) / EXT2_INODES_PER_BLOCK(fs->sb);

	ext2_off_t blkoff = (ino % EXT2_INODES_PER_BLOCK(fs->sb)) * EXT2_INODE_SIZE(fs->sb);

	if ((rc = blk_read(fs->fd, fs->blk_sz, fs->tmp_blk, 1, blkno)) < 0)
		return rc;

	memcpy(inode, fs->tmp_blk + blkoff, EXT2_INODE_SIZE(fs->sb));
	return 0;
}

int main(int argc, char **argv)
{
	ssize_t rc;
	struct e2img img;

	if (argc != 3) {
		fprintf(stderr, "usage: %s <ext2-image> <ino>\n", argv[0]);
		return 1;
	}

	unsigned long ino_val;
	char *eptr;
	errno = 0;
	ino_val = strtoul(argv[2], &eptr, 0);
	if (errno || *eptr) {
		fprintf(stderr, "wrong ino\n");
		return 1;
	}

	if ((rc = e2img_open(&img, argv[1])) < 0) {
		err_display(-rc, "e2img_open failed");
		return 1;
	}

	struct ext2_inode inode;
	if ((rc = e2img_read_inode(&img, ino_val, &inode)) < 0) {
		err_display(-rc, "e2img_read_inode failed");
		return 1;
	}

	printf("i_blocks: %d\n", inode.i_blocks);
	return 0;
}
