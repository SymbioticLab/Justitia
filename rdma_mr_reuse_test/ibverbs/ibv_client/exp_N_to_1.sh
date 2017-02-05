#!/bin/bash

#  exp_N_to_1.sh // test version for 200 chunks only
#
# This script is used for N TO 1 TEST
#

# This script assumes the client code has been successfuily compiled.
# modify the variables to appropriate values
# sudo chmod +x exp_N_to_1.sh; (for permission issue)

#usage() {
#echo "usage: ./exp.sh [server_ip] [port_number] [mode] [data_size]"
#exit 1
#}
#
#if [  $# -lt 4 ]
#then
#    echo insufficient input args
#    usage
#fi

#read -p "Enter number of runs for each data size: " num_run
server_ip=192.168.0.18
port=52271
dsize=1000000000
num_chunk=1
> exp.out
> temp
echo Running $0 on $(date) >> exp.out

echo For RDMA_WRITE_WITH_IMM: >> exp.out
echo data size: $dsize\; num_chunk: $num_chunk\ >> exp.out
echo -n "running RDMA WRITE test on $dsize B data with $num_ck chunks:  "

./rdma-client write $server_ip $port $dsize $num_chunk | grep '^\s' >> exp.out
echo





