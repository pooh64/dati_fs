dd if=/dev/null of=fs.img bs=1M seek=8
mkfs.ext2 -F fs.img
