/***************************************************************************/

/*
 *	linux/arch/m68knommu/platform/523x/config.c
 *
 *	Sub-architcture dependant initialization code for the Freescale
 *	523x CPUs.
 *
 *	Copyright (C) 1999-2005, Greg Ungerer (gerg@snapgear.com)
 *	Copyright (C) 2001-2003, SnapGear Inc. (www.snapgear.com)
 */

/***************************************************************************/

#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <asm/machdep.h>
#include <asm/coldfire.h>
#include <asm/mcfsim.h>
#include <asm/mcfuart.h>

#ifdef CONFIG_MTD
#include <linux/mtd/physmap.h>
#endif

/***************************************************************************/

void coldfire_reset(void);

/***************************************************************************/

static struct mcf_platform_uart m523x_uart_platform[] = {
	{
		.mapbase	= MCF_MBAR + MCFUART_BASE1,
		.irq		= MCFINT_VECBASE + MCFINT_UART0,
	},
	{
		.mapbase 	= MCF_MBAR + MCFUART_BASE2,
		.irq		= MCFINT_VECBASE + MCFINT_UART0 + 1,
	},
	{
		.mapbase 	= MCF_MBAR + MCFUART_BASE3,
		.irq		= MCFINT_VECBASE + MCFINT_UART0 + 2,
	},
	{ },
};

static struct platform_device m523x_uart = {
	.name			= "mcfuart",
	.id			= 0,
	.dev.platform_data	= m523x_uart_platform,
};

static struct platform_device *m523x_devices[] __initdata = {
	&m523x_uart,
};

/***************************************************************************/

#define	INTC0	(MCF_MBAR + MCFICM_INTC0)

static void __init m523x_uart_init_line(int line, int irq)
{
	u32 imr;

	if ((line < 0) || (line > 2))
		return;

	writeb(0x30+line, (INTC0 + MCFINTC_ICR0 + MCFINT_UART0 + line));

	imr = readl(INTC0 + MCFINTC_IMRL);
	imr &= ~((1 << (irq - MCFINT_VECBASE)) | 1);
	writel(imr, INTC0 + MCFINTC_IMRL);
}

static void __init m523x_uarts_init(void)
{
	const int nrlines = ARRAY_SIZE(m523x_uart_platform);
	int line;

	for (line = 0; (line < nrlines); line++)
		m523x_uart_init_line(line, m523x_uart_platform[line].irq);
}

/***************************************************************************/

void mcf_disableall(void)
{
	*((volatile unsigned long *) (MCF_IPSBAR + MCFICM_INTC0 + MCFINTC_IMRH)) = 0xffffffff;
	*((volatile unsigned long *) (MCF_IPSBAR + MCFICM_INTC0 + MCFINTC_IMRL)) = 0xffffffff;
}

/***************************************************************************/

void mcf_autovector(unsigned int vec)
{
	/* Everything is auto-vectored on the 523x */
}

/***************************************************************************/

#if defined(CONFIG_SAVANT)

/*
 *	Do special config for SAVANT BSP
 */
static void __init config_savantBSP(char *commandP, int size)
{
	/* setup BOOTPARAM_STRING */
	strncpy(commandP, "root=/dev/mtdblock1 ro rootfstype=romfs", size);
	/* Look at Chatter DIP Switch, if CS3 is enabled */
	{
		uint32_t *csmr3 = (uint32_t *) (MCF_IPSBAR + MCF523x_CSMR3);
		uint32_t *csar3 = (uint32_t *) (MCF_IPSBAR + MCF523x_CSAR3);
		uint16_t *dipsP = (uint16_t *) *csar3;
		uint16_t dipSetOff = *dipsP & 0x0100; // switch #1
		uint16_t *btnPressP = (uint16_t *)(*csar3 + 0x10);
		uint16_t shortButtonPress = *btnPressP & 0x8000;
		if (*csmr3 & 1) {
			/* CS3 enabled */
			if (!dipSetOff && shortButtonPress) {
				/* switch on, so be quiet */
				strncat(commandP, " console=", size-strlen(commandP)-1);
			}
		}
	}
	commandP[size-1] = 0;

	/* Set on-chip peripheral space to user mode */
	{
		uint8_t *gpacr = (uint8_t *) (MCF_IPSBAR + MCF523x_GPACR);
		uint8_t *pacr1 = (uint8_t *) (MCF_IPSBAR + MCF523x_PACR1);
		uint8_t *pacr4 = (uint8_t *) (MCF_IPSBAR + MCF523x_PACR4);
		uint8_t *pacr7 = (uint8_t *) (MCF_IPSBAR + MCF523x_PACR7);
		uint8_t *pacr8 = (uint8_t *) (MCF_IPSBAR + MCF523x_PACR8);
		*gpacr = 0x04;
		*pacr1 = 0x40; /* EIM required for Chip Select access */
		*pacr4 = 0x40; /* I2C */
		*pacr7 = 0x44; /* INTC0 & 1 handy for debug */
		*pacr8 = 0x40; /* FEC MAC */
	}

#ifdef CONFIG_MTD
	/* all board spins cannot access flash from linux unless we change the map here */
	{
		uint32_t *csar0 = (uint32_t *) (MCF_IPSBAR + MCF523x_CSAR0);
		uint32_t start = *csar0;
		uint32_t size = 0xffffFFFF - start + 1;
		physmap_configure(start, size, CONFIG_MTD_PHYSMAP_BANKWIDTH, NULL);
	}
#endif
}

#endif /* CONFIG_SAVANT */

/***************************************************************************/

void __init config_BSP(char *commandp, int size)
{
	mcf_disableall();
#if defined(CONFIG_SAVANT)
	config_savantBSP(commandp, size);
#endif /* CONFIG_SAVANT */
	mach_reset = coldfire_reset;
	m523x_uarts_init();
}

/***************************************************************************/

static int __init init_BSP(void)
{
	platform_add_devices(m523x_devices, ARRAY_SIZE(m523x_devices));
	return 0;
}

arch_initcall(init_BSP);

/***************************************************************************/
