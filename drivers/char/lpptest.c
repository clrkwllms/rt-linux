/*
 * /dev/lpptest device: test IRQ handling latencies over parallel port
 *
 *      Copyright (C) 2005 Thomas Gleixner, Ingo Molnar
 *
 * licensed under the GPL
 *
 * You need to have CONFIG_PARPORT disabled for this device, it is a
 * completely self-contained device that assumes sole ownership of the
 * parallel port.
 */
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/rtc.h>

/*
 * API wrappers so that the code can be shared with the -rt tree:
 */
#ifndef local_irq_disable
# define local_irq_disable	local_irq_disable
# define local_irq_enable	local_irq_enable
#endif

#ifndef IRQ_NODELAY
# define IRQ_NODELAY		0
# define IRQF_NODELAY		0
#endif

/*
 * Driver:
 */
#define LPPTEST_CHAR_MAJOR 245
#define LPPTEST_DEVICE_NAME "lpptest"

#define LPPTEST_IRQ 7

#define LPPTEST_TEST    _IOR (LPPTEST_CHAR_MAJOR, 1, unsigned long long)
#define LPPTEST_DISABLE _IOR (LPPTEST_CHAR_MAJOR, 2, unsigned long long)
#define LPPTEST_ENABLE  _IOR (LPPTEST_CHAR_MAJOR, 3, unsigned long long)

static char dev_id[] = "lpptest";

#define INIT_PORT()	outb(0x04, 0x37a)
#define ENABLE_IRQ()	outb(0x10, 0x37a)
#define DISABLE_IRQ()	outb(0, 0x37a)

static unsigned char out = 0x5a;

/**
 * Interrupt handler. Flip a bit in the reply.
 */
static int lpptest_irq (int irq, void *dev_id)
{
	out ^= 0xff;
	outb(out, 0x378);

	return IRQ_HANDLED;
}

static cycles_t test_response(void)
{
	cycles_t now, end;
	unsigned char in;
	int timeout = 0;

	local_irq_disable();
	in = inb(0x379);
	inb(0x378);
	outb(0x08, 0x378);
	now = get_cycles();
	while(1) {
		if (inb(0x379) != in)
			break;
		if (timeout++ > 1000000) {
			outb(0x00, 0x378);
			local_irq_enable();

			return 0;
		}
	}
	end = get_cycles();
	outb(0x00, 0x378);
	local_irq_enable();

	return end - now;
}

static int lpptest_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int lpptest_close(struct inode *inode, struct file *file)
{
	return 0;
}

int lpptest_ioctl(struct inode *inode, struct file *file, unsigned int ioctl_num, unsigned long ioctl_param)
{
	int retval = 0;

	switch (ioctl_num) {

	case LPPTEST_DISABLE:
		DISABLE_IRQ();
		break;

	case LPPTEST_ENABLE:
		ENABLE_IRQ();
		break;

	case LPPTEST_TEST: {

		cycles_t diff = test_response();
		if (copy_to_user((void *)ioctl_param, (void*) &diff, sizeof(diff)))
			goto errcpy;
		break;
	}
	default: retval = -EINVAL;
	}

	return retval;

 errcpy:
	return -EFAULT;
}

static struct file_operations lpptest_dev_fops = {
	.ioctl = lpptest_ioctl,
	.open = lpptest_open,
	.release = lpptest_close,
};

static int __init lpptest_init (void)
{
	if (register_chrdev(LPPTEST_CHAR_MAJOR, LPPTEST_DEVICE_NAME, &lpptest_dev_fops))
	{
		printk(KERN_NOTICE "Can't allocate major number %d for lpptest.\n",
		       LPPTEST_CHAR_MAJOR);
		return -EAGAIN;
	}

	if (request_irq (LPPTEST_IRQ, lpptest_irq, 0, "lpptest", dev_id)) {
		printk (KERN_WARNING "lpptest: irq %d in use. Unload parport module!\n", LPPTEST_IRQ);
		unregister_chrdev(LPPTEST_CHAR_MAJOR, LPPTEST_DEVICE_NAME);
		return -EAGAIN;
	}
	irq_desc[LPPTEST_IRQ].status |= IRQ_NODELAY;
	irq_desc[LPPTEST_IRQ].action->flags |= IRQF_NODELAY | IRQF_DISABLED;

	INIT_PORT();
	ENABLE_IRQ();

	return 0;
}
module_init (lpptest_init);

static void __exit lpptest_exit (void)
{
	DISABLE_IRQ();

	free_irq(LPPTEST_IRQ, dev_id);
	unregister_chrdev(LPPTEST_CHAR_MAJOR, LPPTEST_DEVICE_NAME);
}
module_exit (lpptest_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("lpp test module");

