#!/bin/bash
for node in $(cat nodes); do
  echo "compile rdma_pacer in $node..."
  ssh -o "StrictHostKeyChecking no" -p 22 $node "cp -r /proj/gaia-PG0/yiwen/frdma/rdma_pacer /users/yiwenzhg/frdma/ && make clean -C frdma/rdma_pacer && make -C frdma/rdma_pacer"
done
wait
echo "DONE"
