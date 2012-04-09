/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2011-2012 Richard Hughes <richard@hughsie.com>
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

#ifndef __CH_DATABASE_H
#define __CH_DATABASE_H

#include <glib-object.h>

#include "ch-shipping-common.h"

G_BEGIN_DECLS

#define CH_TYPE_DATABASE		(ch_database_get_type ())
#define CH_DATABASE(o)			(G_TYPE_CHECK_INSTANCE_CAST ((o), CH_TYPE_DATABASE, ChDatabase))
#define CH_IS_DATABASE(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), CH_TYPE_DATABASE))

typedef struct _ChDatabasePrivate	ChDatabasePrivate;
typedef struct _ChDatabase		ChDatabase;
typedef struct _ChDatabaseClass	ChDatabaseClass;

struct _ChDatabase
{
	 GObject			 parent;
	 ChDatabasePrivate		*priv;
};

struct _ChDatabaseClass
{
	void		(*changed)	(ChDatabase		*database);
	GObjectClass			 parent_class;
};

typedef struct {
	ChShippingPostage postage;
	gchar		*address;
	gchar		*email;
	gchar		*name;
	gchar		*tracking_number;
	gint64		 sent_date;
	guint32		 order_id;
	gchar		*comment;
	ChOrderState	 state;
} ChDatabaseOrder;

GType		 ch_database_get_type		(void);
ChDatabase	*ch_database_new		(void);
void		 ch_database_set_uri		(ChDatabase	*database,
						 const gchar	*uri);
const gchar	*ch_database_state_to_string	(ChDeviceState state);
guint32		 ch_database_add_device		(ChDatabase	*database,
						 GError		**error);
gboolean	 ch_database_device_set_order_id (ChDatabase	*database,
						 guint32	 device_id,
						 guint32	 order_id,
						 GError		**error);
gboolean	 ch_database_device_set_state	(ChDatabase	*database,
						 guint32	 device_id,
						 ChDeviceState	 state,
						 GError		**error);
ChDeviceState	 ch_database_device_get_state	(ChDatabase	*database,
						 guint32	 device_id,
						 GError		**error);
gboolean	 ch_database_order_set_tracking	(ChDatabase	*database,
						 guint32	 order_id,
						 const gchar	*tracking,
						 GError		**error);
gboolean	 ch_database_order_set_comment	(ChDatabase	*database,
						 guint32	 order_id,
						 const gchar	*comment,
						 GError		**error);
gboolean	 ch_database_order_set_state	(ChDatabase	*database,
						 guint32	 order_id,
						 ChOrderState	 state,
						 GError		**error);
gchar		*ch_database_order_get_comment	(ChDatabase	*database,
						 guint32	 order_id,
						 GError		**error);
guint32		 ch_database_device_find_oldest	(ChDatabase	*database,
						 ChDeviceState state,
						 GError		**error);
guint32		 ch_database_order_get_device_id (ChDatabase	*database,
						 guint32	 order_id,
						 GError		**error);
guint		 ch_database_device_get_number	(ChDatabase	*database,
						 ChDeviceState state,
						 GError		**error);
GPtrArray	*ch_database_get_all_orders	(ChDatabase	*database,
						 GError		**error);
guint32		 ch_database_add_order		(ChDatabase	*database,
						 const gchar	*name,
						 const gchar	*address,
						 const gchar	*email,
						 ChShippingPostage postage,
						 GError		**error);
GPtrArray	*ch_database_get_queue		(ChDatabase	*database,
						 GError		**error);
guint		 ch_database_queue_add		(ChDatabase	*database,
						 const gchar	*email,
						 GError		**error);
gboolean	 ch_database_queue_promote	(ChDatabase	*database,
						 const gchar	*email,
						 GError		**error);
gboolean	 ch_database_queue_remove	(ChDatabase	*database,
						 const gchar	*email,
						 GError		**error);

G_END_DECLS

#endif /* __CH_DATABASE_H */

