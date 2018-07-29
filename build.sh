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
${AS} ${BOOTLOADDIR}/stage2.S -o ${BOOTLOADDIR}/stage2.o
${LD} -melf_i386 -Ttext 0x7c00 --oformat=binary ${BOOTLOADDIR}/stage1.o -o ${BOOTLOADDIR}/stage1.bin

# see "https://stackoverflow.com/questions/41563879/ld-errors-while-linking-16-bit-real-mode-code-into-a-multiboot-compliant-elf-exe" why we cant use "-Ttext 0x10000"
${LD} -melf_i386 -Ttext 0x1000 --oformat=binary ${BOOTLOADDIR}/stage2.o -o ${BOOTLOADDIR}/stage2.bin

# TODO: replace this and build a valid MSDOS 4.0 Floppy Disk which holds
# stage-2 and the kernel!
dd if=${BOOTLOADDIR}/stage1.bin of=${DESTDIR}/boot.bin bs=512 count=1
dd if=${BOOTLOADDIR}/stage2.bin of=${DESTDIR}/boot.bin bs=512 count=1 seek=1
