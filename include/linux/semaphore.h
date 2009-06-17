/*
 * Copyright (c) 2008 Intel Corporation
 * Author: Matthew Wilcox <willy@linux.intel.com>
 *
 * Distributed under the terms of the GNU GPL, version 2
 *
 * Please see kernel/semaphore.c for documentation of these functions
 */
#ifndef __LINUX_SEMAPHORE_H
#define __LINUX_SEMAPHORE_H

#ifndef CONFIG_PREEMPT_RT
# define compat_semaphore semaphore
#endif

# include <linux/list.h>
# include <linux/spinlock.h>

/* Please don't access any members of this structure directly */
struct compat_semaphore {
	spinlock_t		lock;
	unsigned int		count;
	struct list_head	wait_list;
};

#define __COMPAT_SEMAPHORE_INITIALIZER(name, n)				\
{									\
	.lock		= __SPIN_LOCK_UNLOCKED((name).lock),		\
	.count		= n,						\
	.wait_list	= LIST_HEAD_INIT((name).wait_list),		\
}

#define __COMPAT_DECLARE_SEMAPHORE_GENERIC(name, count) \
	struct compat_semaphore name = __COMPAT_SEMAPHORE_INITIALIZER(name, count)

#define COMPAT_DECLARE_MUTEX(name)	__COMPAT_DECLARE_SEMAPHORE_GENERIC(name, 1)
static inline void compat_sema_init(struct compat_semaphore *sem, int val)
{
	static struct lock_class_key __key;
	*sem = (struct compat_semaphore) __COMPAT_SEMAPHORE_INITIALIZER(*sem, val);
	lockdep_init_map(&sem->lock.dep_map, "semaphore->lock", &__key, 0);
}

#define compat_init_MUTEX(sem)		compat_sema_init(sem, 1)
#define compat_init_MUTEX_LOCKED(sem)	compat_sema_init(sem, 0)

extern void compat_down(struct compat_semaphore *sem);
extern int __must_check compat_down_interruptible(struct compat_semaphore *sem);
extern int __must_check compat_down_killable(struct compat_semaphore *sem);
extern int __must_check compat_down_trylock(struct compat_semaphore *sem);
extern int __must_check compat_down_timeout(struct compat_semaphore *sem, long jiffies);
extern void compat_up(struct compat_semaphore *sem);

#ifdef CONFIG_PREEMPT_RT
# include <linux/rt_lock.h>
#else
#define DECLARE_MUTEX COMPAT_DECLARE_MUTEX

static inline void sema_init(struct compat_semaphore *sem, int val)
{
	compat_sema_init(sem, val);
}
static inline void init_MUTEX(struct compat_semaphore *sem)
{
	compat_init_MUTEX(sem);
}
static inline void init_MUTEX_LOCKED(struct compat_semaphore *sem)
{
	compat_init_MUTEX_LOCKED(sem);
}
static inline void down(struct compat_semaphore *sem)
{
	compat_down(sem);
}
static inline int down_interruptible(struct compat_semaphore *sem)
{
	return compat_down_interruptible(sem);
}
static inline int down_trylock(struct compat_semaphore *sem)
{
	return compat_down_trylock(sem);
}
static inline int down_timeout(struct compat_semaphore *sem, long jiffies)
{
	return compat_down_timeout(sem, jiffies);
}

static inline void up(struct compat_semaphore *sem)
{
	compat_up(sem);
}
#endif /* CONFIG_PREEMPT_RT */

#endif /* __LINUX_SEMAPHORE_H */
