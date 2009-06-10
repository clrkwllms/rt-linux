/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996  Linus Torvalds
 * Copyright (C) 1998, 99, 2000, 01, 04  Ralf Baechle
 * Copyright (C) 1999, 2000, 01  Silicon Graphics, Inc.
 * Copyright (C) 2000, 01 MIPS Technologies, Inc.
 *
 * In all honesty, little of the old MIPS code left - the PPC64 variant was
 * just looking nice and portable so I ripped it.  Credits to whoever wrote
 * it.
 */
#ifndef __ASM_SEMAPHORE_H
#define __ASM_SEMAPHORE_H

/*
 * Remove spinlock-based RW semaphores; RW semaphore definitions are
 * now in rwsem.h and we use the generic lib/rwsem.c implementation.
 * Rework semaphores to use atomic_dec_if_positive.
 * -- Paul Mackerras (paulus@samba.org)
 */

#ifdef __KERNEL__

#include <linux/wait.h>
#include <linux/rwsem.h>

/*
 * On !PREEMPT_RT all semaphores are compat:
 */
#ifndef CONFIG_PREEMPT_RT
# define compat_semaphore semaphore
#endif

#include <asm/atomic.h>
#include <asm/system.h>

struct compat_semaphore {
	/*
	 * Note that any negative value of count is equivalent to 0,
	 * but additionally indicates that some process(es) might be
	 * sleeping on `wait'.
	 */
	atomic_t count;
	wait_queue_head_t wait;
};

#define __COMPAT_SEMAPHORE_INITIALIZER(name, n)				\
{									\
	.count		= ATOMIC_INIT(n),				\
	.wait		= __WAIT_QUEUE_HEAD_INITIALIZER((name).wait)	\
}

#define __COMPAT_MUTEX_INITIALIZER(name) \
	__COMPAT_SEMAPHORE_INITIALIZER(name, 1)

#define __COMPAT_DECLARE_SEMAPHORE_GENERIC(name, count) \
	struct compat_semaphore name = __COMPAT_SEMAPHORE_INITIALIZER(name,count)

#define COMPAT_DECLARE_MUTEX(name)		__COMPAT_DECLARE_SEMAPHORE_GENERIC(name, 1)

static inline void compat_sema_init (struct compat_semaphore *sem, int val)
{
	atomic_set(&sem->count, val);
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

extern void __compat_down(struct compat_semaphore * sem);
extern int  __compat_down_interruptible(struct compat_semaphore * sem);
extern void __compat_up(struct compat_semaphore * sem);

static inline void compat_down(struct compat_semaphore * sem)
{
	might_sleep();

	/*
	 * Try to get the semaphore, take the slow path if we fail.
	 */
	if (unlikely(atomic_dec_return(&sem->count) < 0))
		__compat_down(sem);
}

static inline int compat_down_interruptible(struct compat_semaphore * sem)
{
	int ret = 0;

	might_sleep();

	if (unlikely(atomic_dec_return(&sem->count) < 0))
		ret = __compat_down_interruptible(sem);
	return ret;
}

static inline int compat_down_trylock(struct compat_semaphore * sem)
{
	return atomic_dec_if_positive(&sem->count) < 0;
}

static inline void compat_up(struct compat_semaphore * sem)
{
	if (unlikely(atomic_inc_return(&sem->count) <= 0))
		__compat_up(sem);
}

extern int compat_sem_is_locked(struct compat_semaphore *sem);

#define compat_sema_count(sem) atomic_read(&(sem)->count)

#include <linux/semaphore.h>

#endif /* __KERNEL__ */

#endif /* __ASM_SEMAPHORE_H */
