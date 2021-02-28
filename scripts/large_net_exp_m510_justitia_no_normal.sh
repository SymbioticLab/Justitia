#!/bin/bash
## CloudLab m510 RoCEv2
## "nodes" file should also include the last receiving node
ib_write_bw=/users/yiwenzhg/frdma/perftest-4.2/ib_write_bw
ib_write_lat=/users/yiwenzhg/frdma/perftest-4.2/ib_write_lat
J_daemon=/users/yiwenzhg/frdma/rdma_pacer/pacer
out_dir=/tmp
num_justitia_flow=9    # the last one is a latency app; others are bw
num_justitia_bw_flow=$((num_justitia_flow-1))
sender_node=cpa1
receiver_node=cpb1
ip_dst=192.168.0.5     # hardcoded
bw_size=1000000
lat_size=16
normal_bw_iters=500000
bw_iters=20000
lat_iter=50000
#lat_iter=1000000
port_base=5000
normal_port=$((port_base+100))
#cnt=1

# Launch Justitia Daemon (receiver) (gidx = 3)
output="$out_dir/Jdaemon_normal_recver_output_$receiver_node.txt"
cmd="$J_daemon 0 $ip_dst 1 3 > $output"
echo "launch Justitia Daemon (receiver) on $receiver_node: $cmd"
ssh -o "StrictHostKeyChecking no" -p 22 $receiver_node $cmd &
sleep 2

# Launch Justitia Daemon (sender) (gidx = 3)
output="$out_dir/Jdaemon_normal_sender_output_$sender_node.txt"
cmd="$J_daemon 1 $ip_dst 1 3 > $output"
echo "launch Justitia Daemon on $sender_node: $cmd"
ssh -o "StrictHostKeyChecking no" -p 22 $sender_node $cmd &
sleep 2

# Justitia flow receiver
for (( i=1; i<=$num_justitia_flow; i++ )); do
    let port="$port_base + $i"
    if [[ $i -eq $num_justitia_flow ]]; then
        cmd="export LD_LIBRARY_PATH=/usr/lib64; $ib_write_lat -F -s $lat_size -n $lat_iter -x 3 -i 2 -l 1 -t 1 -p $port &> /dev/null"
    else
        cmd="export LD_LIBRARY_PATH=/usr/lib64; $ib_write_bw -F -e -s $bw_size -n $bw_iters -x 3 -i 2 -l 1 -t 1 -p $port &> /dev/null"
    fi
    echo "On $receiver_node: execute $cmd"
    ssh -o "StrictHostKeyChecking no" -p 22 $receiver_node $cmd &
    sleep 0.5
done
echo "Justitia receiver DONE"

# Justitia flow sender
for (( i=1; i<=$num_justitia_flow; i++ )); do
    let port="$port_base + $i"
    if [[ $i -eq $num_justitia_flow ]]; then
        sleep 3
        output="$out_dir/lat_result_large_J_$sender_node.txt"
        cmd="export LD_LIBRARY_PATH=/usr/lib64; $ib_write_lat -F -s $lat_size -n $lat_iter -x 3 -i 2 --log_off -l 1 -t 1 -p $port $ip_dst |tee $output"
    else
        sleep 1
        output="$out_dir/bw_result_large_J_$sender_node.txt"
        log="$out_dir/bw_log_large_$sender_node.txt"
        cmd="export LD_LIBRARY_PATH=/usr/lib64; $ib_write_bw -F -e -s $bw_size -n $bw_iters -x 3 -i 2 -l 1 -t 1 -p $port $ip_dst -L $log |tee $output"
    fi
    echo "On $sender_node: execute $cmd"
    ssh -o "StrictHostKeyChecking no" -p 22 $sender_node $cmd &
done
echo "Justitia sender DONE"

#./ib_write_bw -F -e -d mlx5_1 -x 3 -s 1000000 -n 10000 -l 1 -t 1 -p 8888 192.168.1.2
#./ib_write_lat -F -d mlx5_1 -x 3 -s 16 -n 1000000 -l 1 -t 1 -p 9999 192.168.1.2
# run receiver cmd first
wait
echo "DONE"
