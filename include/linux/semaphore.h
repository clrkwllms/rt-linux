#ifndef _LINUX_SEMAPHORE_H
#define _LINUX_SEMAPHORE_H

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
static inline void up(struct compat_semaphore *sem)
{
	compat_up(sem);
}
static inline int sem_is_locked(struct compat_semaphore *sem)
{
	return compat_sem_is_locked(sem);
}
static inline int sema_count(struct compat_semaphore *sem)
{
	return compat_sema_count(sem);
}

#endif /* CONFIG_PREEMPT_RT */

#endif /* _LINUX_SEMAPHORE_H */
