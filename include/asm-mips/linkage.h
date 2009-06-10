#ifndef __ASM_LINKAGE_H
#define __ASM_LINKAGE_H

#ifdef __ASSEMBLY__
#include <asm/asm.h>

/* FASTCALL stuff */
#define FASTCALL(x)	x
#define fastcall

#endif

#define __weak __attribute__((weak))

#endif
