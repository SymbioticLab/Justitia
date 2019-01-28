#!/bin/bash
## "nodes" file should not include the last receiving node
ib_write_bw=/proj/rsc-PG0/yiwen/frdma/perftest-4.2/ib_write_bw
ib_write_lat=/proj/rsc-PG0/yiwen/frdma/perftest-4.2/ib_write_lat
J_daemon=/proj/rsc-PG0/yiwen/frdma/rdma_pacer/pacer
out_dir=/tmp
ip_dst=10.10.1.10
receiver=10.10.1.10
bw_size=1000000
lat_size=16
bw_iters=8000
lat_iter=5000000
port_base=8887
ib_dev=mlx5_1
gidx=5
cnt=1
num_senders=$(wc -l < nodes)
#echo $num_senders
if [ "$#" -ne 1 ]; then
  echo "Usage: bash $0.sh <sender/receiver>"
  exit
fi


if [ $1 == "receiver" ]; then
  for node in $(cat nodes); do
    #IP addr in this hack is a don't-care; no server is needed
    cmd="$J_daemon 10.10.1.10 1 $gidx"
    echo "launch Justitia Daemon on $node: $cmd"
    ssh -o "StrictHostKeyChecking no" -p 22 $node $cmd &
  done
fi

#./ib_write_bw -F -e -d mlx5_1 -x 3 -s 1000000 -n 10000 -l 1 -t 1 -p 8888 192.168.1.2
#./ib_write_lat -F -d mlx5_1 -x 3 -s 16 -n 1000000 -l 1 -t 1 -p 9999 192.168.1.2
if [ $1 == "sender" ]; then
  for node in $(cat nodes); do
    let port="$port_base + $cnt"
    if [[ $cnt -eq $num_senders ]]; then
      sleep 1
      output="$out_dir/lat_result_Justitia_$node.txt"
      cmd="export LD_LIBRARY_PATH=/usr/lib64; $ib_write_lat -F -d $ib_dev -x $gidx -s $lat_size -n $lat_iter -l 1 -t 1 -p $port -S 3 $ip_dst --log_off 2>&1 |tee $output"
    else
      sleep 1
      output="$out_dir/bw_result_Justitia_$node.txt"
      log="$out_dir/bw_log_Justitia_$node.txt"
      cmd="export LD_LIBRARY_PATH=/usr/lib64; $ib_write_bw -F -e -d $ib_dev -x $gidx -s $bw_size -n $bw_iters -l 1 -t 1 -p $port -S 3 $ip_dst -L $log 2>&1 |tee $output"
    fi
    echo "On $node: execute $cmd"
    ssh -o "StrictHostKeyChecking no" -p 22 $node $cmd &
    let cnt="$cnt + 1"
    #((++cnt))
  done
  wait
  echo "DONE"
elif [ $1 == "receiver" ]; then
  for node in $(cat nodes); do
    let port="$port_base + $cnt"
    if [[ $cnt -eq $num_senders ]]; then
      cmd="export LD_LIBRARY_PATH=/usr/lib64; $ib_write_lat -F -d $ib_dev -x $gidx -s $lat_size -n $lat_iter -l 1 -t 1 -p $port -S 3 &> /dev/null"
    else
      cmd="export LD_LIBRARY_PATH=/usr/lib64; $ib_write_bw -F -e -d $ib_dev -x $gidx -s $bw_size -n $bw_iters -l 1 -t 1 -p $port -S 3 &> /dev/null"
    fi
    echo "On $receiver: execute $cmd"
    ssh -o "StrictHostKeyChecking no" -p 22 $receiver $cmd &
    let cnt="$cnt + 1"
  done
  echo "DONE"
else
  echo "Usage: bash $0.sh <sender/receiver>"
  exit
fi
