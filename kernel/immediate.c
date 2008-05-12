/*
 * Copyright (C) 2007 Mathieu Desnoyers
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
 */
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/immediate.h>
#include <linux/memory.h>

#include <asm/sections.h>

/*
 * Kernel ready to execute the SMP update that may depend on trap and ipi.
 */
static int imv_early_boot_complete;

extern struct __imv __start___imv[];
extern struct __imv __stop___imv[];

/*
 * imv_mutex nests inside module_mutex. imv_mutex protects builtin
 * immediates and module immediates.
 */
static DEFINE_MUTEX(imv_mutex);

/**
 * imv_update_range - Update immediate values in a range
 * @begin: pointer to the beginning of the range
 * @end: pointer to the end of the range
 *
 * Updates a range of immediates.
 */
void imv_update_range(struct __imv *begin,
		struct __imv *end)
{
	struct __imv *iter;
	int ret;
	for (iter = begin; iter < end; iter++) {
		mutex_lock(&imv_mutex);
		if (!iter->imv)	/* Skip removed __init immediate values */
			goto skip;
		kernel_text_lock();
		ret = arch_imv_update(iter, !imv_early_boot_complete);
		kernel_text_unlock();
		if (imv_early_boot_complete && ret)
			printk(KERN_WARNING
				"Invalid immediate value. "
				"Variable at %p, "
				"instruction at %p, size %hu\n",
				(void *)iter->imv,
				(void *)iter->var, iter->size);
skip:
		mutex_unlock(&imv_mutex);
	}
}
EXPORT_SYMBOL_GPL(imv_update_range);

/**
 * imv_update - update all immediate values in the kernel
 *
 * Iterate on the kernel core and modules to update the immediate values.
 */
void core_imv_update(void)
{
	/* Core kernel imvs */
	imv_update_range(__start___imv, __stop___imv);
}
EXPORT_SYMBOL_GPL(core_imv_update);

/**
 * imv_unref
 *
 * Deactivate any immediate value reference pointing into the code region in the
 * range start to start + size.
 */
void imv_unref(struct __imv *begin, struct __imv *end, void *start,
		unsigned long size)
{
	struct __imv *iter;

	for (iter = begin; iter < end; iter++)
		if (iter->imv >= (unsigned long)start
			&& iter->imv < (unsigned long)start + size)
			iter->imv = 0UL;
}

void imv_unref_core_init(void)
{
	imv_unref(__start___imv, __stop___imv, __init_begin,
		(unsigned long)__init_end - (unsigned long)__init_begin);
}

void __init imv_init_complete(void)
{
	imv_early_boot_complete = 1;
}
