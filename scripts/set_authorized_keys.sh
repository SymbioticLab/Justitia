#!/bin/bash
# This script sets up ssh for root mode on cloudlab clusters. (only the first node can ssh to the rest)
# run as normal user NOT ROOT on cloudlab
# assume the first node HAS REGENERATED ssh key as root
# there needs to be a file called "nodes" with all the hostnames (doesn't matter to include the first node)
sudo cp /root/.ssh/id_rsa.pub ~
for node in $(cat nodes); do
  echo "copying root's pubkey to $node..."
  scp ~/id_rsa.pub $node:~
  echo "append pubkey to root's authorized_keys in $node..."
  ssh -o "StrictHostKeyChecking no" -p 22 $node "cat ~/id_rsa.pub | sudo tee -a /root/.ssh/authorized_keys" &
done
wait
echo "DONE"
