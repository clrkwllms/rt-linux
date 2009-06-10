#ifndef __ASM_MACH_TX49XX_CPU_FEATURE_OVERRIDES_H
#define __ASM_MACH_TX49XX_CPU_FEATURE_OVERRIDES_H

/* finish_arch_switch_empty is defined if we know finish_arch_switch() will
 * be empty, based on the lack of features defined in this file.  This is
 * needed because config preempt will barf in kernel/sched.c ifdef
 * finish_arch_switch
 */
#define finish_arch_switch_empty

#define cpu_has_llsc	1
#define cpu_has_64bits	1
#define cpu_has_inclusive_pcaches	0

#define cpu_has_mips16		0
#define cpu_has_mdmx		0
#define cpu_has_mips3d		0
#define cpu_has_smartmips	0
#define cpu_has_vtag_icache	0
#define cpu_has_ic_fills_f_dc	0
#define cpu_has_dsp	0
#define cpu_has_mipsmt	0
#define cpu_has_userlocal	0

#define cpu_has_mips32r1	0
#define cpu_has_mips32r2	0
#define cpu_has_mips64r1	0
#define cpu_has_mips64r2	0

#endif /* __ASM_MACH_TX49XX_CPU_FEATURE_OVERRIDES_H */
