#!/bin/sh
set -e
. ./build.sh

qemu-system-$(./target-triplet-to-arch.sh $HOST) -fda sysroot/boot/boot.bin -d guest_errors
