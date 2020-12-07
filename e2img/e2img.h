#include <ext2fs/ext2fs.h>
#include <stddef.h>

#define EXT2_I_NBLOCKS(sb, i) ((i)->i_blocks / (2 << (sb)->s_log_block_size))
#define EXT2_I_FTYPE(i) ((i)->i_mode & (0xf000))

#define E2IMG_INCOMPAT_SUPPORTED (EXT2_FEATURE_INCOMPAT_FILETYPE)

struct e2img {
	int fd;
	size_t blk_sz;
	struct ext2_super_block *sb;
};

ssize_t e2img_blk_read(struct e2img *fs, void *buf, blk_t len, blk_t off);

int e2img_bcache_access(struct e2img *fs, blk_t blkno, void **blk);
int e2img_bcache_release(struct e2img *fs, void *blk);

int e2img_open(struct e2img *fs, char const *path);
int e2img_close(struct e2img *fs);

int e2img_read_group(struct e2img *fs, dgrp_t grpno, struct ext2_group_desc *grp);

int e2img_read_inode(struct e2img *fs, ext2_ino_t ino, struct ext2_inode *inode);

int e2img_inode_get_blkno(struct e2img *fs, struct ext2_inode *inode,
		blk_t file_blkno, blk_t *fs_blkno);

int e2img_iterate_dir(struct e2img *fs, struct ext2_inode *inode,
		int (*func)(struct ext2_dir_entry *dirent, void *priv), void *priv);

extern char *e2img_ftype_str_tab[EXT2_FT_MAX];

int e2img_path_lookup(struct e2img *fs, char const *path, ext2_ino_t *ino);
