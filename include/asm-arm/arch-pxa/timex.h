/*
 * linux/include/asm-arm/arch-pxa/timex.h
 *
 * Author:	Nicolas Pitre
 * Created:	Jun 15, 2001
 * Copyright:	MontaVista Software Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */


#if defined(CONFIG_PXA25x)
/* PXA250/210 timer base */
#define CLOCK_TICK_RATE 3686400
#elif defined(CONFIG_PXA27x)
/* PXA27x timer base */
#include <asm-arm/arch-pxa/hardware.h>
#include <asm-arm/arch-pxa/pxa-regs.h>
#ifdef CONFIG_MACH_MAINSTONE
#define CLOCK_TICK_RATE 3249600
#else
#define CLOCK_TICK_RATE 3250000
#endif
#else
#define CLOCK_TICK_RATE 3250000
#endif

#define mach_read_cycles() OSCR
#define mach_cycles_to_usecs(d) (((d) * ((1000000LL << 32) / CLOCK_TICK_RATE)) >> 32)
#define mach_usecs_to_cycles(d) (((d) * (((long long)CLOCK_TICK_RATE << 32) / 1000000)) >> 32)
