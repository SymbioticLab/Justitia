#!/bin/bash

#  exp_1_to_N_cp8.sh
#
# This script is used for 1 TO N TEST
#

# This script assumes the client code has been successfuily compiled.
# modify the variables to appropriate values
# sudo chmod +x exp_N_to_1_cp8.sh; (for permission issue)

server_ip=192.168.0.98
port=40723
dsize=10000000
num_chunk=1
> exp8.out
> temp
echo Running $0 on $(date) >> exp8.out

echo For RDMA_WRITE_WITH_IMM: >> exp8.out
echo data size: $dsize\; num_chunk: $num_chunk\ >> exp8.out
echo -n "running RDMA WRITE test on $dsize B data with $num_ck chunks:  "

./rdma-client write $server_ip $port $dsize $num_chunk | grep '^\s' >> exp8.out
echo





