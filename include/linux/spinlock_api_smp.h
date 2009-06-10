#ifndef __LINUX_SPINLOCK_API_SMP_H
#define __LINUX_SPINLOCK_API_SMP_H

#ifndef __LINUX_SPINLOCK_H
# error "please don't include this file directly"
#endif

/*
 * include/linux/spinlock_api_smp.h
 *
 * spinlock API declarations on SMP (and debug)
 * (implemented in kernel/spinlock.c)
 *
 * portions Copyright 2005, Red Hat, Inc., Ingo Molnar
 * Released under the General Public License (GPL).
 */

int in_lock_functions(unsigned long addr);

#define assert_spin_locked(x)	BUG_ON(!spin_is_locked(x))

#define ACQUIRE_SPIN		__acquires(lock)
#define ACQUIRE_RW		__acquires(lock)
#define RELEASE_SPIN		__releases(lock)
#define RELEASE_RW		__releases(lock)

void __lockfunc __spin_lock(raw_spinlock_t *lock)		ACQUIRE_SPIN;
void __lockfunc __spin_lock_nested(raw_spinlock_t *lock, int subclass)
								ACQUIRE_SPIN;
void __lockfunc __read_lock(raw_rwlock_t *lock)			ACQUIRE_RW;
void __lockfunc __write_lock(raw_rwlock_t *lock)		ACQUIRE_RW;
void __lockfunc __spin_lock_bh(raw_spinlock_t *lock)		ACQUIRE_SPIN;
void __lockfunc __read_lock_bh(raw_rwlock_t *lock)		ACQUIRE_RW;
void __lockfunc __write_lock_bh(raw_rwlock_t *lock)		ACQUIRE_RW;
void __lockfunc __spin_lock_irq(raw_spinlock_t *lock)		ACQUIRE_SPIN;
void __lockfunc __read_lock_irq(raw_rwlock_t *lock)		ACQUIRE_RW;
void __lockfunc __write_lock_irq(raw_rwlock_t *lock)		ACQUIRE_RW;
unsigned long __lockfunc __spin_lock_irqsave(raw_spinlock_t *lock)
								ACQUIRE_SPIN;
unsigned long __lockfunc
__spin_lock_irqsave_nested(raw_spinlock_t *lock, int subclass)	ACQUIRE_SPIN;
unsigned long __lockfunc __read_lock_irqsave(raw_rwlock_t *lock)
								ACQUIRE_RW;
unsigned long __lockfunc __write_lock_irqsave(raw_rwlock_t *lock)
								ACQUIRE_RW;
int __lockfunc __spin_trylock(raw_spinlock_t *lock);
int __lockfunc
__spin_trylock_irqsave(raw_spinlock_t *lock, unsigned long *flags);
int __lockfunc __read_trylock(raw_rwlock_t *lock);
int __lockfunc __write_trylock(raw_rwlock_t *lock);
int __lockfunc
__write_trylock_irqsave(raw_rwlock_t *lock, unsigned long *flags);
int __lockfunc __spin_trylock_bh(raw_spinlock_t *lock);
int __lockfunc __spin_trylock_irq(raw_spinlock_t *lock);
void __lockfunc __spin_unlock(raw_spinlock_t *lock)		RELEASE_SPIN;
void __lockfunc __spin_unlock_no_resched(raw_spinlock_t *lock)
								RELEASE_SPIN;
void __lockfunc __read_unlock(raw_rwlock_t *lock)		RELEASE_RW;
void __lockfunc __write_unlock(raw_rwlock_t *lock)		RELEASE_RW;
void __lockfunc __spin_unlock_bh(raw_spinlock_t *lock)		RELEASE_SPIN;
void __lockfunc __read_unlock_bh(raw_rwlock_t *lock)		RELEASE_RW;
void __lockfunc __write_unlock_bh(raw_rwlock_t *lock)		RELEASE_RW;
void __lockfunc __spin_unlock_irq(raw_spinlock_t *lock)		RELEASE_SPIN;
void __lockfunc __read_unlock_irq(raw_rwlock_t *lock)		RELEASE_RW;
void __lockfunc __write_unlock_irq(raw_rwlock_t *lock)		RELEASE_RW;
void __lockfunc
__spin_unlock_irqrestore(raw_spinlock_t *lock, unsigned long flags)
								RELEASE_SPIN;
void __lockfunc
__read_unlock_irqrestore(raw_rwlock_t *lock, unsigned long flags)
								RELEASE_RW;
void
__lockfunc __write_unlock_irqrestore(raw_rwlock_t *lock, unsigned long flags)
								RELEASE_RW;

#endif /* __LINUX_SPINLOCK_API_SMP_H */
