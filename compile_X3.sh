cd /users/yiwenzhg/frdma/libmlx4
make clean
./autogen.sh
./configure --prefix=/usr libdir=/usr/lib64
make
sudo make install