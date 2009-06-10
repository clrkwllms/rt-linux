/*
 * linux/include/asm-arm/semaphore.h
 */
#ifndef __ASM_ARM_SEMAPHORE_H
#define __ASM_ARM_SEMAPHORE_H

#include <linux/linkage.h>

#ifdef CONFIG_PREEMPT_RT
# include <linux/rt_lock.h>
#endif

#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/rwsem.h>

/*
 * On !PREEMPT_RT all semaphores are compat:
 */
#ifndef CONFIG_PREEMPT_RT
# define semaphore compat_semaphore
#endif

#include <asm/atomic.h>
#include <asm/locks.h>

struct compat_semaphore {
	atomic_t count;
	int sleepers;
	wait_queue_head_t wait;
};

#define __COMPAT_SEMAPHORE_INITIALIZER(name, cnt)				\
{								\
	.count	= ATOMIC_INIT(cnt),				\
	.wait	= __WAIT_QUEUE_HEAD_INITIALIZER((name).wait),	\
}

#define __COMPAT_MUTEX_INITIALIZER(name) \
	__COMPAT_SEMAPHORE_INITIALIZER(name,1)

#define __COMPAT_DECLARE_SEMAPHORE_GENERIC(name,count) \
	struct compat_semaphore name = __COMPAT_SEMAPHORE_INITIALIZER(name,count)

#define COMPAT_DECLARE_MUTEX(name) __COMPAT_DECLARE_SEMAPHORE_GENERIC(name,1)

static inline void compat_sema_init(struct compat_semaphore *sem, int val)
{
	atomic_set(&sem->count, val);
	sem->sleepers = 0;
	init_waitqueue_head(&sem->wait);
}

static inline void compat_init_MUTEX(struct compat_semaphore *sem)
{
	compat_sema_init(sem, 1);
}

static inline void compat_init_MUTEX_LOCKED(struct compat_semaphore *sem)
{
	compat_sema_init(sem, 0);
}

static inline int compat_sema_count(struct compat_semaphore *sem)
{
	return atomic_read(&sem->count);
}

/*
 * special register calling convention
 */
asmlinkage void __down_failed(void);
asmlinkage int  __down_interruptible_failed(void);
asmlinkage int  __down_trylock_failed(void);
asmlinkage void __up_wakeup(void);

extern void __compat_up(struct compat_semaphore *sem);
extern int __compat_down_interruptible(struct compat_semaphore * sem);
extern int __compat_down_trylock(struct compat_semaphore * sem);
extern void __compat_down(struct compat_semaphore * sem);

extern int compat_sem_is_locked(struct compat_semaphore *sem);

/*
 * This is ugly, but we want the default case to fall through.
 * "__down" is the actual routine that waits...
 */
static inline void compat_down(struct compat_semaphore * sem)
{
	might_sleep();
	__down_op(sem, __down_failed);
}

/*
 * This is ugly, but we want the default case to fall through.
 * "__down_interruptible" is the actual routine that waits...
 */
static inline int compat_down_interruptible (struct compat_semaphore * sem)
{
	might_sleep();
	return __down_op_ret(sem, __down_interruptible_failed);
}

static inline int compat_down_trylock(struct compat_semaphore *sem)
{
	return __down_op_ret(sem, __down_trylock_failed);
}

/*
 * Note! This is subtle. We jump to wake people up only if
 * the semaphore was negative (== somebody was waiting on it).
 * The default case (no contention) will result in NO
 * jumps for both down() and up().
 */
static inline void compat_up(struct compat_semaphore * sem)
{
	__up_op(sem, __up_wakeup);
}

#include <linux/semaphore.h>
#endif
