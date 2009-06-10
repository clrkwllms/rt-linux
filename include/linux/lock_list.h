/*
 * Copyright (C) 2006, Red Hat, Inc., Peter Zijlstra <pzijlstr@redhat.com>
 * Licenced under the GPLv2.
 *
 * Simple fine grain locked double linked list.
 */
#ifndef _LINUX_LOCK_LIST_H
#define _LINUX_LOCK_LIST_H

#ifdef __KERNEL__

#include <linux/list.h>
#include <linux/rcupdate.h>
#include <linux/spinlock.h>

struct lock_list_head {
	union {
		struct list_head head;
		struct {
			struct lock_list_head *next, *prev;
		};
	};
	spinlock_t lock;
};

enum {
	LOCK_LIST_NESTING_PREV = 1,
	LOCK_LIST_NESTING_CUR,
	LOCK_LIST_NESTING_NEXT,
};

static inline void INIT_LOCK_LIST_HEAD(struct lock_list_head *list)
{
	INIT_LIST_HEAD(&list->head);
	spin_lock_init(&list->lock);
}

/*
 * Passed pointers are assumed stable by external means (refcount, rcu)
 */
extern void lock_list_add(struct lock_list_head *new,
			  struct lock_list_head *list);
extern void lock_list_del_init(struct lock_list_head *entry);
extern void lock_list_splice_init(struct lock_list_head *list,
				  struct lock_list_head *head);

struct lock_list_head *lock_list_next_entry(struct lock_list_head *list,
					    struct lock_list_head *entry);
struct lock_list_head *lock_list_first_entry(struct lock_list_head *list);

#define lock_list_for_each_entry(pos, list, member)			\
	for (pos = list_entry(lock_list_first_entry(list), 		\
			      typeof(*pos), member); 			\
	     pos;							\
	     pos = list_entry(lock_list_next_entry(list, &pos->member),	\
			      typeof(*pos), member))

/*
 * to be used when iteration is terminated by breaking out of the
 * lock_list_for_each_entry() loop.
 *
 * 	lock_list_for_each_entry(i, list, member) {
 * 		if (cond) {
 * 			lock_list_for_each_entry_stop(i, member);
 * 			goto foo;
 * 		}
 * 	}
 *
 */
#define lock_list_for_each_entry_stop(pos, member)			\
	spin_unlock(&(pos->member.lock))

#endif /* __KERNEL__ */
#endif /* _LINUX_LOCK_LIST_H */
