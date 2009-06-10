/*
 * kernel/trace/trace_hist.h
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
#ifndef _LIB_TRACING_TRACER_HIST_H_
#define _LIB_TRACING_TRACER_HIST_H_

#if defined(CONFIG_INTERRUPT_OFF_HIST) || defined(CONFIG_PREEMPT_OFF_HIST)
# define TRACE_STOP 2
void tracing_hist_preempt_start(void);
void tracing_hist_preempt_stop(int irqs_on);
#else
# define tracing_hist_preempt_start() do { } while (0)
# define tracing_hist_preempt_stop(irqs_off) do { } while (0)
#endif

#ifdef CONFIG_WAKEUP_LATENCY_HIST
void tracing_hist_wakeup_start(struct task_struct *p,
			       struct task_struct *curr);
void tracing_hist_wakeup_stop(struct task_struct *next);
extern int tracing_wakeup_hist;
#else
# define tracing_hist_wakeup_start(p, curr) do { } while (0)
# define tracing_hist_wakeup_stop(next) do { } while (0)
# define tracing_wakeup_hist 0
#endif

#endif /* ifndef _LIB_TRACING_TRACER_HIST_H_ */
