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
	int  jmp_off;		/* offset for jump target */
	unsigned char size;	/* Type size. */
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
				".int 0\n\t"				\
				".byte %c2, (2b-1b)\n\t"		\
				".previous\n\t"				\
				"mov $0,%0\n\t"				\
				"3:\n\t"				\
				: "=q" (value)				\
				: "i" (&name##__imv),			\
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
				".int 0\n\t"				\
				".byte %c2, (2b-1b)\n\t"		\
				".previous\n\t"				\
				".org . + ((-.-(2b-1b)) & (%c2-1)), 0x90\n\t" \
				"mov $0,%0\n\t"				\
				"3:\n\t"				\
				: "=r" (value)				\
				: "i" (&name##__imv),			\
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
				".int 0\n\t"				\
				".byte %c2, (2b-1b)\n\t"		\
				".previous\n\t"				\
				".org . + ((-.-(2b-1b)) & (%c2-1)), 0x90\n\t" \
				"mov $0xFEFEFEFE01010101,%0\n\t" 	\
				"3:\n\t"				\
				: "=r" (value)				\
				: "i" (&name##__imv),			\
				  "i" (sizeof(value)));			\
			break;						\
		};							\
		value;							\
	})

/*
 * Uses %al.
 * size is 0.
 * Use in if (unlikely(imv_cond(var)))
 * Given a char as argument.
 */
#define imv_cond(name)							\
	({								\
		__typeof__(name##__imv) value;				\
		BUILD_BUG_ON(sizeof(value) > 1);			\
		asm (".section __discard,\"\",@progbits\n\t"		\
			"1:\n\t"					\
			"mov $0,%0\n\t"					\
			"2:\n\t"					\
			".previous\n\t"					\
			".section __imv,\"aw\",@progbits\n\t"		\
			_ASM_PTR "%c1, (3f)-1\n\t"			\
			".int 0\n\t"					\
			".byte %c2, (2b-1b)\n\t"			\
			".previous\n\t"					\
			"mov $0,%0\n\t"					\
			"3:\n\t"					\
			: "=a" (value)					\
			: "i" (&name##__imv),				\
			  "i" (0));					\
		value;							\
	})

extern int arch_imv_update(struct __imv *imv, int early);

#endif /* _ASM_X86_IMMEDIATE_H */
