/*
 *  alloc_rtsj_mem.c -- Hack to allocate some memory
 *
 *  Copyright (C) 2005 by Theodore Ts'o
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/sysctl.h>
#include <linux/bootmem.h>

#include <asm/io.h>

MODULE_AUTHOR("Theodore Tso");
MODULE_DESCRIPTION("RTSJ alloc memory");
MODULE_LICENSE("GPL");

static void *mem = 0;
int size = 0, addr = 0;

module_param(size, int, 0444);
module_param(addr, int, 0444);

static void __exit shutdown_module(void)
{
	kfree(mem);
}

#ifndef MODULE
void __init alloc_rtsj_mem_early_setup(void)
{
	if (size > PAGE_SIZE*2) {
		mem = alloc_bootmem(size);
		if (mem) {
			printk(KERN_INFO "alloc_rtsj_mem: got %d bytes "
			       "using alloc_bootmem\n", size);
		} else {
			printk(KERN_INFO "alloc_rtsj_mem: failed to "
			       "get %d bytes from alloc_bootmem\n", size);
		}
	}
}
#endif

static int __init startup_module(void)
{
	static char test_string[] = "The BOFH: Servicing users the way the "
		"military\n\tservices targets for 15 years.\n";

	if (!size)
		return 0;

	if (!mem) {
		mem = kmalloc(size, GFP_KERNEL);
		if (mem) {
			printk(KERN_INFO "alloc_rtsj_mem: got %d bytes "
			       "using kmalloc\n", size);
		} else {
			printk(KERN_ERR "alloc_rtsj_mem: failed to get "
			       "%d bytes using kmalloc\n", size);
			return -ENOMEM;
		}
	}
	memcpy(mem, test_string, min(sizeof(test_string), (size_t) size));
	addr = virt_to_phys(mem);
	return 0;
}

module_init(startup_module);
module_exit(shutdown_module);

