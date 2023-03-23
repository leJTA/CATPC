#!/bin/bash

echo "pkill -15 master_daemon"
pkill -15 master_daemon
#echo -n "" > /tmp/catpc.started.fifo
#echo -n "" > /tmp/catpc.terminated.fifo
rm /tmp/catpc.*.fifo > /dev/null 2>&1
pkill -9 master_daemon
pqos -R
