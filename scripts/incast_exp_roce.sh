#!/bin/bash
ib_write_bw=/proj/rsc-PG0/yiwen/frdma/perftest-4.2/ib_write_bw
ib_write_lat=/proj/rsc-PG0/yiwen/frdma/perftest-4.2/ib_write_lat
out_dir=/tmp
ip_base=192.168.1 # not used here
ip_dst=192.168.1.2
bw_size=1000000
lat_size=16
bw_iters=100000
lat_iter=100000
port_base=8887
ib_dev=mlx5_1
gidx=3
cnt=1

if [ "$#" -ne 1 ]; then
  echo "Usage: bash $0.sh <sender/receiver>"
  exit
fi

#./ib_write_bw -F -e -d mlx5_1 -x 3 -s 1000000 -n 10000 -l 1 -t 1 -p 8888 192.168.1.2
#./ib_write_lat -F -d mlx5_1 -x 3 -s 16 -n 1000000 -l 1 -t 1 -p 9999 192.168.1.2
if [ $1 == "sender" ]; then
  for node in $(cat nodes); do
    let port="$port_base + $cnt"
    #ip="$ip_base.$cnt"
    if [ $cnt -eq 1 ]; then
      output="$out_dir/lat_result_$node.txt"
      cmd="$ib_write_lat -F -d $ib_dev -x $gidx -s $lat_size -n $lat_iter -l 1 -t 1 -p $port $ip_dst > $output"
    else
      output="$out_dir/bw_result_$node.txt"
      log="$out_dir/bw_log_$node.txt"
      cmd="$ib_write_bw -F -e -d $ib_dev -x $gidx -s $bw_size -n $bw_iters -l 1 -t 1 -p $port $ip_dst -L $log > $output"
    fi
    echo "On $node: execute $cmd"
    ssh -o "StrictHostKeyChecking no" -p 22 $node $cmd &
    let cnt="$cnt + 1"
    #((++cnt))
  done
elif [ $1 == "receiver" ]; then
  for node in $(cat nodes); do
    let port="$port_base + $cnt"
    if [ $cnt -eq 1 ]; then
      cmd="$ib_write_lat -F -d $ib_dev -x $gidx -s $lat_size -n $lat_iter -l 1 -t 1 -p $port"
    else
      cmd="$ib_write_bw -F -e -d $ib_dev -x $gidx -s $bw_size -n $bw_iters -l 1 -t 1 -p $port"
    fi
    echo "On $node: execute $cmd"
    ssh -o "StrictHostKeyChecking no" -p 22 $node $cmd &
    let cnt="$cnt + 1"
  done
else
  echo "Usage: bash $0.sh <sender/receiver>"
  exit
fi
wait
echo "DONE"
