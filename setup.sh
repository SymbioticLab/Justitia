sudo apt-get update
sudo apt-get install -y pkg-config libnl-3-dev libnl-route-3-dev libibumad-dev
cd ~/frdma/libibverbs-41mlnx1/
./autogen.sh
./configure --prefix=/usr libdir=/usr/lib64
make
sudo make install
cd ~/frdma/libmlx5-41mlnx1
./autogen.sh
./configure --prefix=/usr libdir=/usr/lib64
make
sudo make install
cd ~/frdma/perftest-4.2/
./autogen.sh
./configure --prefix=/usr libdir=/usr/lib64
make