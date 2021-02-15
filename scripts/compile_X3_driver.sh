#!/bin/bash

for node in $(cat nodes); do
    echo "compile and install X3 driver code in $node..."
    ssh -o "StrictHostKeyChecking no" -p 22 $node "cp -r /proj/gaia-PG0/yiwen/frdma/libmlx4 /users/yiwenzhg/frdma/ "
    ssh -o "StrictHostKeyChecking no" -p 22 $node "bash -s" < ../compile_X3.sh
done
wait
echo "DONE"
