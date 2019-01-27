#!/bin/bash
# assume there is a file called "nodes" in cmd which includes all the node hostname
proc="pacer"
for node in $(cat nodes); do
  echo "killing $proc process in $node..."
  pids=`ssh -o 'StrictHostKeyChecking no' -p 22 $node ps -ef | grep $proc | awk '{print $2}'`
  for pid in $pids; do
    echo $pid
    ssh -o "StrictHostKeyChecking no" -p 22 $node "sudo kill -9 $pid"  &
  done
done
wait
echo "DONE"
