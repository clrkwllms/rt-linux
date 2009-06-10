/*
 * linux/kernel/workqueue.c
 *
 * Generic mechanism for defining kernel helper threads for running
 * arbitrary tasks in process context.
 *
 * Started by Ingo Molnar, Copyright (C) 2002
 *
 * Derived from the taskqueue/keventd code by:
 *
 *   David Woodhouse <dwmw2@infradead.org>
 *   Andrew Morton <andrewm@uow.edu.au>
 *   Kai Petzke <wpp@marie.physik.tu-berlin.de>
 *   Theodore Ts'o <tytso@mit.edu>
 *
 * Made to use alloc_percpu by Christoph Lameter <clameter@sgi.com>.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/signal.h>
#include <linux/completion.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/cpu.h>
#include <linux/notifier.h>
#include <linux/syscalls.h>
#include <linux/kthread.h>
#include <linux/hardirq.h>
#include <linux/mempolicy.h>
#include <linux/freezer.h>
#include <linux/kallsyms.h>
#include <linux/debug_locks.h>
#include <linux/lockdep.h>

#include <asm/uaccess.h>

struct wq_full_barrier;

/*
 * The per-CPU workqueue (if single thread, we always use the first
 * possible cpu).
 */
struct cpu_workqueue_struct {

	spinlock_t lock;

	struct plist_head worklist;
	wait_queue_head_t more_work;
	struct work_struct *current_work;

	struct workqueue_struct *wq;
	struct task_struct *thread;

	int run_depth;		/* Detect run_workqueue() recursion depth */

	struct wq_full_barrier *barrier;
} ____cacheline_aligned;

/*
 * The externally visible workqueue abstraction is an array of
 * per-CPU workqueues:
 */
struct workqueue_struct {
	struct cpu_workqueue_struct *cpu_wq;
	struct list_head list;
	const char *name;
	int singlethread;
	int freezeable;		/* Freeze threads during suspend */
#ifdef CONFIG_LOCKDEP
	struct lockdep_map lockdep_map;
#endif
};

/* All the per-cpu workqueues on the system, for hotplug cpu to add/remove
   threads to each one as cpus come/go. */
static DEFINE_MUTEX(workqueue_mutex);
static LIST_HEAD(workqueues);

static int singlethread_cpu __read_mostly;
static cpumask_t cpu_singlethread_map __read_mostly;
/*
 * _cpu_down() first removes CPU from cpu_online_map, then CPU_DEAD
 * flushes cwq->worklist. This means that flush_workqueue/wait_on_work
 * which comes in between can't use for_each_online_cpu(). We could
 * use cpu_possible_map, the cpumask below is more a documentation
 * than optimization.
 */
static cpumask_t cpu_populated_map __read_mostly;

/* If it's single threaded, it isn't in the list of workqueues. */
static inline int is_single_threaded(struct workqueue_struct *wq)
{
	return wq->singlethread;
}

static const cpumask_t *wq_cpu_map(struct workqueue_struct *wq)
{
	return is_single_threaded(wq)
		? &cpu_singlethread_map : &cpu_populated_map;
}

static
struct cpu_workqueue_struct *wq_per_cpu(struct workqueue_struct *wq, int cpu)
{
	if (unlikely(is_single_threaded(wq)))
		cpu = singlethread_cpu;
	return per_cpu_ptr(wq->cpu_wq, cpu);
}

/*
 * Set the workqueue on which a work item is to be run
 * - Must *only* be called if the pending flag is set
 */
static inline void set_wq_data(struct work_struct *work,
				struct cpu_workqueue_struct *cwq)
{
	unsigned long new;

	BUG_ON(!work_pending(work));

	new = (unsigned long) cwq | (1UL << WORK_STRUCT_PENDING);
	new |= WORK_STRUCT_FLAG_MASK & *work_data_bits(work);
	atomic_long_set(&work->data, new);
}

static inline
struct cpu_workqueue_struct *get_wq_data(struct work_struct *work)
{
	return (void *) (atomic_long_read(&work->data) & WORK_STRUCT_WQ_DATA_MASK);
}

static void insert_work(struct cpu_workqueue_struct *cwq,
		struct work_struct *work, int prio, int boost_prio)
{
	set_wq_data(work, cwq);
	/*
	 * Ensure that we get the right work->data if we see the
	 * result of list_add() below, see try_to_grab_pending().
	 */
	smp_wmb();
	plist_node_init(&work->entry, prio);
	plist_add(&work->entry, &cwq->worklist);

	if (boost_prio < cwq->thread->prio)
		task_setprio(cwq->thread, boost_prio);
	wake_up(&cwq->more_work);
}

/* Preempt must be disabled. */
static void __queue_work(struct cpu_workqueue_struct *cwq,
			 struct work_struct *work)
{
	unsigned long flags;

	spin_lock_irqsave(&cwq->lock, flags);
	insert_work(cwq, work, current->normal_prio, current->normal_prio);
	spin_unlock_irqrestore(&cwq->lock, flags);
}

/**
 * queue_work - queue work on a workqueue
 * @wq: workqueue to use
 * @work: work to queue
 *
 * Returns 0 if @work was already on a queue, non-zero otherwise.
 *
 * We queue the work to the CPU it was submitted, but there is no
 * guarantee that it will be processed by that CPU.
 *
 * Especially no such guarantee on PREEMPT_RT.
 */
int fastcall queue_work(struct workqueue_struct *wq, struct work_struct *work)
{
	int ret = 0, cpu = raw_smp_processor_id();

	if (!test_and_set_bit(WORK_STRUCT_PENDING, work_data_bits(work))) {
		BUG_ON(!plist_node_empty(&work->entry));
		__queue_work(wq_per_cpu(wq, cpu), work);
		ret = 1;
	}
	return ret;
}
EXPORT_SYMBOL_GPL(queue_work);

void delayed_work_timer_fn(unsigned long __data)
{
	struct delayed_work *dwork = (struct delayed_work *)__data;
	struct cpu_workqueue_struct *cwq = get_wq_data(&dwork->work);
	struct workqueue_struct *wq = cwq->wq;

	__queue_work(wq_per_cpu(wq, raw_smp_processor_id()), &dwork->work);
}

/**
 * queue_delayed_work - queue work on a workqueue after delay
 * @wq: workqueue to use
 * @dwork: delayable work to queue
 * @delay: number of jiffies to wait before queueing
 *
 * Returns 0 if @work was already on a queue, non-zero otherwise.
 */
int fastcall queue_delayed_work(struct workqueue_struct *wq,
			struct delayed_work *dwork, unsigned long delay)
{
	timer_stats_timer_set_start_info(&dwork->timer);
	if (delay == 0)
		return queue_work(wq, &dwork->work);

	return queue_delayed_work_on(-1, wq, dwork, delay);
}
EXPORT_SYMBOL_GPL(queue_delayed_work);

/**
 * queue_delayed_work_on - queue work on specific CPU after delay
 * @cpu: CPU number to execute work on
 * @wq: workqueue to use
 * @dwork: work to queue
 * @delay: number of jiffies to wait before queueing
 *
 * Returns 0 if @work was already on a queue, non-zero otherwise.
 */
int queue_delayed_work_on(int cpu, struct workqueue_struct *wq,
			struct delayed_work *dwork, unsigned long delay)
{
	int ret = 0;
	struct timer_list *timer = &dwork->timer;
	struct work_struct *work = &dwork->work;

	if (!test_and_set_bit(WORK_STRUCT_PENDING, work_data_bits(work))) {
		BUG_ON(timer_pending(timer));
		BUG_ON(!plist_node_empty(&work->entry));

		/* This stores cwq for the moment, for the timer_fn */
		set_wq_data(work, wq_per_cpu(wq, raw_smp_processor_id()));
		timer->expires = jiffies + delay;
		timer->data = (unsigned long)dwork;
		timer->function = delayed_work_timer_fn;

		if (unlikely(cpu >= 0))
			add_timer_on(timer, cpu);
		else
			add_timer(timer);
		ret = 1;
	}
	return ret;
}
EXPORT_SYMBOL_GPL(queue_delayed_work_on);

static void leak_check(void *func)
{
	if (!in_atomic() && lockdep_depth(current) <= 0)
		return;
	printk(KERN_ERR "BUG: workqueue leaked lock or atomic: "
				"%s/0x%08x/%d\n",
				current->comm, preempt_count(),
				current->pid);
	printk(KERN_ERR "    last function: ");
	print_symbol("%s\n", (unsigned long)func);
	debug_show_held_locks(current);
	dump_stack();
}

struct wq_full_barrier {
	struct work_struct		work;
	struct plist_head		worklist;
	struct wq_full_barrier 		*prev_barrier;
	int				prev_prio;
	int				waiter_prio;
	struct cpu_workqueue_struct 	*cwq;
	struct completion		done;
};

static void run_workqueue(struct cpu_workqueue_struct *cwq)
{
	struct plist_head *worklist = &cwq->worklist;

	spin_lock_irq(&cwq->lock);
	cwq->run_depth++;
	if (cwq->run_depth > 3) {
		/* morton gets to eat his hat */
		printk("%s: recursion depth exceeded: %d\n",
			__FUNCTION__, cwq->run_depth);
		dump_stack();
	}

again:
	while (!plist_head_empty(worklist)) {
		int prio;
		struct work_struct *work = plist_first_entry(worklist,
						struct work_struct, entry);
		work_func_t f = work->func;
#ifdef CONFIG_LOCKDEP
		/*
		 * It is permissible to free the struct work_struct
		 * from inside the function that is called from it,
		 * this we need to take into account for lockdep too.
		 * To avoid bogus "held lock freed" warnings as well
		 * as problems when looking into work->lockdep_map,
		 * make a copy and use that here.
		 */
		struct lockdep_map lockdep_map = work->lockdep_map;
#endif

		prio = work->entry.prio;
		if (unlikely(worklist != &cwq->worklist)) {
			prio = min(prio, cwq->barrier->prev_prio);
			prio = min(prio, cwq->barrier->waiter_prio);
			prio = min(prio, plist_first(&cwq->worklist)->prio);
		}
		prio = max(prio, 0);

		if (likely(cwq->thread->prio != prio))
			task_setprio(cwq->thread, prio);

		cwq->current_work = work;
		plist_del(&work->entry, worklist);
		plist_node_init(&work->entry, MAX_PRIO);
		spin_unlock_irq(&cwq->lock);

		BUG_ON(get_wq_data(work) != cwq);
		work_clear_pending(work);
		leak_check(NULL);
		lock_acquire(&cwq->wq->lockdep_map, 0, 0, 0, 2, _THIS_IP_);
		lock_acquire(&lockdep_map, 0, 0, 0, 2, _THIS_IP_);
		f(work);
		lock_release(&lockdep_map, 1, _THIS_IP_);
		lock_release(&cwq->wq->lockdep_map, 1, _THIS_IP_);
		leak_check(f);

		spin_lock_irq(&cwq->lock);
		cwq->current_work = NULL;

		if (unlikely(cwq->barrier))
			worklist = &cwq->barrier->worklist;
	}

	if (unlikely(worklist != &cwq->worklist)) {
		struct wq_full_barrier *barrier = cwq->barrier;

		BUG_ON(!barrier);
		cwq->barrier = barrier->prev_barrier;
		complete(&barrier->done);

		if (unlikely(cwq->barrier))
			worklist = &cwq->barrier->worklist;
		else
			worklist = &cwq->worklist;

		if (!plist_head_empty(worklist))
			goto again;
	}

	task_setprio(cwq->thread, current->normal_prio);
	cwq->run_depth--;
	spin_unlock_irq(&cwq->lock);
}

static int worker_thread(void *__cwq)
{
	struct cpu_workqueue_struct *cwq = __cwq;
	DEFINE_WAIT(wait);

	if (cwq->wq->freezeable)
		set_freezable();

	set_user_nice(current, -5);

	for (;;) {
		prepare_to_wait(&cwq->more_work, &wait, TASK_INTERRUPTIBLE);
		if (!freezing(current) &&
		    !kthread_should_stop() &&
		    plist_head_empty(&cwq->worklist))
			schedule();
		finish_wait(&cwq->more_work, &wait);

		try_to_freeze();

		if (kthread_should_stop())
			break;

		run_workqueue(cwq);
	}

	return 0;
}

struct wq_barrier {
	struct work_struct	work;
	struct completion	done;
};

static void wq_barrier_func(struct work_struct *work)
{
	struct wq_barrier *barr = container_of(work, struct wq_barrier, work);
	complete(&barr->done);
}

static void insert_wq_barrier(struct cpu_workqueue_struct *cwq,
					struct wq_barrier *barr, int prio)
{
	INIT_WORK(&barr->work, wq_barrier_func);
	__set_bit(WORK_STRUCT_PENDING, work_data_bits(&barr->work));

	init_completion(&barr->done);

	insert_work(cwq, &barr->work, prio, current->prio);
}

static void wq_full_barrier_func(struct work_struct *work)
{
	struct wq_full_barrier *barrier =
		container_of(work, struct wq_full_barrier, work);
	struct cpu_workqueue_struct *cwq = barrier->cwq;
	int prio = MAX_PRIO;

	spin_lock_irq(&cwq->lock);
	barrier->prev_barrier = cwq->barrier;
	if (cwq->barrier) {
		prio = min(prio, cwq->barrier->waiter_prio);
		prio = min(prio, plist_first(&cwq->barrier->worklist)->prio);
	}
	barrier->prev_prio = prio;
	cwq->barrier = barrier;
	spin_unlock_irq(&cwq->lock);
}

static void insert_wq_full_barrier(struct cpu_workqueue_struct *cwq,
		struct wq_full_barrier *barr)
{
	INIT_WORK(&barr->work, wq_full_barrier_func);
	__set_bit(WORK_STRUCT_PENDING, work_data_bits(&barr->work));

	plist_head_init(&barr->worklist, NULL);
	plist_head_splice(&cwq->worklist, &barr->worklist);
	barr->cwq = cwq;
	init_completion(&barr->done);
	barr->waiter_prio = current->prio;

	insert_work(cwq, &barr->work, 0, current->prio);
}

static int flush_cpu_workqueue(struct cpu_workqueue_struct *cwq)
{
	int active;

	if (cwq->thread == current) {
		/*
		 * Probably keventd trying to flush its own queue. So simply run
		 * it by hand rather than deadlocking.
		 */
		run_workqueue(cwq);
		active = 1;
	} else {
		struct wq_full_barrier barr;

		active = 0;
		spin_lock_irq(&cwq->lock);
		if (!plist_head_empty(&cwq->worklist) ||
			cwq->current_work != NULL) {
			insert_wq_full_barrier(cwq, &barr);
			active = 1;
		}
		spin_unlock_irq(&cwq->lock);

		if (active)
			wait_for_completion(&barr.done);
	}

	return active;
}

/**
 * flush_workqueue - ensure that any scheduled work has run to completion.
 * @wq: workqueue to flush
 *
 * Forces execution of the workqueue and blocks until its completion.
 * This is typically used in driver shutdown handlers.
 *
 * We sleep until all works which were queued on entry have been handled,
 * but we are not livelocked by new incoming ones.
 *
 * This function used to run the workqueues itself.  Now we just wait for the
 * helper threads to do it.
 */
void fastcall flush_workqueue(struct workqueue_struct *wq)
{
	const cpumask_t *cpu_map = wq_cpu_map(wq);
	int cpu;

	might_sleep();
	lock_acquire(&wq->lockdep_map, 0, 0, 0, 2, _THIS_IP_);
	lock_release(&wq->lockdep_map, 1, _THIS_IP_);
	for_each_cpu_mask(cpu, *cpu_map)
		flush_cpu_workqueue(per_cpu_ptr(wq->cpu_wq, cpu));
}
EXPORT_SYMBOL_GPL(flush_workqueue);

/*
 * Upon a successful return (>= 0), the caller "owns" WORK_STRUCT_PENDING bit,
 * so this work can't be re-armed in any way.
 */
static int try_to_grab_pending(struct work_struct *work)
{
	struct cpu_workqueue_struct *cwq;
	int ret = -1;

	if (!test_and_set_bit(WORK_STRUCT_PENDING, work_data_bits(work)))
		return 0;

	/*
	 * The queueing is in progress, or it is already queued. Try to
	 * steal it from ->worklist without clearing WORK_STRUCT_PENDING.
	 */

	cwq = get_wq_data(work);
	if (!cwq)
		return ret;

	spin_lock_irq(&cwq->lock);
	if (!plist_node_empty(&work->entry)) {
		/*
		 * This work is queued, but perhaps we locked the wrong cwq.
		 * In that case we must see the new value after rmb(), see
		 * insert_work()->wmb().
		 */
		smp_rmb();
		if (cwq == get_wq_data(work)) {
			plist_del(&work->entry, &cwq->worklist);
			plist_node_init(&work->entry, MAX_PRIO);
			ret = 1;
		}
	}
	spin_unlock_irq(&cwq->lock);

	return ret;
}

static void wait_on_cpu_work(struct cpu_workqueue_struct *cwq,
				struct work_struct *work)
{
	struct wq_barrier barr;
	int running = 0;

	spin_lock_irq(&cwq->lock);
	if (unlikely(cwq->current_work == work)) {
		insert_wq_barrier(cwq, &barr, -1);
		running = 1;
	}
	spin_unlock_irq(&cwq->lock);

	if (unlikely(running))
		wait_for_completion(&barr.done);
}

static void wait_on_work(struct work_struct *work)
{
	struct cpu_workqueue_struct *cwq;
	struct workqueue_struct *wq;
	const cpumask_t *cpu_map;
	int cpu;

	might_sleep();

	lock_acquire(&work->lockdep_map, 0, 0, 0, 2, _THIS_IP_);
	lock_release(&work->lockdep_map, 1, _THIS_IP_);

	cwq = get_wq_data(work);
	if (!cwq)
		return;

	wq = cwq->wq;
	cpu_map = wq_cpu_map(wq);

	for_each_cpu_mask(cpu, *cpu_map)
		wait_on_cpu_work(per_cpu_ptr(wq->cpu_wq, cpu), work);
}

static int __cancel_work_timer(struct work_struct *work,
				struct timer_list* timer)
{
	int ret;

	do {
		ret = (timer && likely(del_timer(timer)));
		if (!ret)
			ret = try_to_grab_pending(work);
		wait_on_work(work);
	} while (unlikely(ret < 0));

	work_clear_pending(work);
	return ret;
}

/**
 * cancel_work_sync - block until a work_struct's callback has terminated
 * @work: the work which is to be flushed
 *
 * Returns true if @work was pending.
 *
 * cancel_work_sync() will cancel the work if it is queued. If the work's
 * callback appears to be running, cancel_work_sync() will block until it
 * has completed.
 *
 * It is possible to use this function if the work re-queues itself. It can
 * cancel the work even if it migrates to another workqueue, however in that
 * case it only guarantees that work->func() has completed on the last queued
 * workqueue.
 *
 * cancel_work_sync(&delayed_work->work) should be used only if ->timer is not
 * pending, otherwise it goes into a busy-wait loop until the timer expires.
 *
 * The caller must ensure that workqueue_struct on which this work was last
 * queued can't be destroyed before this function returns.
 */
int cancel_work_sync(struct work_struct *work)
{
	return __cancel_work_timer(work, NULL);
}
EXPORT_SYMBOL_GPL(cancel_work_sync);

/**
 * cancel_delayed_work_sync - reliably kill off a delayed work.
 * @dwork: the delayed work struct
 *
 * Returns true if @dwork was pending.
 *
 * It is possible to use this function if @dwork rearms itself via queue_work()
 * or queue_delayed_work(). See also the comment for cancel_work_sync().
 */
int cancel_delayed_work_sync(struct delayed_work *dwork)
{
	return __cancel_work_timer(&dwork->work, &dwork->timer);
}
EXPORT_SYMBOL(cancel_delayed_work_sync);

static struct workqueue_struct *keventd_wq __read_mostly;

/**
 * schedule_work - put work task in global workqueue
 * @work: job to be done
 *
 * This puts a job in the kernel-global workqueue.
 */
int fastcall schedule_work(struct work_struct *work)
{
	return queue_work(keventd_wq, work);
}
EXPORT_SYMBOL(schedule_work);

/**
 * schedule_delayed_work - put work task in global workqueue after delay
 * @dwork: job to be done
 * @delay: number of jiffies to wait or 0 for immediate execution
 *
 * After waiting for a given time this puts a job in the kernel-global
 * workqueue.
 */
int fastcall schedule_delayed_work(struct delayed_work *dwork,
					unsigned long delay)
{
	timer_stats_timer_set_start_info(&dwork->timer);
	return queue_delayed_work(keventd_wq, dwork, delay);
}
EXPORT_SYMBOL(schedule_delayed_work);

/**
 * schedule_delayed_work_on - queue work in global workqueue on CPU after delay
 * @cpu: cpu to use
 * @dwork: job to be done
 * @delay: number of jiffies to wait
 *
 * After waiting for a given time this puts a job in the kernel-global
 * workqueue on the specified CPU.
 */
int schedule_delayed_work_on(int cpu,
			struct delayed_work *dwork, unsigned long delay)
{
	return queue_delayed_work_on(cpu, keventd_wq, dwork, delay);
}
EXPORT_SYMBOL(schedule_delayed_work_on);

struct schedule_on_each_cpu_work {
	struct work_struct work;
	void (*func)(void *info);
	void *info;
};

static void schedule_on_each_cpu_func(struct work_struct *work)
{
	struct schedule_on_each_cpu_work *w;

	w = container_of(work, typeof(*w), work);
	w->func(w->info);

	kfree(w);
}

/**
 * schedule_on_each_cpu - call a function on each online CPU from keventd
 * @func: the function to call
 * @info: data to pass to function
 * @retry: ignored
 * @wait: wait for completion
 *
 * Returns zero on success.
 * Returns -ve errno on failure.
 *
 * Appears to be racy against CPU hotplug.
 *
 * schedule_on_each_cpu() is very slow.
 */
int schedule_on_each_cpu(void (*func)(void *info), void *info, int retry, int wait)
{
	int cpu;
	struct schedule_on_each_cpu_work **works;
	int err = 0;

	works = kzalloc(sizeof(void *)*nr_cpu_ids, GFP_KERNEL);
	if (!works)
		return -ENOMEM;

	for_each_possible_cpu(cpu) {
		works[cpu] = kmalloc_node(sizeof(struct schedule_on_each_cpu_work),
				GFP_KERNEL, cpu_to_node(cpu));
		if (!works[cpu]) {
			err = -ENOMEM;
			goto out;
		}
	}

	lock_cpu_hotplug();
	for_each_online_cpu(cpu) {
		struct schedule_on_each_cpu_work *work;

		work = works[cpu];
		works[cpu] = NULL;

		work->func = func;
		work->info = info;
		INIT_WORK(&work->work, schedule_on_each_cpu_func);
		set_bit(WORK_STRUCT_PENDING, work_data_bits(&work->work));
		__queue_work(per_cpu_ptr(keventd_wq->cpu_wq, cpu), &work->work);
	}
	unlock_cpu_hotplug();

out:
	for_each_possible_cpu(cpu) {
		if (works[cpu])
			kfree(works[cpu]);
	}
	kfree(works);

	if (!err && wait)
		flush_workqueue(keventd_wq);

	return err;
}
EXPORT_SYMBOL(schedule_on_each_cpu);

/**
 * schedule_on_each_cpu_wq - call a function on each online CPU on a per-CPU wq
 * @func: the function to call
 *
 * Returns zero on success.
 * Returns -ve errno on failure.
 *
 * Appears to be racy against CPU hotplug.
 *
 * schedule_on_each_cpu() is very slow.
 */
int schedule_on_each_cpu_wq(struct workqueue_struct *wq, work_func_t func)
{
	int cpu;
	struct work_struct *works;

	if (is_single_threaded(wq)) {
		WARN_ON(1);
		return -EINVAL;
	}
	works = alloc_percpu(struct work_struct);
	if (!works)
		return -ENOMEM;

	for_each_online_cpu(cpu) {
		struct work_struct *work = per_cpu_ptr(works, cpu);

		INIT_WORK(work, func);
		set_bit(WORK_STRUCT_PENDING, work_data_bits(work));
		__queue_work(per_cpu_ptr(wq->cpu_wq, cpu), work);
	}
	flush_workqueue(wq);
	free_percpu(works);

	return 0;
}


void flush_scheduled_work(void)
{
	flush_workqueue(keventd_wq);
}
EXPORT_SYMBOL(flush_scheduled_work);

/**
 * execute_in_process_context - reliably execute the routine with user context
 * @fn:		the function to execute
 * @ew:		guaranteed storage for the execute work structure (must
 *		be available when the work executes)
 *
 * Executes the function immediately if process context is available,
 * otherwise schedules the function for delayed execution.
 *
 * Returns:	0 - function was executed
 *		1 - function was scheduled for execution
 */
int execute_in_process_context(work_func_t fn, struct execute_work *ew)
{
	if (!in_interrupt()) {
		fn(&ew->work);
		return 0;
	}

	INIT_WORK(&ew->work, fn);
	schedule_work(&ew->work);

	return 1;
}
EXPORT_SYMBOL_GPL(execute_in_process_context);

int keventd_up(void)
{
	return keventd_wq != NULL;
}

int current_is_keventd(void)
{
	struct cpu_workqueue_struct *cwq;
	int cpu = raw_smp_processor_id(); /* preempt-safe: keventd is per-cpu */
	int ret = 0;

	BUG_ON(!keventd_wq);

	cwq = per_cpu_ptr(keventd_wq->cpu_wq, cpu);
	if (current == cwq->thread)
		ret = 1;

	return ret;

}

static struct cpu_workqueue_struct *
init_cpu_workqueue(struct workqueue_struct *wq, int cpu)
{
	struct cpu_workqueue_struct *cwq = per_cpu_ptr(wq->cpu_wq, cpu);

	cwq->wq = wq;
	spin_lock_init(&cwq->lock);
	plist_head_init(&cwq->worklist, NULL);
	init_waitqueue_head(&cwq->more_work);
	cwq->barrier = NULL;

	return cwq;
}

static int create_workqueue_thread(struct cpu_workqueue_struct *cwq, int cpu)
{
	struct workqueue_struct *wq = cwq->wq;
	const char *fmt = is_single_threaded(wq) ? "%s" : "%s/%d";
	struct task_struct *p;

	p = kthread_create(worker_thread, cwq, fmt, wq->name, cpu);
	/*
	 * Nobody can add the work_struct to this cwq,
	 *	if (caller is __create_workqueue)
	 *		nobody should see this wq
	 *	else // caller is CPU_UP_PREPARE
	 *		cpu is not on cpu_online_map
	 * so we can abort safely.
	 */
	if (IS_ERR(p))
		return PTR_ERR(p);

	cwq->thread = p;

	return 0;
}

static void start_workqueue_thread(struct cpu_workqueue_struct *cwq, int cpu)
{
	struct task_struct *p = cwq->thread;

	if (p != NULL) {
		if (cpu >= 0)
			kthread_bind(p, cpu);
		wake_up_process(p);
	}
}

struct workqueue_struct *__create_workqueue_key(const char *name,
						int singlethread,
						int freezeable,
						struct lock_class_key *key,
						const char *lock_name)
{
	struct workqueue_struct *wq;
	struct cpu_workqueue_struct *cwq;
	int err = 0, cpu;

	wq = kzalloc(sizeof(*wq), GFP_KERNEL);
	if (!wq)
		return NULL;

	wq->cpu_wq = alloc_percpu(struct cpu_workqueue_struct);
	if (!wq->cpu_wq) {
		kfree(wq);
		return NULL;
	}

	wq->name = name;
	lockdep_init_map(&wq->lockdep_map, lock_name, key, 0);
	wq->singlethread = singlethread;
	wq->freezeable = freezeable;
	INIT_LIST_HEAD(&wq->list);

	if (singlethread) {
		cwq = init_cpu_workqueue(wq, singlethread_cpu);
		err = create_workqueue_thread(cwq, singlethread_cpu);
		start_workqueue_thread(cwq, -1);
	} else {
		mutex_lock(&workqueue_mutex);
		list_add(&wq->list, &workqueues);

		for_each_possible_cpu(cpu) {
			cwq = init_cpu_workqueue(wq, cpu);
			if (err || !cpu_online(cpu))
				continue;
			err = create_workqueue_thread(cwq, cpu);
			start_workqueue_thread(cwq, cpu);
		}
		mutex_unlock(&workqueue_mutex);
	}

	if (err) {
		destroy_workqueue(wq);
		wq = NULL;
	}
	return wq;
}
EXPORT_SYMBOL_GPL(__create_workqueue_key);

static void cleanup_workqueue_thread(struct cpu_workqueue_struct *cwq, int cpu)
{
	/*
	 * Our caller is either destroy_workqueue() or CPU_DEAD,
	 * workqueue_mutex protects cwq->thread
	 */
	if (cwq->thread == NULL)
		return;

	lock_acquire(&cwq->wq->lockdep_map, 0, 0, 0, 2, _THIS_IP_);
	lock_release(&cwq->wq->lockdep_map, 1, _THIS_IP_);

	flush_cpu_workqueue(cwq);
	/*
	 * If the caller is CPU_DEAD and cwq->worklist was not empty,
	 * a concurrent flush_workqueue() can insert a barrier after us.
	 * However, in that case run_workqueue() won't return and check
	 * kthread_should_stop() until it flushes all work_struct's.
	 * When ->worklist becomes empty it is safe to exit because no
	 * more work_structs can be queued on this cwq: flush_workqueue
	 * checks list_empty(), and a "normal" queue_work() can't use
	 * a dead CPU.
	 */
	kthread_stop(cwq->thread);
	cwq->thread = NULL;
}

void set_workqueue_thread_prio(struct workqueue_struct *wq, int cpu,
			       int policy, int rt_priority, int nice)
{
	struct sched_param param = { .sched_priority = rt_priority };
	struct cpu_workqueue_struct *cwq;
	mm_segment_t oldfs = get_fs();
	struct task_struct *p;
	unsigned long flags;
	int ret;

	cwq = per_cpu_ptr(wq->cpu_wq, cpu);
	spin_lock_irqsave(&cwq->lock, flags);
	p = cwq->thread;
	spin_unlock_irqrestore(&cwq->lock, flags);

	set_user_nice(p, nice);

	set_fs(KERNEL_DS);
	ret = sys_sched_setscheduler(p->pid, policy, &param);
	set_fs(oldfs);

	WARN_ON(ret);
}

 void set_workqueue_prio(struct workqueue_struct *wq, int policy,
			int rt_priority, int nice)
{
	int cpu;

	/* We don't need the distraction of CPUs appearing and vanishing. */
	mutex_lock(&workqueue_mutex);
	if (is_single_threaded(wq))
		set_workqueue_thread_prio(wq, 0, policy, rt_priority, nice);
	else {
		for_each_online_cpu(cpu)
			set_workqueue_thread_prio(wq, cpu, policy,
						  rt_priority, nice);
	}
	mutex_unlock(&workqueue_mutex);
}

/**
 * destroy_workqueue - safely terminate a workqueue
 * @wq: target workqueue
 *
 * Safely destroy a workqueue. All work currently pending will be done first.
 */
void destroy_workqueue(struct workqueue_struct *wq)
{
	const cpumask_t *cpu_map = wq_cpu_map(wq);
	struct cpu_workqueue_struct *cwq;
	int cpu;

	mutex_lock(&workqueue_mutex);
	list_del(&wq->list);
	mutex_unlock(&workqueue_mutex);

	for_each_cpu_mask(cpu, *cpu_map) {
		cwq = per_cpu_ptr(wq->cpu_wq, cpu);
		cleanup_workqueue_thread(cwq, cpu);
	}

	free_percpu(wq->cpu_wq);
	kfree(wq);
}
EXPORT_SYMBOL_GPL(destroy_workqueue);

static int __devinit workqueue_cpu_callback(struct notifier_block *nfb,
						unsigned long action,
						void *hcpu)
{
	unsigned int cpu = (unsigned long)hcpu;
	struct cpu_workqueue_struct *cwq;
	struct workqueue_struct *wq;

	action &= ~CPU_TASKS_FROZEN;

	switch (action) {
	case CPU_LOCK_ACQUIRE:
		mutex_lock(&workqueue_mutex);
		return NOTIFY_OK;

	case CPU_LOCK_RELEASE:
		mutex_unlock(&workqueue_mutex);
		return NOTIFY_OK;

	case CPU_UP_PREPARE:
		cpu_set(cpu, cpu_populated_map);
	}

	list_for_each_entry(wq, &workqueues, list) {
		cwq = per_cpu_ptr(wq->cpu_wq, cpu);

		switch (action) {
		case CPU_UP_PREPARE:
			if (!create_workqueue_thread(cwq, cpu))
				break;
			printk(KERN_ERR "workqueue for %i failed\n", cpu);
			return NOTIFY_BAD;

		case CPU_ONLINE:
			start_workqueue_thread(cwq, cpu);
			break;

		case CPU_UP_CANCELED:
			start_workqueue_thread(cwq, -1);
		case CPU_DEAD:
			cleanup_workqueue_thread(cwq, cpu);
			break;
		}
	}

	return NOTIFY_OK;
}

void __init init_workqueues(void)
{
	cpu_populated_map = cpu_online_map;
	singlethread_cpu = first_cpu(cpu_possible_map);
	cpu_singlethread_map = cpumask_of_cpu(singlethread_cpu);
	hotcpu_notifier(workqueue_cpu_callback, 0);
	keventd_wq = create_workqueue("events");
	BUG_ON(!keventd_wq);
	set_workqueue_prio(keventd_wq, SCHED_FIFO, 1, -20);
}
