#ifndef ARCH__X86__MM__KMEMCHECK__OPCODE_H
#define ARCH__X86__MM__KMEMCHECK__OPCODE_H

#include <linux/types.h>

void kmemcheck_opcode_decode(const uint8_t *op,
	const uint8_t **rep_pfx, const uint8_t **rex_pfx, unsigned int *size);
const uint8_t *kmemcheck_opcode_get_primary(const uint8_t *op);

#endif
