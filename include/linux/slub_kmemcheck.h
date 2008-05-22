#ifndef LINUX__SLUB_KMEMCHECK__H
#define LINUX__SLUB_KMEMCHECK__H

#ifdef CONFIG_KMEMCHECK
void kmemcheck_alloc_shadow(struct kmem_cache *s, gfp_t flags, int node,
			   struct page *page);
void kmemcheck_free_shadow(struct kmem_cache *s, struct page *page);
void kmemcheck_slab_alloc(struct kmem_cache *s, gfp_t gfpflags, void *object);
void kmemcheck_slab_free(struct kmem_cache *s, void *object);
#else
static inline void
kmemcheck_alloc_shadow(struct kmem_cache *s, gfp_t flags, int node,
		       struct page *page)
{
}

static inline void
kmemcheck_free_shadow(struct kmem_cache *s, struct page *page)
{
}

static inline void
kmemcheck_slab_alloc(struct kmem_cache *s, gfp_t gfpflags, void *object)
{
}

static inline void kmemcheck_slab_free(struct kmem_cache *s, void *object)
{
}
#endif /* CONFIG_KMEMCHECK */

#endif /* LINUX__SLUB_KMEMCHECK__H */
