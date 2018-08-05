/*
	floppy_fat12_desc.S

	Holds the BPB information for a Floppy disk formated with FAT12. This
	common to both 'stage1' and 'stage2'.
*/


# The following macro is required to directly use values in assembly without
# dereferencing a label. For some reasons, we can't use it with ".ascii",
# nor multiple bytes expressions (e.g. 'A', B','C') since there is no variadic
# expression in GNU as. I don't know if there is a better way to do this with
# GNU as syntax without switching to Intel syntax noprefix mode (I like AT&T).

# XXX: can we actually do not use this and use label as memory location ?

.macro decl_var name type value
	.set \name, \value
	\type \value
.endm



/*
BIOS Parameter Block (BPB).

Describes the physical layout of a data storage volume. In our case
this is a 1.44mb floppy disk (3 1/2).

This should start at offset 0xb of sector 0.

See: https://en.wikipedia.org/wiki/BIOS_parameter_block
*/

	 BPB_OEM_NAME:			.ascii 	"Ah!OS  " # padded so BPB starts at 0xb
decl_var BPB_BYTES_PER_SECTOR 		.hword 	512	# sector size for floppy disk is 512
decl_var BPB_SECTORS_PER_CLUSTER	.byte  	1
decl_var BPB_RESERVED_SECTORS		.hword 	1	# One extra sector for bootloader
decl_var BPB_NB_FATS			.byte 	2 	# FAT-12 has two FATs
decl_var BPB_ROOT_ENTRIES		.hword	224	# Maximum entries in root directory
decl_var BPB_TOTAL_SECTORS		.hword	2880	# Max. sectors in a floppy disk
decl_var BPB_MEDIA			.byte	0xf0	# XXX: Is this correct ?!
decl_var BPB_SECTORS_PER_FAT		.hword 	9
decl_var BPB_SECTORS_PER_TRACK		.hword 	18
decl_var BPB_HEADS_PER_CYLINDER		.hword 	2	# two-sided
decl_var BPB_HIDDEN_SECTORS		.long 	0
decl_var BPB_TOTAL_SECTORS_BIG		.long 	0
decl_var BPB_DRIVE_NUMBER		.byte	0 	# starts of Extended Boot Record (EBPB)
decl_var BPB_UNUSED			.byte	0
decl_var BPB_EXT_BOOT_SIGNATURE		.byte 	0x29	 # MS 4.1
decl_var BPB_SERIAL_NUMBER		.long	0xa0a1a2a3
	 BPB_VOLUME_LABEL:		.byte 	'M','O','S',' ','F','L','O','P','P','Y',' '
	 BPB_FILE_SYSTEM:		.byte 	'F','A','T','1','2',' ',' ',' '

