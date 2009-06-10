/*
 * linux/kernel/irq/proc.c
 *
 * Copyright (C) 1992, 1998-2004 Linus Torvalds, Ingo Molnar
 *
 * This file contains the /proc/irq/ handling code.
 */

#include <linux/irq.h>
#include <asm/uaccess.h>
#include <linux/profile.h>
#include <linux/proc_fs.h>
#include <linux/interrupt.h>

#include "internals.h"

static struct proc_dir_entry *root_irq_dir;

#ifdef CONFIG_SMP

static int irq_affinity_read_proc(char *page, char **start, off_t off,
				  int count, int *eof, void *data)
{
	struct irq_desc *desc = irq_desc + (long)data;
	cpumask_t *mask = &desc->affinity;
	int len;

#ifdef CONFIG_GENERIC_PENDING_IRQ
	if (desc->status & IRQ_MOVE_PENDING)
		mask = &desc->pending_mask;
#endif
	len = cpumask_scnprintf(page, count, *mask);

	if (count - len < 2)
		return -EINVAL;
	len += sprintf(page + len, "\n");
	return len;
}

#ifndef is_affinity_mask_valid
#define is_affinity_mask_valid(val) 1
#endif

int no_irq_affinity;
static int irq_affinity_write_proc(struct file *file, const char __user *buffer,
				   unsigned long count, void *data)
{
	unsigned int irq = (int)(long)data, full_count = count, err;
	cpumask_t new_value, tmp;

	if (!irq_desc[irq].chip->set_affinity || no_irq_affinity ||
	    irq_balancing_disabled(irq))
		return -EIO;

	err = cpumask_parse_user(buffer, count, new_value);
	if (err)
		return err;

	if (!is_affinity_mask_valid(new_value))
		return -EINVAL;

	/*
	 * Do not allow disabling IRQs completely - it's a too easy
	 * way to make the system unusable accidentally :-) At least
	 * one online CPU still has to be targeted.
	 */
	cpus_and(tmp, new_value, cpu_online_map);
	if (cpus_empty(tmp))
		/* Special case for empty set - allow the architecture
		   code to set default SMP affinity. */
		return select_smp_affinity(irq) ? -EINVAL : full_count;

	irq_set_affinity(irq, new_value);

	return full_count;
}

#endif

#define MAX_NAMELEN 10

void register_irq_proc(unsigned int irq)
{
	char name [MAX_NAMELEN];

	if (!root_irq_dir ||
		(irq_desc[irq].chip == &no_irq_chip) ||
			irq_desc[irq].dir)
		return;

	memset(name, 0, MAX_NAMELEN);
	sprintf(name, "%d", irq);

	/* create /proc/irq/1234 */
	irq_desc[irq].dir = proc_mkdir(name, root_irq_dir);

#ifdef CONFIG_SMP
	{
		struct proc_dir_entry *entry;

		/* create /proc/irq/<irq>/smp_affinity */
		entry = create_proc_entry("smp_affinity", 0600, irq_desc[irq].dir);

		if (entry) {
			entry->data = (void *)(long)irq;
			entry->read_proc = irq_affinity_read_proc;
			entry->write_proc = irq_affinity_write_proc;
		}
	}
#endif
}

#undef MAX_NAMELEN

void unregister_handler_proc(unsigned int irq, struct irqaction *action)
{
	if (action->threaded)
		remove_proc_entry(action->threaded->name, action->dir);
	if (action->dir)
		remove_proc_entry(action->dir->name, irq_desc[irq].dir);
}

#ifndef CONFIG_PREEMPT_RT

static int threaded_read_proc(char *page, char **start, off_t off,
			      int count, int *eof, void *data)
{
	return sprintf(page, "%c\n",
		((struct irqaction *)data)->flags & IRQF_NODELAY ? '0' : '1');
}

static int threaded_write_proc(struct file *file, const char __user *buffer,
			       unsigned long count, void *data)
{
	int c;
	struct irqaction *action = data;
	irq_desc_t *desc = irq_desc + action->irq;

	if (get_user(c, buffer))
		return -EFAULT;
	if (c != '0' && c != '1')
		return -EINVAL;

	spin_lock_irq(&desc->lock);

	if (c == '0')
		action->flags |= IRQF_NODELAY;
	if (c == '1')
		action->flags &= ~IRQF_NODELAY;
	recalculate_desc_flags(desc);

	spin_unlock_irq(&desc->lock);

	return 1;
}

#endif

#define MAX_NAMELEN 128

static int name_unique(unsigned int irq, struct irqaction *new_action)
{
	struct irq_desc *desc = irq_desc + irq;
	struct irqaction *action;

	for (action = desc->action ; action; action = action->next)
		if ((action != new_action) && action->name &&
				!strcmp(new_action->name, action->name))
			return 0;
	return 1;
}

void register_handler_proc(unsigned int irq, struct irqaction *action)
{
	char name [MAX_NAMELEN];

	if (!irq_desc[irq].dir || action->dir || !action->name ||
					!name_unique(irq, action))
		return;

	memset(name, 0, MAX_NAMELEN);
	snprintf(name, MAX_NAMELEN, "%s", action->name);

	/* create /proc/irq/1234/handler/ */
	action->dir = proc_mkdir(name, irq_desc[irq].dir);

	if (!action->dir)
		return;
#ifndef CONFIG_PREEMPT_RT
	{
		struct proc_dir_entry *entry;
		/* create /proc/irq/1234/handler/threaded */
		entry = create_proc_entry("threaded", 0600, action->dir);
		if (!entry)
			return;
		entry->nlink = 1;
		entry->data = (void *)action;
		entry->read_proc = threaded_read_proc;
		entry->write_proc = threaded_write_proc;
		action->threaded = entry;
	}
#endif
}

#undef MAX_NAMELEN

void init_irq_proc(void)
{
	int i;

	/* create /proc/irq */
	root_irq_dir = proc_mkdir("irq", NULL);
	if (!root_irq_dir)
		return;

	/* create /proc/irq/prof_cpu_mask */
	create_prof_cpu_mask(root_irq_dir);

	/*
	 * Create entries for all existing IRQs.
	 */
	for (i = 0; i < NR_IRQS; i++)
		register_irq_proc(i);
}

