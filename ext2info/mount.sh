mkdir -p mnt
sudo mount -t ext2 -o loop fs.img mnt
sudo chown -R :snail mnt
sudo chmod -R g+rw mnt
