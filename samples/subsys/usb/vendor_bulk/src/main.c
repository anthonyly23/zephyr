/*
 * Copyright (c) 2026 Zephyr contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Vendor-class bulk sample app: enables the USB device and, in producer mode,
 * continuously submits a counter pattern on the bulk IN endpoint. Pipelining
 * is handled by submitting the next IN transfer from the completion hook,
 * which runs in USB-stack context.
 */

#include <sample_usbd.h>

#include <zephyr/kernel.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/logging/log.h>

#include "vendor_bulk.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#if CONFIG_APP_VENDOR_BULK_PRODUCER

#define IN_DEPTH CONFIG_APP_VENDOR_BULK_IN_DEPTH
#define XFER_SIZE CONFIG_APP_VENDOR_BULK_XFER_SIZE

/* Single source buffer reused for every IN transfer. The class driver memcpys
 * its content into a UDC net_buf at submit time, so the host receives a
 * deterministic pattern but we avoid touching this memory on the hot path.
 */
static uint8_t in_buf[XFER_SIZE];

static void fill_pattern_once(void)
{
	for (size_t i = 0; i < XFER_SIZE; i += sizeof(uint32_t)) {
		uint32_t v = (uint32_t)(i / sizeof(uint32_t));

		in_buf[i + 0] = (uint8_t)(v);
		in_buf[i + 1] = (uint8_t)(v >> 8);
		in_buf[i + 2] = (uint8_t)(v >> 16);
		in_buf[i + 3] = (uint8_t)(v >> 24);
	}
}

static void refill_pipeline(void)
{
	while (vendor_bulk_in_inflight() < IN_DEPTH) {
		int err = vendor_bulk_in_submit(in_buf, XFER_SIZE);

		if (err) {
			LOG_WRN("submit failed (%d)", err);
			return;
		}
	}
}

void vendor_bulk_on_enable(void)
{
	refill_pipeline();
}

void vendor_bulk_on_in_complete(void)
{
	refill_pipeline();
}

#else /* loopback mode - main has nothing to do */

void vendor_bulk_on_enable(void) {}
void vendor_bulk_on_in_complete(void) {}

#endif

int main(void)
{
	struct usbd_context *sample_usbd;
	int err;

#if CONFIG_APP_VENDOR_BULK_PRODUCER
	fill_pattern_once();
#endif

	sample_usbd = sample_usbd_init_device(NULL);
	if (sample_usbd == NULL) {
		LOG_ERR("sample_usbd_init_device failed");
		return -ENODEV;
	}

	if (!usbd_can_detect_vbus(sample_usbd)) {
		err = usbd_enable(sample_usbd);
		if (err) {
			LOG_ERR("usbd_enable failed (%d)", err);
			return err;
		}
	}

	LOG_INF("USB vendor bulk sample running (%s mode)",
		IS_ENABLED(CONFIG_APP_VENDOR_BULK_PRODUCER) ?
			"producer" : "loopback");
	return 0;
}
