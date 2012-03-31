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

#ifndef CH_CELL_RENDERER_POSTAGE_H
#define CH_CELL_RENDERER_POSTAGE_H

#include <glib-object.h>
#include <gtk/gtk.h>

#define CH_TYPE_CELL_RENDERER_POSTAGE		(ch_cell_renderer_postage_get_type())
#define CH_CELL_RENDERER_POSTAGE(obj)		(G_TYPE_CHECK_INSTANCE_CAST((obj), CH_TYPE_CELL_RENDERER_POSTAGE, ChCellRendererPostage))
#define CH_CELL_RENDERER_POSTAGE_CLASS(cls)	(G_TYPE_CHECK_CLASS_CAST((cls), CH_TYPE_CELL_RENDERER_POSTAGE, ChCellRendererPostageClass))
#define CH_IS_CELL_RENDERER_POSTAGE(obj)	(G_TYPE_CHECK_INSTANCE_TYPE((obj), CH_TYPE_CELL_RENDERER_POSTAGE))
#define CH_IS_CELL_RENDERER_POSTAGE_CLASS(cls)	(G_TYPE_CHECK_CLASS_TYPE((cls), CH_TYPE_CELL_RENDERER_POSTAGE))
#define CH_CELL_RENDERER_POSTAGE_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS((obj), CH_TYPE_CELL_RENDERER_POSTAGE, ChCellRendererPostageClass))

G_BEGIN_DECLS

typedef struct _ChCellRendererPostage		ChCellRendererPostage;
typedef struct _ChCellRendererPostageClass	ChCellRendererPostageClass;

struct _ChCellRendererPostage
{
	GtkCellRendererText	 parent;
	guint			 value;
	const gchar		*markup;
};

struct _ChCellRendererPostageClass
{
	GtkCellRendererTextClass parent_class;
};

GType		 ch_cell_renderer_postage_get_type	(void);
GtkCellRenderer	*ch_cell_renderer_postage_new		(void);

G_END_DECLS

#endif /* CH_CELL_RENDERER_POSTAGE_H */

