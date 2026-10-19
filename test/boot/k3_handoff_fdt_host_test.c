// SPDX-License-Identifier: GPL-2.0+
/* Standalone K3 FDT fixup test; link against the build's libfdt objects. */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <linux/libfdt.h>
#include <linux/string.h>
#include <linux/soc/ti/k3-handoff.h>

#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))

static struct k3_handoff_data test_handoff = {
	.host_id = 15,
	.rx_thread = 8,
	.tx_request_low = 14,
	.has_request_low = true,
};

int k3_handoff_get_linux(struct k3_handoff_data *handoff)
{
	*handoff = test_handoff;

	return 0;
}

#include "../../arch/arm/mach-k3/k3_handoff_fdt.c"

static int add_proxy(void *blob, const char *name, u32 phandle)
{
	int root = fdt_path_offset(blob, "/");
	int node = fdt_add_subnode(blob, root, name);

	assert(node >= 0);
	assert(!fdt_setprop_string(blob, node, "compatible",
				   "ti,am654-secure-proxy"));
	assert(!fdt_setprop_u32(blob, node, "#mbox-cells", 1));
	assert(!fdt_setprop_u32(blob, node, "phandle", phandle));

	return node;
}

static int add_sci(void *blob, const char *name, const char *compatible,
		   u32 rx_phandle, u32 tx_phandle)
{
	static const char names[] = "rx\0tx\0notify\0";
	fdt32_t mboxes[] = {
		cpu_to_fdt32(rx_phandle), cpu_to_fdt32(2),
		cpu_to_fdt32(tx_phandle), cpu_to_fdt32(3),
		cpu_to_fdt32(rx_phandle), cpu_to_fdt32(33),
	};
	int root = fdt_path_offset(blob, "/");
	int node = fdt_add_subnode(blob, root, name);

	assert(node >= 0);
	assert(!fdt_setprop_string(blob, node, "compatible", compatible));
	assert(!fdt_setprop(blob, node, "mbox-names", names, sizeof(names) - 1));
	assert(!fdt_setprop(blob, node, "mboxes", mboxes, sizeof(mboxes)));
	assert(!fdt_setprop(blob, node, "ti,secure-host", NULL, 0));
	assert(!fdt_setprop_u32(blob, node, "ti,host-id", 99));

	return node;
}

static void make_tree(void *blob, bool duplicate_sci)
{
	int node;

	assert(!fdt_create_empty_tree(blob, 4096));
	add_proxy(blob, "secure-proxy@1000", 1);
	add_proxy(blob, "secure-proxy@2000", 2);
	node = add_sci(blob, "system-controller@500", "ti,am654-sci", 1, 2);
	/* An ignored candidate must not replace the selected node's offsets. */
	assert(!fdt_setprop(blob, node, "mbox-names", "tx\0rx\0notify\0",
			    sizeof("tx\0rx\0notify\0") - 1));
	add_sci(blob, "system-controller@1000", "ti,am654-sci", 1, 1);
	node = fdt_add_subnode(blob, 0, "dm-sci");
	assert(node >= 0);
	assert(!fdt_setprop_string(blob, node, "compatible", "ti,j721e-dm-sci"));
	if (duplicate_sci)
		add_sci(blob, "system-controller@3000", "ti,am654-sci", 1, 1);
}

int main(void)
{
	char blob[4096];
	const fdt32_t *prop;
	const char *names;
	int sci, len;

	make_tree(blob, false);
	assert(!fdt_fixup_k3_sec_proxy_handoff(blob));
	sci = fdt_path_offset(blob, "/system-controller@1000");
	assert(sci >= 0);
	prop = fdt_getprop(blob, sci, "ti,host-id", &len);
	assert(prop && len == sizeof(*prop) && fdt32_to_cpu(*prop) == 15);
	prop = fdt_getprop(blob, sci, "mboxes", &len);
	assert(prop && len == 6 * sizeof(*prop));
	assert(fdt32_to_cpu(prop[0]) == 1 && fdt32_to_cpu(prop[1]) == 8);
	assert(fdt32_to_cpu(prop[2]) == 1 && fdt32_to_cpu(prop[3]) == 14);
	assert(fdt32_to_cpu(prop[4]) == 1 && fdt32_to_cpu(prop[5]) == 33);
	names = fdt_getprop(blob, sci, "mbox-names", &len);
	assert(names && len == sizeof("rx\0tx\0notify\0") - 1);
	assert(!memcmp(names, "rx\0tx\0notify\0", len));
	assert(fdt_getprop(blob, sci, "ti,secure-host", &len) && !len);
	sci = fdt_path_offset(blob, "/system-controller@500");
	prop = fdt_getprop(blob, sci, "ti,host-id", &len);
	assert(prop && fdt32_to_cpu(*prop) == 99);

	make_tree(blob, true);
	assert(fdt_fixup_k3_sec_proxy_handoff(blob) == -EEXIST);
	return 0;
}
