#include <ext2fs/ext2fs.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "common.h"
#include "e2img.h"

static
int print_dirent_info(struct ext2_dir_entry *dirent, void *priv)
{
	struct e2img *fs = priv;
	printf("ino: %8.u name: %.*s", dirent->inode,
		ext2fs_dirent_name_len(dirent), dirent->name);
	if (EXT2_HAS_INCOMPAT_FEATURE(fs->sb, EXT2_FEATURE_INCOMPAT_FILETYPE))
		printf(" \ttype: %s", e2img_ftype_str_tab[ext2fs_dirent_file_type(dirent)]);
	printf("\n");
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

int ext2info_print_regfile(struct e2img *fs, struct ext2_inode *inode)
{
	ssize_t rc = 0;
	void *blk = NULL;
	ssize_t file_sz = EXT2_I_SIZE(inode);
	for (ssize_t i = 0; i < file_sz; i += fs->blk_sz) {
		blk_t blkno;
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
		if ((rc = e2img_iterate_dir(fs, &inode, print_dirent_info, fs)) < 0) {
			err_display(-rc, "e2img_iterate_dir");
			return rc;
		}
		return 0;
	}
	if (LINUX_S_ISREG(inode.i_mode)) {
		if ((rc = ext2info_print_regfile(fs, &inode)) < 0) {
			err_display(-rc, "ext2info_print_file");
			return rc;
		}
		return 0;
	}

	fprintf(stderr, "can't read this type of file\n");
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
			goto out_close;
		}
	}

	if (ext2info_process_ino(&img, ino) < 0)
		goto out_close;

	if ((rc = e2img_close(&img)) < 0) {
		err_display(-rc, "e2img_close");
		return 1;
	}
	return 0;
out_close:
	e2img_close(&img);
	return 1;
}
