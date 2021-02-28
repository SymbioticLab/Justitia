#!/bin/bash
## CloudLab m510 RoCEv2
## "nodes" file should also include the last receiving node
ib_write_bw=/users/yiwenzhg/frdma/perftest-4.2/ib_write_bw
ib_write_lat=/users/yiwenzhg/frdma/perftest-4.2/ib_write_lat
out_dir=/tmp
receiver_node=$(tail -n 1 nodes)
ip_dst=192.168.0.5     # hardcoded
bw_size=1000000
lat_size=16
bw_iters=10000
#lat_iter=50000  # w/o ecn
#lat_iter=10000000 # w/ ecn
lat_iter=1000000 # w/ ecn
port_base=5000
cnt=1
incast_size=$(wc -l < nodes)
num_senders=$((incast_size-1))

#./ib_write_bw -F -e -d mlx5_1 -x 3 -s 1000000 -n 10000 -l 1 -t 1 -p 8888 192.168.1.2
#./ib_write_lat -F -d mlx5_1 -x 3 -s 16 -n 1000000 -l 1 -t 1 -p 9999 192.168.1.2
# run receiver cmd first
for node in $(cat nodes); do
    let port="$port_base + $cnt"
    if [[ $cnt -eq $num_senders ]]; then
        cmd="$ib_write_lat -F -s $lat_size -n $lat_iter -x 3 -i 2 -l 1 -t 1 -p $port &> /dev/null"
    else
        cmd="$ib_write_bw -F -e -s $bw_size -n $bw_iters -x 3 -i 2 -l 1 -t 1 -p $port &> /dev/null"
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
        output="$out_dir/lat_result_incast_$node.txt"
        cmd="$ib_write_lat -F -s $lat_size -n $lat_iter -x 3 -i 2 --log_off -l 1 -t 1 -p $port $ip_dst |tee $output"
    else
        sleep 1
        output="$out_dir/bw_result_incast_$node.txt"
        log="$out_dir/bw_log_incast_$node.txt"
        cmd="$ib_write_bw -F -e -s $bw_size -n $bw_iters -x 3 -i 2 -l 1 -t 1 -p $port $ip_dst -L $log |tee $output"
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

