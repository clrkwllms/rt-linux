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
 */
#define imv_read(name)							\
	({								\
		__typeof__(name##__imv) value;				\
		BUILD_BUG_ON(sizeof(value) > 8);			\
		switch (sizeof(value)) {				\
		case 1:							\
			asm(".section __imv,\"a\",@progbits\n\t"	\
				_ASM_PTR "%c1, (3f)-%c2\n\t"		\
				".byte %c2\n\t"				\
				".previous\n\t"				\
				"mov $0,%0\n\t"				\
				"3:\n\t"				\
				: "=q" (value)				\
				: "i" (&name##__imv),			\
				  "i" (sizeof(value)));			\
			break;						\
		case 2:							\
		case 4:							\
			asm(".section __imv,\"a\",@progbits\n\t"	\
				_ASM_PTR "%c1, (3f)-%c2\n\t"		\
				".byte %c2\n\t"				\
				".previous\n\t"				\
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
			asm(".section __imv,\"a\",@progbits\n\t"	\
				_ASM_PTR "%c1, (3f)-%c2\n\t"		\
				".byte %c2\n\t"				\
				".previous\n\t"				\
				"mov $0xFEFEFEFE01010101,%0\n\t" 	\
				"3:\n\t"				\
				: "=r" (value)				\
				: "i" (&name##__imv),			\
				  "i" (sizeof(value)));			\
			break;						\
		};							\
		value;							\
	})

#endif /* _ASM_X86_IMMEDIATE_H */
