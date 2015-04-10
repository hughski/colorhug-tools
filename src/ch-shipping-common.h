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
	CH_SHIPPING_KIND_UNKNOWN,
	__UNUSED_CH_SHIPPING_KIND_CH2_UK,
	__UNUSED_CH_SHIPPING_KIND_CH2_EUROPE,
	__UNUSED_CH_SHIPPING_KIND_CH2_WORLD,
	CH_SHIPPING_KIND_CH2_UK_SIGNED,
	CH_SHIPPING_KIND_CH2_EUROPE_SIGNED,
	CH_SHIPPING_KIND_CH2_WORLD_SIGNED,
	CH_SHIPPING_KIND_CH1_UK,
	CH_SHIPPING_KIND_CH1_EUROPE,
	CH_SHIPPING_KIND_CH1_WORLD,
	CH_SHIPPING_KIND_CH1_UK_SIGNED,
	CH_SHIPPING_KIND_CH1_EUROPE_SIGNED,
	CH_SHIPPING_KIND_CH1_WORLD_SIGNED,
	CH_SHIPPING_KIND_STRAP_UK,
	CH_SHIPPING_KIND_STRAP_EUROPE,
	CH_SHIPPING_KIND_STRAP_WORLD,
	CH_SHIPPING_KIND_ALS_UK,
	CH_SHIPPING_KIND_ALS_EUROPE,
	CH_SHIPPING_KIND_ALS_WORLD,
	CH_SHIPPING_KIND_LAST
} ChShippingKind;

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

const gchar	*ch_shipping_kind_to_string	(ChShippingKind postage);
const gchar	*ch_shipping_kind_to_service	(ChShippingKind postage);
gdouble		 ch_shipping_kind_to_price	(ChShippingKind postage);
guint		 ch_shipping_device_to_price	(ChShippingKind postage);
gboolean	 ch_shipping_send_email		(const gchar	*sender,
						 const gchar	*recipient,
						 const gchar	*subject,
						 const gchar	*body,
						 const gchar	*authtoken,
						 GError		**error);
GString		*ch_shipping_string_load	(const gchar	*filename,
						 GError		**error);
guint		 ch_shipping_string_replace	(GString	*string,
						 const gchar	*search,
						 const gchar	*replace);
gboolean	 ch_shipping_print_latex_doc	(const gchar	*str,
						 const gchar	*printer,
						 GError		**error);
gboolean	 ch_shipping_print_svg_doc	(const gchar	*str,
						 const gchar	*printer,
						 GError		**error);

G_END_DECLS

#endif /* CH_SHIPPING_COMMON_H */
