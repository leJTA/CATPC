#!/bin/bash

cmdline=$(echo $1 | sed 's/ //g')
flock /tmp/catpc.terminated.fifo echo -n $cmdline > /tmp/catpc.terminated.fifo
echo "remove app : $cmdline"