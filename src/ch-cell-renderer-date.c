/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009-2012 Richard Hughes <richard@hughsie.com>
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

#include "ch-cell-renderer-date.h"

enum {
	PROP_0,
	PROP_VALUE
};

G_DEFINE_TYPE (ChCellRendererDate, ch_cell_renderer_date, GTK_TYPE_CELL_RENDERER_TEXT)

static gpointer parent_class = NULL;

static void
ch_cell_renderer_date_get_property (GObject *object, guint param_id,
				    GValue *value, GParamSpec *pspec)
{
	ChCellRendererDate *cru = CH_CELL_RENDERER_DATE (object);

	switch (param_id) {
	case PROP_VALUE:
		g_value_set_int64 (value, cru->value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	}
}

static void
ch_cell_renderer_date_set_property (GObject *object, guint param_id,
				    const GValue *value, GParamSpec *pspec)
{
	ChCellRendererDate *cru = CH_CELL_RENDERER_DATE (object);
	GDateTime *datetime;

	switch (param_id) {
	case PROP_VALUE:
		cru->value = g_value_get_int64 (value);
		g_free (cru->markup);
		datetime = g_date_time_new_from_unix_utc (cru->value / G_USEC_PER_SEC);
		g_assert (datetime != NULL);
		cru->markup = g_date_time_format (datetime, "%F");
		g_date_time_unref (datetime);

		/* if the date is zero, we hide the markup */
		g_object_set (cru,
			      "markup", cru->markup,
			      "visible", (cru->value != 0),
			      NULL);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	}
}

/**
 * ch_cell_renderer_finalize:
 * @object: The object to finalize
 **/
static void
ch_cell_renderer_finalize (GObject *object)
{
	ChCellRendererDate *cru;
	cru = CH_CELL_RENDERER_DATE (object);
	g_free (cru->markup);
	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
ch_cell_renderer_date_class_init (ChCellRendererDateClass *class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (class);
	object_class->finalize = ch_cell_renderer_finalize;

	parent_class = g_type_class_peek_parent (class);

	object_class->get_property = ch_cell_renderer_date_get_property;
	object_class->set_property = ch_cell_renderer_date_set_property;

	g_object_class_install_property (object_class, PROP_VALUE,
					 g_param_spec_int64 ("value", "VALUE",
					 "VALUE", 0, G_MAXINT64, 0, G_PARAM_READWRITE));
}

/**
 * ch_cell_renderer_date_init:
 **/
static void
ch_cell_renderer_date_init (ChCellRendererDate *cru)
{
	cru->value = 0;
	cru->markup = NULL;
}

/**
 * ch_cell_renderer_date_new:
 **/
GtkCellRenderer *
ch_cell_renderer_date_new (void)
{
	return g_object_new (CH_TYPE_CELL_RENDERER_DATE, NULL);
}

