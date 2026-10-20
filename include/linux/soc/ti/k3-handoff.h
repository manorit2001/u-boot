/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __LINUX_SOC_TI_K3_HANDOFF_H
#define __LINUX_SOC_TI_K3_HANDOFF_H

#include <linux/types.h>

#define K3_HANDOFF_TAG_SEC_PROXY	0xfff000
#define K3_HANDOFF_CONSUMER_LINUX	6

struct k3_handoff_data {
	u32 host_id;
	u32 rx_thread;
	u32 tx_request;
	u32 tx_request_high;
	u32 tx_request_low;
	bool has_request;
	bool has_request_high;
	bool has_request_low;
};

int k3_handoff_get_consumer(const void *data, size_t size, u32 consumer,
			    struct k3_handoff_data *handoff);
int k3_handoff_get(struct k3_handoff_data *handoff);
int k3_handoff_get_linux(struct k3_handoff_data *handoff);

#endif
