/*
 * Copyright (c) 2026 Zephyr contributors
 *
 * Custom vendor-class USB function with a single interface that exposes one
 * bulk IN endpoint and one bulk OUT endpoint. Derived from
 * `subsys/usb/device_next/class/loopback.c` but trimmed to the minimum needed
 * for high-throughput vendor bulk transfers.
 *
 * Two operating modes:
 *   - Producer mode (CONFIG_APP_VENDOR_BULK_PRODUCER=y): an external thread
 *     fills the bulk IN endpoint via vendor_bulk_in_submit(); incoming OUT
 *     traffic is dropped and re-armed automatically.
 *   - Loopback mode (=n): copies OUT->IN automatically, like the upstream
 *     loopback class.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/drivers/usb/udc.h>
#include <zephyr/logging/log.h>

#include "vendor_bulk.h"

LOG_MODULE_REGISTER(vendor_bulk, LOG_LEVEL_INF);

#define VB_BULK_FS_MPS 64
#define VB_BULK_HS_MPS 512

/* state bits */
#define VB_ENABLED        0
#define VB_OUT_ENGAGED    1

struct vb_desc {
	struct usb_if_descriptor if0;
	struct usb_ep_descriptor if0_out_ep;
	struct usb_ep_descriptor if0_in_ep;
	struct usb_ep_descriptor if0_hs_out_ep;
	struct usb_ep_descriptor if0_hs_in_ep;
	struct usb_desc_header nil_desc;
};

struct vb_data {
	struct vb_desc *const desc;
	const struct usb_desc_header **const fs_desc;
	const struct usb_desc_header **const hs_desc;
	atomic_t state;
	atomic_t in_inflight;
#if !IS_ENABLED(CONFIG_APP_VENDOR_BULK_PRODUCER)
	/* Sink buffer for OUT data only used in loopback mode. */
	uint8_t out_sink[CONFIG_APP_VENDOR_BULK_XFER_SIZE];
#endif
};

static struct usbd_class_data *vb_class_data;

static uint8_t vb_get_bulk_out(struct usbd_class_data *const c_data)
{
	struct vb_data *data = usbd_class_get_private(c_data);
	struct usbd_context *uds_ctx = usbd_class_get_ctx(c_data);

	if (usbd_bus_speed(uds_ctx) == USBD_SPEED_HS) {
		return data->desc->if0_hs_out_ep.bEndpointAddress;
	}

	return data->desc->if0_out_ep.bEndpointAddress;
}

static uint8_t vb_get_bulk_in(struct usbd_class_data *const c_data)
{
	struct vb_data *data = usbd_class_get_private(c_data);
	struct usbd_context *uds_ctx = usbd_class_get_ctx(c_data);

	if (usbd_bus_speed(uds_ctx) == USBD_SPEED_HS) {
		return data->desc->if0_hs_in_ep.bEndpointAddress;
	}

	return data->desc->if0_in_ep.bEndpointAddress;
}

static int vb_submit_bulk_out(struct usbd_class_data *const c_data)
{
	struct vb_data *data = usbd_class_get_private(c_data);
	struct net_buf *buf;
	int err;

	if (!atomic_test_bit(&data->state, VB_ENABLED)) {
		return -EPERM;
	}

	if (atomic_test_and_set_bit(&data->state, VB_OUT_ENGAGED)) {
		return -EBUSY;
	}

	buf = usbd_ep_buf_alloc(c_data, vb_get_bulk_out(c_data),
				CONFIG_APP_VENDOR_BULK_XFER_SIZE);
	if (buf == NULL) {
		atomic_clear_bit(&data->state, VB_OUT_ENGAGED);
		LOG_ERR("OUT buf alloc failed");
		return -ENOMEM;
	}

	err = usbd_ep_enqueue(c_data, buf);
	if (err) {
		atomic_clear_bit(&data->state, VB_OUT_ENGAGED);
		net_buf_unref(buf);
		LOG_ERR("OUT enqueue failed (%d)", err);
	}

	return err;
}

#if !IS_ENABLED(CONFIG_APP_VENDOR_BULK_PRODUCER)
/* Loopback variant - copies sink into a new IN buffer. */
static int vb_submit_bulk_in_echo(struct usbd_class_data *const c_data)
{
	struct vb_data *data = usbd_class_get_private(c_data);
	struct net_buf *buf;
	int err;

	if (!atomic_test_bit(&data->state, VB_ENABLED)) {
		return -EPERM;
	}

	buf = usbd_ep_buf_alloc(c_data, vb_get_bulk_in(c_data),
				CONFIG_APP_VENDOR_BULK_XFER_SIZE);
	if (buf == NULL) {
		return -ENOMEM;
	}

	net_buf_add_mem(buf, data->out_sink,
			MIN(sizeof(data->out_sink), net_buf_tailroom(buf)));

	atomic_inc(&data->in_inflight);
	err = usbd_ep_enqueue(c_data, buf);
	if (err) {
		atomic_dec(&data->in_inflight);
		net_buf_unref(buf);
	}

	return err;
}
#endif

/* Producer-mode public API: submit an arbitrary payload on bulk IN. */
int vendor_bulk_in_submit(const uint8_t *payload, size_t len)
{
	struct usbd_class_data *c_data = vb_class_data;
	struct vb_data *data;
	struct net_buf *buf;
	int err;

	if (c_data == NULL) {
		return -ENODEV;
	}

	data = usbd_class_get_private(c_data);

	if (!atomic_test_bit(&data->state, VB_ENABLED)) {
		return -ENETDOWN;
	}

	buf = usbd_ep_buf_alloc(c_data, vb_get_bulk_in(c_data), len);
	if (buf == NULL) {
		return -ENOMEM;
	}

	net_buf_add_mem(buf, payload, MIN(len, net_buf_tailroom(buf)));

	atomic_inc(&data->in_inflight);
	err = usbd_ep_enqueue(c_data, buf);
	if (err) {
		atomic_dec(&data->in_inflight);
		net_buf_unref(buf);
	}

	return err;
}

int vendor_bulk_in_inflight(void)
{
	struct usbd_class_data *c_data = vb_class_data;
	struct vb_data *data;

	if (c_data == NULL) {
		return 0;
	}

	data = usbd_class_get_private(c_data);
	return (int)atomic_get(&data->in_inflight);
}

bool vendor_bulk_is_ready(void)
{
	struct usbd_class_data *c_data = vb_class_data;
	struct vb_data *data;

	if (c_data == NULL) {
		return false;
	}

	data = usbd_class_get_private(c_data);
	return atomic_test_bit(&data->state, VB_ENABLED);
}

static int vb_request_handler(struct usbd_class_data *const c_data,
			      struct net_buf *const buf, const int err)
{
	struct vb_data *data = usbd_class_get_private(c_data);
	struct udc_buf_info *bi = (struct udc_buf_info *)net_buf_user_data(buf);
	const uint8_t ep = bi->ep;

	if (ep == vb_get_bulk_out(c_data)) {
		atomic_clear_bit(&data->state, VB_OUT_ENGAGED);
#if !IS_ENABLED(CONFIG_APP_VENDOR_BULK_PRODUCER)
		if (err == 0) {
			memcpy(data->out_sink, buf->data,
			       MIN(sizeof(data->out_sink), buf->len));
		}
#endif
	} else if (ep == vb_get_bulk_in(c_data)) {
		atomic_dec(&data->in_inflight);
	}

	net_buf_unref(buf);

	if (err == -ECONNABORTED) {
		LOG_INF("Transfer ep 0x%02x cancelled", ep);
		return 0;
	} else if (err) {
		LOG_ERR("Transfer ep 0x%02x failed (%d)", ep, err);
		return err;
	}

	/* Re-arm. In producer mode, IN is driven by the app thread. */
	if (ep == vb_get_bulk_out(c_data)) {
		(void)vb_submit_bulk_out(c_data);
	}
#if !IS_ENABLED(CONFIG_APP_VENDOR_BULK_PRODUCER)
	if (ep == vb_get_bulk_in(c_data)) {
		(void)vb_submit_bulk_in_echo(c_data);
	}
#else
	if (ep == vb_get_bulk_in(c_data)) {
		vendor_bulk_on_in_complete();
	}
#endif

	return 0;
}

static void vb_update(struct usbd_class_data *c_data, uint8_t iface, uint8_t alt)
{
	ARG_UNUSED(c_data);
	ARG_UNUSED(iface);
	ARG_UNUSED(alt);
}

static void *vb_get_desc(struct usbd_class_data *const c_data,
			 const enum usbd_speed speed)
{
	struct vb_data *data = usbd_class_get_private(c_data);

	if (USBD_SUPPORTS_HIGH_SPEED && speed == USBD_SPEED_HS) {
		return data->hs_desc;
	}

	return data->fs_desc;
}

static void vb_enable(struct usbd_class_data *const c_data)
{
	struct vb_data *data = usbd_class_get_private(c_data);

	LOG_INF("Enable %s", c_data->name);
	if (!atomic_test_and_set_bit(&data->state, VB_ENABLED)) {
#if !IS_ENABLED(CONFIG_APP_VENDOR_BULK_PRODUCER)
		/* Loopback mode: arm OUT and IN. In producer mode we skip OUT
		 * to keep the entire UDC pool available for IN transfers.
		 */
		(void)vb_submit_bulk_out(c_data);
		(void)vb_submit_bulk_in_echo(c_data);
#else
		vendor_bulk_on_enable();
#endif
	}
}

static void vb_disable(struct usbd_class_data *const c_data)
{
	struct vb_data *data = usbd_class_get_private(c_data);

	atomic_clear_bit(&data->state, VB_ENABLED);
	LOG_INF("Disable %s", c_data->name);
}

static int vb_init(struct usbd_class_data *c_data)
{
	vb_class_data = c_data;
	LOG_INF("Vendor bulk class registered");
	return 0;
}

static struct usbd_class_api vb_api = {
	.update = vb_update,
	.request = vb_request_handler,
	.get_desc = vb_get_desc,
	.enable = vb_enable,
	.disable = vb_disable,
	.init = vb_init,
};

static struct vb_desc vb_desc_0 = {
	.if0 = {
		.bLength = sizeof(struct usb_if_descriptor),
		.bDescriptorType = USB_DESC_INTERFACE,
		.bInterfaceNumber = 0,
		.bAlternateSetting = 0,
		.bNumEndpoints = 2,
		.bInterfaceClass = USB_BCC_VENDOR,
		.bInterfaceSubClass = 0,
		.bInterfaceProtocol = 0,
		.iInterface = 0,
	},
	.if0_out_ep = {
		.bLength = sizeof(struct usb_ep_descriptor),
		.bDescriptorType = USB_DESC_ENDPOINT,
		.bEndpointAddress = 0x01,
		.bmAttributes = USB_EP_TYPE_BULK,
		.wMaxPacketSize = sys_cpu_to_le16(VB_BULK_FS_MPS),
		.bInterval = 0x00,
	},
	.if0_in_ep = {
		.bLength = sizeof(struct usb_ep_descriptor),
		.bDescriptorType = USB_DESC_ENDPOINT,
		.bEndpointAddress = 0x81,
		.bmAttributes = USB_EP_TYPE_BULK,
		.wMaxPacketSize = sys_cpu_to_le16(VB_BULK_FS_MPS),
		.bInterval = 0x00,
	},
	.if0_hs_out_ep = {
		.bLength = sizeof(struct usb_ep_descriptor),
		.bDescriptorType = USB_DESC_ENDPOINT,
		.bEndpointAddress = 0x01,
		.bmAttributes = USB_EP_TYPE_BULK,
		.wMaxPacketSize = sys_cpu_to_le16(VB_BULK_HS_MPS),
		.bInterval = 0x00,
	},
	.if0_hs_in_ep = {
		.bLength = sizeof(struct usb_ep_descriptor),
		.bDescriptorType = USB_DESC_ENDPOINT,
		.bEndpointAddress = 0x81,
		.bmAttributes = USB_EP_TYPE_BULK,
		.wMaxPacketSize = sys_cpu_to_le16(VB_BULK_HS_MPS),
		.bInterval = 0x00,
	},
	.nil_desc = {
		.bLength = 0,
		.bDescriptorType = 0,
	},
};

static const struct usb_desc_header *vb_fs_desc[] = {
	(struct usb_desc_header *)&vb_desc_0.if0,
	(struct usb_desc_header *)&vb_desc_0.if0_out_ep,
	(struct usb_desc_header *)&vb_desc_0.if0_in_ep,
	(struct usb_desc_header *)&vb_desc_0.nil_desc,
};

static const struct usb_desc_header *vb_hs_desc[] = {
	(struct usb_desc_header *)&vb_desc_0.if0,
	(struct usb_desc_header *)&vb_desc_0.if0_hs_out_ep,
	(struct usb_desc_header *)&vb_desc_0.if0_hs_in_ep,
	(struct usb_desc_header *)&vb_desc_0.nil_desc,
};

static struct vb_data vb_data_0 = {
	.desc = &vb_desc_0,
	.fs_desc = vb_fs_desc,
	.hs_desc = vb_hs_desc,
};

USBD_DEFINE_CLASS(vendor_bulk_0, &vb_api, &vb_data_0, NULL);
