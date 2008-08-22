#include <linux/kmemcheck.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/module.h>

#include <asm/page.h>
#include <asm/pgtable.h>

#include "pte.h"
#include "shadow.h"

/*
 * Return the shadow address for the given address. Returns NULL if the
 * address is not tracked.
 *
 * We need to be extremely careful not to follow any invalid pointers,
 * because this function can be called for *any* possible address.
 */
void *kmemcheck_shadow_lookup(unsigned long address)
{
	pte_t *pte;
	struct page *page;

	if (!virt_addr_valid(address))
		return NULL;

	pte = kmemcheck_pte_lookup(address);
	if (!pte)
		return NULL;

	page = virt_to_page(address);
	if (!page->shadow)
		return NULL;
	return page->shadow + (address & (PAGE_SIZE - 1));
}

static void mark_shadow(void *address, unsigned int n,
	enum kmemcheck_shadow status)
{
	void *shadow;

	shadow = kmemcheck_shadow_lookup((unsigned long) address);
	if (!shadow)
		return;
	__memset(shadow, status, n);
}

void kmemcheck_mark_unallocated(void *address, unsigned int n)
{
	mark_shadow(address, n, KMEMCHECK_SHADOW_UNALLOCATED);
}

void kmemcheck_mark_uninitialized(void *address, unsigned int n)
{
	mark_shadow(address, n, KMEMCHECK_SHADOW_UNINITIALIZED);
}

/*
 * Fill the shadow memory of the given address such that the memory at that
 * address is marked as being initialized.
 */
void kmemcheck_mark_initialized(void *address, unsigned int n)
{
	mark_shadow(address, n, KMEMCHECK_SHADOW_INITIALIZED);
}
EXPORT_SYMBOL_GPL(kmemcheck_mark_initialized);

void kmemcheck_mark_freed(void *address, unsigned int n)
{
	mark_shadow(address, n, KMEMCHECK_SHADOW_FREED);
}

void kmemcheck_mark_unallocated_pages(struct page *p, unsigned int n)
{
	unsigned int i;

	for (i = 0; i < n; ++i)
		kmemcheck_mark_unallocated(page_address(&p[i]), PAGE_SIZE);
}

void kmemcheck_mark_uninitialized_pages(struct page *p, unsigned int n)
{
	unsigned int i;

	for (i = 0; i < n; ++i)
		kmemcheck_mark_uninitialized(page_address(&p[i]), PAGE_SIZE);
}

enum kmemcheck_shadow kmemcheck_shadow_test(void *shadow, unsigned int size)
{
	uint8_t *x;

	x = shadow;

#ifdef CONFIG_KMEMCHECK_PARTIAL_OK
	/*
	 * Make sure _some_ bytes are initialized. Gcc frequently generates
	 * code to access neighboring bytes.
	 */
	switch (size) {
#ifdef CONFIG_X86_64
	case 64:
		if (x[7] == KMEMCHECK_SHADOW_INITIALIZED)
			return x[7];
		if (x[6] == KMEMCHECK_SHADOW_INITIALIZED)
			return x[6];
		if (x[5] == KMEMCHECK_SHADOW_INITIALIZED)
			return x[5];
		if (x[4] == KMEMCHECK_SHADOW_INITIALIZED)
			return x[4];
#endif
	case 32:
		if (x[3] == KMEMCHECK_SHADOW_INITIALIZED)
			return x[3];
		if (x[2] == KMEMCHECK_SHADOW_INITIALIZED)
			return x[2];
	case 16:
		if (x[1] == KMEMCHECK_SHADOW_INITIALIZED)
			return x[1];
	case 8:
		if (x[0] == KMEMCHECK_SHADOW_INITIALIZED)
			return x[0];
	}
#else
	switch (size) {
#ifdef CONFIG_X86_64
	case 64:
		if (x[7] != KMEMCHECK_SHADOW_INITIALIZED)
			return x[7];
		if (x[6] != KMEMCHECK_SHADOW_INITIALIZED)
			return x[6];
		if (x[5] != KMEMCHECK_SHADOW_INITIALIZED)
			return x[5];
		if (x[4] != KMEMCHECK_SHADOW_INITIALIZED)
			return x[4];
#endif
	case 32:
		if (x[3] != KMEMCHECK_SHADOW_INITIALIZED)
			return x[3];
		if (x[2] != KMEMCHECK_SHADOW_INITIALIZED)
			return x[2];
	case 16:
		if (x[1] != KMEMCHECK_SHADOW_INITIALIZED)
			return x[1];
	case 8:
		if (x[0] != KMEMCHECK_SHADOW_INITIALIZED)
			return x[0];
	}
#endif

	return x[0];
}

void kmemcheck_shadow_set(void *shadow, unsigned int size)
{
	uint8_t *x;

	x = shadow;

	switch (size) {
#ifdef CONFIG_X86_64
	case 64:
		x[7] = KMEMCHECK_SHADOW_INITIALIZED;
		x[6] = KMEMCHECK_SHADOW_INITIALIZED;
		x[5] = KMEMCHECK_SHADOW_INITIALIZED;
		x[4] = KMEMCHECK_SHADOW_INITIALIZED;
#endif
	case 32:
		x[3] = KMEMCHECK_SHADOW_INITIALIZED;
		x[2] = KMEMCHECK_SHADOW_INITIALIZED;
	case 16:
		x[1] = KMEMCHECK_SHADOW_INITIALIZED;
	case 8:
		x[0] = KMEMCHECK_SHADOW_INITIALIZED;
	}

	return;
}
