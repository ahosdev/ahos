/*
 * atomic.h
 *
 * Basic atomic operations.
 *
 * Mostly stolen from Linux (shame on me!).
 */

#ifndef ARCH_I386_ATOMIC_H_
#define ARCH_I386_ATOMIC_H_

#include <kernel/types.h>

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

typedef struct {
	int32_t value;
} atomic_t;

// ----------------------------------------------------------------------------

#define __READ_ONCE_SIZE						\
({									\
	switch (size) {							\
	case 1: *(uint8_t *)res = *(volatile uint8_t *)p; break;		\
	case 2: *(uint16_t *)res = *(volatile uint16_t *)p; break;		\
	case 4: *(uint32_t *)res = *(volatile uint32_t *)p; break;		\
	case 8: *(uint64_t *)res = *(volatile uint64_t *)p; break;		\
	default:							\
		asm volatile ("": : :"memory"); /* memory barrier */ \
		__builtin_memcpy((void *)res, (const void *)p, size);	\
		asm volatile ("": : :"memory"); /* memory barrier */ \
	}								\
})

static __attribute__((always_inline))
inline void __read_once_size_nocheck(const volatile void *p, void *res, int size)
{
	__READ_ONCE_SIZE;
}

#define READ_ONCE(x) \
({ \
	union {typeof(x) __val; char __c[1]; } __u; \
	__read_once_size_nocheck(&(x), __u.__c, sizeof(x)); \
	__u.__val; \
})

__attribute__((always_inline))
static inline int32_t atomic_read(atomic_t *v)
{
	return READ_ONCE((v)->value);
}

// ----------------------------------------------------------------------------

static __attribute__((always_inline))
inline void __write_once_size(volatile void *p, void *res, int size)
{
	switch (size) {
	case 1: *(volatile uint8_t *)p = *(uint8_t *)res; break;
	case 2: *(volatile uint16_t *)p = *(uint16_t *)res; break;
	case 4: *(volatile uint32_t *)p = *(uint32_t *)res; break;
	case 8: *(volatile uint64_t *)p = *(uint64_t *)res; break;
	default:
		asm volatile ("": : :"memory"); /* memory barrier */
		__builtin_memcpy((void *)p, (const void *)res, size);
		asm volatile ("": : :"memory"); /* memory barrier */
	}
}

#define WRITE_ONCE(x, val) \
({							\
	union { typeof(x) __val; char __c[1]; } __u =	\
		{ .__val = (typeof(x)) (val) }; \
	__write_once_size(&(x), __u.__c, sizeof(x));	\
	__u.__val;					\
})

__attribute__((always_inline))
static inline void atomic_write(atomic_t *v, int32_t new_val)
{
	WRITE_ONCE(v->value, new_val);
}

// ----------------------------------------------------------------------------

__attribute__((always_inline))
static inline void atomic_inc(atomic_t *v)
{
	asm volatile ("lock incl %0" : "+m" (v->value));
}

// ----------------------------------------------------------------------------

__attribute__((always_inline))
static inline void atomic_dec(atomic_t *v)
{
	asm volatile ("lock decl %0" : "+m" (v->value));
}

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

#endif /* !ARCH_I386_ATOMIC_H_ */
