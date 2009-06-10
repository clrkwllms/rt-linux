/*
 * IRQ-Chip implementation for Coldfire
 *
 * Author: Sebastian Siewior <bigeasy@linutronix.de>
 */

#include <linux/types.h>
#include <linux/irq.h>
#include <asm/coldfire.h>
#include <asm/mcfsim.h>

static inline void *coldfire_irqnum_to_mem(unsigned int irq)
{
	u32 imrp;

	imrp = MCF_IPSBAR;
#if defined(MCFINT_INTC1_VECBASE)
	if (irq > MCFINT_INTC1_VECBASE) {
		imrp += MCFICM_INTC1;
		irq -= MCFINT_PER_INTC;
	} else
#endif
		imrp += MCFICM_INTC0;

	irq -= MCFINT_VECBASE;

	if (irq > 32)
		imrp += MCFINTC_IMRH;
	else
		imrp += MCFINTC_IMRL;

	return (void *)imrp;
}

static inline unsigned int coldfire_irqnum_to_bit(unsigned int irq)
{
	irq -= MCFINT_VECBASE;

	if (irq > 32)
		irq -= 32;

	return irq;
}

static void coldfire_mask(unsigned int irq)
{
	volatile unsigned long *imrp;
	u32 mask;
	u32 irq_bit;

	imrp = coldfire_irqnum_to_mem(irq);
	irq_bit = coldfire_irqnum_to_bit(irq);

	mask = 1 << irq_bit;
	*imrp |= mask;
}

static void coldfire_unmask(unsigned int irq)
{
	volatile unsigned long *imrp;
	u32 mask;
	u32 irq_bit;

	imrp = coldfire_irqnum_to_mem(irq);
	irq_bit = coldfire_irqnum_to_bit(irq);

	mask = 1 << irq_bit;
	*imrp &= ~mask;
}

static void coldfire_nop(unsigned int irq)
{
}

static struct irq_chip m_irq_chip = {
	.name           = "M68K-INTC",
	.ack            = coldfire_nop,
	.mask           = coldfire_mask,
	.unmask         = coldfire_unmask,
};

void __init coldfire_init_irq_chip(void)
{
	volatile u32 *imrp;
	volatile u8 *icrp;
	u32 irq;
	u32 i;

	for (irq = 0; irq < NR_IRQS; irq++)
		set_irq_chip_and_handler_name(irq, &m_irq_chip,
				handle_level_irq, m_irq_chip.name);

	/* setup prios for interrupt sources (first field is reserved) */
	icrp = (u8 *)MCF_IPSBAR + MCFICM_INTC0 + MCFINTC_ICR0;
	for (i = 1; i <= 63; i++)
		icrp[i] = i;

	/* remove the disable all flag, disable all interrupt sources */
	imrp = coldfire_irqnum_to_mem(MCFINT_VECBASE);
	*imrp = 0xfffffffe;

#if defined(MCFINT_INTC1_VECBASE)
	icrp = (u8 *)MCF_IPSBAR + MCFICM_INTC1 + MCFINTC_ICR0;
	for (i = 1; i <= 63; i++)
		icrp[i] = i;

	imrp = coldfire_irqnum_to_mem(MCFINT_INTC1_VECBASE);
	*imrp = 0xfffffffe;
#endif
}
