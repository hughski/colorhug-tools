/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2010 Richard Hughes <richard@hughsie.com>
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

#ifndef __CH_SAMPLE_WINDOW_H
#define __CH_SAMPLE_WINDOW_H

#include <glib-object.h>
#include <gtk/gtk.h>
#include <colord.h>

G_BEGIN_DECLS

#define CH_TYPE_SAMPLE_WINDOW		(ch_sample_window_get_type ())
#define CH_SAMPLE_WINDOW(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), CH_TYPE_SAMPLE_WINDOW, ChSampleWindow))
#define CH_IS_SAMPLE_WINDOW(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), CH_TYPE_SAMPLE_WINDOW))

typedef struct _ChSampleWindowPrivate	ChSampleWindowPrivate;
typedef struct _ChSampleWindow		ChSampleWindow;
typedef struct _ChSampleWindowClass	ChSampleWindowClass;

struct _ChSampleWindow
{
	 GtkWindow			 parent;
	 ChSampleWindowPrivate		*priv;
};

struct _ChSampleWindowClass
{
	GtkWindowClass			 parent_class;
};

GType		 ch_sample_window_get_type		(void);
GtkWindow	*ch_sample_window_new			(void);
void		 ch_sample_window_set_color		(ChSampleWindow		*sample_window,
							 const CdColorRGB	*color);
void		 ch_sample_window_set_fraction		(ChSampleWindow		*sample_window,
							 gdouble		 fraction);

G_END_DECLS

#endif /* __CH_SAMPLE_WINDOW_H */

