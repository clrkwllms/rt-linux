#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/sysdev.h>

#include "rtl.h"
static unsigned int table_addr;

int ibm_rtl_write(u8 value)
{
	int ret = 0;
	void __iomem *rtl;

	/* only valid value is 0 and 1 */
	if (value > 1) {
		ret = -EINVAL;
		goto err_out;
	}

	rtl = ioremap(table_addr,RTL_TABLE_SIZE);

	if (!rtl) {
		printk ("could not map table\n");
		ret = -ENOMEM;
		goto err_out;
	}

	if (readb(rtl+RTL_STATE) != value) {
		if (value == 1)
			writeb(1,(rtl+RTL_CMD));
		else
			writeb(2,(rtl+RTL_CMD));

		/*write special command*/
		outb(bios_to_value(rtl, RTL_CMD_PORT_VALUE),
			bios_to_value(rtl, RTL_CMD_PORT_ADDR));

		while (readb(rtl+RTL_CMD)) {
			msleep(10);
		}

		if (readb(rtl+RTL_CMD_STATUS))
			ret = -EIO;
	}

	iounmap(rtl);
err_out:
	return ret;
}

static ssize_t rtl_show_version (struct sysdev_class * dev, char * buf)
{
	int ret;
	void  __iomem *rtl;

	rtl = ioremap(table_addr,RTL_TABLE_SIZE);

	if (!rtl) {
		ret = -ENOMEM;
		goto err_out;
	}
	ret = sprintf(buf, "%d\n",(int)readb(rtl+RTL_VERSION));

	iounmap(rtl);
err_out:
	return ret;
}

static ssize_t rtl_show_state (struct sysdev_class * dev, char * buf)
{
	int ret;
	void __iomem *rtl;

	rtl = ioremap(table_addr,RTL_TABLE_SIZE);

	if (!rtl) {
		printk ("could not map table\n");
		ret = -ENOMEM;
		goto err_out;
	}
	ret = sprintf(buf, "%d\n",readb(rtl+RTL_STATE));

	iounmap(rtl);
err_out:
	return ret;
}

static ssize_t rtl_set_state(struct sysdev_class * dev, const char * buf, size_t size)
{
	ssize_t ret;
	switch (buf[0]) {
	case '0':
		ret = ibm_rtl_write(0);
		break;
	case '1':
		ret = ibm_rtl_write(1);
		break;
	default:
		ret = -EINVAL;
	}
	if (ret >= 0)
		ret = size;

	return ret;
}

static struct sysdev_class class_rtl = {
	.name = "ibm_rtl",
};

static SYSDEV_CLASS_ATTR(version, S_IRUGO, rtl_show_version, NULL);
static SYSDEV_CLASS_ATTR(state, 0600, rtl_show_state, rtl_set_state);

static struct sysdev_class_attribute *rtl_attributes[] = {
	&attr_version,
	&attr_state,
	NULL
};


static int rtl_setup_sysfs(void) {
	int ret,i;
	ret = sysdev_class_register(&class_rtl);

	if (!ret) {
		for (i = 0; rtl_attributes[i]; i ++)
			sysdev_class_create_file(&class_rtl, rtl_attributes[i]);
	}
	return ret;
}

static void rtl_teardown_sysfs(void) {
	int i;
	for (i = 0; rtl_attributes[i]; i ++)
		sysdev_class_remove_file(&class_rtl, rtl_attributes[i]);
	sysdev_class_unregister(&class_rtl);
	return;;
}

/* only allow the modules to load if the _RTL_ table can be found*/
int init_module(void) {
	unsigned long ebda_addr,ebda_size;
	void __iomem *data, *d;
	int ret,i;

	/*get the address for the RTL table from the EBDA */
	ebda_addr = *(unsigned short *)phys_to_virt(0x40E);
	ebda_addr <<= 4;
	ebda_size = 64*1024;

	data = ioremap(ebda_addr,ebda_size);
	d = data;
	if (!data) {
		ret = -ENOMEM;
		goto exit;
	}

	for (i = 0 ; i < ebda_size/4; i ++) {
		unsigned int *tmp = (unsigned int *) data++;
		if (*tmp == RTL_MAGIC_IDENT) {
			table_addr = ebda_addr + i;
			ret = rtl_setup_sysfs();
			goto exit;
		}
	}

	ret = -ENODEV;

exit:
	iounmap(d);
	return ret;
}

void cleanup_module(void)
{
	rtl_teardown_sysfs();
}

MODULE_LICENSE("GPL");

