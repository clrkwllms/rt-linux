#ifndef _X86_64_SEMAPHORE_H
#define _X86_64_SEMAPHORE_H

#include <linux/linkage.h>

#ifdef __KERNEL__

#ifndef CONFIG_PREEMPT_RT
# define compat_semaphore semaphore
#endif

/*
 * SMP- and interrupt-safe semaphores..
 *
 * (C) Copyright 1996 Linus Torvalds
 *
 * Modified 1996-12-23 by Dave Grothe <dave@gcom.com> to fix bugs in
 *                     the original code and to make semaphore waits
 *                     interruptible so that processes waiting on
 *                     semaphores can be killed.
 * Modified 1999-02-14 by Andrea Arcangeli, split the sched.c helper
 *		       functions in asm/sempahore-helper.h while fixing a
 *		       potential and subtle race discovered by Ulrich Schmid
 *		       in down_interruptible(). Since I started to play here I
 *		       also implemented the `trylock' semaphore operation.
 *          1999-07-02 Artur Skawina <skawina@geocities.com>
 *                     Optimized "0(ecx)" -> "(ecx)" (the assembler does not
 *                     do this). Changed calling sequences from push/jmp to
 *                     traditional call/ret.
 * Modified 2001-01-01 Andreas Franck <afranck@gmx.de>
 *		       Some hacks to ensure compatibility with recent
 *		       GCC snapshots, to avoid stack corruption when compiling
 *		       with -fomit-frame-pointer. It's not sure if this will
 *		       be fixed in GCC, as our previous implementation was a
 *		       bit dubious.
 *
 * If you would like to see an analysis of this implementation, please
 * ftp to gcom.com and download the file
 * /pub/linux/src/semaphore/semaphore-2.0.24.tar.gz.
 *
 */

#include <asm/system.h>
#include <asm/atomic.h>
#include <asm/rwlock.h>
#include <linux/wait.h>
#include <linux/rwsem.h>
#include <linux/stringify.h>

struct compat_semaphore {
	atomic_t count;
	int sleepers;
	wait_queue_head_t wait;
};

#define __COMPAT_SEMAPHORE_INITIALIZER(name, n)				\
{									\
	.count		= ATOMIC_INIT(n),				\
	.sleepers	= 0,						\
	.wait		= __WAIT_QUEUE_HEAD_INITIALIZER((name).wait)	\
}

#define __COMPAT_MUTEX_INITIALIZER(name) \
	__COMPAT_SEMAPHORE_INITIALIZER(name,1)

#define __COMPAT_DECLARE_SEMAPHORE_GENERIC(name,count) \
	struct compat_semaphore name = __COMPAT_SEMAPHORE_INITIALIZER(name,count)

#define COMPAT_DECLARE_MUTEX(name) __COMPAT_DECLARE_SEMAPHORE_GENERIC(name,1)

#define compat_sema_count(sem) atomic_read(&(sem)->count)

static inline void compat_sema_init (struct compat_semaphore *sem, int val)
{
/*
 *	*sem = (struct compat_semaphore)__SEMAPHORE_INITIALIZER((*sem),val);
 *
 * i'd rather use the more flexible initialization above, but sadly
 * GCC 2.7.2.3 emits a bogus warning. EGCS doesn't. Oh well.
 */
	atomic_set(&sem->count, val);
	sem->sleepers = 0;
	init_waitqueue_head(&sem->wait);
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
asmlinkage int compat_sem_is_locked(struct compat_semaphore *sem);

/*
 * This is ugly, but we want the default case to fall through.
 * "__down_failed" is a special asm handler that calls the C
 * routine that actually waits. See arch/x86_64/kernel/semaphore.c
 */
static inline void compat_down(struct compat_semaphore * sem)
{
	might_sleep();

	__asm__ __volatile__(
		"# atomic down operation\n\t"
		LOCK_PREFIX "decl %0\n\t"     /* --sem->count */
		"jns 1f\n\t"
		"call __compat_down_failed\n"
		"1:"
		:"=m" (sem->count)
		:"D" (sem)
		:"memory");
}

/*
 * Interruptible try to acquire a semaphore.  If we obtained
 * it, return zero.  If we were interrupted, returns -EINTR
 */
static inline int compat_down_interruptible(struct compat_semaphore * sem)
{
	int result;

	might_sleep();

	__asm__ __volatile__(
		"# atomic interruptible down operation\n\t"
		"xorl %0,%0\n\t"
		LOCK_PREFIX "decl %1\n\t"     /* --sem->count */
		"jns 2f\n\t"
		"call __compat_down_failed_interruptible\n"
		"2:\n"
		:"=&a" (result), "=m" (sem->count)
		:"D" (sem)
		:"memory");
	return result;
}

/*
 * Non-blockingly attempt to down() a semaphore.
 * Returns zero if we acquired it
 */
static inline int compat_down_trylock(struct compat_semaphore * sem)
{
	int result;

	__asm__ __volatile__(
		"# atomic interruptible down operation\n\t"
		"xorl %0,%0\n\t"
		LOCK_PREFIX "decl %1\n\t"     /* --sem->count */
		"jns 2f\n\t"
		"call __compat_down_failed_trylock\n\t"
		"2:\n"
		:"=&a" (result), "=m" (sem->count)
		:"D" (sem)
		:"memory","cc");
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
		"# atomic up operation\n\t"
		LOCK_PREFIX "incl %0\n\t"     /* ++sem->count */
		"jg 1f\n\t"
		"call __compat_up_wakeup\n"
		"1:"
		:"=m" (sem->count)
		:"D" (sem)
		:"memory");
}

#include <linux/semaphore.h>

#endif /* __KERNEL__ */
#endif
