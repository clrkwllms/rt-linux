#ifndef LINUX_KMEMCHECK_H
#define LINUX_KMEMCHECK_H

#include <linux/types.h>

#ifdef CONFIG_KMEMCHECK
extern int kmemcheck_enabled;

void kmemcheck_init(void);

int kmemcheck_show_addr(unsigned long address);
int kmemcheck_hide_addr(unsigned long address);
#else
#define kmemcheck_enabled 0

static inline void kmemcheck_init(void)
{
}
#endif /* CONFIG_KMEMCHECK */

#endif /* LINUX_KMEMCHECK_H */
