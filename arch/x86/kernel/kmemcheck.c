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
#include <linux/kallsyms.h>
#include <linux/kdebug.h>
#include <linux/kernel.h>
#include <linux/kmemcheck.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/page-flags.h>
#include <linux/stacktrace.h>
#include <linux/timer.h>

#include <asm/cacheflush.h>
#include <asm/kmemcheck.h>
#include <asm/pgtable.h>
#include <asm/string.h>
#include <asm/tlbflush.h>

enum shadow {
	SHADOW_UNALLOCATED,
	SHADOW_UNINITIALIZED,
	SHADOW_INITIALIZED,
	SHADOW_FREED,
};

enum kmemcheck_error_type {
	ERROR_INVALID_ACCESS,
	ERROR_BUG,
};

#define SHADOW_COPY_SIZE (1 << CONFIG_KMEMCHECK_SHADOW_COPY_SHIFT)

struct kmemcheck_error {
	enum kmemcheck_error_type type;

	union {
		/* ERROR_INVALID_ACCESS */
		struct {
			/* Kind of access that caused the error */
			enum shadow	state;
			/* Address and size of the erroneous read */
			unsigned long	address;
			unsigned int	size;
		};
	};

	struct pt_regs		regs;
	struct stack_trace	trace;
	unsigned long		trace_entries[32];

	enum shadow shadow_copy[SHADOW_COPY_SIZE];
};

/*
 * Create a ring queue of errors to output. We can't call printk() directly
 * from the kmemcheck traps, since this may call the console drivers and
 * result in a recursive fault.
 */
static struct kmemcheck_error error_fifo[CONFIG_KMEMCHECK_QUEUE_SIZE];
static unsigned int error_count;
static unsigned int error_rd;
static unsigned int error_wr;
static unsigned int error_missed_count;

static struct timer_list kmemcheck_timer;

static struct kmemcheck_error *
error_next_wr(void)
{
	struct kmemcheck_error *e;

	if (error_count == ARRAY_SIZE(error_fifo)) {
		++error_missed_count;
		return NULL;
	}

	e = &error_fifo[error_wr];
	if (++error_wr == ARRAY_SIZE(error_fifo))
		error_wr = 0;
	++error_count;
	return e;
}

static struct kmemcheck_error *
error_next_rd(void)
{
	struct kmemcheck_error *e;

	if (error_count == 0)
		return NULL;

	e = &error_fifo[error_rd];
	if (++error_rd == ARRAY_SIZE(error_fifo))
		error_rd = 0;
	--error_count;
	return e;
}

static void *
address_get_shadow(unsigned long address);

/*
 * Save the context of an error.
 */
static void
error_save(enum shadow state, unsigned long address, unsigned int size,
	struct pt_regs *regs)
{
	static unsigned long prev_ip;

	struct kmemcheck_error *e;
	enum shadow *shadow_copy;

	/* Don't report several adjacent errors from the same EIP. */
	if (regs->ip == prev_ip)
		return;
	prev_ip = regs->ip;

	e = error_next_wr();
	if (!e)
		return;

	e->type = ERROR_INVALID_ACCESS;

	e->state = state;
	e->address = address;
	e->size = size;

	/* Save regs */
	memcpy(&e->regs, regs, sizeof(*regs));

	/* Save stack trace */
	e->trace.nr_entries = 0;
	e->trace.entries = e->trace_entries;
	e->trace.max_entries = ARRAY_SIZE(e->trace_entries);
	e->trace.skip = 1;
	save_stack_trace(&e->trace);

	/* Round address down to nearest 16 bytes */
	shadow_copy = address_get_shadow(address & ~(SHADOW_COPY_SIZE - 1));
	BUG_ON(!shadow_copy);

	memcpy(e->shadow_copy, shadow_copy, SHADOW_COPY_SIZE);
}

/*
 * Save the context of a kmemcheck bug.
 */
static void
error_save_bug(struct pt_regs *regs)
{
	struct kmemcheck_error *e;

	e = error_next_wr();
	if (!e)
		return;

	e->type = ERROR_BUG;

	memcpy(&e->regs, regs, sizeof(*regs));

	e->trace.nr_entries = 0;
	e->trace.entries = e->trace_entries;
	e->trace.max_entries = ARRAY_SIZE(e->trace_entries);
	e->trace.skip = 1;
	save_stack_trace(&e->trace);
}

static void
error_recall(void)
{
	static const char *desc[] = {
		[SHADOW_UNALLOCATED]	= "unallocated",
		[SHADOW_UNINITIALIZED]	= "uninitialized",
		[SHADOW_INITIALIZED]	= "initialized",
		[SHADOW_FREED]		= "freed",
	};

	static const char short_desc[] = {
		[SHADOW_UNALLOCATED]	= 'a',
		[SHADOW_UNINITIALIZED]	= 'u',
		[SHADOW_INITIALIZED]	= 'i',
		[SHADOW_FREED]		= 'f',
	};

	struct kmemcheck_error *e;
	unsigned int i;

	e = error_next_rd();
	if (!e)
		return;

	switch (e->type) {
	case ERROR_INVALID_ACCESS:
		printk(KERN_ERR  "kmemcheck: Caught %d-bit read "
			"from %s memory (%p)\n",
			e->size, e->state < ARRAY_SIZE(desc) ?
				desc[e->state] : "(invalid shadow state)",
			(void *) e->address);

		printk(KERN_INFO);
		for (i = 0; i < SHADOW_COPY_SIZE; ++i) {
			if (e->shadow_copy[i] < ARRAY_SIZE(short_desc))
				printk("%c", short_desc[e->shadow_copy[i]]);
			else
				printk("?");
		}
		printk("\n");
		printk(KERN_INFO "%*c\n",
			1 + (int) (e->address & (SHADOW_COPY_SIZE - 1)), '^');
		break;
	case ERROR_BUG:
		printk(KERN_EMERG "kmemcheck: Fatal error\n");
		break;
	}

	__show_regs(&e->regs, 1);
	print_stack_trace(&e->trace, 0);
}

static void
do_wakeup(unsigned long data)
{
	while (error_count > 0)
		error_recall();

	if (error_missed_count > 0) {
		printk(KERN_WARNING "kmemcheck: Lost %d error reports because "
			"the queue was too small\n", error_missed_count);
		error_missed_count = 0;
	}

	mod_timer(&kmemcheck_timer, kmemcheck_timer.expires + HZ);
}

void __init
kmemcheck_init(void)
{
	printk(KERN_INFO "kmemcheck: \"Bugs, beware!\"\n");

#ifdef CONFIG_SMP
	/* Limit SMP to use a single CPU. We rely on the fact that this code
	 * runs before SMP is set up. */
	if (setup_max_cpus > 1) {
		printk(KERN_INFO
			"kmemcheck: Limiting number of CPUs to 1.\n");
		setup_max_cpus = 1;
	}
#endif

	setup_timer(&kmemcheck_timer, &do_wakeup, 0);
	mod_timer(&kmemcheck_timer, jiffies + HZ);
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
static int __init
param_kmemcheck(char *str)
{
	if (!str)
		return -EINVAL;

	sscanf(str, "%d", &kmemcheck_enabled);
	return 0;
}

early_param("kmemcheck", param_kmemcheck);

static pte_t *
address_get_pte(unsigned long address)
{
	pte_t *pte;
	unsigned int level;

	pte = lookup_address(address, &level);
	if (!pte)
		return NULL;
	if (level != PG_LEVEL_4K)
		return NULL;
	if (!pte_hidden(*pte))
		return NULL;

	return pte;
}

/*
 * Return the shadow address for the given address. Returns NULL if the
 * address is not tracked.
 *
 * We need to be extremely careful not to follow any invalid pointers,
 * because this function can be called for *any* possible address.
 */
static void *
address_get_shadow(unsigned long address)
{
	pte_t *pte;
	struct page *page;
	struct page *head;

	if (!virt_addr_valid(address))
		return NULL;

	pte = address_get_pte(address);
	if (!pte)
		return NULL;

	page = virt_to_page(address);
	head = compound_head(page);
	return head->shadow + ((void *) address - page_address(head));
}

static int
show_addr(unsigned long address)
{
	pte_t *pte;

	pte = address_get_pte(address);
	if (!pte)
		return 0;

	set_pte(pte, __pte(pte_val(*pte) | _PAGE_PRESENT));
	__flush_tlb_one(address);
	return 1;
}

static int
hide_addr(unsigned long address)
{
	pte_t *pte;

	pte = address_get_pte(address);
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

bool
kmemcheck_active(struct pt_regs *regs)
{
	struct kmemcheck_context *data = &__get_cpu_var(kmemcheck_context);

	return data->balance > 0;
}

/*
 * Called from the #PF handler.
 */
void
kmemcheck_show(struct pt_regs *regs)
{
	struct kmemcheck_context *data = &__get_cpu_var(kmemcheck_context);
	int n;

	BUG_ON(!irqs_disabled());

	if (unlikely(data->balance != 0)) {
		show_addr(data->addr1);
		show_addr(data->addr2);
		error_save_bug(regs);
		data->balance = 0;
		return;
	}

	n = 0;
	n += show_addr(data->addr1);
	n += show_addr(data->addr2);

	/* None of the addresses actually belonged to kmemcheck. Note that
	 * this is not an error. */
	if (n == 0)
		return;

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
void
kmemcheck_hide(struct pt_regs *regs)
{
	struct kmemcheck_context *data = &__get_cpu_var(kmemcheck_context);
	int n;

	BUG_ON(!irqs_disabled());

	if (data->balance == 0)
		return;

	if (unlikely(data->balance != 1)) {
		show_addr(data->addr1);
		show_addr(data->addr2);
		error_save_bug(regs);
		data->addr1 = 0;
		data->addr2 = 0;
		data->balance = 0;

		if (!(data->flags & X86_EFLAGS_TF))
			regs->flags &= ~X86_EFLAGS_TF;
		if (data->flags & X86_EFLAGS_IF)
			regs->flags |= X86_EFLAGS_IF;
		return;
	}

	n = 0;
	if (kmemcheck_enabled) {
		n += hide_addr(data->addr1);
		n += hide_addr(data->addr2);
	} else {
		n += show_addr(data->addr1);
		n += show_addr(data->addr2);
	}

	if (n == 0)
		return;

	--data->balance;

	data->addr1 = 0;
	data->addr2 = 0;

	if (!(data->flags & X86_EFLAGS_TF))
		regs->flags &= ~X86_EFLAGS_TF;
	if (data->flags & X86_EFLAGS_IF)
		regs->flags |= X86_EFLAGS_IF;
}

void
kmemcheck_show_pages(struct page *p, unsigned int n)
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

bool
kmemcheck_page_is_tracked(struct page *p)
{
	/* This will also check the "hidden" flag of the PTE. */
	return address_get_pte((unsigned long) page_address(p));
}

void
kmemcheck_hide_pages(struct page *p, unsigned int n)
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

static void
mark_shadow(void *address, unsigned int n, enum shadow status)
{
	void *shadow;

	shadow = address_get_shadow((unsigned long) address);
	if (!shadow)
		return;
	__memset(shadow, status, n);
}

void
kmemcheck_mark_unallocated(void *address, unsigned int n)
{
	mark_shadow(address, n, SHADOW_UNALLOCATED);
}

void
kmemcheck_mark_uninitialized(void *address, unsigned int n)
{
	mark_shadow(address, n, SHADOW_UNINITIALIZED);
}

/*
 * Fill the shadow memory of the given address such that the memory at that
 * address is marked as being initialized.
 */
void
kmemcheck_mark_initialized(void *address, unsigned int n)
{
	mark_shadow(address, n, SHADOW_INITIALIZED);
}

void
kmemcheck_mark_freed(void *address, unsigned int n)
{
	mark_shadow(address, n, SHADOW_FREED);
}

void
kmemcheck_mark_unallocated_pages(struct page *p, unsigned int n)
{
	unsigned int i;

	for (i = 0; i < n; ++i)
		kmemcheck_mark_unallocated(page_address(&p[i]), PAGE_SIZE);
}

void
kmemcheck_mark_uninitialized_pages(struct page *p, unsigned int n)
{
	unsigned int i;

	for (i = 0; i < n; ++i)
		kmemcheck_mark_uninitialized(page_address(&p[i]), PAGE_SIZE);
}

static bool
opcode_is_prefix(uint8_t b)
{
	return
		/* Group 1 */
		b == 0xf0 || b == 0xf2 || b == 0xf3
		/* Group 2 */
		|| b == 0x2e || b == 0x36 || b == 0x3e || b == 0x26
		|| b == 0x64 || b == 0x65 || b == 0x2e || b == 0x3e
		/* Group 3 */
		|| b == 0x66
		/* Group 4 */
		|| b == 0x67;
}

/* This is a VERY crude opcode decoder. We only need to find the size of the
 * load/store that caused our #PF and this should work for all the opcodes
 * that we care about. Moreover, the ones who invented this instruction set
 * should be shot. */
static unsigned int
opcode_get_size(const uint8_t *op)
{
	/* Default operand size */
	int operand_size_override = 32;

	/* prefixes */
	for (; opcode_is_prefix(*op); ++op) {
		if (*op == 0x66)
			operand_size_override = 16;
	}

	/* escape opcode */
	if (*op == 0x0f) {
		++op;

		if (*op == 0xb6)
			return 8;
		if (*op == 0xb7)
			return 16;
	}

	return (*op & 1) ? operand_size_override : 8;
}

static const uint8_t *
opcode_get_primary(const uint8_t *op)
{
	/* skip prefixes */
	for (; opcode_is_prefix(*op); ++op);
	return op;
}

/*
 * Check that an access does not span across two different pages, because
 * that will mess up our shadow lookup.
 */
static bool
check_page_boundary(struct pt_regs *regs, unsigned long addr, unsigned int size)
{
	unsigned long page[4];

	if (size == 8)
		return false;

	page[0] = (addr + 0) & PAGE_MASK;
	page[1] = (addr + 1) & PAGE_MASK;

	if (size == 16 && page[0] == page[1])
		return false;

	page[2] = (addr + 2) & PAGE_MASK;
	page[3] = (addr + 3) & PAGE_MASK;

	if (size == 32 && page[0] == page[2] && page[0] == page[3])
		return false;

	/*
	 * XXX: The addr/size data is also really interesting if this
	 * case ever triggers. We should make a separate class of errors
	 * for this case. -Vegard
	 */
	error_save_bug(regs);
	return true;
}

static inline enum shadow
test(void *shadow, unsigned int size)
{
	uint8_t *x;

	x = shadow;

#ifdef CONFIG_KMEMCHECK_PARTIAL_OK
	/*
	 * Make sure _some_ bytes are initialized. Gcc frequently generates
	 * code to access neighboring bytes.
	 */
	switch (size) {
	case 32:
		if (x[3] == SHADOW_INITIALIZED)
			return x[3];
		if (x[2] == SHADOW_INITIALIZED)
			return x[2];
	case 16:
		if (x[1] == SHADOW_INITIALIZED)
			return x[1];
	case 8:
		if (x[0] == SHADOW_INITIALIZED)
			return x[0];
	}
#else
	switch (size) {
	case 32:
		if (x[3] != SHADOW_INITIALIZED)
			return x[3];
		if (x[2] != SHADOW_INITIALIZED)
			return x[2];
	case 16:
		if (x[1] != SHADOW_INITIALIZED)
			return x[1];
	case 8:
		if (x[0] != SHADOW_INITIALIZED)
			return x[0];
	}
#endif

	return x[0];
}

static inline void
set(void *shadow, unsigned int size)
{
	uint8_t *x;

	x = shadow;

	switch (size) {
	case 32:
		x[3] = SHADOW_INITIALIZED;
		x[2] = SHADOW_INITIALIZED;
	case 16:
		x[1] = SHADOW_INITIALIZED;
	case 8:
		x[0] = SHADOW_INITIALIZED;
	}

	return;
}

static void
kmemcheck_read(struct pt_regs *regs, unsigned long address, unsigned int size)
{
	void *shadow;
	enum shadow status;

	shadow = address_get_shadow(address);
	if (!shadow)
		return;

	if (check_page_boundary(regs, address, size))
		return;

	status = test(shadow, size);
	if (status == SHADOW_INITIALIZED)
		return;

	if (kmemcheck_enabled)
		error_save(status, address, size, regs);

	if (kmemcheck_enabled == 2)
		kmemcheck_enabled = 0;

	/* Don't warn about it again. */
	set(shadow, size);
}

static void
kmemcheck_write(struct pt_regs *regs, unsigned long address, unsigned int size)
{
	void *shadow;

	shadow = address_get_shadow(address);
	if (!shadow)
		return;

	if (check_page_boundary(regs, address, size))
		return;

	set(shadow, size);
}

void
kmemcheck_access(struct pt_regs *regs,
	unsigned long fallback_address, enum kmemcheck_method fallback_method)
{
	const uint8_t *insn;
	const uint8_t *insn_primary;
	unsigned int size;

	struct kmemcheck_context *data = &__get_cpu_var(kmemcheck_context);

	/* Recursive fault -- ouch. */
	if (data->busy) {
		show_addr(fallback_address);
		error_save_bug(regs);
		return;
	}

	data->busy = true;

	insn = (const uint8_t *) regs->ip;
	insn_primary = opcode_get_primary(insn);

	size = opcode_get_size(insn);

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
		/* These instructions are special because they take two
		 * addresses, but we only get one page fault. */
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

	/* If the opcode isn't special in any way, we use the data from the
	 * page fault handler to determine the address and type of memory
	 * access. */
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

/*
 * A faster implementation of memset() when tracking is enabled where the
 * whole memory area is within a single page.
 */
static void
memset_one_page(void *s, int c, size_t n)
{
	unsigned long addr;
	void *x;
	unsigned long flags;

	addr = (unsigned long) s;

	x = address_get_shadow(addr);
	if (!x) {
		/* The page isn't being tracked. */
		__memset(s, c, n);
		return;
	}

	/* While we are not guarding the page in question, nobody else
	 * should be able to change them. */
	local_irq_save(flags);

	show_addr(addr);
	__memset(s, c, n);
	__memset(x, SHADOW_INITIALIZED, n);
	if (kmemcheck_enabled)
		hide_addr(addr);

	local_irq_restore(flags);
}

/*
 * A faster implementation of memset() when tracking is enabled. We cannot
 * assume that all pages within the range are tracked, so copying has to be
 * split into page-sized (or smaller, for the ends) chunks.
 */
void *
kmemcheck_memset(void *s, int c, size_t n)
{
	unsigned long addr;
	unsigned long start_page, start_offset;
	unsigned long end_page, end_offset;
	unsigned long i;

	if (!n)
		return s;

	if (!slab_is_available()) {
		__memset(s, c, n);
		return s;
	}

	addr = (unsigned long) s;

	start_page = addr & PAGE_MASK;
	end_page = (addr + n) & PAGE_MASK;

	if (start_page == end_page) {
		/* The entire area is within the same page. Good, we only
		 * need one memset(). */
		memset_one_page(s, c, n);
		return s;
	}

	start_offset = addr & ~PAGE_MASK;
	end_offset = (addr + n) & ~PAGE_MASK;

	/* Clear the head, body, and tail of the memory area. */
	if (start_offset < PAGE_SIZE)
		memset_one_page(s, c, PAGE_SIZE - start_offset);
	for (i = start_page + PAGE_SIZE; i < end_page; i += PAGE_SIZE)
		memset_one_page((void *) i, c, PAGE_SIZE);
	if (end_offset > 0)
		memset_one_page((void *) end_page, c, end_offset);

	return s;
}

EXPORT_SYMBOL(kmemcheck_memset);
