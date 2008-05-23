#ifndef __ASM_I8253_H__
#define __ASM_I8253_H__

#include <linux/delay.h>
#include <linux/io.h>

/* i8253A PIT registers */
#define PIT_MODE		0x43
#define PIT_CH0			0x40
#define PIT_CH2			0x42

extern spinlock_t i8253_lock;

extern struct clock_event_device *global_clock_event;

extern void setup_pit_timer(void);

/*
 * Accesses to PIT registers need careful delays on some platforms. Define
 * them here in a common place
 */
static inline unsigned char inb_pit(unsigned int port)
{
	unsigned char val = inb(port);

	/*
	 * Delay for some accesses to PIT on motherboard or in chipset must be
	 * at least one microsecond, so be safe here:
	 */
	udelay(2);

	return val;
}

static inline void outb_pit(unsigned char value, unsigned int port)
{
	outb(value, port);
	/*
	 * delay for some accesses to PIT on motherboard or in chipset
	 * must be at least one microsecond, so be safe here:
	 */
	udelay(2);
}

#endif	/* __ASM_I8253_H__ */
