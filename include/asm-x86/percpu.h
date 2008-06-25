#ifndef _ASM_X86_PERCPU_H_
#define _ASM_X86_PERCPU_H_

#ifdef CONFIG_X86_64
#include <linux/compiler.h>

/* Same as asm-generic/percpu.h, except that we store the per cpu offset
   in the PDA. Longer term the PDA and every per cpu variable
   should be just put into a single section and referenced directly
   from %gs */

#ifdef CONFIG_SMP
#include <asm/pda.h>

#define __per_cpu_offset(cpu) (cpu_pda(cpu)->data_offset)
#define __my_cpu_offset read_pda(data_offset)

#define per_cpu_offset(x) (__per_cpu_offset(x))

#endif
#include <asm-generic/percpu.h>

DECLARE_PER_CPU(struct x8664_pda, pda);

#else /* CONFIG_X86_64 */

#ifdef __ASSEMBLY__

/*
 * PER_CPU finds an address of a per-cpu variable.
 *
 * Args:
 *    var - variable name
 *    reg - 32bit register
 *
 * The resulting address is stored in the "reg" argument.
 *
 * Example:
 *    PER_CPU(cpu_gdt_descr, %ebx)
 */
#ifdef CONFIG_SMP
#define PER_CPU(var, reg)				\
	movl %fs:per_cpu__##this_cpu_off, reg;		\
	lea per_cpu__##var(reg), reg
#define PER_CPU_VAR(var)	%fs:per_cpu__##var
#else /* ! SMP */
#define PER_CPU(var, reg)			\
	movl $per_cpu__##var, reg
#define PER_CPU_VAR(var)	per_cpu__##var
#endif	/* SMP */

#else /* ...!ASSEMBLY */

/*
 * PER_CPU finds an address of a per-cpu variable.
 *
 * Args:
 *    var - variable name
 *    cpu - 32bit register containing the current CPU number
 *
 * The resulting address is stored in the "cpu" argument.
 *
 * Example:
 *    PER_CPU(cpu_gdt_descr, %ebx)
 */
#ifdef CONFIG_SMP

#define __my_cpu_offset x86_read_percpu(this_cpu_off)

/* fs segment starts at (positive) offset == __per_cpu_offset[cpu] */
#define __percpu_seg "%%fs:"

#else  /* !SMP */

#define __percpu_seg ""

#endif	/* SMP */

#include <asm-generic/percpu.h>

/* We can use this directly for local CPU (faster). */
DECLARE_PER_CPU(unsigned long, this_cpu_off);

/* For arch-specific code, we can use direct single-insn ops (they
 * don't give an lvalue though). */
extern void __bad_percpu_size(void);

#define percpu_to_op(op, var, val)			\
do {							\
	typedef typeof(var) T__;			\
	if (0) {					\
		T__ tmp__;				\
		tmp__ = (val);				\
	}						\
	switch (sizeof(var)) {				\
	case 1:						\
		asm(op "b %1,"__percpu_seg"%0"		\
		    : "+m" (var)			\
		    : "ri" ((T__)val));			\
		break;					\
	case 2:						\
		asm(op "w %1,"__percpu_seg"%0"		\
		    : "+m" (var)			\
		    : "ri" ((T__)val));			\
		break;					\
	case 4:						\
		asm(op "l %1,"__percpu_seg"%0"		\
		    : "+m" (var)			\
		    : "ri" ((T__)val));			\
		break;					\
	case 8:						\
		asm(op "q %1,"__percpu_seg"%0"		\
		    : "+m" (var)			\
		    : "ri" ((T__)val));			\
		break;					\
	default: __bad_percpu_size();			\
	}						\
} while (0)

#define percpu_from_op(op, var)				\
({							\
	typeof(var) ret__;				\
	switch (sizeof(var)) {				\
	case 1:						\
		asm(op "b "__percpu_seg"%1,%0"		\
		    : "=r" (ret__)			\
		    : "m" (var));			\
		break;					\
	case 2:						\
		asm(op "w "__percpu_seg"%1,%0"		\
		    : "=r" (ret__)			\
		    : "m" (var));			\
		break;					\
	case 4:						\
		asm(op "l "__percpu_seg"%1,%0"		\
		    : "=r" (ret__)			\
		    : "m" (var));			\
		break;					\
	case 8:						\
		asm(op "q "__percpu_seg"%1,%0"		\
		    : "=r" (ret__)			\
		    : "m" (var));			\
		break;					\
	default: __bad_percpu_size();			\
	}						\
	ret__;						\
})

#define percpu_addr_op(op, var)				\
({							\
	switch (sizeof(var)) {				\
	case 1:						\
		asm(op "b "__percpu_seg"%0"		\
				: : "m"(var));		\
		break;					\
	case 2:						\
		asm(op "w "__percpu_seg"%0"		\
				: : "m"(var));		\
		break;					\
	case 4:						\
		asm(op "l "__percpu_seg"%0"		\
				: : "m"(var));		\
		break;					\
	case 8:						\
		asm(op "q "__percpu_seg"%0"		\
				: : "m"(var));		\
		break;					\
	default: __bad_percpu_size();			\
	}						\
})

#define percpu_cmpxchg_op(var, old, new)				\
({									\
	typeof(var) prev;						\
	switch (sizeof(var)) {						\
	case 1:								\
		asm("cmpxchgb %b1, "__percpu_seg"%2"			\
				     : "=a"(prev)			\
				     : "q"(new), "m"(var), "0"(old)	\
				     : "memory");			\
		break;							\
	case 2:								\
		asm("cmpxchgw %w1, "__percpu_seg"%2"			\
				     : "=a"(prev)			\
				     : "r"(new), "m"(var), "0"(old)	\
				     : "memory");			\
		break;							\
	case 4:								\
		asm("cmpxchgl %k1, "__percpu_seg"%2"			\
				     : "=a"(prev)			\
				     : "r"(new), "m"(var), "0"(old)	\
				     : "memory");			\
		break;							\
	case 8:								\
		asm("cmpxchgq %1, "__percpu_seg"%2"			\
				     : "=a"(prev)			\
				     : "r"(new), "m"(var), "0"(old)	\
				     : "memory");			\
		break;							\
	default:							\
		__bad_percpu_size();					\
	}								\
	return prev;							\
})

#define x86_read_percpu(var) percpu_from_op("mov", per_cpu_var(var))
#define x86_write_percpu(var, val) percpu_to_op("mov", per_cpu_var(var), val)
#define x86_add_percpu(var, val) percpu_to_op("add", per_cpu_var(var), val)
#define x86_sub_percpu(var, val) percpu_to_op("sub", per_cpu_var(var), val)
#define x86_inc_percpu(var) percpu_addr_op("inc", per_cpu_var(var))
#define x86_dec_percpu(var) percpu_addr_op("dec", per_cpu_var(var))
#define x86_or_percpu(var, val) percpu_to_op("or", per_cpu_var(var), val)
#define x86_xchg_percpu(var, val) percpu_to_op("xchg", per_cpu_var(var), val)
#define x86_cmpxchg_percpu(var, old, new) \
				percpu_cmpxchg_op(per_cpu_var(var), old, new)

#endif /* !__ASSEMBLY__ */
#endif /* !CONFIG_X86_64 */
#endif /* _ASM_X86_PERCPU_H_ */
