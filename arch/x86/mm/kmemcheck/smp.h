#ifndef ARCH__X86__MM__KMEMCHECK__SMP_H
#define ARCH__X86__MM__KMEMCHECK__SMP_H

#ifdef CONFIG_KMEMCHECK_USE_SMP
void kmemcheck_smp_init(void);

void kmemcheck_pause_allbutself(void);
void kmemcheck_resume(void);
#else
static inline void kmemcheck_smp_init(void)
{
}

static inline void kmemcheck_pause_allbutself(void)
{
}

static inline void kmemcheck_resume(void)
{
}
#endif

#endif
