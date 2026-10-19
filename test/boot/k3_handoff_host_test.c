// SPDX-License-Identifier: GPL-2.0+
/* Standalone parser and container validation test. */
#define K3_HANDOFF_PARSER_ONLY
#include "../../lib/firmware_handoff.c"
#include "../../arch/arm/mach-k3/k3_handoff.c"

#include <assert.h>
#include <string.h>

#define TEST_TL_SIGNATURE	0x4a0fb10b
#define TEST_TL_HEADER_SIZE	24

static void put32(u8 *p, u32 value)
{
	p[0] = value;
	p[1] = value >> 8;
	p[2] = value >> 16;
	p[3] = value >> 24;
}

static void fix_checksum(u8 *data, u32 used)
{
	u8 sum = 0;
	u32 i;

	data[4] = 0;
	for (i = 0; i < used; i++)
		sum += data[i];
	data[4] = -sum;
}

static void make_list(u8 *data, u32 duplicate)
{
	u32 used = 56 + duplicate * 32;
	u32 i;

	memset(data, 0, 96);
	put32(data, TEST_TL_SIGNATURE);
	data[5] = 2;
	data[6] = TEST_TL_HEADER_SIZE;
	data[7] = 3;
	put32(data + 8, used);
	put32(data + 12, 96);
	put32(data + 16, 1);
	for (i = 0; i <= duplicate; i++) {
		u8 *te = data + 24 + i * 32;

		put32(te, (8U << 24) | K3_HANDOFF_TAG_SEC_PROXY);
		put32(te + 4, 24);
		put32(te + 8, 5);
		put32(te + 12, 14);
		put32(te + 16, 12);
		put32(te + 20, 1);
		put32(te + 24, 0);
		put32(te + 28, 11);
	}
	fix_checksum(data, used);
}

static void make_priority_list(u8 *data, u32 rx, u32 high, u32 low)
{
	memset(data, 0, 96);
	put32(data, TEST_TL_SIGNATURE);
	data[5] = 2;
	data[6] = TEST_TL_HEADER_SIZE;
	data[7] = 3;
	put32(data + 8, 64);
	put32(data + 12, 96);
	put32(data + 16, 1);
	put32(data + 24, (8U << 24) | K3_HANDOFF_TAG_SEC_PROXY);
	put32(data + 28, 32);
	put32(data + 32, 5);
	put32(data + 36, 14);
	put32(data + 40, rx);
	put32(data + 44, 2);
	put32(data + 48, 1);
	put32(data + 52, high);
	put32(data + 56, 2);
	put32(data + 60, low);
	fix_checksum(data, 64);
}

static void make_unknown_consumer_list(u8 *data)
{
	make_list(data, 1);
	put32(data + 32, 99);
	fix_checksum(data, 88);
}

static void make_unknown_tag_list(u8 *data)
{
	make_list(data, 0);
	memmove(data + 32, data + 24, 32);
	put32(data + 24, (8U << 24) | 0xabcdef);
	put32(data + 28, 0);
	put32(data + 8, 64);
	fix_checksum(data, 64);
}

int main(void)
{
	u8 data[96];
	struct k3_handoff_data handoff;
	struct firmware_handoff_entry entry;
	size_t available = 99;

	assert(!firmware_handoff_get(&available) && !available);
	make_list(data, 0);
	assert(firmware_handoff_validate(data, sizeof(data)) == 0);
	assert(firmware_handoff_get_entry(data, sizeof(data),
					  K3_HANDOFF_TAG_SEC_PROXY, 0,
					  &entry) == 0);
	assert(entry.size == 24);
	assert(k3_handoff_get_consumer(data, sizeof(data), 5, &handoff) == 0);
	assert(handoff.host_id == 14 && handoff.rx_thread == 12);
	assert(handoff.has_request && handoff.tx_request == 11);
	assert(k3_handoff_get_consumer(data, 23, 5, &handoff) == -EINVAL);
	data[30] ^= 1;
	assert(k3_handoff_get_consumer(data, sizeof(data), 5, &handoff) == -EBADMSG);

	make_list(data, 0);
	put32(data + 16, 0);
	data[7] = 4;
	assert(firmware_handoff_validate(data, sizeof(data)) == 0);
	assert(k3_handoff_get_consumer(data, sizeof(data), 5, &handoff) == -EINVAL);

	make_list(data, 0);
	put32(data + 32, 0);
	fix_checksum(data, 56);
	assert(k3_handoff_get_consumer(data, sizeof(data), 5, &handoff) == -EINVAL);

	make_list(data, 0);
	put32(data + 12, 0xffffffff);
	assert(k3_handoff_get_consumer(data, sizeof(data), 5, &handoff) == -EINVAL);

	make_list(data, 0);
	put32(data + 28, 0xffffffff);
	fix_checksum(data, 56);
	assert(k3_handoff_get_consumer(data, sizeof(data), 5, &handoff) == -EINVAL);

	make_list(data, 1);
	handoff.host_id = 99;
	assert(k3_handoff_get_consumer(data, sizeof(data), 5, &handoff) == -EEXIST);
	assert(handoff.host_id == 99);

	make_unknown_tag_list(data);
	assert(k3_handoff_get_consumer(data, sizeof(data), 5, &handoff) == 0);

	make_unknown_consumer_list(data);
	assert(k3_handoff_get_consumer(data, sizeof(data), 5, &handoff) == 0);

	make_priority_list(data, 3, 7, 8);
	assert(k3_handoff_get_consumer(data, sizeof(data), 5, &handoff) == 0);
	assert(handoff.has_request_high && handoff.has_request_low);
	make_priority_list(data, 7, 7, 8);
	assert(k3_handoff_get_consumer(data, sizeof(data), 5, &handoff) == -EINVAL);
	make_priority_list(data, 3, 7, 7);
	assert(k3_handoff_get_consumer(data, sizeof(data), 5, &handoff) == -EEXIST);
	return 0;
}
