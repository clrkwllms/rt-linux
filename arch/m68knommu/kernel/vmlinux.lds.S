/*
 *	vmlinux.lds.S -- master linker script for m68knommu arch
 *
 *	(C) Copyright 2002-2006, Greg Ungerer <gerg@snapgear.com>
 *
 *	This linker script is equiped to build either ROM loaded or RAM
 *	run kernels.
 */

#define	OUTPUT_DATA_SECTION > DATA

#include <asm-generic/vmlinux.lds.h>

#if defined(CONFIG_RAMKERNEL)
#define	RAM_START	CONFIG_KERNELBASE
#define	RAM_LENGTH	(CONFIG_RAMBASE + CONFIG_RAMSIZE - CONFIG_KERNELBASE)
#define	TEXT		ram
#define	DATA		ram
#define	INIT		ram
#define	BSS		ram
#endif
#if defined(CONFIG_ROMKERNEL) || defined(CONFIG_HIMEMKERNEL)
#define	RAM_START	CONFIG_RAMBASE
#define	RAM_LENGTH	CONFIG_RAMSIZE
#define	ROMVEC_START	CONFIG_ROMVEC
#define	ROMVEC_LENGTH	CONFIG_ROMVECSIZE
#define	ROM_START	CONFIG_ROMSTART
#define	ROM_LENGTH	CONFIG_ROMSIZE
#define	TEXT		rom
#define	DATA		ram
#define	INIT		ram
#define	BSS		ram
#endif

#ifndef DATA_ADDR
#define	DATA_ADDR
#endif

OUTPUT_ARCH(m68k)
ENTRY(_start)

MEMORY {
	ram	: ORIGIN = RAM_START, LENGTH = RAM_LENGTH
#ifdef ROM_START
	romvec	: ORIGIN = ROMVEC_START, LENGTH = ROMVEC_LENGTH
	rom	: ORIGIN = ROM_START, LENGTH = ROM_LENGTH
#endif
}

jiffies = jiffies_64 + 4;

SECTIONS {

#ifdef ROMVEC_START
	. = ROMVEC_START ;
	.romvec : {
		__rom_start = . ;
		_romvec = .;
		*(.data.initvect)
	} > romvec
#endif

	.text : {
		_text = .;
		_stext = . ;
		TEXT_TEXT
		SCHED_TEXT
		LOCK_TEXT
        	*(.text.lock)

		. = ALIGN(4) ;
	} > TEXT

	_etext = . ;

	RODATA

	.data DATA_ADDR : {
		. = ALIGN(4);
		_sdata = . ;
		DATA_DATA
		. = ALIGN(16);          /* Exception table              */
		__start___ex_table = .;
		*(__ex_table)
		__stop___ex_table = .;
		. = ALIGN(8192) ;
		*(.data.init_task)
		_edata = . ;
	} > DATA

	BUG_TABLE
	PERCPU(4096)

	.init : {
		. = ALIGN(4096);
		__init_begin = .;
		_sinittext = .;
		*(.init.text)
		_einittext = .;
		*(.init.data)
		. = ALIGN(16);
		__setup_start = .;
		*(.init.setup)
		__setup_end = .;
		__initcall_start = .;
		INITCALLS
		__initcall_end = .;
		__con_initcall_start = .;
		*(.con_initcall.init)
		__con_initcall_end = .;
		__security_initcall_start = .;
		*(.security_initcall.init)
		__security_initcall_end = .;
#ifdef CONFIG_BLK_DEV_INITRD
		. = ALIGN(4);
		__initramfs_start = .;
		*(.init.ramfs)
		__initramfs_end = .;
#endif
		. = ALIGN(4096);
		__init_end = .;
	} > INIT

	.bss : {
		. = ALIGN(4);
		_sbss = . ;
		*(.bss)
		*(COMMON)
		. = ALIGN(4) ;
		_ebss = . ;
	} > BSS

	_end = . ;

	/DISCARD/ : {
		*(.exit.text)
		*(.exit.data)
		*(.exitcall.exit)
	}
}
