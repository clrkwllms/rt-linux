#ifndef _M68K_SEMAPHORE_H
#define _M68K_SEMAPHORE_H

#define RW_LOCK_BIAS		 0x01000000

#ifndef __ASSEMBLY__

#include <linux/linkage.h>
#include <linux/wait.h>
#include <linux/spinlock.h>
#include <linux/rwsem.h>

#include <asm/system.h>
#include <asm/atomic.h>

/*
 * Interrupt-safe semaphores..
 *
 * (C) Copyright 1996 Linus Torvalds
 *
 * m68k version by Andreas Schwab
 */

#ifndef CONFIG_PREEMPT_RT
# define compat_semaphore semaphore
#endif

struct compat_semaphore {
	atomic_t count;
	atomic_t waking;
	wait_queue_head_t wait;
};

#define __COMPAT_SEMAPHORE_INITIALIZER(name, n)				\
{									\
	.count		= ATOMIC_INIT(n),				\
	.waking		= ATOMIC_INIT(0),				\
	.wait		= __WAIT_QUEUE_HEAD_INITIALIZER((name).wait)	\
}

#define __COMPAT_DECLARE_SEMAPHORE_GENERIC(name,count) \
	struct compat_semaphore name = __COMPAT_SEMAPHORE_INITIALIZER(name,count)

#define COMPAT_DECLARE_MUTEX(name) __COMPAT_DECLARE_SEMAPHORE_GENERIC(name,1)

static inline void compat_sema_init (struct compat_semaphore *sem, int val)
{
	*sem = (struct compat_semaphore)__COMPAT_SEMAPHORE_INITIALIZER(*sem, val);
}

static inline void compat_init_MUTEX (struct compat_semaphore *sem)
{
	compat_sema_init(sem, 1);
}

static inline void compat_init_MUTEX_LOCKED (struct compat_semaphore *sem)
{
	compat_sema_init(sem, 0);
}

asmlinkage void __compat_down_failed(void /* special register calling convention */);
asmlinkage int  __compat_down_failed_interruptible(void  /* params in registers */);
asmlinkage int  __compat_down_failed_trylock(void  /* params in registers */);
asmlinkage void __compat_up_wakeup(void /* special register calling convention */);

asmlinkage void __compat_down(struct compat_semaphore * sem);
asmlinkage int  __compat_down_interruptible(struct compat_semaphore * sem);
asmlinkage int  __compat_down_trylock(struct compat_semaphore * sem);
asmlinkage void __compat_up(struct compat_semaphore * sem);

extern int compat_sem_is_locked(struct compat_semaphore *sem);
#define compat_sema_count(sem) atomic_read(&(sem)->count)

extern spinlock_t semaphore_wake_lock;

/*
 * This is ugly, but we want the default case to fall through.
 * "down_failed" is a special asm handler that calls the C
 * routine that actually waits. See arch/m68k/lib/semaphore.S
 */
static inline void compat_down(struct compat_semaphore * sem)
{
	might_sleep();
	__asm__ __volatile__(
		"| atomic down operation\n\t"
		"movel	%0, %%a1\n\t"
		"lea	%%pc@(1f), %%a0\n\t"
		"subql	#1, %%a1@\n\t"
		"jmi __down_failed\n"
		"1:"
		: /* no outputs */
		: "g" (sem)
		: "cc", "%a0", "%a1", "memory");
}

static inline int compat_down_interruptible(struct compat_semaphore * sem)
{
	int ret;

	might_sleep();
	__asm__ __volatile__(
		"| atomic down operation\n\t"
		"movel	%1, %%a1\n\t"
		"lea	%%pc@(1f), %%a0\n\t"
		"subql	#1, %%a1@\n\t"
		"jmi __down_failed_interruptible\n\t"
		"clrl	%%d0\n"
		"1: movel	%%d0, %0\n"
		: "=d" (ret)
		: "g" (sem)
		: "cc", "%d0", "%a0", "%a1", "memory");
	return(ret);
}

static inline int compat_down_trylock(struct compat_semaphore * sem)
{
	register struct compat_semaphore *sem1 __asm__ ("%a1") = sem;
	register int result __asm__ ("%d0");

	__asm__ __volatile__(
		"| atomic down trylock operation\n\t"
		"subql #1,%1@\n\t"
		"jmi 2f\n\t"
		"clrl %0\n"
		"1:\n"
		".section .text.lock,\"ax\"\n"
		".even\n"
		"2:\tpea 1b\n\t"
		"jbra __down_failed_trylock\n"
		".previous"
		: "=d" (result)
		: "a" (sem1)
		: "memory");
	return result;
}

/*
 * Note! This is subtle. We jump to wake people up only if
 * the semaphore was negative (== somebody was waiting on it).
 * The default case (no contention) will result in NO
 * jumps for both down() and up().
 */
static inline void compat_up(struct compat_semaphore * sem)
{
	__asm__ __volatile__(
		"| atomic up operation\n\t"
		"movel	%0, %%a1\n\t"
		"lea	%%pc@(1f), %%a0\n\t"
		"addql	#1, %%a1@\n\t"
		"jle __up_wakeup\n"
		"1:"
		: /* no outputs */
		: "g" (sem)
		: "cc", "%a0", "%a1", "memory");
}

#include <linux/semaphore.h>

#endif /* __ASSEMBLY__ */

#endif
