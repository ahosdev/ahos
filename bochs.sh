#!/bin/sh
set -e 
. ./iso.sh

# the LD_PRELOAD is a work around for some unknown BOCH bug
# see: https://bugs.launchpad.net/ubuntu/+source/bochs/+bug/980167
LD_PRELOAD=/usr/lib/x86_64-linux-gnu/libXpm.so.4 bochs -f ./bochs.conf
