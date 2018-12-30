#!/bin/bash
# on cloudlab, do not run as root
for node in $(cat nodes); do
  echo "installing gcc5 in $node..."
  ssh -o "StrictHostKeyChecking no" -p 22 $node "sudo apt-get -y update && sudo apt-get -y install software-properties-common && sudo add-apt-repository -y ppa:ubuntu-toolchain-r/test && sudo apt-get -y update && sudo apt-get install -y gcc-5 g++-5 && sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-5 60 --slave /usr/bin/g++ g++ /usr/bin/g++-5"
done
wait
echo "DONE"