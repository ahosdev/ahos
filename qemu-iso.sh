#!/bin/sh
set -e
. ./iso.sh

qemu-system-$(./target-triplet-to-arch.sh $HOST) \
	-cdrom ahos.iso \
	-d guest_errors \
	-serial stdio \
	-no-reboot
