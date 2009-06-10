/*
 * trace task events
 *
 * Copyright (C) 2007 Steven Rostedt <srostedt@redhat.com>
 *
 * Based on code from the latency_tracer, that is:
 *
 *  Copyright (C) 2004-2006 Ingo Molnar
 *  Copyright (C) 2004 William Lee Irwin III
 */
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/debugfs.h>
#include <linux/kallsyms.h>
#include <linux/uaccess.h>
#include <linux/ftrace.h>

#include "trace.h"

static struct trace_array __read_mostly	*events_trace;
static int __read_mostly	tracer_enabled;
static atomic_t			event_ref;

static void event_reset(struct trace_array *tr)
{
	struct trace_array_cpu *data;
	int cpu;

	for_each_possible_cpu(cpu) {
		data = tr->data[cpu];
		tracing_reset(data);
	}

	tr->time_start = ftrace_now(raw_smp_processor_id());
}

/* HACK */
void notrace
sys_call(unsigned long nr, unsigned long p1, unsigned long p2, unsigned long p3)
{
	struct trace_array *tr;
	struct trace_array_cpu *data;
	unsigned long flags;
	unsigned long ip;
	int cpu;

	if (!tracer_enabled)
		return;

	tr = events_trace;
	local_irq_save(flags);
	cpu = raw_smp_processor_id();
	data = tr->data[cpu];

	atomic_inc(&data->disabled);
	if (atomic_read(&data->disabled) != 1)
		goto out;

	ip = CALLER_ADDR0;

	tracing_event_syscall(tr, data, flags, ip, nr, p1, p2, p3);

 out:
	atomic_dec(&data->disabled);
	local_irq_restore(flags);
}

#if defined(CONFIG_COMPAT) && defined(CONFIG_X86)
void notrace
sys_ia32_call(unsigned long nr, unsigned long p1, unsigned long p2,
	      unsigned long p3)
{
	struct trace_array *tr;
	struct trace_array_cpu *data;
	unsigned long flags;
	unsigned long ip;
	int cpu;

	if (!tracer_enabled)
		return;

	tr = events_trace;
	local_irq_save(flags);
	cpu = raw_smp_processor_id();
	data = tr->data[cpu];

	atomic_inc(&data->disabled);
	if (atomic_read(&data->disabled) != 1)
		goto out;

	ip = CALLER_ADDR0;
	tracing_event_syscall(tr, data, flags, ip, nr | 0x80000000, p1, p2, p3);

 out:
	atomic_dec(&data->disabled);
	local_irq_restore(flags);
}
#endif

void notrace
sys_ret(unsigned long ret)
{
	struct trace_array *tr;
	struct trace_array_cpu *data;
	unsigned long flags;
	unsigned long ip;
	int cpu;

	if (!tracer_enabled)
		return;

	tr = events_trace;
	local_irq_save(flags);
	cpu = raw_smp_processor_id();
	data = tr->data[cpu];

	atomic_inc(&data->disabled);
	if (atomic_read(&data->disabled) != 1)
		goto out;

	ip = CALLER_ADDR0;
	tracing_event_sysret(tr, data, flags, ip, ret);

 out:
	atomic_dec(&data->disabled);
	local_irq_restore(flags);
}

#define getarg(arg, ap) arg = va_arg(ap, typeof(arg));

static void
event_irq_callback(void *probe_data, void *call_data,
		   const char *format, va_list *args)
{
	struct trace_array *tr = probe_data;
	struct trace_array_cpu *data;
	unsigned long ip, flags;
	int irq, user, cpu;
	long disable;

	if (!tracer_enabled)
		return;

	getarg(irq, *args);
	getarg(user, *args);
	getarg(ip, *args);

	/* interrupts should be off, we are in an interrupt */
	cpu = smp_processor_id();
	data = tr->data[cpu];

	disable = atomic_inc_return(&data->disabled);
	if (disable != 1)
		goto out;

	local_save_flags(flags);
	tracing_event_irq(tr, data, flags, CALLER_ADDR1, irq, user, ip);

 out:
	atomic_dec(&data->disabled);
}

static void
event_fault_callback(void *probe_data, void *call_data,
		     const char *format, va_list *args)
{
	struct trace_array *tr = probe_data;
	struct trace_array_cpu *data;
	unsigned long ip, flags, error, addr;
	long disable;
	int cpu;

	if (!tracer_enabled)
		return;

	getarg(ip, *args);
	getarg(error, *args);
	getarg(addr, *args);

	preempt_disable_notrace();
	cpu = smp_processor_id();
	data = tr->data[cpu];

	disable = atomic_inc_return(&data->disabled);
	if (disable != 1)
		goto out;

	local_save_flags(flags);
	tracing_event_fault(tr, data, flags, CALLER_ADDR1, ip, error, addr);

 out:
	atomic_dec(&data->disabled);
	preempt_enable_notrace();
}

static void
event_timer_set_callback(void *probe_data, void *call_data,
			 const char *format, va_list *args)
{
	struct trace_array *tr = probe_data;
	struct trace_array_cpu *data;
	unsigned long flags;
	ktime_t *expires;
	void *timer;
	long disable;
	int cpu;

	if (!tracer_enabled)
		return;

	getarg(expires, *args);
	getarg(timer, *args);

	/* interrupts should be off, we are in an interrupt */
	cpu = smp_processor_id();
	data = tr->data[cpu];

	disable = atomic_inc_return(&data->disabled);
	if (disable != 1)
		goto out;

	local_save_flags(flags);
	tracing_event_timer_set(tr, data, flags, CALLER_ADDR1, expires, timer);

 out:
	atomic_dec(&data->disabled);
}

static void
event_timer_triggered_callback(void *probe_data, void *call_data,
			       const char *format, va_list *args)
{
	struct trace_array *tr = probe_data;
	struct trace_array_cpu *data;
	unsigned long flags;
	ktime_t *expired;
	void *timer;
	long disable;
	int cpu;

	if (!tracer_enabled)
		return;

	getarg(expired, *args);
	getarg(timer, *args);

	/* interrupts should be off, we are in an interrupt */
	cpu = smp_processor_id();
	data = tr->data[cpu];

	disable = atomic_inc_return(&data->disabled);
	if (disable != 1)
		goto out;

	local_save_flags(flags);
	tracing_event_timer_triggered(tr, data, flags, CALLER_ADDR1, expired, timer);

 out:
	atomic_dec(&data->disabled);
}

static void
event_hrtimer_callback(void *probe_data, void *call_data,
		       const char *format, va_list *args)
{
	struct trace_array *tr = probe_data;
	struct trace_array_cpu *data;
	unsigned long flags;
	ktime_t *now;
	long disable;
	int cpu;

	if (!tracer_enabled)
		return;

	getarg(now, *args);

	/* interrupts should be off, we are in an interrupt */
	cpu = smp_processor_id();
	data = tr->data[cpu];

	disable = atomic_inc_return(&data->disabled);
	if (disable != 1)
		goto out;

	local_save_flags(flags);
	tracing_event_timestamp(tr, data, flags, CALLER_ADDR1, now);

 out:
	atomic_dec(&data->disabled);
}

static void
event_program_event_callback(void *probe_data, void *call_data,
			     const char *format, va_list *args)
{
	struct trace_array *tr = probe_data;
	struct trace_array_cpu *data;
	unsigned long flags;
	ktime_t *expires;
	int64_t *delta;
	long disable;
	int cpu;

	if (!tracer_enabled)
		return;

	getarg(expires, *args);
	getarg(delta, *args);

	/* interrupts should be off, we are in an interrupt */
	cpu = smp_processor_id();
	data = tr->data[cpu];

	disable = atomic_inc_return(&data->disabled);
	if (disable != 1)
		goto out;

	local_save_flags(flags);
	tracing_event_program_event(tr, data, flags, CALLER_ADDR1, expires, delta);

 out:
	atomic_dec(&data->disabled);
}


static void
event_task_activate_callback(void *probe_data, void *call_data,
			     const char *format, va_list *args)
{
	struct trace_array *tr = probe_data;
	struct trace_array_cpu *data;
	unsigned long flags;
	struct task_struct *p;
	long disable;
	int cpu, rqcpu;

	if (!tracer_enabled)
		return;

	getarg(p, *args);
	getarg(rqcpu, *args);

	/* interrupts should be off, we are in an interrupt */
	cpu = smp_processor_id();
	data = tr->data[cpu];

	disable = atomic_inc_return(&data->disabled);
	if (disable != 1)
		goto out;

	local_save_flags(flags);
	tracing_event_task_activate(tr, data, flags, CALLER_ADDR1, p, rqcpu);

 out:
	atomic_dec(&data->disabled);
}

static void
event_task_deactivate_callback(void *probe_data, void *call_data,
			       const char *format, va_list *args)
{
	struct trace_array *tr = probe_data;
	struct trace_array_cpu *data;
	unsigned long flags;
	struct task_struct *p;
	long disable;
	int cpu, rqcpu;

	if (!tracer_enabled)
		return;

	getarg(p, *args);
	getarg(rqcpu, *args);

	/* interrupts should be off, we are in an interrupt */
	cpu = smp_processor_id();
	data = tr->data[cpu];

	disable = atomic_inc_return(&data->disabled);
	if (disable != 1)
		goto out;

	local_save_flags(flags);
	tracing_event_task_deactivate(tr, data, flags, CALLER_ADDR1, p, rqcpu);

 out:
	atomic_dec(&data->disabled);
}

static void
event_wakeup_callback(void *probe_data, void *call_data,
		      const char *format, va_list *args)
{
	struct trace_array *tr = probe_data;
	struct trace_array_cpu *data;
	unsigned long flags;
	struct task_struct *wakee, *curr;
	long disable, ignore2;
	void *ignore3;
	int ignore1;
	int cpu;

	if (!tracer_enabled)
		return;

	getarg(ignore1, *args);
	getarg(ignore2, *args);
	getarg(ignore3, *args);

	getarg(wakee, *args);
	getarg(curr, *args);

	/* interrupts should be disabled */
	cpu = smp_processor_id();
	data = tr->data[cpu];

	disable = atomic_inc_return(&data->disabled);
	if (unlikely(disable != 1))
		goto out;

	local_save_flags(flags);
	/* record process's command line */
	tracing_record_cmdline(wakee);
	tracing_record_cmdline(curr);

	tracing_sched_wakeup_trace(tr, data, wakee, curr, flags);

 out:
	atomic_dec(&data->disabled);
}
static void
event_ctx_callback(void *probe_data, void *call_data,
		   const char *format, va_list *args)
{
	struct trace_array *tr = probe_data;
	struct trace_array_cpu *data;
	unsigned long flags;
	struct task_struct *prev;
	struct task_struct *next;
	long disable, ignore2;
	void *ignore3;
	int ignore1;
	int cpu;

	if (!tracer_enabled)
		return;

	/* skip prev_pid %d next_pid %d prev_state %ld */
	getarg(ignore1, *args);
	getarg(ignore1, *args);
	getarg(ignore2, *args);
	getarg(ignore3, *args);

	prev = va_arg(*args, typeof(prev));
	next = va_arg(*args, typeof(next));

	tracing_record_cmdline(prev);
	tracing_record_cmdline(next);

	/* interrupts should be disabled */
	cpu = smp_processor_id();
	data = tr->data[cpu];
	disable = atomic_inc_return(&data->disabled);

	if (likely(disable != 1))
		goto out;

	local_save_flags(flags);
	tracing_sched_switch_trace(tr, data, prev, next, flags);
 out:
	atomic_dec(&data->disabled);
}

static int event_register_marker(const char *name, const char *format,
				 marker_probe_func *probe, void *data)
{
	int ret;

	ret = marker_probe_register(name, format, probe, data);
	if (ret) {
		pr_info("event trace: Couldn't add marker"
			" probe to %s\n", name);
		return ret;
	}

	return 0;
}

static void event_tracer_register(struct trace_array *tr)
{
	int ret;

	ret = event_register_marker("ftrace_event_irq", "%d %d %ld",
				    event_irq_callback, tr);
	if (ret)
		return;

	ret = event_register_marker("ftrace_event_fault", "%ld %ld %ld",
				    event_fault_callback, tr);
	if (ret)
		goto out1;

	ret = event_register_marker("ftrace_event_timer_set", "%p %p",
				    event_timer_set_callback, tr);
	if (ret)
		goto out2;

	ret = event_register_marker("ftrace_event_timer_triggered", "%p %p",
				    event_timer_triggered_callback, tr);
	if (ret)
		goto out3;

	ret = event_register_marker("ftrace_event_hrtimer", "%p",
				    event_hrtimer_callback, tr);
	if (ret)
		goto out4;

	ret = event_register_marker("ftrace_event_task_activate", "%p %d",
				    event_task_activate_callback, tr);
	if (ret)
		goto out5;

	ret = event_register_marker("ftrace_event_task_deactivate", "%p %d",
				    event_task_deactivate_callback, tr);
	if (ret)
		goto out6;

	ret = event_register_marker("kernel_sched_wakeup",
				    "pid %d state %ld ## rq %p task %p rq->curr %p",
				    event_wakeup_callback, tr);
	if (ret)
		goto out7;

	ret = event_register_marker("kernel_sched_wakeup_new",
				    "pid %d state %ld ## rq %p task %p rq->curr %p",
				    event_wakeup_callback, tr);
	if (ret)
		goto out8;

	ret = event_register_marker("kernel_sched_schedule",
				    "prev_pid %d next_pid %d prev_state %ld "
				    "## rq %p prev %p next %p",
				    event_ctx_callback, tr);
	if (ret)
		goto out9;

	ret = event_register_marker("ftrace_event_timer", "%p %p",
				    event_program_event_callback, tr);
	if (ret)
		goto out10;

	return;

 out10:
	marker_probe_unregister("kernel_sched_schedule",
				event_ctx_callback, tr);
 out9:
	marker_probe_unregister("kernel_sched_wakeup_new",
				event_wakeup_callback, tr);
 out8:
	marker_probe_unregister("kernel_sched_wakeup",
				event_wakeup_callback, tr);
 out7:
	marker_probe_unregister("ftrace_event_task_deactivate",
				event_task_deactivate_callback, tr);
 out6:
	marker_probe_unregister("ftrace_event_task_activate",
				event_task_activate_callback, tr);
 out5:
	marker_probe_unregister("ftrace_event_hrtimer",
				event_hrtimer_callback, tr);
 out4:
	marker_probe_unregister("ftrace_event_timer_triggered",
				event_timer_triggered_callback, tr);
 out3:
	marker_probe_unregister("ftrace_event_timer_set",
				event_timer_set_callback, tr);
 out2:
	marker_probe_unregister("ftrace_event_fault",
				event_fault_callback, tr);
 out1:
	marker_probe_unregister("ftrace_event_irq",
				event_irq_callback, tr);
}

static void event_tracer_unregister(struct trace_array *tr)
{
	marker_probe_unregister("ftrace_event_timer",
				event_program_event_callback, tr);
	marker_probe_unregister("kernel_sched_schedule",
				event_ctx_callback, tr);
	marker_probe_unregister("kernel_sched_wakeup_new",
				event_wakeup_callback, tr);
	marker_probe_unregister("kernel_sched_wakeup",
				event_wakeup_callback, tr);
	marker_probe_unregister("ftrace_event_task_deactivate",
			      event_task_deactivate_callback, tr);
	marker_probe_unregister("ftrace_event_task_activate",
				event_task_activate_callback, tr);
	marker_probe_unregister("ftrace_event_hrtimer",
				event_hrtimer_callback, tr);
	marker_probe_unregister("ftrace_event_timer_triggered",
				event_timer_triggered_callback, tr);
	marker_probe_unregister("ftrace_event_timer_set",
				event_timer_set_callback, tr);
	marker_probe_unregister("ftrace_event_fault",
				event_fault_callback, tr);
	marker_probe_unregister("ftrace_event_irq",
				event_irq_callback, tr);
}

void trace_event_register(struct trace_array *tr)
{
	long ref;

	ref = atomic_inc_return(&event_ref);
	if (ref == 1)
		event_tracer_register(tr);
}

void trace_event_unregister(struct trace_array *tr)
{
	long ref;

	ref = atomic_dec_and_test(&event_ref);
	if (ref)
		event_tracer_unregister(tr);
}

static void start_event_trace(struct trace_array *tr)
{
	event_reset(tr);
	trace_event_register(tr);
	tracing_start_function_trace();
	tracer_enabled = 1;
}

static void stop_event_trace(struct trace_array *tr)
{
	tracer_enabled = 0;
	tracing_stop_function_trace();
	trace_event_unregister(tr);
}

static void event_trace_init(struct trace_array *tr)
{
	events_trace = tr;

	if (tr->ctrl)
		start_event_trace(tr);
}

static void event_trace_reset(struct trace_array *tr)
{
	if (tr->ctrl)
		stop_event_trace(tr);
}

static void event_trace_ctrl_update(struct trace_array *tr)
{
	if (tr->ctrl)
		start_event_trace(tr);
	else
		stop_event_trace(tr);
}

static void event_trace_open(struct trace_iterator *iter)
{
	/* stop the trace while dumping */
	if (iter->tr->ctrl)
		tracer_enabled = 0;
}

static void event_trace_close(struct trace_iterator *iter)
{
	if (iter->tr->ctrl)
		tracer_enabled = 1;
}

static struct tracer event_trace __read_mostly =
{
	.name = "events",
	.init = event_trace_init,
	.reset = event_trace_reset,
	.open = event_trace_open,
	.close = event_trace_close,
	.ctrl_update = event_trace_ctrl_update,
};

__init static int init_event_trace(void)
{
	int ret;

	ret = register_tracer(&event_trace);
	if (ret)
		return ret;

	return 0;
}

device_initcall(init_event_trace);
