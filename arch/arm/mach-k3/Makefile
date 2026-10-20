# SPDX-License-Identifier:	GPL-2.0+
#
# Copyright (C) 2017-2018 Texas Instruments Incorporated - https://www.ti.com/
#	Lokesh Vutla <lokeshvutla@ti.com>

obj-$(CONFIG_ARM64) += arm64/
obj-$(CONFIG_CPU_V7R) += r5/
obj-$(CONFIG_OF_LIBFDT) += common_fdt.o
obj-y += common.o security.o k3-ddr.o
obj-$(CONFIG_OF_LIBFDT) += k3_handoff_fdt.o
ifeq ($(CONFIG_SPL_BUILD),y)
obj-$(CONFIG_SPL_K3_SEC_PROXY_HANDOFF) += k3_handoff.o
K3_HANDOFF_ENABLED := $(CONFIG_SPL_K3_SEC_PROXY_HANDOFF)
else
obj-$(CONFIG_K3_SEC_PROXY_HANDOFF) += k3_handoff.o
K3_HANDOFF_ENABLED := $(CONFIG_K3_SEC_PROXY_HANDOFF)
endif
obj-$(CONFIG_SOC_K3_AM62A7) += am62ax/
obj-$(CONFIG_SOC_K3_AM62P5) += am62px/
obj-$(CONFIG_SOC_K3_AM625) += am62x/
obj-$(CONFIG_SOC_K3_AM642) += am64x/
obj-$(CONFIG_SOC_K3_AM654) += am65x/
obj-$(CONFIG_SOC_K3_J721E) += j721e/
obj-$(CONFIG_SOC_K3_J7200) += j7200/
obj-$(CONFIG_SOC_K3_J721S2) += j721s2/
obj-$(CONFIG_SOC_K3_J722S) += j722s/
obj-$(CONFIG_SOC_K3_J784S4) += j784s4/

ifeq ($(K3_HANDOFF_ENABLED),y)
targets += k3_handoff_data.h

K3_HANDOFF_CONFIG_FILE := $(strip $(if $(K3_HANDOFF_CONFIG),\
	$(if $(filter /%,$(K3_HANDOFF_CONFIG)),\
		$(wildcard $(K3_HANDOFF_CONFIG)),\
		$(firstword $(wildcard $(addsuffix /$(K3_HANDOFF_CONFIG),\
			$(binman_indirs)))))))

ifeq ($(CONFIG_CPU_V7R),y)
K3_HANDOFF_CONSUMERS := --consumer r5-spl
else ifeq ($(CONFIG_SPL_BUILD),y)
K3_HANDOFF_CONSUMERS := --consumer aarch64-spl
else
K3_HANDOFF_CONSUMERS := --consumer u-boot --consumer linux
endif

quiet_cmd_k3_handoff = GEN     $@
      cmd_k3_handoff = test -n "$(K3_HANDOFF_CONFIG)" || { echo "K3_HANDOFF_CONFIG is required with CONFIG_K3_SEC_PROXY_HANDOFF" >&2; exit 1; }; \
	test -n "$(K3_HANDOFF_CONFIG_FILE)" || { echo "K3_HANDOFF_CONFIG '$(K3_HANDOFF_CONFIG)' not found in Binman input directories" >&2; exit 1; }; \
	python3 $(srctree)/tools/binman/k3_handoff.py --config "$(K3_HANDOFF_CONFIG_FILE)" $(K3_HANDOFF_CONSUMERS) --output $@ --c-header

$(obj)/k3_handoff_data.h: $(srctree)/tools/binman/k3_handoff.py \
	$(srctree)/tools/binman/firmware_handoff.py $(K3_HANDOFF_CONFIG_FILE) FORCE
	$(call if_changed,k3_handoff)

$(obj)/k3_handoff.o: $(obj)/k3_handoff_data.h
endif
