#!/bin/bash

cmdline=$(echo $1 | sed 's/ //g')
flock /tmp/catpc.started.fifo echo -n $cmdline > /tmp/catpc.started.fifo
echo "add app : $cmdline"
