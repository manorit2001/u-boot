// SPDX-License-Identifier: GPL-2.0+

#include <errno.h>
#include <linux/compiler_attributes.h>
#include <linux/types.h>
#include <firmware_handoff.h>

#define HANDOFF_SIGNATURE	0x4a0fb10b
#define HANDOFF_HEADER_SIZE	24
#define HANDOFF_ALIGNMENT	8

static u32 handoff_read_le32(const u8 *p)
{
	return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

int firmware_handoff_validate(const void *data, size_t available)
{
	const u8 *buf = data;
	u32 used, total;
	size_t off;
	u8 checksum = 0;

	if (!buf || ((unsigned long)buf & (HANDOFF_ALIGNMENT - 1)) ||
	    available < HANDOFF_HEADER_SIZE)
		return -EINVAL;
	used = handoff_read_le32(buf + 8);
	total = handoff_read_le32(buf + 12);
	if (handoff_read_le32(buf) != HANDOFF_SIGNATURE || buf[5] != 2 ||
	    buf[6] < HANDOFF_HEADER_SIZE || buf[7] < 3 ||
	    buf[7] >= sizeof(size_t) * 8 ||
	    used < buf[6] || used > total || total > available ||
	    used % HANDOFF_ALIGNMENT || total % HANDOFF_ALIGNMENT ||
	    (handoff_read_le32(buf + 16) & ~1U) ||
	    handoff_read_le32(buf + 20))
		return -EINVAL;
	if (handoff_read_le32(buf + 16) & 1U) {
		for (off = 0; off < used; off++)
			checksum += buf[off];
		if (checksum)
			return -EBADMSG;
	}

	off = (buf[6] + HANDOFF_ALIGNMENT - 1) &
	      ~(size_t)(HANDOFF_ALIGNMENT - 1);
	while (off < used) {
		const u8 *te = buf + off;
		size_t entry_size, padding, remaining = used - off;
		u32 data_size;
		u8 header_size;

		if (remaining < 8)
			return -EINVAL;
		header_size = te[3];
		if (header_size < 8 || header_size > remaining)
			return -EINVAL;
		data_size = handoff_read_le32(te + 4);
		if (data_size > remaining - header_size)
			return -EINVAL;
		entry_size = header_size + (size_t)data_size;
		padding = (HANDOFF_ALIGNMENT - (entry_size & (HANDOFF_ALIGNMENT - 1))) &
			  (HANDOFF_ALIGNMENT - 1);
		if (padding > remaining - entry_size)
			return -EINVAL;
		off += entry_size + padding;
	}

	return 0;
}

int firmware_handoff_get_entry(const void *data, size_t available, u32 tag,
			       size_t index,
			       struct firmware_handoff_entry *entry)
{
	const u8 *buf = data;
	size_t off, used;
	int ret;

	if (!entry)
		return -EINVAL;
	ret = firmware_handoff_validate(data, available);
	if (ret)
		return ret;
	used = handoff_read_le32(buf + 8);
	off = (buf[6] + HANDOFF_ALIGNMENT - 1) &
	      ~(size_t)(HANDOFF_ALIGNMENT - 1);
	while (off < used) {
		const u8 *te = buf + off;
		u8 header_size = te[3];
		u32 data_size = handoff_read_le32(te + 4);
		size_t entry_size = header_size + (size_t)data_size;
		size_t padding = (HANDOFF_ALIGNMENT -
				  (entry_size & (HANDOFF_ALIGNMENT - 1))) &
				 (HANDOFF_ALIGNMENT - 1);

		if ((handoff_read_le32(te) & 0xffffff) == tag) {
			if (!index--) {
				entry->tag = tag;
				entry->data = te + header_size;
				entry->size = data_size;
				return 0;
			}
		}
		off += entry_size + padding;
	}

	return -ENOENT;
}

const void *__weak plat_get_transfer_list(size_t *available)
{
	if (available)
		*available = 0;

	return NULL;
}

const void *firmware_handoff_get(size_t *available)
{
	const void *data;
	size_t size = 0;

	if (!available)
		return NULL;
	*available = 0;
	data = plat_get_transfer_list(&size);
	if (!data || firmware_handoff_validate(data, size))
		return NULL;
	*available = size;

	return data;
}
