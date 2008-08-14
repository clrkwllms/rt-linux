#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/kmemcheck.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/types.h>

#include "shadow.h"
#include "smp.h"

/*
 * A faster implementation of memset() when tracking is enabled where the
 * whole memory area is within a single page.
 */
static void memset_one_page(void *s, int c, size_t n)
{
	unsigned long addr;
	void *x;
	unsigned long flags;

	addr = (unsigned long) s;

	x = kmemcheck_shadow_lookup(addr);
	if (!x) {
		/* The page isn't being tracked. */
		__memset(s, c, n);
		return;
	}

	/*
	 * While we are not guarding the page in question, nobody else
	 * should be able to change them.
	 */
	local_irq_save(flags);

	kmemcheck_pause_allbutself();
	kmemcheck_show_addr(addr);
	__memset(s, c, n);
	__memset(x, KMEMCHECK_SHADOW_INITIALIZED, n);
	if (kmemcheck_enabled)
		kmemcheck_hide_addr(addr);
	kmemcheck_resume();

	local_irq_restore(flags);
}

/*
 * A faster implementation of memset() when tracking is enabled. We cannot
 * assume that all pages within the range are tracked, so copying has to be
 * split into page-sized (or smaller, for the ends) chunks.
 */
void *kmemcheck_memset(void *s, int c, size_t n)
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
		/*
		 * The entire area is within the same page. Good, we only
		 * need one memset().
		 */
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
