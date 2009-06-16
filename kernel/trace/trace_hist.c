/*
 * kernel/trace/trace_hist.c
 *
 * Add support for histograms of preemption-off latency and
 * interrupt-off latency and wakeup latency, it depends on
 * Real-Time Preemption Support.
 *
 *  Copyright (C) 2005 MontaVista Software, Inc.
 *  Yi Yang <yyang@ch.mvista.com>
 *
 *  Converted to work with the new latency tracer.
 *  Copyright (C) 2008 Red Hat, Inc.
 *    Steven Rostedt <srostedt@redhat.com>
 *
 */
#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/percpu.h>
#include <linux/marker.h>
#include <asm/atomic.h>
#include <asm/div64.h>
#include <asm/uaccess.h>

#include "trace.h"
#include "trace_hist.h"

enum {
	INTERRUPT_LATENCY = 0,
	PREEMPT_LATENCY,
	PREEMPT_INTERRUPT_LATENCY,
};

#define MAX_ENTRY_NUM 10240

struct hist_data {
	atomic_t hist_mode; /* 0 log, 1 don't log */
	unsigned long min_lat;
	unsigned long max_lat;
	unsigned long long beyond_hist_bound_samples;
	unsigned long long accumulate_lat;
	unsigned long long total_samples;
	unsigned long long hist_array[MAX_ENTRY_NUM];
};

static char *latency_hist_dir_root = "latency_hist";

#ifdef CONFIG_INTERRUPT_OFF_HIST
static DEFINE_PER_CPU(struct hist_data, interrupt_off_hist);
static char *interrupt_off_hist_dir = "interrupt_off_latency";
#endif

#ifdef CONFIG_PREEMPT_OFF_HIST
static DEFINE_PER_CPU(struct hist_data, preempt_off_hist);
static char *preempt_off_hist_dir = "preempt_off_latency";
#endif

#if defined(CONFIG_PREEMPT_OFF_HIST) && defined(CONFIG_INTERRUPT_OFF_HIST)
static DEFINE_PER_CPU(struct hist_data, preempt_irqs_off_hist);
static char *preempt_irqs_off_hist_dir = "preempt_interrupts_off_latency";
#endif

void notrace latency_hist(int latency_type, int cpu, unsigned long latency)
{
	struct hist_data *my_hist;

	if ((cpu < 0) || (cpu >= NR_CPUS) || (latency_type < INTERRUPT_LATENCY)
			|| (latency_type > PREEMPT_INTERRUPT_LATENCY)
			|| (latency < 0))
		return;

	switch (latency_type) {
#ifdef CONFIG_INTERRUPT_OFF_HIST
	case INTERRUPT_LATENCY:
		my_hist = &per_cpu(interrupt_off_hist, cpu);
		break;
#endif

#ifdef CONFIG_PREEMPT_OFF_HIST
	case PREEMPT_LATENCY:
		my_hist = &per_cpu(preempt_off_hist, cpu);
		break;
#endif

#if defined(CONFIG_PREEMPT_OFF_HIST) && defined(CONFIG_INTERRUPT_OFF_HIST)
	case PREEMPT_INTERRUPT_LATENCY:
		my_hist = &per_cpu(preempt_irqs_off_hist, cpu);
		break;
#endif

	default:
		return;
	}

	if (atomic_read(&my_hist->hist_mode) == 0)
		return;

	if (latency >= MAX_ENTRY_NUM)
		my_hist->beyond_hist_bound_samples++;
	else
		my_hist->hist_array[latency]++;

	if (latency < my_hist->min_lat)
		my_hist->min_lat = latency;
	else if (latency > my_hist->max_lat)
		my_hist->max_lat = latency;

	my_hist->total_samples++;
	my_hist->accumulate_lat += latency;
	return;
}

static void *l_start(struct seq_file *m, loff_t *pos)
{
	loff_t *index_ptr = kmalloc(sizeof(loff_t), GFP_KERNEL);
	loff_t index = *pos;
	struct hist_data *my_hist = m->private;

	if (!index_ptr)
		return NULL;

	if (index == 0) {
		char avgstr[32];

		atomic_dec(&my_hist->hist_mode);
		if (likely(my_hist->total_samples)) {
			unsigned long avg = (unsigned long)
			    div64_u64(my_hist->accumulate_lat,
			    my_hist->total_samples);
			sprintf(avgstr, "%lu", avg);
		} else
			strcpy(avgstr, "<undef>");

		seq_printf(m, "#Minimum latency: %lu microseconds.\n"
			   "#Average latency: %s microseconds.\n"
			   "#Maximum latency: %lu microseconds.\n"
			   "#Total samples: %llu\n"
			   "#There are %llu samples greater or equal"
			   " than %d microseconds\n"
			   "#usecs\t%16s\n"
			   , my_hist->min_lat
			   , avgstr
			   , my_hist->max_lat
			   , my_hist->total_samples
			   , my_hist->beyond_hist_bound_samples
			   , MAX_ENTRY_NUM, "samples");
	}
	if (index >= MAX_ENTRY_NUM)
		return NULL;

	*index_ptr = index;
	return index_ptr;
}

static void *l_next(struct seq_file *m, void *p, loff_t *pos)
{
	loff_t *index_ptr = p;
	struct hist_data *my_hist = m->private;

	if (++*pos >= MAX_ENTRY_NUM) {
		atomic_inc(&my_hist->hist_mode);
		return NULL;
	}
	*index_ptr = *pos;
	return index_ptr;
}

static void l_stop(struct seq_file *m, void *p)
{
	kfree(p);
}

static int l_show(struct seq_file *m, void *p)
{
	int index = *(loff_t *) p;
	struct hist_data *my_hist = m->private;

	seq_printf(m, "%5d\t%16llu\n", index, my_hist->hist_array[index]);
	return 0;
}

static struct seq_operations latency_hist_seq_op = {
	.start = l_start,
	.next  = l_next,
	.stop  = l_stop,
	.show  = l_show
};

static int latency_hist_open(struct inode *inode, struct file *file)
{
	int ret;

	ret = seq_open(file, &latency_hist_seq_op);
	if (!ret) {
		struct seq_file *seq = file->private_data;
		seq->private = inode->i_private;
	}
	return ret;
}

static struct file_operations latency_hist_fops = {
	.open = latency_hist_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};

static void hist_reset(struct hist_data *hist)
{
	atomic_dec(&hist->hist_mode);

	memset(hist->hist_array, 0, sizeof(hist->hist_array));
	hist->beyond_hist_bound_samples = 0ULL;
	hist->min_lat = 0xFFFFFFFFUL;
	hist->max_lat = 0UL;
	hist->total_samples = 0ULL;
	hist->accumulate_lat = 0ULL;

	atomic_inc(&hist->hist_mode);
}

ssize_t latency_hist_reset(struct file *file, const char __user *a,
			   size_t size, loff_t *off)
{
	int cpu;
	struct hist_data *hist;
	int latency_type = (long)file->private_data;

	switch (latency_type) {

#ifdef CONFIG_PREEMPT_OFF_HIST
	case PREEMPT_LATENCY:
		for_each_online_cpu(cpu) {
			hist = &per_cpu(preempt_off_hist, cpu);
			hist_reset(hist);
		}
		break;
#endif

#ifdef CONFIG_INTERRUPT_OFF_HIST
	case INTERRUPT_LATENCY:
		for_each_online_cpu(cpu) {
			hist = &per_cpu(interrupt_off_hist, cpu);
			hist_reset(hist);
		}
		break;
#endif

#if defined(CONFIG_INTERRUPT_OFF_HIST) && defined(CONFIG_PREEMPT_OFF_HIST)
	case PREEMPT_INTERRUPT_LATENCY:
		for_each_online_cpu(cpu) {
			hist = &per_cpu(preempt_irqs_off_hist, cpu);
			hist_reset(hist);
		}
		break;
#endif
	}

	return size;
}

static struct file_operations latency_hist_reset_fops = {
	.open = tracing_open_generic,
	.write = latency_hist_reset,
};

#if defined(CONFIG_INTERRUPT_OFF_HIST) || defined(CONFIG_PREEMPT_OFF_HIST)
#ifdef CONFIG_INTERRUPT_OFF_HIST
static DEFINE_PER_CPU(cycles_t, hist_irqsoff_start);
static DEFINE_PER_CPU(int, hist_irqsoff_tracing);
#endif
#ifdef CONFIG_PREEMPT_OFF_HIST
static DEFINE_PER_CPU(cycles_t, hist_preemptoff_start);
static DEFINE_PER_CPU(int, hist_preemptoff_tracing);
#endif
#if defined(CONFIG_INTERRUPT_OFF_HIST) && defined(CONFIG_PREEMPT_OFF_HIST)
static DEFINE_PER_CPU(cycles_t, hist_preemptirqsoff_start);
static DEFINE_PER_CPU(int, hist_preemptirqsoff_tracing);
#endif

notrace void tracing_hist_preempt_start(void)
{
	cycle_t uninitialized_var(start);
	int start_set = 0;
	int cpu;

	if (!preempt_count() && !irqs_disabled())
		return;

	/* cpu is only used if we are in atomic */
	cpu = raw_smp_processor_id();

#ifdef CONFIG_INTERRUPT_OFF_HIST
	if (irqs_disabled() &&
	    !per_cpu(hist_irqsoff_tracing, cpu)) {
		per_cpu(hist_irqsoff_tracing, cpu) = 1;
		start_set++;
		start = ftrace_now(cpu);
		per_cpu(hist_irqsoff_start, cpu) = start;
	}
#endif

#ifdef CONFIG_PREEMPT_OFF_HIST
	if (preempt_count() &&
	    !per_cpu(hist_preemptoff_tracing, cpu)) {
		per_cpu(hist_preemptoff_tracing, cpu) = 1;
		if (1 || !(start_set++))
			start = ftrace_now(cpu);
		per_cpu(hist_preemptoff_start, cpu) = start;

	}
#endif

#if defined(CONFIG_INTERRUPT_OFF_HIST) && defined(CONFIG_PREEMPT_OFF_HIST)
	if (per_cpu(hist_irqsoff_tracing, cpu) &&
	    per_cpu(hist_preemptoff_tracing, cpu) &&
	    !per_cpu(hist_preemptirqsoff_tracing, cpu)) {
		per_cpu(hist_preemptirqsoff_tracing, cpu) = 1;
		if (1 || !(start_set))
			start = ftrace_now(cpu);
		per_cpu(hist_preemptirqsoff_start, cpu) = start;
	}
#endif
}

notrace void tracing_hist_preempt_stop(int irqs_on)
{
	long latency;
	cycle_t start;
	cycle_t uninitialized_var(stop);
	int stop_set = 0;
	int cpu;

	/* irqs_on == TRACE_STOP if we must stop tracing. */

	/* cpu is only used if we are in atomic */
	cpu = raw_smp_processor_id();

#ifdef CONFIG_INTERRUPT_OFF_HIST
	if (irqs_on  &&
	    per_cpu(hist_irqsoff_tracing, cpu)) {
		stop = ftrace_now(cpu);
		stop_set++;
		start = per_cpu(hist_irqsoff_start, cpu);
		latency = (long)nsecs_to_usecs(stop - start);
		if (latency > 1000000) {
			printk(KERN_WARNING "%s(%d): latency = %ld (%lu)\n",
				__FILE__, __LINE__, latency, latency);
			printk(KERN_WARNING "        start = %Ld, stop = %Ld\n",
				start, stop);
		}
		barrier();
		per_cpu(hist_irqsoff_tracing, cpu) = 0;
		latency_hist(INTERRUPT_LATENCY, cpu, latency);
	}
#endif

#ifdef CONFIG_PREEMPT_OFF_HIST
	if ((!irqs_on || irqs_on == TRACE_STOP) &&
	    per_cpu(hist_preemptoff_tracing, cpu)) {
		WARN_ON(!preempt_count());
		if (1 || !(stop_set++))
			stop = ftrace_now(cpu);
		start = per_cpu(hist_preemptoff_start, cpu);
		latency = (long)nsecs_to_usecs(stop - start);
		if (latency > 1000000) {
			printk(KERN_WARNING "%s(%d): latency = %ld (%lu)\n",
				__FILE__, __LINE__, latency, latency);
			printk(KERN_WARNING "        start = %Ld, stop = %Ld\n",
				start, stop);
		}
		barrier();
		per_cpu(hist_preemptoff_tracing, cpu) = 0;
		latency_hist(PREEMPT_LATENCY, cpu, latency);
	}
#endif

#if defined(CONFIG_INTERRUPT_OFF_HIST) && defined(CONFIG_PREEMPT_OFF_HIST)
	if ((!per_cpu(hist_irqsoff_tracing, cpu) ||
	    !per_cpu(hist_preemptoff_tracing, cpu)) &&
	    per_cpu(hist_preemptirqsoff_tracing, cpu)) {
		WARN_ON(!preempt_count() && !irqs_disabled());
		if (1 || !stop_set)
			stop = ftrace_now(cpu);
		start = per_cpu(hist_preemptirqsoff_start, cpu);
		latency = (long)nsecs_to_usecs(stop - start);
		if (latency > 1000000) {
			printk(KERN_WARNING "%s(%d): latency = %ld (%lu)\n",
				__FILE__, __LINE__, latency, latency);
			printk(KERN_WARNING "        start = %Ld, stop = %Ld\n",
				start, stop);
		}
		barrier();
		per_cpu(hist_preemptirqsoff_tracing, cpu) = 0;
		latency_hist(PREEMPT_INTERRUPT_LATENCY, cpu, latency);
	}
#endif
}
#endif

static __init int latency_hist_init(void)
{
	struct dentry *latency_hist_root = NULL;
	struct dentry *dentry;
	struct dentry *entry;
	int i = 0, len = 0;
	struct hist_data *my_hist;
	char name[64];

	dentry = tracing_init_dentry();

	latency_hist_root =
		debugfs_create_dir(latency_hist_dir_root, dentry);

#ifdef CONFIG_INTERRUPT_OFF_HIST
	dentry = debugfs_create_dir(interrupt_off_hist_dir,
				    latency_hist_root);
	for_each_possible_cpu(i) {
		len = sprintf(name, "CPU%d", i);
		name[len] = '\0';
		entry = debugfs_create_file(name, 0444, dentry,
					    &per_cpu(interrupt_off_hist, i),
					    &latency_hist_fops);
		my_hist = &per_cpu(interrupt_off_hist, i);
		atomic_set(&my_hist->hist_mode, 1);
		my_hist->min_lat = 0xFFFFFFFFUL;
	}
	entry = debugfs_create_file("reset", 0444, dentry,
				    (void *)INTERRUPT_LATENCY,
				    &latency_hist_reset_fops);
#endif

#ifdef CONFIG_PREEMPT_OFF_HIST
	dentry = debugfs_create_dir(preempt_off_hist_dir,
				    latency_hist_root);
	for_each_possible_cpu(i) {
		len = sprintf(name, "CPU%d", i);
		name[len] = '\0';
		entry = debugfs_create_file(name, 0444, dentry,
					    &per_cpu(preempt_off_hist, i),
					    &latency_hist_fops);
		my_hist = &per_cpu(preempt_off_hist, i);
		atomic_set(&my_hist->hist_mode, 1);
		my_hist->min_lat = 0xFFFFFFFFUL;
	}
	entry = debugfs_create_file("reset", 0444, dentry,
				    (void *)PREEMPT_LATENCY,
				    &latency_hist_reset_fops);
#endif

#if defined(CONFIG_INTERRUPT_OFF_HIST) && defined(CONFIG_PREEMPT_OFF_HIST)
	dentry = debugfs_create_dir(preempt_irqs_off_hist_dir,
				    latency_hist_root);
	for_each_possible_cpu(i) {
		len = sprintf(name, "CPU%d", i);
		name[len] = '\0';
		entry = debugfs_create_file(name, 0444, dentry,
					    &per_cpu(preempt_off_hist, i),
					    &latency_hist_fops);
		my_hist = &per_cpu(preempt_irqs_off_hist, i);
		atomic_set(&my_hist->hist_mode, 1);
		my_hist->min_lat = 0xFFFFFFFFUL;
	}
	entry = debugfs_create_file("reset", 0444, dentry,
				    (void *)PREEMPT_INTERRUPT_LATENCY,
				    &latency_hist_reset_fops);
#endif

	return 0;

}

__initcall(latency_hist_init);
