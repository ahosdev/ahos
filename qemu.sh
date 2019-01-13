#!/bin/sh
set -e
. ./build.sh

qemu-system-$(./target-triplet-to-arch.sh $HOST) -kernel sysroot/boot/ahos.kernel -d guest_errors -serial stdio -no-reboot
