#ifndef _LINUX_PICKOP_H
#define _LINUX_PICKOP_H

#undef PICK_TYPE_EQUAL
#define PICK_TYPE_EQUAL(var, type) \
		__builtin_types_compatible_p(typeof(var), type)

extern int __bad_func_type(void);

#define PICK_FUNCTION(type1, type2, func1, func2, arg0, ...)		\
do {									\
	if (PICK_TYPE_EQUAL((arg0), type1))				\
		func1((type1)(arg0), ##__VA_ARGS__);			\
	else if (PICK_TYPE_EQUAL((arg0), type2))			\
		func2((type2)(arg0), ##__VA_ARGS__);			\
	else __bad_func_type();						\
} while (0)

#define PICK_FUNCTION_RET(type1, type2, func1, func2, arg0, ...)	\
({									\
	unsigned long __ret;						\
									\
	if (PICK_TYPE_EQUAL((arg0), type1))				\
		__ret = func1((type1)(arg0), ##__VA_ARGS__);		\
	else if (PICK_TYPE_EQUAL((arg0), type2))			\
		__ret = func2((type2)(arg0), ##__VA_ARGS__);		\
	else __ret = __bad_func_type();					\
									\
	__ret;								\
})

#endif /* _LINUX_PICKOP_H */
