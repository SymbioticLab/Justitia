#!/bin/bash

# used when some cloudlab node get hang on openibd restart and thus need to manually reboot from the web interface. This alternative script saves the time to find that node icon to reboot manually
for node in $(cat nodes); do
  echo "reboot $node..."
  ssh -o "StrictHostKeyChecking no" -p 22 $node "sudo reboot" &
done
wait
echo "DONE"
