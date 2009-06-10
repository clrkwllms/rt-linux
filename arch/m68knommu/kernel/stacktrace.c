/*
 * Quick & dirty stacktrace implementation.
 */
#include <linux/sched.h>
#include <linux/stacktrace.h>

typedef void (save_stack_addr_t)(void *data, unsigned long addr, int reliable);

static void save_stack_address(void *data, unsigned long addr, int reliable)
{
	struct stack_trace *trace = data;
	if (!reliable)
		return;
	if (trace->skip > 0) {
		trace->skip--;
		return;
	}
	if (trace->nr_entries < trace->max_entries)
		trace->entries[trace->nr_entries++] = addr;
}

static void print_context_stack(unsigned long *stack,
		save_stack_addr_t *sstack_func, struct stack_trace *trace)
{
	unsigned long *last_stack;
	unsigned long *endstack;
	unsigned long addr;

	addr = (unsigned long) stack;
	endstack = (unsigned long *) PAGE_ALIGN(addr);

	last_stack = stack - 1;
	while (stack <= endstack && stack > last_stack) {

		addr = *(stack + 1);
		sstack_func(trace, addr, 1);

		last_stack = stack;
		stack = (unsigned long *)*stack;
	}
}

static noinline long *get_current_stack(void)
{
	unsigned long *stack;

	stack = (unsigned long *)&stack;
	stack++;
	return stack;
}

static void save_current_stack(save_stack_addr_t *sstack_func,
		struct stack_trace *trace)
{
	unsigned long *stack;

	stack = get_current_stack();
	print_context_stack(stack, save_stack_address, trace);
}

/*
 * Save stack-backtrace addresses into a stack_trace buffer.
 */
void save_stack_trace(struct stack_trace *trace)
{
	save_current_stack(save_stack_address, trace);
	if (trace->nr_entries < trace->max_entries)
		trace->entries[trace->nr_entries++] = ULONG_MAX;
}
