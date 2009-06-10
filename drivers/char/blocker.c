/*
 * priority inheritance testing device
 */

#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/timex.h>
#include <linux/sched.h>

#define BLOCKER_MINOR		221

#define BLOCK_IOCTL		4245
#define BLOCK_SET_DEPTH		4246

#define BLOCKER_MAX_LOCK_DEPTH		10

void loop(int loops)
{
	int i;

	for (i = 0; i < loops; i++)
		get_cycles();
}

static spinlock_t blocker_lock[BLOCKER_MAX_LOCK_DEPTH];

static unsigned int lock_depth = 1;

void do_the_lock_and_loop(unsigned int args)
{
	int i, max;

	if (rt_task(current))
		max = lock_depth;
	else if (lock_depth > 1)
		max = (current->pid % lock_depth) + 1;
	else
		max = 1;

	/* Always lock from the top down */
	for (i = max-1; i >= 0; i--)
		 spin_lock(&blocker_lock[i]);
	loop(args);
	for (i = 0; i < max; i++)
		spin_unlock(&blocker_lock[i]);
}

static int blocker_open(struct inode *in, struct file *file)
{
	printk(KERN_INFO "blocker_open called\n");

	return 0;
}

static long blocker_ioctl(struct file *file,
			  unsigned int cmd, unsigned long args)
{
	switch(cmd) {
	case BLOCK_IOCTL:
		do_the_lock_and_loop(args);
		return 0;
	case BLOCK_SET_DEPTH:
		if (args >= BLOCKER_MAX_LOCK_DEPTH)
			return -EINVAL;
		lock_depth = args;
		return 0;
	default:
		return -EINVAL;
	}
}

static struct file_operations blocker_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.unlocked_ioctl = blocker_ioctl,
	.open		= blocker_open,
};

static struct miscdevice blocker_dev =
{
	BLOCKER_MINOR,
	"blocker",
	&blocker_fops
};

static int __init blocker_init(void)
{
	int i;

	if (misc_register(&blocker_dev))
		return -ENODEV;

	for (i = 0; i < BLOCKER_MAX_LOCK_DEPTH; i++)
		spin_lock_init(blocker_lock + i);

	return 0;
}

void __exit blocker_exit(void)
{
	printk(KERN_INFO "blocker device uninstalled\n");
	misc_deregister(&blocker_dev);
}

module_init(blocker_init);
module_exit(blocker_exit);

MODULE_LICENSE("GPL");

