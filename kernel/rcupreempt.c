/*
 * Read-Copy Update mechanism for mutual exclusion, realtime implementation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright IBM Corporation, 2006
 *
 * Authors: Paul E. McKenney <paulmck@us.ibm.com>
 *		With thanks to Esben Nielsen, Bill Huey, and Ingo Molnar
 *		for pushing me away from locks and towards counters, and
 *		to Suparna Bhattacharya for pushing me completely away
 *		from atomic instructions on the read side.
 *
 * Papers:  http://www.rdrop.com/users/paulmck/RCU
 *
 * For detailed explanation of Read-Copy Update mechanism see -
 * 		Documentation/RCU/ *.txt
 *
 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/smp.h>
#include <linux/rcupdate.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <asm/atomic.h>
#include <linux/bitops.h>
#include <linux/module.h>
#include <linux/completion.h>
#include <linux/moduleparam.h>
#include <linux/percpu.h>
#include <linux/notifier.h>
#include <linux/rcupdate.h>
#include <linux/cpu.h>
#include <linux/random.h>
#include <linux/delay.h>
#include <linux/byteorder/swabb.h>
#include <linux/cpumask.h>
#include <linux/rcupreempt_trace.h>

/*
 * PREEMPT_RCU data structures.
 */

#define GP_STAGES 2
struct rcu_data {
	spinlock_t	lock;		/* Protect rcu_data fields. */
	long		completed;	/* Number of last completed batch. */
	int		waitlistcount;
	struct rcu_head *nextlist;
	struct rcu_head **nexttail;
	struct rcu_head *waitlist[GP_STAGES];
	struct rcu_head **waittail[GP_STAGES];
	struct rcu_head *donelist;
	struct rcu_head **donetail;
#ifdef CONFIG_RCU_TRACE
	struct rcupreempt_trace trace;
#endif /* #ifdef CONFIG_RCU_TRACE */
};
struct rcu_ctrlblk {
	spinlock_t	fliplock;	/* Protect state-machine transitions. */
	long		completed;	/* Number of last completed batch. */
};
static DEFINE_PER_CPU(struct rcu_data, rcu_data);
static struct rcu_ctrlblk rcu_ctrlblk = {
	.fliplock = SPIN_LOCK_UNLOCKED,
	.completed = 0,
};
static DEFINE_PER_CPU(int [2], rcu_flipctr) = { 0, 0 };

/*
 * States for rcu_try_flip() and friends.
 */

enum rcu_try_flip_states {
	rcu_try_flip_idle_state,	/* "I" */
	rcu_try_flip_waitack_state, 	/* "A" */
	rcu_try_flip_waitzero_state,	/* "Z" */
	rcu_try_flip_waitmb_state	/* "M" */
};
static enum rcu_try_flip_states rcu_try_flip_state = rcu_try_flip_idle_state;
#ifdef CONFIG_RCU_TRACE
static char *rcu_try_flip_state_names[] =
	{ "idle", "waitack", "waitzero", "waitmb" };
#endif /* #ifdef CONFIG_RCU_TRACE */

/*
 * Enum and per-CPU flag to determine when each CPU has seen
 * the most recent counter flip.
 */

enum rcu_flip_flag_values {
	rcu_flip_seen,		/* Steady/initial state, last flip seen. */
				/* Only GP detector can update. */
	rcu_flipped		/* Flip just completed, need confirmation. */
				/* Only corresponding CPU can update. */
};
static DEFINE_PER_CPU(enum rcu_flip_flag_values, rcu_flip_flag) = rcu_flip_seen;

/*
 * Enum and per-CPU flag to determine when each CPU has executed the
 * needed memory barrier to fence in memory references from its last RCU
 * read-side critical section in the just-completed grace period.
 */

enum rcu_mb_flag_values {
	rcu_mb_done,		/* Steady/initial state, no mb()s required. */
				/* Only GP detector can update. */
	rcu_mb_needed		/* Flip just completed, need an mb(). */
				/* Only corresponding CPU can update. */
};
static DEFINE_PER_CPU(enum rcu_mb_flag_values, rcu_mb_flag) = rcu_mb_done;

static cpumask_t rcu_cpu_online_map = CPU_MASK_NONE;

/*
 * Macro that prevents the compiler from reordering accesses, but does
 * absolutely -nothing- to prevent CPUs from reordering.  This is used
 * only to mediate communication between mainline code and hardware
 * interrupt and NMI handlers.
 */
#define ORDERED_WRT_IRQ(x) (*(volatile typeof(x) *)&(x))

/*
 * RCU_DATA_ME: find the current CPU's rcu_data structure.
 * RCU_DATA_CPU: find the specified CPU's rcu_data structure.
 */
#define RCU_DATA_ME()		(&__get_cpu_var(rcu_data))
#define RCU_DATA_CPU(cpu)	(&per_cpu(rcu_data, cpu))

/*
 * Helper macro for tracing when the appropriate rcu_data is not
 * cached in a local variable, but where the CPU number is so cached.
 */
#define RCU_TRACE_CPU(f, cpu) RCU_TRACE(f, &(RCU_DATA_CPU(cpu)->trace));

/*
 * Helper macro for tracing when the appropriate rcu_data is not
 * cached in a local variable.
 */
#define RCU_TRACE_ME(f) RCU_TRACE(f, &(RCU_DATA_ME()->trace));

/*
 * Helper macro for tracing when the appropriate rcu_data is pointed
 * to by a local variable.
 */
#define RCU_TRACE_RDP(f, rdp) RCU_TRACE(f, &((rdp)->trace));

/*
 * Return the number of RCU batches processed thus far.  Useful
 * for debug and statistics.
 */
long rcu_batches_completed(void)
{
	return rcu_ctrlblk.completed;
}
EXPORT_SYMBOL_GPL(rcu_batches_completed);

/*
 * Return the number of RCU batches processed thus far.  Useful for debug
 * and statistics.  The _bh variant is identical to straight RCU.
 */
long rcu_batches_completed_bh(void)
{
	return rcu_ctrlblk.completed;
}
EXPORT_SYMBOL_GPL(rcu_batches_completed_bh);

void __rcu_read_lock(void)
{
	int idx;
	struct task_struct *me = current;
	int nesting;

	nesting = ORDERED_WRT_IRQ(me->rcu_read_lock_nesting);
	if (nesting != 0) {

		/* An earlier rcu_read_lock() covers us, just count it. */

		me->rcu_read_lock_nesting = nesting + 1;

	} else {
		unsigned long oldirq;

		/*
		 * Disable local interrupts to prevent the grace-period
		 * detection state machine from seeing us half-done.
		 * NMIs can still occur, of course, and might themselves
		 * contain rcu_read_lock().
		 */

		local_irq_save(oldirq);

		/*
		 * Outermost nesting of rcu_read_lock(), so increment
		 * the current counter for the current CPU.  Use volatile
		 * casts to prevent the compiler from reordering.
		 */

		idx = ORDERED_WRT_IRQ(rcu_ctrlblk.completed) & 0x1;
		smp_read_barrier_depends();  /* @@@@ might be unneeded */
		ORDERED_WRT_IRQ(__get_cpu_var(rcu_flipctr)[idx])++;

		/*
		 * Now that the per-CPU counter has been incremented, we
		 * are protected from races with rcu_read_lock() invoked
		 * from NMI handlers on this CPU.  We can therefore safely
		 * increment the nesting counter, relieving further NMIs
		 * of the need to increment the per-CPU counter.
		 */

		ORDERED_WRT_IRQ(me->rcu_read_lock_nesting) = nesting + 1;

		/*
		 * Now that we have preventing any NMIs from storing
		 * to the ->rcu_flipctr_idx, we can safely use it to
		 * remember which counter to decrement in the matching
		 * rcu_read_unlock().
		 */

		ORDERED_WRT_IRQ(me->rcu_flipctr_idx) = idx;
		local_irq_restore(oldirq);
	}
}
EXPORT_SYMBOL_GPL(__rcu_read_lock);

void __rcu_read_unlock(void)
{
	int idx;
	struct task_struct *me = current;
	int nesting;

	nesting = ORDERED_WRT_IRQ(me->rcu_read_lock_nesting);
	if (nesting > 1) {

		/*
		 * We are still protected by the enclosing rcu_read_lock(),
		 * so simply decrement the counter.
		 */

		me->rcu_read_lock_nesting = nesting - 1;

	} else {
		unsigned long oldirq;

		/*
		 * Disable local interrupts to prevent the grace-period
		 * detection state machine from seeing us half-done.
		 * NMIs can still occur, of course, and might themselves
		 * contain rcu_read_lock() and rcu_read_unlock().
		 */

		local_irq_save(oldirq);

		/*
		 * Outermost nesting of rcu_read_unlock(), so we must
		 * decrement the current counter for the current CPU.
		 * This must be done carefully, because NMIs can
		 * occur at any point in this code, and any rcu_read_lock()
		 * and rcu_read_unlock() pairs in the NMI handlers
		 * must interact non-destructively with this code.
		 * Lots of volatile casts, and -very- careful ordering.
		 *
		 * Changes to this code, including this one, must be
		 * inspected, validated, and tested extremely carefully!!!
		 */

		/*
		 * First, pick up the index.  Enforce ordering for
		 * DEC Alpha.
		 */

		idx = ORDERED_WRT_IRQ(me->rcu_flipctr_idx);
		smp_read_barrier_depends();  /* @@@ Needed??? */

		/*
		 * Now that we have fetched the counter index, it is
		 * safe to decrement the per-task RCU nesting counter.
		 * After this, any interrupts or NMIs will increment and
		 * decrement the per-CPU counters.
		 */
		ORDERED_WRT_IRQ(me->rcu_read_lock_nesting) = nesting - 1;

		/*
		 * It is now safe to decrement this task's nesting count.
		 * NMIs that occur after this statement will route their
		 * rcu_read_lock() calls through this "else" clause, and
		 * will thus start incrementing the per-CPU coutner on
		 * their own.  They will also clobber ->rcu_flipctr_idx,
		 * but that is OK, since we have already fetched it.
		 */

		ORDERED_WRT_IRQ(__get_cpu_var(rcu_flipctr)[idx])--;
		local_irq_restore(oldirq);
	}
}
EXPORT_SYMBOL_GPL(__rcu_read_unlock);

/*
 * If a global counter flip has occurred since the last time that we
 * advanced callbacks, advance them.  Hardware interrupts must be
 * disabled when calling this function.
 */
static void __rcu_advance_callbacks(struct rcu_data *rdp)
{
	int cpu;
	int i;
	int wlc = 0;

	if (rdp->completed != rcu_ctrlblk.completed) {
		if (rdp->waitlist[GP_STAGES - 1] != NULL) {
			*rdp->donetail = rdp->waitlist[GP_STAGES - 1];
			rdp->donetail = rdp->waittail[GP_STAGES - 1];
			RCU_TRACE_RDP(rcupreempt_trace_move2done, rdp);
		}
		for (i = GP_STAGES - 2; i >= 0; i--) {
			if (rdp->waitlist[i] != NULL) {
				rdp->waitlist[i + 1] = rdp->waitlist[i];
				rdp->waittail[i + 1] = rdp->waittail[i];
				wlc++;
			} else {
				rdp->waitlist[i + 1] = NULL;
				rdp->waittail[i + 1] =
					&rdp->waitlist[i + 1];
			}
		}
		if (rdp->nextlist != NULL) {
			rdp->waitlist[0] = rdp->nextlist;
			rdp->waittail[0] = rdp->nexttail;
			wlc++;
			rdp->nextlist = NULL;
			rdp->nexttail = &rdp->nextlist;
			RCU_TRACE_RDP(rcupreempt_trace_move2wait, rdp);
		} else {
			rdp->waitlist[0] = NULL;
			rdp->waittail[0] = &rdp->waitlist[0];
		}
		rdp->waitlistcount = wlc;
		rdp->completed = rcu_ctrlblk.completed;
	}

	/*
	 * Check to see if this CPU needs to report that it has seen
	 * the most recent counter flip, thereby declaring that all
	 * subsequent rcu_read_lock() invocations will respect this flip.
	 */

	cpu = raw_smp_processor_id();
	if (per_cpu(rcu_flip_flag, cpu) == rcu_flipped) {
		smp_mb();  /* Subsequent counter accesses must see new value */
		per_cpu(rcu_flip_flag, cpu) = rcu_flip_seen;
		smp_mb();  /* Subsequent RCU read-side critical sections */
			   /*  seen -after- acknowledgement. */
	}
}

/*
 * Get here when RCU is idle.  Decide whether we need to
 * move out of idle state, and return non-zero if so.
 * "Straightforward" approach for the moment, might later
 * use callback-list lengths, grace-period duration, or
 * some such to determine when to exit idle state.
 * Might also need a pre-idle test that does not acquire
 * the lock, but let's get the simple case working first...
 */

static int
rcu_try_flip_idle(void)
{
	int cpu;

	RCU_TRACE_ME(rcupreempt_trace_try_flip_i1);
	if (!rcu_pending(smp_processor_id())) {
		RCU_TRACE_ME(rcupreempt_trace_try_flip_ie1);
		return 0;
	}

	/*
	 * Do the flip.
	 */

	RCU_TRACE_ME(rcupreempt_trace_try_flip_g1);
	rcu_ctrlblk.completed++;  /* stands in for rcu_try_flip_g2 */

	/*
	 * Need a memory barrier so that other CPUs see the new
	 * counter value before they see the subsequent change of all
	 * the rcu_flip_flag instances to rcu_flipped.
	 */

	smp_mb();	/* see above block comment. */

	/* Now ask each CPU for acknowledgement of the flip. */

	for_each_cpu_mask(cpu, rcu_cpu_online_map)
		per_cpu(rcu_flip_flag, cpu) = rcu_flipped;

	return 1;
}

/*
 * Wait for CPUs to acknowledge the flip.
 */

static int
rcu_try_flip_waitack(void)
{
	int cpu;

	RCU_TRACE_ME(rcupreempt_trace_try_flip_a1);
	for_each_cpu_mask(cpu, rcu_cpu_online_map)
		if (per_cpu(rcu_flip_flag, cpu) != rcu_flip_seen) {
			RCU_TRACE_ME(rcupreempt_trace_try_flip_ae1);
			return 0;
		}

	/*
	 * Make sure our checks above don't bleed into subsequent
	 * waiting for the sum of the counters to reach zero.
	 */

	smp_mb();	/* see above block comment. */
	RCU_TRACE_ME(rcupreempt_trace_try_flip_a2);
	return 1;
}

/*
 * Wait for collective ``last'' counter to reach zero,
 * then tell all CPUs to do an end-of-grace-period memory barrier.
 */

static int
rcu_try_flip_waitzero(void)
{
	int cpu;
	int lastidx = !(rcu_ctrlblk.completed & 0x1);
	int sum = 0;

	/* Check to see if the sum of the "last" counters is zero. */

	RCU_TRACE_ME(rcupreempt_trace_try_flip_z1);
	for_each_possible_cpu(cpu)
		sum += per_cpu(rcu_flipctr, cpu)[lastidx];
	if (sum != 0) {
		RCU_TRACE_ME(rcupreempt_trace_try_flip_ze1);
		return 0;
	}

	smp_mb();  /* Don't call for memory barriers before we see zero. */

	/* Call for a memory barrier from each CPU. */

	for_each_cpu_mask(cpu, rcu_cpu_online_map)
		per_cpu(rcu_mb_flag, cpu) = rcu_mb_needed;

	RCU_TRACE_ME(rcupreempt_trace_try_flip_z2);
	return 1;
}

/*
 * Wait for all CPUs to do their end-of-grace-period memory barrier.
 * Return 0 once all CPUs have done so.
 */

static int
rcu_try_flip_waitmb(void)
{
	int cpu;

	RCU_TRACE_ME(rcupreempt_trace_try_flip_m1);
	for_each_cpu_mask(cpu, rcu_cpu_online_map)
		if (per_cpu(rcu_mb_flag, cpu) != rcu_mb_done) {
			RCU_TRACE_ME(rcupreempt_trace_try_flip_me1);
			return 0;
		}

	smp_mb(); /* Ensure that the above checks precede any following flip. */
	RCU_TRACE_ME(rcupreempt_trace_try_flip_m2);
	return 1;
}

/*
 * Attempt a single flip of the counters.  Remember, a single flip does
 * -not- constitute a grace period.  Instead, the interval between
 * at least three consecutive flips is a grace period.
 *
 * If anyone is nuts enough to run this CONFIG_PREEMPT_RCU implementation
 * on a large SMP, they might want to use a hierarchical organization of
 * the per-CPU-counter pairs.
 */
static void rcu_try_flip(void)
{
	unsigned long oldirq;

	RCU_TRACE_ME(rcupreempt_trace_try_flip_1);
	if (unlikely(!spin_trylock_irqsave(&rcu_ctrlblk.fliplock, oldirq))) {
		RCU_TRACE_ME(rcupreempt_trace_try_flip_e1);
		return;
	}

	/*
	 * Take the next transition(s) through the RCU grace-period
	 * flip-counter state machine.
	 */

	switch (rcu_try_flip_state) {
	case rcu_try_flip_idle_state:
		if (rcu_try_flip_idle())
			rcu_try_flip_state = rcu_try_flip_waitack_state;
		break;
	case rcu_try_flip_waitack_state:
		if (rcu_try_flip_waitack())
			rcu_try_flip_state = rcu_try_flip_waitzero_state;
		break;
	case rcu_try_flip_waitzero_state:
		if (rcu_try_flip_waitzero())
			rcu_try_flip_state = rcu_try_flip_waitmb_state;
		break;
	case rcu_try_flip_waitmb_state:
		if (rcu_try_flip_waitmb())
			rcu_try_flip_state = rcu_try_flip_idle_state;
	}
	spin_unlock_irqrestore(&rcu_ctrlblk.fliplock, oldirq);
}

/*
 * Check to see if this CPU needs to do a memory barrier in order to
 * ensure that any prior RCU read-side critical sections have committed
 * their counter manipulations and critical-section memory references
 * before declaring the grace period to be completed.
 */
static void rcu_check_mb(int cpu)
{
	if (per_cpu(rcu_mb_flag, cpu) == rcu_mb_needed) {
		smp_mb();  /* Ensure RCU read-side accesses are visible. */
		per_cpu(rcu_mb_flag, cpu) = rcu_mb_done;
	}
}

void rcu_check_callbacks_rt(int cpu, int user)
{
	unsigned long oldirq;
	struct rcu_data *rdp = RCU_DATA_CPU(cpu);

	rcu_check_mb(cpu);
	if (rcu_ctrlblk.completed == rdp->completed)
		rcu_try_flip();
	spin_lock_irqsave(&rdp->lock, oldirq);
	RCU_TRACE_RDP(rcupreempt_trace_check_callbacks, rdp);
	__rcu_advance_callbacks(rdp);
	spin_unlock_irqrestore(&rdp->lock, oldirq);
}

/*
 * Needed by dynticks, to make sure all RCU processing has finished
 * when we go idle.  (Currently unused, needed?)
 */
void rcu_advance_callbacks_rt(int cpu, int user)
{
	unsigned long oldirq;
	struct rcu_data *rdp = RCU_DATA_CPU(cpu);

	if (rcu_ctrlblk.completed == rdp->completed) {
		rcu_try_flip();
		if (rcu_ctrlblk.completed == rdp->completed)
			return;
	}
	spin_lock_irqsave(&rdp->lock, oldirq);
	RCU_TRACE_RDP(rcupreempt_trace_check_callbacks, rdp);
	__rcu_advance_callbacks(rdp);
	spin_unlock_irqrestore(&rdp->lock, oldirq);
}

#ifdef CONFIG_HOTPLUG_CPU

#define rcu_offline_cpu_rt_enqueue(srclist, srctail, dstlist, dsttail) do { \
		*dsttail = srclist; \
		if (srclist != NULL) { \
			dsttail = srctail; \
			srclist = NULL; \
			srctail = &srclist;\
		} \
	} while (0)


void rcu_offline_cpu_rt(int cpu)
{
	int i;
	struct rcu_head *list = NULL;
	unsigned long oldirq;
	struct rcu_data *rdp = RCU_DATA_CPU(cpu);
	struct rcu_head **tail = &list;

	/* Remove all callbacks from the newly dead CPU, retaining order. */

	spin_lock_irqsave(&rdp->lock, oldirq);
	rcu_offline_cpu_rt_enqueue(rdp->donelist, rdp->donetail, list, tail);
	for (i = GP_STAGES - 1; i >= 0; i--)
		rcu_offline_cpu_rt_enqueue(rdp->waitlist[i], rdp->waittail[i],
					   list, tail);
	rcu_offline_cpu_rt_enqueue(rdp->nextlist, rdp->nexttail, list, tail);
	spin_unlock_irqrestore(&rdp->lock, oldirq);
	rdp->waitlistcount = 0;

	/* Disengage the newly dead CPU from grace-period computation. */

	spin_lock_irqsave(&rcu_ctrlblk.fliplock, oldirq);
	rcu_check_mb(cpu);
	if (per_cpu(rcu_flip_flag, cpu) == rcu_flipped) {
		smp_mb();  /* Subsequent counter accesses must see new value */
		per_cpu(rcu_flip_flag, cpu) = rcu_flip_seen;
		smp_mb();  /* Subsequent RCU read-side critical sections */
			   /*  seen -after- acknowledgement. */
	}
	cpu_clear(cpu, rcu_cpu_online_map);
	spin_unlock_irqrestore(&rcu_ctrlblk.fliplock, oldirq);

	/*
	 * Place the removed callbacks on the current CPU's queue.
	 * Make them all start a new grace period: simple approach,
	 * in theory could starve a given set of callbacks, but
	 * you would need to be doing some serious CPU hotplugging
	 * to make this happen.  If this becomes a problem, adding
	 * a synchronize_rcu() to the hotplug path would be a simple
	 * fix.
	 */

	rdp = RCU_DATA_ME();
	spin_lock_irqsave(&rdp->lock, oldirq);
	*rdp->nexttail = list;
	if (list)
		rdp->nexttail = tail;
	spin_unlock_irqrestore(&rdp->lock, oldirq);
}

void __devinit rcu_online_cpu_rt(int cpu)
{
	unsigned long oldirq;

	spin_lock_irqsave(&rcu_ctrlblk.fliplock, oldirq);
	cpu_set(cpu, rcu_cpu_online_map);
	spin_unlock_irqrestore(&rcu_ctrlblk.fliplock, oldirq);
}

#else /* #ifdef CONFIG_HOTPLUG_CPU */

void rcu_offline_cpu(int cpu)
{
}

void __devinit rcu_online_cpu_rt(int cpu)
{
}

#endif /* #else #ifdef CONFIG_HOTPLUG_CPU */

void rcu_process_callbacks_rt(struct softirq_action *unused)
{
	unsigned long flags;
	struct rcu_head *next, *list;
	struct rcu_data *rdp = RCU_DATA_ME();

	spin_lock_irqsave(&rdp->lock, flags);
	list = rdp->donelist;
	if (list == NULL) {
		spin_unlock_irqrestore(&rdp->lock, flags);
		return;
	}
	rdp->donelist = NULL;
	rdp->donetail = &rdp->donelist;
	RCU_TRACE_RDP(rcupreempt_trace_done_remove, rdp);
	spin_unlock_irqrestore(&rdp->lock, flags);
	while (list) {
		next = list->next;
		list->func(list);
		list = next;
		RCU_TRACE_ME(rcupreempt_trace_invoke);
	}
}

void fastcall call_rcu_preempt(struct rcu_head *head,
			       void (*func)(struct rcu_head *rcu))
{
	unsigned long oldirq;
	struct rcu_data *rdp;

	head->func = func;
	head->next = NULL;
	local_irq_save(oldirq);
	rdp = RCU_DATA_ME();
	spin_lock(&rdp->lock);
	__rcu_advance_callbacks(rdp);
	*rdp->nexttail = head;
	rdp->nexttail = &head->next;
	RCU_TRACE_RDP(rcupreempt_trace_next_add, rdp);
	spin_unlock(&rdp->lock);
	local_irq_restore(oldirq);
}
EXPORT_SYMBOL_GPL(call_rcu_preempt);

/*
 * Check to see if any future RCU-related work will need to be done
 * by the current CPU, even if none need be done immediately, returning
 * 1 if so.  Assumes that notifiers would take care of handling any
 * outstanding requests from the RCU core.
 *
 * This function is part of the RCU implementation; it is -not-
 * an exported member of the RCU API.
 */
int rcu_needs_cpu_rt(int cpu)
{
	struct rcu_data *rdp = RCU_DATA_CPU(cpu);

	return (rdp->donelist != NULL ||
		!!rdp->waitlistcount ||
		rdp->nextlist != NULL);
}

int rcu_pending_rt(int cpu)
{
	struct rcu_data *rdp = RCU_DATA_CPU(cpu);

	/* The CPU has at least one callback queued somewhere. */

	if (rdp->donelist != NULL ||
	    !!rdp->waitlistcount ||
	    rdp->nextlist != NULL)
		return 1;

	/* The RCU core needs an acknowledgement from this CPU. */

	if ((per_cpu(rcu_flip_flag, cpu) == rcu_flipped) ||
	    (per_cpu(rcu_mb_flag, cpu) == rcu_mb_needed))
		return 1;

	/* This CPU has fallen behind the global grace-period number. */

	if (rdp->completed != rcu_ctrlblk.completed)
		return 1;

	/* Nothing needed from this CPU. */

	return 0;
}

void __init rcu_init_rt(void)
{
	int cpu;
	int i;
	struct rcu_data *rdp;

	for_each_possible_cpu(cpu) {
		rdp = RCU_DATA_CPU(cpu);
		spin_lock_init(&rdp->lock);
		rdp->completed = 0;
		rdp->waitlistcount = 0;
		rdp->nextlist = NULL;
		rdp->nexttail = &rdp->nextlist;
		for (i = 0; i < GP_STAGES; i++) {
			rdp->waitlist[i] = NULL;
			rdp->waittail[i] = &rdp->waitlist[i];
		}
		rdp->donelist = NULL;
		rdp->donetail = &rdp->donelist;
	}
}

/*
 * Deprecated, use synchronize_rcu() or synchronize_sched() instead.
 */
void synchronize_kernel(void)
{
	synchronize_rcu();
}

#ifdef CONFIG_RCU_TRACE
int *rcupreempt_flipctr(int cpu)
{
	return &per_cpu(rcu_flipctr, cpu)[0];
}
EXPORT_SYMBOL_GPL(rcupreempt_flipctr);

int rcupreempt_flip_flag(int cpu)
{
	return per_cpu(rcu_flip_flag, cpu);
}
EXPORT_SYMBOL_GPL(rcupreempt_flip_flag);

int rcupreempt_mb_flag(int cpu)
{
	return per_cpu(rcu_mb_flag, cpu);
}
EXPORT_SYMBOL_GPL(rcupreempt_mb_flag);

char *rcupreempt_try_flip_state_name(void)
{
	return rcu_try_flip_state_names[rcu_try_flip_state];
}
EXPORT_SYMBOL_GPL(rcupreempt_try_flip_state_name);

struct rcupreempt_trace *rcupreempt_trace_cpu(int cpu)
{
	struct rcu_data *rdp = RCU_DATA_CPU(cpu);

	return &rdp->trace;
}
EXPORT_SYMBOL_GPL(rcupreempt_trace_cpu);

#endif /* #ifdef RCU_TRACE */
