#!/bin/bash
## CloudLab m510 RoCEv2
## "nodes" file should also include the last receiving node
## This is the version without launching the pacer (for debugging the pacer manually)
## If used for only doing no-pacer run and you encounter an error, run cleanup_shm.sh
ib_write_bw=/users/yiwenzhg/frdma/perftest-4.2/ib_write_bw
ib_write_lat=/users/yiwenzhg/frdma/perftest-4.2/ib_write_lat
J_daemon=/users/yiwenzhg/frdma/rdma_pacer/pacer
out_dir=/tmp
receiver_node=$(tail -n 1 nodes)
ip_dst=192.168.0.5     # hardcoded
bw_size=1000000
lat_size=16
bw_iters=40000
#lat_iter=10000000
lat_iter=1000000
port_base=5200
cnt=1
incast_size=$(wc -l < nodes)
num_senders=$((incast_size-1))

#./ib_write_bw -F -e -d mlx5_1 -x 3 -s 1000000 -n 10000 -l 1 -t 1 -p 8888 192.168.1.2
#./ib_write_lat -F -d mlx5_1 -x 3 -s 16 -n 1000000 -l 1 -t 1 -p 9999 192.168.1.2
# run receiver cmd first
cnt=1
for node in $(cat nodes); do
    let port="$port_base + $cnt"
    if [[ $cnt -eq $num_senders ]]; then
        cmd="export LD_LIBRARY_PATH=/usr/lib64; $ib_write_lat -F -s $lat_size -n $lat_iter -x 3 -i 2 -l 1 -t 1 -p $port &> /dev/null"
    else
        cmd="export LD_LIBRARY_PATH=/usr/lib64; $ib_write_bw -F -e -s $bw_size -n $bw_iters -x 3 -i 2 -l 1 -t 1 -p $port &> /dev/null"
    fi
    echo "On $receiver_node: execute $cmd"
    ssh -o "StrictHostKeyChecking no" -p 22 $receiver_node $cmd &
    sleep 1
    if [[ $cnt -eq $num_senders ]]; then
        break
    fi
    let cnt+=1
done
echo "receiver DONE"

sleep 5
# then sender command
cnt=1
for node in $(cat nodes); do
    let port="$port_base + $cnt"
    if [[ $cnt -eq $num_senders ]]; then
        sleep 3
        output="$out_dir/lat_result_incast_J_$node.txt"
        cmd="export LD_LIBRARY_PATH=/usr/lib64; $ib_write_lat -F -s $lat_size -n $lat_iter -x 3 --log_off -i 2 -l 1 -t 1 -p $port $ip_dst |tee $output"
    else
        sleep 1
        output="$out_dir/bw_result_incast_J_$node.txt"
        log="$out_dir/bw_log_incast_J_$node.txt"
        cmd="export LD_LIBRARY_PATH=/usr/lib64; $ib_write_bw -F -e -s $bw_size -n $bw_iters -x 3 -i 2 -l 1 -t 1 -p $port $ip_dst -L $log |tee $output"
    fi
    echo "On $node: execute $cmd"
    ssh -o "StrictHostKeyChecking no" -p 22 $node $cmd &
    if [[ $cnt -eq $num_senders ]]; then
        break
    fi
    let cnt+=1
done
wait
echo "DONE"

