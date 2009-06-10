/*
 * lib/plist.c
 *
 * Descending-priority-sorted double-linked list
 *
 * (C) 2002-2003 Intel Corp
 * Inaky Perez-Gonzalez <inaky.perez-gonzalez@intel.com>.
 *
 * 2001-2005 (c) MontaVista Software, Inc.
 * Daniel Walker <dwalker@mvista.com>
 *
 * (C) 2005 Thomas Gleixner <tglx@linutronix.de>
 *
 * Simplifications of the original code by
 * Oleg Nesterov <oleg@tv-sign.ru>
 *
 * Licensed under the FSF's GNU Public License v2 or later.
 *
 * Based on simple lists (include/linux/list.h).
 *
 * This file contains the add / del functions which are considered to
 * be too large to inline. See include/linux/plist.h for further
 * information.
 */

#include <linux/plist.h>
#include <linux/spinlock.h>

#ifdef CONFIG_DEBUG_PI_LIST

static void plist_check_prev_next(struct list_head *t, struct list_head *p,
				  struct list_head *n)
{
	if (n->prev != p || p->next != n) {
		printk("top: %p, n: %p, p: %p\n", t, t->next, t->prev);
		printk("prev: %p, n: %p, p: %p\n", p, p->next, p->prev);
		printk("next: %p, n: %p, p: %p\n", n, n->next, n->prev);
		WARN_ON(1);
	}
}

static void plist_check_list(struct list_head *top)
{
	struct list_head *prev = top, *next = top->next;

	plist_check_prev_next(top, prev, next);
	while (next != top) {
		prev = next;
		next = prev->next;
		plist_check_prev_next(top, prev, next);
	}
}

static void plist_check_head(struct plist_head *head)
{
#ifndef CONFIG_PREEMPT_RT
	WARN_ON(!head->lock);
#endif
	if (head->lock)
		WARN_ON_SMP(!spin_is_locked(head->lock));
	plist_check_list(&head->prio_list);
	plist_check_list(&head->node_list);
}

#else
# define plist_check_head(h)	do { } while (0)
#endif

static inline struct plist_node *prev_node(struct plist_node *iter)
{
	return list_entry(iter->plist.node_list.prev, struct plist_node,
			plist.node_list);
}

static inline struct plist_node *next_node(struct plist_node *iter)
{
	return list_entry(iter->plist.node_list.next, struct plist_node,
			plist.node_list);
}

static inline struct plist_node *prev_prio(struct plist_node *iter)
{
	return list_entry(iter->plist.prio_list.prev, struct plist_node,
			plist.prio_list);
}

static inline struct plist_node *next_prio(struct plist_node *iter)
{
	return list_entry(iter->plist.prio_list.next, struct plist_node,
			plist.prio_list);
}

/**
 * plist_add - add @node to @head
 *
 * @node:	&struct plist_node pointer
 * @head:	&struct plist_head pointer
 */
void plist_add(struct plist_node *node, struct plist_head *head)
{
	struct plist_node *iter;

	plist_check_head(head);
	WARN_ON(!plist_node_empty(node));

	list_for_each_entry(iter, &head->prio_list, plist.prio_list) {
		if (node->prio < iter->prio)
			goto lt_prio;
		else if (node->prio == iter->prio) {
			iter = next_prio(iter);
			goto eq_prio;
		}
	}

lt_prio:
	list_add_tail(&node->plist.prio_list, &iter->plist.prio_list);
eq_prio:
	list_add_tail(&node->plist.node_list, &iter->plist.node_list);

	plist_check_head(head);
}

/**
 * plist_del - Remove a @node from plist.
 *
 * @node:	&struct plist_node pointer - entry to be removed
 * @head:	&struct plist_head pointer - list head
 */
void plist_del(struct plist_node *node, struct plist_head *head)
{
	plist_check_head(head);

	if (!list_empty(&node->plist.prio_list)) {
		struct plist_node *next = plist_first(&node->plist);

		list_move_tail(&next->plist.prio_list, &node->plist.prio_list);
		list_del_init(&node->plist.prio_list);
	}

	list_del_init(&node->plist.node_list);

	plist_check_head(head);
}

void plist_head_splice(struct plist_head *src, struct plist_head *dst)
{
	struct plist_node *src_iter_first, *src_iter_last, *dst_iter;
	struct plist_node *tail = container_of(dst, struct plist_node, plist);

	dst_iter = next_prio(tail);

	while (!plist_head_empty(src) && dst_iter != tail) {
		src_iter_first = plist_first(src);

		src_iter_last = next_prio(src_iter_first);
		src_iter_last = prev_node(src_iter_last);

		WARN_ON(src_iter_first->prio != src_iter_last->prio);
		WARN_ON(list_empty(&src_iter_first->plist.prio_list));

		while (src_iter_first->prio > dst_iter->prio) {
			dst_iter = next_prio(dst_iter);
			if (dst_iter == tail)
				goto tail;
		}

		list_del_init(&src_iter_first->plist.prio_list);

		if (src_iter_first->prio < dst_iter->prio) {
			list_add_tail(&src_iter_first->plist.prio_list,
					&dst_iter->plist.prio_list);
		} else if (src_iter_first->prio == dst_iter->prio) {
			dst_iter = next_prio(dst_iter);
		} else BUG();

		list_splice2_tail(&src_iter_first->plist.node_list,
			       	  &src_iter_last->plist.node_list,
				  &dst_iter->plist.node_list);
	}

tail:
	list_splice_tail_init(&src->prio_list, &dst->prio_list);
	list_splice_tail_init(&src->node_list, &dst->node_list);
}
