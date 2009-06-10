#include <linux/marker.h>

static inline void trace_kernel_sched_wait(struct task_struct *p)
{
	trace_mark(kernel_sched_wait_task, "pid %d state %ld",
			p->pid, p->state);
}

static inline
void trace_kernel_sched_wakeup(struct rq *rq, struct task_struct *p)
{
	trace_mark(kernel_sched_wakeup,
			"pid %d state %ld ## rq %p task %p rq->curr %p",
			p->pid, p->state, rq, p, rq->curr);
}

static inline
void trace_kernel_sched_wakeup_new(struct rq *rq, struct task_struct *p)
{
	trace_mark(kernel_sched_wakeup_new,
			"pid %d state %ld ## rq %p task %p rq->curr %p",
			p->pid, p->state, rq, p, rq->curr);
}

static inline void trace_kernel_sched_switch(struct rq *rq,
		struct task_struct *prev, struct task_struct *next)
{
	trace_mark(kernel_sched_schedule,
			"prev_pid %d next_pid %d prev_state %ld "
			"## rq %p prev %p next %p",
			prev->pid, next->pid, prev->state,
			rq, prev, next);
}

static inline void
trace_kernel_sched_migrate_task(struct task_struct *p, int src, int dst)
{
	trace_mark(kernel_sched_migrate_task,
			"pid %d state %ld dest_cpu %d",
			p->pid, p->state, dst);
}
