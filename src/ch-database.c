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

/**
 * ChDatabasePrivate:
 *
 * Private #ChDatabase data
 **/
struct _ChDatabasePrivate
{
	sqlite3				*db;
	gchar				*uri;
};

G_DEFINE_TYPE (ChDatabase, ch_database, G_TYPE_OBJECT)

/**
 * ch_database_state_to_string:
 **/
const gchar *
ch_database_state_to_string (ChDatabaseState state)
{
	if (state == CH_DATABASE_STATE_INIT)
		return "init";
	if (state == CH_DATABASE_STATE_CALIBRATED)
		return "calibrated";
	if (state == CH_DATABASE_STATE_ALLOCATED)
		return "allocated";
	return NULL;
}

/**
 * ch_database_set_uri:
 **/
void
ch_database_set_uri (ChDatabase *database, const gchar *uri)
{
	g_return_if_fail (CH_IS_DATABASE (database));
	g_return_if_fail (uri != NULL);
	g_return_if_fail (database->priv->uri == NULL);
	database->priv->uri = g_strdup (uri);
}

/**
 * ch_database_load:
 **/
static gboolean
ch_database_load (ChDatabase *database, GError **error)
{
	ChDatabasePrivate *priv = database->priv;
	const gchar *statement;
	gboolean ret = TRUE;
	gchar *error_msg = NULL;
	GFile *file = NULL;
	gint rc;

	/* already open */
	if (priv->db != NULL)
		goto out;

	/* open database */
	file = g_file_new_for_path (database->priv->uri);
	ret = g_file_query_exists (file, NULL);
	if (!ret) {
		ret = g_file_make_directory_with_parents (file, NULL, error);
		if (!ret)
			goto out;
	}
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
			    "postage INTEGER,"
			    "sent_date INTEGER);";
		sqlite3_exec (priv->db, statement, NULL, NULL, NULL);
		statement = "CREATE TABLE queue ("
			    "email STRING,"
			    "added INTEGER);";
		sqlite3_exec (priv->db, statement, NULL, NULL, NULL);
	}
out:
	if (file != NULL)
		g_object_unref (file);
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
ch_database_add_device (ChDatabase *database, GError **error)
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
	statement = g_strdup_printf ("INSERT INTO devices (calibrated_date, order_id, state) "
				     "VALUES ('%" G_GINT64_FORMAT "', '', %i);",
				     g_get_real_time (),
				     CH_DATABASE_STATE_INIT);
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
 * @state: the #ChDatabaseState
 * @error: A #GError or %NULL
 *
 * Changes the state on a device
 *
 * Return value: %TRUE if the new state was set
 **/
gboolean
ch_database_device_set_state (ChDatabase *database,
			      guint32 id,
			      ChDatabaseState state,
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

/**
 * ch_database_order_set_tracking:
 * @database: a valid #ChDatabase instance
 * @id: the device serial number
 * @state: the #ChDatabaseState
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
 * ch_database_device_set_order_id:
 * @database: a valid #ChDatabase instance
 * @id: the device serial number
 * @state: the #ChDatabaseState
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

/**
 * ch_database_device_get_number_cb:
 **/
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
 * @state: A #ChDatabaseState
 * @error: A #GError or %NULL
 *
 * Finds the oldest device of a known state
 *
 * Return value: The device serial number or G_MAXUINT32 for error
 **/
guint
ch_database_device_get_number (ChDatabase *database,
			       ChDatabaseState state,
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
				     "FROM devices WHERE state = '%i';", state);
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

/**
 * ch_database_device_find_oldest_cb:
 **/
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
 * @state: A #ChDatabaseState
 * @error: A #GError or %NULL
 *
 * Finds the oldest device of a known state
 *
 * Return value: The device serial number or G_MAXUINT32 for error
 **/
guint32
ch_database_device_find_oldest (ChDatabase *database,
				ChDatabaseState state,
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
				     "FROM devices WHERE state = '%i' "
				     "ORDER BY device_id ASC LIMIT 1", state);
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

/**
 * ch_database_order_get_device_id_cb:
 **/
static gint
ch_database_order_get_device_id_cb (void *data, gint argc, gchar **argv, gchar **col_name)
{
	guint32 *id = (guint32 *) data;
	*id = atoi (argv[0]);
	return 0;
}

/**
 * ch_database_order_get_device_id:
 **/
guint32
ch_database_order_get_device_id (ChDatabase *database,
				 guint32 order_id,
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
				     "FROM devices WHERE order_id = '%i' "
				     "ORDER BY device_id DESC LIMIT 1", order_id);
	rc = sqlite3_exec (priv->db,
			   statement,
			   ch_database_order_get_device_id_cb,
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
			     "No devices for order %i", order_id);
		goto out;
	}
out:
	g_free (statement);
	return id;
}

/**
 * ch_database_get_all_orders_cb:
 **/
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
	statement = g_strdup ("SELECT order_id, name, address, email, postage, tracking_number, sent_date "
			      "FROM orders "
			      "ORDER BY order_id DESC");
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
		       ChShippingPostage postage,
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

/**
 * ch_database_get_queue_cb:
 **/
static gint
ch_database_get_queue_cb (void *data, gint argc, gchar **argv, gchar **col_name)
{
	GPtrArray *array = (GPtrArray *) data;
	g_ptr_array_add (array, g_strdup (argv[0]));
	return 0;
}

/**
 * ch_database_get_queue:
 **/
GPtrArray *
ch_database_get_queue (ChDatabase *database,
		       GError **error)
{
	ChDatabasePrivate *priv = database->priv;
	gboolean ret;
	gchar *error_msg = NULL;
	gchar *statement = NULL;
	gint rc;
	GPtrArray *array = NULL;
	GPtrArray *array_tmp = NULL;

	/* ensure db is loaded */
	ret = ch_database_load (database, error);
	if (!ret)
		goto out;

	/* find */
	array_tmp = g_ptr_array_new ();
	statement = g_strdup ("SELECT email "
			      "FROM queue "
			      "ORDER BY added ASC");
	rc = sqlite3_exec (priv->db,
			   statement,
			   ch_database_get_queue_cb,
			   array_tmp,
			   &error_msg);
	if (rc != SQLITE_OK) {
		g_set_error (error, 1, 0,
			     "failed to find entry: %s",
			     sqlite3_errmsg (priv->db));
		goto out;
	}

	/* success */
	array = g_ptr_array_ref (array_tmp);
out:
	if (array_tmp != NULL)
		g_ptr_array_unref (array_tmp);
	g_free (statement);
	return array;
}

/**
 * ch_database_queue_add:
 **/
guint
ch_database_queue_add (ChDatabase *database,
		       const gchar *email,
		       GError **error)
{
	ChDatabasePrivate *priv = database->priv;
	gboolean ret;
	gchar *error_msg = NULL;
	gchar *statement = NULL;
	gint rc;
	guint id = G_MAXUINT;

	/* ensure db is loaded */
	ret = ch_database_load (database, error);
	if (!ret)
		goto out;

	/* add newest */
	statement = sqlite3_mprintf ("INSERT INTO queue (email, added) "
				     "VALUES ('%q', '%li');",
				     email,
				     g_get_real_time ());
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

/**
 * ch_database_queue_remove:
 **/
gboolean
ch_database_queue_remove (ChDatabase *database,
			  const gchar *email,
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

	/* add newest */
	statement = sqlite3_mprintf ("DELETE FROM queue WHERE email = '%q';",
				     email);
	rc = sqlite3_exec (priv->db, statement, NULL, NULL, &error_msg);
	if (rc != SQLITE_OK) {
		ret = FALSE;
		g_set_error (error, 1, 0,
			     "failed to delete entry: %s",
			     sqlite3_errmsg (priv->db));
		goto out;
	}

	/* success */
	ret = TRUE;
out:
	sqlite3_free (statement);
	return ret;
}

/**
 * ch_database_queue_promote:
 **/
gboolean
ch_database_queue_promote (ChDatabase *database,
			   const gchar *email,
			   GError **error)
{
	ChDatabasePrivate *priv = database->priv;
	gboolean ret;
	gchar *error_msg = NULL;
	gchar *statement = NULL;
	gint rc;
	const gint64 added = 1332438884000000;

	/* ensure db is loaded */
	ret = ch_database_load (database, error);
	if (!ret)
		goto out;

	/* set added to a date in the past */
	statement = sqlite3_mprintf ("UPDATE queue SET added = '%li' WHERE email = '%q'",
				     added, email);
	rc = sqlite3_exec (priv->db, statement, NULL, NULL, &error_msg);
	if (rc != SQLITE_OK) {
		ret = FALSE;
		g_set_error (error, 1, 0,
			     "failed to promote entry: %s",
			     sqlite3_errmsg (priv->db));
		goto out;
	}

	/* success */
	ret = TRUE;
out:
	sqlite3_free (statement);
	return ret;
}

/**
 * ch_database_class_init:
 **/
static void
ch_database_class_init (ChDatabaseClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = ch_database_finalize;
	g_type_class_add_private (klass, sizeof (ChDatabasePrivate));
}

/**
 * ch_database_init:
 **/
static void
ch_database_init (ChDatabase *database)
{
	database->priv = CH_DATABASE_GET_PRIVATE (database);
}

/**
 * ch_database_finalize:
 **/
static void
ch_database_finalize (GObject *object)
{
	ChDatabase *database = CH_DATABASE (object);
	ChDatabasePrivate *priv = database->priv;

	g_free (priv->uri);
	if (priv->db != NULL)
		sqlite3_close (priv->db);

	G_OBJECT_CLASS (ch_database_parent_class)->finalize (object);
}

/**
 * ch_database_new:
 *
 * Return value: a new #ChDatabase object.
 **/
ChDatabase *
ch_database_new (void)
{
	ChDatabase *database;
	database = g_object_new (CH_TYPE_DATABASE, NULL);
	return CH_DATABASE (database);
}

