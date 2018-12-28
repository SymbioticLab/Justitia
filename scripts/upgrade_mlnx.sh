#!/bin/bash

for node in $(cat nodes); do
  echo "installing openmpi in $node..."
  ssh -o "StrictHostKeyChecking no" -p 22 $node "sudo /proj/gaia-PG0/yiwen/MLNX_OFED_LINUX-4.2-1.0.0.0-ubuntu14.04-x86_64/mlnxofedinstall --force && sudo /proj/gaia-PG0/yiwen/MLNX_OFED_LINUX-4.2-1.0.0.0-ubuntu14.04-x86_64/etc/init.d/openibd restart" &
done
wait
echo "DONE"
