#ifndef _ASM_X86_IMMEDIATE_H
#define _ASM_X86_IMMEDIATE_H

/*
 * Immediate values. x86 architecture optimizations.
 *
 * (C) Copyright 2006 Mathieu Desnoyers <mathieu.desnoyers@polymtl.ca>
 *
 * This file is released under the GPLv2.
 * See the file COPYING for more details.
 */

#include <asm/asm.h>

struct __imv {
	unsigned long var;	/* Pointer to the identifier variable of the
				 * immediate value
				 */
	unsigned long imv;	/*
				 * Pointer to the memory location of the
				 * immediate value within the instruction.
				 */
	unsigned char var_size;	/* Type size of variable. */
	unsigned char size;	/* Type size of immediate value. */
	unsigned char insn_size;/* Instruction size. */
} __attribute__ ((packed));

/**
 * imv_read - read immediate variable
 * @name: immediate value name
 *
 * Reads the value of @name.
 * Optimized version of the immediate.
 * Do not use in __init and __exit functions. Use _imv_read() instead.
 * If size is bigger than the architecture long size, fall back on a memory
 * read.
 *
 * Make sure to populate the initial static 64 bits opcode with a value
 * what will generate an instruction with 8 bytes immediate value (not the REX.W
 * prefixed one that loads a sign extended 32 bits immediate value in a r64
 * register).
 *
 * Create the instruction in a discarded section to calculate its size. This is
 * how we can align the beginning of the instruction on an address that will
 * permit atomic modification of the immediate value without knowing the size of
 * the opcode used by the compiler. The operand size is known in advance.
 */
#define imv_read(name)							\
	({								\
		__typeof__(name##__imv) value;				\
		BUILD_BUG_ON(sizeof(value) > 8);			\
		switch (sizeof(value)) {				\
		case 1:							\
			asm(".section __discard,\"\",@progbits\n\t"	\
				"1:\n\t"				\
				"mov $0,%0\n\t"				\
				"2:\n\t"				\
				".previous\n\t"				\
				".section __imv,\"aw\",@progbits\n\t"	\
				_ASM_PTR "%c1, (3f)-%c2\n\t"		\
				".byte %c2, %c2, (2b-1b)\n\t"		\
				".previous\n\t"				\
				"mov $0,%0\n\t"				\
				"3:\n\t"				\
				: "=q" (value)				\
				: "g" (&name##__imv),			\
				  "i" (sizeof(value)));			\
			break;						\
		case 2:							\
		case 4:							\
			asm(".section __discard,\"\",@progbits\n\t"	\
				"1:\n\t"				\
				"mov $0,%0\n\t"				\
				"2:\n\t"				\
				".previous\n\t"				\
				".section __imv,\"aw\",@progbits\n\t"	\
				_ASM_PTR "%c1, (3f)-%c2\n\t"		\
				".byte %c2, %c2, (2b-1b)\n\t"		\
				".previous\n\t"				\
				".org . + ((-.-(2b-1b)) & (%c2-1)), 0x90\n\t" \
				"mov $0,%0\n\t"				\
				"3:\n\t"				\
				: "=r" (value)				\
				: "g" (&name##__imv),			\
				  "i" (sizeof(value)));			\
			break;						\
		case 8:							\
			if (sizeof(long) < 8) {				\
				value = name##__imv;			\
				break;					\
			}						\
			asm(".section __discard,\"\",@progbits\n\t"	\
				"1:\n\t"				\
				"mov $0xFEFEFEFE01010101,%0\n\t"	\
				"2:\n\t"				\
				".previous\n\t"				\
				".section __imv,\"aw\",@progbits\n\t"	\
				_ASM_PTR "%c1, (3f)-%c2\n\t"		\
				".byte %c2, %c2, (2b-1b)\n\t"		\
				".previous\n\t"				\
				".org . + ((-.-(2b-1b)) & (%c2-1)), 0x90\n\t" \
				"mov $0xFEFEFEFE01010101,%0\n\t" 	\
				"3:\n\t"				\
				: "=r" (value)				\
				: "g" (&name##__imv),			\
				  "i" (sizeof(value)));			\
			break;						\
		};							\
		value;							\
	})

/*
 * Uses %eax.
 * immediate value size is declared as 0.
 * Use in
 * if (unlikely(imv_cond(var))) {
 *   imv_cond_end();
 *   ...
 * } else {
 *   imv_cond_end();
 *   ...
 * }
 * Given a char as argument.
 * If the expected code pattern insuring correct liveliness of ZF and %eax isn't
 * met, fallback on standard immediate value.
 * patches the 5 bytes mov for a e9 XX XX XX XX (near jump)
 * Note : Patching the the 4 bytes immediate value with 1 byte variable
 * on fallback.
 */
#define imv_cond(name)							\
	({								\
		uint32_t value;						\
		BUILD_BUG_ON(sizeof(__typeof__(name##__imv)) > 1);	\
		asm (".section __discard,\"\",@progbits\n\t"		\
			"1:\n\t"					\
			"mov $0,%0\n\t"					\
			"2:\n\t"					\
			".previous\n\t"					\
			".section __imv,\"aw\",@progbits\n\t"		\
			_ASM_PTR "%c1, (3f)-%c2\n\t"			\
			".byte %c3, 0, (2b-1b)\n\t"			\
			".previous\n\t"					\
			"mov $0,%0\n\t"					\
			"3:\n\t"					\
			: "=a" (value)					\
			: "g" (&name##__imv),				\
			  "i" (sizeof(value)),				\
			  "i" (sizeof(__typeof__(name##__imv))));	\
		value;							\
	})

/*
 * Make sure the %eax register and ZF are not live anymore at the current
 * address, which is declared in the __imv_cond_end section.
 * All asm statements clobbers the flags, but add "cc" clobber just to be sure.
 * Clobbers %eax.
 */
#define imv_cond_end()							\
	do {								\
		asm (".section __imv_cond_end,\"a\",@progbits\n\t"	\
				_ASM_PTR "1f\n\t"			\
				".previous\n\t"				\
				"1:\n\t"				\
				: : : "eax", "cc");			\
	} while (0)

extern int arch_imv_update(struct __imv *imv, int early);

#endif /* _ASM_X86_IMMEDIATE_H */
