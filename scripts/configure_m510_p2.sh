#!/bin/bash

for node in $(cat nodes); do
  echo "restart mlnx driver in $node..."
  ssh -o "StrictHostKeyChecking no" -p 22 $node "sudo /etc/init.d/openibd restart" &
done
wait
echo "DONE"
