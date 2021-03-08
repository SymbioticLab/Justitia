#!/bin/bash
## CloudLab r320 IB
ib_write_bw=/users/yiwenzhg/frdma/perftest-4.2/ib_write_bw
ib_write_lat=/users/yiwenzhg/frdma/perftest-4.2/ib_write_lat
J_daemon=/users/yiwenzhg/frdma/rdma_pacer/pacer
out_dir=/tmp
num_justitia_flow=5    # the last one is a latency app; others are bw
num_justitia_bw_flow=$((num_justitia_flow-1))
sender_node=cpn1
receiver_node=cpn2
ip_dst=192.168.0.12     # hardcoded
bw_size=1000000
lat_size=16
bw_iters=20000
lat_iter=1000000
port_base=5000
#cnt=1

# Justitia flow receiver
for (( i=1; i<=$num_justitia_flow; i++ )); do
    let port="$port_base + $i"
    if [[ $i -eq $num_justitia_flow ]]; then
        cmd="$ib_write_lat -F -s $lat_size -n $lat_iter -l 1 -t 1 -p $port &> /dev/null"
    else
        cmd="$ib_write_bw -F -e -s $bw_size -n $bw_iters -l 1 -t 1 -p $port &> /dev/null"
    fi
    echo "On $receiver_node: execute $cmd"
    ssh -o "StrictHostKeyChecking no" -p 22 $receiver_node $cmd &
    sleep 1
done
echo "Justitia receiver DONE"

# Justitia flow sender
for (( i=1; i<=$num_justitia_flow; i++ )); do
    let port="$port_base + $i"
    if [[ $i -eq $num_justitia_flow ]]; then
        sleep 2
        output="$out_dir/lat_result_weight_J_$sender_node.txt"
        cmd="$ib_write_lat -F -s $lat_size -n $lat_iter --log_off -l 1 -t 1 -p $port $ip_dst |tee $output"
    else
        sleep 1
        output="$out_dir/bw_result_weight_J_$sender_node_$i.txt"
        log="$out_dir/bw_log_large_$sender_node_$i.txt"
        cmd="$ib_write_bw -F -e -s $bw_size -n $bw_iters -l 1 -t 1 -p $port $ip_dst -L $log |tee $output"
    fi
    echo "On $sender_node: execute $cmd"
    ssh -o "StrictHostKeyChecking no" -p 22 $sender_node $cmd &
done
echo "Justitia sender DONE"

wait
echo "DONE"
