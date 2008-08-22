#include <linux/kdebug.h>
#include <linux/notifier.h>
#include <linux/smp.h>

#include <mach_ipi.h>

#include "smp.h"
#include <asm/irq_vectors.h>

static DEFINE_SPINLOCK(nmi_spinlock);

static atomic_t nmi_wait;
static atomic_t nmi_resume;
static atomic_t paused;

static int nmi_notifier(struct notifier_block *self,
	unsigned long val, void *data)
{
	if (val != DIE_NMI_IPI || !atomic_read(&nmi_wait))
		return NOTIFY_DONE;

	atomic_inc(&paused);

	/* Pause until the fault has been handled */
	while (!atomic_read(&nmi_resume))
		cpu_relax();

	atomic_dec(&paused);

	return NOTIFY_STOP;
}

static struct notifier_block nmi_nb = {
	.notifier_call = &nmi_notifier,
};

void kmemcheck_smp_init(void)
{
	int err;

	err = register_die_notifier(&nmi_nb);
	BUG_ON(err);
}

void kmemcheck_pause_allbutself(void)
{
	int cpus;
	cpumask_t mask = cpu_online_map;

	spin_lock(&nmi_spinlock);

	cpus = num_online_cpus() - 1;

	atomic_set(&paused, 0);
	atomic_set(&nmi_wait, 1);
	atomic_set(&nmi_resume, 0);

	cpu_clear(safe_smp_processor_id(), mask);
	if (!cpus_empty(mask))
		send_IPI_mask(mask, NMI_VECTOR);

	while (atomic_read(&paused) != cpus)
		cpu_relax();

	atomic_set(&nmi_wait, 0);
}

void kmemcheck_resume(void)
{
	int cpus;

	cpus = num_online_cpus() - 1;

	atomic_set(&nmi_resume, 1);

	while (atomic_read(&paused) != 0)
		cpu_relax();

	spin_unlock(&nmi_spinlock);
}
