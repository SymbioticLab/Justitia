#! /usr/bin/bash
mkdir -p $HOME/tools/frdma_build
cd $HOME/frdma/libibverbs-41mlnx1/
./autogen.sh
./configure --prefix=$HOME/tools/frdma_build
make -j
make install
export CPATH=$HOME/tools/frdma_build/include
export LD_LIBRARY_PATH=$HOME/tools/frdma_build/lib
export INCLUDE=$CPATH
export LIB=$LD_LIBRARY_PATH
cd ../libmlx5-41mlnx1/
./autogen.sh
./configure --prefix=$HOME/tools/frdma_build
make -j
make install
echo "Remember to export LD_LIBRARY_PATH=$LD_LIBRARY_PATH"