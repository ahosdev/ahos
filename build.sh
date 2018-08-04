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
#${LD} -melf_i386 -Ttext 0x7c00 --oformat=binary ${BOOTLOADDIR}/stage1.o -o ${BOOTLOADDIR}/stage1.bin
${LD} -Ttext 0x7c00 --oformat=binary ${BOOTLOADDIR}/stage1.o -o ${BOOTLOADDIR}/stage1.bin
# see "https://stackoverflow.com/questions/41563879/ld-errors-while-linking-16-bit-real-mode-code-into-a-multiboot-compliant-elf-exe" why we cant use "-Ttext 0x10000"
${LD} -melf_i386 -Ttext 0x1000 --oformat=binary ${BOOTLOADDIR}/stage2.o -o ${BOOTLOADDIR}/stage2.bin


# create a floppy disk
rm -f ${BOOTLOADDIR}/floppy.bin
# WARNING: any modification on this line implies to modify the stage1 as well
mkfs.msdos -C ${BOOTLOADDIR}/floppy.bin $((2880/2)) -D 0 -f 2 -F 12 -h 0 -i a0a1a2a3 -M 0xF0 -n "MOS FLOPPY " -r 224 -R 1 -s 1 -S 512 -v

# mount it
mkdir -p ${BOOTLOADDIR}/mnt_floppy
sudo mount -o loop -t msdos ${BOOTLOADDIR}/floppy.bin ${BOOTLOADDIR}/mnt_floppy

# push stage 2 inside it
# Check we can read the second sector of rootdir
#python -c "print 'A'*2048" > /dev/shm/tmp-floppy-haaaa1
#sudo cp /dev/shm/tmp-floppy-haaaa1 ${BOOTLOADDIR}/mnt_floppy/haaaa1
#python -c "print 'B'*512" > /dev/shm/tmp-floppy-haaaa2
#sudo cp /dev/shm/tmp-floppy-haaaa2 ${BOOTLOADDIR}/mnt_floppy/haaaa2
#sudo touch ${BOOTLOADDIR}/mnt_floppy/haaaa2
#sudo touch ${BOOTLOADDIR}/mnt_floppy/haaaa3
#sudo touch ${BOOTLOADDIR}/mnt_floppy/haaaa4
#sudo touch ${BOOTLOADDIR}/mnt_floppy/haaaa5
#sudo touch ${BOOTLOADDIR}/mnt_floppy/haaaa6
#sudo touch ${BOOTLOADDIR}/mnt_floppy/haaaa7
#sudo touch ${BOOTLOADDIR}/mnt_floppy/haaaa8
#sudo touch ${BOOTLOADDIR}/mnt_floppy/haaaa9
#sudo touch ${BOOTLOADDIR}/mnt_floppy/haaaae
#sudo touch ${BOOTLOADDIR}/mnt_floppy/haaaad
#sudo touch ${BOOTLOADDIR}/mnt_floppy/haaaar
#sudo touch ${BOOTLOADDIR}/mnt_floppy/haaaaf
#sudo touch ${BOOTLOADDIR}/mnt_floppy/haaaag
#sudo touch ${BOOTLOADDIR}/mnt_floppy/haaaah
#sudo touch ${BOOTLOADDIR}/mnt_floppy/haaaat
sudo cp ${BOOTLOADDIR}/stage2.bin ${BOOTLOADDIR}/mnt_floppy # this should lands in 2nd rootdir sector
#sudo rm ${BOOTLOADDIR}/mnt_floppy/haaaa2 # fragment

# umount it
sudo umount -f ${BOOTLOADDIR}/mnt_floppy
#sleep 3
rmdir ${BOOTLOADDIR}/mnt_floppy

# overwrite its first sector (bootsector) with stage 1
hd ${BOOTLOADDIR}/floppy.bin | head -n 10
dd conv=notrunc if=${BOOTLOADDIR}/stage1.bin of=${BOOTLOADDIR}/floppy.bin bs=512 count=1 seek=0
hd ${BOOTLOADDIR}/floppy.bin | head -n 10

# TODO: replace this and build a valid MSDOS 4.0 Floppy Disk which holds
# stage-2 and the kernel!
#dd if=${BOOTLOADDIR}/stage1.bin of=${DESTDIR}/boot.bin bs=512 count=1
#dd conv=notrunc if=${BOOTLOADDIR}/stage2.bin of=${BOOTLOADDIR}/floppy.bin bs=512 count=1 seek=1

cp ${BOOTLOADDIR}/floppy.bin ${DESTDIR} -v
