/* rwsem-spinlock.h: fallback C implementation
 *
 * Copyright (c) 2001   David Howells (dhowells@redhat.com).
 * - Derived partially from ideas by Andrea Arcangeli <andrea@suse.de>
 * - Derived also from comments by Linus
 */

#ifndef _LINUX_RWSEM_SPINLOCK_H
#define _LINUX_RWSEM_SPINLOCK_H

#ifndef _LINUX_RWSEM_H
#error "please don't include linux/rwsem-spinlock.h directly, use linux/rwsem.h instead"
#endif

#include <linux/spinlock.h>
#include <linux/list.h>

#ifdef __KERNEL__

#include <linux/types.h>

struct rwsem_waiter;

/*
 * the rw-semaphore definition
 * - if activity is 0 then there are no active readers or writers
 * - if activity is +ve then that is the number of active readers
 * - if activity is -1 then there is one active writer
 * - if wait_list is not empty, then there are processes waiting for the semaphore
 */
struct compat_rw_semaphore {
	__s32			activity;
	spinlock_t		wait_lock;
	struct list_head	wait_list;
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	struct lockdep_map dep_map;
#endif
};

#ifdef CONFIG_DEBUG_LOCK_ALLOC
# define __RWSEM_DEP_MAP_INIT(lockname) , .dep_map = { .name = #lockname }
#else
# define __RWSEM_DEP_MAP_INIT(lockname)
#endif

#define __COMPAT_RWSEM_INITIALIZER(name) \
{ 0, SPIN_LOCK_UNLOCKED, LIST_HEAD_INIT((name).wait_list) __RWSEM_DEP_MAP_INIT(name) }

#define COMPAT_DECLARE_RWSEM(name) \
	struct compat_rw_semaphore name = __COMPAT_RWSEM_INITIALIZER(name)

extern void __compat_init_rwsem(struct compat_rw_semaphore *sem, const char *name,
			 struct lock_class_key *key);

#define compat_init_rwsem(sem)					\
do {								\
	static struct lock_class_key __key;			\
								\
	__compat_init_rwsem((sem), #sem, &__key);		\
} while (0)

extern void __down_read(struct compat_rw_semaphore *sem);
extern int __down_read_trylock(struct compat_rw_semaphore *sem);
extern void __down_write(struct compat_rw_semaphore *sem);
extern void __down_write_nested(struct compat_rw_semaphore *sem, int subclass);
extern int __down_write_trylock(struct compat_rw_semaphore *sem);
extern void __up_read(struct compat_rw_semaphore *sem);
extern void __up_write(struct compat_rw_semaphore *sem);
extern void __downgrade_write(struct compat_rw_semaphore *sem);

static inline int compat_rwsem_is_locked(struct compat_rw_semaphore *sem)
{
	return (sem->activity != 0);
}

#endif /* __KERNEL__ */
#endif /* _LINUX_RWSEM_SPINLOCK_H */
