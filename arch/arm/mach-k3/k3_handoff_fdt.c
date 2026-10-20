// SPDX-License-Identifier: GPL-2.0+

#include <errno.h>
#include <linux/kernel.h>
#include <linux/libfdt.h>
#include <linux/soc/ti/k3-handoff.h>

#if CONFIG_IS_ENABLED(K3_SEC_PROXY_HANDOFF) && !defined(CONFIG_SPL_BUILD)
static int fdt_get_k3_mbox_spec(const void *blob, int sci, int index,
				int *provider, u32 *thread_offset)
{
	const fdt32_t *mboxes, *cells;
	size_t mbox_cells, cell;
	int len, entry = 0;

	mboxes = fdt_getprop(blob, sci, "mboxes", &len);
	if (!mboxes || len < 0 || len % sizeof(*mboxes))
		return -EINVAL;
	mbox_cells = len / sizeof(*mboxes);

	for (cell = 0; cell < mbox_cells;) {
		u32 count;
		int provider_node;

		provider_node = fdt_node_offset_by_phandle(blob,
							   fdt32_to_cpu(mboxes[cell]));
		if (provider_node < 0)
			return provider_node;
		cells = fdt_getprop(blob, provider_node, "#mbox-cells", &len);
		if (!cells || len != sizeof(*cells))
			return -EINVAL;
		count = fdt32_to_cpu(*cells);
		if (count > mbox_cells - cell - 1)
			return -EINVAL;
		if (entry++ == index) {
			if (count != 1)
				return -EINVAL;
			*provider = provider_node;
			*thread_offset = (cell + 1) * sizeof(*mboxes);
			return 0;
		}
		cell += count + 1;
	}

	return -ENOENT;
}

static int fdt_fixup_k3_mbox_thread(void *blob, int sci, u32 thread,
				    u32 thread_offset)
{
	fdt32_t value = cpu_to_fdt32(thread);

	return fdt_setprop_inplace_namelen_partial(blob, sci, "mboxes",
						    sizeof("mboxes") - 1,
						    thread_offset, &value,
						    sizeof(value));
}
#endif

int fdt_fixup_k3_sec_proxy_handoff(void *blob)
{
#if CONFIG_IS_ENABLED(K3_SEC_PROXY_HANDOFF) && !defined(CONFIG_SPL_BUILD)
	static const char * const compatibles[] = {
		"ti,k2g-sci", "ti,am654-sci"
	};
	struct k3_handoff_data handoff;
	u32 tx_thread, tx_offset = 0, rx_offset = 0;
	size_t i;
	int sci = -FDT_ERR_NOTFOUND, tx_provider, rx_provider, tx, rx, ret;

	ret = k3_handoff_get_linux(&handoff);
	if (ret)
		return ret;
	for (i = 0; i < ARRAY_SIZE(compatibles); i++) {
		int node = -1;

		while ((node = fdt_node_offset_by_compatible(blob, node,
							     compatibles[i])) >= 0) {
			u32 candidate_tx_offset, candidate_rx_offset;

			if (node == sci)
				continue;
			tx = fdt_stringlist_search(blob, node, "mbox-names", "tx");
			rx = fdt_stringlist_search(blob, node, "mbox-names", "rx");
			if (tx < 0 || rx < 0)
				return -EINVAL;
			ret = fdt_get_k3_mbox_spec(blob, node, tx, &tx_provider,
						   &candidate_tx_offset);
			if (ret)
				return ret;
			ret = fdt_get_k3_mbox_spec(blob, node, rx, &rx_provider,
						   &candidate_rx_offset);
			if (ret)
				return ret;
			ret = fdt_node_check_compatible(blob, tx_provider, "ti,am654-secure-proxy");
			if (tx_provider != rx_provider || ret)
				continue;
			if (sci >= 0)
				return -EEXIST;
			sci = node;
			tx_offset = candidate_tx_offset;
			rx_offset = candidate_rx_offset;
		}
	}
	if (sci < 0)
		return -ENOENT;
	if (handoff.has_request)
		tx_thread = handoff.tx_request;
	else if (handoff.has_request_low)
		tx_thread = handoff.tx_request_low;
	else
		return -EINVAL;
	ret = fdt_fixup_k3_mbox_thread(blob, sci, tx_thread, tx_offset);
	if (ret)
		return ret;
	ret = fdt_fixup_k3_mbox_thread(blob, sci, handoff.rx_thread, rx_offset);
	if (ret)
		return ret;
	/* ti,secure-host controls message framing and is deliberately preserved. */
	return fdt_setprop_u32(blob, sci, "ti,host-id", handoff.host_id);
#else
	return 0;
#endif
}
