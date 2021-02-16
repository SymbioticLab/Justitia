#!/bin/bash

#TODO: test if openibd restart can be replaced by reboot since on m510 openibd restart will hang and a reboot is needed anyway
for node in $(cat nodes); do
  echo "restart mlnx driver in $node..."
  ssh -o "StrictHostKeyChecking no" -p 22 $node "sudo /etc/init.d/openibd restart" &
done
wait
echo "DONE"
