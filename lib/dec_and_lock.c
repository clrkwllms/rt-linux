#include <linux/module.h>
#include <linux/spinlock.h>
#include <asm/atomic.h>

/*
 * This is an implementation of the notion of "decrement a
 * reference count, and return locked if it decremented to zero".
 *
 * NOTE NOTE NOTE! This is _not_ equivalent to
 *
 *	if (atomic_dec_and_test(&atomic)) {
 *		spin_lock(&lock);
 *		return 1;
 *	}
 *	return 0;
 *
 * because the spin-lock and the decrement must be
 * "atomic".
 */
int __atomic_dec_and_spin_lock(raw_spinlock_t *lock, atomic_t *atomic)
{
#ifdef CONFIG_SMP
	/* Subtract 1 from counter unless that drops it to 0 (ie. it was 1) */
	if (atomic_add_unless(atomic, -1, 1))
		return 0;
#endif
	/* Otherwise do it the slow way */
	spin_lock(lock);
	if (atomic_dec_and_test(atomic))
		return 1;
	spin_unlock(lock);
	return 0;
}

EXPORT_SYMBOL(__atomic_dec_and_spin_lock);
