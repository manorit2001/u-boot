// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2016, NVIDIA CORPORATION.
 */

#include <dm.h>
#include <malloc.h>
#include <mailbox.h>
#include <dm/test.h>
#include <asm/mbox.h>
#include <test/test.h>
#include <test/ut.h>

static int dm_test_mailbox(struct unit_test_state *uts)
{
	struct udevice *dev;
	uint32_t msg;

	ut_assertok(uclass_get_device_by_name(UCLASS_MISC, "mbox-test", &dev));
	ut_assertok(sandbox_mbox_test_get(dev));

	ut_asserteq(-ETIMEDOUT, sandbox_mbox_test_recv(dev, &msg));
	ut_assertok(sandbox_mbox_test_send(dev, 0xaaff9955UL));
	ut_assertok(sandbox_mbox_test_recv(dev, &msg));
	ut_asserteq(msg, 0xaaff9955UL ^ SANDBOX_MBOX_PING_XOR);
	ut_asserteq(-ETIMEDOUT, sandbox_mbox_test_recv(dev, &msg));

	ut_assertok(sandbox_mbox_test_free(dev));

	return 0;
}
DM_TEST(dm_test_mailbox, UTF_SCAN_FDT);

static int dm_test_mailbox_override(struct unit_test_state *uts)
{
	struct udevice *dev;
	struct mbox_chan channel;

	ut_assertok(uclass_get_device_by_name(UCLASS_MISC, "mbox-test", &dev));
	ut_assertok(mbox_get_by_name_with_id(dev, "test", 0, &channel));
	ut_asserteq(0, channel.id);
	ut_assertok(mbox_free(&channel));
	ut_assertok(mbox_get_by_name_with_id(dev, "test", 1, &channel));
	ut_asserteq(1, channel.id);
	ut_assertok(mbox_free(&channel));
	ut_asserteq(-EINVAL, mbox_get_by_name_with_id(dev, "test", 2, &channel));
	ut_asserteq(-EINVAL, mbox_get_by_name_with_id(dev, "test", ~0U, &channel));
	return 0;
}

DM_TEST(dm_test_mailbox_override, UTF_SCAN_FDT);
