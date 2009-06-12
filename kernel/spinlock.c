/*
 * Copyright (2004) Linus Torvalds
 *
 * Author: Zwane Mwaikambo <zwane@fsmlabs.com>
 *
 * Copyright (2004, 2005) Ingo Molnar
 *
 * This file contains the spinlock/rwlock implementations for the
 * SMP and the DEBUG_SPINLOCK cases. (UP-nondebug inlines them)
 *
 * Note that some architectures have special knowledge about the
 * stack frames of these functions in their profile_pc. If you
 * change anything significant here that could change the stack
 * frame contact the architecture maintainers.
 */

#include <linux/linkage.h>
#include <linux/preempt.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/debug_locks.h>
#include <linux/module.h>

int __lockfunc __spin_trylock(raw_spinlock_t *lock)
{
	preempt_disable();
	if (_raw_spin_trylock(lock)) {
		spin_acquire(&lock->dep_map, 0, 1, _RET_IP_);
		return 1;
	}
	
	preempt_enable();
	return 0;
}
EXPORT_SYMBOL(__spin_trylock);

int __lockfunc __spin_trylock_irq(raw_spinlock_t *lock)
{
	local_irq_disable();
	preempt_disable();

	if (_raw_spin_trylock(lock)) {
		spin_acquire(&lock->dep_map, 0, 1, _RET_IP_);
		return 1;
	}

	__preempt_enable_no_resched();
	local_irq_enable();
	preempt_check_resched();

	return 0;
}
EXPORT_SYMBOL(__spin_trylock_irq);

int __lockfunc __spin_trylock_irqsave(raw_spinlock_t *lock,
					 unsigned long *flags)
{
	local_irq_save(*flags);
	preempt_disable();

	if (_raw_spin_trylock(lock)) {
		spin_acquire(&lock->dep_map, 0, 1, _RET_IP_);
		return 1;
	}

	__preempt_enable_no_resched();
	local_irq_restore(*flags);
	preempt_check_resched();

	return 0;
}
EXPORT_SYMBOL(__spin_trylock_irqsave);

int __lockfunc __read_trylock(raw_rwlock_t *lock)
{
	preempt_disable();
	if (_raw_read_trylock(lock)) {
		rwlock_acquire_read(&lock->dep_map, 0, 1, _RET_IP_);
		return 1;
	}

	preempt_enable();
	return 0;
}
EXPORT_SYMBOL(__read_trylock);

int __lockfunc __write_trylock(raw_rwlock_t *lock)
{
	preempt_disable();
	if (_raw_write_trylock(lock)) {
		rwlock_acquire(&lock->dep_map, 0, 1, _RET_IP_);
		return 1;
	}

	preempt_enable();
	return 0;
}
EXPORT_SYMBOL(__write_trylock);

int __lockfunc __write_trylock_irqsave(raw_rwlock_t *lock, unsigned long *flags)
{
	int ret;

	local_irq_save(*flags);
	ret = __write_trylock(lock);
	if (ret)
		return ret;

	local_irq_restore(*flags);
	return 0;
}
EXPORT_SYMBOL(__write_trylock_irqsave);

/*
 * If lockdep is enabled then we use the non-preemption spin-ops
 * even on CONFIG_PREEMPT, because lockdep assumes that interrupts are
 * not re-enabled during lock-acquire (which the preempt-spin-ops do):
 */
#if !defined(CONFIG_GENERIC_LOCKBREAK) || defined(CONFIG_DEBUG_LOCK_ALLOC)

void __lockfunc __read_lock(raw_rwlock_t *lock)
{
	preempt_disable();
	rwlock_acquire_read(&lock->dep_map, 0, 0, _RET_IP_);
	LOCK_CONTENDED(lock, _raw_read_trylock, _raw_read_lock);
}
EXPORT_SYMBOL(__read_lock);

unsigned long __lockfunc __spin_lock_irqsave(raw_spinlock_t *lock)
{
	unsigned long flags;

	local_irq_save(flags);
	preempt_disable();
	spin_acquire(&lock->dep_map, 0, 0, _RET_IP_);
	/*
	 * On lockdep we dont want the hand-coded irq-enable of
	 * _raw_spin_lock_flags() code, because lockdep assumes
	 * that interrupts are not re-enabled during lock-acquire:
	 */
#ifdef CONFIG_LOCKDEP
	LOCK_CONTENDED(lock, _raw_spin_trylock, _raw_spin_lock);
#else
	_raw_spin_lock_flags(lock, &flags);
#endif
	return flags;
}
EXPORT_SYMBOL(__spin_lock_irqsave);

void __lockfunc __spin_lock_irq(raw_spinlock_t *lock)
{
	local_irq_disable();
	preempt_disable();
	spin_acquire(&lock->dep_map, 0, 0, _RET_IP_);
	LOCK_CONTENDED(lock, _raw_spin_trylock, _raw_spin_lock);
}
EXPORT_SYMBOL(__spin_lock_irq);

void __lockfunc __spin_lock_bh(raw_spinlock_t *lock)
{
	local_bh_disable();
	preempt_disable();
	spin_acquire(&lock->dep_map, 0, 0, _RET_IP_);
	LOCK_CONTENDED(lock, _raw_spin_trylock, _raw_spin_lock);
}
EXPORT_SYMBOL(__spin_lock_bh);

unsigned long __lockfunc __read_lock_irqsave(raw_rwlock_t *lock)
{
	unsigned long flags;

	local_irq_save(flags);
	preempt_disable();
	rwlock_acquire_read(&lock->dep_map, 0, 0, _RET_IP_);
	LOCK_CONTENDED(lock, _raw_read_trylock, _raw_read_lock);
	return flags;
}
EXPORT_SYMBOL(__read_lock_irqsave);

void __lockfunc __read_lock_irq(raw_rwlock_t *lock)
{
	local_irq_disable();
	preempt_disable();
	rwlock_acquire_read(&lock->dep_map, 0, 0, _RET_IP_);
	LOCK_CONTENDED(lock, _raw_read_trylock, _raw_read_lock);
}
EXPORT_SYMBOL(__read_lock_irq);

void __lockfunc __read_lock_bh(raw_rwlock_t *lock)
{
	local_bh_disable();
	preempt_disable();
	rwlock_acquire_read(&lock->dep_map, 0, 0, _RET_IP_);
	LOCK_CONTENDED(lock, _raw_read_trylock, _raw_read_lock);
}
EXPORT_SYMBOL(__read_lock_bh);

unsigned long __lockfunc __write_lock_irqsave(raw_rwlock_t *lock)
{
	unsigned long flags;

	local_irq_save(flags);
	preempt_disable();
	rwlock_acquire(&lock->dep_map, 0, 0, _RET_IP_);
	LOCK_CONTENDED(lock, _raw_write_trylock, _raw_write_lock);
	return flags;
}
EXPORT_SYMBOL(__write_lock_irqsave);

void __lockfunc __write_lock_irq(raw_rwlock_t *lock)
{
	local_irq_disable();
	preempt_disable();
	rwlock_acquire(&lock->dep_map, 0, 0, _RET_IP_);
	LOCK_CONTENDED(lock, _raw_write_trylock, _raw_write_lock);
}
EXPORT_SYMBOL(__write_lock_irq);

void __lockfunc __write_lock_bh(raw_rwlock_t *lock)
{
	local_bh_disable();
	preempt_disable();
	rwlock_acquire(&lock->dep_map, 0, 0, _RET_IP_);
	LOCK_CONTENDED(lock, _raw_write_trylock, _raw_write_lock);
}
EXPORT_SYMBOL(__write_lock_bh);

void __lockfunc __spin_lock(raw_spinlock_t *lock)
{
	preempt_disable();
	spin_acquire(&lock->dep_map, 0, 0, _RET_IP_);
	LOCK_CONTENDED(lock, _raw_spin_trylock, _raw_spin_lock);
}

EXPORT_SYMBOL(__spin_lock);

void __lockfunc __write_lock(raw_rwlock_t *lock)
{
	preempt_disable();
	rwlock_acquire(&lock->dep_map, 0, 0, _RET_IP_);
	LOCK_CONTENDED(lock, _raw_write_trylock, _raw_write_lock);
}

EXPORT_SYMBOL(__write_lock);

#else /* CONFIG_PREEMPT: */

/*
 * This could be a long-held lock. We both prepare to spin for a long
 * time (making _this_ CPU preemptable if possible), and we also signal
 * towards that other CPU that it should break the lock ASAP.
 *
 * (We do this in a function because inlining it would be excessive.)
 */

#define BUILD_LOCK_OPS(op, locktype)					\
void __lockfunc __##op##_lock(locktype##_t *lock)			\
{									\
	for (;;) {							\
		preempt_disable();					\
		if (likely(_raw_##op##_trylock(lock)))			\
			break;						\
		preempt_enable();					\
									\
		if (!(lock)->break_lock)				\
			(lock)->break_lock = 1;				\
		while (!__raw_##op##_can_lock(&(lock)->raw_lock) &&	\
					(lock)->break_lock)		\
			__raw_##op##_relax(&lock->raw_lock);		\
	}								\
	(lock)->break_lock = 0;						\
}									\
									\
EXPORT_SYMBOL(__##op##_lock);						\
									\
unsigned long __lockfunc __##op##_lock_irqsave(locktype##_t *lock)	\
{									\
	unsigned long flags;						\
									\
	for (;;) {							\
		preempt_disable();					\
		local_irq_save(flags);					\
		if (likely(_raw_##op##_trylock(lock)))			\
			break;						\
		local_irq_restore(flags);				\
		preempt_enable();					\
									\
		if (!(lock)->break_lock)				\
			(lock)->break_lock = 1;				\
		while (!__raw_##op##_can_lock(&(lock)->raw_lock) &&	\
						 (lock)->break_lock)	\
			__raw_##op##_relax(&lock->raw_lock);		\
	}								\
	(lock)->break_lock = 0;						\
	return flags;							\
}									\
									\
EXPORT_SYMBOL(__##op##_lock_irqsave);					\
									\
void __lockfunc __##op##_lock_irq(locktype##_t *lock)			\
{									\
	__##op##_lock_irqsave(lock);					\
}									\
									\
EXPORT_SYMBOL(__##op##_lock_irq);					\
									\
void __lockfunc __##op##_lock_bh(locktype##_t *lock)			\
{									\
	unsigned long flags;						\
									\
	/*							*/	\
	/* Careful: we must exclude softirqs too, hence the	*/	\
	/* irq-disabling. We use the generic preemption-aware	*/	\
	/* function:						*/	\
	/**/								\
	flags = __##op##_lock_irqsave(lock);				\
	local_bh_disable();						\
	local_irq_restore(flags);					\
}									\
									\
EXPORT_SYMBOL(__##op##_lock_bh)

/*
 * Build preemption-friendly versions of the following
 * lock-spinning functions:
 *
 *         __[spin|read|write]_lock()
 *         __[spin|read|write]_lock_irq()
 *         __[spin|read|write]_lock_irqsave()
 *         __[spin|read|write]_lock_bh()
 */
BUILD_LOCK_OPS(spin, raw_spinlock);
BUILD_LOCK_OPS(read, raw_rwlock);
BUILD_LOCK_OPS(write, raw_rwlock);

#endif /* CONFIG_PREEMPT */

#ifdef CONFIG_DEBUG_LOCK_ALLOC

void __lockfunc __spin_lock_nested(raw_spinlock_t *lock, int subclass)
{
	preempt_disable();
	spin_acquire(&lock->dep_map, subclass, 0, _RET_IP_);
	LOCK_CONTENDED(lock, _raw_spin_trylock, _raw_spin_lock);
}
EXPORT_SYMBOL(__spin_lock_nested);

void __lockfunc __spin_lock_nest_lock(raw_spinlock_t *lock,
				     struct lockdep_map *nest_lock)
{
	preempt_disable();
	spin_acquire_nest(&lock->dep_map, 0, 0, nest_lock, _RET_IP_);
	LOCK_CONTENDED(lock, _raw_spin_trylock, _raw_spin_lock);
}
EXPORT_SYMBOL(__spin_lock_nest_lock);

unsigned long __lockfunc __spin_lock_irqsave_nested(raw_spinlock_t *lock, int subclass)
{
	unsigned long flags;

	local_irq_save(flags);
	preempt_disable();
	spin_acquire(&lock->dep_map, subclass, 0, _RET_IP_);
	/*
	 * On lockdep we dont want the hand-coded irq-enable of
	 * _raw_spin_lock_flags() code, because lockdep assumes
	 * that interrupts are not re-enabled during lock-acquire:
	 */
#ifdef CONFIG_LOCKDEP
	LOCK_CONTENDED(lock, _raw_spin_trylock, _raw_spin_lock);
#else
	_raw_spin_lock_flags(lock, &flags);
#endif
	return flags;
}
EXPORT_SYMBOL(__spin_lock_irqsave_nested);

#endif

void __lockfunc __spin_unlock(raw_spinlock_t *lock)
{
	spin_release(&lock->dep_map, 1, _RET_IP_);
	_raw_spin_unlock(lock);
	preempt_enable();
}
EXPORT_SYMBOL(__spin_unlock);

void __lockfunc __spin_unlock_no_resched(raw_spinlock_t *lock)
{
	spin_release(&lock->dep_map, 1, _RET_IP_);
	_raw_spin_unlock(lock);
	__preempt_enable_no_resched();
}
/* not exported */

void __lockfunc __write_unlock(raw_rwlock_t *lock)
{
	rwlock_release(&lock->dep_map, 1, _RET_IP_);
	_raw_write_unlock(lock);
	preempt_enable();
}
EXPORT_SYMBOL(__write_unlock);

void __lockfunc __read_unlock(raw_rwlock_t *lock)
{
	rwlock_release(&lock->dep_map, 1, _RET_IP_);
	_raw_read_unlock(lock);
	preempt_enable();
}
EXPORT_SYMBOL(__read_unlock);

void __lockfunc __spin_unlock_irqrestore(raw_spinlock_t *lock, unsigned long flags)
{
	spin_release(&lock->dep_map, 1, _RET_IP_);
	_raw_spin_unlock(lock);
	__preempt_enable_no_resched();
	local_irq_restore(flags);
	preempt_check_resched();
}
EXPORT_SYMBOL(__spin_unlock_irqrestore);

void __lockfunc __spin_unlock_irq(raw_spinlock_t *lock)
{
	spin_release(&lock->dep_map, 1, _RET_IP_);
	_raw_spin_unlock(lock);
	__preempt_enable_no_resched();
	local_irq_enable();
	preempt_check_resched();
}
EXPORT_SYMBOL(__spin_unlock_irq);

void __lockfunc __spin_unlock_bh(raw_spinlock_t *lock)
{
	spin_release(&lock->dep_map, 1, _RET_IP_);
	_raw_spin_unlock(lock);
	__preempt_enable_no_resched();
	local_bh_enable_ip((unsigned long)__builtin_return_address(0));
}
EXPORT_SYMBOL(__spin_unlock_bh);

void __lockfunc __read_unlock_irqrestore(raw_rwlock_t *lock, unsigned long flags)
{
	rwlock_release(&lock->dep_map, 1, _RET_IP_);
	_raw_read_unlock(lock);
	__preempt_enable_no_resched();
	local_irq_restore(flags);
	preempt_check_resched();
}
EXPORT_SYMBOL(__read_unlock_irqrestore);

void __lockfunc __read_unlock_irq(raw_rwlock_t *lock)
{
	rwlock_release(&lock->dep_map, 1, _RET_IP_);
	_raw_read_unlock(lock);
	__preempt_enable_no_resched();
	local_irq_enable();
	preempt_check_resched();
}
EXPORT_SYMBOL(__read_unlock_irq);

void __lockfunc __read_unlock_bh(raw_rwlock_t *lock)
{
	rwlock_release(&lock->dep_map, 1, _RET_IP_);
	_raw_read_unlock(lock);
	__preempt_enable_no_resched();
	local_bh_enable_ip((unsigned long)__builtin_return_address(0));
}
EXPORT_SYMBOL(__read_unlock_bh);

void __lockfunc __write_unlock_irqrestore(raw_rwlock_t *lock, unsigned long flags)
{
	rwlock_release(&lock->dep_map, 1, _RET_IP_);
	_raw_write_unlock(lock);
	__preempt_enable_no_resched();
	local_irq_restore(flags);
	preempt_check_resched();
}
EXPORT_SYMBOL(__write_unlock_irqrestore);

void __lockfunc __write_unlock_irq(raw_rwlock_t *lock)
{
	rwlock_release(&lock->dep_map, 1, _RET_IP_);
	_raw_write_unlock(lock);
	__preempt_enable_no_resched();
	local_irq_enable();
	preempt_check_resched();
}
EXPORT_SYMBOL(__write_unlock_irq);

void __lockfunc __write_unlock_bh(raw_rwlock_t *lock)
{
	rwlock_release(&lock->dep_map, 1, _RET_IP_);
	_raw_write_unlock(lock);
	__preempt_enable_no_resched();
	local_bh_enable_ip((unsigned long)__builtin_return_address(0));
}
EXPORT_SYMBOL(__write_unlock_bh);

int __lockfunc __spin_trylock_bh(raw_spinlock_t *lock)
{
	local_bh_disable();
	preempt_disable();
	if (_raw_spin_trylock(lock)) {
		spin_acquire(&lock->dep_map, 0, 1, _RET_IP_);
		return 1;
	}

	__preempt_enable_no_resched();
	local_bh_enable_ip((unsigned long)__builtin_return_address(0));
	return 0;
}
EXPORT_SYMBOL(__spin_trylock_bh);

notrace int in_lock_functions(unsigned long addr)
{
	/* Linker adds these: start and end of __lockfunc functions */
	extern char __lock_text_start[], __lock_text_end[];

	return addr >= (unsigned long)__lock_text_start
		&& addr < (unsigned long)__lock_text_end;
}
EXPORT_SYMBOL(in_lock_functions);

void notrace __debug_atomic_dec_and_test(atomic_t *v)
{
	static int warn_once = 1;

	if (!atomic_read(v) && warn_once) {
		warn_once = 0;
		printk("BUG: atomic counter underflow!\n");
		WARN_ON(1);
	}
}
