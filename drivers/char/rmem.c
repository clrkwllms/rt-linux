/*
 * Rmem - REALLY simple memory mapping demonstration.
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

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/kdev_t.h>
#include <asm/page.h>
#include <linux/cdev.h>
#include <linux/device.h>

static int rmem_major = 0;
module_param(rmem_major, int, 0444);

static struct class *rmem_class;

MODULE_AUTHOR("Theodore Ts'o");
MODULE_LICENSE("GPL");

struct page *rmem_vma_nopage(struct vm_area_struct *vma,
                unsigned long address, int *type)
{
	struct page *pageptr;
	unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
	unsigned long physaddr = address - vma->vm_start + offset;
	unsigned long pageframe = physaddr >> PAGE_SHIFT;

	if (!pfn_valid(pageframe))
		return NOPAGE_SIGBUS;
	pageptr = pfn_to_page(pageframe);
	get_page(pageptr);
	if (type)
		*type = VM_FAULT_MINOR;
	return pageptr;
}

static struct vm_operations_struct rmem_nopage_vm_ops = {
	.nopage = rmem_vma_nopage,
};

static int rmem_nopage_mmap(struct file *filp, struct vm_area_struct *vma)
{
	unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;

	if (offset >= __pa(high_memory) || (filp->f_flags & O_SYNC))
		vma->vm_flags |= VM_IO;
	vma->vm_flags |= VM_RESERVED;
	vma->vm_ops = &rmem_nopage_vm_ops;
#ifdef TAINT_USER
	add_taint(TAINT_USER);
#endif
	return 0;
}

static struct file_operations rmem_nopage_ops = {
	.owner   = THIS_MODULE,
	.mmap    = rmem_nopage_mmap,
};

static struct cdev rmem_cdev = {
	.kobj	=	{.k_name = "rmem", },
	.owner	=	THIS_MODULE,
};

static int __init rmem_init(void)
{
	int result;
	dev_t dev = MKDEV(rmem_major, 0);

	/* Figure out our device number. */
	if (rmem_major)
		result = register_chrdev_region(dev, 1, "rmem");
	else {
		result = alloc_chrdev_region(&dev, 0, 1, "rmem");
		rmem_major = MAJOR(dev);
	}
	if (result < 0) {
		printk(KERN_WARNING "rmem: unable to get major %d\n", rmem_major);
		return result;
	}
	if (rmem_major == 0)
		rmem_major = result;

	cdev_init(&rmem_cdev, &rmem_nopage_ops);
	result = cdev_add(&rmem_cdev, dev, 1);
	if (result) {
		printk (KERN_NOTICE "Error %d adding /dev/rmem", result);
		kobject_put(&rmem_cdev.kobj);
		unregister_chrdev_region(dev, 1);
		return 1;
	}

	rmem_class = class_create(THIS_MODULE, "rmem");
	class_device_create(rmem_class, NULL, dev, NULL, "rmem");

	return 0;
}


static void __exit rmem_cleanup(void)
{
	cdev_del(&rmem_cdev);
	unregister_chrdev_region(MKDEV(rmem_major, 0), 1);
	class_destroy(rmem_class);
}


module_init(rmem_init);
module_exit(rmem_cleanup);
