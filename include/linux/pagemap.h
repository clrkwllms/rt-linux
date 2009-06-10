#ifndef _LINUX_PAGEMAP_H
#define _LINUX_PAGEMAP_H

/*
 * Copyright 1995 Linus Torvalds
 */
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/list.h>
#include <linux/highmem.h>
#include <linux/compiler.h>
#include <asm/uaccess.h>
#include <linux/gfp.h>
#include <linux/bitops.h>
#include <linux/page-flags.h>
#include <linux/hardirq.h> /* for in_interrupt() */
#include <linux/bit_spinlock.h>

/*
 * Bits in mapping->flags.  The lower __GFP_BITS_SHIFT bits are the page
 * allocation mode flags.
 */
#define	AS_EIO		(__GFP_BITS_SHIFT + 0)	/* IO error on async write */
#define AS_ENOSPC	(__GFP_BITS_SHIFT + 1)	/* ENOSPC on async write */

static inline void mapping_set_error(struct address_space *mapping, int error)
{
	if (error) {
		if (error == -ENOSPC)
			set_bit(AS_ENOSPC, &mapping->flags);
		else
			set_bit(AS_EIO, &mapping->flags);
	}
}

static inline gfp_t mapping_gfp_mask(struct address_space * mapping)
{
	return (__force gfp_t)mapping->flags & __GFP_BITS_MASK;
}

/*
 * This is non-atomic.  Only to be used before the mapping is activated.
 * Probably needs a barrier...
 */
static inline void mapping_set_gfp_mask(struct address_space *m, gfp_t mask)
{
	m->flags = (m->flags & ~(__force unsigned long)__GFP_BITS_MASK) |
				(__force unsigned long)mask;
}

/*
 * The page cache can done in larger chunks than
 * one page, because it allows for more efficient
 * throughput (it can then be mapped into user
 * space in smaller chunks for same flexibility).
 *
 * Or rather, it _will_ be done in larger chunks.
 */
#define PAGE_CACHE_SHIFT	PAGE_SHIFT
#define PAGE_CACHE_SIZE		PAGE_SIZE
#define PAGE_CACHE_MASK		PAGE_MASK
#define PAGE_CACHE_ALIGN(addr)	(((addr)+PAGE_CACHE_SIZE-1)&PAGE_CACHE_MASK)

#define page_cache_get(page)		get_page(page)
#define page_cache_release(page)	put_page(page)
void release_pages(struct page **pages, int nr, int cold);

static inline void lock_page_ref(struct page *page)
{
	bit_spin_lock(PG_nonewrefs, &page->flags);
	smp_wmb();
}

static inline void unlock_page_ref(struct page *page)
{
	bit_spin_unlock(PG_nonewrefs, &page->flags);
}

static inline void wait_on_page_ref(struct page *page)
{
	while (unlikely(test_bit(PG_nonewrefs, &page->flags)))
		cpu_relax();
}

#define lock_page_ref_irq(page)					\
	do {							\
		local_irq_disable();				\
		lock_page_ref(page);				\
	} while (0)

#define unlock_page_ref_irq(page)				\
	do {							\
		unlock_page_ref(page);				\
		local_irq_enable();				\
	} while (0)

#define lock_page_ref_irqsave(page, flags)			\
	do {							\
		local_irq_save(flags);				\
		lock_page_ref(page);				\
	} while (0)

#define unlock_page_ref_irqrestore(page, flags)			\
	do {							\
		unlock_page_ref(page);				\
		local_irq_restore(flags);			\
	} while (0)

/*
 * speculatively take a reference to a page.
 * If the page is free (_count == 0), then _count is untouched, and 0
 * is returned. Otherwise, _count is incremented by 1 and 1 is returned.
 *
 * This function must be run in the same rcu_read_lock() section as has
 * been used to lookup the page in the pagecache radix-tree: this allows
 * allocators to use a synchronize_rcu() to stabilize _count.
 *
 * Unless an RCU grace period has passed, the count of all pages coming out
 * of the allocator must be considered unstable. page_count may return higher
 * than expected, and put_page must be able to do the right thing when the
 * page has been finished with (because put_page is what is used to drop an
 * invalid speculative reference).
 *
 * After incrementing the refcount, this function spins until PageNoNewRefs
 * is clear, then a read memory barrier is issued.
 *
 * This forms the core of the lockless pagecache locking protocol, where
 * the lookup-side (eg. find_get_page) has the following pattern:
 * 1. find page in radix tree
 * 2. conditionally increment refcount
 * 3. wait for PageNoNewRefs
 * 4. check the page is still in pagecache
 *
 * Remove-side (that cares about _count, eg. reclaim) has the following:
 * A. SetPageNoNewRefs
 * B. check refcount is correct
 * C. remove page
 * D. ClearPageNoNewRefs
 *
 * There are 2 critical interleavings that matter:
 * - 2 runs before B: in this case, B sees elevated refcount and bails out
 * - B runs before 2: in this case, 3 ensures 4 will not run until *after* C
 *   (after D, even). In which case, 4 will notice C and lookup side can retry
 *
 * It is possible that between 1 and 2, the page is removed then the exact same
 * page is inserted into the same position in pagecache. That's OK: the
 * old find_get_page using tree_lock could equally have run before or after
 * the write-side, depending on timing.
 *
 * Pagecache insertion isn't a big problem: either 1 will find the page or
 * it will not. Likewise, the old find_get_page could run either before the
 * insertion or afterwards, depending on timing.
 */
static inline int page_cache_get_speculative(struct page *page)
{
	VM_BUG_ON(in_interrupt());

#ifndef CONFIG_SMP
# ifdef CONFIG_PREEMPT
	VM_BUG_ON(!in_atomic());
# endif
	/*
	 * Preempt must be disabled here - we rely on rcu_read_lock doing
	 * this for us.
	 *
	 * Pagecache won't be truncated from interrupt context, so if we have
	 * found a page in the radix tree here, we have pinned its refcount by
	 * disabling preempt, and hence no need for the "speculative get" that
	 * SMP requires.
	 */
	VM_BUG_ON(page_count(page) == 0);
	atomic_inc(&page->_count);

#else
	if (unlikely(!get_page_unless_zero(page)))
		return 0; /* page has been freed */

	/*
	 * Note that get_page_unless_zero provides a memory barrier.
	 * This is needed to ensure PageNoNewRefs is evaluated after the
	 * page refcount has been raised. See below comment.
	 */

	wait_on_page_ref(page);

	/*
	 * smp_rmb is to ensure the load of page->flags (for PageNoNewRefs())
	 * is performed before a future load used to ensure the page is
	 * the correct on (usually: page->mapping and page->index).
	 *
	 * Those places that set PageNoNewRefs have the following pattern:
	 * 	SetPageNoNewRefs(page)
	 * 	wmb();
	 * 	if (page_count(page) == X)
	 * 		remove page from pagecache
	 * 	wmb();
	 * 	ClearPageNoNewRefs(page)
	 *
	 * If the load was out of order, page->mapping might be loaded before
	 * the page is removed from pagecache but PageNoNewRefs evaluated
	 * after the ClearPageNoNewRefs().
	 */
	smp_rmb();

#endif
	VM_BUG_ON(PageCompound(page) && (struct page *)page_private(page) != page);

	return 1;
}

#ifdef CONFIG_NUMA
extern struct page *__page_cache_alloc(gfp_t gfp);
#else
static inline struct page *__page_cache_alloc(gfp_t gfp)
{
	return alloc_pages(gfp, 0);
}
#endif

static inline struct page *page_cache_alloc(struct address_space *x)
{
	return __page_cache_alloc(mapping_gfp_mask(x));
}

static inline struct page *page_cache_alloc_cold(struct address_space *x)
{
	return __page_cache_alloc(mapping_gfp_mask(x)|__GFP_COLD);
}

typedef int filler_t(void *, struct page *);

extern struct page * find_get_page(struct address_space *mapping,
				pgoff_t index);
extern struct page * find_lock_page(struct address_space *mapping,
				pgoff_t index);
extern struct page * find_or_create_page(struct address_space *mapping,
				pgoff_t index, gfp_t gfp_mask);
unsigned find_get_pages(struct address_space *mapping, pgoff_t start,
			unsigned int nr_pages, struct page **pages);
unsigned find_get_pages_contig(struct address_space *mapping, pgoff_t start,
			       unsigned int nr_pages, struct page **pages);
unsigned find_get_pages_tag(struct address_space *mapping, pgoff_t *index,
			int tag, unsigned int nr_pages, struct page **pages);

struct page *__grab_cache_page(struct address_space *mapping, pgoff_t index);

/*
 * Returns locked page at given index in given cache, creating it if needed.
 */
static inline struct page *grab_cache_page(struct address_space *mapping,
								pgoff_t index)
{
	return find_or_create_page(mapping, index, mapping_gfp_mask(mapping));
}

extern struct page * grab_cache_page_nowait(struct address_space *mapping,
				pgoff_t index);
extern struct page * read_cache_page_async(struct address_space *mapping,
				pgoff_t index, filler_t *filler,
				void *data);
extern struct page * read_cache_page(struct address_space *mapping,
				pgoff_t index, filler_t *filler,
				void *data);
extern int read_cache_pages(struct address_space *mapping,
		struct list_head *pages, filler_t *filler, void *data);

static inline struct page *read_mapping_page_async(
						struct address_space *mapping,
						     pgoff_t index, void *data)
{
	filler_t *filler = (filler_t *)mapping->a_ops->readpage;
	return read_cache_page_async(mapping, index, filler, data);
}

static inline struct page *read_mapping_page(struct address_space *mapping,
					     pgoff_t index, void *data)
{
	filler_t *filler = (filler_t *)mapping->a_ops->readpage;
	return read_cache_page(mapping, index, filler, data);
}

int add_to_page_cache(struct page *page, struct address_space *mapping,
				pgoff_t index, gfp_t gfp_mask);
int add_to_page_cache_lru(struct page *page, struct address_space *mapping,
				pgoff_t index, gfp_t gfp_mask);
extern void remove_from_page_cache(struct page *page);
extern void __remove_from_page_cache(struct page *page);

/*
 * Return byte-offset into filesystem object for page.
 */
static inline loff_t page_offset(struct page *page)
{
	return ((loff_t)page->index) << PAGE_CACHE_SHIFT;
}

static inline pgoff_t linear_page_index(struct vm_area_struct *vma,
					unsigned long address)
{
	pgoff_t pgoff = (address - vma->vm_start) >> PAGE_SHIFT;
	pgoff += vma->vm_pgoff;
	return pgoff >> (PAGE_CACHE_SHIFT - PAGE_SHIFT);
}

extern void FASTCALL(__lock_page(struct page *page));
extern void FASTCALL(__lock_page_nosync(struct page *page));
extern void FASTCALL(unlock_page(struct page *page));

/*
 * lock_page may only be called if we have the page's inode pinned.
 */
static inline void lock_page(struct page *page)
{
	might_sleep();
	if (TestSetPageLocked(page))
		__lock_page(page);
}

/*
 * lock_page_nosync should only be used if we can't pin the page's inode.
 * Doesn't play quite so well with block device plugging.
 */
static inline void lock_page_nosync(struct page *page)
{
	might_sleep();
	if (TestSetPageLocked(page))
		__lock_page_nosync(page);
}
	
/*
 * This is exported only for wait_on_page_locked/wait_on_page_writeback.
 * Never use this directly!
 */
extern void FASTCALL(wait_on_page_bit(struct page *page, int bit_nr));

/* 
 * Wait for a page to be unlocked.
 *
 * This must be called with the caller "holding" the page,
 * ie with increased "page->count" so that the page won't
 * go away during the wait..
 */
static inline void wait_on_page_locked(struct page *page)
{
	if (PageLocked(page))
		wait_on_page_bit(page, PG_locked);
}

/* 
 * Wait for a page to complete writeback
 */
static inline void wait_on_page_writeback(struct page *page)
{
	if (PageWriteback(page))
		wait_on_page_bit(page, PG_writeback);
}

extern void end_page_writeback(struct page *page);

/*
 * Fault a userspace page into pagetables.  Return non-zero on a fault.
 *
 * This assumes that two userspace pages are always sufficient.  That's
 * not true if PAGE_CACHE_SIZE > PAGE_SIZE.
 */
static inline int fault_in_pages_writeable(char __user *uaddr, int size)
{
	int ret;

	if (unlikely(size == 0))
		return 0;

	/*
	 * Writing zeroes into userspace here is OK, because we know that if
	 * the zero gets there, we'll be overwriting it.
	 */
	ret = __put_user(0, uaddr);
	if (ret == 0) {
		char __user *end = uaddr + size - 1;

		/*
		 * If the page was already mapped, this will get a cache miss
		 * for sure, so try to avoid doing it.
		 */
		if (((unsigned long)uaddr & PAGE_MASK) !=
				((unsigned long)end & PAGE_MASK))
		 	ret = __put_user(0, end);
	}
	return ret;
}

static inline int fault_in_pages_readable(const char __user *uaddr, int size)
{
	volatile char c;
	int ret;

	if (unlikely(size == 0))
		return 0;

	ret = __get_user(c, uaddr);
	if (ret == 0) {
		const char __user *end = uaddr + size - 1;

		if (((unsigned long)uaddr & PAGE_MASK) !=
				((unsigned long)end & PAGE_MASK))
		 	ret = __get_user(c, end);
	}
	return ret;
}

#endif /* _LINUX_PAGEMAP_H */
