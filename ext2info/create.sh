dd if=/dev/null of=fs.img bs=1M seek=2
mkfs.ext2 -F fs.img
