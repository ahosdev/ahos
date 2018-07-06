#!/bin/sh
set -e
. ./build.sh

mkdir -p isodir
mkdir -p isodir/boot
mkdir -p isodir/boot/grub

cp sysroot/boot/ahos.kernel isodir/boot/ahos.kernel
cat > isodir/boot/grub/grub.cfg << EOF
menuentry "Ah!OS" {
	multiboot /boot/ahos.kernel
}
EOF
grub-mkrescue -o ahos.iso isodir
