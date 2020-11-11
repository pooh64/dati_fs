dd if=/dev/null of=fs.img bs=1M seek=2
mkfs.ext2 -F fs.img
mkdir -p mnt
sudo mount -t ext2 -o loop fs.img mnt
sudo chown -R :snail mnt
sudo chmod -R g+rw mnt
