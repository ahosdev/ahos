#ifndef KERNEL_IO_H_
#define KERNEL_IO_H_

#if defined(__i386__)
	#include "../arch/i386/io.h"
#else
	#error "Only ix86 architecture for now"
#endif

#endif
