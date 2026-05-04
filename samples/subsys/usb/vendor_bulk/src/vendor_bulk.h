/*
 * Copyright (c) 2026 Zephyr contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SAMPLES_USB_VENDOR_BULK_H_
#define SAMPLES_USB_VENDOR_BULK_H_

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Submit a bulk IN transfer on the vendor-class endpoint.
 *
 * Producer-mode only (CONFIG_APP_VENDOR_BULK_PRODUCER=y). The class driver
 * allocates a UDC buffer, copies @p payload into it, and enqueues it on the
 * bulk IN endpoint. Multiple transfers may be in flight concurrently up to
 * the limit imposed by the UDC pool; submission only fails on -ENOMEM /
 * -ENETDOWN, never -EBUSY.
 */
int vendor_bulk_in_submit(const uint8_t *payload, size_t len);

/** Number of bulk IN transfers currently queued in the USB controller. */
int vendor_bulk_in_inflight(void);

/** True after the host has configured the device and bulk EPs are armed. */
bool vendor_bulk_is_ready(void);

/* Application-side hooks (weak in practice; the sample provides them). */
void vendor_bulk_on_enable(void);
void vendor_bulk_on_in_complete(void);

#ifdef __cplusplus
}
#endif

#endif /* SAMPLES_USB_VENDOR_BULK_H_ */
