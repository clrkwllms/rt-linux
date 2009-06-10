#include <linux/sched.h>
#include <linux/hardirq.h>
#include <linux/kallsyms.h>

void print_preempt_trace(struct task_struct *task)
{
	unsigned int count;
	unsigned int i, lim;

	if (!task)
		task = current;

	count = task_thread_info(task)->preempt_count;
	lim = count & PREEMPT_MASK;

	if (lim >= MAX_PREEMPT_TRACE)
		lim = MAX_PREEMPT_TRACE-1;
	printk("---------------------------\n");
	printk("| preempt count: %08x ]\n", count);
	printk("| %d-level deep critical section nesting:\n", lim);
	printk("----------------------------------------\n");
	for (i = 1; i <= lim; i++) {
		printk(".. [<%08lx>] .... ", task->preempt_trace_eip[i]);
		print_symbol("%s\n", task->preempt_trace_eip[i]);
		printk(".....[<%08lx>] ..   ( <= ",
				task->preempt_trace_parent_eip[i]);
		print_symbol("%s)\n", task->preempt_trace_parent_eip[i]);
	}
	printk("\n");
}
