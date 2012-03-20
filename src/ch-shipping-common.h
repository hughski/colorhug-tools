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
	CH_SHIPPING_POSTAGE_LAST
} ChShippingPostage;

const gchar	*ch_shipping_postage_to_string	(ChShippingPostage postage);

G_END_DECLS

#endif /* CH_SHIPPING_COMMON_H */
