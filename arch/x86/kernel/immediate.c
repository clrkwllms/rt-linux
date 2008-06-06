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
#define JMP_REL32		0xe9
#define BREAKPOINT_INS_LEN	1
#define NR_NOPS			10

/*#define DEBUG_IMMEDIATE 1*/

#ifdef DEBUG_IMMEDIATE
#define printk_dbg printk
#else
#define printk_dbg(fmt , a...)
#endif

static unsigned long target_after_int3;	/* EIP of the target after the int3 */
static unsigned long bypass_eip;	/* EIP of the bypass. */
static unsigned long bypass_after_int3;	/* EIP after the end-of-bypass int3 */
static unsigned long after_imv;		/*
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
			/* deal with non-relocatable jmp instructions */
			switch (*(uint8_t *)bypass_eip) {
			case JMP_REL32: /* e9 cw    jmp rel16 (valid on ia32) */
					/* e9 cd    jmp rel32 */
					/* Skip the 4 bytes (jump offset)
					 * following the breakpoint. */
				args->regs->ip +=
					*(int *)(bypass_eip + 1) + 4;
				/* The bypass won't be executed. */
				return NOTIFY_STOP;
			}
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

/*
 * returns -1 if not found
 * return 0 if found.
 */
static inline int detect_mov_test_jne(uint8_t *addr, uint8_t **opcode,
		uint8_t **jmp_offset, int *offset_len)
{
	uint8_t *addr1, *addr2;

	printk_dbg(KERN_DEBUG "Trying at %p %hx %hx %hx %hx %hx %hx\n",
		addr, addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
	/*
	 * b8 imm32        mov  imm32,%eax or
	 * c9 XX XX XX XX  jmp  rel32 (patched instruction)
	 */
	if (addr[0] != 0xb8 && addr[0] != JMP_REL32)
		return -1;
	/* 85 c0    test %eax,%eax */
	if (addr[5] != 0x85 || addr[6] != 0xc0)
		return -1;
	printk_dbg(KERN_DEBUG "Found test %%eax,%%eax at %p\n", addr + 5);
	switch (addr[7]) {
	case 0x75: /* 75 cb       jne rel8 */
		printk_dbg(KERN_DEBUG "Found jne rel8 at %p\n", addr + 7);
		*opcode = addr + 7;
		*jmp_offset = addr + 8;
		*offset_len = 1;
		/* addr1 is the address following the branch instruction */
		addr1 = addr + 9;
		/* addr2 is the branch target */
		addr2 = addr1 + addr[8];
		break;
	case 0x0f:
		switch (addr[8]) {
		case 0x85:	 /* 0F 85 cw    jne rel16 (valid on ia32) */
				 /* 0F 85 cd    jne rel32 */
			printk_dbg(KERN_DEBUG "Found jne rel16/32 at %p\n",
				addr + 8);
			*opcode = addr + 7;
			*jmp_offset = addr + 9;
			*offset_len = 4;
			/*
			 * addr1 is the address following the branch
			 * instruction
			 */
			addr1 = addr + 13;
			/* addr2 is the branch target */
			addr2 = addr1 + *(uint32_t *)(addr + 9);
			break;
		default:
			return -1;
		}
		break;
	default: return -1;
	}
	/*
	 * Now check that the pattern was squeezed between the mov instruction
	 * end the two epilogues (branch taken and not taken), which ensure
	 * %eax and ZF liveliness is limited to our instructions.
	 */
	if (!is_imv_cond_end((unsigned long)addr1, (unsigned long)addr2))
		return -1;
	return 0;
}

/*
 * returns -1 if not found
 * return 0 if found.
 */
static inline int detect_mov_test_je(uint8_t *addr, uint8_t **opcode,
		uint8_t **jmp_offset, int *offset_len)
{
	uint8_t *addr1, *addr2;

	/*
	 * b8 imm32        mov  imm32,%eax or
	 * c9 XX XX XX XX  jmp  rel32 (patched instruction)
	 */
	if (addr[0] != 0xb8 && addr[0] != JMP_REL32)
		return -1;
	/* 85 c0    test %eax,%eax */
	if (addr[5] != 0x85 || addr[6] != 0xc0)
		return -1;
	printk_dbg(KERN_DEBUG "Found test %%eax,%%eax at %p\n", addr + 5);
	switch (addr[7]) {
	case 0x74: /* 74 cb       je rel8 */
		printk_dbg(KERN_DEBUG "Found je rel8 at %p\n", addr + 7);
		*opcode = addr + 7;
		*jmp_offset = addr + 8;
		*offset_len = 1;
		/* addr1 is the address following the branch instruction */
		addr1 = addr + 9;
		/* addr2 is the branch target */
		addr2 = addr1 + addr[8];
		break;
	case 0x0f:
		switch (addr[8]) {
		case 0x84:	 /* 0F 84 cw    je rel16 (valid on ia32) */
				 /* 0F 84 cd    je rel32 */
			printk_dbg(KERN_DEBUG "Found je rel16/32 at %p\n",
				addr + 8);
			*opcode = addr + 7;
			*jmp_offset = addr + 9;
			*offset_len = 4;
			/*
			 * addr1 is the address following the branch
			 * instruction
			 */
			addr1 = addr + 13;
			/* addr2 is the branch target */
			addr2 = addr1 + *(uint32_t *)(addr + 9);
			break;
		default:
			return -1;
		}
		break;
	default: return -1;
	}
	/*
	 * Now check that the pattern was squeezed between the mov instruction
	 * end the two epilogues (branch taken and not taken), which ensure
	 * %eax and ZF liveliness is limited to our instructions.
	 */
	if (!is_imv_cond_end((unsigned long)addr1, (unsigned long)addr2))
		return -1;
	return 0;
}

static int static_early;

/*
 * Marked noinline because we prefer to have only one _imv_bypass. Not that it
 * is required, but there is no need to edit two bypasses.
 */
static noinline int replace_instruction_safe(uint8_t *addr, uint8_t *newcode,
		int size)
{
	char *vaddr;
	struct page *pages[1];
	int len;
	int ret;

	/* bypass is 10 bytes long for x86_64 long */
	WARN_ON(size > 10);

	_imv_bypass(&bypass_eip, &bypass_after_int3);

	if (!static_early) {
		after_imv = (unsigned long)addr + size;

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
			(void *)addr, size);
		/*
		 * Fill the rest with nops.
		 */
		len = NR_NOPS - size;
		add_nops((void *)
			&vaddr[(bypass_eip & ~PAGE_MASK) + size],
			len);
		vunmap(vaddr);

		target_after_int3 = (unsigned long)addr + BREAKPOINT_INS_LEN;
		/* register_die_notifier has memory barriers */
		register_die_notifier(&imv_notify);
		/* The breakpoint will execute the bypass */
		text_poke((void *)addr,
			((unsigned char[]){BREAKPOINT_INSTRUCTION}),
			BREAKPOINT_INS_LEN);
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

		text_poke((void *)(addr + BREAKPOINT_INS_LEN),
			&newcode[BREAKPOINT_INS_LEN],
			size - BREAKPOINT_INS_LEN);
		/*
		 * Make sure the value can be seen from other CPUs and
		 * interrupts.
		 */
		wmb();
#ifdef DEBUG_IMMEDIATE
		mdelay(10);	/* lets the breakpoint for a while */
#endif
		text_poke(addr, newcode, BREAKPOINT_INS_LEN);
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
		text_poke_early(addr, newcode, size);
	return 0;
}

static int patch_jump_target(struct __imv *imv)
{
	uint8_t *opcode, *jmp_offset;
	int offset_len;
	int mov_test_j_found = 0;
	unsigned long logicvar;

	if (!detect_mov_test_jne((uint8_t *)imv->imv - 1,
			&opcode, &jmp_offset, &offset_len)) {
		logicvar = imv->var;	/* positive logic */
		mov_test_j_found = 1;
	} else if (!detect_mov_test_je((uint8_t *)imv->imv - 1,
			&opcode, &jmp_offset, &offset_len)) {
		logicvar = !imv->var;	/* negative logic */
		mov_test_j_found = 1;
	}

	if (mov_test_j_found) {
		int newoff;

		if (offset_len == 1) {
			if (logicvar)
				newoff = *(signed char *)jmp_offset;
			else
				newoff = 0;
			newoff += 4; /* jump over test, branch */
		} else {
			if (logicvar)
				newoff = *(int *)jmp_offset;
			else
				newoff = 0;
			newoff += 8; /*
				      * jump over test (2 bytes)
				      * and branch (6 bytes).
				      */
		}
		/* Speed up by skipping if not changed */
		if (*(uint8_t *)(imv->imv - 1) == JMP_REL32 &&
				*(int *)imv->imv == newoff)
			return 0;
		/* replace with a 5 bytes jump */
		replace_instruction_safe((uint8_t *)imv->imv - 1,
			((unsigned char[]){ JMP_REL32,
			((unsigned char *)&newoff)[0],
			((unsigned char *)&newoff)[1],
			((unsigned char *)&newoff)[2],
			((unsigned char *)&newoff)[3] }),
			5);
		return 0;
	}
	/* Nothing known found. */
	return -1;
}

/**
 * arch_imv_update - update one immediate value
 * @imv: pointer of type const struct __imv to update
 * @early: early boot (1) or normal (0)
 *
 * Update one immediate value. Must be called with imv_mutex held.
 */
__kprobes int arch_imv_update(struct __imv *imv, int early)
{
	int ret;
	uint8_t buf[10];
	unsigned long insn, opcode_size;

	static_early = early;

	/*
	 * If imv_cond is encountered, try to patch it with
	 * patch_jump_target. Continue with normal immediate values if the area
	 * surrounding the instruction is not as expected.
	 */
	if (imv->size == 0) {
		ret = patch_jump_target(imv);
		if (ret) {
#ifdef DEBUG_IMMEDIATE
			static int nr_fail;
			printk(KERN_DEBUG
				"Jump target fallback at %lX, nr fail %d\n",
				imv->imv, ++nr_fail);
#endif
			imv->size = 4;
		} else {
#ifdef DEBUG_IMMEDIATE
			static int nr_success;
			printk(KERN_DEBUG "Jump target at %lX, nr success %d\n",
				imv->imv, ++nr_success);
#endif
			return 0;
		}
	}

	opcode_size = imv->insn_size - imv->size;
	insn = imv->imv - opcode_size;

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
				    (void *)imv->var,
				    (void *)imv->imv, imv->size);
		return -EBUSY;
	}
#endif

	/*
	 * If the variable and the instruction have the same value, there is
	 * nothing to do.
	 */
	BUG_ON(imv->var_size > imv->size);
	switch (imv->var_size) {
	case 1:	if (*(uint8_t *)imv->imv == *(uint8_t *)imv->var)
			return 0;
		break;
	case 2:	if (*(uint16_t *)imv->imv == *(uint16_t *)imv->var)
			return 0;
		break;
	case 4:	if (*(uint32_t *)imv->imv == *(uint32_t *)imv->var)
			return 0;
		break;
#ifdef CONFIG_X86_64
	case 8:	if (*(uint64_t *)imv->imv == *(uint64_t *)imv->var)
			return 0;
		break;
#endif
	default:return -EINVAL;
	}

	memcpy(buf, (uint8_t *)insn, opcode_size);
	memcpy(&buf[opcode_size], (void *)imv->var, imv->var_size);
	/* pad MSBs with 0 */
	memset(&buf[opcode_size + imv->var_size], 0, imv->size - imv->var_size);
	replace_instruction_safe((uint8_t *)insn, buf, imv->insn_size);

	return 0;
}
