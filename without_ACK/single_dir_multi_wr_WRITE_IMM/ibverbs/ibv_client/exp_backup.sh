#!/bin/bash

#  exp.sh
#  
#
#  Created by Yiwen Zhang on 6/29/16.
#

# This script assumes the client code has been successfuily compiled.
# This script also assumes the ip of server is 192.168.0.11
# $1 is the server's ip addr
# $2 is the port number
# $3 is the mode ('read' / 'write')
# $4 is the base data size (data size = base * multiple)
# $5 is the number of work requests
# sudo chmod +x exp.sh; (for permission issue)

usage() {
echo "usage: ./exp.sh [server_ip] [port_number] [mode] [base_data_size] [num_wr]"
exit 1
}

if [  $# -lt 5 ]
then
    echo insufficient input args
    usage
fi
read -p "Enter number of runs for each data size: " num_run
> exp.out
> temp
echo Running $0 on $(date) >> exp.out
if [ $3 == "write" ]
then
# run experiments on different sizes with RDMA WRITE, each time run 5 sets and take the avg value and store to exp_out.txt
echo For RDMA WRITE: >> exp.out
echo Number of work requests: $5 >> exp.out
echo Number of runs at each data size: $num_run >> exp.out
echo 1.data_size 2.setup_time 3.RDMA_RW_time 4.cleanup_time 5.total_time >> exp.out

multiple=1
while [ $multiple -le 10 ]
do
    echo running RDMA WRITE test on size $(($multiple * $4)) Bytes
    echo $(($multiple * $4)) >> temp
    counter=0
    while [ $counter -lt $num_run ]
    do
        echo $(($counter+1))
        ./rdma-client write $1 $2 $(($multiple * $4)) $5 | grep '^\s' >> temp
        let counter+=1
    done
    ./get_avg < temp >> exp.out
    >temp
    let multiple+=1
done
else
# run experiments on different sizes with RDMA READ, each time run 5 sets and take the avg value and store to exp_out.txt
echo For RDMA READ: >> exp.out
echo Number of work requests: $5 >> exp.out
echo Number of runs for each data size: $num_run >> exp.out
echo 1.data_size 2.setup_time 3.RDMA_RW_time 4.cleanup_time 5.total_time >> exp.out

multiple=1
while [ $multiple -le 10 ]
do
    echo running RDMA READ test on size $(($multiple * $4)) Bytes
    echo $(($multiple * $4)) >> temp
    counter=0
    while [ $counter -lt $num_run ]
    do
        echo $(($counter+1))
        ./rdma-client read $1 $2 $(($multiple * $4)) $5 | grep '^\s' >> temp
        let counter+=1
    done
    ./get_avg < temp >> exp.out
    >temp
    let multiple+=1
done
fi





