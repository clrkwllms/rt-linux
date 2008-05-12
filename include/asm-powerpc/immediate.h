#ifndef _ASM_POWERPC_IMMEDIATE_H
#define _ASM_POWERPC_IMMEDIATE_H

/*
 * Immediate values. PowerPC architecture optimizations.
 *
 * (C) Copyright 2006 Mathieu Desnoyers <mathieu.desnoyers@polymtl.ca>
 *
 * This file is released under the GPLv2.
 * See the file COPYING for more details.
 */

#include <asm/asm-compat.h>

/**
 * imv_read - read immediate variable
 * @name: immediate value name
 *
 * Reads the value of @name.
 * Optimized version of the immediate.
 * Do not use in __init and __exit functions. Use _imv_read() instead.
 */
#define imv_read(name)							\
	({								\
		__typeof__(name##__imv) value;				\
		BUILD_BUG_ON(sizeof(value) > 8);			\
		switch (sizeof(value)) {				\
		case 1:							\
			asm(".section __imv,\"aw\",@progbits\n\t"	\
					PPC_LONG "%c1, ((1f)-1)\n\t"	\
					".byte 1\n\t"			\
					".previous\n\t"			\
					"li %0,0\n\t"			\
					"1:\n\t"			\
				: "=r" (value)				\
				: "i" (&name##__imv));			\
			break;						\
		case 2:							\
			asm(".section __imv,\"aw\",@progbits\n\t"	\
					PPC_LONG "%c1, ((1f)-2)\n\t"	\
					".byte 2\n\t"			\
					".previous\n\t"			\
					"li %0,0\n\t"			\
					"1:\n\t"			\
				: "=r" (value)				\
				: "i" (&name##__imv));			\
			break;						\
		case 4:							\
		case 8:	value = name##__imv;				\
			break;						\
		};							\
		value;							\
	})

#endif /* _ASM_POWERPC_IMMEDIATE_H */
