/*
 * highmem.h: virtual kernel memory mappings for high memory
 *
 * Used in CONFIG_HIGHMEM systems for memory pages which
 * are not addressable by direct kernel virtual addresses.
 *
 * Copyright (C) 1999 Gerhard Wichert, Siemens AG
 *		      Gerhard.Wichert@pdb.siemens.de
 *
 *
 * Redesigned the x86 32-bit VM architecture to deal with 
 * up to 16 Terabyte physical memory. With current x86 CPUs
 * we now support up to 64 Gigabytes physical RAM.
 *
 * Copyright (C) 1999 Ingo Molnar <mingo@redhat.com>
 */

#ifndef _ASM_HIGHMEM_H
#define _ASM_HIGHMEM_H

#ifdef __KERNEL__

#include <linux/interrupt.h>
#include <linux/threads.h>
#include <asm/kmap_types.h>
#include <asm/tlbflush.h>
#include <asm/paravirt.h>

/* declarations for highmem.c */
extern unsigned long highstart_pfn, highend_pfn;

extern pte_t *kmap_pte;
extern pgprot_t kmap_prot;
extern pte_t *pkmap_page_table;

/*
 * Right now we initialize only a single pte table. It can be extended
 * easily, subsequent pte tables have to be allocated in one physical
 * chunk of RAM.
 */
#ifdef CONFIG_X86_PAE
#define LAST_PKMAP 512
#else
#define LAST_PKMAP 1024
#endif
/*
 * Ordering is:
 *
 * FIXADDR_TOP
 * 			fixed_addresses
 * FIXADDR_START
 * 			temp fixed addresses
 * FIXADDR_BOOT_START
 * 			Persistent kmap area
 * PKMAP_BASE
 * VMALLOC_END
 * 			Vmalloc area
 * VMALLOC_START
 * high_memory
 */
#define PKMAP_BASE ( (FIXADDR_BOOT_START - PAGE_SIZE*(LAST_PKMAP + 1)) & PMD_MASK )
#define LAST_PKMAP_MASK (LAST_PKMAP-1)
#define PKMAP_NR(virt)  ((virt-PKMAP_BASE) >> PAGE_SHIFT)
#define PKMAP_ADDR(nr)  (PKMAP_BASE + ((nr) << PAGE_SHIFT))

extern void * FASTCALL(kmap_high(struct page *page));
extern void FASTCALL(kunmap_high(struct page *page));

void *kmap(struct page *page);
extern void kunmap_virt(void *ptr);
extern struct page *kmap_to_page(void *ptr);
void kunmap(struct page *page);

void *__kmap_atomic_prot(struct page *page, enum km_type type, pgprot_t prot);
void *__kmap_atomic(struct page *page, enum km_type type);
void __kunmap_atomic(void *kvaddr, enum km_type type);
void *__kmap_atomic_pfn(unsigned long pfn, enum km_type type);
struct page *__kmap_atomic_to_page(void *ptr);

void kunmap(struct page *page);
void *kmap_atomic_prot(struct page *page, enum km_type type, pgprot_t prot);
void *kmap_atomic(struct page *page, enum km_type type);
void kunmap_atomic(void *kvaddr, enum km_type type);
void *kmap_atomic_pfn(unsigned long pfn, enum km_type type);
struct page *kmap_atomic_to_page(void *ptr);

#ifndef CONFIG_PARAVIRT
#define kmap_atomic_pte(page, type)	kmap_atomic(page, type)
#endif

#define flush_cache_kmaps()	do { } while (0)

/*
 * on PREEMPT_RT kmap_atomic() is a wrapper that uses kmap():
 */
#ifdef CONFIG_PREEMPT_RT
# define kmap_atomic_prot(page, type, prot)	({ pagefault_disable(); kmap(page); })
# define kmap_atomic(page, type)	({ pagefault_disable(); kmap(page); })
# define kmap_atomic_pfn(pfn, type)	kmap(pfn_to_page(pfn))
# define kunmap_atomic(kvaddr, type)	do { pagefault_enable(); kunmap_virt(kvaddr); } while(0)
# define kmap_atomic_to_page(kvaddr)	kmap_to_page(kvaddr)
#else
# define kmap_atomic_prot(page, type, prot)	__kmap_atomic_prot(page, type, prot)
# define kmap_atomic(page, type)	__kmap_atomic(page, type)
# define kmap_atomic_pfn(pfn, type)	__kmap_atomic_pfn(pfn, type)
# define kunmap_atomic(kvaddr, type)	__kunmap_atomic(kvaddr, type)
# define kmap_atomic_to_page(kvaddr)	__kmap_atomic_to_page(kvaddr)
#endif

#endif /* __KERNEL__ */

#endif /* _ASM_HIGHMEM_H */
