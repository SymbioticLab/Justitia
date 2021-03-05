#!/bin/bash
# assume there is a file called "nodes" in cmd which includes all the node hostname
shm_path="/dev/shm/rdma-fairness /var/run/shm/rdma-fairness"
for node in $(cat nodes); do
  echo "deleting shm in $node..."
  for path in $shm_path; do
    ssh -o "StrictHostKeyChecking no" -p 22 $node "sudo rm -f $path"  &
  done
done
wait
echo "DONE"
