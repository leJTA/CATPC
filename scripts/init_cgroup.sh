#!/bin/bash

cgcreate -g cpuset:catpc
cgset -r cpuset.cpus=0-7 catpc
cgset -r cpuset.mems=0-1 catpc
