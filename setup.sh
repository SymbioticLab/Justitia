sudo apt-get update
sudo apt-get install -y pkg-config libnl-3-dev libnl-route-3-dev libibumad-dev
cd libibverbs-1.2.1mlnx1/
./autogen.sh
./configure --prefix=/usr libdir=/usr/lib64
make
sudo make install
cd ../libmlx4
./autogen.sh
./configure --prefix=/usr libdir=/usr/lib64
make
sudo make install
cd ../perftest-4.2/
./autogen.sh
./configure --prefix=/usr libdir=/usr/lib64
make
cd ../daemon/
make