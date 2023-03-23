#!/bin/bash

sudo ./stop_daemons.sh
sudo ./reset.sh
sudo ./master_daemon
sleep 1
sudo ./slave_daemon 127.0.0.1
