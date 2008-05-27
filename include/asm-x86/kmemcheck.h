#ifndef ASM_X86_KMEMCHECK_H
#define ASM_X86_KMEMCHECK_H

#include <linux/percpu.h>
#include <asm/pgtable.h>

enum kmemcheck_method {
	KMEMCHECK_READ,
	KMEMCHECK_WRITE,
};

#ifdef CONFIG_KMEMCHECK
bool kmemcheck_active(struct pt_regs *regs);

void kmemcheck_show(struct pt_regs *regs);
void kmemcheck_hide(struct pt_regs *regs);

void kmemcheck_access(struct pt_regs *regs,
	unsigned long address, enum kmemcheck_method method);
#else
static inline bool kmemcheck_active(struct pt_regs *regs) { return false; }

static inline void kmemcheck_show(struct pt_regs *regs) { }
static inline void kmemcheck_hide(struct pt_regs *regs) { }

static inline void kmemcheck_access(struct pt_regs *regs,
	unsigned long address, enum kmemcheck_method method) { }
#endif /* CONFIG_KMEMCHECK */

#endif
