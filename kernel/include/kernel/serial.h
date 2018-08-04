#ifndef _KERNEL_SERIAL_H_
#define _KERNEL_SERIAL_H_

#include <stddef.h>

void serial_init(void);
void serial_write(const char *data, size_t size);

#endif /* _KERNEL_SERIAL_H_ */