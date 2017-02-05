#!/bin/bash

#  exp_1_to_N_cp4.sh
#
# This script is used for 1 TO N TEST
#

# This script assumes the client code has been successfuily compiled.
# modify the variables to appropriate values
# sudo chmod +x exp_N_to_1_cp4.sh; (for permission issue)

server_ip=192.168.0.48
port=39292
dsize=10000000
num_chunk=1
> exp3.out
> temp
echo Running $0 on $(date) >> exp3.out

echo For RDMA_WRITE_WITH_IMM: >> exp3.out
echo data size: $dsize\; num_chunk: $num_chunk\ >> exp3.out
echo -n "running RDMA WRITE test on $dsize B data with $num_ck chunks:  "

./rdma-client write $server_ip $port $dsize $num_chunk | grep '^\s' >> exp3.out
echo





