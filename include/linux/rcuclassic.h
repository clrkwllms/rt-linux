/*
 * Read-Copy Update mechanism for mutual exclusion (classic version)
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
 * Copyright IBM Corporation, 2001
 *
 * Author: Dipankar Sarma <dipankar@in.ibm.com>
 *
 * Based on the original work by Paul McKenney <paulmck@us.ibm.com>
 * and inputs from Rusty Russell, Andrea Arcangeli and Andi Kleen.
 * Papers:
 * http://www.rdrop.com/users/paulmck/paper/rclockpdcsproof.pdf
 * http://lse.sourceforge.net/locking/rclock_OLS.2001.05.01c.sc.pdf (OLS2001)
 *
 * For detailed explanation of Read-Copy Update mechanism see -
 * 		Documentation/RCU
 *
 */

#ifndef __LINUX_RCUCLASSIC_H
#define __LINUX_RCUCLASSIC_H

#ifdef __KERNEL__

#include <linux/cache.h>
#include <linux/spinlock.h>
#include <linux/threads.h>
#include <linux/percpu.h>
#include <linux/cpumask.h>
#include <linux/seqlock.h>

DECLARE_PER_CPU(int, rcu_data_bh_passed_quiesc);

/*
 * Increment the bottom-half quiescent state counter.
 * The counter is a bit degenerated: We do not need to know
 * how many quiescent states passed, just if there was at least
 * one since the start of the grace period. Thus just a flag.
 */
static inline void rcu_bh_qsctr_inc(int cpu)
{
	per_cpu(rcu_data_bh_passed_quiesc, cpu) = 1;
}

#define __rcu_read_lock() \
	do { \
		preempt_disable(); \
		__acquire(RCU); \
	} while (0)
#define __rcu_read_unlock() \
	do { \
		__release(RCU); \
		preempt_enable(); \
	} while (0)
#define __rcu_read_lock_bh() \
	do { \
		local_bh_disable(); \
		__acquire(RCU_BH); \
	} while (0)
#define __rcu_read_unlock_bh() \
	do { \
		__release(RCU_BH); \
		local_bh_enable(); \
	} while (0)

#define __synchronize_sched() synchronize_rcu()

#define rcu_advance_callbacks_rt(cpu, user) do { } while (0)
#define rcu_check_callbacks_rt(cpu, user) do { } while (0)
#define rcu_init_rt() do { } while (0)
#define rcu_needs_cpu_rt(cpu) 0
#define rcu_offline_cpu_rt(cpu)
#define rcu_online_cpu_rt(cpu)
#define rcu_pending_rt(cpu) 0
#define rcu_process_callbacks_rt(unused) do { } while (0)
#define rcu_enter_nohz()	do { } while (0)
#define rcu_exit_nohz()		do { } while (0)
#define rcu_preempt_boost_init() do { } while (0)

extern void FASTCALL(call_rcu_classic(struct rcu_head *head,
		     void (*func)(struct rcu_head *head)));

struct softirq_action;
extern void rcu_process_callbacks(struct softirq_action *unused);

#endif /* __KERNEL__ */
#endif /* __LINUX_RCUCLASSIC_H */
