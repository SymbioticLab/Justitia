#!/bin/bash
## This runs on IB
## Adjust MaxStartups in /etc/ssh/sshd_config (e.g., 60:0:100) to allow more concurrent ssh connections
ib_write_bw=/users/yiwenzhg/frdma/perftest-4.2/ib_write_bw
ib_write_lat=/users/yiwenzhg/frdma/perftest-4.2/ib_write_lat
J_daemon=/users/yiwenzhg/frdma/rdma_pacer/pacer
out_dir=/tmp
ip_sender=10.0.1.1
ip_receiver=10.0.1.2
sender="cp-1"
receiver="cp-2"
bw_size=1000000
lat_size=16
bw_iters=100000
lat_iter=1000000
port_base=8887
lat_port_base=9000
cnt=1
# N-1 big flows & 8 small flows
num_flows=9

if [ "$#" -ne 1 ]; then
  echo "Usage: bash $0.sh <sender/receiver>"
  exit
fi

#if [ $1 == "receiver" ]; then
#  output="$out_dir/pacer_result.txt"
#  cmd="$J_daemon $ip_sender 0 > $output"
#  echo "launch Justitia Daemon on $ip_receiver: $cmd"
#  ssh -o "StrictHostKeyChecking no" -p 22 $receiver $cmd &
#else
#  cmd="$J_daemon $ip_receiver 1 &> /dev/null"
#  echo "launch Justitia Daemon on $ip_sender: $cmd"
#  ssh -o "StrictHostKeyChecking no" -p 22 $sender $cmd &
#  sleep 5
#fi

#./ib_write_bw -F -e -d mlx5_1 -x 3 -s 1000000 -n 10000 -l 1 -t 1 -p 8888 192.168.1.2
#./ib_write_lat -F -d mlx5_1 -x 3 -s 16 -n 1000000 -l 1 -t 1 -p 9999 192.168.1.2
if [ $1 == "sender" ]; then
  for ((i=1; i<=$num_flows; i++)); do
    let port="$port_base + $cnt"
    if [[ $cnt -eq $num_flows ]]; then
      sleep 20
	  for ((j=1; j<=8; j++)); do
	    let port="$lat_port_base + $j"
        output="$out_dir/lat_result_$j.txt"
        cmd="$ib_write_lat -F -s $lat_size -n $lat_iter -l 1 -t 1 --log_off -p $port $ip_receiver |tee $output"
        echo "On $sender: execute $cmd"
        ssh -o "StrictHostKeyChecking no" -p 22 $sender $cmd &
      done
    else
      if [[ $cnt -eq 1 ]] || [[ $cnt -eq 2 ]]; then
	    flow_size=$((bw_size*1000))
	    flow_iter=$((bw_iters/1000))
      elif [[ $cnt -eq 3 ]] || [[ $cnt -eq 4 ]]; then
	    flow_size=$((bw_size*100))
	    flow_iter=$((bw_iters/100))
      elif [[ $cnt -eq 5 ]] || [[ $cnt -eq 6 ]]; then
	    flow_size=$((bw_size*10))
	    flow_iter=$((bw_iters/10))
      else
	    flow_size=$bw_size
	    flow_iter=$bw_iters
	  fi
      sleep 1
      output="$out_dir/bw_result_${flow_size}_$cnt.txt"
      log="$out_dir/bw_log_${flow_size}_$cnt.txt"
      cmd="$ib_write_bw -F -e -s $flow_size -n $flow_iter -l 1 -t 1 -p $port $ip_receiver -L $log |tee $output"
      echo "On $sender: execute $cmd"
      ssh -o "StrictHostKeyChecking no" -p 22 $sender $cmd &
    fi
    let cnt="$cnt + 1"
  done
  wait
  echo "DONE"
elif [ $1 == "receiver" ]; then
  for ((i=1; i<=$num_flows; i++)); do
    let port="$port_base + $cnt"
    if [[ $cnt -eq $num_flows ]]; then
	  for ((j=1; j<=8; j++)); do
	    let port="$lat_port_base + $j"
        cmd="$ib_write_lat -F -s $lat_size -n $lat_iter -l 1 -t 1 -p $port &> /dev/null"
        echo "On $ip_receiver: execute $cmd"
        ssh -o "StrictHostKeyChecking no" -p 22 $receiver $cmd &
      done
	else
      if [[ $cnt -eq 1 ]] || [[ $cnt -eq 2 ]]; then
	    flow_size=$((bw_size*1000))
	    flow_iter=$((bw_iters/1000))
      elif [[ $cnt -eq 3 ]] || [[ $cnt -eq 4 ]]; then
	    flow_size=$((bw_size*100))
	    flow_iter=$((bw_iters/100))
      elif [[ $cnt -eq 5 ]] || [[ $cnt -eq 6 ]]; then
	    flow_size=$((bw_size*10))
	    flow_iter=$((bw_iters/10))
      else
	    flow_size=$bw_size
	    flow_iter=$bw_iters
	  fi
      cmd="$ib_write_bw -F -e -s $flow_size -n $flow_iter -l 1 -t 1 -p $port &> /dev/null"
      echo "On $ip_receiver: execute $cmd"
      ssh -o "StrictHostKeyChecking no" -p 22 $receiver $cmd &
    fi
    let cnt="$cnt + 1"
  done
  echo "DONE"
else
  echo "Usage: bash $0.sh <sender/receiver>"
  exit
fi
