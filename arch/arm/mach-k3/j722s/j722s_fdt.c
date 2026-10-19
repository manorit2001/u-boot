// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2024 Texas Instruments Incorporated - https://www.ti.com/
 */

#include <asm/arch/k3-common-fdt.h>
#include <asm/hardware.h>
#include <fdt_support.h>

int ft_system_setup(void *blob, struct bd_info *bd)
{
	int ret = fdt_fixup_reserved(blob);

	return ret ? ret : fdt_fixup_k3_sec_proxy_handoff(blob);
}
