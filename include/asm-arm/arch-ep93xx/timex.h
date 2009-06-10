/*
 * linux/include/asm-arm/arch-ep93xx/timex.h
 */
#include <asm-arm/arch-ep93xx/ep93xx-regs.h>
#include <asm-arm/io.h>

#define CLOCK_TICK_RATE		983040

#define mach_read_cycles() __raw_readl(EP93XX_TIMER4_VALUE_LOW)
#define mach_cycles_to_usecs(d) (((d) * ((1000000LL << 32) / CLOCK_TICK_RATE)) >> 32)
#define mach_usecs_to_cycles(d) (((d) * (((long long)CLOCK_TICK_RATE << 32) / 1000000)) >> 32)
