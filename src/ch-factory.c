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

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <locale.h>
#include <colord.h>
#include <math.h>
#include <gusb.h>
#include <stdlib.h>
#include <colord-gtk.h>
#include <colorhug.h>
#include <canberra-gtk.h>

#include "ch-flash-md.h"
#include "ch-database.h"
#include "ch-shipping-common.h"

#define CH_FACTORY_AMBIENT_MIN		0.00001
#define CH_FACTORY_BATCH_NUMBER		13

#define CH_DEVICE_ICON_BOOTLOADER	"colorimeter-colorhug-inactive"
#define CH_DEVICE_ICON_BUSY		"emblem-downloads"
#define CH_DEVICE_ICON_FIRMWARE		"colorimeter-colorhug"
#define CH_DEVICE_ICON_CALIBRATED	"emblem-default"
#define CH_DEVICE_ICON_ERROR		"dialog-error"
#define CH_DEVICE_ICON_MISSING		"edit-delete"

typedef struct {
	CdClient	*client;
	ChDeviceQueue	*device_queue;
	gboolean	 in_calibration;
	gchar		*local_calibration_uri;
	gchar		*local_firmware_uri;
	gchar		*firmware_data;
	gsize		 firmware_size;
	GPtrArray	*updates;
	GSettings	*settings;
	GtkApplication	*application;
	GtkBuilder	*builder;
	GtkWindow	*sample_window;
	GUsbContext	*usb_ctx;
	GUsbDeviceList	*device_list;
	ChDatabase	*database;
	GPtrArray	*samples_ti1;
	guint		 samples_ti1_idx;
	GHashTable	*results; /* key = device id, value = GPtrArray of CdColorXYZ values */
} ChFactoryPrivate;

enum {
	COLUMN_ENABLED,
	COLUMN_DESCRIPTION,
	COLUMN_FILENAME,
	COLUMN_ID,
	COLUMN_DEVICE,
	COLUMN_ERROR,
	COLUMN_LAST
};

/**
 * ch_factory_error_dialog:
 **/
static void
ch_factory_error_dialog (ChFactoryPrivate *priv,
			 const gchar *title,
			 const gchar *message)
{
	GtkWindow *window;
	GtkWidget *dialog;

	window = GTK_WINDOW(gtk_builder_get_object (priv->builder, "window_factory"));
	dialog = gtk_message_dialog_new (window,
					 GTK_DIALOG_MODAL,
					 GTK_MESSAGE_ERROR,
					 GTK_BUTTONS_CLOSE,
					 "%s", title);
	gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
						  "%s", message);
	gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);
}

/**
 * ch_factory_activate_cb:
 **/
static void
ch_factory_activate_cb (GApplication *application, ChFactoryPrivate *priv)
{
	GtkWindow *window;
	window = GTK_WINDOW (gtk_builder_get_object (priv->builder, "window_factory"));
	gtk_window_present (window);
}

/**
 * ch_factory_close_button_cb:
 **/
static void
ch_factory_close_button_cb (GtkWidget *widget, ChFactoryPrivate *priv)
{
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "window_factory"));
	gtk_widget_destroy (widget);
}

/**
 * ch_factory_find_by_id:
 **/
static gboolean
ch_factory_find_by_id (GtkTreeModel *model,
		       GtkTreeIter *iter_found,
		       const gchar *desc)
{
	gboolean ret;
	gchar *desc_tmp = NULL;
	GtkTreeIter iter;

	ret = gtk_tree_model_get_iter_first (model, &iter);
	while (ret) {
		gtk_tree_model_get (model, &iter,
				    COLUMN_ID, &desc_tmp,
				    -1);
		ret = g_strcmp0 (desc_tmp, desc) == 0;
		g_free (desc_tmp);
		if (ret) {
			*iter_found = iter;
			break;
		}
		ret = gtk_tree_model_iter_next (model, &iter);
	}
	return ret;
}

/**
 * ch_factory_set_device_state:
 **/
static void
ch_factory_set_device_state (ChFactoryPrivate *priv,
			     GUsbDevice *device,
			     const gchar *state)
{
	gboolean ret;
	GtkListStore *list_store;
	GtkTreeIter iter;

	list_store = GTK_LIST_STORE (gtk_builder_get_object (priv->builder, "liststore_devices"));
	ret = ch_factory_find_by_id (GTK_TREE_MODEL (list_store),
				     &iter,
				     g_usb_device_get_platform_id (device));
	if (!ret)
		return;
	gtk_list_store_set (list_store, &iter,
			    COLUMN_FILENAME, state,
			    -1);
}

/**
 * ch_factory_set_device_enabled:
 **/
static void
ch_factory_set_device_enabled (ChFactoryPrivate *priv,
			       GUsbDevice *device,
			       gboolean enabled)
{
	gboolean ret;
	GtkListStore *list_store;
	GtkTreeIter iter;

	list_store = GTK_LIST_STORE (gtk_builder_get_object (priv->builder, "liststore_devices"));
	ret = ch_factory_find_by_id (GTK_TREE_MODEL (list_store),
				     &iter,
				     g_usb_device_get_platform_id (device));
	if (!ret)
		return;
	gtk_list_store_set (list_store, &iter,
			    COLUMN_ENABLED, enabled,
			    -1);
}

/**
 * ch_factory_set_device_description:
 **/
static void
ch_factory_set_device_description (ChFactoryPrivate *priv,
				   GUsbDevice *device,
				   const gchar *description)
{
	gboolean ret;
	GtkListStore *list_store;
	GtkTreeIter iter;

	list_store = GTK_LIST_STORE (gtk_builder_get_object (priv->builder, "liststore_devices"));
	ret = ch_factory_find_by_id (GTK_TREE_MODEL (list_store),
				     &iter,
				     g_usb_device_get_platform_id (device));
	if (!ret)
		return;
	gtk_list_store_set (list_store, &iter,
			    COLUMN_DESCRIPTION, description,
			    -1);
}

/**
 * ch_factory_set_device_error:
 **/
static void
ch_factory_set_device_error (ChFactoryPrivate *priv,
			     GUsbDevice *device,
			     const gchar *error_message)
{
	gboolean ret;
	GtkListStore *list_store;
	GtkTreeIter iter;
	gchar *markup;

	list_store = GTK_LIST_STORE (gtk_builder_get_object (priv->builder, "liststore_devices"));
	ret = ch_factory_find_by_id (GTK_TREE_MODEL (list_store),
				     &iter,
				     g_usb_device_get_platform_id (device));
	if (!ret)
		return;
	markup = g_markup_escape_text (error_message, -1);
	if (strlen (markup) > 60)
		markup[60] = '\0';
	gtk_list_store_set (list_store, &iter,
			    COLUMN_ERROR, markup,
			    -1);
	g_free (markup);
}

/**
 * ch_factory_got_device:
 **/
static void
ch_factory_got_device (ChFactoryPrivate *priv, GUsbDevice *device)
{
	ChDeviceState state;
	const gchar *icon;
	const gchar *title;
	gboolean ret;
	gchar *description = NULL;
	gchar *error_tmp;
	GError *error = NULL;
	GtkListStore *list_store;
	GtkTreeIter iter;
	guint8 hw_version;
	guint serial_number;

	/* is a calibration in progress */
	if (priv->in_calibration) {
		list_store = GTK_LIST_STORE (gtk_builder_get_object (priv->builder, "liststore_devices"));
		ret = ch_factory_find_by_id (GTK_TREE_MODEL (list_store),
					     &iter,
					     g_usb_device_get_platform_id (device));
		if (!ret)
			gtk_list_store_append (list_store, &iter);
		gtk_list_store_set (list_store, &iter,
				    COLUMN_ID, g_usb_device_get_platform_id (device),
				    COLUMN_DEVICE, device,
				    COLUMN_FILENAME, CH_DEVICE_ICON_ERROR,
				    COLUMN_ERROR, "re-inserted",
				    -1);
		goto out;
	}

	/* open device */
	ret = ch_device_open (device, &error);
	if (!ret) {
		/* TRANSLATORS: permissions error perhaps? */
		title = _("Failed to open device");
		ch_factory_error_dialog (priv, title, error->message);
		g_error_free (error);
		goto out;
	}

	/* add to model */
	list_store = GTK_LIST_STORE (gtk_builder_get_object (priv->builder, "liststore_devices"));
	ret = ch_factory_find_by_id (GTK_TREE_MODEL (list_store),
				     &iter,
				     g_usb_device_get_platform_id (device));
	if (!ret)
		gtk_list_store_append (list_store, &iter);
	gtk_list_store_set (list_store, &iter,
			    COLUMN_ID, g_usb_device_get_platform_id (device),
			    COLUMN_DEVICE, device,
			    -1);

	/* get the serial number */
	ch_device_queue_get_serial_number (priv->device_queue,
					   device,
					   &serial_number);
	ch_device_queue_get_hardware_version (priv->device_queue,
					      device,
					      &hw_version);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_CONTINUE_ERRORS,
				       NULL,
				       &error);
	if (ret) {
		if (serial_number != 0xffffffff)
			description = g_strdup_printf ("ColorHug #%06i", serial_number);
		else
			description = g_strdup ("ColorHug #XXXXXX");
		state = ch_database_device_get_state (priv->database,
						      serial_number,
						      NULL);
		if (state == CH_DEVICE_STATE_CALIBRATED)
			icon = CH_DEVICE_ICON_CALIBRATED;
		else
			icon = CH_DEVICE_ICON_FIRMWARE;
		gtk_list_store_set (list_store, &iter,
				    COLUMN_DESCRIPTION, description,
				    COLUMN_FILENAME, icon,
				    COLUMN_ERROR, "re-inserted",
				    COLUMN_ENABLED, TRUE,
				    -1);
	} else {
		gtk_list_store_set (list_store, &iter,
				    COLUMN_DESCRIPTION, "ColorHug #??????",
				    COLUMN_FILENAME, CH_DEVICE_ICON_BOOTLOADER,
				    COLUMN_ERROR, "inserted",
				    COLUMN_ENABLED, TRUE,
				    -1);
		g_debug ("failed to get serial number: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* check the hardware version */
	if (hw_version != 1) {
		error_tmp = g_strdup_printf ("HWver %i", hw_version);
		ch_factory_set_device_state (priv, device, CH_DEVICE_ICON_ERROR);
		ch_factory_set_device_error (priv, device, error_tmp);
		g_warning ("sensor error; %s", error_tmp);
		g_free (error_tmp);
		goto out;
	}
out:
	g_free (description);
}

/**
 * ch_factory_removed_device:
 **/
static void
ch_factory_removed_device (ChFactoryPrivate *priv, GUsbDevice *device)
{
	gboolean ret;
	GtkListStore *list_store;
	GtkTreeIter iter;

	list_store = GTK_LIST_STORE (gtk_builder_get_object (priv->builder, "liststore_devices"));
	ret = ch_factory_find_by_id (GTK_TREE_MODEL (list_store),
				     &iter,
				     g_usb_device_get_platform_id (device));
	if (!ret)
		return;
	gtk_list_store_set (list_store, &iter,
			    COLUMN_FILENAME, CH_DEVICE_ICON_MISSING,
			    COLUMN_DEVICE, NULL,
			    COLUMN_ERROR, "removed",
			    COLUMN_ENABLED, FALSE,
			    -1);
}

/**
 * ch_factory_get_active_devices:
 **/
static GPtrArray *
ch_factory_get_active_devices (ChFactoryPrivate *priv)
{
	gboolean enabled;
	gboolean ret;
	GPtrArray *devices;
	GtkTreeIter iter;
	GtkTreeModel *model;
	GUsbDevice *device;

	devices = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	model = GTK_TREE_MODEL (gtk_builder_get_object (priv->builder, "liststore_devices"));
	ret = gtk_tree_model_get_iter_first (model, &iter);
	while (ret) {
		gtk_tree_model_get (model, &iter,
				    COLUMN_ENABLED, &enabled,
				    -1);
		if (!enabled) {
			ret = gtk_tree_model_iter_next (model, &iter);
			continue;
		}
		gtk_tree_model_get (model, &iter,
				    COLUMN_DEVICE, &device,
				    -1);
		if (device == NULL) {
			ret = gtk_tree_model_iter_next (model, &iter);
			continue;
		}
		g_ptr_array_add (devices, g_object_ref (device));
		g_object_unref (device);
		ret = gtk_tree_model_iter_next (model, &iter);
	}
	return devices;
}

/**
 * ch_factory_measure_firmware_boot_cb:
 **/
static void
ch_factory_measure_firmware_boot_cb (GObject *source,
				     GAsyncResult *res,
				     gpointer user_data)
{
	ChFactoryPrivate *priv = (ChFactoryPrivate *) user_data;
	gboolean ret;
	GError *error = NULL;

	/* get result */
	ret = ch_device_queue_process_finish (priv->device_queue,
					      res,
					      &error);
	if (!ret) {
		g_warning ("failed to boot flash: %s",
			   error->message);
		g_error_free (error);
	}
}

/**
 * ch_factory_measure_firmware_verify_cb:
 **/
static void
ch_factory_measure_firmware_verify_cb (GObject *source,
				       GAsyncResult *res,
				       gpointer user_data)
{
	ChFactoryPrivate *priv = (ChFactoryPrivate *) user_data;
	gboolean ret;
	GError *error = NULL;
	GPtrArray *devices = NULL;
	guint i;
	GUsbDevice *device;

	/* get result */
	ret = ch_device_queue_process_finish (priv->device_queue,
					      res,
					      &error);
	if (!ret) {
		g_warning ("failed to verify firmware on devices: %s",
			   error->message);
		g_error_free (error);
		goto out;
	}

	/* boot flash */
	devices = ch_factory_get_active_devices (priv);
	for (i = 0; i < devices->len; i++) {
		device = g_ptr_array_index (devices, i);
		ch_device_queue_boot_flash (priv->device_queue,
					    device);
	}
	ch_device_queue_process_async (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONFATAL_ERRORS,
				       NULL,
				       ch_factory_measure_firmware_boot_cb,
				       priv);
out:
	if (devices != NULL)
		g_ptr_array_unref (devices);
}

/**
 * ch_factory_measure_firmware_write_cb:
 **/
static void
ch_factory_measure_firmware_write_cb (GObject *source,
				      GAsyncResult *res,
				      gpointer user_data)
{
	gboolean ret;
	GError *error = NULL;
	GPtrArray *devices = NULL;
	guint i;
	GUsbDevice *device;
	ChFactoryPrivate *priv = (ChFactoryPrivate *) user_data;

	/* get result */
	ret = ch_device_queue_process_finish (priv->device_queue,
					      res,
					      &error);
	if (!ret) {
		g_warning ("failed to write firmware on devices: %s",
			   error->message);
		g_error_free (error);
		goto out;
	}

	/* verify flash code */
	devices = ch_factory_get_active_devices (priv);
	for (i = 0; i < devices->len; i++) {
		device = g_ptr_array_index (devices, i);
		ch_device_queue_verify_firmware (priv->device_queue,
						 device,
						 (const guint8 *) priv->firmware_data,
						 priv->firmware_size);
	}
	ch_device_queue_process_async (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONFATAL_ERRORS,
				       NULL,
				       ch_factory_measure_firmware_verify_cb,
				       priv);
out:
	if (devices != NULL)
		g_ptr_array_unref (devices);
}

/**
 * ch_factory_ensure_firmware_data:
 **/
static gboolean
ch_factory_ensure_firmware_data (ChFactoryPrivate *priv, GError **error)
{
	gboolean ret = TRUE;
	guint i;
	ChFlashUpdate *update_tmp = NULL;
	gchar *filename = NULL;

	if (priv->firmware_data != NULL)
		goto out;

	/* get the newest stable firmware */
	for (i = 0; i < priv->updates->len; i++) {
		update_tmp = g_ptr_array_index (priv->updates, i);
		if (update_tmp->state == CH_FLASH_MD_STATE_STABLE)
			break;
	}
	if (update_tmp == NULL) {
		ret = FALSE;
		g_set_error_literal (error, 1, 0,
				     "Failed to find any stable firmware");
		goto out;
	}
	filename = g_build_filename (priv->local_firmware_uri,
				     update_tmp->filename,
				     NULL);

	/* load file */
	g_debug ("using filename %s", filename);
	ret = g_file_get_contents (filename,
				   &priv->firmware_data,
				   &priv->firmware_size,
				   error);
	if (!ret)
		goto out;
out:
	g_free (filename);
	return ret;
}

/**
 * ch_factory_flash_button_cb:
 **/
static void
ch_factory_flash_button_cb (GtkWidget *widget, ChFactoryPrivate *priv)
{
	gboolean ret;
	GError *error = NULL;
	GPtrArray *devices = NULL;
	guint i;
	GUsbDevice *device;

	/* get the newest stable firmware */
	ret = ch_factory_ensure_firmware_data (priv, &error);
	if (!ret) {
		g_warning ("failed to read firmware: %s",
			   error->message);
		g_error_free (error);
		goto out;
	}

	/* write flash code */
	devices = ch_factory_get_active_devices (priv);
	for (i = 0; i < devices->len; i++) {
		device = g_ptr_array_index (devices, i);
		ch_device_queue_set_flash_success (priv->device_queue,
						   device,
						   0);
		ch_device_queue_write_firmware (priv->device_queue,
						device,
						(const guint8 *) priv->firmware_data,
						priv->firmware_size);
	}
	ch_device_queue_process_async (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONFATAL_ERRORS,
				       NULL,
				       ch_factory_measure_firmware_write_cb,
				       priv);
out:
	if (devices != NULL)
		g_ptr_array_unref (devices);
}

/**
 * ch_factory_set_serial_cb:
 **/
static void
ch_factory_set_serial_cb (GObject *source,
			  GAsyncResult *res,
			  gpointer user_data)
{
	ChFactoryPrivate *priv = (ChFactoryPrivate *) user_data;
	gboolean ret;
	GError *error = NULL;

	/* get result */
	ret = ch_device_queue_process_finish (priv->device_queue,
					      res,
					      &error);
	if (!ret) {
		g_warning ("failed to set serial numbers: %s",
			   error->message);
		g_error_free (error);
	}
}

/**
 * ch_factory_serial_button_cb:
 **/
static void
ch_factory_serial_button_cb (GtkWidget *widget, ChFactoryPrivate *priv)
{
	gchar *description;
	gchar *filename = NULL;
	GError *error = NULL;
	GPtrArray *devices;
	GtkResponseType response;
	GtkWidget *dialog;
	GtkWindow *main_window;
	guint32 serial_number;
	guint i;
	GUsbDevice *device;

	main_window = GTK_WINDOW (gtk_builder_get_object (priv->builder, "window_factory"));
	dialog = gtk_message_dialog_new (main_window,
					 GTK_DIALOG_DESTROY_WITH_PARENT,
					 GTK_MESSAGE_QUESTION,
					 GTK_BUTTONS_YES_NO,
					 "%s",
					 "Allocate actual serial numbers?");
	response = gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);

	/* dialog was closed */
	if (response == GTK_RESPONSE_DELETE_EVENT)
		goto out;

	/* allocate the serial numbers */
	devices = ch_factory_get_active_devices (priv);
	for (i = 0; i < devices->len; i++) {
		device = g_ptr_array_index (devices, i);

		/* yay, atomic serial number */
		if (response == GTK_RESPONSE_YES) {
			serial_number = ch_database_add_device (priv->database, &error);
			if (serial_number == 0) {
				g_warning ("failed to add entry: %s",
					   error->message);
				g_error_free (error);
				goto out;
			}
		} else {
			g_warning ("using dummy serial number");
			serial_number = 10000 + g_random_int_range (0, 1000);
		}
		g_debug ("setting serial number %i for %s",
			 serial_number,
			 g_usb_device_get_platform_id (device));
		ch_factory_set_device_state (priv, device, CH_DEVICE_ICON_BUSY);
		ch_device_queue_set_serial_number (priv->device_queue,
						   device,
						   serial_number);
		ch_device_queue_write_eeprom (priv->device_queue,
					      device,
					      CH_WRITE_EEPROM_MAGIC);
		description = g_strdup_printf ("ColorHug #%06i", serial_number);
		ch_factory_set_device_state (priv, device, CH_DEVICE_ICON_FIRMWARE);
		ch_factory_set_device_description (priv, device, description);
		g_free (description);
	}

	/* process queue */
	ch_device_queue_process_async (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       ch_factory_set_serial_cb,
				       priv);
out:
	g_free (filename);
}

/**
 * ch_factory_check_leds_button_cb:
 **/
static void
ch_factory_check_leds_button_cb (GtkWidget *widget, ChFactoryPrivate *priv)
{
	gboolean ret;
	GError *error = NULL;
	GPtrArray *devices;
	guint i;
	GUsbDevice *device;

	/* turn on all the LEDs */
	devices = ch_factory_get_active_devices (priv);
	for (i = 0; i < devices->len; i++) {
		device = g_ptr_array_index (devices, i);
		ch_device_queue_set_leds (priv->device_queue,
					  device,
					  1,
					  50,
					  100,
					  1);
	}

	/* process queue */
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       &error);
	if (!ret) {
		g_warning ("failed to set leds on devices: %s",
			   error->message);
		g_error_free (error);
	}
	g_ptr_array_unref (devices);
}

/**
 * ch_factory_load_samples:
 **/
static gboolean
ch_factory_load_samples (ChFactoryPrivate *priv,
			 const gchar *ti1_fn,
			 GError **error)
{
	CdColorRGB *rgb;
	CdIt8 *ti1 = NULL;
	gboolean ret = TRUE;
	gchar *ti1_data = NULL;
	gsize ti1_size;
	guint i;
	guint number_of_sets = 0;

	/* already loaded */
	if (priv->samples_ti1->len > 0)
		goto out;

	/* open ti1 file as input */
	g_debug ("loading %s", ti1_fn);
	ret = g_file_get_contents (ti1_fn, &ti1_data, &ti1_size, error);
	if (!ret)
		goto out;

	/* load the ti1 data */
	ti1 = cd_it8_new ();
	ret = cd_it8_load_from_data (ti1, ti1_data, ti1_size, error);
	if (!ret)
		goto out;
	number_of_sets = cd_it8_get_data_size (ti1);

	/* work out what colors to show */
	for (i = 0; i < number_of_sets; i++) {
		rgb = cd_color_rgb_new ();
		cd_it8_get_data_item (ti1, i, rgb, NULL);
		g_ptr_array_add (priv->samples_ti1, rgb);
	}
out:
	if (ti1 != NULL)
		g_object_unref (ti1);
	g_free (ti1_data);
	return ret;
}

/**
 * ch_factory_device_is_shit:
 **/
static void
ch_factory_device_is_shit (ChFactoryPrivate *priv,
			   GUsbDevice *device,
			   const gchar *error_msg)
{
	g_debug ("Taking %s out of the list",
		 g_usb_device_get_platform_id (device));
	ch_factory_set_device_state (priv, device, CH_DEVICE_ICON_ERROR);
	ch_factory_set_device_error (priv, device, error_msg);
	ch_factory_set_device_enabled (priv, device, FALSE);
	g_hash_table_remove (priv->results,
			     g_usb_device_get_platform_id (device));
}

static void ch_factory_measure		(ChFactoryPrivate *priv);
static void ch_factory_measure_save	(ChFactoryPrivate *priv);

/**
 * ch_factory_measure_done_cb:
 **/
static void
ch_factory_measure_done_cb (GObject *source,
			    GAsyncResult *res,
			    gpointer user_data)
{
	CdColorXYZ *xyz;
	ChFactoryPrivate *priv = (ChFactoryPrivate *) user_data;
	gboolean ret;
	GError *error = NULL;
	GPtrArray *devices = NULL;
	GPtrArray *results_tmp;
	guint i;
	GUsbDevice *device;

	/* get return value */
	ret = ch_device_queue_process_finish (priv->device_queue,
					      res,
					      &error);
	if (!ret) {
		g_warning ("failed to submit commands: %s",
			   error->message);
		g_error_free (error);
		goto out;
	}

	/* save sample data */
	devices = ch_factory_get_active_devices (priv);
	for (i = 0; i < devices->len; i++) {
		device = g_ptr_array_index (devices, i);
		xyz = g_object_get_data (G_OBJECT (device), "ChFactory::buffer");
		g_debug ("Adding %f,%f,%f for %s",
			 xyz->X, xyz->Y, xyz->Z,
			 g_usb_device_get_platform_id (device));
		results_tmp = g_hash_table_lookup (priv->results,
						   g_usb_device_get_platform_id (device));
		g_ptr_array_add (results_tmp, xyz);
		g_object_steal_data (G_OBJECT (device), "ChFactory::buffer");
	}

	/* success */
	priv->samples_ti1_idx++;

	/* more samples to do */
	if (priv->samples_ti1_idx < priv->samples_ti1->len) {
		ch_factory_measure (priv);
		goto out;
	}

	/* we're done */
	ch_factory_measure_save (priv);

	/* play sound */
	g_debug ("playing sound");
	ca_context_play (ca_gtk_context_get (), 0,
			 CA_PROP_EVENT_ID, "alarm-clock-elapsed",
			 CA_PROP_APPLICATION_NAME, _("ColorHug Factory"),
			 CA_PROP_EVENT_DESCRIPTION, _("Calibration Completed"), NULL);

	/* hide the sample window */
	priv->in_calibration = FALSE;
	gtk_widget_hide (GTK_WIDGET (priv->sample_window));
out:
	if (devices != NULL)
		g_ptr_array_unref (devices);
}

/**
 * ch_factory_measure_cb:
 **/
static gboolean
ch_factory_measure_cb (ChFactoryPrivate *priv)
{
	CdColorXYZ *xyz;
	GPtrArray *devices;
	guint i;
	GUsbDevice *device;

	/* do this async */
	devices = ch_factory_get_active_devices (priv);
	for (i = 0; i < devices->len; i++) {
		device = g_ptr_array_index (devices, i);
		g_debug ("measuring from %s",
			 g_usb_device_get_platform_id (device));

		xyz = cd_color_xyz_new ();
		g_object_set_data (G_OBJECT (device), "ChFactory::buffer", xyz);
		ch_device_queue_take_readings (priv->device_queue,
					       device,
					       (CdColorRGB *) xyz);
	}

	/* run this sample */
	ch_device_queue_process_async (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONFATAL_ERRORS,
				       NULL,
				       ch_factory_measure_done_cb,
				       priv);

	/* wait for async reply */
	if (devices != NULL)
		g_ptr_array_unref (devices);
	return FALSE;
}

/**
 * ch_factory_measure:
 **/
static void
ch_factory_measure (ChFactoryPrivate *priv)
{
	CdColorRGB *rgb_tmp;
	GtkWidget *widget;

	/* we're trying to get a sample that does not exist */
	if (priv->samples_ti1_idx >= priv->samples_ti1->len) {
		g_assert_not_reached ();
		return;
	}

	/* get samples */
	rgb_tmp = g_ptr_array_index (priv->samples_ti1, priv->samples_ti1_idx);

	cd_sample_window_set_color (CD_SAMPLE_WINDOW (priv->sample_window), rgb_tmp);
	cd_sample_window_set_fraction (CD_SAMPLE_WINDOW (priv->sample_window),
					 (gdouble) priv->samples_ti1_idx / (gdouble) priv->samples_ti1->len);

	/* update global percentage */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "progressbar_calibration"));
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (widget),
				       (gdouble) priv->samples_ti1_idx / (gdouble) priv->samples_ti1->len);

	/* this has to be long enough for the screen to refresh */
	g_timeout_add (800, (GSourceFunc) ch_factory_measure_cb, priv);
}

/**
 * ch_factory_measure_check_matrix:
 **/
static gboolean
ch_factory_measure_check_matrix (const CdMat3x3 *calibration, GError **error)
{
	gboolean ret = TRUE;
	gdouble det;

#define CH_DEVICE_DETERMINANT_AVE	25.85
#define CH_DEVICE_DETERMINANT_ERROR	20.00

	/* check the scale is correct */
	det = cd_mat33_determinant (calibration);
	if (ABS (det - CH_DEVICE_DETERMINANT_AVE) > CH_DEVICE_DETERMINANT_ERROR) {
		ret = FALSE;
		g_set_error (error, 1, 0,
			     "Matrix determinant out of range: %f", det);
		goto out;
	}
out:
	return ret;
}

/**
 * ch_factory_print_device_label:
 **/
static void
ch_factory_print_device_label (ChFactoryPrivate *priv, guint32 device_serial)
{
	gboolean ret;
	GDateTime *datetime;
	GError *error = NULL;
	GString *str;

	datetime = g_date_time_new_now_local ();
	str = ch_shipping_string_load (CH_DATA "/device-label.tex", &error);
	if (str == NULL) {
		ch_factory_error_dialog (priv, "failed to load file: %s", error->message);
		g_error_free (error);
		goto out;
	}
	ch_shipping_string_replace (str, "$SERIAL$", g_strdup_printf ("%06i", device_serial));
	ch_shipping_string_replace (str, "$BATCH$", g_strdup_printf ("%02i",
								     CH_FACTORY_BATCH_NUMBER));
	ch_shipping_string_replace (str, "$DATE$", g_date_time_format (datetime, "%Y-%m-%d"));
	g_date_time_unref (datetime);

	/* print */
	ret = ch_shipping_print_latex_doc (str->str, "LP2844", &error);
	if (!ret) {
		ch_factory_error_dialog (priv, "failed to save file: %s", error->message);
		g_error_free (error);
		goto out;
	}
out:
	g_string_free (str, TRUE);
}

/**
 * ch_factory_measure_save_device:
 **/
static void
ch_factory_measure_save_device (ChFactoryPrivate *priv, GUsbDevice *device)
{
	CdColorRGB *rgb;
	CdColorXYZ *xyz;
	CdIt8 *it8_device = NULL;
	CdIt8 *it8_measured = NULL;
	CdIt8 *it8_reference = NULL;
	const CdMat3x3 *calibration;
	gboolean ret;
	gchar *filename_ccmx = NULL;
	gchar *filename_ti3 = NULL;
	gchar *local_spectral_reference;
	GError *error = NULL;
	GFile *file_device = NULL;
	GFile *file_measured = NULL;
	GFile *file_reference = NULL;
	GPtrArray *results_tmp;
	guint32 serial_number = 0;
	guint i;

	/* get the ti3 file */
	local_spectral_reference = g_settings_get_string (priv->settings,
							  "local-spectral-reference");
	it8_reference = cd_it8_new ();
	file_reference = g_file_new_for_path (local_spectral_reference);
	ret = cd_it8_load_from_file (it8_reference, file_reference, &error);
	if (!ret) {
		ch_factory_device_is_shit (priv, device, error->message);
		g_error_free (error);
		goto out;
	}

	/* get the serial number for the filename */
	ch_device_queue_get_serial_number (priv->device_queue,
					   device,
					   &serial_number);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       &error);
	if (!ret) {
		ch_factory_device_is_shit (priv, device, error->message);
		g_debug ("failed to get serial number: %s", error->message);
		g_error_free (error);
		goto out;
	}
	filename_ti3 = g_strdup_printf ("%s/data/calibration-%06i.ti3",
					priv->local_calibration_uri,
					serial_number);
	filename_ccmx = g_strdup_printf ("%s/archive/calibration-%06i.ccmx",
					 priv->local_calibration_uri,
					 serial_number);

	/* save to file */
	results_tmp = g_hash_table_lookup (priv->results,
					   g_usb_device_get_platform_id (device));

	/* backup to a ti3 file */
	it8_measured = cd_it8_new_with_kind (CD_IT8_KIND_TI3);
	cd_it8_set_originator (it8_measured, "ColorHug Factory");
	cd_it8_set_instrument (it8_measured, "Hughski ColorHug");
	for (i = 0; i < priv->samples_ti1->len; i++) {
		rgb = g_ptr_array_index (priv->samples_ti1, i);
		xyz = g_ptr_array_index (results_tmp, i);
		cd_it8_add_data (it8_measured, rgb, xyz);
	}
	g_debug ("backing up to %s", filename_ti3);
	file_measured = g_file_new_for_path (filename_ti3);
	ret = cd_it8_save_to_file (it8_measured, file_measured, &error);
	if (!ret) {
		ch_factory_device_is_shit (priv, device, "save");
		g_warning ("failed to save to file: %s", "save");
		g_error_free (error);
		goto out;
	}

	/* create ccmx file */
	it8_device = cd_it8_new_with_kind (CD_IT8_KIND_CCMX);
	cd_it8_set_originator (it8_device, "ColorHug Factory");
	cd_it8_set_title (it8_device, "Factory Calibration");
	cd_it8_add_option (it8_device, "TYPE_FACTORY");
	ret = cd_it8_utils_calculate_ccmx (it8_reference,
					   it8_measured,
					   it8_device,
					   &error);
	if (!ret) {
		ch_factory_device_is_shit (priv, device, error->message);
		g_warning ("failed to generate: %s", error->message);
		g_error_free (error);
		goto out;
	}
	file_device = g_file_new_for_path (filename_ccmx);
	ret = cd_it8_save_to_file (it8_device, file_device, &error);
	if (!ret) {
		ch_factory_device_is_shit (priv, device, error->message);
		g_warning ("failed to save file: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* check the scale is correct */
	calibration = cd_it8_get_matrix (it8_device);
	ret = ch_factory_measure_check_matrix (calibration, &error);
	if (!ret) {
		ch_factory_device_is_shit (priv, device, error->message);
		g_warning ("%s", error->message);
		g_error_free (error);
		goto out;
	}

	/* save ccmx to slot 0 */
	g_debug ("writing %s to device", filename_ccmx);
	ret = ch_device_queue_set_calibration_ccmx (priv->device_queue,
						    device,
						    0,
						    it8_device,
						    &error);
	if (!ret) {
		ch_factory_device_is_shit (priv, device, error->message);
		g_debug ("failed to set ccmx file: %s", error->message);
		g_error_free (error);
		goto out;
	}
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       &error);
	if (!ret) {
		ch_factory_device_is_shit (priv, device, error->message);
		g_debug ("failed to set ccmx file: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* allow this device to be sent out */
	ret = ch_database_device_set_state (priv->database,
					    serial_number,
					    CH_DEVICE_STATE_CALIBRATED,
					    &error);
	if (!ret) {
		ch_factory_device_is_shit (priv, device, error->message);
		g_warning ("failed to update database: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* print device label */
	ch_factory_print_device_label (priv, serial_number);

	/* success */
	ch_factory_set_device_state (priv, device, CH_DEVICE_ICON_CALIBRATED);
out:
	if (file_reference != NULL)
		g_object_unref (file_reference);
	if (file_measured != NULL)
		g_object_unref (file_measured);
	if (file_device != NULL)
		g_object_unref (file_device);
	if (it8_reference != NULL)
		g_object_unref (it8_reference);
	if (it8_measured != NULL)
		g_object_unref (it8_measured);
	if (it8_device != NULL)
		g_object_unref (it8_device);
	g_free (filename_ccmx);
	g_free (filename_ti3);
	g_free (local_spectral_reference);
}

/**
 * ch_factory_measure_save:
 **/
static void
ch_factory_measure_save (ChFactoryPrivate *priv)
{
	GPtrArray *devices;
	guint i;
	GUsbDevice *device;

	/* save to a file for backup and create ccmx file */
	devices = ch_factory_get_active_devices (priv);
	for (i = 0; i < devices->len; i++) {
		device = g_ptr_array_index (devices, i);
		ch_factory_measure_save_device (priv, device);
	}
	g_ptr_array_unref (devices);
}

/**
 * ch_factory_calibrate_button_cb:
 **/
static void
ch_factory_calibrate_button_cb (GtkWidget *widget, ChFactoryPrivate *priv)
{
	CdColorRGB *rgb_ambient = NULL;
	CdColorRGB rgb_tmp;
	gboolean ret;
	gchar *error_tmp;
	gchar *filename;
	gchar *local_patches_source;
	GError *error = NULL;
	GPtrArray *devices = NULL;
	guint16 calibration_map[6];
	guint i;
	GUsbDevice *device;

	/* ignore devices that are replugged */
	priv->in_calibration = TRUE;

	/* get the ti1 file from gsettings */
	local_patches_source = g_settings_get_string (priv->settings,
						      "local-patches-source");
	filename = g_build_filename (priv->local_calibration_uri,
				     local_patches_source,
				     NULL);
	ret = ch_factory_load_samples (priv, filename, &error);
	if (!ret) {
		g_warning ("can't load samples: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* get active devices */
	devices = ch_factory_get_active_devices (priv);

	/* prepare for calibration */
	rgb_tmp.R = 0.00005f;
	rgb_tmp.G = 0.00005f;
	rgb_tmp.B = 0.00005f;
	calibration_map[0] = 0;
	calibration_map[1] = 0;
	calibration_map[2] = 0;
	calibration_map[3] = 0;
	calibration_map[4] = 0;
	calibration_map[5] = 0;
	rgb_ambient = g_new0 (CdColorRGB, devices->len);
	for (i = 0; i < devices->len; i++) {
		device = g_ptr_array_index (devices, i);
		ch_factory_set_device_error (priv, device, "calibrating");
		ch_factory_set_device_state (priv, device, CH_DEVICE_ICON_BUSY);

		ch_device_queue_set_flash_success (priv->device_queue,
						   device,
						   1);
		ch_device_queue_set_dark_offsets (priv->device_queue,
						  device,
						  &rgb_tmp);
		ch_device_queue_set_pre_scale (priv->device_queue,
					       device,
					       5);
		ch_device_queue_set_post_scale (priv->device_queue,
						device,
						3000);
		ch_device_queue_set_multiplier (priv->device_queue,
						device,
						CH_FREQ_SCALE_100);
		ch_device_queue_set_integral_time (priv->device_queue,
						   device,
						   CH_INTEGRAL_TIME_VALUE_MAX);
		ch_device_queue_set_calibration_map (priv->device_queue,
						     device,
						     calibration_map);
		ch_device_queue_write_eeprom (priv->device_queue,
					      device,
					      CH_WRITE_EEPROM_MAGIC);
	}

	/* check an ambient reading */
	for (i = 0; i < devices->len; i++) {
		device = g_ptr_array_index (devices, i);
		ch_device_queue_take_readings (priv->device_queue,
					       device,
					       &rgb_ambient[i]);
	}
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONFATAL_ERRORS,
				       NULL,
				       &error);
	if (!ret) {
		g_warning ("Failed to get ambient sample: %s", error->message);
		g_error_free (error);
		goto out;
	}
	for (i = 0; i < devices->len; i++) {
		device = g_ptr_array_index (devices, i);
		if (rgb_ambient[i].R < CH_FACTORY_AMBIENT_MIN ||
		    rgb_ambient[i].B < CH_FACTORY_AMBIENT_MIN ||
		    rgb_ambient[i].B < CH_FACTORY_AMBIENT_MIN) {
			error_tmp = g_strdup_printf ("ambient too low: %f,%f,%f < %f",
						     rgb_tmp.R, rgb_tmp.G, rgb_tmp.B,
						     CH_FACTORY_AMBIENT_MIN);
			ch_factory_set_device_enabled (priv, device, FALSE);
			ch_factory_set_device_error (priv, device, error_tmp);
			g_free (error_tmp);
			g_warning ("failed to get sample: %f,%f,%f",
				   rgb_tmp.R, rgb_tmp.G, rgb_tmp.B);
		}
	}

	/* get still active devices */
	g_hash_table_remove_all (priv->results);
	devices = ch_factory_get_active_devices (priv);
	for (i = 0; i < devices->len; i++) {
		device = g_ptr_array_index (devices, i);
		g_object_set_data (G_OBJECT (device),
				   "ChFactory::error_count",
				   GINT_TO_POINTER (0));

		/* add to devices list */
		g_hash_table_insert (priv->results,
				     g_strdup (g_usb_device_get_platform_id (device)),
				     g_ptr_array_new_with_free_func ((GDestroyNotify) cd_color_xyz_free));
	}

	/* nothing to do */
	if (devices->len == 0)
		goto out;

	/* setup the measure window */
//	gtk_widget_set_size_request (GTK_WIDGET (priv->sample_window), 1180, 1850);
	gtk_widget_set_size_request (GTK_WIDGET (priv->sample_window), 1850, 1180);
	cd_sample_window_set_fraction (CD_SAMPLE_WINDOW (priv->sample_window), 0);
	gtk_window_stick (priv->sample_window);
	gtk_window_present (priv->sample_window);
	gtk_window_move (priv->sample_window, 10, 10);

	/* update global percentage */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "progressbar_calibration"));
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (widget), 0.0f);

	/* take the measurements */
	priv->samples_ti1_idx = 0;
	ch_factory_measure (priv);
out:
	g_free (local_patches_source);
	if (devices != NULL)
		g_ptr_array_unref (devices);
	g_free (filename);
}

/**
 * ch_factory_boot_cb:
 **/
static void
ch_factory_boot_cb (GObject *source,
		    GAsyncResult *res,
		    gpointer user_data)
{
	ChFactoryPrivate *priv = (ChFactoryPrivate *) user_data;
	gboolean ret;
	GError *error = NULL;

	/* get result */
	ret = ch_device_queue_process_finish (priv->device_queue,
					      res,
					      &error);
	if (!ret) {
		g_warning ("failed to boot: %s",
			   error->message);
		g_error_free (error);
	}
}

/**
 * ch_factory_boot_button_cb:
 **/
static void
ch_factory_boot_button_cb (GtkWidget *widget, ChFactoryPrivate *priv)
{
	GPtrArray *devices;
	guint i;
	GUsbDevice *device;

	/* turn on all the LEDs */
	devices = ch_factory_get_active_devices (priv);
	for (i = 0; i < devices->len; i++) {
		device = g_ptr_array_index (devices, i);
		g_debug ("resetting %s",
			 g_usb_device_get_platform_id (device));
		ch_factory_set_device_state (priv,
					     device,
					     CH_DEVICE_ICON_BUSY);
		ch_device_queue_reset (priv->device_queue, device);
	}

	/* process queue */
	ch_device_queue_process_async (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_CONTINUE_ERRORS,
				       NULL,
				       ch_factory_boot_cb,
				       priv);
	g_ptr_array_unref (devices);
}

/**
 * ch_factory_set_leds_cb:
 **/
static void
ch_factory_set_leds_cb (GObject *source,
			GAsyncResult *res,
			gpointer user_data)
{
	ChFactoryPrivate *priv = (ChFactoryPrivate *) user_data;
	gboolean ret;
	GError *error = NULL;

	/* get result */
	ret = ch_device_queue_process_finish (priv->device_queue,
					      res,
					      &error);
	if (!ret) {
		g_warning ("failed to set-leds: %s",
			   error->message);
		g_error_free (error);
	}
}

/**
 * ch_factory_row_activated_cb:
 **/
static void
ch_factory_row_activated_cb (GtkTreeView *treeview,
			     GtkTreePath *path,
			     GtkTreeViewColumn *col,
			     ChFactoryPrivate *priv)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	gboolean ret;
	gchar *id = NULL;
	GUsbDevice *device = NULL;

	/* get selection */
	model = gtk_tree_view_get_model (treeview);
	ret = gtk_tree_model_get_iter (model, &iter, path);
	if (!ret) {
		g_warning ("failed to get selection");
		goto out;
	}

	/* get data */
	gtk_tree_model_get (model, &iter,
			    COLUMN_ID, &id,
			    COLUMN_DEVICE, &device,
			    -1);
	if (device == NULL)
		goto out;
	g_debug ("%s = %i", id, g_usb_device_get_pid (device));


	/* flash the LEDs */
	ch_device_queue_set_leds (priv->device_queue,
				  device,
				  CH_STATUS_LED_GREEN | CH_STATUS_LED_RED,
				  5,
				  100,
				  100);
	ch_device_queue_process_async (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       ch_factory_set_leds_cb,
				       priv);
out:
	if (device != NULL)
		g_object_unref (device);
	g_free (id);
}
/**
 * ch_factory_active_clicked_cb:
 **/
static void
ch_factory_active_clicked_cb (GtkCellRendererToggle *cell,
			      const gchar *path_str,
			      ChFactoryPrivate *priv)
{
	GtkListStore *list_store;
	GtkTreeIter iter;
	GtkTreePath *path;

	/* just invert */
	list_store = GTK_LIST_STORE (gtk_builder_get_object (priv->builder, "liststore_devices"));
	path = gtk_tree_path_new_from_string (path_str);
	gtk_tree_model_get_iter (GTK_TREE_MODEL (list_store), &iter, path);
	gtk_list_store_set (list_store, &iter,
			    COLUMN_ENABLED, !gtk_cell_renderer_toggle_get_active (cell),
			    -1);
	gtk_tree_path_free (path);
}

/**
 * ch_factory_treeview_add_columns:
 **/
static void
ch_factory_treeview_add_columns (ChFactoryPrivate *priv)
{
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;
	GtkTreeView *treeview;

	treeview = GTK_TREE_VIEW (gtk_builder_get_object (priv->builder, "treeview_devices"));
	g_signal_connect (treeview, "row-activated",
			  G_CALLBACK (ch_factory_row_activated_cb), priv);

	/* column for enabled */
	column = gtk_tree_view_column_new ();
	renderer = gtk_cell_renderer_toggle_new ();
	g_signal_connect (renderer, "toggled", G_CALLBACK (ch_factory_active_clicked_cb), priv);
	gtk_tree_view_column_pack_start (column, renderer, FALSE);
	gtk_tree_view_column_add_attribute (column, renderer, "active", COLUMN_ENABLED);
	gtk_tree_view_append_column (treeview, column);

	/* column for images */
	column = gtk_tree_view_column_new ();
	renderer = gtk_cell_renderer_pixbuf_new ();
	g_object_set (renderer, "stock-size", GTK_ICON_SIZE_BUTTON, NULL);
	gtk_tree_view_column_pack_start (column, renderer, FALSE);
	gtk_tree_view_column_add_attribute (column, renderer, "icon-name", COLUMN_FILENAME);
	gtk_tree_view_append_column (treeview, column);

	/* column for name */
	column = gtk_tree_view_column_new ();
	renderer = gtk_cell_renderer_text_new ();
	gtk_tree_view_column_pack_start (column, renderer, FALSE);
	gtk_tree_view_column_add_attribute (column, renderer, "markup", COLUMN_DESCRIPTION);
	gtk_tree_view_append_column (treeview, column);

	/* column for ID */
	column = gtk_tree_view_column_new ();
	renderer = gtk_cell_renderer_text_new ();
	gtk_tree_view_column_pack_start (column, renderer, FALSE);
	gtk_tree_view_column_add_attribute (column, renderer, "markup", COLUMN_ID);
	gtk_tree_view_column_set_sort_column_id (column, COLUMN_ID);
	gtk_tree_view_append_column (treeview, column);

	/* column for error */
	column = gtk_tree_view_column_new ();
	renderer = gtk_cell_renderer_text_new ();
	gtk_tree_view_column_pack_start (column, renderer, FALSE);
	gtk_tree_view_column_add_attribute (column, renderer, "markup", COLUMN_ERROR);
	gtk_tree_view_append_column (treeview, column);
}

/**
 * ch_factory_select_all_cb:
 **/
static void
ch_factory_select_all_cb (GtkToolButton *toolbutton, ChFactoryPrivate *priv)
{
	gboolean ret;
	GtkTreeIter iter;
	GtkTreeModel *model;

	model = GTK_TREE_MODEL (gtk_builder_get_object (priv->builder, "liststore_devices"));
	ret = gtk_tree_model_get_iter_first (model, &iter);
	while (ret) {
		gtk_list_store_set (GTK_LIST_STORE (model), &iter,
				    COLUMN_ENABLED, TRUE,
				    -1);
		ret = gtk_tree_model_iter_next (model, &iter);
	}
}

/**
 * ch_factory_select_none_cb:
 **/
static void
ch_factory_select_none_cb (GtkToolButton *toolbutton, ChFactoryPrivate *priv)
{
	gboolean ret;
	GtkTreeIter iter;
	GtkTreeModel *model;

	model = GTK_TREE_MODEL (gtk_builder_get_object (priv->builder, "liststore_devices"));
	ret = gtk_tree_model_get_iter_first (model, &iter);
	while (ret) {
		gtk_list_store_set (GTK_LIST_STORE (model), &iter,
				    COLUMN_ENABLED, FALSE,
				    -1);
		ret = gtk_tree_model_iter_next (model, &iter);
	}
}

/**
 * ch_factory_select_invert_cb:
 **/
static void
ch_factory_select_invert_cb (GtkToolButton *toolbutton, ChFactoryPrivate *priv)
{
	gboolean enabled;
	gboolean ret;
	GtkTreeIter iter;
	GtkTreeModel *model;

	model = GTK_TREE_MODEL (gtk_builder_get_object (priv->builder, "liststore_devices"));
	ret = gtk_tree_model_get_iter_first (model, &iter);
	while (ret) {
		gtk_tree_model_get (model, &iter,
				    COLUMN_ENABLED, &enabled,
				    -1);
		gtk_list_store_set (GTK_LIST_STORE (model), &iter,
				    COLUMN_ENABLED, !enabled,
				    -1);
		ret = gtk_tree_model_iter_next (model, &iter);
	}
}

/**
 * ch_factory_startup_cb:
 **/
static void
ch_factory_startup_cb (GApplication *application, ChFactoryPrivate *priv)
{
	CdDevice *device = NULL;
	gboolean ret;
	gchar *filename = NULL;
	GError *error = NULL;
	gint retval;
	GtkStyleContext *context;
	GtkWidget *main_window;
	GtkWidget *widget;

	/* get UI */
	priv->builder = gtk_builder_new ();
	retval = gtk_builder_add_from_file (priv->builder,
					    CH_DATA "/ch-factory.ui",
					    &error);
	if (retval == 0) {
		g_warning ("failed to load ui: %s",
			   error->message);
		g_error_free (error);
		goto out;
	}

	/* add application specific icons to search path */
	gtk_icon_theme_append_search_path (gtk_icon_theme_get_default (),
					   CH_DATA G_DIR_SEPARATOR_S "icons");

	main_window = GTK_WIDGET (gtk_builder_get_object (priv->builder, "window_factory"));
	gtk_application_add_window (priv->application, GTK_WINDOW (main_window));
	gtk_widget_set_size_request (main_window, 600, 400);

	/* Hide window first so that the dialogue resizes itself without redrawing */
	gtk_widget_hide (main_window);

	/* setup treeview */
	ch_factory_treeview_add_columns (priv);

	/* buttons */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_close"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (ch_factory_close_button_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_boot"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (ch_factory_boot_button_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_flash"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (ch_factory_flash_button_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_calibrate"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (ch_factory_calibrate_button_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_serial"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (ch_factory_serial_button_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_check_leds"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (ch_factory_check_leds_button_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "toolbutton_select_all"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (ch_factory_select_all_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "toolbutton_select_none"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (ch_factory_select_none_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "toolbutton_select_invert"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (ch_factory_select_invert_cb), priv);

	/* update global percentage */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "progressbar_calibration"));
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (widget), 0.0f);

	/* make devices toolbar sexy */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder,
						     "scrolledwindow_devices"));
	context = gtk_widget_get_style_context (widget);
	gtk_style_context_set_junction_sides (context, GTK_JUNCTION_BOTTOM);

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder,
						     "toolbar_devices"));
	context = gtk_widget_get_style_context (widget);
	gtk_style_context_add_class (context, GTK_STYLE_CLASS_INLINE_TOOLBAR);
	gtk_style_context_set_junction_sides (context, GTK_JUNCTION_TOP);

	/* is the colorhug already plugged in? */
	g_usb_device_list_coldplug (priv->device_list);

	/* get the firmware */
	filename = g_build_filename (priv->local_firmware_uri,
				     "metadata.xml",
				     NULL);
	priv->updates = ch_flash_md_parse_filename (filename, &error);
	if (priv->updates == NULL) {
		g_warning ("failed to load metadata: %s",
			   error->message);
		g_error_free (error);
		goto out;
	}

	/* inhibit the display */
	ret = cd_client_connect_sync (priv->client, NULL, &error);
	if (!ret) {
		g_warning ("failed to contact colord: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* finds the colord device which has a specific property */
	device = cd_client_find_device_by_property_sync (priv->client,
							 CD_DEVICE_METADATA_XRANDR_NAME,
							 "DP2",
							 NULL,
							 &error);
	if (device == NULL) {
		g_warning ("no device with that property: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* inhibit the device */
	ret = cd_device_connect_sync (device, NULL, &error);
	if (!ret) {
		g_warning ("failed to get properties from the device: %s",
			   error->message);
		g_error_free (error);
		goto out;
	}
	ret = cd_device_profiling_inhibit_sync (device, NULL, &error);
	if (!ret) {
		g_warning ("failed to get inhibit the device: %s",
			   error->message);
		g_error_free (error);
		goto out;
	}

	/* show main UI */
	gtk_widget_show (main_window);
out:
	if (device != NULL)
		g_object_unref (device);
	g_free (filename);
}

/**
 * ch_factory_device_added_cb:
 **/
static void
ch_factory_device_added_cb (GUsbDeviceList *list,
			    GUsbDevice *device,
			    ChFactoryPrivate *priv)
{
	if (ch_device_is_colorhug (device)) {
		g_debug ("Added ColorHug: %s",
			 g_usb_device_get_platform_id (device));
		ch_factory_got_device (priv, device);
	}
}

/**
 * ch_factory_device_removed_cb:
 **/
static void
ch_factory_device_removed_cb (GUsbDeviceList *list,
			    GUsbDevice *device,
			    ChFactoryPrivate *priv)
{
	if (ch_device_is_colorhug (device)) {
		g_debug ("Removed ColorHug: %s",
			 g_usb_device_get_platform_id (device));
		ch_factory_removed_device (priv, device);
	}
}

static void
ch_factory_device_queue_device_failed_cb (ChDeviceQueue	*device_queue,
					  GUsbDevice	*device,
					  const gchar	*error_message,
					  ChFactoryPrivate *priv)
{
	g_return_if_fail (CH_IS_DEVICE_QUEUE (device_queue));
	g_return_if_fail (G_USB_IS_DEVICE (device));
	g_return_if_fail (error_message != NULL);
	g_debug ("device %s down, error: %s",
		 g_usb_device_get_platform_id (device),
		 error_message);
	ch_factory_set_device_enabled (priv, device, FALSE);
	ch_factory_set_device_error (priv, device, error_message);
}

static void
ch_factory_device_queue_progress_changed_cb (ChDeviceQueue	*device_queue,
					     guint		 percentage,
					     ChFactoryPrivate	*priv)
{
	GtkWidget *widget;
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "progressbar_queue"));
	g_debug ("queue complete %i%%", percentage);
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (widget), (gdouble) percentage / 100.0f);
}

/**
 * ch_factory_ignore_cb:
 **/
static void
ch_factory_ignore_cb (const gchar *log_domain, GLogLevelFlags log_level,
		      const gchar *message, gpointer user_data)
{
}

/**
 * main:
 **/
int
main (int argc, char **argv)
{
	ChFactoryPrivate *priv;
	gboolean ret;
	gboolean verbose = FALSE;
	gchar *database_uri = NULL;
	GError *error = NULL;
	GOptionContext *context;
	int status = 0;
	const GOptionEntry options[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
			/* TRANSLATORS: command line option */
			_("Show extra debugging information"), NULL },
		{ NULL}
	};

	setlocale (LC_ALL, "");

	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	gtk_init (&argc, &argv);

	/* TRANSLATORS: A program to factory calibrate the hardware */
	context = g_option_context_new (_("ColorHug CCMX loader"));
	g_option_context_add_group (context, gtk_get_option_group (TRUE));
	g_option_context_add_main_entries (context, options, NULL);
	ret = g_option_context_parse (context, &argc, &argv, &error);
	if (!ret) {
		g_warning ("%s: %s",
			   _("Failed to parse command line options"),
			   error->message);
		g_error_free (error);
	}
	g_option_context_free (context);

	priv = g_new0 (ChFactoryPrivate, 1);
	priv->client = cd_client_new ();
	priv->database = ch_database_new ();
	priv->usb_ctx = g_usb_context_new (NULL);
	priv->sample_window = cd_sample_window_new ();
	priv->device_queue = ch_device_queue_new ();
	priv->settings = g_settings_new ("com.hughski.colorhug-tools");
	priv->local_calibration_uri = g_settings_get_string (priv->settings,
							     "local-calibration-uri");
	priv->local_firmware_uri = g_settings_get_string (priv->settings,
							  "local-firmware-uri");
	g_signal_connect (priv->device_queue,
			  "device-failed",
			  G_CALLBACK (ch_factory_device_queue_device_failed_cb),
			  priv);
	g_signal_connect (priv->device_queue,
			  "progress-changed",
			  G_CALLBACK (ch_factory_device_queue_progress_changed_cb),
			  priv);
	priv->device_list = g_usb_device_list_new (priv->usb_ctx);
	g_signal_connect (priv->device_list, "device-added",
			  G_CALLBACK (ch_factory_device_added_cb), priv);
	g_signal_connect (priv->device_list, "device-removed",
			  G_CALLBACK (ch_factory_device_removed_cb), priv);

	/* for calibration */
	priv->results = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) g_ptr_array_unref);
	priv->samples_ti1 = g_ptr_array_new_with_free_func ((GDestroyNotify) cd_color_rgb_free);

	/* set the database location */
	database_uri = g_settings_get_string (priv->settings, "database-uri");
	ch_database_set_uri (priv->database, database_uri);

	/* ensure single instance */
	priv->application = gtk_application_new ("com.hughski.ColorHug.Factory", 0);
	g_signal_connect (priv->application, "startup",
			  G_CALLBACK (ch_factory_startup_cb), priv);
	g_signal_connect (priv->application, "activate",
			  G_CALLBACK (ch_factory_activate_cb), priv);
	/* set verbose? */
	if (verbose) {
		g_setenv ("COLORHUG_VERBOSE", "1", FALSE);
	} else {
		g_log_set_handler (G_LOG_DOMAIN, G_LOG_LEVEL_DEBUG,
				   ch_factory_ignore_cb, NULL);
	}

	/* wait */
	status = g_application_run (G_APPLICATION (priv->application), argc, argv);

	g_object_unref (priv->application);
	if (priv->updates != NULL)
		g_ptr_array_unref (priv->updates);
	if (priv->device_list != NULL)
		g_object_unref (priv->device_list);
	if (priv->device_queue != NULL)
		g_object_unref (priv->device_queue);
	if (priv->usb_ctx != NULL)
		g_object_unref (priv->usb_ctx);
	if (priv->builder != NULL)
		g_object_unref (priv->builder);
	if (priv->settings != NULL)
		g_object_unref (priv->settings);
	if (priv->database != NULL)
		g_object_unref (priv->database);
	if (priv->client != NULL)
		g_object_unref (priv->client);
	g_ptr_array_unref (priv->samples_ti1);
	g_hash_table_unref (priv->results);
	g_free (priv->local_calibration_uri);
	g_free (priv->local_firmware_uri);
	g_free (priv->firmware_data);
	g_free (priv);
	g_free (database_uri);
	return status;
}
