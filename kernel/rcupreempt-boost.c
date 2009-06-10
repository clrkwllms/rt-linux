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
#ifdef CONFIG_RCU_TRACE
	/* The rest are for statistics */
	unsigned long rbs_stat_task_boost_called;
	unsigned long rbs_stat_task_boosted;
	unsigned long rbs_stat_boost_called;
	unsigned long rbs_stat_try_boost;
	unsigned long rbs_stat_boosted;
	unsigned long rbs_stat_unboost_called;
	unsigned long rbs_stat_unboosted;
	unsigned long rbs_stat_try_boost_readers;
	unsigned long rbs_stat_boost_readers;
	unsigned long rbs_stat_try_unboost_readers;
	unsigned long rbs_stat_unboost_readers;
	unsigned long rbs_stat_over_taken;
#endif /* CONFIG_RCU_TRACE */
};

static DEFINE_PER_CPU(struct rcu_boost_dat, rcu_boost_data);
#define RCU_BOOST_ME &__get_cpu_var(rcu_boost_data)

#ifdef CONFIG_RCU_TRACE

#define RCUPREEMPT_BOOST_TRACE_BUF_SIZE 4096
static char rcupreempt_boost_trace_buf[RCUPREEMPT_BOOST_TRACE_BUF_SIZE];

static ssize_t rcuboost_read(struct file *filp, char __user *buffer,
				size_t count, loff_t *ppos)
{
	static DEFINE_MUTEX(mutex);
	int cnt = 0;
	int cpu;
	struct rcu_boost_dat *rbd;
	ssize_t bcount;
	unsigned long task_boost_called = 0;
	unsigned long task_boosted = 0;
	unsigned long boost_called = 0;
	unsigned long try_boost = 0;
	unsigned long boosted = 0;
	unsigned long unboost_called = 0;
	unsigned long unboosted = 0;
	unsigned long try_boost_readers = 0;
	unsigned long boost_readers = 0;
	unsigned long try_unboost_readers = 0;
	unsigned long unboost_readers = 0;
	unsigned long over_taken = 0;

	mutex_lock(&mutex);

	for_each_online_cpu(cpu) {
		rbd = &per_cpu(rcu_boost_data, cpu);

		task_boost_called += rbd->rbs_stat_task_boost_called;
		task_boosted += rbd->rbs_stat_task_boosted;
		boost_called += rbd->rbs_stat_boost_called;
		try_boost += rbd->rbs_stat_try_boost;
		boosted += rbd->rbs_stat_boosted;
		unboost_called += rbd->rbs_stat_unboost_called;
		unboosted += rbd->rbs_stat_unboosted;
		try_boost_readers += rbd->rbs_stat_try_boost_readers;
		boost_readers += rbd->rbs_stat_boost_readers;
		try_unboost_readers += rbd->rbs_stat_try_boost_readers;
		unboost_readers += rbd->rbs_stat_boost_readers;
		over_taken += rbd->rbs_stat_over_taken;
	}

	cnt += snprintf(&rcupreempt_boost_trace_buf[cnt],
			RCUPREEMPT_BOOST_TRACE_BUF_SIZE - cnt,
			"task_boost_called = %ld\n",
			task_boost_called);
	cnt += snprintf(&rcupreempt_boost_trace_buf[cnt],
			RCUPREEMPT_BOOST_TRACE_BUF_SIZE - cnt,
			"task_boosted = %ld\n",
			task_boosted);
	cnt += snprintf(&rcupreempt_boost_trace_buf[cnt],
			RCUPREEMPT_BOOST_TRACE_BUF_SIZE - cnt,
			"boost_called = %ld\n",
			boost_called);
	cnt += snprintf(&rcupreempt_boost_trace_buf[cnt],
			RCUPREEMPT_BOOST_TRACE_BUF_SIZE - cnt,
			"try_boost = %ld\n",
			try_boost);
	cnt += snprintf(&rcupreempt_boost_trace_buf[cnt],
			RCUPREEMPT_BOOST_TRACE_BUF_SIZE - cnt,
			"boosted = %ld\n",
			boosted);
	cnt += snprintf(&rcupreempt_boost_trace_buf[cnt],
			RCUPREEMPT_BOOST_TRACE_BUF_SIZE - cnt,
			"unboost_called = %ld\n",
			unboost_called);
	cnt += snprintf(&rcupreempt_boost_trace_buf[cnt],
			RCUPREEMPT_BOOST_TRACE_BUF_SIZE - cnt,
			"unboosted = %ld\n",
			unboosted);
	cnt += snprintf(&rcupreempt_boost_trace_buf[cnt],
			RCUPREEMPT_BOOST_TRACE_BUF_SIZE - cnt,
			"try_boost_readers = %ld\n",
			try_boost_readers);
	cnt += snprintf(&rcupreempt_boost_trace_buf[cnt],
			RCUPREEMPT_BOOST_TRACE_BUF_SIZE - cnt,
			"boost_readers = %ld\n",
			boost_readers);
	cnt += snprintf(&rcupreempt_boost_trace_buf[cnt],
			RCUPREEMPT_BOOST_TRACE_BUF_SIZE - cnt,
			"try_unboost_readers = %ld\n",
			try_unboost_readers);
	cnt += snprintf(&rcupreempt_boost_trace_buf[cnt],
			RCUPREEMPT_BOOST_TRACE_BUF_SIZE - cnt,
			"unboost_readers = %ld\n",
			unboost_readers);
	cnt += snprintf(&rcupreempt_boost_trace_buf[cnt],
			RCUPREEMPT_BOOST_TRACE_BUF_SIZE - cnt,
			"over_taken = %ld\n",
			over_taken);
	cnt += snprintf(&rcupreempt_boost_trace_buf[cnt],
			RCUPREEMPT_BOOST_TRACE_BUF_SIZE - cnt,
			"rcu_boost_prio = %d\n",
			rcu_boost_prio);
	bcount = simple_read_from_buffer(buffer, count, ppos,
			rcupreempt_boost_trace_buf, strlen(rcupreempt_boost_trace_buf));
	mutex_unlock(&mutex);

	return bcount;
}

static struct file_operations rcuboost_fops = {
	.read = rcuboost_read,
};

static struct dentry  *rcuboostdir;
int rcu_trace_boost_create(struct dentry *rcudir)
{
	rcuboostdir = debugfs_create_file("rcuboost", 0444, rcudir,
					  NULL, &rcuboost_fops);
	if (!rcuboostdir)
		return 1;

	return 0;
}
EXPORT_SYMBOL_GPL(rcu_trace_boost_create);

void rcu_trace_boost_destroy(void)
{
	if (rcuboostdir)
		debugfs_remove(rcuboostdir);
	rcuboostdir = NULL;
}
EXPORT_SYMBOL_GPL(rcu_trace_boost_destroy);

#define RCU_BOOST_TRACE_FUNC_DECL(type)			      \
	static void rcu_trace_boost_##type(struct rcu_boost_dat *rbd)	\
	{								\
		rbd->rbs_stat_##type++;					\
	}
RCU_BOOST_TRACE_FUNC_DECL(task_boost_called)
RCU_BOOST_TRACE_FUNC_DECL(task_boosted)
RCU_BOOST_TRACE_FUNC_DECL(boost_called)
RCU_BOOST_TRACE_FUNC_DECL(try_boost)
RCU_BOOST_TRACE_FUNC_DECL(boosted)
RCU_BOOST_TRACE_FUNC_DECL(unboost_called)
RCU_BOOST_TRACE_FUNC_DECL(unboosted)
RCU_BOOST_TRACE_FUNC_DECL(try_boost_readers)
RCU_BOOST_TRACE_FUNC_DECL(boost_readers)
RCU_BOOST_TRACE_FUNC_DECL(try_unboost_readers)
RCU_BOOST_TRACE_FUNC_DECL(unboost_readers)
RCU_BOOST_TRACE_FUNC_DECL(over_taken)
#else /* CONFIG_RCU_TRACE */
/* These were created by the above macro "RCU_BOOST_TRACE_FUNC_DECL" */
# define rcu_trace_boost_task_boost_called(rbd) do { } while (0)
# define rcu_trace_boost_task_boosted(rbd) do { } while (0)
# define rcu_trace_boost_boost_called(rbd) do { } while (0)
# define rcu_trace_boost_try_boost(rbd) do { } while (0)
# define rcu_trace_boost_boosted(rbd) do { } while (0)
# define rcu_trace_boost_unboost_called(rbd) do { } while (0)
# define rcu_trace_boost_unboosted(rbd) do { } while (0)
# define rcu_trace_boost_try_boost_readers(rbd) do { } while (0)
# define rcu_trace_boost_boost_readers(rbd) do { } while (0)
# define rcu_trace_boost_try_unboost_readers(rbd) do { } while (0)
# define rcu_trace_boost_unboost_readers(rbd) do { } while (0)
# define rcu_trace_boost_over_taken(rbd) do { } while (0)
#endif /* CONFIG_RCU_TRACE */

/*
 * Helper function to boost a task's prio.
 */
static void rcu_boost_task(struct task_struct *task)
{
	WARN_ON(!irqs_disabled());
	WARN_ON_SMP(!spin_is_locked(&task->pi_lock));

	rcu_trace_boost_task_boost_called(RCU_BOOST_ME);

	if (task->rcu_prio < task->prio) {
		rcu_trace_boost_task_boosted(RCU_BOOST_ME);
		rt_mutex_setprio(task, task->rcu_prio);
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

	rcu_trace_boost_boost_called(RCU_BOOST_ME);

	/* check to see if we are already boosted */
	if (unlikely(curr->rcub_rbdp))
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

	rcu_trace_boost_try_boost(rbd);

	prio = rt_mutex_getprio(curr);

	if (list_empty(&curr->rcub_entry))
		list_add_tail(&curr->rcub_entry, &rbd->rbs_toboost);
	if (prio <= rbd->rbs_prio)
		goto out;

	rcu_trace_boost_boosted(curr->rcub_rbdp);

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

	rcu_trace_boost_unboost_called(RCU_BOOST_ME);

	/* if not boosted, then ignore */
	if (likely(!curr->rcub_rbdp))
		return;

	rbd = curr->rcub_rbdp;

	spin_lock_irqsave(&rbd->rbs_lock, flags);
	list_del_init(&curr->rcub_entry);

	rcu_trace_boost_unboosted(curr->rcub_rbdp);

	curr->rcu_prio = MAX_PRIO;

	spin_lock(&curr->pi_lock);
	prio = rt_mutex_getprio(curr);
	rt_mutex_setprio(curr, prio);

	curr->rcub_rbdp = NULL;

	spin_unlock(&curr->pi_lock);
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
			rcu_trace_boost_over_taken(rbd);
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

	rcu_trace_boost_try_boost_readers(RCU_BOOST_ME);

	if (prio >= rcu_boost_prio) {
		/* already boosted */
		spin_unlock_irqrestore(&rcu_boost_wake_lock, flags);
		return;
	}

	rcu_boost_prio = prio;

	rcu_trace_boost_boost_readers(RCU_BOOST_ME);

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

	rcu_trace_boost_try_unboost_readers(RCU_BOOST_ME);

	if (current->rcu_preempt_counter != rcu_boost_counter)
		goto out;

	rcu_trace_boost_unboost_readers(RCU_BOOST_ME);

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

static int __init rcu_preempt_boost_init(void)
{
	struct rcu_boost_dat *rbd;
	struct task_struct *p;
	int cpu;

	for_each_possible_cpu(cpu) {
		rbd = &per_cpu(rcu_boost_data, cpu);

		spin_lock_init(&rbd->rbs_lock);
		rbd->rbs_prio = MAX_PRIO;
		INIT_LIST_HEAD(&rbd->rbs_toboost);
		INIT_LIST_HEAD(&rbd->rbs_boosted);
	}

	p = kthread_create(krcupreemptd, NULL,
			   "krcupreemptd");

	if (IS_ERR(p)) {
		printk("krcupreemptd failed\n");
		return NOTIFY_BAD;
	}
	wake_up_process(p);

	return 0;
}

core_initcall(rcu_preempt_boost_init);
