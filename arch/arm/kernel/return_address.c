#include <linux/module.h>
#include <linux/sched.h>

#include "stacktrace.h"

struct return_address_data {
	unsigned int level;
	void *addr;
};

static int save_return_addr(struct stackframe *frame, void *d)
{
	struct return_address_data *data = d;

	if (!data->level) {
		data->addr = (void *)frame->lr;

		return 1;
	} else {
		--data->level;
		return 0;
	}
}

void *return_address(unsigned int level)
{
	unsigned long fp, base;
	struct return_address_data data;

	data.level = level + 1;

	base = (unsigned long)task_stack_page(current);
	asm("mov %0, fp" : "=r" (fp));

	walk_stackframe(fp, base, base + THREAD_SIZE, save_return_addr, &data);

	if (!data.level)
		return data.addr;
	else
		return NULL;
}
EXPORT_SYMBOL_GPL(return_address);
