/***************************************************************************
 *   Copyright (C) 2009 - 2010 by Simon Qian <SimonQian@SimonQian.com>     *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#include "vsf.h"

#undef vsfusbd_device_get_descriptor
#undef vsfusbd_device_init
#undef vsfusbd_device_fini
#undef vsfusbd_ep_recv
#undef vsfusbd_ep_cancel_recv
#undef vsfusbd_ep_send
#undef vsfusbd_ep_cancel_send
#undef vsfusbd_set_IN_handler
#undef vsfusbd_set_OUT_handler

// events for vsfusbd
#define VSFUSBD_INTEVT_BASE				VSFSM_EVT_USER_LOCAL
enum vsfusbd_evt_t
{
	VSFUSBD_INTEVT_RESET = VSFUSBD_INTEVT_BASE + 0,
	VSFUSBD_INTEVT_SUSPEND = VSFUSBD_INTEVT_BASE + 1,
	VSFUSBD_INTEVT_RESUME = VSFUSBD_INTEVT_BASE + 2,
	VSFUSBD_INTEVT_WAKEUP = VSFUSBD_INTEVT_BASE + 3,
	VSFUSBD_INTEVT_DETACH = VSFUSBD_INTEVT_BASE + 4,
	VSFUSBD_INTEVT_ATTACH = VSFUSBD_INTEVT_BASE + 5,
	VSFUSBD_INTEVT_SOF = VSFUSBD_INTEVT_BASE + 6,
	VSFUSBD_INTEVT_SETUP = VSFUSBD_INTEVT_BASE + 7,
	VSFUSBD_INTEVT_INOUT = VSFUSBD_INTEVT_BASE + 0x20,
	VSFUSBD_STREAM_INOUT = VSFUSBD_INTEVT_BASE + 0x40,
	VSFUSBD_STREAM_CLOSE_INOUT = VSFUSBD_INTEVT_BASE + 0x60,
	VSFUSBD_INTEVT_ERR = VSFUSBD_INTEVT_BASE + 0x100,
};
#define VSFUSBD_EVT_EP_MASK				0x0F
#define VSFUSBD_EVT_DIR_MASK			0x10
#define VSFUSBD_EVT_DIR_IN				0x10
#define VSFUSBD_EVT_DIR_OUT				0x00
#define VSFUSBD_EVT_EVT_MASK			~0xF
#define VSFUSBD_EVT_ERR_MASK			~0xFF
#define VSFUSBD_INTEVT_IN				(VSFUSBD_INTEVT_INOUT | VSFUSBD_EVT_DIR_IN)
#define VSFUSBD_INTEVT_OUT				(VSFUSBD_INTEVT_INOUT | VSFUSBD_EVT_DIR_OUT)
#define VSFUSBD_STREAM_IN				(VSFUSBD_STREAM_INOUT | VSFUSBD_EVT_DIR_IN)
#define VSFUSBD_STREAM_OUT				(VSFUSBD_STREAM_INOUT | VSFUSBD_EVT_DIR_OUT)
#define VSFUSBD_STREAM_CLOSE_IN			(VSFUSBD_STREAM_CLOSE_INOUT | VSFUSBD_EVT_DIR_IN)
#define VSFUSBD_STREAM_CLOSE_OUT		(VSFUSBD_STREAM_CLOSE_INOUT | VSFUSBD_EVT_DIR_OUT)
#define VSFUSBD_INTEVT_INEP(ep)			(VSFUSBD_INTEVT_IN + (ep))
#define VSFUSBD_INTEVT_OUTEP(ep)		(VSFUSBD_INTEVT_OUT + (ep))
#define VSFUSBD_STREAM_INEP(ep)			(VSFUSBD_STREAM_IN + (ep))
#define VSFUSBD_STREAM_OUTEP(ep)		(VSFUSBD_STREAM_OUT + (ep))
#define VSFUSBD_STREAM_CLOSE_INEP(ep)	(VSFUSBD_STREAM_CLOSE_IN + (ep))
#define VSFUSBD_STREAM_CLOSE_OUTEP(ep)	(VSFUSBD_STREAM_CLOSE_OUT + (ep))

vsf_err_t vsfusbd_device_get_descriptor(struct vsfusbd_device_t *device,
		struct vsfusbd_desc_filter_t *filter, uint8_t type, uint8_t index,
		uint16_t lanid, struct vsf_buffer_t *buffer)
{
	while ((filter->buffer.buffer != NULL) && (filter->buffer.size != 0))
	{
		if ((filter->type == type) && (filter->index == index) &&
			(filter->lanid == lanid))
		{
			buffer->size = filter->buffer.size;
			buffer->buffer = filter->buffer.buffer;
			return VSFERR_NONE;
		}
		filter++;
	}
	return VSFERR_FAIL;
}

static vsf_err_t vsfusbd_on_IN_do(struct vsfusbd_device_t *device, uint8_t ep);
static vsf_err_t vsfusbd_on_OUT_do(struct vsfusbd_device_t *device, uint8_t ep);

vsf_err_t vsfusbd_set_IN_handler(struct vsfusbd_device_t *device,
		uint8_t ep, vsf_err_t (*handler)(struct vsfusbd_device_t*, uint8_t))
{
	device->IN_handler[ep] = handler;
	return VSFERR_NONE;
}

vsf_err_t vsfusbd_set_OUT_handler(struct vsfusbd_device_t *device,
		uint8_t ep, vsf_err_t (*handler)(struct vsfusbd_device_t*, uint8_t))
{
	device->OUT_handler[ep] = handler;
	return VSFERR_NONE;
}

static void vsfusbd_stream_on_disconnect_OUT(void *p)
{
	struct vsfusbd_transact_t *transact = (struct vsfusbd_transact_t *)p;
	struct vsfusbd_device_t *device = transact->device;
	uint8_t ep = transact->ep;

	vsfsm_post_evt_pending(&device->sm, VSFUSBD_STREAM_CLOSE_OUTEP(ep));
}

static void vsfusbd_stream_on_out(void *p)
{
	struct vsfusbd_transact_t *transact = (struct vsfusbd_transact_t *)p;
	struct vsfusbd_device_t *device = transact->device;

	vsfsm_post_evt_pending(&device->sm, VSFUSBD_STREAM_OUTEP(transact->ep));
}

static void vsfusbd_transact_out(struct vsfusbd_device_t *device,
									struct vsfusbd_transact_t *transact)
{
	uint8_t ep = transact->ep;
	struct interface_usbd_t *drv = device->drv; 

	if (transact->idle)
	{
		uint16_t ep_size = drv->ep.get_OUT_epsize(ep);
		uint16_t pkg_size = min(ep_size, transact->data_size);
		uint32_t free_size = stream_get_free_size(transact->stream);

		transact->idle = free_size < pkg_size;
		if (!transact->idle)
		{
			drv->ep.enable_OUT(ep);
		}
	}
}

vsf_err_t vsfusbd_ep_recv(struct vsfusbd_device_t *device,
								struct vsfusbd_transact_t *transact)
{
	device->OUT_handler[transact->ep] = vsfusbd_on_OUT_do;
	device->OUT_transact[transact->ep] = transact;
	transact->idle = true;
	transact->device = device;
	if (transact->data_size)
	{
		struct vsf_stream_t *stream = transact->stream;

		stream->callback_tx.param = transact;
		stream->callback_tx.on_connect = NULL;
		stream->callback_tx.on_disconnect = vsfusbd_stream_on_disconnect_OUT;
		stream->callback_tx.on_inout = vsfusbd_stream_on_out;
		stream_connect_tx(stream);
		vsfusbd_transact_out(device, transact);
	}
	else
	{
		device->drv->ep.enable_OUT(transact->ep);
	}

	return VSFERR_NONE;
}

void vsfusbd_ep_cancel_recv(struct vsfusbd_device_t *device,
								struct vsfusbd_transact_t *transact)
{
	if (transact->stream != NULL)
	{
		transact->stream->callback_tx.on_disconnect = NULL;
		transact->stream->callback_tx.on_inout = NULL;
	}
	device->OUT_handler[transact->ep] = NULL;
	device->OUT_transact[transact->ep] = NULL;
	if (transact->cb.on_finish != NULL)
	{
		transact->cb.on_finish(transact->cb.param);
	}
}

static void vsfusbd_stream_on_disconnect_IN(void *p)
{
	struct vsfusbd_transact_t *transact = (struct vsfusbd_transact_t *)p;
	struct vsfusbd_device_t *device = transact->device;
	uint8_t ep = transact->ep;

	vsfsm_post_evt_pending(&device->sm, VSFUSBD_STREAM_CLOSE_INEP(ep));
}

static void vsfusbd_stream_on_in(void *p)
{
	struct vsfusbd_transact_t *transact = (struct vsfusbd_transact_t *)p;
	struct vsfusbd_device_t *device = transact->device;

	vsfsm_post_evt_pending(&device->sm, VSFUSBD_STREAM_INEP(transact->ep));
}

static vsf_err_t vsfusbd_on_IN_do(struct vsfusbd_device_t *device, uint8_t ep);
vsf_err_t vsfusbd_ep_send(struct vsfusbd_device_t *device,
								struct vsfusbd_transact_t *transact)
{
	uint8_t ep = transact->ep;

	device->IN_handler[ep] = vsfusbd_on_IN_do;
	device->IN_transact[ep] = transact;
	transact->idle = true;
	transact->device = device;
	if (transact->data_size)
	{
		struct vsf_stream_t *stream = transact->stream;

		stream->callback_rx.param = transact;
		stream->callback_rx.on_connect = NULL;
		stream->callback_rx.on_disconnect = vsfusbd_stream_on_disconnect_IN;
		stream->callback_rx.on_inout = vsfusbd_stream_on_in;
		stream_connect_rx(stream);

		vsfusbd_on_IN_do(device, transact->ep);
#if VSFUSBD_CFG_DBUFFER_EN
		if (device->drv->ep.is_IN_dbuffer(transact->ep))
		{
			vsfusbd_on_IN_do(device, transact->ep);
		}
#endif
	}
	else
	{
		// send zlp
		device->drv->ep.set_IN_count(ep, 0);
	}

	return VSFERR_NONE;
}

void vsfusbd_ep_cancel_send(struct vsfusbd_device_t *device,
								struct vsfusbd_transact_t *transact)
{
	if (transact->stream != NULL)
	{
		transact->stream->callback_rx.on_disconnect = NULL;
		transact->stream->callback_rx.on_inout = NULL;
	}
	device->IN_handler[transact->ep] = NULL;
	device->IN_transact[transact->ep] = NULL;
	if (transact->cb.on_finish != NULL)
	{
		transact->cb.on_finish(transact->cb.param);
	}
}

// standard request handlers
static int16_t
vsfusbd_get_config(struct vsfusbd_device_t *device, uint8_t value)
{
	uint8_t i;

	for (i = 0; i < device->num_of_configuration; i++)
	{
		if (value == device->config[i].configuration_value)
		{
			return i;
		}
	}
	return -1;
}

#if VSFUSBD_CFG_AUTOSETUP
static vsf_err_t vsfusbd_auto_init(struct vsfusbd_device_t *device)
{
	struct vsfusbd_config_t *config;
	struct vsf_buffer_t desc = {NULL, 0};
	enum interface_usbd_eptype_t ep_type;
	uint16_t pos;
	uint8_t attr, feature;
	uint16_t ep_size, ep_addr, ep_index, ep_attr;
	int16_t cur_iface;

	config = &device->config[device->configuration];

	// config other eps according to descriptors
	if (vsfusbd_device_get_descriptor(device, device->desc_filter,
				USB_DT_CONFIG, device->configuration, 0, &desc)
#if __VSF_DEBUG__
		|| (NULL == desc.buffer) || (desc.size <= USB_DESC_SIZE_CONFIGURATION)
		|| (desc.buffer[0] != USB_DESC_SIZE_CONFIGURATION)
		|| (desc.buffer[1] != USB_DESC_TYPE_CONFIGURATION)
		|| (config->num_of_ifaces != desc.buffer[USB_DESC_CONFIG_OFF_IFNUM])
#endif
		)
	{
		return VSFERR_FAIL;
	}

	// initialize device feature according to
	// bmAttributes field in configuration descriptor
	attr = desc.buffer[7];
	feature = 0;
	if (attr & USB_CONFIG_ATT_SELFPOWER)
	{
		feature |= 1 << USB_DEVICE_SELF_POWERED;
	}
	if (attr & USB_CONFIG_ATT_WAKEUP)
	{
		feature |= 1 << USB_DEVICE_REMOTE_WAKEUP;
	}

#if __VSF_DEBUG__
	num_iface = desc.buffer[USB_DESC_CONFIG_OFF_IFNUM];
	num_endpoint = 0;
#endif

	cur_iface = -1;
	pos = USB_DT_CONFIG_SIZE;
	while (desc.size > pos)
	{
#if __VSF_DEBUG__
		if ((desc.buffer[pos] < 2) || (desc.size < (pos + desc.buffer[pos])))
		{
			return VSFERR_FAIL;
		}
#endif
		switch (desc.buffer[pos + 1])
		{
		case USB_DT_INTERFACE:
#if __VSF_DEBUG__
			if (num_endpoint)
			{
				return VSFERR_FAIL;
			}
			num_endpoint = desc.buffer[pos + 4];
			num_iface--;
#endif
			cur_iface = desc.buffer[pos + 2];
			break;
		case USB_DT_ENDPOINT:
			ep_addr = desc.buffer[pos + 2];
			ep_attr = desc.buffer[pos + 3];
			ep_size = desc.buffer[pos + 4];
			ep_index = ep_addr & 0x0F;
#if __VSF_DEBUG__
			num_endpoint--;
			if (ep_index > (*device->drv->ep.num_of_ep - 1))
			{
				return VSFERR_FAIL;
			}
#endif
			switch (ep_attr & 0x03)
			{
			case 0x00:
				ep_type = USB_EP_TYPE_CONTROL;
				break;
			case 0x01:
				ep_type = USB_EP_TYPE_ISO;
				break;
			case 0x02:
				ep_type = USB_EP_TYPE_BULK;
				break;
			case 0x03:
				ep_type = USB_EP_TYPE_INTERRUPT;
				break;
			default:
				return VSFERR_FAIL;
			}
			if (ep_addr & 0x80)
			{
				// IN ep
				device->drv->ep.set_IN_epsize(ep_index, ep_size);
				config->ep_IN_iface_map[ep_index] = cur_iface;
			}
			else
			{
				// OUT ep
				device->drv->ep.set_OUT_epsize(ep_index, ep_size);
				config->ep_OUT_iface_map[ep_index] = cur_iface;
			}
			device->drv->ep.set_type(ep_index, ep_type);
			break;
		}
		pos += desc.buffer[pos];
	}
#if __VSF_DEBUG__
	if (num_iface || num_endpoint || (desc.size != pos))
	{
		return VSFERR_FAIL;
	}
#endif
	return VSFERR_NONE;
}
#endif	// VSFUSBD_CFG_AUTOSETUP

static vsf_err_t vsfusbd_stdctrl_prepare(struct vsfusbd_device_t *device)
{
	struct interface_usbd_t *drv = device->drv; 
	struct vsfusbd_config_t *config = &device->config[device->configuration];
	struct vsfusbd_ctrl_handler_t *ctrl_handler = &device->ctrl_handler;
	struct usb_ctrlrequest_t *request = &ctrl_handler->request;
	struct vsf_buffer_t *buffer = &ctrl_handler->bufstream.mem.buffer;
	uint8_t *reply_buffer = ctrl_handler->reply_buffer;
	uint8_t recip = request->bRequestType & USB_RECIP_MASK;

	if (USB_RECIP_DEVICE == recip)
	{
		switch (request->bRequest)
		{
		case USB_REQ_GET_STATUS:
			if ((request->wValue != 0) || (request->wIndex != 0))
			{
				goto fail;
			}
			reply_buffer[0] = device->feature;
			reply_buffer[1] = 0;
			buffer->size = 2;
			break;
		case USB_REQ_CLEAR_FEATURE:
			if ((request->wIndex != 0) ||
				(request->wValue != USB_DEVICE_REMOTE_WAKEUP))
			{
				goto fail;
			}
			device->feature &= ~USB_CONFIG_ATT_WAKEUP;
			break;
		case USB_REQ_SET_FEATURE:
			if ((request->wIndex != 0) ||
				(request->wValue != USB_DEVICE_REMOTE_WAKEUP))
			{
				goto fail;
			}
			device->feature |= USB_CONFIG_ATT_WAKEUP;
			break;
		case USB_REQ_SET_ADDRESS:
			if ((request->wValue > 127) || (request->wIndex != 0) ||
				(device->configuration != 0))
			{
				goto fail;
			}
			break;
		case USB_REQ_GET_DESCRIPTOR:
			{
				uint8_t type = (request->wValue >> 8) & 0xFF;
				uint8_t index = request->wValue & 0xFF;
				uint16_t lanid = request->wIndex;

				if (vsfusbd_device_get_descriptor(device, device->desc_filter,
								type, index, lanid, buffer))
				{
					goto fail;
				}
			}
			break;
		case USB_REQ_GET_CONFIGURATION:
			if ((request->wValue != 0) || (request->wIndex != 0))
			{
				goto fail;
			}
			reply_buffer[0] = config->configuration_value;
			buffer->size = 2;
			break;
		case USB_REQ_SET_CONFIGURATION:
			if ((request->wIndex != 0) ||
				(vsfusbd_get_config(device, request->wValue) < 0))
			{
				goto fail;
			}
			device->configured = false;
			break;
		default:
			goto fail;
		}
	}
	else if (USB_RECIP_INTERFACE == recip)
	{
		uint8_t iface_idx = request->wIndex;
		struct vsfusbd_iface_t *iface = &config->iface[iface_idx];
		struct vsfusbd_class_protocol_t *protocol = iface->class_protocol;

		if (iface_idx >= config->num_of_ifaces)
		{
			goto fail;
		}

		switch (request->bRequest)
		{
		case USB_REQ_GET_STATUS:
			if ((request->wValue != 0) ||
				(request->wIndex >= config->num_of_ifaces))
			{
				goto fail;
			}
			reply_buffer[0] = 0;
			reply_buffer[1] = 0;
			buffer->size = 2;
			break;
		case USB_REQ_CLEAR_FEATURE:
			break;
		case USB_REQ_SET_FEATURE:
			break;
		case USB_REQ_GET_DESCRIPTOR:
			{
				uint8_t type = (request->wValue >> 8) & 0xFF;
				uint8_t index = request->wValue & 0xFF;

				if ((NULL == protocol) || (NULL == protocol->get_desc) ||
					protocol->get_desc(device, type, index, 0, buffer))
				{
					goto fail;
				}
			}
			break;
		case USB_REQ_GET_INTERFACE:
			if (request->wValue != 0)
			{
				goto fail;
			}
			reply_buffer[0] = iface->alternate_setting;
			buffer->size = 1;
			break;
		case USB_REQ_SET_INTERFACE:
			iface->alternate_setting = request->wValue;
			if ((device->callback.on_set_interface != NULL) &&
				device->callback.on_set_interface(device, iface_idx,
													iface->alternate_setting))
			{
				goto fail;
			}
			break;
		default:
			goto fail;
		}
	}
	else if (USB_RECIP_ENDPOINT == recip)
	{
		uint8_t ep_num = request->wIndex & 0x7F;
		uint8_t ep_dir = request->wIndex & 0x80;

		if ((request->bRequestType & USB_DIR_MASK) == USB_DIR_IN)
		{
			return VSFERR_FAIL;
		}

		switch (request->bRequest)
		{
		case USB_REQ_GET_STATUS:
			if ((request->wValue != 0) ||
				(request->wIndex >= *drv->ep.num_of_ep))
			{
				goto fail;
			}
			if ((ep_dir && drv->ep.is_IN_stall(ep_num)) ||
				(!ep_dir && drv->ep.is_OUT_stall(ep_num)))
			{
				reply_buffer[0] = 1;
			}
			else
			{
				reply_buffer[0] = 0;
			}
			reply_buffer[1] = 0;
			buffer->size = 2;
			break;
		case USB_REQ_CLEAR_FEATURE:
			if ((request->wValue != USB_ENDPOINT_HALT) ||
				(ep_num >= *drv->ep.num_of_ep))
			{
				goto fail;
			}
			if (ep_dir)
			{
				drv->ep.reset_IN_toggle(ep_num);
				drv->ep.clear_IN_stall(ep_num);
			}
			else
			{
				drv->ep.reset_OUT_toggle(ep_num);
				drv->ep.clear_OUT_stall(ep_num);
				drv->ep.enable_OUT(ep_num);
			}
			break;
		case USB_REQ_SET_FEATURE:
		default:
			goto fail;
		}
	}
	else
	{
	fail:
		return VSFERR_FAIL;
	}

	ctrl_handler->data_size = buffer->size;
	return VSFERR_NONE;
}

static vsf_err_t vsfusbd_stdctrl_process(struct vsfusbd_device_t *device)
{
	struct usb_ctrlrequest_t *request = &device->ctrl_handler.request;
	uint8_t recip = request->bRequestType & USB_RECIP_MASK;

	if (USB_RECIP_DEVICE == recip)
	{
		switch (request->bRequest)
		{
		case USB_REQ_SET_ADDRESS:
			device->address = (uint8_t)request->wValue;
			return device->drv->set_address(device->address);
		case USB_REQ_SET_CONFIGURATION:
			{
				struct vsfusbd_config_t *config;
				int16_t config_idx;
				uint8_t i;
#if __VSF_DEBUG__
				uint8_t num_iface, num_endpoint;
#endif

				config_idx = vsfusbd_get_config(device, request->wValue);
				if (config_idx < 0)
				{
					return VSFERR_FAIL;
				}
				device->configuration = (uint8_t)config_idx;
				config = &device->config[device->configuration];

#if VSFUSBD_CFG_AUTOSETUP
				if (vsfusbd_auto_init(device))
				{
					return VSFERR_FAIL;
				}
#endif

				// call user initialization
				if ((config->init != NULL) && config->init(device))
				{
					return VSFERR_FAIL;
				}

				for (i = 0; i < config->num_of_ifaces; i++)
				{
					config->iface[i].alternate_setting = 0;

					if ((config->iface[i].class_protocol != NULL) &&
							(config->iface[i].class_protocol->init != NULL) &&
							config->iface[i].class_protocol->init(i, device))
					{
						return VSFERR_FAIL;
					}
				}

				device->configured = true;
			}
			break;
		}
	}
	return VSFERR_NONE;
}

static vsf_err_t vsfusbd_ctrl_prepare(struct vsfusbd_device_t *device)
{
	struct vsfusbd_config_t *config = &device->config[device->configuration];
	struct vsfusbd_ctrl_handler_t *ctrl_handler = &device->ctrl_handler;
	struct usb_ctrlrequest_t *request = &ctrl_handler->request;
	uint8_t type = request->bRequestType & USB_TYPE_MASK;
	vsf_err_t err = VSFERR_FAIL;

	// set default stream
	ctrl_handler->stream = (struct vsf_stream_t *)&ctrl_handler->bufstream;
	ctrl_handler->bufstream.stream.op = &bufstream_op;
	ctrl_handler->bufstream.mem.buffer.buffer = ctrl_handler->reply_buffer;
	ctrl_handler->bufstream.mem.buffer.size = 0;
	ctrl_handler->bufstream.mem.read =
						(request->bRequestType & USB_DIR_MASK) == USB_DIR_IN;

	if (USB_TYPE_STANDARD == type)
	{
		err = vsfusbd_stdctrl_prepare(device);
	}
	else if (USB_TYPE_CLASS == type)
	{
		int8_t iface = -1;

		switch (request->bRequestType & USB_RECIP_MASK)
		{
		case USB_RECIP_DEVICE:
			iface = (int8_t)device->device_class_iface;
			break;
		case USB_RECIP_INTERFACE:
			iface = (int8_t)request->wIndex;
			break;
		}
		if ((iface >= 0) && (iface < config->num_of_ifaces) &&
			(config->iface[iface].class_protocol->request_prepare != NULL))
		{
			err = config->iface[iface].class_protocol->request_prepare(device);
		}
	}
	return err ? err : !ctrl_handler->data_size ? VSFERR_NONE :
										stream_init(ctrl_handler->stream);
}

static void vsfusbd_ctrl_process(struct vsfusbd_device_t *device)
{
	struct vsfusbd_config_t *config = &device->config[device->configuration];
	struct vsfusbd_ctrl_handler_t *ctrl_handler = &device->ctrl_handler;
	struct usb_ctrlrequest_t *request = &ctrl_handler->request;
	uint8_t type = request->bRequestType & USB_TYPE_MASK;

	if (USB_TYPE_STANDARD == type)
	{
		vsfusbd_stdctrl_process(device);
	}
	else if (USB_TYPE_CLASS == type)
	{
		int8_t iface = -1;

		switch (request->bRequestType & USB_RECIP_MASK)
		{
		case USB_RECIP_DEVICE:
			iface = (int8_t)device->device_class_iface;
			break;
		case USB_RECIP_INTERFACE:
			iface = (int8_t)request->wIndex;
			break;
		}
		if ((iface >= 0) && (iface < config->num_of_ifaces) &&
			(config->iface[iface].class_protocol->request_process != NULL))
		{
			config->iface[iface].class_protocol->request_process(device);
		}
	}
}

// on_IN and on_OUT
static vsf_err_t vsfusbd_on_IN_do(struct vsfusbd_device_t *device, uint8_t ep)
{
	struct vsfusbd_transact_t *transact = device->IN_transact[ep];

	if (transact->data_size)
	{
		uint32_t data_size = stream_get_data_size(transact->stream);
		uint16_t ep_size = device->drv->ep.get_IN_epsize(ep);
		uint16_t cur_size = min(transact->data_size, ep_size);
		uint8_t buff[64];

		transact->idle = (data_size < cur_size);
		if (transact->idle)
		{
			return VSFERR_NONE;
		}

#if VSFUSBD_CFG_DBUFFER_EN
		if (device->drv->ep.is_IN_dbuffer(ep))
		{
			device->drv->ep.switch_IN_buffer(ep);
		}
#endif

		transact->data_size -= cur_size;
		if (!transact->data_size && (cur_size < ep_size))
		{
			transact->zlp = false;
		}

		{
			struct vsf_buffer_t buffer = {.buffer = buff, .size = cur_size,};
			stream_read(transact->stream, &buffer);
		}

		device->drv->ep.write_IN_buffer(ep, buff, cur_size);
		device->drv->ep.set_IN_count(ep, cur_size);
	}
	else if (transact->zlp)
	{
		transact->zlp = false;
#if VSFUSBD_CFG_DBUFFER_EN
		if (device->drv->ep.is_IN_dbuffer(ep))
		{
			device->drv->ep.switch_IN_buffer(ep);
		}
#endif
		device->drv->ep.set_IN_count(ep, 0);
	}
	else
	{
		vsfusbd_ep_cancel_send(device, transact);
	}

	return VSFERR_NONE;
}

static vsf_err_t vsfusbd_on_OUT_do(struct vsfusbd_device_t *device, uint8_t ep)
{
	struct vsfusbd_transact_t *transact = device->OUT_transact[ep];
	uint16_t ep_size = device->drv->ep.get_OUT_epsize(ep);
	uint16_t pkg_size, next_pkg_size;
	uint32_t free_size;
	uint8_t buff[64];

#if VSFUSBD_CFG_DBUFFER_EN
	if (device->drv->ep.is_OUT_dbuffer(ep))
	{
		device->drv->ep.switch_OUT_buffer(ep);
	}
#endif
	pkg_size = device->drv->ep.get_OUT_count(ep);
	device->drv->ep.read_OUT_buffer(ep, buff, pkg_size);

	if (transact->data_size < pkg_size)
	{
		return VSFERR_BUG;
	}
	transact->data_size -= pkg_size;

	if ((pkg_size < ep_size) || !transact->data_size)
	{
		transact->idle = true;
	}
	else
	{
		free_size = stream_get_free_size(transact->stream) - pkg_size;
		next_pkg_size = min(transact->data_size, ep_size);
		transact->idle = (free_size < next_pkg_size);
		if (!transact->idle)
		{
			device->drv->ep.enable_OUT(ep);
		}
	}

	if (pkg_size > 0)
	{
		struct vsf_buffer_t buffer = {.buffer = buff, .size = pkg_size,};
		stream_write(transact->stream, &buffer);
	}

	if ((pkg_size < ep_size) || !transact->data_size)
	{
		vsfusbd_ep_cancel_recv(device, transact);
	}
	return VSFERR_NONE;
}

static void vsfusbd_setup_end_callback(void *param)
{
	vsfusbd_ctrl_process((struct vsfusbd_device_t *)param);
}

static void vsfusbd_setup_status_callback(void *param)
{
	struct vsfusbd_device_t *device = param;
	struct vsfusbd_ctrl_handler_t *ctrl_handler = &device->ctrl_handler;
	struct usb_ctrlrequest_t *request = &ctrl_handler->request;
	struct vsfusbd_transact_t *transact;
	bool out = (request->bRequestType & USB_DIR_MASK) == USB_DIR_OUT;

	transact = out ? &ctrl_handler->IN_transact : &ctrl_handler->OUT_transact;
	transact->stream = NULL;
	transact->data_size = 0;
	transact->zlp = false;
	transact->cb.param = device;
	transact->cb.on_finish = vsfusbd_setup_end_callback;

	if (out)
	{
		vsfusbd_ep_send(device, transact);
	}
	else
	{
		vsfusbd_ep_recv(device, transact);
	}
}

// interrupts, simply send(pending) interrupt event to sm
static vsf_err_t vsfusbd_on_SETUP(void *p)
{
	struct vsfusbd_device_t *device = p;
	struct vsfsm_t *sm = &device->sm;
	return vsfsm_post_evt_pending(sm, VSFUSBD_INTEVT_SETUP);
}

static vsf_err_t vsfusbd_on_IN(void *p, uint8_t ep)
{
	struct vsfusbd_device_t *device = (struct vsfusbd_device_t *)p;
	struct vsfsm_t *sm = &device->sm;
	return vsfsm_post_evt_pending(sm, VSFUSBD_INTEVT_INEP(ep));
}

static vsf_err_t vsfusbd_on_OUT(void *p, uint8_t ep)
{
	struct vsfusbd_device_t *device = (struct vsfusbd_device_t *)p;
	struct vsfsm_t *sm = &device->sm;
	return vsfsm_post_evt_pending(sm, VSFUSBD_INTEVT_OUTEP(ep));
}

vsf_err_t vsfusbd_on_UNDERFLOW(void *p, uint8_t ep)
{
	return VSFERR_NONE;
}

vsf_err_t vsfusbd_on_OVERFLOW(void *p, uint8_t ep)
{
	return VSFERR_NONE;
}

vsf_err_t vsfusbd_on_RESET(void *p)
{
	struct vsfusbd_device_t *device = p;
	struct vsfsm_t *sm = &device->sm;
	return vsfsm_post_evt_pending(sm, VSFUSBD_INTEVT_RESET);
}

#if VSFUSBD_CFG_LP_EN
vsf_err_t vsfusbd_on_WAKEUP(void *p)
{
	struct vsfusbd_device_t *device = p;
	struct vsfsm_t *sm = &device->sm;
	return vsfsm_post_evt_pending(sm, VSFUSBD_INTEVT_WAKEUP);
}

vsf_err_t vsfusbd_on_SUSPEND(void *p)
{
	struct vsfusbd_device_t *device = p;
	struct vsfsm_t *sm = &device->sm;
	return vsfsm_post_evt_pending(sm, VSFUSBD_INTEVT_SUSPEND);
}

vsf_err_t vsfusbd_on_RESUME(void *p)
{
	struct vsfusbd_device_t *device = p;
	struct vsfsm_t *sm = &device->sm;
	return vsfsm_post_evt_pending(sm, VSFUSBD_INTEVT_RESUME);
}
#endif

vsf_err_t vsfusbd_on_SOF(void *p)
{
	struct vsfusbd_device_t *device = p;
	struct vsfsm_t *sm = &device->sm;
	return vsfsm_post_evt_pending(sm, VSFUSBD_INTEVT_SOF);
}

vsf_err_t vsfusbd_on_ATTACH(void *p)
{
	struct vsfusbd_device_t *device = p;
	struct vsfsm_t *sm = &device->sm;
	return vsfsm_post_evt_pending(sm, VSFUSBD_INTEVT_ATTACH);
}

vsf_err_t vsfusbd_on_DETACH(void *p)
{
	struct vsfusbd_device_t *device = p;
	struct vsfsm_t *sm = &device->sm;
	return vsfsm_post_evt_pending(sm, VSFUSBD_INTEVT_DETACH);
}

vsf_err_t vsfusbd_on_ERROR(void *p, enum interface_usbd_error_t type)
{
	struct vsfusbd_device_t *device = p;
	struct vsfsm_t *sm = &device->sm;
	return vsfsm_post_evt_pending(sm, VSFUSBD_INTEVT_ERR);
}

// state machines
static struct vsfsm_state_t *
vsfusbd_evt_handler(struct vsfsm_t *sm, vsfsm_evt_t evt)
{
	struct vsfusbd_device_t *device =
								container_of(sm, struct vsfusbd_device_t, sm);
	struct interface_usbd_t *drv = device->drv;
	vsf_err_t err = VSFERR_NONE;

	switch (evt)
	{
	case VSFSM_EVT_ENTER:
	case VSFSM_EVT_EXIT:
		break;
	case VSFSM_EVT_FINI:
		drv->fini();
		drv->disconnect();
		if (device->callback.fini != NULL)
		{
			device->callback.fini(device);
		}
		break;
	case VSFSM_EVT_INIT:
		{
		#if VSFUSBD_CFG_AUTOSETUP
			struct vsf_buffer_t desc = {NULL, 0};
			uint8_t i;
		#endif

			device->configured = false;
			device->configuration = 0;
			device->feature = 0;

		#if VSFUSBD_CFG_AUTOSETUP
			for (i = 0; i < device->num_of_configuration; i++)
			{
				if (vsfusbd_device_get_descriptor(device, device->desc_filter,
									USB_DT_CONFIG, i, 0, &desc)
		#if __VSF_DEBUG__
					|| (NULL == desc.buffer)
					|| (desc.size <= USB_DESC_SIZE_CONFIGURATION)
					|| (desc.buffer[0] != USB_DESC_SIZE_CONFIGURATION)
					|| (desc.buffer[1] != USB_DESC_TYPE_CONFIGURATION)
					|| (config->num_of_ifaces !=
								desc.buffer[USB_DESC_CONFIG_OFF_IFNUM])
		#endif
					)
				{
					err = VSFERR_FAIL;
					goto init_exit;
				}
				device->config[i].configuration_value = desc.buffer[5];
			}
		#endif	// VSFUSBD_CFG_AUTOSETUP

			// initialize callback for low level driver before
			// initializing the hardware
			if (drv->callback != NULL)
			{
				drv->callback->param = (void *)device;
				drv->callback->on_attach = NULL;
				drv->callback->on_detach = NULL;
				drv->callback->on_reset = vsfusbd_on_RESET;
				drv->callback->on_setup = vsfusbd_on_SETUP;
				drv->callback->on_error = vsfusbd_on_ERROR;
		#if VSFUSBD_CFG_LP_EN
				drv->callback->on_wakeup = vsfusbd_on_WAKEUP;
				drv->callback->on_suspend = vsfusbd_on_SUSPEND;
		//		drv->callback->on_resume = vsfusbd_on_RESUME;
		#endif
				drv->callback->on_sof = vsfusbd_on_SOF;
				drv->callback->on_underflow = vsfusbd_on_UNDERFLOW;
				drv->callback->on_overflow = vsfusbd_on_OVERFLOW;
				drv->callback->on_in = vsfusbd_on_IN;
				drv->callback->on_out = vsfusbd_on_OUT;
			}

			if (drv->init(device->int_priority) ||
				((device->callback.init != NULL) &&
				 	device->callback.init(device)))
			{
				err = VSFERR_FAIL;
				goto init_exit;
			}

		init_exit:
			break;
		}
	case VSFUSBD_INTEVT_RESET:
		{
			struct vsfusbd_config_t *config;
			uint8_t i;
		#if VSFUSBD_CFG_AUTOSETUP
			struct vsf_buffer_t desc = {NULL, 0};
			uint16_t ep_size;
		#endif

			memset(device->IN_transact, 0, sizeof(device->IN_transact));
			memset(device->OUT_transact, 0,sizeof(device->OUT_transact));
			memset(device->IN_handler, 0, sizeof(device->IN_handler));
			memset(device->OUT_handler, 0, sizeof(device->OUT_handler));

			device->configured = false;
			device->configuration = 0;
			device->feature = 0;

			for (i = 0; i < device->num_of_configuration; i++)
			{
				config = &device->config[i];
				memset(config->ep_OUT_iface_map, -1,
											sizeof(config->ep_OUT_iface_map));
				memset(config->ep_IN_iface_map, -1,
											sizeof(config->ep_OUT_iface_map));
			}

			// reset usb hw
			if (drv->reset() || drv->init(device->int_priority))
			{
				err = VSFERR_FAIL;
				goto reset_exit;
			}

		#if VSFUSBD_CFG_AUTOSETUP
			if (vsfusbd_device_get_descriptor(device, device->desc_filter,
												USB_DT_DEVICE, 0, 0, &desc)
		#if __VSF_DEBUG__
				|| (NULL == desc.buffer) || (desc.size != USB_DESC_SIZE_DEVICE)
				|| (desc.buffer[0] != desc.size)
				|| (desc.buffer[1] != USB_DESC_TYPE_DEVICE)
				|| (device->num_of_configuration !=
										desc.buffer[USB_DESC_DEVICE_OFF_CFGNUM])
		#endif
				)
			{
				err = VSFERR_FAIL;
				goto reset_exit;
			}
			ep_size = desc.buffer[7];
			device->ctrl_handler.ep_size = ep_size;

			// config ep0
			if (drv->prepare_buffer() ||
				drv->ep.set_IN_epsize(0, ep_size) ||
				drv->ep.set_OUT_epsize(0, ep_size) ||
				drv->ep.set_type(0, USB_EP_TYPE_CONTROL))
			{
				err = VSFERR_FAIL;
				goto reset_exit;
			}
		#endif	// VSFUSBD_CFG_AUTOSETUP

			if (device->callback.on_RESET != NULL)
			{
				device->callback.on_RESET(device);
			}

			if (vsfusbd_set_IN_handler(device, 0, vsfusbd_on_IN_do) ||
				vsfusbd_set_OUT_handler(device, 0, vsfusbd_on_OUT_do) ||
				drv->set_address(0))
			{
				err = VSFERR_FAIL;
				goto reset_exit;
			}
		reset_exit:
			// what to do if fail to process setup?
			break;
		}
	case VSFUSBD_INTEVT_SETUP:
		{
			struct vsfusbd_ctrl_handler_t *ctrl_handler = &device->ctrl_handler;
			struct usb_ctrlrequest_t *request = &ctrl_handler->request;
			struct vsfusbd_transact_t *transact;

			if (drv->get_setup((uint8_t *)request) ||
				vsfusbd_ctrl_prepare(device))
			{
				// fail to get setup request data
				err = VSFERR_FAIL;
				goto setup_exit;
			}

			if (ctrl_handler->data_size > request->wLength)
			{
				ctrl_handler->data_size = request->wLength;
			}

			if ((request->bRequestType & USB_DIR_MASK) == USB_DIR_OUT)
			{
				if (0 == request->wLength)
				{
					vsfusbd_setup_status_callback((void *)device);
				}
				else
				{
					transact = &ctrl_handler->OUT_transact;
					transact->data_size = ctrl_handler->data_size;
					transact->stream = ctrl_handler->stream;
					transact->cb.param = device;
					transact->cb.on_finish = vsfusbd_setup_status_callback;
					err = vsfusbd_ep_recv(device, transact);
				}
			}
			else
			{
				transact = &ctrl_handler->IN_transact;
				transact->data_size = ctrl_handler->data_size;
				transact->stream = ctrl_handler->stream;
				transact->cb.param = device;
				transact->cb.on_finish = NULL;
				transact->zlp = ctrl_handler->data_size < request->wLength;
				err = vsfusbd_ep_send(device, transact);
				if (!err)
				{
					vsfusbd_setup_status_callback((void*)device);
				}
			}

		setup_exit:
			if (err)
			{
				drv->ep.set_IN_stall(0);
				drv->ep.set_OUT_stall(0);
			}
			break;
		}
#if VSFUSBD_CFG_LP_EN
	case VSFUSBD_INTEVT_WAKEUP:
		if (device->callback.on_WAKEUP != NULL)
		{
			device->callback.on_WAKEUP(device);
		}
		break;
	case VSFUSBD_INTEVT_SUSPEND:
		if (device->callback.on_SUSPEND != NULL)
		{
			device->callback.on_SUSPEND(device);
		}
		drv->suspend();
		break;
	case VSFUSBD_INTEVT_RESUME:
		if (device->callback.on_RESUME != NULL)
		{
			device->callback.on_RESUME(device);
		}
		drv->resume();
		break;
#endif
	case VSFUSBD_INTEVT_SOF:
		if (device->callback.on_SOF != NULL)
		{
			device->callback.on_SOF(device);
		}
		break;
	case VSFUSBD_INTEVT_ATTACH:
		if (device->callback.on_ATTACH != NULL)
		{
			device->callback.on_ATTACH(device);
		}
		break;
	case VSFUSBD_INTEVT_DETACH:
		if (device->callback.on_DETACH != NULL)
		{
			device->callback.on_DETACH(device);
		}
		break;
	default:
		// not error and transact not valid
		if ((evt & VSFUSBD_EVT_ERR_MASK) == VSFUSBD_INTEVT_ERR)
		{
			if (device->callback.on_ERROR != NULL)
			{
				enum interface_usbd_error_t type =
								(enum interface_usbd_error_t)(evt & 0xFF);
				device->callback.on_ERROR(device, type);
			}
		}
		else
		{
			uint8_t ep = evt & VSFUSBD_EVT_EP_MASK;
			uint8_t dir_in = evt & VSFUSBD_EVT_DIR_IN;
			struct vsfusbd_transact_t *transact =
					dir_in ? device->IN_transact[ep] : device->OUT_transact[ep];

			if (transact != NULL)
			{
				switch (evt & VSFUSBD_EVT_EVT_MASK)
				{
				case VSFUSBD_STREAM_CLOSE_IN:
					vsfusbd_ep_cancel_send(device, transact);
					break;
				case VSFUSBD_STREAM_IN:
					if (!transact->idle)
					{
						break;
					}
				case VSFUSBD_STREAM_CLOSE_OUT:
					vsfusbd_ep_cancel_recv(device, transact);
					break;
				case VSFUSBD_STREAM_OUT:
					vsfusbd_transact_out(device, transact);
					break;
				}
			}
			switch (evt & VSFUSBD_EVT_EVT_MASK)
			{
			case VSFUSBD_INTEVT_IN:
				if (device->IN_handler[ep] != NULL)
					device->IN_handler[ep](device, ep);
				break;
			case VSFUSBD_INTEVT_OUT:
				if (device->OUT_handler[ep] != NULL)
					device->OUT_handler[ep](device, ep);
				break;
			}
		}
		break;
	}
	return NULL;
}

vsf_err_t vsfusbd_device_init(struct vsfusbd_device_t *device)
{
	memset(&device->sm, 0, sizeof(device->sm));
	device->sm.init_state.evt_handler = vsfusbd_evt_handler;
	device->sm.user_data = (void*)device;
	return vsfsm_init(&device->sm);
}

vsf_err_t vsfusbd_device_fini(struct vsfusbd_device_t *device)
{
	return vsfsm_post_evt_pending(&device->sm, VSFSM_EVT_FINI);
}

#ifdef VSFCFG_STANDALONE_MODULE
void vsfusbd_modexit(struct vsf_module_t *module)
{
	vsf_bufmgr_free(module->ifs);
	module->ifs = NULL;
}

vsf_err_t vsfusbd_modinit(struct vsf_module_t *module,
								struct app_hwcfg_t const *cfg)
{
	struct vsfusbd_modifs_t *ifs;
	ifs = vsf_bufmgr_malloc(sizeof(struct vsfusbd_modifs_t));
	if (!ifs) return VSFERR_FAIL;
	memset(ifs, 0, sizeof(*ifs));

	ifs->init = vsfusbd_device_init;
	ifs->fini = vsfusbd_device_fini;
	ifs->ep_send = vsfusbd_ep_send;
	ifs->ep_cancel_send = vsfusbd_ep_cancel_send;
	ifs->ep_recv = vsfusbd_ep_recv;
	ifs->ep_cancel_recv = vsfusbd_ep_cancel_recv;
	ifs->set_IN_handler = vsfusbd_set_IN_handler;
	ifs->set_OUT_handler = vsfusbd_set_OUT_handler;
	ifs->get_descriptor = vsfusbd_device_get_descriptor;
	module->ifs = ifs;
	return VSFERR_NONE;
}
#endif
