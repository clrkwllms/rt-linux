/*
 * This is the Big Kernel Lock - the traditional lock that we
 * inherited from the uniprocessor Linux kernel a decade ago.
 *
 * Largely relegated to obsolescence, but used by various less
 * important (or lazy) subsystems.
 *
 * Don't use in new code.
 *
 * It now has plain mutex semantics (i.e. no auto-drop on
 * schedule() anymore), combined with a very simple self-recursion
 * layer that allows the traditional nested use:
 *
 *   lock_kernel();
 *     lock_kernel();
 *     unlock_kernel();
 *   unlock_kernel();
 *
 * Please migrate all BKL using code to a plain mutex.
 */
#include <linux/smp_lock.h>
#include <linux/kallsyms.h>
#include <linux/module.h>
#include <linux/mutex.h>

static DEFINE_MUTEX(kernel_mutex);

/*
 * Get the big kernel lock:
 */
void __lockfunc lock_kernel(void)
{
	struct task_struct *task = current;
	int depth = task->lock_depth + 1;

	if (likely(!depth))
		/*
		 * No recursion worries - we set up lock_depth _after_
		 */
		mutex_lock(&kernel_mutex);

	task->lock_depth = depth;
}

void __lockfunc unlock_kernel(void)
{
	struct task_struct *task = current;

	if (WARN_ON_ONCE(task->lock_depth < 0))
		return;

	if (likely(--task->lock_depth < 0))
		mutex_unlock(&kernel_mutex);
}

void debug_print_bkl(void)
{
#ifdef CONFIG_DEBUG_MUTEXES
	if (mutex_is_locked(&kernel_mutex)) {
		printk(KERN_EMERG "BUG: **** BKL held by: %d:%s\n",
			kernel_mutex.owner->task->pid,
			kernel_mutex.owner->task->comm);
	}
#endif
}

EXPORT_SYMBOL(lock_kernel);
EXPORT_SYMBOL(unlock_kernel);

