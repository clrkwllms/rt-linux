/*
 * Read-Copy Update preempt priority boosting
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
 * Copyright Red Hat Inc, 2007
 *
 * Authors: Steven Rostedt <srostedt@redhat.com>
 *
 * Based on the original work by Paul McKenney <paulmck@us.ibm.com>.
 *
 */
#include <linux/sched.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/syscalls.h>
#include <linux/kthread.h>

DEFINE_RAW_SPINLOCK(rcu_boost_wake_lock);
static int rcu_boost_prio = MAX_PRIO;	/* Prio to set preempted RCU readers */
static long rcu_boost_counter;		/* used to keep track of who boosted */
static int rcu_preempt_thread_secs = 3;	/* Seconds between waking rcupreemptd thread */

struct rcu_boost_dat {
	raw_spinlock_t rbs_lock;	/* Sync changes to this struct */
	int rbs_prio;			/* CPU copy of rcu_boost_prio  */
	struct list_head rbs_toboost;	/* Preempted RCU readers       */
	struct list_head rbs_boosted;	/* RCU readers that have been boosted */
};

static DEFINE_PER_CPU(struct rcu_boost_dat, rcu_boost_data);

static inline int rcu_is_boosted(struct task_struct *task)
{
	return !list_empty(&task->rcub_entry);
}

/*
 * Helper function to boost a task's prio.
 */
static void rcu_boost_task(struct task_struct *task)
{
	WARN_ON(!irqs_disabled());
	WARN_ON_SMP(!spin_is_locked(&task->pi_lock));

	trace_mark(task_boost_called, "NULL");

	if (task->rcu_prio < task->prio) {
		trace_mark(task_boosted, "NULL");
		task_setprio(task, task->rcu_prio);
	}
}

/**
 * __rcu_preepmt_boost - Called by sleeping RCU readers.
 *
 * When the RCU read-side critical section is preempted
 * (or schedules out due to RT mutex)
 * it places itself onto a list to notify that it is sleeping
 * while holding a RCU read lock. If there is already a
 * synchronize_rcu happening, then it will increase its
 * priority (if necessary).
 */
void __rcu_preempt_boost(void)
{
	struct task_struct *curr = current;
	struct rcu_boost_dat *rbd;
	int prio;
	unsigned long flags;

	WARN_ON(!current->rcu_read_lock_nesting);

	trace_mark(boost_called, "NULL");

	/* check to see if we are already boosted */
	if (unlikely(rcu_is_boosted(curr)))
		return;

	/*
	 * To keep us from preempting between grabing
	 * the rbd and locking it, we use local_irq_save
	 */
	local_irq_save(flags);
	rbd = &__get_cpu_var(rcu_boost_data);
	spin_lock(&rbd->rbs_lock);

	spin_lock(&curr->pi_lock);

	curr->rcub_rbdp = rbd;

	trace_mark(try_boost, "NULL");

	prio = rt_mutex_getprio(curr);

	if (list_empty(&curr->rcub_entry))
		list_add_tail(&curr->rcub_entry, &rbd->rbs_toboost);
	if (prio <= rbd->rbs_prio)
		goto out;

	trace_mark(boosted, "NULL");

	curr->rcu_prio = rbd->rbs_prio;
	rcu_boost_task(curr);

 out:
	spin_unlock(&curr->pi_lock);
	spin_unlock_irqrestore(&rbd->rbs_lock, flags);
}

/**
 * __rcu_preempt_unboost - called when releasing the RCU read lock
 *
 * When releasing the RCU read lock, a check is made to see if
 * the task was preempted. If it was, it removes itself from the
 * RCU data lists and if necessary, sets its priority back to
 * normal.
 */
void __rcu_preempt_unboost(void)
{
	struct task_struct *curr = current;
	struct rcu_boost_dat *rbd;
	int prio;
	unsigned long flags;

	trace_mark(unboost_called, "NULL");

	/* if not boosted, then ignore */
	if (likely(!rcu_is_boosted(curr)))
		return;

	/*
	 * Need to be very careful with NMIs.
	 * If we take the lock and an NMI comes in
	 * and it may try to unboost us if curr->rcub_rbdp
	 * is still set. So we zero it before grabbing the lock.
	 * But this also means that we might be boosted again
	 * so the boosting code needs to be aware of this.
	 */
	rbd = curr->rcub_rbdp;
	curr->rcub_rbdp = NULL;

	/*
	 * Now an NMI might have came in after we grab
	 * the below lock. This check makes sure that
	 * the NMI doesn't try grabbing the lock
	 * while we already have it.
	 */
	if (unlikely(!rbd))
		return;

	spin_lock_irqsave(&rbd->rbs_lock, flags);
	/*
	 * It is still possible that an NMI came in
	 * between the "is_boosted" check and setting
	 * the rcu_rbdp to NULL. This would mean that
	 * the NMI already dequeued us.
	 */
	if (unlikely(!rcu_is_boosted(curr)))
		goto out;

	list_del_init(&curr->rcub_entry);

	trace_mark(unboosted, "NULL");

	curr->rcu_prio = MAX_PRIO;

	spin_lock(&curr->pi_lock);
	prio = rt_mutex_getprio(curr);
	task_setprio(curr, prio);

	curr->rcub_rbdp = NULL;

	spin_unlock(&curr->pi_lock);
 out:
	spin_unlock_irqrestore(&rbd->rbs_lock, flags);
}

/*
 * For each rcu_boost_dat structure, update all the tasks that
 * are on the lists to the priority of the caller of
 * synchronize_rcu.
 */
static int __rcu_boost_readers(struct rcu_boost_dat *rbd, int prio, unsigned long flags)
{
	struct task_struct *curr = current;
	struct task_struct *p;

	spin_lock(&rbd->rbs_lock);

	rbd->rbs_prio = prio;

	/*
	 * Move the already boosted readers onto the list and reboost
	 * them.
	 */
	list_splice_init(&rbd->rbs_boosted,
			 &rbd->rbs_toboost);

	while (!list_empty(&rbd->rbs_toboost)) {
		p = list_entry(rbd->rbs_toboost.next,
			       struct task_struct, rcub_entry);
		list_move_tail(&p->rcub_entry,
			       &rbd->rbs_boosted);
		p->rcu_prio = prio;
		spin_lock(&p->pi_lock);
		rcu_boost_task(p);
		spin_unlock(&p->pi_lock);

		/*
		 * Now we release the lock to allow for a higher
		 * priority task to come in and boost the readers
		 * even higher. Or simply to let a higher priority
		 * task to run now.
		 */
		spin_unlock(&rbd->rbs_lock);
		spin_unlock_irqrestore(&rcu_boost_wake_lock, flags);

		cpu_relax();
		spin_lock_irqsave(&rcu_boost_wake_lock, flags);
		/*
		 * Another task may have taken over.
		 */
		if (curr->rcu_preempt_counter != rcu_boost_counter) {
			trace_mark(over_taken, "NULL");
			return 1;
		}

		spin_lock(&rbd->rbs_lock);
	}

	spin_unlock(&rbd->rbs_lock);

	return 0;
}

/**
 * rcu_boost_readers - called by synchronize_rcu to boost sleeping RCU readers.
 *
 * This function iterates over all the per_cpu rcu_boost_data descriptors
 * and boosts any sleeping (or slept) RCU readers.
 */
void rcu_boost_readers(void)
{
	struct task_struct *curr = current;
	struct rcu_boost_dat *rbd;
	unsigned long flags;
	int prio;
	int cpu;
	int ret;

	spin_lock_irqsave(&rcu_boost_wake_lock, flags);

	prio = rt_mutex_getprio(curr);

	trace_mark(try_boost_readers, "NULL");

	if (prio >= rcu_boost_prio) {
		/* already boosted */
		spin_unlock_irqrestore(&rcu_boost_wake_lock, flags);
		return;
	}

	rcu_boost_prio = prio;

	trace_mark(boost_readers, "NULL");

	/* Flag that we are the one to unboost */
	curr->rcu_preempt_counter = ++rcu_boost_counter;

	for_each_online_cpu(cpu) {
		rbd = &per_cpu(rcu_boost_data, cpu);
		ret = __rcu_boost_readers(rbd, prio, flags);
		if (ret)
			break;
	}

	spin_unlock_irqrestore(&rcu_boost_wake_lock, flags);

}

/**
 * rcu_unboost_readers - set the boost level back to normal.
 *
 * This function DOES NOT change the priority of any RCU reader
 * that was boosted. The RCU readers do that when they release
 * the RCU lock. This function only sets the global
 * rcu_boost_prio to MAX_PRIO so that new RCU readers that sleep
 * do not increase their priority.
 */
void rcu_unboost_readers(void)
{
	struct rcu_boost_dat *rbd;
	unsigned long flags;
	int cpu;

	spin_lock_irqsave(&rcu_boost_wake_lock, flags);

	trace_mark(try_unboost_readers, "NULL");

	if (current->rcu_preempt_counter != rcu_boost_counter)
		goto out;

	trace_mark(unboost_readers, "NULL");

	/*
	 * We could also put in something that
	 * would allow other synchronize_rcu callers
	 * of lower priority that are still waiting
	 * to boost the prio.
	 */
	rcu_boost_prio = MAX_PRIO;

	for_each_online_cpu(cpu) {
		rbd = &per_cpu(rcu_boost_data, cpu);

		spin_lock(&rbd->rbs_lock);
		rbd->rbs_prio = rcu_boost_prio;
		spin_unlock(&rbd->rbs_lock);
	}

 out:
	spin_unlock_irqrestore(&rcu_boost_wake_lock, flags);
}

/*
 * This function exports the rcu_boost_prio variable for use by
 * modules that need it e.g. RCU_TRACE module
 */
int read_rcu_boost_prio(void)
{
	return rcu_boost_prio;
}
EXPORT_SYMBOL_GPL(read_rcu_boost_prio);

/*
 * The krcupreemptd wakes up every "rcu_preempt_thread_secs"
 * seconds at the minimum priority of 1 to do a
 * synchronize_rcu. This ensures that grace periods finish
 * and that we do not starve the system. If there are RT
 * tasks above priority 1 that are hogging the system and
 * preventing release of memory, then its the fault of the
 * system designer running RT tasks too aggressively and the
 * system is flawed regardless.
 */
static int krcupreemptd(void *data)
{
	struct sched_param param = { .sched_priority = 1 };
	int ret;
	int prio;

	ret = sched_setscheduler(current, SCHED_FIFO, &param);
	printk("krcupreemptd setsched %d\n", ret);
	prio = current->prio;
	printk("  prio = %d\n", prio);
	set_current_state(TASK_INTERRUPTIBLE);

	while (!kthread_should_stop()) {
		schedule_timeout(rcu_preempt_thread_secs * HZ);

		__set_current_state(TASK_RUNNING);
		if (prio != current->prio) {
			prio = current->prio;
			printk("krcupreemptd new prio is %d??\n",prio);
		}

		synchronize_rcu();

		set_current_state(TASK_INTERRUPTIBLE);
	}
	__set_current_state(TASK_RUNNING);
	return 0;
}

int __init rcu_preempt_boost_init(void)
{
	struct rcu_boost_dat *rbd;
	int cpu;

	for_each_possible_cpu(cpu) {
		rbd = &per_cpu(rcu_boost_data, cpu);

		spin_lock_init(&rbd->rbs_lock);
		rbd->rbs_prio = MAX_PRIO;
		INIT_LIST_HEAD(&rbd->rbs_toboost);
		INIT_LIST_HEAD(&rbd->rbs_boosted);
	}

	return 0;
}

static int __init rcu_preempt_start_krcupreemptd(void)
{
	struct task_struct *p;

	p = kthread_create(krcupreemptd, NULL,
			   "krcupreemptd");

	if (IS_ERR(p)) {
		printk("krcupreemptd failed\n");
		return NOTIFY_BAD;
	}
	wake_up_process(p);

	return 0;
}

__initcall(rcu_preempt_start_krcupreemptd);
