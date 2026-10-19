// SPDX-License-Identifier: GPL-2.0+
/* Standalone: see doc/develop/firmware-handoff.rst. */
#include "../../lib/firmware_handoff.c"
#include <assert.h>
#include <string.h>

static void put32(u8 *p, u32 value)
{
	p[0] = value;
	p[1] = value >> 8;
	p[2] = value >> 16;
	p[3] = value >> 24;
}

static void finish(u8 *data, u32 used)
{
	u8 checksum = 0;

	data[4] = 0;
	put32(data + 8, used);
	for (u32 i = 0; i < used; i++)
		checksum += data[i];
	data[4] = -checksum;
}

int main(void)
{
	_Alignas(8) u8 data[64];
	struct firmware_handoff_entry entry;
	size_t available = 1;

	assert(!firmware_handoff_get(&available) && !available);
	for (u32 len = 1; len <= 9; len++) {
		u32 next = (32 + len + 7) & ~7U;

		memset(data, 0, sizeof(data));
		put32(data, 0x4a0fb10b);
		data[5] = 2;
		data[6] = 24;
		data[7] = 3;
		put32(data + 12, sizeof(data));
		put32(data + 16, 1);
		put32(data + 24, (8U << 24) | 2);
		put32(data + 28, len);
		finish(data, 32 + len);
		assert(!firmware_handoff_validate(data, sizeof(data)));
		assert(firmware_handoff_validate(data, sizeof(data) - 1));
		assert(!firmware_handoff_get_entry(data, sizeof(data), 2, 0, &entry));
		assert(entry.data == data + 32 && entry.size == len);
		assert(firmware_handoff_get_entry(data, sizeof(data), 2, 1, &entry) == -ENOENT);
		put32(data + next, (8U << 24) | 3);
		put32(data + next + 4, 1);
		finish(data, next + 9);
		assert(!firmware_handoff_validate(data, sizeof(data)));
		assert(!firmware_handoff_get_entry(data, sizeof(data), 3, 0, &entry));
		assert(entry.data == data + next + 8 && entry.size == 1);
		finish(data, (next + 9 + 7) & ~7U);
		assert(!firmware_handoff_validate(data, sizeof(data)));
		data[4]++;
		assert(firmware_handoff_validate(data, sizeof(data)) == -EBADMSG);
	}
	return 0;
}
