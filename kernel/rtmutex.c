/*
 * RT-Mutexes: simple blocking mutual exclusion locks with PI support
 *
 * started by Ingo Molnar and Thomas Gleixner.
 *
 *  Copyright (C) 2004-2006 Red Hat, Inc., Ingo Molnar <mingo@redhat.com>
 *  Copyright (C) 2005-2006 Timesys Corp., Thomas Gleixner <tglx@timesys.com>
 *  Copyright (C) 2005 Kihon Technologies Inc., Steven Rostedt
 *  Copyright (C) 2006 Esben Nielsen
 *
 * Adaptive Spinlocks:
 *  Copyright (C) 2008 Novell, Inc., Gregory Haskins, Sven Dietrich,
 *                                   and Peter Morreale,
 * Adaptive Spinlocks simplification:
 *  Copyright (C) 2008 Red Hat, Inc., Steven Rostedt <srostedt@redhat.com>
 *
 *  See Documentation/rt-mutex-design.txt for details.
 */
#include <linux/spinlock.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/hardirq.h>

#include "rtmutex_common.h"

/*
 * lock->owner state tracking:
 *
 * lock->owner holds the task_struct pointer of the owner. Bit 0 and 1
 * are used to keep track of the "owner is pending" and "lock has
 * waiters" state.
 *
 * owner	bit1	bit0
 * NULL		0	0	lock is free (fast acquire possible)
 * NULL		0	1	invalid state
 * NULL		1	0	Transitional State*
 * NULL		1	1	invalid state
 * taskpointer	0	0	lock is held (fast release possible)
 * taskpointer	0	1	task is pending owner
 * taskpointer	1	0	lock is held and has waiters
 * taskpointer	1	1	task is pending owner and lock has more waiters
 *
 * Pending ownership is assigned to the top (highest priority)
 * waiter of the lock, when the lock is released. The thread is woken
 * up and can now take the lock. Until the lock is taken (bit 0
 * cleared) a competing higher priority thread can steal the lock
 * which puts the woken up thread back on the waiters list.
 *
 * The fast atomic compare exchange based acquire and release is only
 * possible when bit 0 and 1 of lock->owner are 0.
 *
 * (*) There's a small time where the owner can be NULL and the
 * "lock has waiters" bit is set.  This can happen when grabbing the lock.
 * To prevent a cmpxchg of the owner releasing the lock, we need to set this
 * bit before looking at the lock, hence the reason this is a transitional
 * state.
 */

static void
rt_mutex_set_owner(struct rt_mutex *lock, struct task_struct *owner,
		   unsigned long mask)
{
	unsigned long val = (unsigned long)owner | mask;

	if (rt_mutex_has_waiters(lock))
		val |= RT_MUTEX_HAS_WAITERS;

	lock->owner = (struct task_struct *)val;
}

static inline void clear_rt_mutex_waiters(struct rt_mutex *lock)
{
	lock->owner = (struct task_struct *)
			((unsigned long)lock->owner & ~RT_MUTEX_HAS_WAITERS);
}

static void fixup_rt_mutex_waiters(struct rt_mutex *lock)
{
	if (!rt_mutex_has_waiters(lock))
		clear_rt_mutex_waiters(lock);
}

/*
 * We can speed up the acquire/release, if the architecture
 * supports cmpxchg and if there's no debugging state to be set up
 */
#if defined(__HAVE_ARCH_CMPXCHG) && !defined(CONFIG_DEBUG_RT_MUTEXES)
# define rt_mutex_cmpxchg(l,c,n)	(cmpxchg(&l->owner, c, n) == c)
# define rt_rwlock_cmpxchg(rwm,c,n)	(cmpxchg(&(rwm)->owner, c, n) == c)
static inline void mark_rt_mutex_waiters(struct rt_mutex *lock)
{
	unsigned long owner, *p = (unsigned long *) &lock->owner;

	do {
		owner = *p;
	} while (cmpxchg(p, owner, owner | RT_MUTEX_HAS_WAITERS) != owner);
}
#ifdef CONFIG_PREEMPT_RT
static inline void mark_rt_rwlock_check(struct rw_mutex *rwm)
{
	unsigned long owner, *p = (unsigned long *) &rwm->owner;

	do {
		owner = *p;
	} while (cmpxchg(p, owner, owner | RT_RWLOCK_CHECK) != owner);
}
#endif /* CONFIG_PREEMPT_RT */
#else
# define rt_mutex_cmpxchg(l,c,n)	(0)
# define rt_rwlock_cmpxchg(l,c,n)	({ (void)c; (void)n; 0; })
static inline void mark_rt_mutex_waiters(struct rt_mutex *lock)
{
	lock->owner = (struct task_struct *)
			((unsigned long)lock->owner | RT_MUTEX_HAS_WAITERS);
}
#ifdef CONFIG_PREEMPT_RT
static inline void mark_rt_rwlock_check(struct rw_mutex *rwm)
{
	rwm->owner = (struct task_struct *)
			((unsigned long)rwm->owner | RT_RWLOCK_CHECK);
}
#endif /* CONFIG_PREEMPT_RT */
#endif

int pi_initialized;

/*
 * we initialize the wait_list runtime. (Could be done build-time and/or
 * boot-time.)
 */
static inline void init_lists(struct rt_mutex *lock)
{
	if (unlikely(!lock->wait_list.prio_list.prev)) {
		plist_head_init(&lock->wait_list, &lock->wait_lock);
#ifdef CONFIG_DEBUG_RT_MUTEXES
		pi_initialized++;
#endif
	}
}

static int rt_mutex_get_readers_prio(struct task_struct *task, int prio);

/*
 * Calculate task priority from the waiter list priority
 *
 * Return task->normal_prio when the waiter list is empty or when
 * the waiter is not allowed to do priority boosting
 */
int rt_mutex_getprio(struct task_struct *task)
{
	int prio = min(task->normal_prio, get_rcu_prio(task));

	prio = rt_mutex_get_readers_prio(task, prio);

	if (likely(!task_has_pi_waiters(task)))
		return prio;

	return min(task_top_pi_waiter(task)->pi_list_entry.prio, prio);
}

/*
 * Adjust the priority of a task, after its pi_waiters got modified.
 *
 * This can be both boosting and unboosting. task->pi_lock must be held.
 */
static void __rt_mutex_adjust_prio(struct task_struct *task)
{
	int prio = rt_mutex_getprio(task);

	if (task->prio != prio)
		rt_mutex_setprio(task, prio);
}

/*
 * Adjust task priority (undo boosting). Called from the exit path of
 * rt_mutex_slowunlock() and rt_mutex_slowlock().
 *
 * (Note: We do this outside of the protection of lock->wait_lock to
 * allow the lock to be taken while or before we readjust the priority
 * of task. We do not use the spin_xx_mutex() variants here as we are
 * outside of the debug path.)
 */
static void rt_mutex_adjust_prio(struct task_struct *task)
{
	unsigned long flags;

	spin_lock_irqsave(&task->pi_lock, flags);
	__rt_mutex_adjust_prio(task);
	spin_unlock_irqrestore(&task->pi_lock, flags);
}

/*
 * Max number of times we'll walk the boosting chain:
 */
int max_lock_depth = 1024;

static int rt_mutex_adjust_readers(struct rt_mutex *orig_lock,
				   struct rt_mutex_waiter *orig_waiter,
				   struct task_struct *top_task,
				   struct rt_mutex *lock,
				   int recursion_depth);
/*
 * Adjust the priority chain. Also used for deadlock detection.
 * Decreases task's usage by one - may thus free the task.
 * Returns 0 or -EDEADLK.
 */
static int rt_mutex_adjust_prio_chain(struct task_struct *task,
				      int deadlock_detect,
				      struct rt_mutex *orig_lock,
				      struct rt_mutex_waiter *orig_waiter,
				      struct task_struct *top_task,
				      int recursion_depth)
{
	struct rt_mutex *lock;
	struct rt_mutex_waiter *waiter, *top_waiter = orig_waiter;
	int detect_deadlock, ret = 0, depth = 0;
	unsigned long flags;

	detect_deadlock = debug_rt_mutex_detect_deadlock(orig_waiter,
							 deadlock_detect);

	/*
	 * The (de)boosting is a step by step approach with a lot of
	 * pitfalls. We want this to be preemptible and we want hold a
	 * maximum of two locks per step. So we have to check
	 * carefully whether things change under us.
	 */
 again:
	if (++depth > max_lock_depth) {
		static int prev_max;

		/*
		 * Print this only once. If the admin changes the limit,
		 * print a new message when reaching the limit again.
		 */
		if (prev_max != max_lock_depth) {
			prev_max = max_lock_depth;
			printk(KERN_WARNING "Maximum lock depth %d reached "
			       "task: %s (%d)\n", max_lock_depth,
			       top_task->comm, task_pid_nr(top_task));
		}
		put_task_struct(task);

		return deadlock_detect ? -EDEADLK : 0;
	}
 retry:
	/*
	 * Task can not go away as we did a get_task() before !
	 */
	spin_lock_irqsave(&task->pi_lock, flags);

	waiter = task->pi_blocked_on;
	/*
	 * Check whether the end of the boosting chain has been
	 * reached or the state of the chain has changed while we
	 * dropped the locks.
	 */
	if (!waiter || !waiter->task)
		goto out_unlock_pi;

	/*
	 * Check the orig_waiter state. After we dropped the locks,
	 * the previous owner of the lock might have released the lock
	 * and made us the pending owner:
	 */
	if (orig_waiter && !orig_waiter->task)
		goto out_unlock_pi;

	/*
	 * Drop out, when the task has no waiters. Note,
	 * top_waiter can be NULL, when we are in the deboosting
	 * mode!
	 */
	if (top_waiter && (!task_has_pi_waiters(task) ||
			   top_waiter != task_top_pi_waiter(task)))
		goto out_unlock_pi;

	/*
	 * When deadlock detection is off then we check, if further
	 * priority adjustment is necessary.
	 */
	if (!detect_deadlock && waiter->list_entry.prio == task->prio)
		goto out_unlock_pi;

	lock = waiter->lock;
	if (!spin_trylock(&lock->wait_lock)) {
		spin_unlock_irqrestore(&task->pi_lock, flags);
		cpu_relax();
		goto retry;
	}

	/* Deadlock detection */
	if (lock == orig_lock || rt_mutex_owner(lock) == top_task) {
		debug_rt_mutex_deadlock(deadlock_detect, orig_waiter, lock);
		spin_unlock(&lock->wait_lock);
		ret = deadlock_detect ? -EDEADLK : 0;
		goto out_unlock_pi;
	}

	top_waiter = rt_mutex_top_waiter(lock);

	/* Requeue the waiter */
	plist_del(&waiter->list_entry, &lock->wait_list);
	waiter->list_entry.prio = task->prio;
	plist_add(&waiter->list_entry, &lock->wait_list);

	/* Release the task */
	spin_unlock(&task->pi_lock);
	put_task_struct(task);

	/* Grab the next task */
	task = rt_mutex_owner(lock);

	/*
	 * Readers are special. We may need to boost more than one owner.
	 */
	if (task == RT_RW_READER) {
		ret = rt_mutex_adjust_readers(orig_lock, orig_waiter,
					      top_task, lock,
					      recursion_depth);
		spin_unlock_irqrestore(&lock->wait_lock, flags);
		goto out;
	}

	get_task_struct(task);
	spin_lock(&task->pi_lock);

	if (waiter == rt_mutex_top_waiter(lock)) {
		/* Boost the owner */
		plist_del(&top_waiter->pi_list_entry, &task->pi_waiters);
		waiter->pi_list_entry.prio = waiter->list_entry.prio;
		plist_add(&waiter->pi_list_entry, &task->pi_waiters);
		__rt_mutex_adjust_prio(task);

	} else if (top_waiter == waiter) {
		/* Deboost the owner */
		plist_del(&waiter->pi_list_entry, &task->pi_waiters);
		waiter = rt_mutex_top_waiter(lock);
		waiter->pi_list_entry.prio = waiter->list_entry.prio;
		plist_add(&waiter->pi_list_entry, &task->pi_waiters);
		__rt_mutex_adjust_prio(task);
	}

	spin_unlock(&task->pi_lock);

	top_waiter = rt_mutex_top_waiter(lock);
	spin_unlock_irqrestore(&lock->wait_lock, flags);

	if (!detect_deadlock && waiter != top_waiter)
		goto out_put_task;

	goto again;

 out_unlock_pi:
	spin_unlock_irqrestore(&task->pi_lock, flags);
 out_put_task:
	put_task_struct(task);
 out:
	return ret;
}

/*
 * Optimization: check if we can steal the lock from the
 * assigned pending owner [which might not have taken the
 * lock yet]:
 */
static inline int try_to_steal_lock(struct rt_mutex *lock, int mode)
{
	struct task_struct *pendowner = rt_mutex_owner(lock);
	struct rt_mutex_waiter *next;

	if (!rt_mutex_owner_pending(lock))
		return 0;

	if (pendowner == current)
		return 1;

	WARN_ON(rt_mutex_owner(lock) == RT_RW_READER);

	spin_lock(&pendowner->pi_lock);
	if (!lock_is_stealable(pendowner, mode)) {
		spin_unlock(&pendowner->pi_lock);
		return 0;
	}

	/*
	 * Check if a waiter is enqueued on the pending owners
	 * pi_waiters list. Remove it and readjust pending owners
	 * priority.
	 */
	if (likely(!rt_mutex_has_waiters(lock))) {
		spin_unlock(&pendowner->pi_lock);
		return 1;
	}

	/* No chain handling, pending owner is not blocked on anything: */
	next = rt_mutex_top_waiter(lock);
	plist_del(&next->pi_list_entry, &pendowner->pi_waiters);
	__rt_mutex_adjust_prio(pendowner);
	spin_unlock(&pendowner->pi_lock);

	/*
	 * We are going to steal the lock and a waiter was
	 * enqueued on the pending owners pi_waiters queue. So
	 * we have to enqueue this waiter into
	 * current->pi_waiters list. This covers the case,
	 * where current is boosted because it holds another
	 * lock and gets unboosted because the booster is
	 * interrupted, so we would delay a waiter with higher
	 * priority as current->normal_prio.
	 *
	 * Note: in the rare case of a SCHED_OTHER task changing
	 * its priority and thus stealing the lock, next->task
	 * might be current:
	 */
	if (likely(next->task != current)) {
		spin_lock(&current->pi_lock);
		plist_add(&next->pi_list_entry, &current->pi_waiters);
		__rt_mutex_adjust_prio(current);
		spin_unlock(&current->pi_lock);
	}
	return 1;
}

/*
 * Try to take an rt-mutex
 *
 * This fails
 * - when the lock has a real owner
 * - when a different pending owner exists and has higher priority than current
 *
 * Must be called with lock->wait_lock held.
 */
static int do_try_to_take_rt_mutex(struct rt_mutex *lock, int mode)
{
	/*
	 * We have to be careful here if the atomic speedups are
	 * enabled, such that, when
	 *  - no other waiter is on the lock
	 *  - the lock has been released since we did the cmpxchg
	 * the lock can be released or taken while we are doing the
	 * checks and marking the lock with RT_MUTEX_HAS_WAITERS.
	 *
	 * The atomic acquire/release aware variant of
	 * mark_rt_mutex_waiters uses a cmpxchg loop. After setting
	 * the WAITERS bit, the atomic release / acquire can not
	 * happen anymore and lock->wait_lock protects us from the
	 * non-atomic case.
	 *
	 * Note, that this might set lock->owner =
	 * RT_MUTEX_HAS_WAITERS in the case the lock is not contended
	 * any more. This is fixed up when we take the ownership.
	 * This is the transitional state explained at the top of this file.
	 */
	mark_rt_mutex_waiters(lock);

	if (rt_mutex_owner(lock) && !try_to_steal_lock(lock, mode))
		return 0;

	/* We got the lock. */
	debug_rt_mutex_lock(lock);

	rt_mutex_set_owner(lock, current, 0);

	rt_mutex_deadlock_account_lock(lock, current);

	return 1;
}

static inline int try_to_take_rt_mutex(struct rt_mutex *lock)
{
	return do_try_to_take_rt_mutex(lock, STEAL_NORMAL);
}

/*
 * Task blocks on lock.
 *
 * Prepare waiter and propagate pi chain
 *
 * This must be called with lock->wait_lock held.
 */
static int task_blocks_on_rt_mutex(struct rt_mutex *lock,
				   struct rt_mutex_waiter *waiter,
				   int detect_deadlock, unsigned long flags)
{
	struct task_struct *owner = rt_mutex_owner(lock);
	struct rt_mutex_waiter *top_waiter = waiter;
	int chain_walk = 0, res;

	spin_lock(&current->pi_lock);
	__rt_mutex_adjust_prio(current);
	waiter->task = current;
	waiter->lock = lock;
	plist_node_init(&waiter->list_entry, current->prio);
	plist_node_init(&waiter->pi_list_entry, current->prio);

	/* Get the top priority waiter on the lock */
	if (rt_mutex_has_waiters(lock))
		top_waiter = rt_mutex_top_waiter(lock);
	plist_add(&waiter->list_entry, &lock->wait_list);

	current->pi_blocked_on = waiter;

	spin_unlock(&current->pi_lock);

	if (waiter == rt_mutex_top_waiter(lock)) {
		/* readers are handled differently */
		if (owner == RT_RW_READER) {
			res = rt_mutex_adjust_readers(lock, waiter,
						      current, lock, 0);
			return res;
		}

		spin_lock(&owner->pi_lock);
		plist_del(&top_waiter->pi_list_entry, &owner->pi_waiters);
		plist_add(&waiter->pi_list_entry, &owner->pi_waiters);

		__rt_mutex_adjust_prio(owner);
		if (owner->pi_blocked_on)
			chain_walk = 1;
		spin_unlock(&owner->pi_lock);
	}
	else if (debug_rt_mutex_detect_deadlock(waiter, detect_deadlock))
		chain_walk = 1;

	if (!chain_walk || owner == RT_RW_READER)
		return 0;

	/*
	 * The owner can't disappear while holding a lock,
	 * so the owner struct is protected by wait_lock.
	 * Gets dropped in rt_mutex_adjust_prio_chain()!
	 */
	get_task_struct(owner);

	spin_unlock_irqrestore(&lock->wait_lock, flags);

	res = rt_mutex_adjust_prio_chain(owner, detect_deadlock, lock, waiter,
					 current, 0);

	spin_lock_irq(&lock->wait_lock);

	return res;
}

/*
 * Wake up the next waiter on the lock.
 *
 * Remove the top waiter from the current tasks waiter list and from
 * the lock waiter list. Set it as pending owner. Then wake it up.
 *
 * Called with lock->wait_lock held.
 */
static void wakeup_next_waiter(struct rt_mutex *lock, int savestate)
{
	struct rt_mutex_waiter *waiter;
	struct task_struct *pendowner;

	spin_lock(&current->pi_lock);

	waiter = rt_mutex_top_waiter(lock);
	plist_del(&waiter->list_entry, &lock->wait_list);

	/*
	 * Remove it from current->pi_waiters. We do not adjust a
	 * possible priority boost right now. We execute wakeup in the
	 * boosted mode and go back to normal after releasing
	 * lock->wait_lock.
	 */
	plist_del(&waiter->pi_list_entry, &current->pi_waiters);
	pendowner = waiter->task;
	waiter->task = NULL;

	rt_mutex_set_owner(lock, pendowner, RT_MUTEX_OWNER_PENDING);

	spin_unlock(&current->pi_lock);

	/*
	 * Clear the pi_blocked_on variable and enqueue a possible
	 * waiter into the pi_waiters list of the pending owner. This
	 * prevents that in case the pending owner gets unboosted a
	 * waiter with higher priority than pending-owner->normal_prio
	 * is blocked on the unboosted (pending) owner.
	 */
	spin_lock(&pendowner->pi_lock);

	WARN_ON(!pendowner->pi_blocked_on);
	WARN_ON(pendowner->pi_blocked_on != waiter);
	WARN_ON(pendowner->pi_blocked_on->lock != lock);

	pendowner->pi_blocked_on = NULL;

	if (rt_mutex_has_waiters(lock)) {
		struct rt_mutex_waiter *next;

		next = rt_mutex_top_waiter(lock);
		plist_add(&next->pi_list_entry, &pendowner->pi_waiters);
	}
	spin_unlock(&pendowner->pi_lock);

	if (savestate)
		wake_up_process_mutex(pendowner);
	else
		wake_up_process(pendowner);
}

/*
 * Remove a waiter from a lock
 *
 * Must be called with lock->wait_lock held
 */
static void remove_waiter(struct rt_mutex *lock,
			  struct rt_mutex_waiter *waiter,
			  unsigned long flags)
{
	int first = (waiter == rt_mutex_top_waiter(lock));
	struct task_struct *owner = rt_mutex_owner(lock);
	int chain_walk = 0;

	spin_lock(&current->pi_lock);
	plist_del(&waiter->list_entry, &lock->wait_list);
	waiter->task = NULL;
	current->pi_blocked_on = NULL;
	spin_unlock(&current->pi_lock);

	if (first && owner != current && owner != RT_RW_READER) {

		spin_lock(&owner->pi_lock);

		plist_del(&waiter->pi_list_entry, &owner->pi_waiters);

		if (rt_mutex_has_waiters(lock)) {
			struct rt_mutex_waiter *next;

			next = rt_mutex_top_waiter(lock);
			plist_add(&next->pi_list_entry, &owner->pi_waiters);
		}
		__rt_mutex_adjust_prio(owner);

		if (owner->pi_blocked_on)
			chain_walk = 1;

		spin_unlock(&owner->pi_lock);
	}

	WARN_ON(!plist_node_empty(&waiter->pi_list_entry));

	if (!chain_walk)
		return;

	/* gets dropped in rt_mutex_adjust_prio_chain()! */
	get_task_struct(owner);

	spin_unlock_irqrestore(&lock->wait_lock, flags);

	rt_mutex_adjust_prio_chain(owner, 0, lock, NULL, current, 0);

	spin_lock_irq(&lock->wait_lock);
}

/*
 * Recheck the pi chain, in case we got a priority setting
 *
 * Called from sched_setscheduler
 */
void rt_mutex_adjust_pi(struct task_struct *task)
{
	struct rt_mutex_waiter *waiter;
	unsigned long flags;

	spin_lock_irqsave(&task->pi_lock, flags);

	waiter = task->pi_blocked_on;
	if (!waiter || waiter->list_entry.prio == task->prio) {
		spin_unlock_irqrestore(&task->pi_lock, flags);
		return;
	}

	/* gets dropped in rt_mutex_adjust_prio_chain()! */
	get_task_struct(task);
	spin_unlock_irqrestore(&task->pi_lock, flags);

	rt_mutex_adjust_prio_chain(task, 0, NULL, NULL, task, 0);
}

/*
 * preemptible spin_lock functions:
 */

#ifdef CONFIG_PREEMPT_RT

static inline void
rt_spin_lock_fastlock(struct rt_mutex *lock,
		void fastcall (*slowfn)(struct rt_mutex *lock))
{
	/* Temporary HACK! */
	if (likely(!current->in_printk))
		might_sleep();
	else if (in_atomic() || irqs_disabled())
		/* don't grab locks for printk in atomic */
		return;

	if (likely(rt_mutex_cmpxchg(lock, NULL, current)))
		rt_mutex_deadlock_account_lock(lock, current);
	else
		slowfn(lock);
}

static inline void
rt_spin_lock_fastunlock(struct rt_mutex *lock,
			void fastcall (*slowfn)(struct rt_mutex *lock))
{
	/* Temporary HACK! */
	if (unlikely(rt_mutex_owner(lock) != current) && current->in_printk)
		/* don't grab locks for printk in atomic */
		return;

	if (likely(rt_mutex_cmpxchg(lock, current, NULL)))
		rt_mutex_deadlock_account_unlock(current);
	else
		slowfn(lock);
}

static inline void
update_current(unsigned long new_state, unsigned long *saved_state)
{
	unsigned long state = xchg(&current->state, new_state);
	if (unlikely(state == TASK_RUNNING))
		*saved_state = TASK_RUNNING;
}

#ifdef CONFIG_SMP
static int adaptive_wait(struct rt_mutex_waiter *waiter,
			 struct task_struct *orig_owner)
{
	int sleep = 0;

	for (;;) {

		/* we are the owner? */
		if (!waiter->task)
			break;

		/*
		 * We need to read the owner of the lock and then check
		 * its state. But we can't let the owner task be freed
		 * while we read the state. We grab the rcu_lock and
		 * this makes sure that the owner task wont disappear
		 * between testing that it still has the lock, and checking
		 * its state.
		 */
		rcu_read_lock();
		/* Owner changed? Then lets update the original */
		if (orig_owner != rt_mutex_owner(waiter->lock)) {
			rcu_read_unlock();
			break;
		}

		/* Owner went to bed, so should we */
		if (!task_is_current(orig_owner)) {
			sleep = 1;
			rcu_read_unlock();
			break;
		}
		rcu_read_unlock();

		cpu_relax();
	}

	return sleep;
}
#else
static int adaptive_wait(struct rt_mutex_waiter *waiter,
			 struct task_struct *orig_owner)
{
	return 1;
}
#endif

/*
 * Slow path lock function spin_lock style: this variant is very
 * careful not to miss any non-lock wakeups.
 *
 * The wakeup side uses wake_up_process_mutex, which, combined with
 * the xchg code of this function is a transparent sleep/wakeup
 * mechanism nested within any existing sleep/wakeup mechanism. This
 * enables the seemless use of arbitrary (blocking) spinlocks within
 * sleep/wakeup event loops.
 */
static void fastcall noinline __sched
rt_spin_lock_slowlock(struct rt_mutex *lock)
{
	struct rt_mutex_waiter waiter;
	unsigned long saved_state, state, flags;
	struct task_struct *orig_owner;

	debug_rt_mutex_init_waiter(&waiter);
	waiter.task = NULL;
	waiter.write_lock = 0;

	spin_lock_irqsave(&lock->wait_lock, flags);
	init_lists(lock);

	/* Try to acquire the lock again: */
	if (do_try_to_take_rt_mutex(lock, STEAL_LATERAL)) {
		spin_unlock_irqrestore(&lock->wait_lock, flags);
		return;
	}

	BUG_ON(rt_mutex_owner(lock) == current);

	/*
	 * Here we save whatever state the task was in originally,
	 * we'll restore it at the end of the function and we'll take
	 * any intermediate wakeup into account as well, independently
	 * of the lock sleep/wakeup mechanism. When we get a real
	 * wakeup the task->state is TASK_RUNNING and we change
	 * saved_state accordingly. If we did not get a real wakeup
	 * then we return with the saved state.
	 */
	saved_state = current->state;

	for (;;) {
		unsigned long saved_flags;
		int saved_lock_depth = current->lock_depth;

		/* Try to acquire the lock */
		if (do_try_to_take_rt_mutex(lock, STEAL_LATERAL))
			break;
		/*
		 * waiter.task is NULL the first time we come here and
		 * when we have been woken up by the previous owner
		 * but the lock got stolen by an higher prio task.
		 */
		if (!waiter.task) {
			task_blocks_on_rt_mutex(lock, &waiter, 0, flags);
			/* Wakeup during boost ? */
			if (unlikely(!waiter.task))
				continue;
		}

		/*
		 * Prevent schedule() to drop BKL, while waiting for
		 * the lock ! We restore lock_depth when we come back.
		 */
		saved_flags = current->flags & PF_NOSCHED;
		current->lock_depth = -1;
		current->flags &= ~PF_NOSCHED;
		orig_owner = rt_mutex_owner(lock);
		spin_unlock_irqrestore(&lock->wait_lock, flags);

		debug_rt_mutex_print_deadlock(&waiter);

		if (adaptive_wait(&waiter, orig_owner)) {
			update_current(TASK_UNINTERRUPTIBLE, &saved_state);
			if (waiter.task)
				schedule_rt_mutex(lock);
		}

		spin_lock_irqsave(&lock->wait_lock, flags);
		current->flags |= saved_flags;
		current->lock_depth = saved_lock_depth;
	}

	state = xchg(&current->state, saved_state);
	if (unlikely(state == TASK_RUNNING))
		current->state = TASK_RUNNING;

	/*
	 * Extremely rare case, if we got woken up by a non-mutex wakeup,
	 * and we managed to steal the lock despite us not being the
	 * highest-prio waiter (due to SCHED_OTHER changing prio), then we
	 * can end up with a non-NULL waiter.task:
	 */
	if (unlikely(waiter.task))
		remove_waiter(lock, &waiter, flags);
	/*
	 * try_to_take_rt_mutex() sets the waiter bit
	 * unconditionally. We might have to fix that up:
	 */
	fixup_rt_mutex_waiters(lock);

	spin_unlock_irqrestore(&lock->wait_lock, flags);

	debug_rt_mutex_free_waiter(&waiter);
}

/*
 * Slow path to release a rt_mutex spin_lock style
 */
static void fastcall noinline __sched
rt_spin_lock_slowunlock(struct rt_mutex *lock)
{
	unsigned long flags;

	spin_lock_irqsave(&lock->wait_lock, flags);

	debug_rt_mutex_unlock(lock);

	rt_mutex_deadlock_account_unlock(current);

	if (!rt_mutex_has_waiters(lock)) {
		lock->owner = NULL;
		spin_unlock_irqrestore(&lock->wait_lock, flags);
		return;
	}

	wakeup_next_waiter(lock, 1);

	spin_unlock_irqrestore(&lock->wait_lock, flags);

	/* Undo pi boosting.when necessary */
	rt_mutex_adjust_prio(current);
}

void __lockfunc rt_spin_lock(spinlock_t *lock)
{
	spin_acquire(&lock->dep_map, 0, 0, _RET_IP_);
	LOCK_CONTENDED_RT(lock, rt_mutex_trylock, __rt_spin_lock);
}
EXPORT_SYMBOL(rt_spin_lock);

void __lockfunc __rt_spin_lock(struct rt_mutex *lock)
{
	rt_spin_lock_fastlock(lock, rt_spin_lock_slowlock);
}
EXPORT_SYMBOL(__rt_spin_lock);

#ifdef CONFIG_DEBUG_LOCK_ALLOC

void __lockfunc rt_spin_lock_nested(spinlock_t *lock, int subclass)
{
	spin_acquire(&lock->dep_map, subclass, 0, _RET_IP_);
	LOCK_CONTENDED_RT(lock, rt_mutex_trylock, __rt_spin_lock);
}
EXPORT_SYMBOL(rt_spin_lock_nested);

#endif

void __lockfunc rt_spin_unlock(spinlock_t *lock)
{
	/* NOTE: we always pass in '1' for nested, for simplicity */
	spin_release(&lock->dep_map, 1, _RET_IP_);
	rt_spin_lock_fastunlock(&lock->lock, rt_spin_lock_slowunlock);
}
EXPORT_SYMBOL(rt_spin_unlock);

void __lockfunc __rt_spin_unlock(struct rt_mutex *lock)
{
	rt_spin_lock_fastunlock(lock, rt_spin_lock_slowunlock);
}
EXPORT_SYMBOL(__rt_spin_unlock);

/*
 * Wait for the lock to get unlocked: instead of polling for an unlock
 * (like raw spinlocks do), we lock and unlock, to force the kernel to
 * schedule if there's contention:
 */
void __lockfunc rt_spin_unlock_wait(spinlock_t *lock)
{
	spin_lock(lock);
	spin_unlock(lock);
}
EXPORT_SYMBOL(rt_spin_unlock_wait);

int __lockfunc rt_spin_trylock(spinlock_t *lock)
{
	int ret = rt_mutex_trylock(&lock->lock);

	if (ret)
		spin_acquire(&lock->dep_map, 0, 1, _RET_IP_);

	return ret;
}
EXPORT_SYMBOL(rt_spin_trylock);

int __lockfunc rt_spin_trylock_irqsave(spinlock_t *lock, unsigned long *flags)
{
	int ret;

	*flags = 0;
	ret = rt_mutex_trylock(&lock->lock);
	if (ret)
		spin_acquire(&lock->dep_map, 0, 1, _RET_IP_);

	return ret;
}
EXPORT_SYMBOL(rt_spin_trylock_irqsave);

int _atomic_dec_and_spin_lock(spinlock_t *lock, atomic_t *atomic)
{
	/* Subtract 1 from counter unless that drops it to 0 (ie. it was 1) */
	if (atomic_add_unless(atomic, -1, 1))
		return 0;
	rt_spin_lock(lock);
	if (atomic_dec_and_test(atomic))
		return 1;
	rt_spin_unlock(lock);
	return 0;
}
EXPORT_SYMBOL(_atomic_dec_and_spin_lock);

void
__rt_spin_lock_init(spinlock_t *lock, char *name, struct lock_class_key *key)
{
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	/*
	 * Make sure we are not reinitializing a held lock:
	 */
	debug_check_no_locks_freed((void *)lock, sizeof(*lock));
	lockdep_init_map(&lock->dep_map, name, key, 0);
#endif
	__rt_mutex_init(&lock->lock, name);
}
EXPORT_SYMBOL(__rt_spin_lock_init);

int rt_rwlock_limit;

static inline int rt_release_bkl(struct rt_mutex *lock, unsigned long flags);
static inline void rt_reacquire_bkl(int saved_lock_depth);

static inline void
rt_rwlock_set_owner(struct rw_mutex *rwm, struct task_struct *owner,
		    unsigned long mask)
{
	unsigned long val = (unsigned long)owner | mask;

	rwm->owner = (struct task_struct *)val;
}

static inline void init_rw_lists(struct rw_mutex *rwm)
{
	if (unlikely(!rwm->readers.prev)) {
		init_lists(&rwm->mutex);
		INIT_LIST_HEAD(&rwm->readers);
	}
}

/*
 * The fast paths of the rw locks do not set up owners to
 * the mutex. When blocking on an rwlock we must make sure
 * there exists an owner.
 */
static void
update_rw_mutex_owner(struct rw_mutex *rwm)
{
	struct rt_mutex *mutex = &rwm->mutex;
	struct task_struct *mtxowner;

	mtxowner = rt_mutex_owner(mutex);
	if (mtxowner)
		return;

	mtxowner = rt_rwlock_owner(rwm);
	WARN_ON(!mtxowner);
	if (rt_rwlock_writer(rwm))
		WARN_ON(mtxowner == RT_RW_READER);
	else
		mtxowner = RT_RW_READER;
	rt_mutex_set_owner(mutex, mtxowner, 0);
}

/*
 * The fast path does not add itself to the reader list to keep
 * from needing to grab the spinlock. We need to add the owner
 * itself. This may seem racy, but in practice, it is fine.
 * The link list is protected by mutex->wait_lock. But to find
 * the lock on the owner we need to read the owners reader counter.
 * That counter is modified only by the owner. We are OK with that
 * because to remove the lock that we are looking for, the owner
 * must first grab the mutex->wait_lock. The lock will not disappear
 * from the owner now, and we don't care if we see other locks
 * held or not held.
 */

static inline void
rt_rwlock_update_owner(struct rw_mutex *rwm, unsigned owners)
{
	struct reader_lock_struct *rls;
	struct task_struct *own;
	int i;

	if (!owners || rt_rwlock_pending(rwm))
		return;

	own = rt_rwlock_owner(rwm);
	if (own == RT_RW_READER)
		return;

	for (i = own->reader_lock_count - 1; i >= 0; i--) {
		if (own->owned_read_locks[i].lock == rwm)
			break;
	}
	/* It is possible the owner didn't add it yet */
	if (i < 0)
		return;

	rls = &own->owned_read_locks[i];
	/* It is also possible that the owner added it already */
	if (rls->list.prev && !list_empty(&rls->list))
		return;

	list_add(&rls->list, &rwm->readers);

	/* change to reader, so no one else updates too */
	rt_rwlock_set_owner(rwm, RT_RW_READER, RT_RWLOCK_CHECK);
}

static int try_to_take_rw_read(struct rw_mutex *rwm, int mtx)
{
	struct rt_mutex *mutex = &rwm->mutex;
	struct rt_mutex_waiter *waiter;
	struct reader_lock_struct *rls;
	struct task_struct *mtxowner;
	int owners;
	int reader_count, i;
	int incr = 1;

	assert_spin_locked(&mutex->wait_lock);

	/* mark the lock to force the owner to check on release */
	mark_rt_rwlock_check(rwm);

	/* is the owner a writer? */
	if (unlikely(rt_rwlock_writer(rwm)))
		return 0;

	/* check to see if we don't already own this lock */
	for (i = current->reader_lock_count - 1; i >= 0; i--) {
		if (current->owned_read_locks[i].lock == rwm) {
			rls = &current->owned_read_locks[i];
			/*
			 * If this was taken via the fast path, then
			 * it hasn't been added to the link list yet.
			 */
			if (!rls->list.prev || list_empty(&rls->list))
				list_add(&rls->list, &rwm->readers);
			rt_rwlock_set_owner(rwm, RT_RW_READER, 0);
			rls->count++;
			incr = 0;
			goto taken;
		}
	}

	/* A writer is not the owner, but is a writer waiting */
	mtxowner = rt_mutex_owner(mutex);

	/* if the owner released it before we marked it then take it */
	if (!mtxowner && !rt_rwlock_owner(rwm)) {
		/* Still unlock with the slow path (for PI handling) */
		rt_rwlock_set_owner(rwm, RT_RW_READER, 0);
		goto taken;
	}

	owners = atomic_read(&rwm->owners);
	rt_rwlock_update_owner(rwm, owners);

	/* Check for rwlock limits */
	if (rt_rwlock_limit && owners >= rt_rwlock_limit)
		return 0;

	if (mtxowner && mtxowner != RT_RW_READER) {
		int mode = mtx ? STEAL_NORMAL : STEAL_LATERAL;

		if (!try_to_steal_lock(mutex, mode)) {
			/*
			 * readers don't own the mutex, and rwm shows that a
			 * writer doesn't have it either. If we enter this
			 * condition, then we must be pending.
			 */
			WARN_ON(!rt_mutex_owner_pending(mutex));
			/*
			 * Even though we didn't steal the lock, if the owner
			 * is a reader, and we are of higher priority than
			 * any waiting writer, we might still be able to continue.
			 */
			if (rt_rwlock_pending_writer(rwm))
				return 0;
			if (rt_mutex_has_waiters(mutex)) {
				waiter = rt_mutex_top_waiter(mutex);
				if (!lock_is_stealable(waiter->task, mode))
					return 0;
				/*
				 * The pending reader has PI waiters,
				 * but we are taking the lock.
				 * Remove the waiters from the pending owner.
				 */
				spin_lock(&mtxowner->pi_lock);
				plist_del(&waiter->pi_list_entry, &mtxowner->pi_waiters);
				spin_unlock(&mtxowner->pi_lock);
			}
		} else if (rt_mutex_has_waiters(mutex)) {
			/* Readers do things differently with respect to PI */
			waiter = rt_mutex_top_waiter(mutex);
			spin_lock(&current->pi_lock);
			plist_del(&waiter->pi_list_entry, &current->pi_waiters);
			spin_unlock(&current->pi_lock);
		}
		/* Readers never own the mutex */
		rt_mutex_set_owner(mutex, RT_RW_READER, 0);
	}

	/* RT_RW_READER forces slow paths */
	rt_rwlock_set_owner(rwm, RT_RW_READER, 0);
 taken:
	if (incr) {
		atomic_inc(&rwm->owners);
		reader_count = current->reader_lock_count++;
		if (likely(reader_count < MAX_RWLOCK_DEPTH)) {
			rls = &current->owned_read_locks[reader_count];
			rls->lock = rwm;
			rls->count = 1;
			WARN_ON(rls->list.prev && !list_empty(&rls->list));
			list_add(&rls->list, &rwm->readers);
		} else
			WARN_ON_ONCE(1);
	}
	rt_mutex_deadlock_account_lock(mutex, current);
	atomic_inc(&rwm->count);
	return 1;
}

static int
try_to_take_rw_write(struct rw_mutex *rwm, int mtx)
{
	struct rt_mutex *mutex = &rwm->mutex;
	struct task_struct *own;

	/* mark the lock to force the owner to check on release */
	mark_rt_rwlock_check(rwm);

	own = rt_rwlock_owner(rwm);

	/* owners must be zero for writer */
	rt_rwlock_update_owner(rwm, atomic_read(&rwm->owners));

	/* readers or writers? */
	if ((own && !rt_rwlock_pending(rwm)))
		return 0;

	/*
	 * RT_RW_PENDING means that the lock is free, but there are
	 * pending owners on the mutex
	 */
	WARN_ON(own && !rt_mutex_owner_pending(mutex));

	if (!do_try_to_take_rt_mutex(mutex, mtx ? STEAL_NORMAL : STEAL_LATERAL))
		return 0;

	/*
	 * We stole the lock. Add both WRITER and CHECK flags
	 * since we must release the mutex.
	 */
	rt_rwlock_set_owner(rwm, current, RT_RWLOCK_WRITER | RT_RWLOCK_CHECK);

	return 1;
}

static void
rt_read_slowlock(struct rw_mutex *rwm, int mtx)
{
	struct rt_mutex_waiter waiter;
	struct rt_mutex *mutex = &rwm->mutex;
	int saved_lock_depth = -1;
	unsigned long saved_state = -1, state, flags;

	spin_lock_irqsave(&mutex->wait_lock, flags);
	init_rw_lists(rwm);

	if (try_to_take_rw_read(rwm, mtx)) {
		spin_unlock_irqrestore(&mutex->wait_lock, flags);
		return;
	}
	update_rw_mutex_owner(rwm);

	/* Owner is a writer (or a blocked writer). Block on the lock */

	debug_rt_mutex_init_waiter(&waiter);
	waiter.task = NULL;
	waiter.write_lock = 0;

	if (mtx) {
		/*
		 * We drop the BKL here before we go into the wait loop to avoid a
		 * possible deadlock in the scheduler.
		 */
		if (unlikely(current->lock_depth >= 0))
			saved_lock_depth = rt_release_bkl(mutex, flags);
		set_current_state(TASK_UNINTERRUPTIBLE);
	} else {
		/* Spin lock must preserve BKL */
		saved_state = xchg(&current->state, TASK_UNINTERRUPTIBLE);
		saved_lock_depth = current->lock_depth;
	}

	for (;;) {
		unsigned long saved_flags;

		/* Try to acquire the lock: */
		if (try_to_take_rw_read(rwm, mtx))
			break;
		update_rw_mutex_owner(rwm);

		/*
		 * waiter.task is NULL the first time we come here and
		 * when we have been woken up by the previous owner
		 * but the lock got stolen by a higher prio task.
		 */
		if (!waiter.task) {
			task_blocks_on_rt_mutex(mutex, &waiter, 0, flags);
			/* Wakeup during boost ? */
			if (unlikely(!waiter.task))
				continue;
		}
		saved_flags = current->flags & PF_NOSCHED;
		current->flags &= ~PF_NOSCHED;
		if (!mtx)
			current->lock_depth = -1;

		spin_unlock_irqrestore(&mutex->wait_lock, flags);

		debug_rt_mutex_print_deadlock(&waiter);

		if (!mtx || waiter.task)
			schedule_rt_mutex(mutex);

		spin_lock_irqsave(&mutex->wait_lock, flags);

		current->flags |= saved_flags;
		if (mtx)
			set_current_state(TASK_UNINTERRUPTIBLE);
		else {
			current->lock_depth = saved_lock_depth;
			state = xchg(&current->state, TASK_UNINTERRUPTIBLE);
			if (unlikely(state == TASK_RUNNING))
				saved_state = TASK_RUNNING;
		}
	}

	if (mtx)
		set_current_state(TASK_RUNNING);
	else {
		state = xchg(&current->state, saved_state);
		if (unlikely(state == TASK_RUNNING))
			current->state = TASK_RUNNING;
	}

	if (unlikely(waiter.task))
		remove_waiter(mutex, &waiter, flags);

	WARN_ON(rt_mutex_owner(mutex) &&
		rt_mutex_owner(mutex) != current &&
		rt_mutex_owner(mutex) != RT_RW_READER &&
		!rt_mutex_owner_pending(mutex));

	spin_unlock_irqrestore(&mutex->wait_lock, flags);

	/* Must we reaquire the BKL? */
	if (mtx && unlikely(saved_lock_depth >= 0))
		rt_reacquire_bkl(saved_lock_depth);

	debug_rt_mutex_free_waiter(&waiter);
}

static inline int
__rt_read_fasttrylock(struct rw_mutex *rwm)
{
 retry:
	if (likely(rt_rwlock_cmpxchg(rwm, NULL, current))) {
		int reader_count;

		rt_mutex_deadlock_account_lock(&rwm->mutex, current);
		atomic_inc(&rwm->count);
		smp_mb();
		/*
		 * It is possible that the owner was zeroed
		 * before we incremented count. If owner is not
		 * current, then retry again
		 */
		if (unlikely(rwm->owner != current)) {
			atomic_dec(&rwm->count);
			goto retry;
		}

		atomic_inc(&rwm->owners);
		reader_count = current->reader_lock_count;
		if (likely(reader_count < MAX_RWLOCK_DEPTH)) {
			current->owned_read_locks[reader_count].lock = rwm;
			current->owned_read_locks[reader_count].count = 1;
		} else
			WARN_ON_ONCE(1);
		/*
		 * If this task is no longer the sole owner of the lock
		 * or someone is blocking, then we need to add the task
		 * to the lock.
		 */
		smp_mb();
		current->reader_lock_count++;
		if (unlikely(rwm->owner != current)) {
			struct rt_mutex *mutex = &rwm->mutex;
			struct reader_lock_struct *rls;
			unsigned long flags;

			spin_lock_irqsave(&mutex->wait_lock, flags);
			rls = &current->owned_read_locks[reader_count];
			if (!rls->list.prev || list_empty(&rls->list))
				list_add(&rls->list, &rwm->readers);
			spin_unlock_irqrestore(&mutex->wait_lock, flags);
		}
		return 1;
	}
	return 0;
}

static inline void
rt_read_fastlock(struct rw_mutex *rwm,
		 void fastcall (*slowfn)(struct rw_mutex *rwm, int mtx),
		 int mtx)
{
	if (unlikely(!__rt_read_fasttrylock(rwm)))
		slowfn(rwm, mtx);
}

void fastcall rt_mutex_down_read(struct rw_mutex *rwm)
{
	rt_read_fastlock(rwm, rt_read_slowlock, 1);
}

void fastcall rt_rwlock_read_lock(struct rw_mutex *rwm)
{
	rt_read_fastlock(rwm, rt_read_slowlock, 0);
}


static inline int
rt_read_slowtrylock(struct rw_mutex *rwm, int mtx)
{
	struct rt_mutex *mutex = &rwm->mutex;
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&mutex->wait_lock, flags);
	init_rw_lists(rwm);

	if (try_to_take_rw_read(rwm, mtx))
		ret = 1;

	spin_unlock_irqrestore(&mutex->wait_lock, flags);

	return ret;
}

static inline int
rt_read_fasttrylock(struct rw_mutex *rwm,
		    int fastcall (*slowfn)(struct rw_mutex *rwm, int mtx), int mtx)
{
	if (likely(__rt_read_fasttrylock(rwm)))
		return 1;
	else
		return slowfn(rwm, mtx);
}

int __sched rt_mutex_down_read_trylock(struct rw_mutex *rwm)
{
	return rt_read_fasttrylock(rwm, rt_read_slowtrylock, 1);
}

static void
rt_write_slowlock(struct rw_mutex *rwm, int mtx)
{
	struct rt_mutex *mutex = &rwm->mutex;
	struct rt_mutex_waiter waiter;
	int saved_lock_depth = -1;
	unsigned long flags, saved_state = -1, state;

	debug_rt_mutex_init_waiter(&waiter);
	waiter.task = NULL;

	/* we do PI different for writers that are blocked */
	waiter.write_lock = 1;

	spin_lock_irqsave(&mutex->wait_lock, flags);
	init_rw_lists(rwm);

	if (try_to_take_rw_write(rwm, mtx)) {
		spin_unlock_irqrestore(&mutex->wait_lock, flags);
		return;
	}
	update_rw_mutex_owner(rwm);

	if (mtx) {
		/*
		 * We drop the BKL here before we go into the wait loop to avoid a
		 * possible deadlock in the scheduler.
		 */
		if (unlikely(current->lock_depth >= 0))
			saved_lock_depth = rt_release_bkl(mutex, flags);
		set_current_state(TASK_UNINTERRUPTIBLE);
	} else {
		/* Spin locks must preserve the BKL */
		saved_lock_depth = current->lock_depth;
		saved_state = xchg(&current->state, TASK_UNINTERRUPTIBLE);
	}

	for (;;) {
		unsigned long saved_flags;

		/* Try to acquire the lock: */
		if (try_to_take_rw_write(rwm, mtx))
			break;
		update_rw_mutex_owner(rwm);

		/*
		 * waiter.task is NULL the first time we come here and
		 * when we have been woken up by the previous owner
		 * but the lock got stolen by a higher prio task.
		 */
		if (!waiter.task) {
			task_blocks_on_rt_mutex(mutex, &waiter, 0, flags);
			/* Wakeup during boost ? */
			if (unlikely(!waiter.task))
				continue;
		}
		saved_flags = current->flags & PF_NOSCHED;
		current->flags &= ~PF_NOSCHED;
		if (!mtx)
			current->lock_depth = -1;

		spin_unlock_irqrestore(&mutex->wait_lock, flags);

		debug_rt_mutex_print_deadlock(&waiter);

		if (!mtx || waiter.task)
			schedule_rt_mutex(mutex);

		spin_lock_irqsave(&mutex->wait_lock, flags);

		current->flags |= saved_flags;
		if (mtx)
			set_current_state(TASK_UNINTERRUPTIBLE);
		else {
			current->lock_depth = saved_lock_depth;
			state = xchg(&current->state, TASK_UNINTERRUPTIBLE);
			if (unlikely(state == TASK_RUNNING))
				saved_state = TASK_RUNNING;
		}
	}

	if (mtx)
		set_current_state(TASK_RUNNING);
	else {
		state = xchg(&current->state, saved_state);
		if (unlikely(state == TASK_RUNNING))
			current->state = TASK_RUNNING;
	}

	if (unlikely(waiter.task))
		remove_waiter(mutex, &waiter, flags);

	/* check on unlock if we have any waiters. */
	if (rt_mutex_has_waiters(mutex))
		mark_rt_rwlock_check(rwm);

	spin_unlock_irqrestore(&mutex->wait_lock, flags);

	/* Must we reaquire the BKL? */
	if (mtx && unlikely(saved_lock_depth >= 0))
		rt_reacquire_bkl(saved_lock_depth);

	debug_rt_mutex_free_waiter(&waiter);

}

static inline void
rt_write_fastlock(struct rw_mutex *rwm,
		  void fastcall (*slowfn)(struct rw_mutex *rwm, int mtx),
		  int mtx)
{
	unsigned long val = (unsigned long)current | RT_RWLOCK_WRITER;

	if (likely(rt_rwlock_cmpxchg(rwm, NULL, val)))
		rt_mutex_deadlock_account_lock(&rwm->mutex, current);
	else
		slowfn(rwm, mtx);
}

void fastcall rt_mutex_down_write(struct rw_mutex *rwm)
{
	rt_write_fastlock(rwm, rt_write_slowlock, 1);
}

void fastcall rt_rwlock_write_lock(struct rw_mutex *rwm)
{
	rt_write_fastlock(rwm, rt_write_slowlock, 0);
}

static int
rt_write_slowtrylock(struct rw_mutex *rwm, int mtx)
{
	struct rt_mutex *mutex = &rwm->mutex;
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&mutex->wait_lock, flags);
	init_rw_lists(rwm);

	if (try_to_take_rw_write(rwm, mtx))
		ret = 1;

	spin_unlock_irqrestore(&mutex->wait_lock, flags);

	return ret;
}

static inline int
rt_write_fasttrylock(struct rw_mutex *rwm,
		     int fastcall (*slowfn)(struct rw_mutex *rwm, int mtx), int mtx)
{
	unsigned long val = (unsigned long)current | RT_RWLOCK_WRITER;

	if (likely(rt_rwlock_cmpxchg(rwm, NULL, val))) {
		rt_mutex_deadlock_account_lock(&rwm->mutex, current);
		return 1;
	} else
		return slowfn(rwm, mtx);
}

int fastcall rt_mutex_down_write_trylock(struct rw_mutex *rwm)
{
	return rt_write_fasttrylock(rwm, rt_write_slowtrylock, 1);
}

static void fastcall noinline __sched
rt_read_slowunlock(struct rw_mutex *rwm, int mtx)
{
	struct rt_mutex *mutex = &rwm->mutex;
	struct rt_mutex_waiter *waiter;
	struct reader_lock_struct *rls;
	unsigned long flags;
	unsigned int reader_count;
	int savestate = !mtx;
	int i;

	spin_lock_irqsave(&mutex->wait_lock, flags);

	rt_mutex_deadlock_account_unlock(current);

	/*
	 * To prevent multiple readers from zeroing out the owner
	 * when the count goes to zero and then have another task
	 * grab the task. We mark the lock. This makes all tasks
	 * go to the slow path. Then we can check the owner without
	 * worry that it changed.
	 */
	mark_rt_rwlock_check(rwm);

	for (i = current->reader_lock_count - 1; i >= 0; i--) {
		if (current->owned_read_locks[i].lock == rwm) {
			current->owned_read_locks[i].count--;
			if (!current->owned_read_locks[i].count) {
				current->reader_lock_count--;
				WARN_ON_ONCE(i != current->reader_lock_count);
				atomic_dec(&rwm->owners);
				rls = &current->owned_read_locks[i];
				WARN_ON(!rls->list.prev || list_empty(&rls->list));
				list_del_init(&rls->list);
				rls->lock = NULL;
			}
			break;
		}
	}
	WARN_ON_ONCE(i < 0);

	/*
	 * If the last two (or more) readers unlocked at the same
	 * time, the owner could be cleared since the count went to
	 * zero. If this has happened, the rwm owner will not
	 * be set to current or readers. This means that another reader
	 * already reset the lock, so there is nothing left to do.
	 */
	if (unlikely(rt_rwlock_owner(rwm) != current &&
		     rt_rwlock_owner(rwm) != RT_RW_READER)) {
		/* Update the owner if necessary */
		rt_rwlock_update_owner(rwm, atomic_read(&rwm->owners));
		goto out;
	}

	/*
	 * If there are more readers and we are under the limit
	 * let the last reader do the wakeups.
	 */
	reader_count = atomic_read(&rwm->count);
	if (reader_count &&
	    (!rt_rwlock_limit || atomic_read(&rwm->owners) >= rt_rwlock_limit))
		goto out;

	/* If no one is blocked, then clear all ownership */
	if (!rt_mutex_has_waiters(mutex)) {
		rwm->prio = MAX_PRIO;
		/*
		 * If count is not zero, we are under the limit with
		 * no other readers.
		 */
		if (reader_count)
			goto out;

		/* We could still have a pending reader waiting */
		if (rt_mutex_owner_pending(mutex)) {
			/* set the rwm back to pending */
			rwm->owner = RT_RW_PENDING_READ;
		} else {
			rwm->owner = NULL;
			mutex->owner = NULL;
		}
		goto out;
	}

	/*
	 * If the next waiter is a reader, this can be because of
	 * two things. One is that we hit the reader limit, or
	 * Two, there is a pending writer.
	 * We still only wake up one reader at a time (even if
	 * we could wake up more). This is because we dont
	 * have any idea if a writer is pending.
	 */
	waiter = rt_mutex_top_waiter(mutex);
	if (waiter->write_lock) {
		/* only wake up if there are no readers */
		if (reader_count)
			goto out;
		rwm->owner = RT_RW_PENDING_WRITE;
	} else {
		/*
		 * It is also possible that the reader limit decreased.
		 * If the limit did decrease, we may not be able to
		 * wake up the reader if we are currently above the limit.
		 */
		if (rt_rwlock_limit &&
		    unlikely(atomic_read(&rwm->owners) >= rt_rwlock_limit))
			goto out;
		rwm->owner = RT_RW_PENDING_READ;
	}

	wakeup_next_waiter(mutex, savestate);

 out:
	spin_unlock_irqrestore(&mutex->wait_lock, flags);

	/* Undo pi boosting.when necessary */
	rt_mutex_adjust_prio(current);
}

static inline void
rt_read_fastunlock(struct rw_mutex *rwm,
		   void fastcall (*slowfn)(struct rw_mutex *rwm, int mtx),
		   int mtx)
{
	WARN_ON(!atomic_read(&rwm->count));
	WARN_ON(!atomic_read(&rwm->owners));
	WARN_ON(!rwm->owner);
	atomic_dec(&rwm->count);
	if (likely(rt_rwlock_cmpxchg(rwm, current, NULL))) {
		struct reader_lock_struct *rls;
		int reader_count = --current->reader_lock_count;
		int owners;
		rt_mutex_deadlock_account_unlock(current);
		if (unlikely(reader_count < 0)) {
			    reader_count = 0;
			    WARN_ON_ONCE(1);
		}
		owners = atomic_dec_return(&rwm->owners);
		if (unlikely(owners < 0)) {
			atomic_set(&rwm->owners, 0);
			WARN_ON_ONCE(1);
		}
		rls = &current->owned_read_locks[reader_count];
		WARN_ON_ONCE(rls->lock != rwm);
		WARN_ON(rls->list.prev && !list_empty(&rls->list));
		rls->lock = NULL;
	} else
		slowfn(rwm, mtx);
}

void fastcall rt_mutex_up_read(struct rw_mutex *rwm)
{
	rt_read_fastunlock(rwm, rt_read_slowunlock, 1);
}

void fastcall rt_rwlock_read_unlock(struct rw_mutex *rwm)
{
	rt_read_fastunlock(rwm, rt_read_slowunlock, 0);
}

static void fastcall noinline __sched
rt_write_slowunlock(struct rw_mutex *rwm, int mtx)
{
	struct rt_mutex *mutex = &rwm->mutex;
	struct rt_mutex_waiter *waiter;
	struct task_struct *pendowner;
	int savestate = !mtx;
	unsigned long flags;

	spin_lock_irqsave(&mutex->wait_lock, flags);

	rt_mutex_deadlock_account_unlock(current);

	if (!rt_mutex_has_waiters(mutex)) {
		rwm->owner = NULL;
		mutex->owner = NULL;
		spin_unlock_irqrestore(&mutex->wait_lock, flags);
		return;
	}

	debug_rt_mutex_unlock(mutex);

	/*
	 * This is where it gets a bit tricky.
	 * We can have both readers and writers waiting below us.
	 * They are ordered by priority. For each reader we wake
	 * up, we check to see if there's another reader waiting.
	 * If that is the case, we continue to wake up the readers
	 * until we hit a writer. Once we hit a writer, then we
	 * stop (and don't wake it up).
	 *
	 * If the next waiter is a writer, than we just wake up
	 * the writer and we are done.
	 */

	waiter = rt_mutex_top_waiter(mutex);
	pendowner = waiter->task;
	wakeup_next_waiter(mutex, savestate);

	/* another writer is next? */
	if (waiter->write_lock) {
		rwm->owner = RT_RW_PENDING_WRITE;
		goto out;
	}

	rwm->owner = RT_RW_PENDING_READ;

	if (!rt_mutex_has_waiters(mutex))
		goto out;

	spin_lock(&pendowner->pi_lock);
	/*
	 * Wake up all readers.
	 * This gets a bit more complex. More than one reader can't
	 * own the mutex. We give it to the first (highest prio)
	 * reader, and then wake up the rest of the readers until
	 * we wake up all readers or come to a writer. The woken
	 * up readers that don't own the lock will try to take it
	 * when they schedule. Doing this lets a high prio writer
	 * come along and steal the lock.
	 */
	waiter = rt_mutex_top_waiter(mutex);
	while (waiter && !waiter->write_lock) {
		struct task_struct *reader = waiter->task;

		plist_del(&waiter->list_entry, &mutex->wait_list);

		/* nop if not on a list */
		plist_del(&waiter->pi_list_entry, &pendowner->pi_waiters);

		waiter->task = NULL;
		reader->pi_blocked_on = NULL;

		if (savestate)
			wake_up_process_mutex(reader);
		else
			wake_up_process(reader);

		if (rt_mutex_has_waiters(mutex))
			waiter = rt_mutex_top_waiter(mutex);
		else
			waiter = NULL;
	}

	/* If a writer is still pending, then update its plist. */
	if (rt_mutex_has_waiters(mutex)) {
		struct rt_mutex_waiter *next;

		next = rt_mutex_top_waiter(mutex);
		/* delete incase we didn't go through the loop */
		plist_del(&next->pi_list_entry, &pendowner->pi_waiters);
		/* add back in as top waiter */
		plist_add(&next->pi_list_entry, &pendowner->pi_waiters);
	}
	spin_unlock(&pendowner->pi_lock);

 out:

	spin_unlock_irqrestore(&mutex->wait_lock, flags);

	/* Undo pi boosting.when necessary */
	rt_mutex_adjust_prio(current);
}

static inline void
rt_write_fastunlock(struct rw_mutex *rwm,
		    void fastcall (*slowfn)(struct rw_mutex *rwm,
					    int mtx),
		    int mtx)
{
	unsigned long val = (unsigned long)current | RT_RWLOCK_WRITER;

	WARN_ON(rt_rwlock_owner(rwm) != current);
	if (likely(rt_rwlock_cmpxchg(rwm, (struct task_struct *)val, NULL)))
		rt_mutex_deadlock_account_unlock(current);
	else
		slowfn(rwm, mtx);
}

void fastcall rt_mutex_up_write(struct rw_mutex *rwm)
{
	rt_write_fastunlock(rwm, rt_write_slowunlock, 1);
}

void fastcall rt_rwlock_write_unlock(struct rw_mutex *rwm)
{
	rt_write_fastunlock(rwm, rt_write_slowunlock, 0);
}

void rt_mutex_rwsem_init(struct rw_mutex *rwm, const char *name)
{
	struct rt_mutex *mutex = &rwm->mutex;

	rwm->owner = NULL;
	atomic_set(&rwm->count, 0);
	atomic_set(&rwm->owners, 0);
	rwm->prio = MAX_PRIO;
	INIT_LIST_HEAD(&rwm->readers);

	__rt_mutex_init(mutex, name);
}

static int rt_mutex_get_readers_prio(struct task_struct *task, int prio)
{
	struct reader_lock_struct *rls;
	struct rw_mutex *rwm;
	int lock_prio;
	int i;

	for (i = 0; i < task->reader_lock_count; i++) {
		rls = &task->owned_read_locks[i];
		rwm = rls->lock;
		if (rwm) {
			lock_prio = rwm->prio;
			if (prio > lock_prio)
				prio = lock_prio;
		}
	}

	return prio;
}

static int rt_mutex_adjust_readers(struct rt_mutex *orig_lock,
				   struct rt_mutex_waiter *orig_waiter,
				   struct task_struct *top_task,
				   struct rt_mutex *lock,
				   int recursion_depth)
{
	struct reader_lock_struct *rls;
	struct rt_mutex_waiter *waiter;
	struct task_struct *task;
	struct rw_mutex *rwm = container_of(lock, struct rw_mutex, mutex);

	if (rt_mutex_has_waiters(lock)) {
		waiter = rt_mutex_top_waiter(lock);
		/*
		 * Do we need to grab the task->pi_lock?
		 * Really, we are only reading it. If it
		 * changes, then that should follow this chain
		 * too.
		 */
		rwm->prio = waiter->task->prio;
	} else
		rwm->prio = MAX_PRIO;

	if (recursion_depth >= MAX_RWLOCK_DEPTH) {
		WARN_ON(1);
		return 1;
	}

	list_for_each_entry(rls, &rwm->readers, list) {
		task = rls->task;
		get_task_struct(task);
		/*
		 * rt_mutex_adjust_prio_chain will do
		 * the put_task_struct
		 */
		rt_mutex_adjust_prio_chain(task, 0, orig_lock,
					   orig_waiter, top_task,
					   recursion_depth+1);
	}

	return 0;
}
#else
static int rt_mutex_adjust_readers(struct rt_mutex *orig_lock,
				   struct rt_mutex_waiter *orig_waiter,
				   struct task_struct *top_task,
				   struct rt_mutex *lock,
				   int recursion_depth)
{
	return 0;
}

static int rt_mutex_get_readers_prio(struct task_struct *task, int prio)
{
	return prio;
}
#endif /* CONFIG_PREEMPT_RT */

#ifdef CONFIG_PREEMPT_BKL

static inline int rt_release_bkl(struct rt_mutex *lock, unsigned long flags)
{
	int saved_lock_depth = current->lock_depth;

	current->lock_depth = -1;
	/*
	 * try_to_take_lock set the waiters, make sure it's
	 * still correct.
	 */
	fixup_rt_mutex_waiters(lock);
	spin_unlock_irqrestore(&lock->wait_lock, flags);

	up(&kernel_sem);

	spin_lock_irq(&lock->wait_lock);

	return saved_lock_depth;
}

static inline void rt_reacquire_bkl(int saved_lock_depth)
{
	down(&kernel_sem);
	current->lock_depth = saved_lock_depth;
}

#else
# define rt_release_bkl(lock, flags)	(-1)
# define rt_reacquire_bkl(depth)	do { } while (0)
#endif

/*
 * Slow path lock function:
 */
static int __sched
rt_mutex_slowlock(struct rt_mutex *lock, int state,
		  struct hrtimer_sleeper *timeout,
		  int detect_deadlock)
{
	int ret = 0, saved_lock_depth = -1;
	struct rt_mutex_waiter waiter;
	unsigned long flags;

	debug_rt_mutex_init_waiter(&waiter);
	waiter.task = NULL;
	waiter.write_lock = 0;

	spin_lock_irqsave(&lock->wait_lock, flags);
	init_lists(lock);

	/* Try to acquire the lock again: */
	if (try_to_take_rt_mutex(lock)) {
		spin_unlock_irqrestore(&lock->wait_lock, flags);
		return 0;
	}

	/*
	 * We drop the BKL here before we go into the wait loop to avoid a
	 * possible deadlock in the scheduler.
	 */
	if (unlikely(current->lock_depth >= 0))
		saved_lock_depth = rt_release_bkl(lock, flags);

	set_current_state(state);

	/* Setup the timer, when timeout != NULL */
	if (unlikely(timeout))
		hrtimer_start(&timeout->timer, timeout->timer.expires,
			      HRTIMER_MODE_ABS);

	for (;;) {
		unsigned long saved_flags;

		/* Try to acquire the lock: */
		if (try_to_take_rt_mutex(lock))
			break;

		/*
		 * TASK_INTERRUPTIBLE checks for signals and
		 * timeout. Ignored otherwise.
		 */
		if (unlikely(state == TASK_INTERRUPTIBLE)) {
			/* Signal pending? */
			if (signal_pending(current))
				ret = -EINTR;
			if (timeout && !timeout->task)
				ret = -ETIMEDOUT;
			if (ret)
				break;
		}

		/*
		 * waiter.task is NULL the first time we come here and
		 * when we have been woken up by the previous owner
		 * but the lock got stolen by a higher prio task.
		 */
		if (!waiter.task) {
			ret = task_blocks_on_rt_mutex(lock, &waiter,
						      detect_deadlock, flags);
			/*
			 * If we got woken up by the owner then start loop
			 * all over without going into schedule to try
			 * to get the lock now:
			 */
			if (unlikely(!waiter.task)) {
				/*
				 * Reset the return value. We might
				 * have returned with -EDEADLK and the
				 * owner released the lock while we
				 * were walking the pi chain.
				 */
				ret = 0;
				continue;
			}
			if (unlikely(ret))
				break;
		}
		saved_flags = current->flags & PF_NOSCHED;
		current->flags &= ~PF_NOSCHED;

		spin_unlock_irq(&lock->wait_lock);

		debug_rt_mutex_print_deadlock(&waiter);

		if (waiter.task)
			schedule_rt_mutex(lock);

		spin_lock_irq(&lock->wait_lock);

		current->flags |= saved_flags;
		set_current_state(state);
	}

	set_current_state(TASK_RUNNING);

	if (unlikely(waiter.task))
		remove_waiter(lock, &waiter, flags);

	/*
	 * try_to_take_rt_mutex() sets the waiter bit
	 * unconditionally. We might have to fix that up.
	 */
	fixup_rt_mutex_waiters(lock);

	spin_unlock_irqrestore(&lock->wait_lock, flags);

	/* Remove pending timer: */
	if (unlikely(timeout))
		hrtimer_cancel(&timeout->timer);

	/*
	 * Readjust priority, when we did not get the lock. We might
	 * have been the pending owner and boosted. Since we did not
	 * take the lock, the PI boost has to go.
	 */
	if (unlikely(ret))
		rt_mutex_adjust_prio(current);

	/* Must we reaquire the BKL? */
	if (unlikely(saved_lock_depth >= 0))
		rt_reacquire_bkl(saved_lock_depth);

	debug_rt_mutex_free_waiter(&waiter);

	return ret;
}

/*
 * Slow path try-lock function:
 */
static inline int
rt_mutex_slowtrylock(struct rt_mutex *lock)
{
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&lock->wait_lock, flags);

	if (likely(rt_mutex_owner(lock) != current)) {

		init_lists(lock);

		ret = try_to_take_rt_mutex(lock);
		/*
		 * try_to_take_rt_mutex() sets the lock waiters
		 * bit unconditionally. Clean this up.
		 */
		fixup_rt_mutex_waiters(lock);
	}

	spin_unlock_irqrestore(&lock->wait_lock, flags);

	return ret;
}

/*
 * Slow path to release a rt-mutex:
 */
static void __sched
rt_mutex_slowunlock(struct rt_mutex *lock)
{
	unsigned long flags;

	spin_lock_irqsave(&lock->wait_lock, flags);

	debug_rt_mutex_unlock(lock);

	rt_mutex_deadlock_account_unlock(current);

	if (!rt_mutex_has_waiters(lock)) {
		lock->owner = NULL;
		spin_unlock_irqrestore(&lock->wait_lock, flags);
		return;
	}

	wakeup_next_waiter(lock, 0);

	spin_unlock_irqrestore(&lock->wait_lock, flags);

	/* Undo pi boosting if necessary: */
	rt_mutex_adjust_prio(current);
}

/*
 * debug aware fast / slowpath lock,trylock,unlock
 *
 * The atomic acquire/release ops are compiled away, when either the
 * architecture does not support cmpxchg or when debugging is enabled.
 */
static inline int
rt_mutex_fastlock(struct rt_mutex *lock, int state,
		  int detect_deadlock,
		  int (*slowfn)(struct rt_mutex *lock, int state,
				struct hrtimer_sleeper *timeout,
				int detect_deadlock))
{
	if (!detect_deadlock && likely(rt_mutex_cmpxchg(lock, NULL, current))) {
		rt_mutex_deadlock_account_lock(lock, current);
		return 0;
	} else
		return slowfn(lock, state, NULL, detect_deadlock);
}

static inline int
rt_mutex_timed_fastlock(struct rt_mutex *lock, int state,
			struct hrtimer_sleeper *timeout, int detect_deadlock,
			int (*slowfn)(struct rt_mutex *lock, int state,
				      struct hrtimer_sleeper *timeout,
				      int detect_deadlock))
{
	if (!detect_deadlock && likely(rt_mutex_cmpxchg(lock, NULL, current))) {
		rt_mutex_deadlock_account_lock(lock, current);
		return 0;
	} else
		return slowfn(lock, state, timeout, detect_deadlock);
}

static inline int
rt_mutex_fasttrylock(struct rt_mutex *lock,
		     int (*slowfn)(struct rt_mutex *lock))
{
	if (likely(rt_mutex_cmpxchg(lock, NULL, current))) {
		rt_mutex_deadlock_account_lock(lock, current);
		return 1;
	}
	return slowfn(lock);
}

static inline void
rt_mutex_fastunlock(struct rt_mutex *lock,
		    void (*slowfn)(struct rt_mutex *lock))
{
	if (likely(rt_mutex_cmpxchg(lock, current, NULL)))
		rt_mutex_deadlock_account_unlock(current);
	else
		slowfn(lock);
}

/**
 * rt_mutex_lock - lock a rt_mutex
 *
 * @lock: the rt_mutex to be locked
 */
void __sched rt_mutex_lock(struct rt_mutex *lock)
{
	might_sleep();

	rt_mutex_fastlock(lock, TASK_UNINTERRUPTIBLE, 0, rt_mutex_slowlock);
}
EXPORT_SYMBOL_GPL(rt_mutex_lock);

/**
 * rt_mutex_lock_interruptible - lock a rt_mutex interruptible
 *
 * @lock: 		the rt_mutex to be locked
 * @detect_deadlock:	deadlock detection on/off
 *
 * Returns:
 *  0 		on success
 * -EINTR 	when interrupted by a signal
 * -EDEADLK	when the lock would deadlock (when deadlock detection is on)
 */
int __sched rt_mutex_lock_interruptible(struct rt_mutex *lock,
						 int detect_deadlock)
{
	might_sleep();

	return rt_mutex_fastlock(lock, TASK_INTERRUPTIBLE,
				 detect_deadlock, rt_mutex_slowlock);
}
EXPORT_SYMBOL_GPL(rt_mutex_lock_interruptible);

/**
 * rt_mutex_lock_interruptible_ktime - lock a rt_mutex interruptible
 *				       the timeout structure is provided
 *				       by the caller
 *
 * @lock: 		the rt_mutex to be locked
 * @timeout:		timeout structure or NULL (no timeout)
 * @detect_deadlock:	deadlock detection on/off
 *
 * Returns:
 *  0 		on success
 * -EINTR 	when interrupted by a signal
 * -ETIMEOUT	when the timeout expired
 * -EDEADLK	when the lock would deadlock (when deadlock detection is on)
 */
int
rt_mutex_timed_lock(struct rt_mutex *lock, struct hrtimer_sleeper *timeout,
		    int detect_deadlock)
{
	might_sleep();

	return rt_mutex_timed_fastlock(lock, TASK_INTERRUPTIBLE, timeout,
				       detect_deadlock, rt_mutex_slowlock);
}
EXPORT_SYMBOL_GPL(rt_mutex_timed_lock);

/**
 * rt_mutex_trylock - try to lock a rt_mutex
 *
 * @lock:	the rt_mutex to be locked
 *
 * Returns 1 on success and 0 on contention
 */
int __sched rt_mutex_trylock(struct rt_mutex *lock)
{
	return rt_mutex_fasttrylock(lock, rt_mutex_slowtrylock);
}
EXPORT_SYMBOL_GPL(rt_mutex_trylock);

/**
 * rt_mutex_unlock - unlock a rt_mutex
 *
 * @lock: the rt_mutex to be unlocked
 */
void __sched rt_mutex_unlock(struct rt_mutex *lock)
{
	rt_mutex_fastunlock(lock, rt_mutex_slowunlock);
}
EXPORT_SYMBOL_GPL(rt_mutex_unlock);

/***
 * rt_mutex_destroy - mark a mutex unusable
 * @lock: the mutex to be destroyed
 *
 * This function marks the mutex uninitialized, and any subsequent
 * use of the mutex is forbidden. The mutex must not be locked when
 * this function is called.
 */
void rt_mutex_destroy(struct rt_mutex *lock)
{
	WARN_ON(rt_mutex_is_locked(lock));
#ifdef CONFIG_DEBUG_RT_MUTEXES
	lock->magic = NULL;
#endif
}

EXPORT_SYMBOL_GPL(rt_mutex_destroy);

/**
 * __rt_mutex_init - initialize the rt lock
 *
 * @lock: the rt lock to be initialized
 *
 * Initialize the rt lock to unlocked state.
 *
 * Initializing of a locked rt lock is not allowed
 */
void __rt_mutex_init(struct rt_mutex *lock, const char *name)
{
	lock->owner = NULL;
	spin_lock_init(&lock->wait_lock);
	plist_head_init(&lock->wait_list, &lock->wait_lock);

	debug_rt_mutex_init(lock, name);
}
EXPORT_SYMBOL_GPL(__rt_mutex_init);

/**
 * rt_mutex_init_proxy_locked - initialize and lock a rt_mutex on behalf of a
 *				proxy owner
 *
 * @lock: 	the rt_mutex to be locked
 * @proxy_owner:the task to set as owner
 *
 * No locking. Caller has to do serializing itself
 * Special API call for PI-futex support
 */
void rt_mutex_init_proxy_locked(struct rt_mutex *lock,
				struct task_struct *proxy_owner)
{
	__rt_mutex_init(lock, NULL);
	debug_rt_mutex_proxy_lock(lock, proxy_owner);
	rt_mutex_set_owner(lock, proxy_owner, 0);
	rt_mutex_deadlock_account_lock(lock, proxy_owner);
}

/**
 * rt_mutex_proxy_unlock - release a lock on behalf of owner
 *
 * @lock: 	the rt_mutex to be locked
 *
 * No locking. Caller has to do serializing itself
 * Special API call for PI-futex support
 */
void rt_mutex_proxy_unlock(struct rt_mutex *lock,
			   struct task_struct *proxy_owner)
{
	debug_rt_mutex_proxy_unlock(lock);
	rt_mutex_set_owner(lock, NULL, 0);
	rt_mutex_deadlock_account_unlock(proxy_owner);
}

/**
 * rt_mutex_next_owner - return the next owner of the lock
 *
 * @lock: the rt lock query
 *
 * Returns the next owner of the lock or NULL
 *
 * Caller has to serialize against other accessors to the lock
 * itself.
 *
 * Special API call for PI-futex support
 */
struct task_struct *rt_mutex_next_owner(struct rt_mutex *lock)
{
	if (!rt_mutex_has_waiters(lock))
		return NULL;

	return rt_mutex_top_waiter(lock)->task;
}
