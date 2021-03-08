#!/bin/bash
## CloudLab m510 RoCEv2
## "nodes" file should also include the last receiving node
ib_write_bw=/users/yiwenzhg/frdma/perftest-4.2/ib_write_bw
ib_write_lat=/users/yiwenzhg/frdma/perftest-4.2/ib_write_lat
out_dir=/tmp
num_justitia_flow=9    # the last one is a latency app; others are bw
num_justitia_bw_flow=$((num_justitia_flow-1))
sender_node=cpa1
receiver_node=cpb1
ip_dst=192.168.0.5     # hardcoded
bw_size=1000000
lat_size=16
normal_bw_iters=500000
bw_iters=30000
lat_iter=1000000
port_base=5000
normal_port=$((port_base+100))
#cnt=1


#24 1-to-1 bw flow receiver
for node in $(cat normal_receiver_nodes); do
    cmd="$ib_write_bw -F -e -s $bw_size -n $normal_bw_iters -x 3 -i 2 -l 1 -t 1 -p $normal_port &> /dev/null"
    echo "On normal receiver $node: execute $cmd"
    ssh -o "StrictHostKeyChecking no" -p 22 $node $cmd &
    sleep 0.5
done
echo "normal receiver DONE"

# Justitia flow receiver
for (( i=1; i<=$num_justitia_flow; i++ )); do
    let port="$port_base + $i"
    if [[ $i -eq $num_justitia_flow ]]; then
        cmd="$ib_write_lat -F -s $lat_size -n $lat_iter -x 3 -i 2 -l 1 -t 1 -p $port &> /dev/null"
    else
        cmd="$ib_write_bw -F -e -s $bw_size -n $bw_iters -x 3 -i 2 -l 1 -t 1 -p $port &> /dev/null"
    fi
    echo "On $receiver_node: execute $cmd"
    ssh -o "StrictHostKeyChecking no" -p 22 $receiver_node $cmd &
    sleep 0.5
done
echo "Justitia receiver DONE"

#24 1-to-1 bw flow sender
for x in $(cat normal_sender_node_receiver_ip); do
    node=$(echo $x | cut -d ":" -f 1)
    receiver_ip=$(echo $x | cut -d ":" -f 2)
    output="$out_dir/bw_result_normal_$node.txt"
    log="$out_dir/bw_log_normal_$node.txt"
    cmd="$ib_write_bw -F -e -s $bw_size -n $normal_bw_iters -x 3 -i 2 -l 1 -t 1 -p $normal_port $receiver_ip -L $log |tee $output"
    echo "On $node: execute $cmd"
    sleep 0.5
    ssh -o "StrictHostKeyChecking no" -p 22 $node $cmd &
done
echo "normal sender DONE"

# Justitia flow sender
for (( i=1; i<=$num_justitia_flow; i++ )); do
    let port="$port_base + $i"
    if [[ $i -eq $num_justitia_flow ]]; then
        sleep 3
        output="$out_dir/lat_result_large_$sender_node.txt"
        cmd="$ib_write_lat -F -s $lat_size -n $lat_iter -x 3 -i 2 --log_off -l 1 -t 1 -p $port $ip_dst |tee $output"
    else
        sleep 1
        output="$out_dir/bw_result_large_$sender_node_$i.txt"
        log="$out_dir/bw_log_large_$sender_node_$i.txt"
        cmd="$ib_write_bw -F -e -s $bw_size -n $bw_iters -x 3 -i 2 -l 1 -t 1 -p $port $ip_dst -L $log |tee $output"
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
