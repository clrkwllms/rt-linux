/*
 * include/asm-mips/rwsem.h: R/W semaphores for MIPS using the stuff
 * in lib/rwsem.c.  Adapted largely from include/asm-ppc/rwsem.h
 * by john.cooper@timesys.com
 */

#ifndef _MIPS_RWSEM_H
#define _MIPS_RWSEM_H

#ifndef _LINUX_RWSEM_H
#error "please don't include asm/rwsem.h directly, use linux/rwsem.h instead"
#endif

#ifdef __KERNEL__
#include <linux/list.h>
#include <linux/spinlock.h>
#include <asm/atomic.h>
#include <asm/system.h>

/*
 * the semaphore definition
 */
struct compat_rw_semaphore {
	/* XXX this should be able to be an atomic_t  -- paulus */
	signed long		count;
#define RWSEM_UNLOCKED_VALUE		0x00000000
#define RWSEM_ACTIVE_BIAS		0x00000001
#define RWSEM_ACTIVE_MASK		0x0000ffff
#define RWSEM_WAITING_BIAS		(-0x00010000)
#define RWSEM_ACTIVE_READ_BIAS		RWSEM_ACTIVE_BIAS
#define RWSEM_ACTIVE_WRITE_BIAS		(RWSEM_WAITING_BIAS + RWSEM_ACTIVE_BIAS)
	raw_spinlock_t		wait_lock;
	struct list_head	wait_list;
#if RWSEM_DEBUG
	int			debug;
#endif
};

/*
 * initialisation
 */
#if RWSEM_DEBUG
#define __RWSEM_DEBUG_INIT      , 0
#else
#define __RWSEM_DEBUG_INIT	/* */
#endif

#define __COMPAT_RWSEM_INITIALIZER(name) \
	{ RWSEM_UNLOCKED_VALUE, SPIN_LOCK_UNLOCKED, \
	  LIST_HEAD_INIT((name).wait_list) \
	  __RWSEM_DEBUG_INIT }

#define COMPAT_DECLARE_RWSEM(name)		\
	struct compat_rw_semaphore name = __COMPAT_RWSEM_INITIALIZER(name)

extern struct compat_rw_semaphore *rwsem_down_read_failed(struct compat_rw_semaphore *sem);
extern struct compat_rw_semaphore *rwsem_down_write_failed(struct compat_rw_semaphore *sem);
extern struct compat_rw_semaphore *rwsem_wake(struct compat_rw_semaphore *sem);
extern struct compat_rw_semaphore *rwsem_downgrade_wake(struct compat_rw_semaphore *sem);

static inline void compat_init_rwsem(struct compat_rw_semaphore *sem)
{
	sem->count = RWSEM_UNLOCKED_VALUE;
	spin_lock_init(&sem->wait_lock);
	INIT_LIST_HEAD(&sem->wait_list);
#if RWSEM_DEBUG
	sem->debug = 0;
#endif
}

/*
 * lock for reading
 */
static inline void __down_read(struct compat_rw_semaphore *sem)
{
	if (atomic_inc_return((atomic_t *)(&sem->count)) > 0)
		smp_wmb();
	else
		rwsem_down_read_failed(sem);
}

static inline int __down_read_trylock(struct compat_rw_semaphore *sem)
{
	int tmp;

	while ((tmp = sem->count) >= 0) {
		if (tmp == cmpxchg(&sem->count, tmp,
				   tmp + RWSEM_ACTIVE_READ_BIAS)) {
			smp_wmb();
			return 1;
		}
	}
	return 0;
}

/*
 * lock for writing
 */
static inline void __down_write(struct compat_rw_semaphore *sem)
{
	int tmp;

	tmp = atomic_add_return(RWSEM_ACTIVE_WRITE_BIAS,
				(atomic_t *)(&sem->count));
	if (tmp == RWSEM_ACTIVE_WRITE_BIAS)
		smp_wmb();
	else
		rwsem_down_write_failed(sem);
}

static inline int __down_write_trylock(struct compat_rw_semaphore *sem)
{
	int tmp;

	tmp = cmpxchg(&sem->count, RWSEM_UNLOCKED_VALUE,
		      RWSEM_ACTIVE_WRITE_BIAS);
	smp_wmb();
	return tmp == RWSEM_UNLOCKED_VALUE;
}

/*
 * unlock after reading
 */
static inline void __up_read(struct compat_rw_semaphore *sem)
{
	int tmp;

	smp_wmb();
	tmp = atomic_dec_return((atomic_t *)(&sem->count));
	if (tmp < -1 && (tmp & RWSEM_ACTIVE_MASK) == 0)
		rwsem_wake(sem);
}

/*
 * unlock after writing
 */
static inline void __up_write(struct compat_rw_semaphore *sem)
{
	smp_wmb();
	if (atomic_sub_return(RWSEM_ACTIVE_WRITE_BIAS,
			      (atomic_t *)(&sem->count)) < 0)
		rwsem_wake(sem);
}

/*
 * implement atomic add functionality
 */
static inline void rwsem_atomic_add(int delta, struct compat_rw_semaphore *sem)
{
	atomic_add(delta, (atomic_t *)(&sem->count));
}

/*
 * downgrade write lock to read lock
 */
static inline void __downgrade_write(struct compat_rw_semaphore *sem)
{
	int tmp;

	smp_wmb();
	tmp = atomic_add_return(-RWSEM_WAITING_BIAS, (atomic_t *)(&sem->count));
	if (tmp < 0)
		rwsem_downgrade_wake(sem);
}

/*
 * implement exchange and add functionality
 */
static inline int rwsem_atomic_update(int delta, struct compat_rw_semaphore *sem)
{
	smp_mb();
	return atomic_add_return(delta, (atomic_t *)(&sem->count));
}

#endif /* __KERNEL__ */
#endif /* _MIPS_RWSEM_H */
