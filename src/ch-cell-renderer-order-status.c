/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008-2009 Richard Hughes <richard@hughsie.com>
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

#include "config.h"

#include <glib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "ch-shipping-common.h"
#include "ch-cell-renderer-order-status.h"

enum {
	PROP_0,
	PROP_VALUE,
};

#define CH_CELL_RENDERER_ORDER_STATUS_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), CH_TYPE_CELL_RENDERER_ORDER_STATUS, ChCellRendererOrderStatusPrivate))

struct _ChCellRendererOrderStatusPrivate
{
	ChOrderState		 value;
};

G_DEFINE_TYPE (ChCellRendererOrderStatus, ch_cell_renderer_order_status, GTK_TYPE_CELL_RENDERER_PIXBUF)

static gpointer parent_class = NULL;

static void
ch_cell_renderer_order_status_get_property (GObject *object, guint param_id,
				     GValue *value, GParamSpec *pspec)
{
	ChCellRendererOrderStatus *cru = CH_CELL_RENDERER_ORDER_STATUS (object);

	switch (param_id) {
	case PROP_VALUE:
		g_value_set_uint (value, cru->priv->value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	}
}

static void
ch_cell_renderer_order_status_set_property (GObject *object, guint param_id,
				     const GValue *value, GParamSpec *pspec)
{
	ChCellRendererOrderStatus *cru = CH_CELL_RENDERER_ORDER_STATUS (object);

	switch (param_id) {
	case PROP_VALUE:
		cru->priv->value = g_value_get_uint (value);
		switch (cru->priv->value) {
		case CH_ORDER_STATE_NEW:
			g_object_set (cru, "icon-name", "dialog-information", NULL);
			break;
		case CH_ORDER_STATE_PRINTED:
			g_object_set (cru, "icon-name", "printer", NULL);
			break;
		case CH_ORDER_STATE_TO_BE_PRINTED:
			g_object_set (cru, "icon-name", "printer-network", NULL);
			break;
		case CH_ORDER_STATE_SENT:
			g_object_set (cru, "icon-name", "colorimeter-colorhug", NULL);
			break;
		case CH_ORDER_STATE_REFUNDED:
			g_object_set (cru, "icon-name", "mail-forward", NULL);
			break;
		default:
			g_object_set (cru, "icon-name", NULL, NULL);
			break;
		}
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	}
}

static void
ch_cell_renderer_order_status_class_init (ChCellRendererOrderStatusClass *class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (class);

	parent_class = g_type_class_peek_parent (class);

	object_class->get_property = ch_cell_renderer_order_status_get_property;
	object_class->set_property = ch_cell_renderer_order_status_set_property;

	g_object_class_install_property (object_class, PROP_VALUE,
					 g_param_spec_uint ("value", "VALUE",
					 "VALUE", 0, G_MAXUINT, 0, G_PARAM_READWRITE));

	g_type_class_add_private (object_class, sizeof (ChCellRendererOrderStatusPrivate));
}

static void
ch_cell_renderer_order_status_init (ChCellRendererOrderStatus *cru)
{
	cru->priv = CH_CELL_RENDERER_ORDER_STATUS_GET_PRIVATE (cru);
}

GtkCellRenderer *
ch_cell_renderer_order_status_new (void)
{
	return g_object_new (CH_TYPE_CELL_RENDERER_ORDER_STATUS, NULL);
}
