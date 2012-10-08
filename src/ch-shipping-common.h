/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef CH_SHIPPING_COMMON_H
#define CH_SHIPPING_COMMON_H

#include <glib-object.h>

G_BEGIN_DECLS

typedef enum {
	CH_SHIPPING_POSTAGE_UNKNOWN,
	CH_SHIPPING_POSTAGE_UK,
	CH_SHIPPING_POSTAGE_EUROPE,
	CH_SHIPPING_POSTAGE_WORLD,
	CH_SHIPPING_POSTAGE_UK_SIGNED,
	CH_SHIPPING_POSTAGE_EUROPE_SIGNED,
	CH_SHIPPING_POSTAGE_WORLD_SIGNED,
	CH_SHIPPING_POSTAGE_XUK,
	CH_SHIPPING_POSTAGE_XEUROPE,
	CH_SHIPPING_POSTAGE_XWORLD,
	CH_SHIPPING_POSTAGE_XUK_SIGNED,
	CH_SHIPPING_POSTAGE_XEUROPE_SIGNED,
	CH_SHIPPING_POSTAGE_XWORLD_SIGNED,
	CH_SHIPPING_POSTAGE_LAST
} ChShippingPostage;

typedef enum {
	CH_DEVICE_STATE_INIT,
	CH_DEVICE_STATE_CALIBRATED,
	CH_DEVICE_STATE_ALLOCATED,
	CH_DEVICE_STATE_LAST
} ChDeviceState;

typedef enum {
	CH_ORDER_STATE_NEW,
	CH_ORDER_STATE_PRINTED,
	CH_ORDER_STATE_SENT,
	CH_ORDER_STATE_REFUNDED,
	CH_ORDER_STATE_TO_BE_PRINTED,
	CH_ORDER_STATE_LAST
} ChOrderState;

const gchar	*ch_shipping_postage_to_string	(ChShippingPostage postage);
const gchar	*ch_shipping_postage_to_service	(ChShippingPostage postage);
guint		 ch_shipping_postage_to_price	(ChShippingPostage postage);
guint		 ch_shipping_device_to_price	(ChShippingPostage postage);
gboolean	 ch_shipping_send_email		(const gchar	*sender,
						 const gchar	*recipient,
						 const gchar	*subject,
						 const gchar	*body,
						 GError		**error);
G_END_DECLS

#endif /* CH_SHIPPING_COMMON_H */
