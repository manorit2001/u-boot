// SPDX-License-Identifier: GPL-2.0+
/* K3 secure-proxy handoff payload support. */

#include <errno.h>
#include <firmware_handoff.h>
#include <linux/soc/ti/k3-handoff.h>

#define K3_HANDOFF_MAX_TX	3

#ifndef K3_HANDOFF_PARSER_ONLY
#include "k3_handoff_data.h"

const void *plat_get_transfer_list(size_t *available)
{
	if (!available)
		return NULL;
	*available = K3_HANDOFF_DATA_SIZE;

	return k3_handoff_data;
}
#endif

static u32 k3_handoff_le32(const u8 *p)
{
	return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

static int k3_handoff_parse_proxy(const u8 *p, u32 size, u32 wanted,
				  struct k3_handoff_data *out)
{
	u32 count, seen_roles = 0, threads[K3_HANDOFF_MAX_TX], i, j;
	bool selected;

	if (size < 16)
		return -EINVAL;
	if (k3_handoff_le32(p + 4) > 255 ||
	    k3_handoff_le32(p + 8) > 65535)
		return -ERANGE;
	count = k3_handoff_le32(p + 12);
	if (!count || count > K3_HANDOFF_MAX_TX || size != 16 + count * 8)
		return -EINVAL;

	selected = k3_handoff_le32(p) == wanted;
	if (selected) {
		out->host_id = k3_handoff_le32(p + 4);
		out->rx_thread = k3_handoff_le32(p + 8);
	}
	for (i = 0; i < count; i++) {
		u32 role = k3_handoff_le32(p + 16 + i * 8);
		u32 thread = k3_handoff_le32(p + 20 + i * 8);

		if (thread > 65535 || role > 2 || (seen_roles & (1U << role)))
			return -EINVAL;
		if (thread == k3_handoff_le32(p + 8))
			return -EINVAL;
		for (j = 0; j < i; j++)
			if (threads[j] == thread)
				return -EEXIST;
		threads[i] = thread;
		seen_roles |= 1U << role;
		if (!selected)
			continue;
		switch (role) {
		case 0:
			out->tx_request = thread;
			out->has_request = true;
			break;
		case 1:
			out->tx_request_high = thread;
			out->has_request_high = true;
			break;
		case 2:
			out->tx_request_low = thread;
			out->has_request_low = true;
			break;
		}
	}
	if (seen_roles != 1 && seen_roles != 6)
		return -EINVAL;

	return 0;
}

int k3_handoff_get_consumer(const void *data, size_t size, u32 consumer,
			    struct k3_handoff_data *handoff)
{
	struct k3_handoff_data result = {};
	size_t index;
	bool found = false;
	u32 consumers = 0;

	if (!data || !handoff || size < 24 || consumer < 1 || consumer > 6 ||
	    ((const u8 *)data)[6] != 24 || ((const u8 *)data)[7] != 3 ||
	    k3_handoff_le32((const u8 *)data + 16) != 1)
		return -EINVAL;
	for (index = 0; ; index++) {
		struct firmware_handoff_entry entry;
		struct k3_handoff_data parsed = {};
		u32 id;
		int ret;

		ret = firmware_handoff_get_entry(data, size,
						 K3_HANDOFF_TAG_SEC_PROXY, index,
						 &entry);
		if (ret == -ENOENT)
			break;
		if (ret)
			return ret;
		if (entry.size < sizeof(u32))
			return -EINVAL;
		id = k3_handoff_le32(entry.data);
		if (!id)
			return -EINVAL;
		if (id > 6)
			continue;
		if (consumers & (1U << id))
			return -EEXIST;
		consumers |= 1U << id;
		ret = k3_handoff_parse_proxy(entry.data, entry.size, consumer,
					     &parsed);
		if (ret)
			return ret;
		if (id == consumer) {
			result = parsed;
			found = true;
		}
	}
	if (!found)
		return -ENOENT;
	*handoff = result;

	return 0;
}

#ifndef K3_HANDOFF_PARSER_ONLY
static int k3_handoff_get_for_consumer(u32 consumer,
				       struct k3_handoff_data *handoff)
{
	size_t available;
	const void *data = firmware_handoff_get(&available);

	if (!data)
		return -EINVAL;

	return k3_handoff_get_consumer(data, available, consumer, handoff);
}

int k3_handoff_get(struct k3_handoff_data *handoff)
{
	if (IS_ENABLED(CONFIG_CPU_V7R))
		return k3_handoff_get_for_consumer(1, handoff);
	if (IS_ENABLED(CONFIG_SPL_BUILD))
		return k3_handoff_get_for_consumer(4, handoff);
	return k3_handoff_get_for_consumer(5, handoff);
}

int k3_handoff_get_linux(struct k3_handoff_data *handoff)
{
	return k3_handoff_get_for_consumer(K3_HANDOFF_CONSUMER_LINUX, handoff);
}
#endif
