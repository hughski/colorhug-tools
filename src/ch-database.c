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

#include "config.h"

#include <gio/gio.h>
#include <glib-object.h>
#include <stdlib.h>
#include <sqlite3.h>

#include "ch-database.h"

static void     ch_database_finalize	(GObject     *object);

#define CH_DATABASE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CH_TYPE_DATABASE, ChDatabasePrivate))

struct _ChDatabasePrivate
{
	sqlite3				*db;
	gchar				*uri;
	GFileMonitor			*file_monitor;
};

G_DEFINE_TYPE (ChDatabase, ch_database, G_TYPE_OBJECT)

const gchar *
ch_database_state_to_string (ChDeviceState state)
{
	if (state == CH_DEVICE_STATE_INIT)
		return "init";
	if (state == CH_DEVICE_STATE_CALIBRATED)
		return "calibrated";
	if (state == CH_DEVICE_STATE_ALLOCATED)
		return "allocated";
	return NULL;
}

void
ch_database_set_uri (ChDatabase *database, const gchar *uri)
{
	g_return_if_fail (CH_IS_DATABASE (database));
	g_return_if_fail (uri != NULL);
	g_return_if_fail (database->priv->uri == NULL);
	database->priv->uri = g_strdup (uri);
}

static gboolean
ch_database_load (ChDatabase *database, GError **error)
{
	ChDatabasePrivate *priv = database->priv;
	const gchar *statement;
	gboolean ret = TRUE;
	gchar *error_msg = NULL;
	gint rc;

	/* already open */
	if (priv->db != NULL)
		goto out;

	/* open database */
	g_debug ("trying to open database '%s'", database->priv->uri);
	rc = sqlite3_open (database->priv->uri, &priv->db);
	if (rc != SQLITE_OK) {
		ret = FALSE;
		g_set_error (error, 1, 0,
			     "can't open calibration database: %s",
			     sqlite3_errmsg (priv->db));
		goto out;
	}

	/* create if required */
	rc = sqlite3_exec (priv->db, "SELECT * FROM devices LIMIT 1",
			   NULL, NULL, &error_msg);
	if (rc != SQLITE_OK) {
		g_debug ("creating table to repair: %s", error_msg);
		sqlite3_free (error_msg);
		statement = "CREATE TABLE devices ("
			    "device_id INTEGER PRIMARY KEY AUTOINCREMENT,"
			    "hw_ver INTEGER DEFAULT 0,"
			    "calibrated_date INTEGER,"
			    "state INTEGER,"
			    "order_id INTEGER);";
		sqlite3_exec (priv->db, statement, NULL, NULL, NULL);
		statement = "CREATE TABLE orders ("
			    "order_id INTEGER PRIMARY KEY AUTOINCREMENT,"
			    "name STRING,"
			    "address STRING,"
			    "email STRING,"
			    "tracking_number STRING,"
			    "comment STRING,"
			    "state INTEGER,"
			    "postage INTEGER,"
			    "sent_date INTEGER);";
		sqlite3_exec (priv->db, statement, NULL, NULL, NULL);
	}

	/* check devices has hw_ver */
	rc = sqlite3_exec (priv->db,
			   "SELECT hw_ver FROM devices LIMIT 1",
			   NULL, NULL, &error_msg);
	if (rc != SQLITE_OK) {
		g_debug ("altering table to repair: %s", error_msg);
		sqlite3_free (error_msg);
		statement = "ALTER TABLE devices ADD COLUMN hw_ver INTEGER DEFAULT 0;";
		sqlite3_exec (priv->db, statement, NULL, NULL, NULL);
	}

	/* turn off fsync */
	sqlite3_exec (priv->db, "PRAGMA synchronous=NORMAL", NULL, NULL, NULL);
out:
	return ret;
}

/**
 * ch_database_add_device:
 * @database: a valid #ChDatabase instance
 * @error: A #GError or %NULL
 *
 * Add a new device to the database
 *
 * Return value: The new device serial number or G_MAXUINT32 for error
 **/
guint32
ch_database_add_device (ChDatabase *database, guint hw_ver, GError **error)
{
	ChDatabasePrivate *priv = database->priv;
	gboolean ret;
	gchar *error_msg = NULL;
	gchar *statement = NULL;
	gint rc;
	guint32 id = G_MAXUINT32;

	/* ensure db is loaded */
	ret = ch_database_load (database, error);
	if (!ret)
		goto out;

	/* add newest */
	statement = g_strdup_printf ("INSERT INTO devices (calibrated_date, order_id, hw_ver, state) "
				     "VALUES ('%" G_GINT64_FORMAT "', '', %i, %i);",
				     g_get_real_time (),
				     hw_ver,
				     CH_DEVICE_STATE_INIT);
	rc = sqlite3_exec (priv->db, statement, NULL, NULL, &error_msg);
	if (rc != SQLITE_OK) {
		g_set_error (error, 1, 0,
			     "failed to add entry: %s",
			     sqlite3_errmsg (priv->db));
		goto out;
	}

	/* yay, atomic serial number */
	id = sqlite3_last_insert_rowid (priv->db);
out:
	g_free (statement);
	return id;
}

/**
 * ch_database_device_set_state:
 * @database: a valid #ChDatabase instance
 * @id: the device serial number
 * @state: the #ChDeviceState
 * @error: A #GError or %NULL
 *
 * Changes the state on a device
 *
 * Return value: %TRUE if the new state was set
 **/
gboolean
ch_database_device_set_state (ChDatabase *database,
			      guint32 id,
			      ChDeviceState state,
			      GError **error)
{
	ChDatabasePrivate *priv = database->priv;
	gboolean ret;
	gchar *error_msg = NULL;
	gchar *statement = NULL;
	gint rc;

	/* ensure db is loaded */
	ret = ch_database_load (database, error);
	if (!ret)
		goto out;

	/* set state */
	statement = g_strdup_printf ("UPDATE devices SET state = '%i' WHERE device_id = '%i'",
				     state, id);
	rc = sqlite3_exec (priv->db, statement, NULL, NULL, &error_msg);
	if (rc != SQLITE_OK) {
		ret = FALSE;
		g_set_error (error, 1, 0,
			     "failed to update entry: %s",
			     sqlite3_errmsg (priv->db));
		goto out;
	}
out:
	g_free (statement);
	return ret;
}

static gint
ch_database_device_get_state_cb (void *data, gint argc, gchar **argv, gchar **col_name)
{
	ChDeviceState *state = (ChDeviceState *) data;
	*state = atoi (argv[0]);
	return 0;
}

ChDeviceState
ch_database_device_get_state (ChDatabase *database,
			      guint32 device_id,
			      GError **error)
{
	ChDatabasePrivate *priv = database->priv;
	gboolean ret;
	gchar *error_msg = NULL;
	gchar *statement = NULL;
	gint rc;
	ChDeviceState state = CH_DEVICE_STATE_LAST;

	/* ensure db is loaded */
	ret = ch_database_load (database, error);
	if (!ret)
		goto out;

	/* find */
	statement = g_strdup_printf ("SELECT state "
				     "FROM devices WHERE device_id = '%i';",
				     device_id);
	rc = sqlite3_exec (priv->db,
			   statement,
			   ch_database_device_get_state_cb,
			   &state,
			   &error_msg);
	if (rc != SQLITE_OK) {
		g_set_error (error, 1, 0,
			     "failed to find entry: %s",
			     sqlite3_errmsg (priv->db));
		goto out;
	}
	if (state == CH_DEVICE_STATE_LAST) {
		g_set_error (error, 1, 0,
			     "No devices for id %i", device_id);
		goto out;
	}
out:
	g_free (statement);
	return state;
}
/**
 * ch_database_order_set_tracking:
 * @database: a valid #ChDatabase instance
 * @id: the device serial number
 * @state: the #ChDeviceState
 * @error: A #GError or %NULL
 *
 * Changes the state on a device
 *
 * Return value: %TRUE if the new state was set
 **/
gboolean
ch_database_order_set_tracking (ChDatabase *database,
				guint32 order_id,
				const gchar *tracking,
				GError **error)
{
	ChDatabasePrivate *priv = database->priv;
	gboolean ret;
	gchar *error_msg = NULL;
	gchar *statement = NULL;
	gint rc;

	/* ensure db is loaded */
	ret = ch_database_load (database, error);
	if (!ret)
		goto out;

	/* set state */
	statement = sqlite3_mprintf ("UPDATE orders SET tracking_number = '%q', sent_date = '%" G_GINT64_FORMAT "' WHERE order_id = '%i'",
				     tracking,
				     g_get_real_time (),
				     order_id);
	rc = sqlite3_exec (priv->db, statement, NULL, NULL, &error_msg);
	if (rc != SQLITE_OK) {
		ret = FALSE;
		g_set_error (error, 1, 0,
			     "failed to update order: %s",
			     sqlite3_errmsg (priv->db));
		goto out;
	}
out:
	sqlite3_free (statement);
	return ret;
}

/**
 * ch_database_order_set_comment:
 * @database: a valid #ChDatabase instance
 * @id: the device serial number
 * @comment: the #ChDeviceState
 * @error: A #GError or %NULL
 *
 * Changes the state on a device
 *
 * Return value: %TRUE if the new state was set
 **/
gboolean
ch_database_order_set_comment (ChDatabase *database,
			       guint32 order_id,
			       const gchar *comment,
			       GError **error)
{
	ChDatabasePrivate *priv = database->priv;
	gboolean ret;
	gchar *error_msg = NULL;
	gchar *statement = NULL;
	gint rc;

	/* ensure db is loaded */
	ret = ch_database_load (database, error);
	if (!ret)
		goto out;

	/* set state */
	statement = sqlite3_mprintf ("UPDATE orders SET comment = '%q' WHERE order_id = '%i'",
				     comment,
				     order_id);
	rc = sqlite3_exec (priv->db, statement, NULL, NULL, &error_msg);
	if (rc != SQLITE_OK) {
		ret = FALSE;
		g_set_error (error, 1, 0,
			     "failed to update order: %s",
			     sqlite3_errmsg (priv->db));
		goto out;
	}
out:
	sqlite3_free (statement);
	return ret;
}

/**
 * ch_database_order_set_state:
 * @database: a valid #ChDatabase instance
 * @id: the device serial number
 * @comment: the #ChDeviceState
 * @error: A #GError or %NULL
 *
 * Changes the state on an order.
 *
 * Return value: %TRUE if the new state was set
 **/
gboolean
ch_database_order_set_state (ChDatabase *database,
			     guint32 order_id,
			     ChOrderState state,
			     GError **error)
{
	ChDatabasePrivate *priv = database->priv;
	gboolean ret;
	gchar *error_msg = NULL;
	gchar *statement = NULL;
	gint rc;

	/* ensure db is loaded */
	ret = ch_database_load (database, error);
	if (!ret)
		goto out;

	/* set state */
	statement = sqlite3_mprintf ("UPDATE orders SET state = '%i' WHERE order_id = '%i'",
				     state,
				     order_id);
	rc = sqlite3_exec (priv->db, statement, NULL, NULL, &error_msg);
	if (rc != SQLITE_OK) {
		ret = FALSE;
		g_set_error (error, 1, 0,
			     "failed to update order: %s",
			     sqlite3_errmsg (priv->db));
		goto out;
	}
out:
	sqlite3_free (statement);
	return ret;
}

/**
 * ch_database_device_set_order_id:
 * @database: a valid #ChDatabase instance
 * @id: the device serial number
 * @state: the #ChDeviceState
 * @error: A #GError or %NULL
 *
 * Changes the order-id on a device
 *
 * Return value: %TRUE if the new state was set
 **/
gboolean
ch_database_device_set_order_id (ChDatabase *database,
				 guint32 device_id,
				 guint32 order_id,
				 GError **error)
{
	ChDatabasePrivate *priv = database->priv;
	gboolean ret;
	gchar *error_msg = NULL;
	gchar *statement = NULL;
	gint rc;

	/* ensure db is loaded */
	ret = ch_database_load (database, error);
	if (!ret)
		goto out;

	/* set state */
	statement = g_strdup_printf ("UPDATE devices SET order_id = '%i' WHERE device_id = '%i'",
				     order_id, device_id);
	rc = sqlite3_exec (priv->db, statement, NULL, NULL, &error_msg);
	if (rc != SQLITE_OK) {
		ret = FALSE;
		g_set_error (error, 1, 0,
			     "failed to update entry: %s",
			     sqlite3_errmsg (priv->db));
		goto out;
	}
out:
	g_free (statement);
	return ret;
}

static gint
ch_database_device_get_number_cb (void *data, gint argc, gchar **argv, gchar **col_name)
{
	guint *len = (guint *) data;
	(*len)++;
	return 0;
}

/**
 * ch_database_device_get_number:
 * @database: a valid #ChDatabase instance
 * @state: A #ChDeviceState
 * @error: A #GError or %NULL
 *
 * Finds the oldest device of a known state
 *
 * Return value: The device serial number or G_MAXUINT32 for error
 **/
guint
ch_database_device_get_number (ChDatabase *database,
			       ChDeviceState state,
			       guint hw_ver,
			       GError **error)
{
	ChDatabasePrivate *priv = database->priv;
	gboolean ret;
	gchar *error_msg = NULL;
	gchar *statement = NULL;
	gint rc;
	guint len = 0;

	/* ensure db is loaded */
	ret = ch_database_load (database, error);
	if (!ret) {
		len = G_MAXUINT32;
		goto out;
	}

	/* find */
	statement = g_strdup_printf ("SELECT device_id "
				     "FROM devices WHERE state = '%i' and hw_ver = '%i';", state, hw_ver);
	rc = sqlite3_exec (priv->db,
			   statement,
			   ch_database_device_get_number_cb,
			   &len,
			   &error_msg);
	if (rc != SQLITE_OK) {
		len = G_MAXUINT32;
		g_set_error (error, 1, 0,
			     "failed to find entry: %s",
			     sqlite3_errmsg (priv->db));
		goto out;
	}
out:
	g_free (statement);
	return len;
}

static gint
ch_database_device_find_oldest_cb (void *data, gint argc, gchar **argv, gchar **col_name)
{
	guint32 *id = (guint32 *) data;
	*id = atoi (argv[0]);
	return 0;
}

/**
 * ch_database_device_find_oldest:
 * @database: a valid #ChDatabase instance
 * @state: A #ChDeviceState
 * @error: A #GError or %NULL
 *
 * Finds the oldest device of a known state
 *
 * Return value: The device serial number or G_MAXUINT32 for error
 **/
guint32
ch_database_device_find_oldest (ChDatabase *database,
				ChDeviceState state,
				guint hw_ver,
				GError **error)
{
	ChDatabasePrivate *priv = database->priv;
	gboolean ret;
	gchar *error_msg = NULL;
	gchar *statement = NULL;
	gint rc;
	guint32 id = G_MAXUINT32;

	/* ensure db is loaded */
	ret = ch_database_load (database, error);
	if (!ret)
		goto out;

	/* find */
	statement = g_strdup_printf ("SELECT device_id "
				     "FROM devices WHERE state = '%i' AND hw_ver = '%i' "
				     "ORDER BY device_id ASC LIMIT 1", state, hw_ver);
	rc = sqlite3_exec (priv->db,
			   statement,
			   ch_database_device_find_oldest_cb,
			   &id,
			   &error_msg);
	if (rc != SQLITE_OK) {
		g_set_error (error, 1, 0,
			     "failed to find entry: %s",
			     sqlite3_errmsg (priv->db));
		goto out;
	}
	if (id == G_MAXUINT32) {
		g_set_error (error, 1, 0,
			     "No devices in state %s",
			     ch_database_state_to_string (state));
		goto out;
	}
out:
	g_free (statement);
	return id;
}

static gint
ch_database_order_get_device_ids_cb (void *data, gint argc, gchar **argv, gchar **col_name)
{
	GArray *array = (GArray *) data;
	guint32 tmp;
	tmp = atoi (argv[0]);
	g_array_append_val (array, tmp);
	return 0;
}

GArray *
ch_database_order_get_device_ids (ChDatabase *database,
				 guint32 order_id,
				 GError **error)
{
	ChDatabasePrivate *priv = database->priv;
	GArray *array = NULL;
	GArray *array_tmp = NULL;
	gboolean ret;
	gchar *error_msg = NULL;
	gchar *statement = NULL;
	gint rc;

	/* ensure db is loaded */
	ret = ch_database_load (database, error);
	if (!ret)
		goto out;

	/* find */
	array_tmp = g_array_new (FALSE, FALSE, sizeof (guint32));
	statement = g_strdup_printf ("SELECT device_id "
				     "FROM devices WHERE order_id = '%i' "
				     "ORDER BY device_id DESC", order_id);
	rc = sqlite3_exec (priv->db,
			   statement,
			   ch_database_order_get_device_ids_cb,
			   array_tmp,
			   &error_msg);
	if (rc != SQLITE_OK) {
		g_set_error (error, 1, 0,
			     "failed to find entry: %s",
			     sqlite3_errmsg (priv->db));
		goto out;
	}
	if (array_tmp->len == 0) {
		g_set_error (error, 1, 0,
			     "No devices for order %i", order_id);
		goto out;
	}
	array = g_array_ref (array_tmp);
out:
	if (array_tmp != NULL)
		g_array_unref (array_tmp);
	g_free (statement);
	return array;
}

static gint
ch_database_order_get_comment_cb (void *data, gint argc, gchar **argv, gchar **col_name)
{
	gchar **comment = (gchar **) data;
	*comment = g_strdup (argv[0]);
	return 0;
}

gchar *
ch_database_order_get_comment (ChDatabase *database,
				guint32 order_id,
				GError **error)
{
	ChDatabasePrivate *priv = database->priv;
	gboolean ret;
	gchar *error_msg = NULL;
	gchar *statement = NULL;
	gint rc;
	gchar *comment = NULL;

	/* ensure db is loaded */
	ret = ch_database_load (database, error);
	if (!ret)
		goto out;

	/* find */
	statement = g_strdup_printf ("SELECT comment "
				     "FROM orders WHERE order_id = '%i';", order_id);
	rc = sqlite3_exec (priv->db,
			   statement,
			   ch_database_order_get_comment_cb,
			   &comment,
			   &error_msg);
	if (rc != SQLITE_OK) {
		g_set_error (error, 1, 0,
			     "failed to find entry: %s",
			     sqlite3_errmsg (priv->db));
		goto out;
	}

	/* don't return NULL for success */
	if (comment == NULL)
		comment = g_strdup ("");
out:
	g_free (statement);
	return comment;
}

static gint
ch_database_get_all_orders_cb (void *data, gint argc, gchar **argv, gchar **col_name)
{
	GPtrArray *array = (GPtrArray *) data;
	ChDatabaseOrder *order;
	order = g_new0 (ChDatabaseOrder, 1);
	order->order_id = atoi (argv[0]);
	order->name = g_strdup (argv[1]);
	order->address = g_strdup (argv[2]);
	order->email = g_strdup (argv[3]);
	order->postage = atoi (argv[4]);
	order->tracking_number = g_strdup (argv[5]);
	order->sent_date = g_ascii_strtoll (argv[6], NULL, 10);
	order->comment = g_strdup (argv[7]);
	order->state = argv[8] != NULL ? atoi (argv[8]) : 0;
	g_ptr_array_add (array, order);
	return 0;
}

/**
 * ch_database_get_all_orders:
 * @database: a valid #ChDatabase instance
 * @error: A #GError or %NULL
 *
 * xxxxxxxxxxxxxxxx
 *
 * Return value: xxxxxxxxxxxxxxxxxxxxxxxxx
 **/
GPtrArray *
ch_database_get_all_orders (ChDatabase *database,
			    GError **error)
{
	ChDatabasePrivate *priv = database->priv;
	gboolean ret;
	gchar *error_msg = NULL;
	gchar *statement = NULL;
	gint rc;
	GPtrArray *orders = NULL;
	GPtrArray *orders_tmp = NULL;

	/* ensure db is loaded */
	ret = ch_database_load (database, error);
	if (!ret)
		goto out;

	/* find */
	orders_tmp = g_ptr_array_new ();
	statement = g_strdup ("SELECT order_id, name, address, email, "
			      "postage, tracking_number, sent_date, "
			      "comment, state "
			      "FROM orders "
			      "ORDER BY order_id DESC LIMIT 1500");
	rc = sqlite3_exec (priv->db,
			   statement,
			   ch_database_get_all_orders_cb,
			   orders_tmp,
			   &error_msg);
	if (rc != SQLITE_OK) {
		g_set_error (error, 1, 0,
			     "failed to find entry: %s",
			     sqlite3_errmsg (priv->db));
		goto out;
	}

	/* success */
	orders = g_ptr_array_ref (orders_tmp);
out:
	if (orders_tmp != NULL)
		g_ptr_array_unref (orders_tmp);
	g_free (statement);
	return orders;
}

/**
 * ch_database_add_order:
 * @database: a valid #ChDatabase instance
 * @error: A #GError or %NULL
 *
 * Add a new order to the database
 *
 * Return value: The new device serial number or 0 for error
 **/
guint32
ch_database_add_order (ChDatabase *database,
		       const gchar *name,
		       const gchar *address,
		       const gchar *email,
		       ChShippingKind postage,
		       GError **error)
{
	ChDatabasePrivate *priv = database->priv;
	gboolean ret;
	gchar *error_msg = NULL;
	gchar *statement = NULL;
	gint rc;
	guint32 id = G_MAXUINT32;

	/* ensure db is loaded */
	ret = ch_database_load (database, error);
	if (!ret)
		goto out;

	/* add newest */
	statement = sqlite3_mprintf ("INSERT INTO orders (name, address, email, postage, tracking_number, sent_date) "
				     "VALUES ('%q', '%q', '%q', '%i', '', '');",
				     name,
				     address,
				     email,
				     postage);
	rc = sqlite3_exec (priv->db, statement, NULL, NULL, &error_msg);
	if (rc != SQLITE_OK) {
		g_set_error (error, 1, 0,
			     "failed to add entry: %s",
			     sqlite3_errmsg (priv->db));
		goto out;
	}

	/* yay, atomic serial number */
	id = sqlite3_last_insert_rowid (priv->db);
out:
	sqlite3_free (statement);
	return id;
}

static void
ch_database_class_init (ChDatabaseClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = ch_database_finalize;
	g_type_class_add_private (klass, sizeof (ChDatabasePrivate));
}

static void
ch_database_init (ChDatabase *database)
{
	database->priv = CH_DATABASE_GET_PRIVATE (database);
}

static void
ch_database_finalize (GObject *object)
{
	ChDatabase *database = CH_DATABASE (object);
	ChDatabasePrivate *priv = database->priv;

	g_free (priv->uri);
	if (priv->db != NULL)
		sqlite3_close (priv->db);
	if (priv->file_monitor != NULL)
		g_object_unref (priv->file_monitor);

	G_OBJECT_CLASS (ch_database_parent_class)->finalize (object);
}

ChDatabase *
ch_database_new (void)
{
	ChDatabase *database;
	database = g_object_new (CH_TYPE_DATABASE, NULL);
	return CH_DATABASE (database);
}

