#!/bin/sh
set -e
. ./config.sh

for PROJECT in $PROJECTS; do
  (cd $PROJECT && $MAKE clean)
done

rm -rf sysroot
rm -rf isodir
rm -rf ahos.iso

# clean bootloader files
rm -f ${BOOTLOADDIR}/floppy.bin
rm -f ${BOOTLOADDIR}/stage1.o ${BOOTLOADDIR}/stage1.bin
rm -f ${BOOTLOADDIR}/stage2.o ${BOOTLOADDIR}/stage2.bin
