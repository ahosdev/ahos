/* The bootloader will look at this image and start execution at the symbol
   designated at the entry point. */
ENTRY(_start)

/* Tell where the various sections of the object files will be put in the final
   kernel image. */
SECTIONS
{
	/* Begin putting sections at 1 MiB, a conventional place for kernels to be
	   loaded at by the bootloader. */
	. = 1M;

	kernel_start_ldsym = . ;

	/* First put the multiboot header, as it is required to be put very early
	   early in the image or the bootloader won't recognize the file format.
	   Next we'll put the .text section. */
	.text BLOCK(4K) : ALIGN(4K)
	{
		*(.multiboot)

		kernel_code_start_ldsym = . ;
		*(.text)
		kernel_code_end_ldsym = . ;
	}

	/* Read-only data. */
	.rodata BLOCK(4K) : ALIGN(4K)
	{
		kernel_rodata_start_ldsym = . ;
		*(.rodata)
		kernel_rodata_end_ldsym = . ;
	}

	/* Read-write data (initialized) */
	.data BLOCK(4K) : ALIGN(4K)
	{
		kernel_data_start_ldsym = . ;
		*(.data)
		kernel_data_end_ldsym = . ;
	}

	/* Read-write data (uninitialized) and stack */
	.bss BLOCK(4K) : ALIGN(4K)
	{
		kernel_bss_start_ldsym = . ;
		*(COMMON)
		*(.bss)
		kernel_bss_end_ldsym = . ;
	}

	kernel_end_ldsym = . ;

	/* The compiler may produce other sections, put them in the proper place in
	   in this file, if you'd like to include them in the final kernel. */
}
