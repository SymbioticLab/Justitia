#!/bin/bash

#  exp_mr_reuse.sh
#  
# This script is used to test diff num of chunks on same data size
#

# This script assumes the client code has been successfuily compiled.
# $1 is the server's ip addr
# $2 is the port number
# $3 is the mode ('read' / 'write')
# NOTE: for this bash script, please always pick 'write' as the mode
# $4 is the data size
# sudo chmod +x exp_mr_reuse.sh; (for permission issue)

usage() {
echo "usage: ./exp.sh [server_ip] [port_number] [mode] [data_size]"
exit 1
}

if [  $# -lt 4 ]
then
    echo insufficient input args
    usage
fi
read -p "Enter number of runs for each data size: " num_run
spin="-\|/"
> exp.out
> temp
echo Running $0 on $(date) >> exp.out
if [ $3 == "write" ]
then
# run experiments on different sizes with RDMA WRITE, each time run 5 sets and take the avg value and store to exp_out.txt
echo For RDMA_WRITE_WITH_IMM: >> exp.out
echo Number of runs at each data size: $num_run >> exp.out
echo 1.data_size 2.setup_time 3.copy_chunk_time 4.communication_time 5.total_time 6.num_chunk >> exp.out
#
for num_ck in 1 2 5 10 20 40 50 100 200 400 500 1000
do
    echo -n "running RDMA WRITE test on $num_ck chunks:  "
    echo $4 >> temp
    counter=0
    while [ $counter -lt $num_run ]
    do
        #echo $(($counter+1))
        ./rdma-client write $1 $2 $4 $num_ck | grep '^\s' | sed "s/$/ $num_ck"/ >> temp
        let counter+=1
        echo -ne "\b${spin:counter%${#spin}:1}" # spin
        #sleep .1
    done
    ./get_avg_multi_wr < temp >> exp.out
    >temp
    echo
done
else
# run experiments on different sizes with RDMA READ, each time run 5 sets and take the avg value and store to exp_out.txt
echo For RDMA READ: >> exp.out
echo Number of work requests: $5 >> exp.out
echo Number of runs at each data size: $num_run >> exp.out
echo 1.data_size 2.setup_time 3.RDMA_RW_time 4.cleanup_time 5.total_time >> exp.out

multiple=1
while [ $multiple -le 10 ]
do
    echo -n "running RDMA READ test on size $(($multiple * $4)) Bytes:  "
    echo $(($multiple * $4)) >> temp
    counter=0
    while [ $counter -lt $num_run ]
    do
        #echo $(($counter+1))
        ./rdma-client read $1 $2 $(($multiple * $4)) $5 | grep '^\s' >> temp
        let counter+=1
        echo -ne "\b${spin:counter%${#spin}:1}"
    done
    ./get_avg < temp >> exp.out
    >temp
    let multiple+=1
    echo
done
fi





