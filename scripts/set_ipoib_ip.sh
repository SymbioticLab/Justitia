#!/bin/bash
cnt=11
for node in $(cat nodes); do
  echo "installing ipoib in $node..."
  ssh -o "StrictHostKeyChecking no" -p 22 $node "sudo modprobe ib_ipoib"
  ip="192.168.1.$cnt"
  echo "setting ip addr for ib0 as $ip in $node..."
  ssh -o "StrictHostKeyChecking no" -p 22 $node "sudo ifconfig ib0 $ip/24" &
  ((++cnt))
done
wait
echo "DONE"
