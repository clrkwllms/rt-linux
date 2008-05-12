/*
 * Immediate Value - x86 architecture specific code.
 *
 * Rationale
 *
 * Required because of :
 * - Erratum 49 fix for Intel PIII.
 * - Still present on newer processors : Intel Core 2 Duo Processor for Intel
 *   Centrino Duo Processor Technology Specification Update, AH33.
 *   Unsynchronized Cross-Modifying Code Operations Can Cause Unexpected
 *   Instruction Execution Results.
 *
 * Permits immediate value modification by XMC with correct serialization.
 *
 * Reentrant for NMI and trap handler instrumentation. Permits XMC to a
 * location that has preemption enabled because it involves no temporary or
 * reused data structure.
 *
 * Quoting Richard J Moore, source of the information motivating this
 * implementation which differs from the one proposed by Intel which is not
 * suitable for kernel context (does not support NMI and would require disabling
 * interrupts on every CPU for a long period) :
 *
 * "There is another issue to consider when looking into using probes other
 * then int3:
 *
 * Intel erratum 54 - Unsynchronized Cross-modifying code - refers to the
 * practice of modifying code on one processor where another has prefetched
 * the unmodified version of the code. Intel states that unpredictable general
 * protection faults may result if a synchronizing instruction (iret, int,
 * int3, cpuid, etc ) is not executed on the second processor before it
 * executes the pre-fetched out-of-date copy of the instruction.
 *
 * When we became aware of this I had a long discussion with Intel's
 * microarchitecture guys. It turns out that the reason for this erratum
 * (which incidentally Intel does not intend to fix) is because the trace
 * cache - the stream of micro-ops resulting from instruction interpretation -
 * cannot be guaranteed to be valid. Reading between the lines I assume this
 * issue arises because of optimization done in the trace cache, where it is
 * no longer possible to identify the original instruction boundaries. If the
 * CPU discoverers that the trace cache has been invalidated because of
 * unsynchronized cross-modification then instruction execution will be
 * aborted with a GPF. Further discussion with Intel revealed that replacing
 * the first opcode byte with an int3 would not be subject to this erratum.
 *
 * So, is cmpxchg reliable? One has to guarantee more than mere atomicity."
 *
 * Overall design
 *
 * The algorithm proposed by Intel applies not so well in kernel context: it
 * would imply disabling interrupts and looping on every CPUs while modifying
 * the code and would not support instrumentation of code called from interrupt
 * sources that cannot be disabled.
 *
 * Therefore, we use a different algorithm to respect Intel's erratum (see the
 * quoted discussion above). We make sure that no CPU sees an out-of-date copy
 * of a pre-fetched instruction by 1 - using a breakpoint, which skips the
 * instruction that is going to be modified, 2 - issuing an IPI to every CPU to
 * execute a sync_core(), to make sure that even when the breakpoint is removed,
 * no cpu could possibly still have the out-of-date copy of the instruction,
 * modify the now unused 2nd byte of the instruction, and then put back the
 * original 1st byte of the instruction.
 *
 * It has exactly the same intent as the algorithm proposed by Intel, but
 * it has less side-effects, scales better and supports NMI, SMI and MCE.
 *
 * Mathieu Desnoyers <mathieu.desnoyers@polymtl.ca>
 */

#include <linux/preempt.h>
#include <linux/smp.h>
#include <linux/notifier.h>
#include <linux/module.h>
#include <linux/immediate.h>
#include <linux/kdebug.h>
#include <linux/rcupdate.h>
#include <linux/kprobes.h>
#include <linux/io.h>

#include <asm/cacheflush.h>

#define BREAKPOINT_INSTRUCTION  0xcc
#define BREAKPOINT_INS_LEN	1
#define NR_NOPS			10

static unsigned long target_after_int3;	/* EIP of the target after the int3 */
static unsigned long bypass_eip;	/* EIP of the bypass. */
static unsigned long bypass_after_int3;	/* EIP after the end-of-bypass int3 */
static unsigned long after_imv;	/*
					 * EIP where to resume after the
					 * single-stepping.
					 */

/*
 * Internal bypass used during value update. The bypass is skipped by the
 * function in which it is inserted.
 * No need to be aligned because we exclude readers from the site during
 * update.
 * Layout is:
 * (10x nop) int3
 * (maximum size is 2 bytes opcode + 8 bytes immediate value for long on x86_64)
 * The nops are the target replaced by the instruction to single-step.
 * Align on 16 bytes to make sure the nops fit within a single page so remapping
 * it can be done easily.
 */
static inline void _imv_bypass(unsigned long *bypassaddr,
	unsigned long *breaknextaddr)
{
		asm volatile("jmp 2f;\n\t"
				".align 16;\n\t"
				"0:\n\t"
				".space 10, 0x90;\n\t"
				"1:\n\t"
				"int3;\n\t"
				"2:\n\t"
				"mov $(0b),%0;\n\t"
				"mov $((1b)+1),%1;\n\t"
				: "=r" (*bypassaddr),
				  "=r" (*breaknextaddr));
}

static void imv_synchronize_core(void *info)
{
	sync_core();	/* use cpuid to stop speculative execution */
}

/*
 * The eip value points right after the breakpoint instruction, in the second
 * byte of the movl.
 * Disable preemption in the bypass to make sure no thread will be preempted in
 * it. We can then use synchronize_sched() to make sure every bypass users have
 * ended.
 */
static int imv_notifier(struct notifier_block *nb,
	unsigned long val, void *data)
{
	enum die_val die_val = (enum die_val) val;
	struct die_args *args = data;

	if (!args->regs || user_mode_vm(args->regs))
		return NOTIFY_DONE;

	if (die_val == DIE_INT3) {
		if (args->regs->ip == target_after_int3) {
			preempt_disable();
			args->regs->ip = bypass_eip;
			return NOTIFY_STOP;
		} else if (args->regs->ip == bypass_after_int3) {
			args->regs->ip = after_imv;
			preempt_enable();
			return NOTIFY_STOP;
		}
	}
	return NOTIFY_DONE;
}

static struct notifier_block imv_notify = {
	.notifier_call = imv_notifier,
	.priority = 0x7fffffff,	/* we need to be notified first */
};

/**
 * arch_imv_update - update one immediate value
 * @imv: pointer of type const struct __imv to update
 * @early: early boot (1) or normal (0)
 *
 * Update one immediate value. Must be called with imv_mutex held.
 */
__kprobes int arch_imv_update(const struct __imv *imv, int early)
{
	int ret;
	unsigned char opcode_size = imv->insn_size - imv->size;
	unsigned long insn = imv->imv - opcode_size;
	unsigned long len;
	char *vaddr;
	struct page *pages[1];

#ifdef CONFIG_KPROBES
	/*
	 * Fail if a kprobe has been set on this instruction.
	 * (TODO: we could eventually do better and modify all the (possibly
	 * nested) kprobes for this site if kprobes had an API for this.
	 */
	if (unlikely(!early
			&& *(unsigned char *)insn == BREAKPOINT_INSTRUCTION)) {
		printk(KERN_WARNING "Immediate value in conflict with kprobe. "
				    "Variable at %p, "
				    "instruction at %p, size %hu\n",
				    (void *)imv->imv,
				    (void *)imv->var, imv->size);
		return -EBUSY;
	}
#endif

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
#ifdef CONFIG_X86_64
	case 8:	if (*(uint64_t *)imv->imv
				== *(uint64_t *)imv->var)
			return 0;
		break;
#endif
	default:return -EINVAL;
	}

	if (!early) {
		/* bypass is 10 bytes long for x86_64 long */
		WARN_ON(imv->insn_size > 10);
		_imv_bypass(&bypass_eip, &bypass_after_int3);

		after_imv = imv->imv + imv->size;

		/*
		 * Using the _early variants because nobody is executing the
		 * bypass code while we patch it. It is protected by the
		 * imv_mutex. Since we modify the instructions non atomically
		 * (for nops), we have to use the _early variant.
		 * We must however deal with RO pages.
		 * Use a single page : 10 bytes are aligned on 16 bytes
		 * boundaries.
		 */
		pages[0] = virt_to_page((void *)bypass_eip);
		vaddr = vmap(pages, 1, VM_MAP, PAGE_KERNEL);
		BUG_ON(!vaddr);
		text_poke_early(&vaddr[bypass_eip & ~PAGE_MASK],
			(void *)insn, imv->insn_size);
		/*
		 * Fill the rest with nops.
		 */
		len = NR_NOPS - imv->insn_size;
		add_nops((void *)
			&vaddr[(bypass_eip & ~PAGE_MASK) + imv->insn_size],
			len);
		vunmap(vaddr);

		target_after_int3 = insn + BREAKPOINT_INS_LEN;
		/* register_die_notifier has memory barriers */
		register_die_notifier(&imv_notify);
		/* The breakpoint will single-step the bypass */
		text_poke((void *)insn,
			((unsigned char[]){BREAKPOINT_INSTRUCTION}), 1);
		/*
		 * Make sure the breakpoint is set before we continue (visible
		 * to other CPUs and interrupts).
		 */
		wmb();
		/*
		 * Execute serializing instruction on each CPU.
		 */
		ret = on_each_cpu(imv_synchronize_core, NULL, 1, 1);
		BUG_ON(ret != 0);

		text_poke((void *)(insn + opcode_size), (void *)imv->var,
				imv->size);
		/*
		 * Make sure the value can be seen from other CPUs and
		 * interrupts.
		 */
		wmb();
		text_poke((void *)insn, (unsigned char *)bypass_eip, 1);
		/*
		 * Wait for all int3 handlers to end (interrupts are disabled in
		 * int3). This CPU is clearly not in a int3 handler, because
		 * int3 handler is not preemptible and there cannot be any more
		 * int3 handler called for this site, because we placed the
		 * original instruction back.  synchronize_sched has memory
		 * barriers.
		 */
		synchronize_sched();
		unregister_die_notifier(&imv_notify);
		/* unregister_die_notifier has memory barriers */
	} else
		text_poke_early((void *)imv->imv, (void *)imv->var,
			imv->size);
	return 0;
}
