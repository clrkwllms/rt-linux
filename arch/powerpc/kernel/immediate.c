/*
 * Powerpc optimized immediate values enabling/disabling.
 *
 * Mathieu Desnoyers <mathieu.desnoyers@polymtl.ca>
 */

#include <linux/module.h>
#include <linux/immediate.h>
#include <linux/string.h>
#include <linux/kprobes.h>
#include <asm/cacheflush.h>
#include <asm/page.h>

#define LI_OPCODE_LEN	2

/**
 * arch_imv_update - update one immediate value
 * @imv: pointer of type const struct __imv to update
 * @early: early boot (1), normal (0)
 *
 * Update one immediate value. Must be called with imv_mutex held.
 */
int arch_imv_update(const struct __imv *imv, int early)
{
#ifdef CONFIG_KPROBES
	kprobe_opcode_t *insn;
	/*
	 * Fail if a kprobe has been set on this instruction.
	 * (TODO: we could eventually do better and modify all the (possibly
	 * nested) kprobes for this site if kprobes had an API for this.
	 */
	switch (imv->size) {
	case 1:	/* The uint8_t points to the 3rd byte of the
		 * instruction */
		insn = (void *)(imv->imv - 1 - LI_OPCODE_LEN);
		break;
	case 2:	insn = (void *)(imv->imv - LI_OPCODE_LEN);
		break;
	default:
	return -EINVAL;
	}

	if (unlikely(!early && *insn == BREAKPOINT_INSTRUCTION)) {
		printk(KERN_WARNING "Immediate value in conflict with kprobe. "
				    "Variable at %p, "
				    "instruction at %p, size %lu\n",
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
	case 1:	if (*(uint8_t *)imv->imv == *(uint8_t *)imv->var)
			return 0;
		*(uint8_t *)imv->imv = *(uint8_t *)imv->var;
		break;
	case 2:	if (*(uint16_t *)imv->imv == *(uint16_t *)imv->var)
			return 0;
		*(uint16_t *)imv->imv = *(uint16_t *)imv->var;
		break;
	default:return -EINVAL;
	}
	flush_icache_range(imv->imv, imv->imv + imv->size);
	return 0;
}
