#ifndef __LINUX_SPINLOCK_H
#define __LINUX_SPINLOCK_H

/*
 * include/linux/spinlock.h - generic spinlock/rwlock declarations
 *
 * here's the role of the various spinlock/rwlock related include files:
 *
 * on SMP builds:
 *
 *  asm/spinlock_types.h: contains the raw_spinlock_t/raw_rwlock_t and the
 *                        initializers
 *
 *  linux/spinlock_types.h:
 *                        defines the generic type and initializers
 *
 *  asm/spinlock.h:       contains the __raw_spin_*()/etc. lowlevel
 *                        implementations, mostly inline assembly code
 *
 *   (also included on UP-debug builds:)
 *
 *  linux/spinlock_api_smp.h:
 *                        contains the prototypes for the _spin_*() APIs.
 *
 *  linux/spinlock.h:     builds the final spin_*() APIs.
 *
 * on UP builds:
 *
 *  linux/spinlock_type_up.h:
 *                        contains the generic, simplified UP spinlock type.
 *                        (which is an empty structure on non-debug builds)
 *
 *  linux/spinlock_types.h:
 *                        defines the generic type and initializers
 *
 *  linux/spinlock_up.h:
 *                        contains the __raw_spin_*()/etc. version of UP
 *                        builds. (which are NOPs on non-debug, non-preempt
 *                        builds)
 *
 *   (included on UP-non-debug builds:)
 *
 *  linux/spinlock_api_up.h:
 *                        builds the _spin_*() APIs.
 *
 *  linux/spinlock.h:     builds the final spin_*() APIs.
 *
 *
 * Public types and naming conventions:
 * ------------------------------------
 * spinlock_t:				type:  sleep-lock
 * raw_spinlock_t:			type:  spin-lock (debug)
 *
 * spin_lock([raw_]spinlock_t):		API:   acquire lock, both types
 *
 *
 * Internal types and naming conventions:
 * -------------------------------------
 * __raw_spinlock_t:			type: lowlevel spin-lock
 *
 * _spin_lock(struct rt_mutex):		API:  acquire sleep-lock
 * __spin_lock(raw_spinlock_t):		API:  acquire spin-lock (highlevel)
 * _raw_spin_lock(raw_spinlock_t):	API:  acquire spin-lock (debug)
 * __raw_spin_lock(__raw_spinlock_t):	API:  acquire spin-lock (lowlevel)
 *
 *
 * spin_lock(raw_spinlock_t) translates into the following chain of
 * calls/inlines/macros, if spin-lock debugging is enabled:
 *
 *       spin_lock()			[include/linux/spinlock.h]
 * ->    __spin_lock()			[kernel/spinlock.c]
 *  ->   _raw_spin_lock()		[lib/spinlock_debug.c]
 *   ->  __raw_spin_lock()		[include/asm/spinlock.h]
 *
 * spin_lock(spinlock_t) translates into the following chain of
 * calls/inlines/macros:
 *
 *       spin_lock()			[include/linux/spinlock.h]
 * ->    _spin_lock()			[include/linux/spinlock.h]
 *  ->   rt_spin_lock()			[kernel/rtmutex.c]
 *   ->  rt_spin_lock_fastlock()	[kernel/rtmutex.c]
 *    -> rt_spin_lock_slowlock()	[kernel/rtmutex.c]
 */

#include <linux/preempt.h>
#include <linux/linkage.h>
#include <linux/compiler.h>
#include <linux/thread_info.h>
#include <linux/kernel.h>
#include <linux/cache.h>
#include <linux/stringify.h>
#include <linux/bottom_half.h>
#include <linux/irqflags.h>

#include <asm/system.h>

/*
 * Pull the raw_spinlock_t and raw_rwlock_t definitions:
 */
#include <linux/spinlock_types.h>

extern int __lockfunc generic__raw_read_trylock(raw_rwlock_t *lock);

/*
 * Pull the __raw*() functions/declarations (UP-nondebug doesnt need them):
 */
#ifdef CONFIG_SMP
# include <asm/spinlock.h>
#else
# include <linux/spinlock_up.h>
#endif

/*
 * Pull the RT types:
 */
#include <linux/rt_lock.h>

/*
 * Pull the _spin_*()/_read_*()/_write_*() functions/declarations:
 */
#if defined(CONFIG_SMP) || defined(CONFIG_DEBUG_SPINLOCK)
# include <linux/spinlock_api_smp.h>
#else
# include <linux/spinlock_api_up.h>
#endif

#ifdef CONFIG_DEBUG_SPINLOCK
 extern __lockfunc void _raw_spin_lock(raw_spinlock_t *lock);
# define _raw_spin_lock_flags(lock, flags) _raw_spin_lock(lock)
 extern __lockfunc int _raw_spin_trylock(raw_spinlock_t *lock);
 extern __lockfunc void _raw_spin_unlock(raw_spinlock_t *lock);
 extern __lockfunc void _raw_read_lock(raw_rwlock_t *lock);
 extern __lockfunc int _raw_read_trylock(raw_rwlock_t *lock);
 extern __lockfunc void _raw_read_unlock(raw_rwlock_t *lock);
 extern __lockfunc void _raw_write_lock(raw_rwlock_t *lock);
 extern __lockfunc int _raw_write_trylock(raw_rwlock_t *lock);
 extern __lockfunc void _raw_write_unlock(raw_rwlock_t *lock);
#else
# define _raw_spin_lock(lock)		__raw_spin_lock(&(lock)->raw_lock)
# define _raw_spin_lock_flags(lock, flags) \
		__raw_spin_lock_flags(&(lock)->raw_lock, *(flags))
# define _raw_spin_trylock(lock)	__raw_spin_trylock(&(lock)->raw_lock)
# define _raw_spin_unlock(lock)		__raw_spin_unlock(&(lock)->raw_lock)
# define _raw_read_lock(rwlock)		__raw_read_lock(&(rwlock)->raw_lock)
# define _raw_read_trylock(rwlock)	__raw_read_trylock(&(rwlock)->raw_lock)
# define _raw_read_unlock(rwlock)	__raw_read_unlock(&(rwlock)->raw_lock)
# define _raw_write_lock(rwlock)	__raw_write_lock(&(rwlock)->raw_lock)
# define _raw_write_trylock(rwlock)	__raw_write_trylock(&(rwlock)->raw_lock)
# define _raw_write_unlock(rwlock)	__raw_write_unlock(&(rwlock)->raw_lock)
#endif

extern int __bad_spinlock_type(void);
extern int __bad_rwlock_type(void);

extern void
__rt_spin_lock_init(spinlock_t *lock, char *name, struct lock_class_key *key);

extern void __lockfunc rt_spin_lock(spinlock_t *lock);
extern void __lockfunc rt_spin_lock_nested(spinlock_t *lock, int subclass);
extern void __lockfunc rt_spin_unlock(spinlock_t *lock);
extern void __lockfunc rt_spin_unlock_wait(spinlock_t *lock);
extern int __lockfunc
rt_spin_trylock_irqsave(spinlock_t *lock, unsigned long *flags);
extern int __lockfunc rt_spin_trylock(spinlock_t *lock);
extern int _atomic_dec_and_spin_lock(atomic_t *atomic, spinlock_t *lock);

/*
 * lockdep-less calls, for derived types like rwlock:
 * (for trylock they can use rt_mutex_trylock() directly.
 */
extern void __lockfunc __rt_spin_lock(struct rt_mutex *lock);
extern void __lockfunc __rt_spin_unlock(struct rt_mutex *lock);

#ifdef CONFIG_PREEMPT_RT
# define _spin_lock(l)			rt_spin_lock(l)
# define _spin_lock_nested(l, s)	rt_spin_lock_nested(l, s)
# define _spin_lock_bh(l)		rt_spin_lock(l)
# define _spin_lock_irq(l)		rt_spin_lock(l)
# define _spin_unlock(l)		rt_spin_unlock(l)
# define _spin_unlock_no_resched(l)	rt_spin_unlock(l)
# define _spin_unlock_bh(l)		rt_spin_unlock(l)
# define _spin_unlock_irq(l)		rt_spin_unlock(l)
# define _spin_unlock_irqrestore(l, f)	rt_spin_unlock(l)
static inline unsigned long __lockfunc _spin_lock_irqsave(spinlock_t *lock)
{
	rt_spin_lock(lock);
	return 0;
}
static inline unsigned long __lockfunc
_spin_lock_irqsave_nested(spinlock_t *lock, int subclass)
{
	rt_spin_lock_nested(lock, subclass);
	return 0;
}
#else
static inline unsigned long __lockfunc _spin_lock_irqsave(spinlock_t *lock)
{
	return 0;
}
static inline unsigned long __lockfunc
_spin_lock_irqsave_nested(spinlock_t *lock, int subclass)
{
	return 0;
}
# define _spin_lock(l)			do { } while (0)
# define _spin_lock_nested(l, s)	do { } while (0)
# define _spin_lock_bh(l)		do { } while (0)
# define _spin_lock_irq(l)		do { } while (0)
# define _spin_unlock(l)		do { } while (0)
# define _spin_unlock_no_resched(l)	do { } while (0)
# define _spin_unlock_bh(l)		do { } while (0)
# define _spin_unlock_irq(l)		do { } while (0)
# define _spin_unlock_irqrestore(l, f)	do { } while (0)
#endif

#define _spin_lock_init(sl, n, f, l) \
do {							\
	static struct lock_class_key __key;		\
							\
	__rt_spin_lock_init(sl, n, &__key);		\
} while (0)

# ifdef CONFIG_PREEMPT_RT
#  define _spin_can_lock(l)		(!rt_mutex_is_locked(&(l)->lock))
#  define _spin_is_locked(l)		rt_mutex_is_locked(&(l)->lock)
#  define _spin_unlock_wait(l)		rt_spin_unlock_wait(l)

#  define _spin_trylock(l)		rt_spin_trylock(l)
#  define _spin_trylock_bh(l)		rt_spin_trylock(l)
#  define _spin_trylock_irq(l)		rt_spin_trylock(l)
#  define _spin_trylock_irqsave(l,f)	rt_spin_trylock_irqsave(l, f)
# else

   extern int this_should_never_be_called_on_non_rt(spinlock_t *lock);
#  define TSNBCONRT(l) this_should_never_be_called_on_non_rt(l)
#  define _spin_can_lock(l)		TSNBCONRT(l)
#  define _spin_is_locked(l)		TSNBCONRT(l)
#  define _spin_unlock_wait(l)		TSNBCONRT(l)

#  define _spin_trylock(l)		TSNBCONRT(l)
#  define _spin_trylock_bh(l)		TSNBCONRT(l)
#  define _spin_trylock_irq(l)		TSNBCONRT(l)
#  define _spin_trylock_irqsave(l,f)	TSNBCONRT(l)
#endif

#undef TYPE_EQUAL
#define TYPE_EQUAL(lock, type) \
		__builtin_types_compatible_p(typeof(lock), type *)

#define PICK_OP(op, lock)						\
do {									\
	if (TYPE_EQUAL((lock), raw_spinlock_t))				\
		__spin##op((raw_spinlock_t *)(lock));			\
	else if (TYPE_EQUAL(lock, spinlock_t))				\
		_spin##op((spinlock_t *)(lock));			\
	else __bad_spinlock_type();					\
} while (0)

#define PICK_OP_RET(op, lock...)					\
({									\
	unsigned long __ret;						\
									\
	if (TYPE_EQUAL((lock), raw_spinlock_t))	 			\
		__ret = __spin##op((raw_spinlock_t *)(lock));		\
	else if (TYPE_EQUAL(lock, spinlock_t))				\
		__ret = _spin##op((spinlock_t *)(lock));		\
	else __ret = __bad_spinlock_type();				\
									\
	__ret;								\
})

#define PICK_OP2(op, lock, flags)					\
do {									\
	if (TYPE_EQUAL((lock), raw_spinlock_t))				\
		__spin##op((raw_spinlock_t *)(lock), flags);		\
	else if (TYPE_EQUAL(lock, spinlock_t))				\
		_spin##op((spinlock_t *)(lock), flags);			\
	else __bad_spinlock_type();					\
} while (0)

#define PICK_OP2_RET(op, lock, flags)					\
({									\
	unsigned long __ret;						\
									\
	if (TYPE_EQUAL((lock), raw_spinlock_t))				\
		__ret = __spin##op((raw_spinlock_t *)(lock), flags);	\
	else if (TYPE_EQUAL(lock, spinlock_t))				\
		__ret = _spin##op((spinlock_t *)(lock), flags);		\
	else __bad_spinlock_type();					\
									\
	__ret;								\
})

extern void __lockfunc rt_write_lock(rwlock_t *rwlock);
extern void __lockfunc rt_read_lock(rwlock_t *rwlock);
extern int __lockfunc rt_write_trylock(rwlock_t *rwlock);
extern int __lockfunc rt_read_trylock(rwlock_t *rwlock);
extern void __lockfunc rt_write_unlock(rwlock_t *rwlock);
extern void __lockfunc rt_read_unlock(rwlock_t *rwlock);
extern unsigned long __lockfunc rt_write_lock_irqsave(rwlock_t *rwlock);
extern unsigned long __lockfunc rt_read_lock_irqsave(rwlock_t *rwlock);
extern void
__rt_rwlock_init(rwlock_t *rwlock, char *name, struct lock_class_key *key);

#define _rwlock_init(rwl, n, f, l)			\
do {							\
	static struct lock_class_key __key;		\
							\
	__rt_rwlock_init(rwl, n, &__key);		\
} while (0)

#ifdef CONFIG_PREEMPT_RT
# define rt_read_can_lock(rwl)	(!rt_mutex_is_locked(&(rwl)->lock))
# define rt_write_can_lock(rwl)	(!rt_mutex_is_locked(&(rwl)->lock))
#else
 extern int rt_rwlock_can_lock_never_call_on_non_rt(rwlock_t *rwlock);
# define rt_read_can_lock(rwl)	rt_rwlock_can_lock_never_call_on_non_rt(rwl)
# define rt_write_can_lock(rwl)	rt_rwlock_can_lock_never_call_on_non_rt(rwl)
#endif

# define _read_can_lock(rwl)	rt_read_can_lock(rwl)
# define _write_can_lock(rwl)	rt_write_can_lock(rwl)

# define _read_trylock(rwl)	rt_read_trylock(rwl)
# define _write_trylock(rwl)	rt_write_trylock(rwl)
# define _write_trylock_irqsave(rwl, flags) \
	rt_write_trylock_irqsave(rwl, flags)

# define _read_lock(rwl)	rt_read_lock(rwl)
# define _write_lock(rwl)	rt_write_lock(rwl)
# define _read_unlock(rwl)	rt_read_unlock(rwl)
# define _write_unlock(rwl)	rt_write_unlock(rwl)

# define _read_lock_bh(rwl)	rt_read_lock(rwl)
# define _write_lock_bh(rwl)	rt_write_lock(rwl)
# define _read_unlock_bh(rwl)	rt_read_unlock(rwl)
# define _write_unlock_bh(rwl)	rt_write_unlock(rwl)

# define _read_lock_irq(rwl)	rt_read_lock(rwl)
# define _write_lock_irq(rwl)	rt_write_lock(rwl)
# define _read_unlock_irq(rwl)	rt_read_unlock(rwl)
# define _write_unlock_irq(rwl)	rt_write_unlock(rwl)

# define _read_lock_irqsave(rwl) 	rt_read_lock_irqsave(rwl)
# define _write_lock_irqsave(rwl)	rt_write_lock_irqsave(rwl)

# define _read_unlock_irqrestore(rwl, f)	rt_read_unlock(rwl)
# define _write_unlock_irqrestore(rwl, f)	rt_write_unlock(rwl)

#define __PICK_RW_OP(optype, op, lock)					\
do {									\
	if (TYPE_EQUAL((lock), raw_rwlock_t))				\
		__##optype##op((raw_rwlock_t *)(lock));			\
	else if (TYPE_EQUAL(lock, rwlock_t))				\
		##op((rwlock_t *)(lock));				\
	else __bad_rwlock_type();					\
} while (0)

#define PICK_RW_OP(optype, op, lock)					\
do {									\
	if (TYPE_EQUAL((lock), raw_rwlock_t))				\
		__##optype##op((raw_rwlock_t *)(lock));			\
	else if (TYPE_EQUAL(lock, rwlock_t))				\
		_##optype##op((rwlock_t *)(lock));			\
	else __bad_rwlock_type();					\
} while (0)

#define __PICK_RW_OP_RET(optype, op, lock...)				\
({									\
	unsigned long __ret;						\
									\
	if (TYPE_EQUAL((lock), raw_rwlock_t))		  		\
		__ret = __##optype##op((raw_rwlock_t *)(lock));		\
	else if (TYPE_EQUAL(lock, rwlock_t))				\
		__ret = _##optype##op((rwlock_t *)(lock));		\
	else __ret = __bad_rwlock_type();				\
									\
	__ret;								\
})

#define PICK_RW_OP_RET(optype, op, lock...)				\
({									\
	unsigned long __ret;						\
									\
	if (TYPE_EQUAL((lock), raw_rwlock_t))				\
		__ret = __##optype##op((raw_rwlock_t *)(lock));		\
	else if (TYPE_EQUAL(lock, rwlock_t))				\
		__ret = _##optype##op((rwlock_t *)(lock));		\
	else __ret = __bad_rwlock_type();				\
									\
	__ret;								\
})

#define PICK_RW_OP2(optype, op, lock, flags)				\
do {									\
	if (TYPE_EQUAL((lock), raw_rwlock_t))				\
		__##optype##op((raw_rwlock_t *)(lock), flags);		\
	else if (TYPE_EQUAL(lock, rwlock_t))				\
		_##optype##op((rwlock_t *)(lock), flags);		\
	else __bad_rwlock_type();					\
} while (0)

#define PICK_RW_OP2_RET(optype, op, lock, flags)			\
({									\
	unsigned long __ret;						\
									\
	if (TYPE_EQUAL((lock), raw_rwlock_t))				\
		__ret = __##optype##op((raw_rwlock_t *)(lock), flags);	\
	else if (TYPE_EQUAL(lock, rwlock_t))				\
		__ret = _##optype##op((rwlock_t *)(lock), flags);	\
	else __bad_rwlock_type();					\
									\
	__ret;								\
})

#ifdef CONFIG_DEBUG_SPINLOCK
  extern void __raw_spin_lock_init(raw_spinlock_t *lock, const char *name,
				   struct lock_class_key *key);
# define _raw_spin_lock_init(lock)				\
do {								\
	static struct lock_class_key __key;			\
								\
	__raw_spin_lock_init((lock), #lock, &__key);		\
} while (0)

#else
#define __raw_spin_lock_init(lock) \
	do { *(lock) = RAW_SPIN_LOCK_UNLOCKED(lock); } while (0)
# define _raw_spin_lock_init(lock) __raw_spin_lock_init(lock)
#endif

#define PICK_OP_INIT(op, lock)						\
do {									\
	if (TYPE_EQUAL((lock), raw_spinlock_t))				\
		_raw_spin##op((raw_spinlock_t *)(lock));		\
	else if (TYPE_EQUAL(lock, spinlock_t))				\
		_spin##op((spinlock_t *)(lock), #lock, __FILE__, __LINE__); \
	else __bad_spinlock_type();					\
} while (0)


#define spin_lock_init(lock)		PICK_OP_INIT(_lock_init, lock)

#ifdef CONFIG_DEBUG_SPINLOCK
  extern void __raw_rwlock_init(raw_rwlock_t *lock, const char *name,
				struct lock_class_key *key);
# define _raw_rwlock_init(lock)					\
do {								\
	static struct lock_class_key __key;			\
								\
	__raw_rwlock_init((lock), #lock, &__key);		\
} while (0)
#else
#define __raw_rwlock_init(lock) \
	do { *(lock) = RAW_RW_LOCK_UNLOCKED(lock); } while (0)
# define _raw_rwlock_init(lock) __raw_rwlock_init(lock)
#endif

#define __PICK_RW_OP_INIT(optype, op, lock)				\
do {									\
	if (TYPE_EQUAL((lock), raw_rwlock_t))				\
		_raw_##optype##op((raw_rwlock_t *)(lock));		\
	else if (TYPE_EQUAL(lock, rwlock_t))				\
		_##optype##op((rwlock_t *)(lock), #lock, __FILE__, __LINE__);\
	else __bad_spinlock_type();					\
} while (0)

#define rwlock_init(lock)	__PICK_RW_OP_INIT(rwlock, _init, lock)

#define __spin_is_locked(lock)	__raw_spin_is_locked(&(lock)->raw_lock)

#define spin_is_locked(lock)	PICK_OP_RET(_is_locked, lock)

#define __spin_unlock_wait(lock) __raw_spin_unlock_wait(&(lock)->raw_lock)

#define spin_unlock_wait(lock)	PICK_OP(_unlock_wait, lock)
/*
 * Define the various spin_lock and rw_lock methods.  Note we define these
 * regardless of whether CONFIG_SMP or CONFIG_PREEMPT are set. The various
 * methods are defined as nops in the case they are not required.
 */
// #define spin_trylock(lock)	_spin_trylock(lock)
#define spin_trylock(lock)	__cond_lock(lock, PICK_OP_RET(_trylock, lock))

//#define read_trylock(lock)	_read_trylock(lock)
#define read_trylock(lock)	__cond_lock(lock, PICK_RW_OP_RET(read, _trylock, lock))

//#define write_trylock(lock)	_write_trylock(lock)
#define write_trylock(lock)	__cond_lock(lock, PICK_RW_OP_RET(write, _trylock, lock))

#define write_trylock_irqsave(lock, flags) \
	__cond_lock(lock, PICK_RW_OP2_RET(write, _trylock_irqsave, lock, &flags))

#define __spin_can_lock(lock)	__raw_spin_can_lock(&(lock)->raw_lock)
#define __read_can_lock(lock)	__raw_read_can_lock(&(lock)->raw_lock)
#define __write_can_lock(lock)	__raw_write_can_lock(&(lock)->raw_lock)

#define spin_can_lock(lock) \
	__cond_lock(lock, PICK_OP_RET(_can_lock, lock))

#define read_can_lock(lock) \
	__cond_lock(lock, PICK_RW_OP_RET(read, _can_lock, lock))

#define write_can_lock(lock) \
	__cond_lock(lock, PICK_RW_OP_RET(write, _can_lock, lock))

// #define spin_lock(lock)	_spin_lock(lock)
#define spin_lock(lock)		PICK_OP(_lock, lock)

#ifdef CONFIG_DEBUG_LOCK_ALLOC
# define spin_lock_nested(lock, subclass) PICK_OP2(_lock_nested, lock, subclass)
#else
# define spin_lock_nested(lock, subclass) spin_lock(lock)
#endif

//#define write_lock(lock)	_write_lock(lock)
#define write_lock(lock)	PICK_RW_OP(write, _lock, lock)

// #define read_lock(lock)	_read_lock(lock)
#define read_lock(lock)		PICK_RW_OP(read, _lock, lock)

# define spin_lock_irqsave(lock, flags)				\
do {								\
	BUILD_CHECK_IRQ_FLAGS(flags);				\
	flags = PICK_OP_RET(_lock_irqsave, lock);		\
} while (0)

#ifdef CONFIG_DEBUG_LOCK_ALLOC
# define spin_lock_irqsave_nested(lock, flags, subclass)		\
do {									\
	BUILD_CHECK_IRQ_FLAGS(flags);					\
	flags = PICK_OP2_RET(_lock_irqsave_nested, lock, subclass);	\
} while (0)
#else
# define spin_lock_irqsave_nested(lock, flags, subclass) \
				spin_lock_irqsave(lock, flags)
#endif

# define read_lock_irqsave(lock, flags)				\
do {								\
	BUILD_CHECK_IRQ_FLAGS(flags);				\
	flags = PICK_RW_OP_RET(read, _lock_irqsave, lock);	\
} while (0)

# define write_lock_irqsave(lock, flags)			\
do {								\
	BUILD_CHECK_IRQ_FLAGS(flags);				\
	flags = PICK_RW_OP_RET(write, _lock_irqsave, lock);	\
} while (0)

// #define spin_lock_irq(lock)	_spin_lock_irq(lock)
// #define spin_lock_bh(lock)	_spin_lock_bh(lock)
#define spin_lock_irq(lock)	PICK_OP(_lock_irq, lock)
#define spin_lock_bh(lock)	PICK_OP(_lock_bh, lock)

// #define read_lock_irq(lock)	_read_lock_irq(lock)
// #define read_lock_bh(lock)	_read_lock_bh(lock)
#define read_lock_irq(lock)	PICK_RW_OP(read, _lock_irq, lock)
#define read_lock_bh(lock)	PICK_RW_OP(read, _lock_bh, lock)

// #define write_lock_irq(lock)		_write_lock_irq(lock)
// #define write_lock_bh(lock)		_write_lock_bh(lock)
#define write_lock_irq(lock)	PICK_RW_OP(write, _lock_irq, lock)
#define write_lock_bh(lock)	PICK_RW_OP(write, _lock_bh, lock)

// #define spin_unlock(lock)	_spin_unlock(lock)
// #define write_unlock(lock)	_write_unlock(lock)
// #define read_unlock(lock)	_read_unlock(lock)
#define spin_unlock(lock)	PICK_OP(_unlock, lock)
#define read_unlock(lock)	PICK_RW_OP(read, _unlock, lock)
#define write_unlock(lock)	PICK_RW_OP(write, _unlock, lock)

// #define spin_unlock(lock)	_spin_unlock_no_resched(lock)
#define spin_unlock_no_resched(lock) \
				PICK_OP(_unlock_no_resched, lock)

//#define spin_unlock_irqrestore(lock, flags)
//		_spin_unlock_irqrestore(lock, flags)
//#define spin_unlock_irq(lock)	_spin_unlock_irq(lock)
//#define spin_unlock_bh(lock)	_spin_unlock_bh(lock)
#define spin_unlock_irqrestore(lock, flags)		\
do {							\
	BUILD_CHECK_IRQ_FLAGS(flags);			\
	PICK_OP2(_unlock_irqrestore, lock, flags);	\
} while (0)

#define spin_unlock_irq(lock)	PICK_OP(_unlock_irq, lock)
#define spin_unlock_bh(lock)	PICK_OP(_unlock_bh, lock)

// #define read_unlock_irqrestore(lock, flags)
// 		_read_unlock_irqrestore(lock, flags)
// #define read_unlock_irq(lock)	_read_unlock_irq(lock)
// #define read_unlock_bh(lock)	_read_unlock_bh(lock)
#define read_unlock_irqrestore(lock, flags)			\
do {								\
	BUILD_CHECK_IRQ_FLAGS(flags);				\
	PICK_RW_OP2(read, _unlock_irqrestore, lock, flags);	\
} while (0)

#define read_unlock_irq(lock)	PICK_RW_OP(read, _unlock_irq, lock)
#define read_unlock_bh(lock)	PICK_RW_OP(read, _unlock_bh, lock)

// #define write_unlock_irqrestore(lock, flags)
// 	_write_unlock_irqrestore(lock, flags)
// #define write_unlock_irq(lock)			_write_unlock_irq(lock)
// #define write_unlock_bh(lock)			_write_unlock_bh(lock)
#define write_unlock_irqrestore(lock, flags)			\
do {								\
	BUILD_CHECK_IRQ_FLAGS(flags);				\
	PICK_RW_OP2(write, _unlock_irqrestore, lock, flags);	\
} while (0)
#define write_unlock_irq(lock)	PICK_RW_OP(write, _unlock_irq, lock)
#define write_unlock_bh(lock)	PICK_RW_OP(write, _unlock_bh, lock)

// #define spin_trylock_bh(lock)	_spin_trylock_bh(lock)
#define spin_trylock_bh(lock)	__cond_lock(lock, PICK_OP_RET(_trylock_bh, lock))

// #define spin_trylock_irq(lock)

#define spin_trylock_irq(lock)	__cond_lock(lock, PICK_OP_RET(_trylock_irq, lock))

// #define spin_trylock_irqsave(lock, flags)

#define spin_trylock_irqsave(lock, flags) \
		__cond_lock(lock, PICK_OP2_RET(_trylock_irqsave, lock, &flags))

/* "lock on reference count zero" */
#ifndef ATOMIC_DEC_AND_LOCK
# include <asm/atomic.h>
  extern int __atomic_dec_and_spin_lock(atomic_t *atomic, raw_spinlock_t *lock);
#endif

#define atomic_dec_and_lock(atomic, lock)				\
__cond_lock(lock, ({							\
	unsigned long __ret;						\
									\
	if (TYPE_EQUAL(lock, raw_spinlock_t))				\
		__ret = __atomic_dec_and_spin_lock(atomic,		\
					(raw_spinlock_t *)(lock));	\
	else if (TYPE_EQUAL(lock, spinlock_t))				\
		__ret = _atomic_dec_and_spin_lock(atomic,		\
					(spinlock_t *)(lock));		\
	else __ret = __bad_spinlock_type();				\
									\
	__ret;								\
}))


/*
 *  bit-based spin_lock()
 *
 * Don't use this unless you really need to: spin_lock() and spin_unlock()
 * are significantly faster.
 */
static inline void bit_spin_lock(int bitnum, unsigned long *addr)
{
	/*
	 * Assuming the lock is uncontended, this never enters
	 * the body of the outer loop. If it is contended, then
	 * within the inner loop a non-atomic test is used to
	 * busywait with less bus contention for a good time to
	 * attempt to acquire the lock bit.
	 */
#if defined(CONFIG_SMP) || defined(CONFIG_DEBUG_SPINLOCK) || defined(CONFIG_PREEMPT)
	while (test_and_set_bit(bitnum, addr))
		while (test_bit(bitnum, addr))
			cpu_relax();
#endif
	__acquire(bitlock);
}

/*
 * Return true if it was acquired
 */
static inline int bit_spin_trylock(int bitnum, unsigned long *addr)
{
#if defined(CONFIG_SMP) || defined(CONFIG_DEBUG_SPINLOCK) || defined(CONFIG_PREEMPT)
	if (test_and_set_bit(bitnum, addr))
		return 0;
#endif
	__acquire(bitlock);
	return 1;
}

/*
 *  bit-based spin_unlock()
 */
static inline void bit_spin_unlock(int bitnum, unsigned long *addr)
{
#if defined(CONFIG_SMP) || defined(CONFIG_DEBUG_SPINLOCK) || defined(CONFIG_PREEMPT)
	BUG_ON(!test_bit(bitnum, addr));
	smp_mb__before_clear_bit();
	clear_bit(bitnum, addr);
#endif
	__release(bitlock);
}

/*
 * Return true if the lock is held.
 */
static inline int bit_spin_is_locked(int bitnum, unsigned long *addr)
{
#if defined(CONFIG_SMP) || defined(CONFIG_DEBUG_SPINLOCK) || defined(CONFIG_PREEMPT)
	return test_bit(bitnum, addr);
#else
	return 1;
#endif
}

/**
 * __raw_spin_can_lock - would __raw_spin_trylock() succeed?
 * @lock: the spinlock in question.
 */
#define __raw_spin_can_lock(lock)            (!__raw_spin_is_locked(lock))

/*
 * Locks two spinlocks l1 and l2.
 * l1_first indicates if spinlock l1 should be taken first.
 */
static inline void
raw_double_spin_lock(raw_spinlock_t *l1, raw_spinlock_t *l2, bool l1_first)
	__acquires(l1)
	__acquires(l2)
{
	if (l1_first) {
		spin_lock(l1);
		spin_lock(l2);
	} else {
		spin_lock(l2);
		spin_lock(l1);
	}
}

static inline void
double_spin_lock(spinlock_t *l1, spinlock_t *l2, bool l1_first)
	__acquires(l1)
	__acquires(l2)
{
	if (l1_first) {
		spin_lock(l1);
		spin_lock(l2);
	} else {
		spin_lock(l2);
		spin_lock(l1);
	}
}


/*
 * Unlocks two spinlocks l1 and l2.
 * l1_taken_first indicates if spinlock l1 was taken first and therefore
 * should be released after spinlock l2.
 */
static inline void
raw_double_spin_unlock(raw_spinlock_t *l1, raw_spinlock_t *l2,
		       bool l1_taken_first)
	__releases(l1)
	__releases(l2)
{
	if (l1_taken_first) {
		spin_unlock(l2);
		spin_unlock(l1);
	} else {
		spin_unlock(l1);
		spin_unlock(l2);
	}
}

static inline void
double_spin_unlock(spinlock_t *l1, spinlock_t *l2, bool l1_taken_first)
	__releases(l1)
	__releases(l2)
{
	if (l1_taken_first) {
		spin_unlock(l2);
		spin_unlock(l1);
	} else {
		spin_unlock(l1);
		spin_unlock(l2);
	}
}

#endif /* __LINUX_SPINLOCK_H */

