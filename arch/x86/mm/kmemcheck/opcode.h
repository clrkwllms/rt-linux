#ifndef ARCH__X86__MM__KMEMCHECK__OPCODE_H
#define ARCH__X86__MM__KMEMCHECK__OPCODE_H

#include <linux/types.h>

unsigned int kmemcheck_opcode_get_size(const uint8_t *op);
const uint8_t *kmemcheck_opcode_get_primary(const uint8_t *op);

#endif
