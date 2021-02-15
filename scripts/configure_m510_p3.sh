#!/bin/bash

for node in $(cat nodes); do
  echo "upgrade mlnx driver in $node..."
  ssh -o "StrictHostKeyChecking no" -p 22 $node "sudo bash -c ' echo 1 | tee /sys/kernel/debug/mlx4_ib/mlx4_0/ecn/*/ports/*/params/*/*/enable'" &
done
wait
echo "DONE"
