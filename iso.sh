#!/bin/sh
set -e
. ./build.sh

mkdir -p isodir
mkdir -p isodir/boot
mkdir -p isodir/boot/grub

cp sysroot/boot/ahos.kernel isodir/boot/ahos.kernel
cp sysroot/boot/symbols.map isodir/boot/symbols.map

cat > isodir/boot/grub/grub.cfg << EOF
set timeout=0
menuentry "Ah!OS" {
	multiboot /boot/ahos.kernel
	module /boot/symbols.map
}
EOF

grub-mkrescue -o ahos.iso isodir
