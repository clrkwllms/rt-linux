#ifndef __LINUX_RT_LOCK_H
#define __LINUX_RT_LOCK_H

/*
 * Real-Time Preemption Support
 *
 * started by Ingo Molnar:
 *
 *  Copyright (C) 2004, 2005 Red Hat, Inc., Ingo Molnar <mingo@redhat.com>
 *
 * This file contains the main data structure definitions.
 */
#include <linux/rtmutex.h>
#include <asm/atomic.h>
#include <linux/spinlock_types.h>

#ifdef CONFIG_PREEMPT_RT
# define preempt_rt 1
/*
 * spinlocks - an RT mutex plus lock-break field:
 */
typedef struct {
	struct rt_mutex		lock;
	unsigned int		break_lock;
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	struct lockdep_map	dep_map;
#endif
} spinlock_t;

#ifdef CONFIG_DEBUG_RT_MUTEXES
# define __RT_SPIN_INITIALIZER(name)					\
	{ .wait_lock = _RAW_SPIN_LOCK_UNLOCKED(name.wait_lock),		\
	  .save_state = 1,						\
	  .file = __FILE__,						\
	  .line = __LINE__, }
#else
# define __RT_SPIN_INITIALIZER(name)					\
	{ .wait_lock = _RAW_SPIN_LOCK_UNLOCKED(name.wait_lock) }
#endif

#define __SPIN_LOCK_UNLOCKED(name) (spinlock_t)				\
	{ .lock = __RT_SPIN_INITIALIZER(name),				\
	  SPIN_DEP_MAP_INIT(name) }

#else /* !PREEMPT_RT */

typedef raw_spinlock_t spinlock_t;

#define __SPIN_LOCK_UNLOCKED	_RAW_SPIN_LOCK_UNLOCKED

#endif

#define SPIN_LOCK_UNLOCKED	__SPIN_LOCK_UNLOCKED(spin_old_style)


#define __DEFINE_SPINLOCK(name) \
	spinlock_t name = __SPIN_LOCK_UNLOCKED(name)

#define DEFINE_SPINLOCK(name) \
	spinlock_t name __cacheline_aligned_in_smp = __SPIN_LOCK_UNLOCKED(name)

#ifdef CONFIG_PREEMPT_RT

/*
 * RW-semaphores are a spinlock plus a reader-depth count.
 *
 * Note that the semantics are different from the usual
 * Linux rw-sems, in PREEMPT_RT mode we do not allow
 * multiple readers to hold the lock at once, we only allow
 * a read-lock owner to read-lock recursively. This is
 * better for latency, makes the implementation inherently
 * fair and makes it simpler as well:
 */
struct rw_semaphore {
	struct rt_mutex		lock;
	int			read_depth;
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	struct lockdep_map	dep_map;
#endif
};

/*
 * rwlocks - an RW semaphore plus lock-break field:
 */
typedef struct {
	struct rt_mutex		lock;
	int			read_depth;
	unsigned int		break_lock;
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	struct lockdep_map	dep_map;
#endif
} rwlock_t;

#define __RW_LOCK_UNLOCKED(name) (rwlock_t) \
	{ .lock = __RT_SPIN_INITIALIZER(name),	\
	  RW_DEP_MAP_INIT(name) }
#else /* !PREEMPT_RT */

typedef raw_rwlock_t rwlock_t;

#define __RW_LOCK_UNLOCKED	_RAW_RW_LOCK_UNLOCKED

#endif

#define RW_LOCK_UNLOCKED	__RW_LOCK_UNLOCKED(rw_old_style)


#define DEFINE_RWLOCK(name) \
	rwlock_t name __cacheline_aligned_in_smp = __RW_LOCK_UNLOCKED(name)

#ifdef CONFIG_PREEMPT_RT

/*
 * Semaphores - a spinlock plus the semaphore count:
 */
struct semaphore {
	atomic_t		count;
	struct rt_mutex		lock;
};

#define DECLARE_MUTEX(name) \
struct semaphore name = \
	{ .count = { 1 }, .lock = __RT_MUTEX_INITIALIZER(name.lock) }

extern void
__sema_init(struct semaphore *sem, int val, char *name, char *file, int line);

#define rt_sema_init(sem, val) \
		__sema_init(sem, val, #sem, __FILE__, __LINE__)

extern void
__init_MUTEX(struct semaphore *sem, char *name, char *file, int line);
#define rt_init_MUTEX(sem) \
		__init_MUTEX(sem, #sem, __FILE__, __LINE__)

extern void there_is_no_init_MUTEX_LOCKED_for_RT_semaphores(void);

/*
 * No locked initialization for RT semaphores
 */
#define rt_init_MUTEX_LOCKED(sem) \
		there_is_no_init_MUTEX_LOCKED_for_RT_semaphores()
extern void  rt_down(struct semaphore *sem);
extern int  rt_down_interruptible(struct semaphore *sem);
extern int  rt_down_timeout(struct semaphore *sem, long jiffies);
extern int  rt_down_trylock(struct semaphore *sem);
extern void  rt_up(struct semaphore *sem);

#define rt_sem_is_locked(s)	rt_mutex_is_locked(&(s)->lock)
#define rt_sema_count(s)	atomic_read(&(s)->count)

extern int __bad_func_type(void);

#include <linux/pickop.h>

/*
 * PICK_SEM_OP() is a small redirector to allow less typing of the lock
 * types struct compat_semaphore, struct semaphore, at the front of the
 * PICK_FUNCTION macro.
 */
#define PICK_SEM_OP(...) PICK_FUNCTION(struct compat_semaphore *,	\
	struct semaphore *, ##__VA_ARGS__)
#define PICK_SEM_OP_RET(...) PICK_FUNCTION_RET(struct compat_semaphore *,\
	struct semaphore *, ##__VA_ARGS__)

#define sema_init(sem, val) \
	PICK_SEM_OP(compat_sema_init, rt_sema_init, sem, val)

#define init_MUTEX(sem) PICK_SEM_OP(compat_init_MUTEX, rt_init_MUTEX, sem)

#define init_MUTEX_LOCKED(sem) \
	PICK_SEM_OP(compat_init_MUTEX_LOCKED, rt_init_MUTEX_LOCKED, sem)

#define down(sem) PICK_SEM_OP(compat_down, rt_down, sem)

#define down_timeout(sem, jiff) \
	PICK_SEM_OP_RET(compat_down_timeout, rt_down_timeout, sem, jiff)

#define down_interruptible(sem) \
	PICK_SEM_OP_RET(compat_down_interruptible, rt_down_interruptible, sem)

#define down_trylock(sem) \
	PICK_SEM_OP_RET(compat_down_trylock, rt_down_trylock, sem)

#define up(sem) PICK_SEM_OP(compat_up, rt_up, sem)

/*
 * rwsems:
 */

#define __RWSEM_INITIALIZER(name) \
	{ .lock = __RT_MUTEX_INITIALIZER(name.lock), \
	  RW_DEP_MAP_INIT(name) }

#define DECLARE_RWSEM(lockname) \
	struct rw_semaphore lockname = __RWSEM_INITIALIZER(lockname)

extern void  __rt_rwsem_init(struct rw_semaphore *rwsem, char *name,
				     struct lock_class_key *key);

# define rt_init_rwsem(sem)				\
do {							\
	static struct lock_class_key __key;		\
							\
	__rt_rwsem_init((sem), #sem, &__key);		\
} while (0)

extern void __dont_do_this_in_rt(struct rw_semaphore *rwsem);

#define rt_down_read_non_owner(rwsem)	__dont_do_this_in_rt(rwsem)
#define rt_up_read_non_owner(rwsem)	__dont_do_this_in_rt(rwsem)

extern void  rt_down_write(struct rw_semaphore *rwsem);
extern void
rt_down_read_nested(struct rw_semaphore *rwsem, int subclass);
extern void
rt_down_write_nested(struct rw_semaphore *rwsem, int subclass);
extern void  rt_down_read(struct rw_semaphore *rwsem);
extern int  rt_down_write_trylock(struct rw_semaphore *rwsem);
extern int  rt_down_read_trylock(struct rw_semaphore *rwsem);
extern void  rt_up_read(struct rw_semaphore *rwsem);
extern void  rt_up_write(struct rw_semaphore *rwsem);
extern void  rt_downgrade_write(struct rw_semaphore *rwsem);

# define rt_rwsem_is_locked(rws)	(rt_mutex_is_locked(&(rws)->lock))

#define PICK_RWSEM_OP(...) PICK_FUNCTION(struct compat_rw_semaphore *,	\
	struct rw_semaphore *, ##__VA_ARGS__)
#define PICK_RWSEM_OP_RET(...) PICK_FUNCTION_RET(struct compat_rw_semaphore *,\
	struct rw_semaphore *, ##__VA_ARGS__)

#define init_rwsem(rwsem) PICK_RWSEM_OP(compat_init_rwsem, rt_init_rwsem, rwsem)

#define down_read(rwsem) PICK_RWSEM_OP(compat_down_read, rt_down_read, rwsem)

#define down_read_non_owner(rwsem) \
	PICK_RWSEM_OP(compat_down_read_non_owner, rt_down_read_non_owner, rwsem)

#define down_read_trylock(rwsem) \
	PICK_RWSEM_OP_RET(compat_down_read_trylock, rt_down_read_trylock, rwsem)

#define down_write(rwsem) PICK_RWSEM_OP(compat_down_write, rt_down_write, rwsem)

#define down_read_nested(rwsem, subclass) \
	PICK_RWSEM_OP(compat_down_read_nested, rt_down_read_nested,	\
		rwsem, subclass)

#define down_write_nested(rwsem, subclass) \
	PICK_RWSEM_OP(compat_down_write_nested, rt_down_write_nested,	\
		rwsem, subclass)

#define down_write_trylock(rwsem) \
	PICK_RWSEM_OP_RET(compat_down_write_trylock, rt_down_write_trylock,\
		rwsem)

#define up_read(rwsem) PICK_RWSEM_OP(compat_up_read, rt_up_read, rwsem)

#define up_read_non_owner(rwsem) \
	PICK_RWSEM_OP(compat_up_read_non_owner, rt_up_read_non_owner, rwsem)

#define up_write(rwsem) PICK_RWSEM_OP(compat_up_write, rt_up_write, rwsem)

#define downgrade_write(rwsem) \
	PICK_RWSEM_OP(compat_downgrade_write, rt_downgrade_write, rwsem)

#define rwsem_is_locked(rwsem) \
	PICK_RWSEM_OP_RET(compat_rwsem_is_locked, rt_rwsem_is_locked, rwsem)

#else
# define preempt_rt 0
#endif /* CONFIG_PREEMPT_RT */

#endif

