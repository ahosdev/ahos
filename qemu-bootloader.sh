#!/bin/sh
set -e
. ./build.sh

#qemu-system-$(./target-triplet-to-arch.sh $HOST) -fda sysroot/boot/boot.bin -d guest_errors -no-reboot
qemu-system-$(./target-triplet-to-arch.sh $HOST) -fda sysroot/boot/boot.bin -d guest_errors #-no-reboot
