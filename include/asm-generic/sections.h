#ifndef _ASM_GENERIC_SECTIONS_H_
#define _ASM_GENERIC_SECTIONS_H_

/* References to section boundaries */

extern char _text[], _stext[], _etext[];
extern char _data[], _sdata[], _edata[];
extern char __bss_start[], __bss_stop[];
extern char __init_begin[], __init_end[];
extern char _sinittext[], _einittext[];
extern char _end[];
#ifdef CONFIG_HAVE_ZERO_BASED_PER_CPU
extern char __per_cpu_load[];
extern char ____per_cpu_size[];
#define __per_cpu_size ((unsigned long)&____per_cpu_size)
#define __per_cpu_start ((char *)0)
#define __per_cpu_end ((char *)__per_cpu_size)
#else
extern char __per_cpu_start[], __per_cpu_end[];
#define __per_cpu_load __per_cpu_start
#define __per_cpu_size (__per_cpu_end - __per_cpu_start)
#endif
extern char __kprobes_text_start[], __kprobes_text_end[];
extern char __initdata_begin[], __initdata_end[];
extern char __start_rodata[], __end_rodata[];

#endif /* _ASM_GENERIC_SECTIONS_H_ */
