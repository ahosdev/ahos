/*
 * registers.h
 *
 * i386 Architecture-specific registers manipulation.
 *
 * TODO: Add static assert checks.
 */

#ifndef ARCH_I386_REGISTERS_H_
#define ARCH_I386_REGISTERS_H_

#include <kernel/types.h>

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

/*
 * Control registers definition (Intel section 2.5).
 */

typedef union
{
	uint32_t val;

	// cr0 definition
	struct {
		uint32_t pe:1; // Protection Enable (0=real mode, 1=protected mode)
		uint32_t mp:1; // Monitor Coprocessor
		uint32_t em:1; // Emulation
		uint32_t ts:1; // Task Switched
		uint32_t et:1; // Extension Type
		uint32_t ne:1; // Numeric Error
		uint32_t pad0:10; // unused
		uint32_t wp:1; // Write Protect
		uint32_t pad1:1; // unused
		uint32_t am:1; // Alignment Mask
		uint32_t pad2:10; // unused
		uint32_t nw:1; // Not Write-through
		uint32_t cd:1; // Cache Disable
		uint32_t pg:1; // Paging
	} __attribute__((packed)) cr0;

	// cr1 is reserved by intel

	// cr2 definition
	struct {
		uint32_t pf_addr; // hold the faulty (virtual) address on page fault
	} __attribute__((packed)) cr2;

	// cr3 definition
	struct {
		uint32_t pad0:3; // unused
		uint32_t pwt:1; // Page-level Writes Transparent
		uint32_t pcd:1; // Page-level Cache Disable
		uint32_t pad1:7;
		uint32_t pdb:20; // Page Directory Base address (physical)
	} __attribute__((packed)) cr3;

	// cr4 definition
	struct {
		uint32_t vme:1; // Virtual-8086 Mode Extensions
		uint32_t pvi:1; // Protected-Mode Virtual Interrupts
		uint32_t tsd:1; // Time Stamp Disable
		uint32_t de:1; // Debugging Extensions
		uint32_t pse:1; // Page Size Extensions
		uint32_t pae:1; // Physical Address Extension
		uint32_t mce:1; // Machine-Check Enable
		uint32_t pge:1; // Page Global Enable
		uint32_t pce:1; // Performance-Monitoring Counter Enable
		uint32_t osfxsr:1; // Operating System Support for FXSAVE/FXRSTOR instructions
		uint32_t osxmmexcpt:1; // OS Support for Unmasked SIMD Float-Pouint32_t Exceptions
		uint32_t reserved:21;
	} __attribute__((packed)) cr4;
} reg_t;

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

#endif /* !ARCH_I386_REGISTERS_H_ */
