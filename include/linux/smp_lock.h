#ifndef __LINUX_SMPLOCK_H
#define __LINUX_SMPLOCK_H

#include <linux/compiler.h>
#include <linux/sched.h>

extern void __lockfunc lock_kernel(void)	__acquires(kernel_lock);
extern void __lockfunc unlock_kernel(void)	__releases(kernel_lock);

static inline int kernel_locked(void)
{
	return current->lock_depth >= 0;
}

extern void debug_print_bkl(void);

#endif
