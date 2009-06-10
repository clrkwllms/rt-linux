/*
 * Read-Copy Update tracing for realtime implementation
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
#include <linux/mutex.h>
#include <linux/rcupreempt_trace.h>
#include <linux/debugfs.h>
#include <linux/percpu.h>

static struct mutex rcupreempt_trace_mutex;
static char *rcupreempt_trace_buf;
#define RCUPREEMPT_TRACE_BUF_SIZE 4096

static DEFINE_PER_CPU(struct rcupreempt_trace, trace_data);

#ifdef CONFIG_PREEMPT_RCU_BOOST
#define RCUPREEMPT_BOOST_TRACE_BUF_SIZE 4096
static char rcupreempt_boost_trace_buf[RCUPREEMPT_BOOST_TRACE_BUF_SIZE];
static DEFINE_PER_CPU(struct preempt_rcu_boost_trace, boost_trace_data);

DEFINE_PREEMPT_RCU_BOOST_MARKER_HANDLER(task_boost_called);
DEFINE_PREEMPT_RCU_BOOST_MARKER_HANDLER(task_boosted);
DEFINE_PREEMPT_RCU_BOOST_MARKER_HANDLER(boost_called);
DEFINE_PREEMPT_RCU_BOOST_MARKER_HANDLER(try_boost);
DEFINE_PREEMPT_RCU_BOOST_MARKER_HANDLER(boosted);
DEFINE_PREEMPT_RCU_BOOST_MARKER_HANDLER(unboost_called);
DEFINE_PREEMPT_RCU_BOOST_MARKER_HANDLER(unboosted);
DEFINE_PREEMPT_RCU_BOOST_MARKER_HANDLER(try_boost_readers);
DEFINE_PREEMPT_RCU_BOOST_MARKER_HANDLER(boost_readers);
DEFINE_PREEMPT_RCU_BOOST_MARKER_HANDLER(try_unboost_readers);
DEFINE_PREEMPT_RCU_BOOST_MARKER_HANDLER(unboost_readers);
DEFINE_PREEMPT_RCU_BOOST_MARKER_HANDLER(over_taken);

static struct preempt_rcu_boost_probe preempt_rcu_boost_probe_array[] =
{
	INIT_PREEMPT_RCU_BOOST_PROBE(task_boost_called),
	INIT_PREEMPT_RCU_BOOST_PROBE(task_boosted),
	INIT_PREEMPT_RCU_BOOST_PROBE(boost_called),
	INIT_PREEMPT_RCU_BOOST_PROBE(try_boost),
	INIT_PREEMPT_RCU_BOOST_PROBE(boosted),
	INIT_PREEMPT_RCU_BOOST_PROBE(unboost_called),
	INIT_PREEMPT_RCU_BOOST_PROBE(unboosted),
	INIT_PREEMPT_RCU_BOOST_PROBE(try_boost_readers),
	INIT_PREEMPT_RCU_BOOST_PROBE(boost_readers),
	INIT_PREEMPT_RCU_BOOST_PROBE(try_unboost_readers),
	INIT_PREEMPT_RCU_BOOST_PROBE(unboost_readers),
	INIT_PREEMPT_RCU_BOOST_PROBE(over_taken)
};

static ssize_t rcuboost_read(struct file *filp, char __user *buffer,
				size_t count, loff_t *ppos)
{
	static DEFINE_MUTEX(mutex);
	int cnt = 0;
	int cpu;
	struct preempt_rcu_boost_trace *prbt;
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
		prbt = &per_cpu(boost_trace_data, cpu);

		task_boost_called += prbt->rbs_stat_task_boost_called;
		task_boosted += prbt->rbs_stat_task_boosted;
		boost_called += prbt->rbs_stat_boost_called;
		try_boost += prbt->rbs_stat_try_boost;
		boosted += prbt->rbs_stat_boosted;
		unboost_called += prbt->rbs_stat_unboost_called;
		unboosted += prbt->rbs_stat_unboosted;
		try_boost_readers += prbt->rbs_stat_try_boost_readers;
		boost_readers += prbt->rbs_stat_boost_readers;
		try_unboost_readers += prbt->rbs_stat_try_boost_readers;
		unboost_readers += prbt->rbs_stat_boost_readers;
		over_taken += prbt->rbs_stat_over_taken;
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
			read_rcu_boost_prio());
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
		return 0;

	return 1;
}

void rcu_trace_boost_destroy(void)
{
	if (rcuboostdir)
		debugfs_remove(rcuboostdir);
	rcuboostdir = NULL;
}

#endif /* CONFIG_PREEMPT_RCU_BOOST */

struct rcupreempt_trace *rcupreempt_trace_cpu(int cpu)
{
	return &per_cpu(trace_data, cpu);
}

void rcupreempt_trace_move2done(struct rcupreempt_trace *trace)
{
	trace->done_length += trace->wait_length;
	trace->done_add += trace->wait_length;
	trace->wait_length = 0;
}
void rcupreempt_trace_move2wait(struct rcupreempt_trace *trace)
{
	trace->wait_length += trace->next_length;
	trace->wait_add += trace->next_length;
	trace->next_length = 0;
}
void rcupreempt_trace_try_flip_1(struct rcupreempt_trace *trace)
{
	atomic_inc(&trace->rcu_try_flip_1);
}
void rcupreempt_trace_try_flip_e1(struct rcupreempt_trace *trace)
{
	atomic_inc(&trace->rcu_try_flip_e1);
}
void rcupreempt_trace_try_flip_i1(struct rcupreempt_trace *trace)
{
	trace->rcu_try_flip_i1++;
}
void rcupreempt_trace_try_flip_ie1(struct rcupreempt_trace *trace)
{
	trace->rcu_try_flip_ie1++;
}
void rcupreempt_trace_try_flip_g1(struct rcupreempt_trace *trace)
{
	trace->rcu_try_flip_g1++;
}
void rcupreempt_trace_try_flip_a1(struct rcupreempt_trace *trace)
{
	trace->rcu_try_flip_a1++;
}
void rcupreempt_trace_try_flip_ae1(struct rcupreempt_trace *trace)
{
	trace->rcu_try_flip_ae1++;
}
void rcupreempt_trace_try_flip_a2(struct rcupreempt_trace *trace)
{
	trace->rcu_try_flip_a2++;
}
void rcupreempt_trace_try_flip_z1(struct rcupreempt_trace *trace)
{
	trace->rcu_try_flip_z1++;
}
void rcupreempt_trace_try_flip_ze1(struct rcupreempt_trace *trace)
{
	trace->rcu_try_flip_ze1++;
}
void rcupreempt_trace_try_flip_z2(struct rcupreempt_trace *trace)
{
	trace->rcu_try_flip_z2++;
}
void rcupreempt_trace_try_flip_m1(struct rcupreempt_trace *trace)
{
	trace->rcu_try_flip_m1++;
}
void rcupreempt_trace_try_flip_me1(struct rcupreempt_trace *trace)
{
	trace->rcu_try_flip_me1++;
}
void rcupreempt_trace_try_flip_m2(struct rcupreempt_trace *trace)
{
	trace->rcu_try_flip_m2++;
}
void rcupreempt_trace_check_callbacks(struct rcupreempt_trace *trace)
{
	trace->rcu_check_callbacks++;
}
void rcupreempt_trace_done_remove(struct rcupreempt_trace *trace)
{
	trace->done_remove += trace->done_length;
	trace->done_length = 0;
}
void rcupreempt_trace_invoke(struct rcupreempt_trace *trace)
{
	atomic_inc(&trace->done_invoked);
}
void rcupreempt_trace_next_add(struct rcupreempt_trace *trace)
{
	trace->next_add++;
	trace->next_length++;
}

DEFINE_RCUPREEMPT_MARKER_HANDLER(rcupreempt_trace_move2done);
DEFINE_RCUPREEMPT_MARKER_HANDLER(rcupreempt_trace_move2wait);
DEFINE_RCUPREEMPT_MARKER_HANDLER(rcupreempt_trace_try_flip_1);
DEFINE_RCUPREEMPT_MARKER_HANDLER(rcupreempt_trace_try_flip_e1);
DEFINE_RCUPREEMPT_MARKER_HANDLER(rcupreempt_trace_try_flip_i1);
DEFINE_RCUPREEMPT_MARKER_HANDLER(rcupreempt_trace_try_flip_ie1);
DEFINE_RCUPREEMPT_MARKER_HANDLER(rcupreempt_trace_try_flip_g1);
DEFINE_RCUPREEMPT_MARKER_HANDLER(rcupreempt_trace_try_flip_a1);
DEFINE_RCUPREEMPT_MARKER_HANDLER(rcupreempt_trace_try_flip_ae1);
DEFINE_RCUPREEMPT_MARKER_HANDLER(rcupreempt_trace_try_flip_a2);
DEFINE_RCUPREEMPT_MARKER_HANDLER(rcupreempt_trace_try_flip_z1);
DEFINE_RCUPREEMPT_MARKER_HANDLER(rcupreempt_trace_try_flip_ze1);
DEFINE_RCUPREEMPT_MARKER_HANDLER(rcupreempt_trace_try_flip_z2);
DEFINE_RCUPREEMPT_MARKER_HANDLER(rcupreempt_trace_try_flip_m1);
DEFINE_RCUPREEMPT_MARKER_HANDLER(rcupreempt_trace_try_flip_me1);
DEFINE_RCUPREEMPT_MARKER_HANDLER(rcupreempt_trace_try_flip_m2);
DEFINE_RCUPREEMPT_MARKER_HANDLER(rcupreempt_trace_check_callbacks);
DEFINE_RCUPREEMPT_MARKER_HANDLER(rcupreempt_trace_done_remove);
DEFINE_RCUPREEMPT_MARKER_HANDLER(rcupreempt_trace_invoke);
DEFINE_RCUPREEMPT_MARKER_HANDLER(rcupreempt_trace_next_add);

static struct rcupreempt_probe_data rcupreempt_probe_array[] =
{
	INIT_RCUPREEMPT_PROBE(rcupreempt_trace_move2done),
	INIT_RCUPREEMPT_PROBE(rcupreempt_trace_move2wait),
	INIT_RCUPREEMPT_PROBE(rcupreempt_trace_try_flip_1),
	INIT_RCUPREEMPT_PROBE(rcupreempt_trace_try_flip_e1),
	INIT_RCUPREEMPT_PROBE(rcupreempt_trace_try_flip_i1),
	INIT_RCUPREEMPT_PROBE(rcupreempt_trace_try_flip_ie1),
	INIT_RCUPREEMPT_PROBE(rcupreempt_trace_try_flip_g1),
	INIT_RCUPREEMPT_PROBE(rcupreempt_trace_try_flip_a1),
	INIT_RCUPREEMPT_PROBE(rcupreempt_trace_try_flip_ae1),
	INIT_RCUPREEMPT_PROBE(rcupreempt_trace_try_flip_a2),
	INIT_RCUPREEMPT_PROBE(rcupreempt_trace_try_flip_z1),
	INIT_RCUPREEMPT_PROBE(rcupreempt_trace_try_flip_ze1),
	INIT_RCUPREEMPT_PROBE(rcupreempt_trace_try_flip_z2),
	INIT_RCUPREEMPT_PROBE(rcupreempt_trace_try_flip_m1),
	INIT_RCUPREEMPT_PROBE(rcupreempt_trace_try_flip_me1),
	INIT_RCUPREEMPT_PROBE(rcupreempt_trace_try_flip_m2),
	INIT_RCUPREEMPT_PROBE(rcupreempt_trace_check_callbacks),
	INIT_RCUPREEMPT_PROBE(rcupreempt_trace_done_remove),
	INIT_RCUPREEMPT_PROBE(rcupreempt_trace_invoke),
	INIT_RCUPREEMPT_PROBE(rcupreempt_trace_next_add)
};

static void rcupreempt_trace_sum(struct rcupreempt_trace *sp)
{
	struct rcupreempt_trace *cp;
	int cpu;

	memset(sp, 0, sizeof(*sp));
	for_each_possible_cpu(cpu) {
		cp = rcupreempt_trace_cpu(cpu);
		sp->next_length += cp->next_length;
		sp->next_add += cp->next_add;
		sp->wait_length += cp->wait_length;
		sp->wait_add += cp->wait_add;
		sp->done_length += cp->done_length;
		sp->done_add += cp->done_add;
		sp->done_remove += cp->done_remove;
		atomic_set(&sp->done_invoked, atomic_read(&cp->done_invoked));
		sp->rcu_check_callbacks += cp->rcu_check_callbacks;
		atomic_set(&sp->rcu_try_flip_1,
			   atomic_read(&cp->rcu_try_flip_1));
		atomic_set(&sp->rcu_try_flip_e1,
			   atomic_read(&cp->rcu_try_flip_e1));
		sp->rcu_try_flip_i1 += cp->rcu_try_flip_i1;
		sp->rcu_try_flip_ie1 += cp->rcu_try_flip_ie1;
		sp->rcu_try_flip_g1 += cp->rcu_try_flip_g1;
		sp->rcu_try_flip_a1 += cp->rcu_try_flip_a1;
		sp->rcu_try_flip_ae1 += cp->rcu_try_flip_ae1;
		sp->rcu_try_flip_a2 += cp->rcu_try_flip_a2;
		sp->rcu_try_flip_z1 += cp->rcu_try_flip_z1;
		sp->rcu_try_flip_ze1 += cp->rcu_try_flip_ze1;
		sp->rcu_try_flip_z2 += cp->rcu_try_flip_z2;
		sp->rcu_try_flip_m1 += cp->rcu_try_flip_m1;
		sp->rcu_try_flip_me1 += cp->rcu_try_flip_me1;
		sp->rcu_try_flip_m2 += cp->rcu_try_flip_m2;
	}
}

static ssize_t rcustats_read(struct file *filp, char __user *buffer,
				size_t count, loff_t *ppos)
{
	struct rcupreempt_trace trace;
	ssize_t bcount;
	int cnt = 0;

	rcupreempt_trace_sum(&trace);
	mutex_lock(&rcupreempt_trace_mutex);
	snprintf(&rcupreempt_trace_buf[cnt], RCUPREEMPT_TRACE_BUF_SIZE - cnt,
		 "ggp=%ld rcc=%ld\n",
		 rcu_batches_completed(),
		 trace.rcu_check_callbacks);
	snprintf(&rcupreempt_trace_buf[cnt], RCUPREEMPT_TRACE_BUF_SIZE - cnt,
		 "na=%ld nl=%ld wa=%ld wl=%ld da=%ld dl=%ld dr=%ld di=%d\n"
		 "1=%d e1=%d i1=%ld ie1=%ld g1=%ld a1=%ld ae1=%ld a2=%ld\n"
		 "z1=%ld ze1=%ld z2=%ld m1=%ld me1=%ld m2=%ld\n",

		 trace.next_add, trace.next_length,
		 trace.wait_add, trace.wait_length,
		 trace.done_add, trace.done_length,
		 trace.done_remove, atomic_read(&trace.done_invoked),
		 atomic_read(&trace.rcu_try_flip_1),
		 atomic_read(&trace.rcu_try_flip_e1),
		 trace.rcu_try_flip_i1, trace.rcu_try_flip_ie1,
		 trace.rcu_try_flip_g1,
		 trace.rcu_try_flip_a1, trace.rcu_try_flip_ae1,
			 trace.rcu_try_flip_a2,
		 trace.rcu_try_flip_z1, trace.rcu_try_flip_ze1,
			 trace.rcu_try_flip_z2,
		 trace.rcu_try_flip_m1, trace.rcu_try_flip_me1,
			trace.rcu_try_flip_m2);
	bcount = simple_read_from_buffer(buffer, count, ppos,
			rcupreempt_trace_buf, strlen(rcupreempt_trace_buf));
	mutex_unlock(&rcupreempt_trace_mutex);
	return bcount;
}

static ssize_t rcugp_read(struct file *filp, char __user *buffer,
				size_t count, loff_t *ppos)
{
	long oldgp = rcu_batches_completed();
	ssize_t bcount;

	mutex_lock(&rcupreempt_trace_mutex);
	synchronize_rcu();
	snprintf(rcupreempt_trace_buf, RCUPREEMPT_TRACE_BUF_SIZE,
		"oldggp=%ld  newggp=%ld\n", oldgp, rcu_batches_completed());
	bcount = simple_read_from_buffer(buffer, count, ppos,
			rcupreempt_trace_buf, strlen(rcupreempt_trace_buf));
	mutex_unlock(&rcupreempt_trace_mutex);
	return bcount;
}

static ssize_t rcuctrs_read(struct file *filp, char __user *buffer,
				size_t count, loff_t *ppos)
{
	int cnt = 0;
	int cpu;
	int f = rcu_batches_completed() & 0x1;
	ssize_t bcount;

	mutex_lock(&rcupreempt_trace_mutex);

	cnt += snprintf(&rcupreempt_trace_buf[cnt], RCUPREEMPT_TRACE_BUF_SIZE,
				"CPU last cur F M\n");
	for_each_online_cpu(cpu) {
		int *flipctr = rcupreempt_flipctr(cpu);
		cnt += snprintf(&rcupreempt_trace_buf[cnt],
				RCUPREEMPT_TRACE_BUF_SIZE - cnt,
					"%3d %4d %3d %d %d\n",
			       cpu,
			       flipctr[!f],
			       flipctr[f],
			       rcupreempt_flip_flag(cpu),
			       rcupreempt_mb_flag(cpu));
	}
	cnt += snprintf(&rcupreempt_trace_buf[cnt],
			RCUPREEMPT_TRACE_BUF_SIZE - cnt,
			"ggp = %ld, state = %s\n",
			rcu_batches_completed(),
			rcupreempt_try_flip_state_name());
	cnt += snprintf(&rcupreempt_trace_buf[cnt],
			RCUPREEMPT_TRACE_BUF_SIZE - cnt,
			"\n");
	bcount = simple_read_from_buffer(buffer, count, ppos,
			rcupreempt_trace_buf, strlen(rcupreempt_trace_buf));
	mutex_unlock(&rcupreempt_trace_mutex);
	return bcount;
}

static struct file_operations rcustats_fops = {
	.owner = THIS_MODULE,
	.read = rcustats_read,
};

static struct file_operations rcugp_fops = {
	.owner = THIS_MODULE,
	.read = rcugp_read,
};

static struct file_operations rcuctrs_fops = {
	.owner = THIS_MODULE,
	.read = rcuctrs_read,
};

static struct dentry *rcudir, *statdir, *ctrsdir, *gpdir;
static int rcupreempt_debugfs_init(void)
{
	rcudir = debugfs_create_dir("rcu", NULL);
	if (!rcudir)
		goto out;
	statdir = debugfs_create_file("rcustats", 0444, rcudir,
						NULL, &rcustats_fops);
	if (!statdir)
		goto free_out;

	gpdir = debugfs_create_file("rcugp", 0444, rcudir, NULL, &rcugp_fops);
	if (!gpdir)
		goto free_out;

	ctrsdir = debugfs_create_file("rcuctrs", 0444, rcudir,
						NULL, &rcuctrs_fops);
	if (!ctrsdir)
		goto free_out;

#ifdef CONFIG_PREEMPT_RCU_BOOST
	if (!rcu_trace_boost_create(rcudir))
		goto free_out;
#endif /* CONFIG_PREEMPT_RCU_BOOST */
	return 0;
free_out:
	if (ctrsdir)
		debugfs_remove(ctrsdir);
	if (statdir)
		debugfs_remove(statdir);
	if (gpdir)
		debugfs_remove(gpdir);
	debugfs_remove(rcudir);
out:
	return 1;
}

static int __init rcupreempt_trace_init(void)
{
	int ret;
	int i;

	for (i = 0; i < ARRAY_SIZE(rcupreempt_probe_array); i++) {
		struct rcupreempt_probe_data *p = &rcupreempt_probe_array[i];
		ret = marker_probe_register(p->name, p->format,
							p->probe_func, p);
		if (ret)
			printk(KERN_INFO "Unable to register rcupreempt \
				probe %s\n", rcupreempt_probe_array[i].name);
		ret = marker_arm(p->name);
		if (ret)
			printk(KERN_INFO "Unable to arm rcupreempt probe %s\n",
				p->name);
	}
	printk(KERN_INFO "RCU Preempt markers registered\n");

#ifdef CONFIG_PREEMPT_RCU_BOOST
	for (i = 0; i < ARRAY_SIZE(preempt_rcu_boost_probe_array); i++) {
		struct preempt_rcu_boost_probe *p = \
					&preempt_rcu_boost_probe_array[i];
		ret = marker_probe_register(p->name, p->format,
					    p->probe_func, p);
		if (ret)
			printk(KERN_INFO "Unable to register Preempt RCU Boost \
			probe %s\n", preempt_rcu_boost_probe_array[i].name);
		ret = marker_arm(p->name);
		if (ret)
			printk(KERN_INFO "Unable to arm Preempt RCU Boost \
				markers %s\n", p->name);
}
#endif /* CONFIG_PREEMPT_RCU_BOOST */

	mutex_init(&rcupreempt_trace_mutex);
	rcupreempt_trace_buf = kmalloc(RCUPREEMPT_TRACE_BUF_SIZE, GFP_KERNEL);
	if (!rcupreempt_trace_buf)
		return 1;
	ret = rcupreempt_debugfs_init();
	if (ret)
		kfree(rcupreempt_trace_buf);
	return ret;
}

static void __exit rcupreempt_trace_cleanup(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(rcupreempt_probe_array); i++)
		marker_probe_unregister(rcupreempt_probe_array[i].name);
	printk(KERN_INFO "RCU Preempt markers unregistered\n");

#ifdef CONFIG_PREEMPT_RCU_BOOST
	rcu_trace_boost_destroy();
	for (i = 0; i < ARRAY_SIZE(preempt_rcu_boost_probe_array); i++)
		marker_probe_unregister(preempt_rcu_boost_probe_array[i].name);
	printk(KERN_INFO "Preempt RCU Boost markers unregistered\n");
#endif /* CONFIG_PREEMPT_RCU_BOOST */
	debugfs_remove(statdir);
	debugfs_remove(gpdir);
	debugfs_remove(ctrsdir);
	debugfs_remove(rcudir);
	kfree(rcupreempt_trace_buf);
}

MODULE_LICENSE("GPL");

module_init(rcupreempt_trace_init);
module_exit(rcupreempt_trace_cleanup);
