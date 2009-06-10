/*
 * Copyright (C) 2006, Red Hat, Inc., Peter Zijlstra <pzijlstr@redhat.com>
 * Licenced under the GPLv2.
 *
 * Simple fine grain locked double linked list.
 *
 * Locking order is from prev -> next.
 * Edges are locked not nodes; that is, cur->lock protects:
 *  - cur->next,
 *  - cur->next->prev.
 *
 * Passed pointers are assumed to be stable by external means such as
 * refcounts or RCU. The individual list entries are assumed to be RCU
 * freed (requirement of __lock_list_del).
 */

#include <linux/lock_list.h>

void lock_list_add(struct lock_list_head *new,
		   struct lock_list_head *list)
{
	struct lock_list_head *next;

	spin_lock(&new->lock);
	spin_lock_nested(&list->lock, LOCK_LIST_NESTING_PREV);
	next = list->next;
	__list_add(&new->head, &list->head, &next->head);
	spin_unlock(&list->lock);
	spin_unlock(&new->lock);
}

static spinlock_t *__lock_list(struct lock_list_head *entry)
{
	struct lock_list_head *prev;
	spinlock_t *lock = NULL;

again:
	prev = entry->prev;
	if (prev == entry)
		goto one;
	spin_lock_nested(&prev->lock, LOCK_LIST_NESTING_PREV);
	if (unlikely(entry->prev != prev)) {
		/*
		 * we lost
		 */
		spin_unlock(&prev->lock);
		goto again;
	}
	lock = &prev->lock;
one:
	spin_lock_nested(&entry->lock, LOCK_LIST_NESTING_CUR);
	return lock;
}

void lock_list_del_init(struct lock_list_head *entry)
{
	spinlock_t *lock;

	rcu_read_lock();
	lock = __lock_list(entry);
	list_del_init(&entry->head);
	spin_unlock(&entry->lock);
	if (lock)
		spin_unlock(lock);
	rcu_read_unlock();
}

void lock_list_splice_init(struct lock_list_head *list,
			struct lock_list_head *head)
{
	spinlock_t *lock;

	rcu_read_lock();
	lock = __lock_list(list);
	if (!list_empty(&list->head)) {
		spin_lock_nested(&head->lock, LOCK_LIST_NESTING_NEXT);
		__list_splice(&list->head, &head->head);
		INIT_LIST_HEAD(&list->head);
		spin_unlock(&head->lock);
	}
	spin_unlock(&list->lock);
	if (lock)
		spin_unlock(lock);
	rcu_read_unlock();
}

struct lock_list_head *lock_list_next_entry(struct lock_list_head *list,
					    struct lock_list_head *entry)
{
	struct lock_list_head *next = entry->next;
	if (likely(next != list)) {
		lock_set_subclass(&entry->lock.dep_map,
				  LOCK_LIST_NESTING_CUR, _THIS_IP_);
		spin_lock_nested(&next->lock, LOCK_LIST_NESTING_NEXT);
		BUG_ON(entry->next != next);
	} else
		next = NULL;
	spin_unlock(&entry->lock);
	return next;
}

struct lock_list_head *lock_list_first_entry(struct lock_list_head *list)
{
	spin_lock(&list->lock);
	return lock_list_next_entry(list, list);
}

