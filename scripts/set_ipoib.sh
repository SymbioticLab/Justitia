#!/bin/bash
for node in $(cat nodes); do
  echo "installing ipoib in $node..."
  ssh -o "StrictHostKeyChecking no" -p 22 $node "sudo modprobe ib_ipoib" &
done
wait
echo "DONE"
