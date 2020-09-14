#!/bin/bash
for node in $(cat nodes); do
  echo "compile rdma_pacer in $node..."
  ssh -o "StrictHostKeyChecking no" -p 22 $node "make -C frdma/rdma_pacer"
done
wait
echo "DONE"
