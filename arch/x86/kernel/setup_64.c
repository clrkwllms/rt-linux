/*
 *  Copyright (C) 1995  Linus Torvalds
 */

/*
 * This file handles the architecture-dependent parts of initialization
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/user.h>
#include <linux/screen_info.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/initrd.h>
#include <linux/highmem.h>
#include <linux/bootmem.h>
#include <linux/module.h>
#include <asm/processor.h>
#include <linux/console.h>
#include <linux/seq_file.h>
#include <linux/root_dev.h>
#include <linux/pci.h>
#include <asm/pci-direct.h>
#include <linux/efi.h>
#include <linux/acpi.h>
#include <linux/kallsyms.h>
#include <linux/edd.h>
#include <linux/iscsi_ibft.h>
#include <linux/mmzone.h>
#include <linux/kexec.h>
#include <linux/cpufreq.h>
#include <linux/dmi.h>
#include <linux/dma-mapping.h>
#include <linux/ctype.h>
#include <linux/uaccess.h>
#include <linux/init_ohci1394_dma.h>
#include <linux/kvm_para.h>

#include <asm/mtrr.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/vsyscall.h>
#include <asm/io.h>
#include <asm/smp.h>
#include <asm/desc.h>
#include <video/edid.h>
#include <asm/e820.h>
#include <asm/mpspec.h>
#include <asm/dma.h>
#include <asm/gart.h>
#include <asm/mpspec.h>
#include <asm/mmu_context.h>
#include <asm/proto.h>
#include <asm/setup.h>
#include <asm/numa.h>
#include <asm/sections.h>
#include <asm/dmi.h>
#include <asm/cacheflush.h>

#include <mach_apic.h>
#ifdef CONFIG_PARAVIRT
#include <asm/paravirt.h>
#else
#define ARCH_SETUP
#endif

/*
 * Machine setup..
 */

struct cpuinfo_x86 boot_cpu_data __read_mostly;
EXPORT_SYMBOL(boot_cpu_data);

unsigned long mmu_cr4_features;

/* Boot loader ID as an integer, for the benefit of proc_dointvec */
int bootloader_type;

unsigned long saved_video_mode;

/*
 * Early DMI memory
 */
int dmi_alloc_index;
char dmi_alloc_data[DMI_MAX_DATA];

/*
 * Setup options
 */
struct screen_info screen_info;
EXPORT_SYMBOL(screen_info);
struct sys_desc_table_struct {
	unsigned short length;
	unsigned char table[0];
};

struct edid_info edid_info;
EXPORT_SYMBOL_GPL(edid_info);

extern int root_mountflags;

static char __initdata command_line[COMMAND_LINE_SIZE];

#define IORESOURCE_RAM (IORESOURCE_BUSY | IORESOURCE_MEM)

static struct resource data_resource = {
	.name = "Kernel data",
	.start = 0,
	.end = 0,
	.flags = IORESOURCE_RAM,
};
static struct resource code_resource = {
	.name = "Kernel code",
	.start = 0,
	.end = 0,
	.flags = IORESOURCE_RAM,
};
static struct resource bss_resource = {
	.name = "Kernel bss",
	.start = 0,
	.end = 0,
	.flags = IORESOURCE_RAM,
};

#if defined(CONFIG_EDD) || defined(CONFIG_EDD_MODULE)
struct edd edd;
#ifdef CONFIG_EDD_MODULE
EXPORT_SYMBOL(edd);
#endif
/**
 * copy_edd() - Copy the BIOS EDD information
 *              from boot_params into a safe place.
 *
 */
static inline void copy_edd(void)
{
     memcpy(edd.mbr_signature, boot_params.edd_mbr_sig_buffer,
	    sizeof(edd.mbr_signature));
     memcpy(edd.edd_info, boot_params.eddbuf, sizeof(edd.edd_info));
     edd.mbr_signature_nr = boot_params.edd_mbr_sig_buf_entries;
     edd.edd_info_nr = boot_params.eddbuf_entries;
}
#else
static inline void copy_edd(void)
{
}
#endif

static void __init reserve_initrd(void)
{
#ifdef CONFIG_BLK_DEV_INITRD
	if (boot_params.hdr.type_of_loader && boot_params.hdr.ramdisk_image) {
		unsigned long ramdisk_image = boot_params.hdr.ramdisk_image;
		unsigned long ramdisk_size  = boot_params.hdr.ramdisk_size;
		unsigned long ramdisk_end   = ramdisk_image + ramdisk_size;
		unsigned long end_of_mem    = max_pfn << PAGE_SHIFT;

		if (ramdisk_end <= end_of_mem) {
			/*
			 * don't need to reserve again, already reserved early
			 * in x86_64_start_kernel, and early_res_to_bootmem
			 * will convert that to reserved in bootmem
			 */
			initrd_start = ramdisk_image + PAGE_OFFSET;
			initrd_end = initrd_start+ramdisk_size;
		} else {
			free_early(ramdisk_image, ramdisk_end);
			printk(KERN_ERR "initrd extends beyond end of memory "
			       "(0x%08lx > 0x%08lx)\ndisabling initrd\n",
			       ramdisk_end, end_of_mem);
			initrd_start = 0;
		}
	}
#endif
}
/*
 * setup_arch - architecture-specific boot-time initializations
 *
 * Note: On x86_64, fixmaps are ready for use even before this is called.
 */
void __init setup_arch(char **cmdline_p)
{
	printk(KERN_INFO "Command line: %s\n", boot_command_line);

	ROOT_DEV = old_decode_dev(boot_params.hdr.root_dev);
	screen_info = boot_params.screen_info;
	edid_info = boot_params.edid_info;
	saved_video_mode = boot_params.hdr.vid_mode;
	bootloader_type = boot_params.hdr.type_of_loader;

#ifdef CONFIG_BLK_DEV_RAM
	rd_image_start = boot_params.hdr.ram_size & RAMDISK_IMAGE_START_MASK;
	rd_prompt = ((boot_params.hdr.ram_size & RAMDISK_PROMPT_FLAG) != 0);
	rd_doload = ((boot_params.hdr.ram_size & RAMDISK_LOAD_FLAG) != 0);
#endif
#ifdef CONFIG_EFI
	if (!strncmp((char *)&boot_params.efi_info.efi_loader_signature,
		     "EL64", 4)) {
		efi_enabled = 1;
		efi_reserve_early();
	}
#endif

	ARCH_SETUP

	setup_memory_map();
	copy_edd();

	if (!boot_params.hdr.root_flags)
		root_mountflags &= ~MS_RDONLY;
	init_mm.start_code = (unsigned long) &_text;
	init_mm.end_code = (unsigned long) &_etext;
	init_mm.end_data = (unsigned long) &_edata;
	init_mm.brk = (unsigned long) &_end;

	code_resource.start = virt_to_phys(&_text);
	code_resource.end = virt_to_phys(&_etext)-1;
	data_resource.start = virt_to_phys(&_etext);
	data_resource.end = virt_to_phys(&_edata)-1;
	bss_resource.start = virt_to_phys(&__bss_start);
	bss_resource.end = virt_to_phys(&__bss_stop)-1;

	early_cpu_init();

	strlcpy(command_line, boot_command_line, COMMAND_LINE_SIZE);
	*cmdline_p = command_line;

	parse_setup_data();

	parse_early_param();

	if (acpi_mps_check()) {
		disable_apic = 1;
		clear_cpu_cap(&boot_cpu_data, X86_FEATURE_APIC);
	}

#ifdef CONFIG_PROVIDE_OHCI1394_DMA_INIT
	if (init_ohci1394_dma_early)
		init_ohci1394_dma_on_all_controllers();
#endif

	finish_e820_parsing();

	/* after parse_early_param, so could debug it */
	insert_resource(&iomem_resource, &code_resource);
	insert_resource(&iomem_resource, &data_resource);
	insert_resource(&iomem_resource, &bss_resource);

	if (efi_enabled)
		efi_init();

	early_gart_iommu_check();

	e820_register_active_regions(0, 0, -1UL);
	/*
	 * partially used pages are not usable - thus
	 * we are rounding upwards:
	 */
	max_pfn = e820_end_of_ram();

	/* pre allocte 4k for mptable mpc */
	early_reserve_e820_mpc_new();
	/* update e820 for memory not covered by WB MTRRs */
	mtrr_bp_init();
	if (mtrr_trim_uncached_memory(max_pfn)) {
		remove_all_active_ranges();
		e820_register_active_regions(0, 0, -1UL);
		max_pfn = e820_end_of_ram();
	}

	reserve_initrd();

	num_physpages = max_pfn;

	check_efer();

	max_pfn_mapped = init_memory_mapping(0, (max_pfn << PAGE_SHIFT));

	vsmp_init();

	dmi_scan_machine();

	io_delay_init();

	/*
	 * Initialize the ACPI boot-time table parser (gets the RSDP and SDT).
	 * Call this early for SRAT node setup.
	 */
	acpi_boot_table_init();

	/* How many end-of-memory variables you have, grandma! */
	max_low_pfn = max_pfn;
	high_memory = (void *)__va(max_pfn * PAGE_SIZE - 1) + 1;

	/* Remove active ranges so rediscovery with NUMA-awareness happens */
	remove_all_active_ranges();

#ifdef CONFIG_ACPI_NUMA
	/*
	 * Parse SRAT to discover nodes.
	 */
	acpi_numa_init();
#endif

	initmem_init(0, max_pfn);

	dma32_reserve_bootmem();

#ifdef CONFIG_ACPI_SLEEP
	/*
	 * Reserve low memory region for sleep support.
	 */
       acpi_reserve_bootmem();
#endif

#ifdef CONFIG_X86_MPPARSE
       /*
	* Find and reserve possible boot-time SMP configuration:
	*/
	find_smp_config();
#endif
	reserve_crashkernel();

	reserve_ibft_region();

#ifdef CONFIG_KVM_CLOCK
	kvmclock_init();
#endif
	paging_init();
	map_vsyscall();

	early_quirks();

	/*
	 * Read APIC and some other early information from ACPI tables.
	 */
	acpi_boot_init();

	init_cpu_to_node();

#ifdef CONFIG_X86_MPPARSE
	/*
	 * get boot-time SMP configuration:
	 */
	if (smp_found_config)
		get_smp_config();
#endif
	init_apic_mappings();
	ioapic_init_mappings();

	kvm_guest_init();

	/*
	 * We trust e820 completely. No explicit ROM probing in memory.
	 */
	e820_reserve_resources();
	e820_mark_nosave_regions(max_pfn);

	reserve_standard_io_resources();

	e820_setup_gap();

#ifdef CONFIG_VT
#if defined(CONFIG_VGA_CONSOLE)
	if (!efi_enabled || (efi_mem_type(0xa0000) != EFI_CONVENTIONAL_MEMORY))
		conswitchp = &vga_con;
#elif defined(CONFIG_DUMMY_CONSOLE)
	conswitchp = &dummy_con;
#endif
#endif
}

