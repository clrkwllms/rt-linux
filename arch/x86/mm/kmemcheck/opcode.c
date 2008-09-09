#include <linux/types.h>

#include "opcode.h"

static bool opcode_is_prefix(uint8_t b)
{
	return
		/* Group 1 */
		b == 0xf0 || b == 0xf2 || b == 0xf3
		/* Group 2 */
		|| b == 0x2e || b == 0x36 || b == 0x3e || b == 0x26
		|| b == 0x64 || b == 0x65 || b == 0x2e || b == 0x3e
		/* Group 3 */
		|| b == 0x66
		/* Group 4 */
		|| b == 0x67;
}

static bool opcode_is_rex_prefix(uint8_t b)
{
	return (b & 0xf0) == 0x40;
}

/*
 * This is a VERY crude opcode decoder. We only need to find the size of the
 * load/store that caused our #PF and this should work for all the opcodes
 * that we care about. Moreover, the ones who invented this instruction set
 * should be shot.
 */
void kmemcheck_opcode_decode(const uint8_t *op,
	const uint8_t **rep_prefix, const uint8_t **rex_prefix,
	unsigned int *size)
{
	/* Default operand size */
	int operand_size_override = 4;

	*rep_prefix = NULL;

	/* prefixes */
	for (; opcode_is_prefix(*op); ++op) {
		if (*op == 0xf2 || *op == 0xf3)
			*rep_prefix = op;
		if (*op == 0x66)
			operand_size_override = 2;
	}

	*rex_prefix = NULL;

#ifdef CONFIG_X86_64
	/* REX prefix */
	if (opcode_is_rex_prefix(*op)) {
		*rex_prefix = op;

		if (*op & 0x08) {
			*size = 8;
			return;
		}

		++op;
	}
#endif

	/* escape opcode */
	if (*op == 0x0f) {
		++op;

		if (*op == 0xb6) {
			*size = 1;
			return;
		}

		if (*op == 0xb7) {
			*size = 2;
			return;
		}
	}

	*size = (*op & 1) ? operand_size_override : 1;
}

const uint8_t *kmemcheck_opcode_get_primary(const uint8_t *op)
{
	/* skip prefixes */
	while (opcode_is_prefix(*op))
		++op;
	if (opcode_is_rex_prefix(*op))
		++op;
	return op;
}

