#!/bin/bash
cnt=1
for node in $(cat nodes); do
  ip="10.0.0.$cnt"
  echo "setting ip addr for ib0 as $ip in $node..."
  ssh -o "StrictHostKeyChecking no" -p 22 $node "sudo ifconfig ib0 $ip" &
  ((++cnt))
done
wait
echo "DONE"
