/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __FIRMWARE_HANDOFF_H
#define __FIRMWARE_HANDOFF_H

#include <linux/types.h>

struct firmware_handoff_entry {
	u32 tag;
	const void *data;
	u32 size;
};

/* The platform returns a transfer list and its independently known bound. */
const void *plat_get_transfer_list(size_t *available);
const void *firmware_handoff_get(size_t *available);

int firmware_handoff_validate(const void *data, size_t available);
int firmware_handoff_get_entry(const void *data, size_t available, u32 tag,
			       size_t index,
			       struct firmware_handoff_entry *entry);

#endif
