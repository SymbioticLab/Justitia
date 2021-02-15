#!/bin/bash

for node in $(cat nodes); do
    echo "set up frdma code and X3 in $node... (remember to compile pacer)"
  ssh -o "StrictHostKeyChecking no" -p 22 $node "cp -r /proj/gaia-PG0/yiwen/frdma /users/yiwenzhg && bash /users/yiwenzhg/frdma/setup_X3.sh" &
done
wait
echo "DONE"
