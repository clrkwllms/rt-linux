/*
 * Read-Copy Update mechanism for mutual exclusion (RT implementation)
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
 * Copyright (C) IBM Corporation, 2006
 *
 * Author:  Paul McKenney <paulmck@us.ibm.com>
 *
 * Based on the original work by Paul McKenney <paul.mckenney@us.ibm.com>
 * and inputs from Rusty Russell, Andrea Arcangeli and Andi Kleen.
 * Papers:
 * http://www.rdrop.com/users/paulmck/paper/rclockpdcsproof.pdf
 * http://lse.sourceforge.net/locking/rclock_OLS.2001.05.01c.sc.pdf (OLS2001)
 *
 * For detailed explanation of Read-Copy Update mechanism see -
 * 		http://lse.sourceforge.net/locking/rcupdate.html
 *
 */

#ifndef __LINUX_RCUPREEMPT_TRACE_H
#define __LINUX_RCUPREEMPT_TRACE_H

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/kernel.h>

#include <asm/atomic.h>

/*
 * PREEMPT_RCU data structures.
 */

struct rcupreempt_trace {
	long		next_length;
	long		next_add;
	long		wait_length;
	long		wait_add;
	long		done_length;
	long		done_add;
	long		done_remove;
	atomic_t	done_invoked;
	long		rcu_check_callbacks;
	atomic_t	rcu_try_flip_1;
	atomic_t	rcu_try_flip_e1;
	long		rcu_try_flip_i1;
	long		rcu_try_flip_ie1;
	long		rcu_try_flip_g1;
	long		rcu_try_flip_a1;
	long		rcu_try_flip_ae1;
	long		rcu_try_flip_a2;
	long		rcu_try_flip_z1;
	long		rcu_try_flip_ze1;
	long		rcu_try_flip_z2;
	long		rcu_try_flip_m1;
	long		rcu_try_flip_me1;
	long		rcu_try_flip_m2;
};

struct rcupreempt_probe_data {
	const char *name;
	const char *format;
	marker_probe_func *probe_func;
};

#define DEFINE_RCUPREEMPT_MARKER_HANDLER(rcupreempt_trace_worker) \
void rcupreempt_trace_worker##_callback(const struct marker *mdata, \
				void *private_data, const char *format, ...) \
{ \
	struct rcupreempt_trace *trace; \
	trace = (&per_cpu(trace_data, smp_processor_id())); \
	rcupreempt_trace_worker(trace); \
}

#define INIT_RCUPREEMPT_PROBE(rcupreempt_trace_worker) \
{ \
	.name = __stringify(rcupreempt_trace_worker), \
	.probe_func = rcupreempt_trace_worker##_callback \
}

extern int *rcupreempt_flipctr(int cpu);
extern long rcupreempt_data_completed(void);
extern int rcupreempt_flip_flag(int cpu);
extern int rcupreempt_mb_flag(int cpu);
extern char *rcupreempt_try_flip_state_name(void);

#ifdef CONFIG_PREEMPT_RCU_BOOST
struct preempt_rcu_boost_trace {
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
};

#define DEFINE_PREEMPT_RCU_BOOST_MARKER_HANDLER(preempt_rcu_boost_var) \
void preempt_rcu_boost_var##_callback(const struct marker *mdata, \
				void *private_data, const char *format, ...) \
{ \
	struct preempt_rcu_boost_trace *boost_trace; \
	boost_trace = (&per_cpu(boost_trace_data, smp_processor_id())); \
	boost_trace->rbs_stat_##preempt_rcu_boost_var++; \
}

struct preempt_rcu_boost_probe {
	const char *name;
	const char *format;
	marker_probe_func *probe_func;
};

#define INIT_PREEMPT_RCU_BOOST_PROBE(preempt_rcu_boost_probe_worker) \
{ \
	.name = __stringify(preempt_rcu_boost_probe_worker), \
	.probe_func = preempt_rcu_boost_probe_worker##_callback \
}

extern int read_rcu_boost_prio(void);
#endif /* CONFIG_PREEMPT_RCU_BOOST */

#endif /* __KERNEL__ */
#endif /* __LINUX_RCUPREEMPT_TRACE_H */
