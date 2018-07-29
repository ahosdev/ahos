#!/bin/sh
set -e
. ./headers.sh

for PROJECT in $PROJECTS; do
  (cd $PROJECT && DESTDIR="$SYSROOT" $MAKE install)
done

export DESTDIR=$SYSROOT/boot
echo $BOOTLOADDIR
echo $DESTDIR

# build the bootloader
${AS} ${BOOTLOADDIR}/stage1.S -o ${BOOTLOADDIR}/stage1.o
${LD} -Ttext 0x7c00 --oformat=binary ${BOOTLOADDIR}/stage1.o -o ${BOOTLOADDIR}/stage1.bin
dd if=${BOOTLOADDIR}/stage1.bin of=${DESTDIR}/boot.bin bs=512 count=2
