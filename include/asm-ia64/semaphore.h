#ifndef _ASM_IA64_SEMAPHORE_H
#define _ASM_IA64_SEMAPHORE_H

/*
 * Copyright (C) 1998-2000 Hewlett-Packard Co
 * Copyright (C) 1998-2000 David Mosberger-Tang <davidm@hpl.hp.com>
 */

#include <linux/wait.h>
#include <linux/rwsem.h>

#include <asm/atomic.h>

/*
 * On !PREEMPT_RT all semaphores are compat:
 */
#ifndef CONFIG_PREEMPT_RT
# define compat_semaphore semaphore
#endif

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

#define __COMPAT_DECLARE_SEMAPHORE_GENERIC(name,count)					\
	struct compat_semaphore name = __COMPAT_SEMAPHORE_INITIALIZER(name, count)

#define COMPAT_DECLARE_MUTEX(name)		__COMPAT_DECLARE_SEMAPHORE_GENERIC(name, 1)

#define compat_sema_count(sem) atomic_read(&(sem)->count)

asmlinkage int compat_sem_is_locked(struct compat_semaphore *sem);

static inline void
compat_sema_init (struct compat_semaphore *sem, int val)
{
	*sem = (struct compat_semaphore) __COMPAT_SEMAPHORE_INITIALIZER(*sem, val);
}

static inline void
compat_init_MUTEX (struct compat_semaphore *sem)
{
	compat_sema_init(sem, 1);
}

static inline void
compat_init_MUTEX_LOCKED (struct compat_semaphore *sem)
{
	compat_sema_init(sem, 0);
}

extern void __down (struct compat_semaphore * sem);
extern int  __down_interruptible (struct compat_semaphore * sem);
extern int  __down_trylock (struct compat_semaphore * sem);
extern void __up (struct compat_semaphore * sem);

/*
 * Atomically decrement the semaphore's count.  If it goes negative,
 * block the calling thread in the TASK_UNINTERRUPTIBLE state.
 */
static inline void
compat_down (struct compat_semaphore *sem)
{
	might_sleep();
	if (ia64_fetchadd(-1, &sem->count.counter, acq) < 1)
		__down(sem);
}

/*
 * Atomically decrement the semaphore's count.  If it goes negative,
 * block the calling thread in the TASK_INTERRUPTIBLE state.
 */
static inline int
compat_down_interruptible (struct compat_semaphore * sem)
{
	int ret = 0;

	might_sleep();
	if (ia64_fetchadd(-1, &sem->count.counter, acq) < 1)
		ret = __down_interruptible(sem);
	return ret;
}

static inline int
compat_down_trylock (struct compat_semaphore *sem)
{
	int ret = 0;

	if (ia64_fetchadd(-1, &sem->count.counter, acq) < 1)
		ret = __down_trylock(sem);
	return ret;
}

static inline void
compat_up (struct compat_semaphore * sem)
{
	if (ia64_fetchadd(1, &sem->count.counter, rel) <= -1)
		__up(sem);
}

#include <linux/semaphore.h>

#endif /* _ASM_IA64_SEMAPHORE_H */
