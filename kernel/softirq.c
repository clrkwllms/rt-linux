/*
 *	linux/kernel/softirq.c
 *
 *	Copyright (C) 1992 Linus Torvalds
 *
 * Rewritten. Old one was good in 2.2, but in 2.3 it was immoral. --ANK (990903)
 *
 *	Softirq-split implemetation by
 *	Copyright (C) 2005 Thomas Gleixner, Ingo Molnar
 */

#include <linux/module.h>
#include <linux/kallsyms.h>
#include <linux/syscalls.h>
#include <linux/wait.h>
#include <linux/kernel_stat.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/notifier.h>
#include <linux/percpu.h>
#include <linux/delay.h>
#include <linux/cpu.h>
#include <linux/freezer.h>
#include <linux/kthread.h>
#include <linux/rcupdate.h>
#include <linux/smp.h>
#include <linux/tick.h>

#include <asm/irq.h>
/*
   - No shared variables, all the data are CPU local.
   - If a softirq needs serialization, let it serialize itself
     by its own spinlocks.
   - Even if softirq is serialized, only local cpu is marked for
     execution. Hence, we get something sort of weak cpu binding.
     Though it is still not clear, will it result in better locality
     or will not.

   Examples:
   - NET RX softirq. It is multithreaded and does not require
     any global serialization.
   - NET TX softirq. It kicks software netdevice queues, hence
     it is logically serialized per device, but this serialization
     is invisible to common code.
   - Tasklets: serialized wrt itself.
 */

#ifndef __ARCH_IRQ_STAT
irq_cpustat_t irq_stat[NR_CPUS] ____cacheline_aligned;
EXPORT_SYMBOL(irq_stat);
#endif

static struct softirq_action softirq_vec[32] __cacheline_aligned_in_smp;

struct softirqdata {
	int			nr;
	unsigned long		cpu;
	struct task_struct	*tsk;
#ifdef CONFIG_PREEMPT_SOFTIRQS
	wait_queue_head_t	wait;
	int			running;
#endif
};

static DEFINE_PER_CPU(struct softirqdata [MAX_SOFTIRQ], ksoftirqd);

#ifdef CONFIG_PREEMPT_SOFTIRQS
/*
 * Preempting the softirq causes cases that would not be a
 * problem when the softirq is not preempted. That is a
 * process may have code to spin while waiting for a softirq
 * to finish on another CPU.  But if it happens that the
 * process has preempted the softirq, this could cause a
 * deadlock.
 */
void wait_for_softirq(int softirq)
{
	struct softirqdata *data = &__get_cpu_var(ksoftirqd)[softirq];
	if (data->running) {
		DECLARE_WAITQUEUE(wait, current);
		set_current_state(TASK_UNINTERRUPTIBLE);
		add_wait_queue(&data->wait, &wait);
		if (data->running)
			schedule();
		remove_wait_queue(&data->wait, &wait);
		__set_current_state(TASK_RUNNING);
	}
}
#endif

/*
 * we cannot loop indefinitely here to avoid userspace starvation,
 * but we also don't want to introduce a worst case 1/HZ latency
 * to the pending events, so lets the scheduler to balance
 * the softirq load for us.
 */
static void wakeup_softirqd(int softirq)
{
	/* Interrupts are disabled: no need to stop preemption */
	struct task_struct *tsk = __get_cpu_var(ksoftirqd)[softirq].tsk;

	if (unlikely(!tsk))
		return;
	/*
	 * Wake up the softirq task:
	 */
	wake_up_process(tsk);
}

/*
 * Wake up the softirq threads which have work
 */
static void trigger_softirqs(void)
{
	u32 pending = local_softirq_pending();
	int curr = 0;

	while (pending) {
		if (pending & 1)
			wakeup_softirqd(curr);
		pending >>= 1;
		curr++;
	}
}

#ifndef CONFIG_PREEMPT_HARDIRQS

/*
 * This one is for softirq.c-internal use,
 * where hardirqs are disabled legitimately:
 */
#ifdef CONFIG_TRACE_IRQFLAGS
static void __local_bh_disable(unsigned long ip)
{
	unsigned long flags;

	WARN_ON_ONCE(in_irq());

	raw_local_irq_save(flags);
	add_preempt_count(SOFTIRQ_OFFSET);
	/*
	 * Were softirqs turned off above:
	 */
	if (softirq_count() == SOFTIRQ_OFFSET)
		trace_softirqs_off(ip);
	raw_local_irq_restore(flags);
}
#else /* !CONFIG_TRACE_IRQFLAGS */
static inline void __local_bh_disable(unsigned long ip)
{
	add_preempt_count(SOFTIRQ_OFFSET);
	barrier();
}
#endif /* CONFIG_TRACE_IRQFLAGS */

void local_bh_disable(void)
{
	__local_bh_disable((unsigned long)__builtin_return_address(0));
}

EXPORT_SYMBOL(local_bh_disable);

/*
 * Special-case - softirqs can safely be enabled in
 * cond_resched_softirq(), or by __do_softirq(),
 * without processing still-pending softirqs:
 */
void _local_bh_enable(void)
{
	WARN_ON_ONCE(in_irq());
	WARN_ON_ONCE(!irqs_disabled());

	if (softirq_count() == SOFTIRQ_OFFSET)
		trace_softirqs_on((unsigned long)__builtin_return_address(0));
	sub_preempt_count(SOFTIRQ_OFFSET);
}

EXPORT_SYMBOL(_local_bh_enable);

void local_bh_enable(void)
{
#ifdef CONFIG_TRACE_IRQFLAGS
	unsigned long flags;

	WARN_ON_ONCE(in_irq());
#endif

#ifdef CONFIG_TRACE_IRQFLAGS
	local_irq_save(flags);
#endif
	/*
	 * Are softirqs going to be turned on now:
	 */
	if (softirq_count() == SOFTIRQ_OFFSET)
		trace_softirqs_on((unsigned long)__builtin_return_address(0));
	/*
	 * Keep preemption disabled until we are done with
	 * softirq processing:
 	 */
 	sub_preempt_count(SOFTIRQ_OFFSET - 1);

	if (unlikely(!in_interrupt() && local_softirq_pending()))
		do_softirq();

	dec_preempt_count();
#ifdef CONFIG_TRACE_IRQFLAGS
	local_irq_restore(flags);
#endif
	preempt_check_resched();
}
EXPORT_SYMBOL(local_bh_enable);

void local_bh_enable_ip(unsigned long ip)
{
#ifdef CONFIG_TRACE_IRQFLAGS
	unsigned long flags;

	WARN_ON_ONCE(in_irq());

	local_irq_save(flags);
#endif
	/*
	 * Are softirqs going to be turned on now:
	 */
	if (softirq_count() == SOFTIRQ_OFFSET)
		trace_softirqs_on(ip);
	/*
	 * Keep preemption disabled until we are done with
	 * softirq processing:
 	 */
 	sub_preempt_count(SOFTIRQ_OFFSET - 1);

	if (unlikely(!in_interrupt() && local_softirq_pending()))
		do_softirq();

	dec_preempt_count();
#ifdef CONFIG_TRACE_IRQFLAGS
	local_irq_restore(flags);
#endif
	preempt_check_resched();
}
EXPORT_SYMBOL(local_bh_enable_ip);

#endif

/*
 * We restart softirq processing MAX_SOFTIRQ_RESTART times,
 * and we fall back to softirqd after that.
 *
 * This number has been established via experimentation.
 * The two things to balance is latency against fairness -
 * we want to handle softirqs as soon as possible, but they
 * should not be able to lock up the box.
 */
#define MAX_SOFTIRQ_RESTART 20

static DEFINE_PER_CPU(u32, softirq_running);

static void ___do_softirq(const int same_prio_only)
{
	int max_restart = MAX_SOFTIRQ_RESTART, max_loops = MAX_SOFTIRQ_RESTART;
	__u32 pending, available_mask, same_prio_skipped;
	struct softirq_action *h;
	struct task_struct *tsk;
	int cpu, softirq;

	pending = local_softirq_pending();
	account_system_vtime(current);

	cpu = smp_processor_id();
restart:
	available_mask = -1;
	softirq = 0;
	same_prio_skipped = 0;
	/* Reset the pending bitmask before enabling irqs */
	set_softirq_pending(0);

	h = softirq_vec;

	do {
		u32 softirq_mask = 1 << softirq;

		if (pending & 1) {
			u32 preempt_count = preempt_count();

#if defined(CONFIG_PREEMPT_SOFTIRQS) && defined(CONFIG_PREEMPT_HARDIRQS)
			/*
			 * If executed by a same-prio hardirq thread
			 * then skip pending softirqs that belong
			 * to softirq threads with different priority:
			 */
			if (same_prio_only) {
				tsk = __get_cpu_var(ksoftirqd)[softirq].tsk;
				if (tsk && tsk->normal_prio !=
						current->normal_prio) {
					same_prio_skipped |= softirq_mask;
					available_mask &= ~softirq_mask;
					goto next;
				}
			}
#endif
			/*
			 * Is this softirq already being processed?
			 */
			if (per_cpu(softirq_running, cpu) & softirq_mask) {
				available_mask &= ~softirq_mask;
				goto next;
			}
			per_cpu(softirq_running, cpu) |= softirq_mask;
			local_irq_enable();

			h->action(h);
			if (preempt_count != preempt_count()) {
				print_symbol("BUG: softirq exited %s with wrong preemption count!\n", (unsigned long) h->action);
				printk("entered with %08x, exited with %08x.\n", preempt_count, preempt_count());
				preempt_count() = preempt_count;
			}
			rcu_bh_qsctr_inc(cpu);
			cond_resched_softirq_context();
			local_irq_disable();
			per_cpu(softirq_running, cpu) &= ~softirq_mask;
		}
next:
		h++;
		softirq++;
		pending >>= 1;
	} while (pending);

	or_softirq_pending(same_prio_skipped);
	pending = local_softirq_pending();
	if (pending & available_mask) {
		if (--max_restart)
			goto restart;
		/*
		 * With softirq threading there's no reason not to
		 * finish the workload we have:
		 */
#ifdef CONFIG_PREEMPT_SOFTIRQS
		if (--max_loops) {
			if (printk_ratelimit())
				printk("INFO: softirq overload: %08x\n", pending);
			max_restart = MAX_SOFTIRQ_RESTART;
			goto restart;
		}
		if (printk_ratelimit())
			printk("BUG: softirq loop! %08x\n", pending);
#endif
	}

	if (pending)
		trigger_softirqs();
}

asmlinkage void __do_softirq(void)
{
	unsigned long p_flags;

#ifdef CONFIG_PREEMPT_SOFTIRQS
	/*
	 * 'preempt harder'. Push all softirq processing off to ksoftirqd.
	 */
	if (softirq_preemption) {
		if (local_softirq_pending())
			trigger_softirqs();
		return;
	}
#endif
	/*
	 * 'immediate' softirq execution:
	 */
	__local_bh_disable((unsigned long)__builtin_return_address(0));
	trace_softirq_enter();
	p_flags = current->flags & PF_HARDIRQ;
	current->flags &= ~PF_HARDIRQ;

	___do_softirq(0);

	trace_softirq_exit();

	account_system_vtime(current);
	_local_bh_enable();

	current->flags |= p_flags;
}

/*
 * Process softirqs straight from hardirq context,
 * without having to switch to a softirq thread.
 * This can reduce the context-switch rate.
 *
 * NOTE: this is unused right now.
 */
void do_softirq_from_hardirq(void)
{
	unsigned long p_flags;

	/*
	 * 'immediate' softirq execution, from hardirq context:
	 */
	local_irq_disable();
	if (!local_softirq_pending())
		goto out;
	__local_bh_disable((unsigned long)__builtin_return_address(0));
#ifndef CONFIG_PREEMPT_SOFTIRQS
	trace_softirq_enter();
#endif
	p_flags = current->flags & PF_HARDIRQ;
	current->flags &= ~PF_HARDIRQ;
	current->flags |= PF_SOFTIRQ;

	___do_softirq(1);

#ifndef CONFIG_PREEMPT_SOFTIRQS
	trace_softirq_exit();
#endif
	account_system_vtime(current);

	current->flags |= p_flags;
	current->flags &= ~PF_SOFTIRQ;

	_local_bh_enable();
out:
	local_irq_enable();
}

#ifndef __ARCH_HAS_DO_SOFTIRQ

asmlinkage void do_softirq(void)
{
	__u32 pending;
	unsigned long flags;

	if (in_interrupt())
		return;

	local_irq_save(flags);

	pending = local_softirq_pending();

	if (pending)
		__do_softirq();

	local_irq_restore(flags);
}

#endif

/*
 * Enter an interrupt context.
 */
void irq_enter(void)
{
	__irq_enter();
#ifdef CONFIG_NO_HZ
	if (idle_cpu(smp_processor_id()))
		tick_nohz_update_jiffies();
#endif
}

#ifdef __ARCH_IRQ_EXIT_IRQS_DISABLED
# define invoke_softirq()	__do_softirq()
#else
# define invoke_softirq()	do_softirq()
#endif

/*
 * Exit an interrupt context. Process softirqs if needed and possible:
 */
void irq_exit(void)
{
	account_system_vtime(current);
	trace_hardirq_exit();
	sub_preempt_count(IRQ_EXIT_OFFSET);
	if (!in_interrupt() && local_softirq_pending())
		invoke_softirq();

#ifdef CONFIG_NO_HZ
	/* Make sure that timer wheel updates are propagated */
	if (!in_interrupt() && idle_cpu(smp_processor_id()) && !need_resched())
		tick_nohz_stop_sched_tick();
	rcu_irq_exit();
#endif
	__preempt_enable_no_resched();
}

/*
 * This function must run with irqs disabled!
 */
inline fastcall void raise_softirq_irqoff(unsigned int nr)
{
	__do_raise_softirq_irqoff(nr);

#ifdef CONFIG_PREEMPT_SOFTIRQS
	wakeup_softirqd(nr);
#endif
}

void fastcall raise_softirq(unsigned int nr)
{
	unsigned long flags;

	local_irq_save(flags);
	raise_softirq_irqoff(nr);
	local_irq_restore(flags);
}

void open_softirq(int nr, void (*action)(struct softirq_action*), void *data)
{
	softirq_vec[nr].data = data;
	softirq_vec[nr].action = action;
}

/* Tasklets */
struct tasklet_head
{
	struct tasklet_struct *list;
};

/* Some compilers disobey section attribute on statics when not
   initialized -- RR */
static DEFINE_PER_CPU(struct tasklet_head, tasklet_vec) = { NULL };
static DEFINE_PER_CPU(struct tasklet_head, tasklet_hi_vec) = { NULL };

static void inline
__tasklet_common_schedule(struct tasklet_struct *t, struct tasklet_head *head, unsigned int nr)
{
	if (tasklet_trylock(t)) {
again:
		/* We may have been preempted before tasklet_trylock
		 * and __tasklet_action may have already run.
		 * So double check the sched bit while the takslet
		 * is locked before adding it to the list.
		 */
		if (test_bit(TASKLET_STATE_SCHED, &t->state)) {
			WARN_ON(t->next != NULL);
			t->next = head->list;
			head->list = t;
			raise_softirq_irqoff(nr);
			tasklet_unlock(t);
		} else {
			/* This is subtle. If we hit the corner case above
			 * It is possible that we get preempted right here,
			 * and another task has successfully called
			 * tasklet_schedule(), then this function, and
			 * failed on the trylock. Thus we must be sure
			 * before releasing the tasklet lock, that the
			 * SCHED_BIT is clear. Otherwise the tasklet
			 * may get its SCHED_BIT set, but not added to the
			 * list
			 */
			if (!tasklet_tryunlock(t))
				goto again;
		}
	}
}

void fastcall __tasklet_schedule(struct tasklet_struct *t)
{
	unsigned long flags;

	local_irq_save(flags);
	__tasklet_common_schedule(t, &__get_cpu_var(tasklet_vec), TASKLET_SOFTIRQ);
	local_irq_restore(flags);
}

EXPORT_SYMBOL(__tasklet_schedule);

void fastcall __tasklet_hi_schedule(struct tasklet_struct *t)
{
	unsigned long flags;

	local_irq_save(flags);
	__tasklet_common_schedule(t, &__get_cpu_var(tasklet_hi_vec), HI_SOFTIRQ);
	local_irq_restore(flags);
}

EXPORT_SYMBOL(__tasklet_hi_schedule);

void fastcall tasklet_enable(struct tasklet_struct *t)
{
	if (!atomic_dec_and_test(&t->count))
		return;
	if (test_and_clear_bit(TASKLET_STATE_PENDING, &t->state))
		tasklet_schedule(t);
}

EXPORT_SYMBOL(tasklet_enable);

void fastcall tasklet_hi_enable(struct tasklet_struct *t)
{
	if (!atomic_dec_and_test(&t->count))
		return;
	if (test_and_clear_bit(TASKLET_STATE_PENDING, &t->state))
		tasklet_hi_schedule(t);
}

EXPORT_SYMBOL(tasklet_hi_enable);

static void
__tasklet_action(struct softirq_action *a, struct tasklet_struct *list)
{
	int loops = 1000000;

	while (list) {
		struct tasklet_struct *t = list;

		list = list->next;
		/*
		 * Should always succeed - after a tasklist got on the
		 * list (after getting the SCHED bit set from 0 to 1),
		 * nothing but the tasklet softirq it got queued to can
		 * lock it:
		 */
		if (!tasklet_trylock(t)) {
			WARN_ON(1);
			continue;
		}

		t->next = NULL;

		/*
		 * If we cannot handle the tasklet because it's disabled,
		 * mark it as pending. tasklet_enable() will later
		 * re-schedule the tasklet.
		 */
		if (unlikely(atomic_read(&t->count))) {
out_disabled:
			/* implicit unlock: */
			wmb();
			t->state = TASKLET_STATEF_PENDING;
			continue;
		}

		/*
		 * After this point on the tasklet might be rescheduled
		 * on another CPU, but it can only be added to another
		 * CPU's tasklet list if we unlock the tasklet (which we
		 * dont do yet).
		 */
		if (!test_and_clear_bit(TASKLET_STATE_SCHED, &t->state))
			WARN_ON(1);

again:
		t->func(t->data);

		/*
		 * Try to unlock the tasklet. We must use cmpxchg, because
		 * another CPU might have scheduled or disabled the tasklet.
		 * We only allow the STATE_RUN -> 0 transition here.
		 */
		while (!tasklet_tryunlock(t)) {
			/*
			 * If it got disabled meanwhile, bail out:
			 */
			if (atomic_read(&t->count))
				goto out_disabled;
			/*
			 * If it got scheduled meanwhile, re-execute
			 * the tasklet function:
			 */
			if (test_and_clear_bit(TASKLET_STATE_SCHED, &t->state))
				goto again;
			if (!--loops) {
				printk("hm, tasklet state: %08lx\n", t->state);
				WARN_ON(1);
				tasklet_unlock(t);
				break;
			}
		}
	}
}

static void tasklet_action(struct softirq_action *a)
{
	struct tasklet_struct *list;

	local_irq_disable();
	list = __get_cpu_var(tasklet_vec).list;
	__get_cpu_var(tasklet_vec).list = NULL;
	local_irq_enable();

	__tasklet_action(a, list);
}

static void tasklet_hi_action(struct softirq_action *a)
{
	struct tasklet_struct *list;

	local_irq_disable();
	list = __get_cpu_var(tasklet_hi_vec).list;
	__get_cpu_var(tasklet_hi_vec).list = NULL;
	local_irq_enable();

	__tasklet_action(a, list);
}

void tasklet_init(struct tasklet_struct *t,
		  void (*func)(unsigned long), unsigned long data)
{
	t->next = NULL;
	t->state = 0;
	atomic_set(&t->count, 0);
	t->func = func;
	t->data = data;
}

EXPORT_SYMBOL(tasklet_init);

void tasklet_kill(struct tasklet_struct *t)
{
	if (in_interrupt())
		printk("Attempt to kill tasklet from interrupt\n");

	while (test_and_set_bit(TASKLET_STATE_SCHED, &t->state)) {
		do
			msleep(1);
		while (test_bit(TASKLET_STATE_SCHED, &t->state));
	}
	tasklet_unlock_wait(t);
	clear_bit(TASKLET_STATE_SCHED, &t->state);
}

EXPORT_SYMBOL(tasklet_kill);

void __init softirq_init(void)
{
	open_softirq(TASKLET_SOFTIRQ, tasklet_action, NULL);
	open_softirq(HI_SOFTIRQ, tasklet_hi_action, NULL);
}

#if defined(CONFIG_SMP) || defined(CONFIG_PREEMPT_RT)

void tasklet_unlock_wait(struct tasklet_struct *t)
{
	while (test_bit(TASKLET_STATE_RUN, &(t)->state)) {
		/*
		 * Hack for now to avoid this busy-loop:
		 */
#ifdef CONFIG_PREEMPT_RT
		msleep(1);
#else
		barrier();
#endif
	}
}
EXPORT_SYMBOL(tasklet_unlock_wait);

#endif

static int ksoftirqd(void * __data)
{
	struct sched_param param = { .sched_priority = MAX_USER_RT_PRIO/2 };
	struct softirqdata *data = __data;
	u32 softirq_mask = (1 << data->nr);
	struct softirq_action *h;
	int cpu = data->cpu;

#ifdef CONFIG_PREEMPT_SOFTIRQS
	init_waitqueue_head(&data->wait);
#endif

	sys_sched_setscheduler(current->pid, SCHED_FIFO, &param);
	current->flags |= PF_SOFTIRQ;
	set_current_state(TASK_INTERRUPTIBLE);

	while (!kthread_should_stop()) {
		preempt_disable();
		if (!(local_softirq_pending() & softirq_mask)) {
sleep_more:
			__preempt_enable_no_resched();
			schedule();
			preempt_disable();
		}

		__set_current_state(TASK_RUNNING);

#ifdef CONFIG_PREEMPT_SOFTIRQS
		data->running = 1;
#endif

		while (local_softirq_pending() & softirq_mask) {
			/* Preempt disable stops cpu going offline.
			   If already offline, we'll be on wrong CPU:
			   don't process */
			if (cpu_is_offline(cpu))
				goto wait_to_die;

			local_irq_disable();
			/*
			 * Is the softirq already being executed by
			 * a hardirq context?
			 */
			if (per_cpu(softirq_running, cpu) & softirq_mask) {
				local_irq_enable();
				set_current_state(TASK_INTERRUPTIBLE);
				goto sleep_more;
			}
			per_cpu(softirq_running, cpu) |= softirq_mask;
			__preempt_enable_no_resched();
			set_softirq_pending(local_softirq_pending() & ~softirq_mask);
			local_bh_disable();
			local_irq_enable();

			h = &softirq_vec[data->nr];
			if (h)
				h->action(h);
			rcu_bh_qsctr_inc(data->cpu);

			local_irq_disable();
			per_cpu(softirq_running, cpu) &= ~softirq_mask;
			_local_bh_enable();
			local_irq_enable();

			cond_resched();
			preempt_disable();
		}
		preempt_enable();
		set_current_state(TASK_INTERRUPTIBLE);
#ifdef CONFIG_PREEMPT_SOFTIRQS
		data->running = 0;
		wake_up(&data->wait);
#endif
	}
	__set_current_state(TASK_RUNNING);
	return 0;

wait_to_die:
	preempt_enable();
	/* Wait for kthread_stop */
	set_current_state(TASK_INTERRUPTIBLE);
	while (!kthread_should_stop()) {
		schedule();
		set_current_state(TASK_INTERRUPTIBLE);
	}
	__set_current_state(TASK_RUNNING);
	return 0;
}

#ifdef CONFIG_HOTPLUG_CPU
/*
 * tasklet_kill_immediate is called to remove a tasklet which can already be
 * scheduled for execution on @cpu.
 *
 * Unlike tasklet_kill, this function removes the tasklet
 * _immediately_, even if the tasklet is in TASKLET_STATE_SCHED state.
 *
 * When this function is called, @cpu must be in the CPU_DEAD state.
 */
void tasklet_kill_immediate(struct tasklet_struct *t, unsigned int cpu)
{
	struct tasklet_struct **i;

	BUG_ON(cpu_online(cpu));
	BUG_ON(test_bit(TASKLET_STATE_RUN, &t->state));

	if (!test_bit(TASKLET_STATE_SCHED, &t->state))
		return;

	/* CPU is dead, so no lock needed. */
	for (i = &per_cpu(tasklet_vec, cpu).list; *i; i = &(*i)->next) {
		if (*i == t) {
			*i = t->next;
			return;
		}
	}
	BUG();
}

void takeover_tasklets(unsigned int cpu)
{
	struct tasklet_struct **i;

	/* CPU is dead, so no lock needed. */
	local_irq_disable();

	/* Find end, append list for that CPU. */
	for (i = &__get_cpu_var(tasklet_vec).list; *i; i = &(*i)->next);
	*i = per_cpu(tasklet_vec, cpu).list;
	per_cpu(tasklet_vec, cpu).list = NULL;
	raise_softirq_irqoff(TASKLET_SOFTIRQ);

	for (i = &__get_cpu_var(tasklet_hi_vec).list; *i; i = &(*i)->next);
	*i = per_cpu(tasklet_hi_vec, cpu).list;
	per_cpu(tasklet_hi_vec, cpu).list = NULL;
	raise_softirq_irqoff(HI_SOFTIRQ);

	local_irq_enable();
}
#endif /* CONFIG_HOTPLUG_CPU */

static const char *softirq_names [] =
{
  [HI_SOFTIRQ]		= "high",
  [SCHED_SOFTIRQ]	= "sched",
  [TIMER_SOFTIRQ]	= "timer",
  [NET_TX_SOFTIRQ]	= "net-tx",
  [NET_RX_SOFTIRQ]	= "net-rx",
  [BLOCK_SOFTIRQ]	= "block",
  [TASKLET_SOFTIRQ]	= "tasklet",
#ifdef CONFIG_HIGH_RES_TIMERS
  [HRTIMER_SOFTIRQ]	= "hrtimer",
#endif
  [RCU_SOFTIRQ]		= "rcu",
};

static int __cpuinit cpu_callback(struct notifier_block *nfb,
				  unsigned long action,
				  void *hcpu)
{
	int hotcpu = (unsigned long)hcpu, i;
	struct task_struct *p;

	switch (action) {
	case CPU_UP_PREPARE:
	case CPU_UP_PREPARE_FROZEN:
		for (i = 0; i < MAX_SOFTIRQ; i++) {
			per_cpu(ksoftirqd, hotcpu)[i].nr = i;
			per_cpu(ksoftirqd, hotcpu)[i].cpu = hotcpu;
			per_cpu(ksoftirqd, hotcpu)[i].tsk = NULL;
		}
		for (i = 0; i < MAX_SOFTIRQ; i++) {
			p = kthread_create(ksoftirqd,
					   &per_cpu(ksoftirqd, hotcpu)[i],
					   "sirq-%s/%d", softirq_names[i],
					   hotcpu);
			if (IS_ERR(p)) {
				printk("ksoftirqd %d for %i failed\n", i,
				       hotcpu);
				return NOTIFY_BAD;
			}
			kthread_bind(p, hotcpu);
			per_cpu(ksoftirqd, hotcpu)[i].tsk = p;
		}
		break;
	break;
	case CPU_ONLINE:
	case CPU_ONLINE_FROZEN:
		for (i = 0; i < MAX_SOFTIRQ; i++)
			wake_up_process(per_cpu(ksoftirqd, hotcpu)[i].tsk);
		break;
#ifdef CONFIG_HOTPLUG_CPU
	case CPU_UP_CANCELED:
	case CPU_UP_CANCELED_FROZEN:
#if 0
		for (i = 0; i < MAX_SOFTIRQ; i++) {
			if (!per_cpu(ksoftirqd, hotcpu)[i].tsk)
				continue;
			kthread_bind(per_cpu(ksoftirqd, hotcpu)[i].tsk,
				     any_online_cpu(cpu_online_map));
		}
#endif
	case CPU_DEAD:
	case CPU_DEAD_FROZEN:
		for (i = 0; i < MAX_SOFTIRQ; i++) {
			struct sched_param param;

			param.sched_priority = MAX_RT_PRIO-1;
			p = per_cpu(ksoftirqd, hotcpu)[i].tsk;
			sched_setscheduler(p, SCHED_FIFO, &param);
			per_cpu(ksoftirqd, hotcpu)[i].tsk = NULL;
			kthread_stop(p);
		}
		takeover_tasklets(hotcpu);
		break;
#endif /* CONFIG_HOTPLUG_CPU */
	}
	return NOTIFY_OK;
}

static struct notifier_block __cpuinitdata cpu_nfb = {
	.notifier_call = cpu_callback
};

__init int spawn_ksoftirqd(void)
{
	void *cpu = (void *)(long)smp_processor_id();
	int err = cpu_callback(&cpu_nfb, CPU_UP_PREPARE, cpu);

	BUG_ON(err == NOTIFY_BAD);
	cpu_callback(&cpu_nfb, CPU_ONLINE, cpu);
	register_cpu_notifier(&cpu_nfb);
	return 0;
}


#ifdef CONFIG_PREEMPT_SOFTIRQS

int softirq_preemption = 1;

EXPORT_SYMBOL(softirq_preemption);

/*
 * Real-Time Preemption depends on softirq threading:
 */
#ifndef CONFIG_PREEMPT_RT

static int __init softirq_preempt_setup (char *str)
{
	if (!strncmp(str, "off", 3))
		softirq_preemption = 0;
	else
		get_option(&str, &softirq_preemption);
	if (!softirq_preemption)
		printk("turning off softirq preemption!\n");

	return 1;
}

__setup("softirq-preempt=", softirq_preempt_setup);
#endif
#endif

#ifdef CONFIG_SMP
/*
 * Call a function on all processors
 */
int on_each_cpu(void (*func) (void *info), void *info, int retry, int wait)
{
	int ret = 0;

	preempt_disable();
	ret = smp_call_function(func, info, retry, wait);
	local_irq_disable();
	func(info);
	local_irq_enable();
	preempt_enable();
	return ret;
}
EXPORT_SYMBOL(on_each_cpu);
#endif
