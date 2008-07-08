/**
 * kmemcheck - a heavyweight memory checker for the linux kernel
 * Copyright (C) 2007, 2008  Vegard Nossum <vegardno@ifi.uio.no>
 * (With a lot of help from Ingo Molnar and Pekka Enberg.)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License (version 2) as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kallsyms.h>
#include <linux/kernel.h>
#include <linux/kmemcheck.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/page-flags.h>
#include <linux/percpu.h>
#include <linux/ptrace.h>
#include <linux/string.h>
#include <linux/types.h>

#include <asm/cacheflush.h>
#include <asm/kmemcheck.h>
#include <asm/pgtable.h>
#include <asm/tlbflush.h>

#include "error.h"
#include "opcode.h"
#include "pte.h"
#include "shadow.h"
#include "smp.h"

void __init kmemcheck_init(void)
{
	printk(KERN_INFO "kmemcheck: \"Bugs, beware!\"\n");

	kmemcheck_smp_init();

#if defined(CONFIG_SMP) && !defined(CONFIG_KMEMCHECK_USE_SMP)
	/*
	 * Limit SMP to use a single CPU. We rely on the fact that this code
	 * runs before SMP is set up.
	 */
	if (setup_max_cpus > 1) {
		printk(KERN_INFO
			"kmemcheck: Limiting number of CPUs to 1.\n");
		setup_max_cpus = 1;
	}
#endif
}

#ifdef CONFIG_KMEMCHECK_DISABLED_BY_DEFAULT
int kmemcheck_enabled = 0;
#endif

#ifdef CONFIG_KMEMCHECK_ENABLED_BY_DEFAULT
int kmemcheck_enabled = 1;
#endif

#ifdef CONFIG_KMEMCHECK_ONESHOT_BY_DEFAULT
int kmemcheck_enabled = 2;
#endif

/*
 * We need to parse the kmemcheck= option before any memory is allocated.
 */
static int __init param_kmemcheck(char *str)
{
	if (!str)
		return -EINVAL;

	sscanf(str, "%d", &kmemcheck_enabled);
	return 0;
}

early_param("kmemcheck", param_kmemcheck);

int kmemcheck_show_addr(unsigned long address)
{
	pte_t *pte;

	pte = kmemcheck_pte_lookup(address);
	if (!pte)
		return 0;

	set_pte(pte, __pte(pte_val(*pte) | _PAGE_PRESENT));
	__flush_tlb_one(address);
	return 1;
}

int kmemcheck_hide_addr(unsigned long address)
{
	pte_t *pte;

	pte = kmemcheck_pte_lookup(address);
	if (!pte)
		return 0;

	set_pte(pte, __pte(pte_val(*pte) & ~_PAGE_PRESENT));
	__flush_tlb_one(address);
	return 1;
}

struct kmemcheck_context {
	bool busy;
	int balance;

	unsigned long addr1;
	unsigned long addr2;
	unsigned long flags;
};

static DEFINE_PER_CPU(struct kmemcheck_context, kmemcheck_context);

bool kmemcheck_active(struct pt_regs *regs)
{
	struct kmemcheck_context *data = &__get_cpu_var(kmemcheck_context);

	return data->balance > 0;
}

/*
 * Called from the #PF handler.
 */
void kmemcheck_show(struct pt_regs *regs)
{
	struct kmemcheck_context *data = &__get_cpu_var(kmemcheck_context);
	int n;

	BUG_ON(!irqs_disabled());

	kmemcheck_pause_allbutself();

	if (unlikely(data->balance != 0)) {
		kmemcheck_show_addr(data->addr1);
		kmemcheck_show_addr(data->addr2);
		kmemcheck_error_save_bug(regs);
		data->balance = 0;
		kmemcheck_resume();
		return;
	}

	n = 0;
	n += kmemcheck_show_addr(data->addr1);
	n += kmemcheck_show_addr(data->addr2);

	/*
	 * None of the addresses actually belonged to kmemcheck. Note that
	 * this is not an error.
	 */
	if (n == 0) {
		kmemcheck_resume();
		return;
	}

	++data->balance;

	/*
	 * The IF needs to be cleared as well, so that the faulting
	 * instruction can run "uninterrupted". Otherwise, we might take
	 * an interrupt and start executing that before we've had a chance
	 * to hide the page again.
	 *
	 * NOTE: In the rare case of multiple faults, we must not override
	 * the original flags:
	 */
	if (!(regs->flags & X86_EFLAGS_TF))
		data->flags = regs->flags;

	regs->flags |= X86_EFLAGS_TF;
	regs->flags &= ~X86_EFLAGS_IF;
}

/*
 * Called from the #DB handler.
 */
void kmemcheck_hide(struct pt_regs *regs)
{
	struct kmemcheck_context *data = &__get_cpu_var(kmemcheck_context);
	int n;

	BUG_ON(!irqs_disabled());

	if (data->balance == 0) {
		kmemcheck_resume();
		return;
	}

	if (unlikely(data->balance != 1)) {
		kmemcheck_show_addr(data->addr1);
		kmemcheck_show_addr(data->addr2);
		kmemcheck_error_save_bug(regs);
		data->addr1 = 0;
		data->addr2 = 0;
		data->balance = 0;

		if (!(data->flags & X86_EFLAGS_TF))
			regs->flags &= ~X86_EFLAGS_TF;
		if (data->flags & X86_EFLAGS_IF)
			regs->flags |= X86_EFLAGS_IF;
		kmemcheck_resume();
		return;
	}

	n = 0;
	if (kmemcheck_enabled) {
		n += kmemcheck_hide_addr(data->addr1);
		n += kmemcheck_hide_addr(data->addr2);
	} else {
		n += kmemcheck_show_addr(data->addr1);
		n += kmemcheck_show_addr(data->addr2);
	}

	if (n == 0) {
		kmemcheck_resume();
		return;
	}

	--data->balance;

	data->addr1 = 0;
	data->addr2 = 0;

	if (!(data->flags & X86_EFLAGS_TF))
		regs->flags &= ~X86_EFLAGS_TF;
	if (data->flags & X86_EFLAGS_IF)
		regs->flags |= X86_EFLAGS_IF;
	kmemcheck_resume();
}

void kmemcheck_show_pages(struct page *p, unsigned int n)
{
	unsigned int i;

	for (i = 0; i < n; ++i) {
		unsigned long address;
		pte_t *pte;
		unsigned int level;

		address = (unsigned long) page_address(&p[i]);
		pte = lookup_address(address, &level);
		BUG_ON(!pte);
		BUG_ON(level != PG_LEVEL_4K);

		set_pte(pte, __pte(pte_val(*pte) | _PAGE_PRESENT));
		set_pte(pte, __pte(pte_val(*pte) & ~_PAGE_HIDDEN));
		__flush_tlb_one(address);
	}
}

bool kmemcheck_page_is_tracked(struct page *p)
{
	/* This will also check the "hidden" flag of the PTE. */
	return kmemcheck_pte_lookup((unsigned long) page_address(p));
}

void kmemcheck_hide_pages(struct page *p, unsigned int n)
{
	unsigned int i;

	set_memory_4k((unsigned long) page_address(p), n);

	for (i = 0; i < n; ++i) {
		unsigned long address;
		pte_t *pte;
		unsigned int level;

		address = (unsigned long) page_address(&p[i]);
		pte = lookup_address(address, &level);
		BUG_ON(!pte);
		BUG_ON(level != PG_LEVEL_4K);

		set_pte(pte, __pte(pte_val(*pte) & ~_PAGE_PRESENT));
		set_pte(pte, __pte(pte_val(*pte) | _PAGE_HIDDEN));
		__flush_tlb_one(address);
	}
}

/*
 * Check that an access does not span across two different pages, because
 * that will mess up our shadow lookup.
 */
static bool check_page_boundary(struct pt_regs *regs,
	unsigned long addr, unsigned int size)
{
	if (size == 8)
		return false;
	if (size == 16 && (addr & PAGE_MASK) == ((addr + 1) & PAGE_MASK))
		return false;
	if (size == 32 && (addr & PAGE_MASK) == ((addr + 3) & PAGE_MASK))
		return false;
#ifdef CONFIG_X86_64
	if (size == 64 && (addr & PAGE_MASK) == ((addr + 7) & PAGE_MASK))
		return false;
#endif

	/*
	 * XXX: The addr/size data is also really interesting if this
	 * case ever triggers. We should make a separate class of errors
	 * for this case. -Vegard
	 */
	kmemcheck_error_save_bug(regs);
	return true;
}

static void kmemcheck_read(struct pt_regs *regs,
	unsigned long address, unsigned int size)
{
	void *shadow;
	enum kmemcheck_shadow status;

	shadow = kmemcheck_shadow_lookup(address);
	if (!shadow)
		return;

	if (check_page_boundary(regs, address, size))
		return;

	status = kmemcheck_shadow_test(shadow, size);
	if (status == KMEMCHECK_SHADOW_INITIALIZED)
		return;

	if (kmemcheck_enabled)
		kmemcheck_error_save(status, address, size, regs);

	if (kmemcheck_enabled == 2)
		kmemcheck_enabled = 0;

	/* Don't warn about it again. */
	kmemcheck_shadow_set(shadow, size);
}

static void kmemcheck_write(struct pt_regs *regs,
	unsigned long address, unsigned int size)
{
	void *shadow;

	shadow = kmemcheck_shadow_lookup(address);
	if (!shadow)
		return;

	if (check_page_boundary(regs, address, size))
		return;

	kmemcheck_shadow_set(shadow, size);
}

enum kmemcheck_method {
	KMEMCHECK_READ,
	KMEMCHECK_WRITE,
};

static void kmemcheck_access(struct pt_regs *regs,
	unsigned long fallback_address, enum kmemcheck_method fallback_method)
{
	const uint8_t *insn;
	const uint8_t *insn_primary;
	unsigned int size;

	struct kmemcheck_context *data = &__get_cpu_var(kmemcheck_context);

	/* Recursive fault -- ouch. */
	if (data->busy) {
		kmemcheck_show_addr(fallback_address);
		kmemcheck_error_save_bug(regs);
		return;
	}

	data->busy = true;

	insn = (const uint8_t *) regs->ip;
	insn_primary = kmemcheck_opcode_get_primary(insn);

	size = kmemcheck_opcode_get_size(insn);

	switch (insn_primary[0]) {
#ifdef CONFIG_KMEMCHECK_BITOPS_OK
		/* AND, OR, XOR */
		/*
		 * Unfortunately, these instructions have to be excluded from
		 * our regular checking since they access only some (and not
		 * all) bits. This clears out "bogus" bitfield-access warnings.
		 */
	case 0x80:
	case 0x81:
	case 0x82:
	case 0x83:
		switch ((insn_primary[1] >> 3) & 7) {
			/* OR */
		case 1:
			/* AND */
		case 4:
			/* XOR */
		case 6:
			kmemcheck_write(regs, fallback_address, size);
			data->addr1 = fallback_address;
			data->addr2 = 0;
			data->busy = false;
			return;

			/* ADD */
		case 0:
			/* ADC */
		case 2:
			/* SBB */
		case 3:
			/* SUB */
		case 5:
			/* CMP */
		case 7:
			break;
		}
		break;
#endif

		/* MOVS, MOVSB, MOVSW, MOVSD */
	case 0xa4:
	case 0xa5:
		/*
		 * These instructions are special because they take two
		 * addresses, but we only get one page fault.
		 */
		kmemcheck_read(regs, regs->si, size);
		kmemcheck_write(regs, regs->di, size);
		data->addr1 = regs->si;
		data->addr2 = regs->di;
		data->busy = false;
		return;

		/* CMPS, CMPSB, CMPSW, CMPSD */
	case 0xa6:
	case 0xa7:
		kmemcheck_read(regs, regs->si, size);
		kmemcheck_read(regs, regs->di, size);
		data->addr1 = regs->si;
		data->addr2 = regs->di;
		data->busy = false;
		return;
	}

	/*
	 * If the opcode isn't special in any way, we use the data from the
	 * page fault handler to determine the address and type of memory
	 * access.
	 */
	switch (fallback_method) {
	case KMEMCHECK_READ:
		kmemcheck_read(regs, fallback_address, size);
		data->addr1 = fallback_address;
		data->addr2 = 0;
		data->busy = false;
		return;
	case KMEMCHECK_WRITE:
		kmemcheck_write(regs, fallback_address, size);
		data->addr1 = fallback_address;
		data->addr2 = 0;
		data->busy = false;
		return;
	}
}

bool kmemcheck_fault(struct pt_regs *regs, unsigned long address,
	unsigned long error_code)
{
	pte_t *pte;
	unsigned int level;

	pte = lookup_address(address, &level);
	if (!pte)
		return false;
	if (level != PG_LEVEL_4K)
		return false;
	if (!pte_hidden(*pte))
		return false;

	if (error_code & 2)
		kmemcheck_access(regs, address, KMEMCHECK_WRITE);
	else
		kmemcheck_access(regs, address, KMEMCHECK_READ);

	kmemcheck_show(regs);
	return true;
}
