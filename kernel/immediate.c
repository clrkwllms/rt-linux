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
#include <linux/cpu.h>
#include <linux/stop_machine.h>

#include <asm/cacheflush.h>

/*
 * Kernel ready to execute the SMP update that may depend on trap and ipi.
 */
static int imv_early_boot_complete;
static int wrote_text;

extern const struct __imv __start___imv[];
extern const struct __imv __stop___imv[];

static int stop_machine_imv_update(void *imv_ptr)
{
	struct __imv *imv = imv_ptr;

	if (!wrote_text) {
		text_poke((void *)imv->imv, (void *)imv->var, imv->size);
		wrote_text = 1;
		smp_wmb(); /* make sure other cpus see that this has run */
	} else
		sync_core();

	flush_icache_range(imv->imv, imv->imv + imv->size);

	return 0;
}

/*
 * imv_mutex nests inside module_mutex. imv_mutex protects builtin
 * immediates and module immediates.
 */
static DEFINE_MUTEX(imv_mutex);


/**
 * apply_imv_update - update one immediate value
 * @imv: pointer of type const struct __imv to update
 *
 * Update one immediate value. Must be called with imv_mutex held.
 * It makes sure all CPUs are not executing the modified code by having them
 * busy looping with interrupts disabled.
 * It does _not_ protect against NMI and MCE (could be a problem with Intel's
 * errata if we use immediate values in their code path).
 */
static int apply_imv_update(const struct __imv *imv)
{
	/*
	 * If the variable and the instruction have the same value, there is
	 * nothing to do.
	 */
	switch (imv->size) {
	case 1:	if (*(uint8_t *)imv->imv
				== *(uint8_t *)imv->var)
			return 0;
		break;
	case 2:	if (*(uint16_t *)imv->imv
				== *(uint16_t *)imv->var)
			return 0;
		break;
	case 4:	if (*(uint32_t *)imv->imv
				== *(uint32_t *)imv->var)
			return 0;
		break;
	case 8:	if (*(uint64_t *)imv->imv
				== *(uint64_t *)imv->var)
			return 0;
		break;
	default:return -EINVAL;
	}

	if (imv_early_boot_complete) {
		kernel_text_lock();
		wrote_text = 0;
		stop_machine_run(stop_machine_imv_update, (void *)imv,
					ALL_CPUS);
		kernel_text_unlock();
	} else
		text_poke_early((void *)imv->imv, (void *)imv->var,
				imv->size);
	return 0;
}

/**
 * imv_update_range - Update immediate values in a range
 * @begin: pointer to the beginning of the range
 * @end: pointer to the end of the range
 *
 * Updates a range of immediates.
 */
void imv_update_range(const struct __imv *begin,
		const struct __imv *end)
{
	const struct __imv *iter;
	int ret;
	for (iter = begin; iter < end; iter++) {
		mutex_lock(&imv_mutex);
		ret = apply_imv_update(iter);
		if (imv_early_boot_complete && ret)
			printk(KERN_WARNING
				"Invalid immediate value. "
				"Variable at %p, "
				"instruction at %p, size %hu\n",
				(void *)iter->imv,
				(void *)iter->var, iter->size);
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

void __init imv_init_complete(void)
{
	imv_early_boot_complete = 1;
}
