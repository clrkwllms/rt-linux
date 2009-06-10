/****************************************************************************/

/*
 *	m523xsim.h -- ColdFire 523x System Integration Module support.
 *
 *	(C) Copyright 2003-2005, Greg Ungerer <gerg@snapgear.com>
 */

/****************************************************************************/
#ifndef	m523xsim_h
#define	m523xsim_h
/****************************************************************************/

/*
 *	Define the 523x SIM register set addresses.
 */
#define	MCFICM_INTC0		0x0c00		/* Base for Interrupt Ctrl 0 */
#define	MCFICM_INTC1		0x0d00		/* Base for Interrupt Ctrl 0 */
#define	MCFINTC_IPRH		0x00		/* Interrupt pending 32-63 */
#define	MCFINTC_IPRL		0x04		/* Interrupt pending 1-31 */
#define	MCFINTC_IMRH		0x08		/* Interrupt mask 32-63 */
#define	MCFINTC_IMRL		0x0c		/* Interrupt mask 1-31 */
#define	MCFINTC_INTFRCH		0x10		/* Interrupt force 32-63 */
#define	MCFINTC_INTFRCL		0x14		/* Interrupt force 1-31 */
#define	MCFINTC_IRLR		0x18		/* */
#define	MCFINTC_IACKL		0x19		/* */
#define	MCFINTC_ICR0		0x40		/* Base ICR register */

/* INTC0 - interrupt numbers */
#define	MCFINT_VECBASE		64		/* Vector base number */
#define	MCFINT_EPF4		4		/* EPORT4 */
#define	MCFINT_EPF5		5		/* EPORT5 */
#define	MCFINT_EPF6		6		/* EPORT6 */
#define	MCFINT_EPF7		7		/* EPORT7 */
#define	MCFINT_UART0		13		/* UART0 */
#define	MCFINT_QSPI		18		/* QSPI */
#define	MCFINT_PIT1		36		/* PIT1 */
#define	MCFINT_PER_INTC		64

/* INTC1 - interrupt numbers */
#define	MCFINT_INTC1_VECBASE	(MCFINT_VECBASE + MCFINT_PER_INTC)
#define	MCFINT_TC0F		27		/* eTPU Channel 0 */
#define	MCFINT_TC1F		28		/* eTPU Channel 1 */
#define	MCFINT_TC2F		29		/* eTPU Channel 2 */
#define	MCFINT_TC3F		30		/* eTPU Channel 3 */
#define	MCFINT_TC4F		31		/* eTPU Channel 4 */
#define	MCFINT_TC5F		32		/* eTPU Channel 5 */
#define	MCFINT_TC6F		33		/* eTPU Channel 6 */
#define	MCFINT_TC7F		34		/* eTPU Channel 7 */
#define	MCFINT_TC8F		35		/* eTPU Channel 8 */
#define	MCFINT_TC9F		36		/* eTPU Channel 9 */
#define	MCFINT_TC10F		37		/* eTPU Channel 10 */
#define	MCFINT_TC11F		38		/* eTPU Channel 11 */
#define	MCFINT_TC12F		39		/* eTPU Channel 12 */
#define	MCFINT_TC13F		40		/* eTPU Channel 13 */
#define	MCFINT_TC14F		41		/* eTPU Channel 14 */
#define	MCFINT_TC15F		42		/* eTPU Channel 15 */

/*
 *	SDRAM configuration registers.
 */
#define	MCFSIM_DCR		0x44		/* SDRAM control */
#define	MCFSIM_DACR0		0x48		/* SDRAM base address 0 */
#define	MCFSIM_DMR0		0x4c		/* SDRAM address mask 0 */
#define	MCFSIM_DACR1		0x50		/* SDRAM base address 1 */
#define	MCFSIM_DMR1		0x54		/* SDRAM address mask 1 */

/*
 *	GPIO Registers and Pin Assignments
 */
#define	MCF_GPIO_PAR_FECI2C		0x100047 /* FEC Pin Assignment reg */
#define	MCF523x_GPIO_PAR_UART		0x100048 /* UART Pin Assignment reg */
#define	MCF523x_GPIO_PAR_QSPI		0x10004a /* QSPI Pin Assignment reg */
#define	MCF523x_GPIO_PAR_TIMER		0x10004c /* TIMER Pin Assignment reg */
#define	MCF523x_GPIO_PDDR_QSPI		0x10001a /* QSPI Pin Direction reg */
#define	MCF523x_GPIO_PDDR_TIMER		0x10001b /* TIMER Pin Direction reg */
#define	MCF523x_GPIO_PPDSDR_QSPI	0x10002a /* QSPI Pin Data reg */
#define	MCF523x_GPIO_PPDSDR_TIMER	0x10002b /* TIMER Pin Data reg */

#define	MCF_GPIO_PAR_FECI2C_PAR_SDA(x)	(((x) & 0x03) << 0)
#define	MCF_GPIO_PAR_FECI2C_PAR_SCL(x)	(((x) & 0x03) << 2)

/*
 *	eTPU Registers
 */
#define	MCF523x_ETPU		0x1d0000	/* eTPU Base */
#define	MCF523x_ETPU_CIOSR	0x00220		/* eTPU Intr Overflow Status */
#define	MCF523x_ETPU_CIER	0x00240		/* eTPU Intr Enable */
#define	MCF523x_ETPU_CR(c)	(0x00400 + ((c) * 0x10)) /* eTPU c Config */
#define	MCF523x_ETPU_SCR(c)	(0x00404 + ((c) * 0x10)) /* eTPU c Status & Ctrl */
#define	MCF523x_ETPU_SDM	0x08000		/* eTPU Shared Data Memory */

/*
 *	WDOG registers
 */
#define	MCF523x_WCR		((volatile uint16_t *) (MCF_IPSBAR + 0x140000)) /* control register 16 bits */
#define	MCF523x_WMR		((volatile uint16_t *) (MCF_IPSBAR + 0x140002)) /* modulus status 16 bits */
#define	MCF523x_MCNTR		((volatile uint16_t *) (MCF_IPSBAR + 0x140004)) /* count register 16 bits */
#define	MCF523x_WSR		((volatile uint16_t *) (MCF_IPSBAR + 0x140006)) /* service register 16 bits */

/*
 *	Reset registers
 */
#define	MCF523x_RSR		((volatile uint8_t *) (MCF_IPSBAR + 0x110001)) /* reset reason codes */

/*
 *	WDOG bit level definitions and macros.
 */
#define	MCF523x_WCR_ENABLE_BIT	0x0001

#define	MCF523x_WCR_ENABLE	0x0001
#define	MCF523x_WCR_DISABLE	0x0000
#define	MCF523x_WCR_HALTEDSTOP	0x0002
#define	MCF523x_WCR_HALTEDRUN	0x0000
#define	MCF523x_WCR_DOZESTOP	0x0004
#define	MCF523x_WCR_DOZERUN	0x0000
#define	MCF523x_WCR_WAITSTOP	0x0008
#define	MCF523x_WCR_WAITRUN	0x0000

#define	MCF523x_WMR_DEFAULT_VALUE	0xffff

/*
 *	Inter-IC (I2C) Module
 *	Read/Write access macros for general use
 */
#define	MCF_I2C_I2ADR		((volatile u8 *) (MCF_IPSBAR + 0x0300)) /* Address */
#define	MCF_I2C_I2FDR		((volatile u8 *) (MCF_IPSBAR + 0x0304)) /* Freq Divider */
#define	MCF_I2C_I2CR		((volatile u8 *) (MCF_IPSBAR + 0x0308)) /* Control */
#define	MCF_I2C_I2SR		((volatile u8 *) (MCF_IPSBAR + 0x030C)) /* Status */
#define	MCF_I2C_I2DR		((volatile u8 *) (MCF_IPSBAR + 0x0310)) /* Data I/O */

/*
 *	Bit level definitions and macros
 */
#define	MCF_I2C_I2ADR_ADDR(x)	(((x) & 0x7F) << 0x01)
#define	MCF_I2C_I2FDR_IC(x)	((x) & 0x3F)

#define	MCF_I2C_I2CR_IEN	0x80	/* I2C enable */
#define	MCF_I2C_I2CR_IIEN	0x40	/* interrupt enable */
#define	MCF_I2C_I2CR_MSTA	0x20	/* master/slave mode */
#define	MCF_I2C_I2CR_MTX	0x10	/* transmit/receive mode */
#define	MCF_I2C_I2CR_TXAK	0x08	/* transmit acknowledge enable */
#define	MCF_I2C_I2CR_RSTA	0x04	/* repeat start */

#define	MCF_I2C_I2SR_ICF	0x80	/* data transfer bit */
#define	MCF_I2C_I2SR_IAAS	0x40	/* I2C addressed as a slave */
#define	MCF_I2C_I2SR_IBB	0x20	/* I2C bus busy */
#define	MCF_I2C_I2SR_IAL	0x10	/* aribitration lost */
#define	MCF_I2C_I2SR_SRW	0x04	/* slave read/write */
#define	MCF_I2C_I2SR_IIF	0x02	/* I2C interrupt */
#define	MCF_I2C_I2SR_RXAK	0x01	/* received acknowledge */

/*
 *	Edge Port (EPORT) Module
 */
#define	MCF523x_EPPAR		0x130000
#define	MCF523x_EPDDR		0x130002
#define	MCF523x_EPIER		0x130003
#define	MCF523x_EPDR		0x130004
#define	MCF523x_EPPDR		0x130005
#define	MCF523x_EPFR		0x130006

/*
 *	Chip Select (CS) Module
 */
#define	MCF523x_CSAR0		0x80
#define	MCF523x_CSAR3		0xA4
#define	MCF523x_CSMR3		0xA8

/*
 *	System Access Control Unit (SACU)
 */
#define	MCF523x_PACR1		0x25
#define	MCF523x_PACR2		0x26
#define	MCF523x_PACR3		0x27
#define	MCF523x_PACR4		0x28
#define	MCF523x_PACR5		0x2A
#define	MCF523x_PACR6		0x2B
#define	MCF523x_PACR7		0x2C
#define	MCF523x_PACR8		0x2E
#define	MCF523x_GPACR		0x30

/****************************************************************************/
#endif	/* m523xsim_h */
