rm DISKFILE
fusermount -u /tmp/vb471/mountdir 
make clean
make
./rufs -f -d -s /tmp/vb471/mountdir
