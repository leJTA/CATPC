#!/bin/bash

pqos -R -r

## Normal
# pqos -e "llc:0=0x7ff"
# pqos -e "llc:1=0x1ff"
# pqos -e "llc:2=0x7f0"
# pqos -e "llc:3=0x01f"
# pqos -e "llc:4=0x01e"
# pqos -e "llc:5=0x0e0"
# pqos -e "llc:6=0x300"
# pqos -e "llc:7=0x400"

## Top Down
pqos -e "llc:0=0x7ff"
pqos -e "llc:1=0x7fe"
pqos -e "llc:2=0x7fc"
pqos -e "llc:3=0x7f0"
pqos -e "llc:4=0x7c0"
pqos -e "llc:5=0x700"
pqos -e "llc:6=0x600"
pqos -e "llc:7=0x7ff"

rm /tmp/*.csv
