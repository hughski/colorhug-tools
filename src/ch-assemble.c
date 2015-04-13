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

#include "config.h"

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <locale.h>
#include <colord.h>
#include <colord-gtk.h>
#include <colorhug.h>

#include "ch-cleanup.h"

typedef struct {
	CdSampleWidget	*sample_widget;
	ChDeviceQueue	*device_queue;
	GUsbDevice	*device;
	GtkApplication	*application;
	GtkBuilder	*builder;
	GUsbContext	*usb_ctx;
} ChAssemblePrivate;

/**
 * ch_assemble_activate_cb:
 **/
static void
ch_assemble_activate_cb (GApplication *application,
			 ChAssemblePrivate *priv)
{
	GtkWindow *window;
	window = GTK_WINDOW (gtk_builder_get_object (priv->builder, "window_assemble"));
	gtk_window_present (window);
}

/**
 * ch_assemble_set_label:
 **/
static void
ch_assemble_set_label (ChAssemblePrivate *priv, const gchar *text)
{
	GtkWidget *widget;
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_success"));
	gtk_label_set_markup (GTK_LABEL (widget), text);

}

/**
 * ch_assemble_set_color:
 **/
static void
ch_assemble_set_color (ChAssemblePrivate *priv,
		       gdouble red,
		       gdouble green,
		       gdouble blue)
{
	CdColorRGB color;

	/* set color */
	color.R = red;
	color.G = green;
	color.B = blue;
	cd_sample_widget_set_color (priv->sample_widget, &color);

	/* force redraw */
	gtk_widget_set_visible (GTK_WIDGET (priv->sample_widget), FALSE);
	gtk_widget_set_visible (GTK_WIDGET (priv->sample_widget), TRUE);

}

/**
 * ch_assemble_set_leds_flash_cb:
 **/
static void
ch_assemble_set_leds_flash_cb (GObject *source, GAsyncResult *res, gpointer user_data)
{
	ChAssemblePrivate *priv = (ChAssemblePrivate *) user_data;
	_cleanup_error_free_ GError *error = NULL;

	/* get result */
	if (!ch_device_queue_process_finish (priv->device_queue, res, &error)) {
		g_warning ("Failed to set LEDs: %s", error->message);
		return;
	}
}

/**
 * ch_assemble_set_serial_cb:
 **/
static void
ch_assemble_set_serial_cb (GObject *source, GAsyncResult *res, gpointer user_data)
{
	ChAssemblePrivate *priv = (ChAssemblePrivate *) user_data;
	_cleanup_error_free_ GError *error = NULL;

	/* get result */
	if (!ch_device_queue_process_finish (priv->device_queue, res, &error)) {
		g_warning ("Failed to set flash success number: %s", error->message);
		return;
	}

	/* set LEDs green */
	ch_device_queue_set_leds (priv->device_queue, priv->device,
				  CH_STATUS_LED_GREEN, 0, 0, 0);
	ch_device_queue_process_async (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       ch_assemble_set_leds_flash_cb,
				       priv);

	ch_assemble_set_label (priv, _("All okay!"));
	ch_assemble_set_color (priv, 0.0f, 1.0f, 0.0f);
}

/**
 * ch_assemble_boot_flash_cb:
 **/
static void
ch_assemble_boot_flash_cb (GObject *source, GAsyncResult *res, gpointer user_data)
{
	ChAssemblePrivate *priv = (ChAssemblePrivate *) user_data;
	_cleanup_error_free_ GError *error = NULL;

	/* get result */
	if (!ch_device_queue_process_finish (priv->device_queue, res, &error)) {
		g_warning ("Failed to boot firmware: %s", error->message);
		return;
	}
}

/**
 * ch_assemble_firmware_cb:
 **/
static void
ch_assemble_firmware_cb (GObject *source, GAsyncResult *res, gpointer user_data)
{
	ChAssemblePrivate *priv = (ChAssemblePrivate *) user_data;
	_cleanup_error_free_ GError *error = NULL;

	/* get result */
	if (!ch_device_queue_process_finish (priv->device_queue, res, &error)) {
		g_warning ("Failed to flash firmware: %s", error->message);
		return;
	}

	ch_assemble_set_label (priv, _("Booting firmware..."));
	ch_assemble_set_color (priv, 0.5f, 0.5f, 0.5f);
	ch_device_queue_boot_flash (priv->device_queue, priv->device);
	ch_device_queue_process_async (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       ch_assemble_boot_flash_cb,
				       priv);
}

/**
 * _ch_device_get_download_id:
 * @device: the #GUsbDevice
 *
 * Returns the string identifier to use for the device type.
 *
 * Return value: string, e.g. "colorhug2"
 *
 * Since: x.x.x
 **/
static const gchar *
_ch_device_get_download_id (GUsbDevice *device)
{
	const char *str = NULL;
	switch (ch_device_get_mode (device)) {
	case CH_DEVICE_MODE_LEGACY:
	case CH_DEVICE_MODE_BOOTLOADER:
	case CH_DEVICE_MODE_FIRMWARE:
		str = "colorhug";
		break;
	case CH_DEVICE_MODE_BOOTLOADER2:
	case CH_DEVICE_MODE_FIRMWARE2:
		str = "colorhug2";
		break;
	case CH_DEVICE_MODE_BOOTLOADER_PLUS:
	case CH_DEVICE_MODE_FIRMWARE_PLUS:
		str = "colorhug-plus";
		break;
	default:
		break;
	}
	return str;
}

/**
 * ch_assemble_flash_firmware:
 **/
static void
ch_assemble_flash_firmware (ChAssemblePrivate *priv)
{
	gsize len;
	_cleanup_error_free_ GError *error = NULL;
	_cleanup_free_ gchar *fn = NULL;
	_cleanup_free_ guint8 *data = NULL;

	/* set to false */
	ch_device_queue_set_flash_success (priv->device_queue,
					   priv->device, 0);

	switch (ch_device_get_mode (priv->device)) {
	case CH_DEVICE_MODE_BOOTLOADER:
	case CH_DEVICE_MODE_BOOTLOADER2:
	case CH_DEVICE_MODE_BOOTLOADER_ALS:

		/* read firmware */
		ch_assemble_set_color (priv, 0.25f, 0.25f, 0.25f);
		ch_assemble_set_label (priv, _("Loading firmware..."));
		fn = g_strdup_printf ("./firmware/%s/firmware.bin",
				       _ch_device_get_download_id (priv->device));
		if (!g_file_get_contents (fn, (gchar **) &data, &len, &error)) {
			g_warning ("Failed to open firmware file %s: %s",
				   fn, error->message);
			return;
		}

		/* write to device */
		ch_assemble_set_color (priv, 0.5f, 0.5f, 0.5f);
		ch_assemble_set_label (priv, _("Writing firmware..."));
		ch_device_queue_write_firmware (priv->device_queue, priv->device,
						data, len);
		break;
	default:
		break;
	}

	ch_device_queue_process_async (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       ch_assemble_firmware_cb,
				       priv);
}

/**
 * ch_assemble_self_test_cb:
 **/
static void
ch_assemble_self_test_cb (GObject *source,
			  GAsyncResult *res,
			  gpointer user_data)
{
	ChAssemblePrivate *priv = (ChAssemblePrivate *) user_data;
	_cleanup_error_free_ GError *error = NULL;

	/* get result */
	if (!ch_device_queue_process_finish (priv->device_queue, res, &error)) {
		switch (error->code) {
		case CH_ERROR_SELF_TEST_SENSOR:
			ch_assemble_set_color (priv, 1.0f, 0.0f, 0.0f);
			ch_assemble_set_label (priv, _("FAILED: Sensor"));
			break;
		case CH_ERROR_SELF_TEST_RED:
			ch_assemble_set_color (priv, 1.0f, 0.0f, 0.0f);
			ch_assemble_set_label (priv, _("FAILED: Sensor Red"));
			break;
		case CH_ERROR_SELF_TEST_GREEN:
			ch_assemble_set_color (priv, 1.0f, 0.0f, 0.0f);
			ch_assemble_set_label (priv, _("FAILED: Sensor Green"));
			break;
		case CH_ERROR_SELF_TEST_BLUE:
			ch_assemble_set_color (priv, 1.0f, 0.0f, 0.0f);
			ch_assemble_set_label (priv, _("FAILED: Sensor Blue"));
			break;
		case CH_ERROR_SELF_TEST_MULTIPLIER:
			ch_assemble_set_color (priv, 1.0f, 0.0f, 0.0f);
			ch_assemble_set_label (priv, _("FAILED: Sensor Multiplier"));
			break;
		case CH_ERROR_SELF_TEST_COLOR_SELECT:
			ch_assemble_set_color (priv, 1.0f, 0.0f, 0.0f);
			ch_assemble_set_label (priv, _("FAILED: Sensor Color Select"));
			break;
		case CH_ERROR_UNKNOWN_CMD:
			ch_assemble_set_color (priv, 1.0f, 1.0f, 0.0f);
			ch_assemble_set_label (priv, _("FAILED: Old bootloader version"));
			break;
		default:
			ch_assemble_set_color (priv, 0.7f, 0.1f, 0.1f);
			ch_assemble_set_label (priv, error->message);
			break;
		}
		g_debug ("failed to self test [%i]: %s",
			 error->code, error->message);
		return;
	}
	ch_assemble_set_label (priv, _("All okay!"));
	ch_assemble_set_color (priv, 0.0f, 1.0f, 0.0f);

	/* flash firmware */
	ch_assemble_flash_firmware (priv);
}

/**
 * ch_assemble_got_device_bl:
 **/
static void
ch_assemble_got_device_bl (ChAssemblePrivate *priv)
{
	_cleanup_error_free_ GError *error = NULL;

	/* open device */
	if (!ch_device_open (priv->device, &error)) {
		g_warning ("Failed to open: %s", error->message);
		/* TRANSLATORS: permissions error perhaps? */
		ch_assemble_set_label (priv, _("Failed to open device"));
		ch_assemble_set_color (priv, 0, 0, 0);
		return;
	}

	/* test the device */
	ch_assemble_set_label (priv, _("Testing device..."));
	ch_assemble_set_color (priv, 0.5f, 0.5f, 0.5f);
	ch_device_queue_self_test (priv->device_queue,
				   priv->device);
	ch_device_queue_process_async (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       ch_assemble_self_test_cb,
				       priv);
}

/**
 * ch_assemble_got_device_fw:
 **/
static void
ch_assemble_got_device_fw (ChAssemblePrivate *priv)
{
	_cleanup_error_free_ GError *error = NULL;

	/* open device */
	if (!ch_device_open (priv->device, &error)) {
		g_warning ("Failed to open: %s", error->message);
		/* TRANSLATORS: permissions error perhaps? */
		ch_assemble_set_label (priv, _("Failed to open device"));
		ch_assemble_set_color (priv, 0, 0, 0);
		return;
	}

	ch_assemble_set_label (priv, _("Setting flash success..."));
	ch_assemble_set_color (priv, 0.5f, 0.5f, 0.5f);
	ch_device_queue_set_flash_success (priv->device_queue, priv->device, 1);
	ch_device_queue_process_async (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       ch_assemble_set_serial_cb,
				       priv);
}

/**
 * ch_assemble_removed_device:
 **/
static void
ch_assemble_removed_device (ChAssemblePrivate *priv)
{
	ch_assemble_set_label (priv, _("No device detected"));
	ch_assemble_set_color (priv, 0.0f, 0.0f, 0.0f);
}

/**
 * ch_assemble_startup_cb:
 **/
static void
ch_assemble_startup_cb (GApplication *application, ChAssemblePrivate *priv)
{
	_cleanup_error_free_ GError *error = NULL;
	gint retval;
	GtkWidget *box;
	GtkWidget *main_window;

	/* get UI */
	priv->builder = gtk_builder_new ();
	retval = gtk_builder_add_from_file (priv->builder,
					    CH_DATA "/ch-assemble.ui",
					    &error);
	if (retval == 0) {
		g_warning ("failed to load ui: %s", error->message);
		return;
	}

	/* add application specific icons to search path */
	gtk_icon_theme_append_search_path (gtk_icon_theme_get_default (),
					   CH_DATA G_DIR_SEPARATOR_S "icons");

	main_window = GTK_WIDGET (gtk_builder_get_object (priv->builder, "window_assemble"));
	gtk_application_add_window (priv->application, GTK_WINDOW (main_window));
	gtk_widget_set_size_request (main_window, 600, 400);

	/* Set initial state */
	box = GTK_WIDGET (gtk_builder_get_object (priv->builder, "box1"));
	gtk_box_pack_start (GTK_BOX (box), GTK_WIDGET (priv->sample_widget), TRUE, TRUE, 0);
	ch_assemble_set_label (priv, _("No device detected"));
	ch_assemble_set_color (priv, 0.5f, 0.5f, 0.5f);

	/* is the colorhug already plugged in? */
	g_usb_context_enumerate (priv->usb_ctx);

	/* show main UI */
	gtk_window_maximize (GTK_WINDOW (main_window));
	gtk_widget_show (main_window);
}

/**
 * ch_assemble_device_added_cb:
 **/
static void
ch_assemble_device_added_cb (GUsbContext *ctx,
			     GUsbDevice *device,
			     ChAssemblePrivate *priv)
{
	switch (ch_device_get_mode (device)) {
	case CH_DEVICE_MODE_BOOTLOADER:
	case CH_DEVICE_MODE_BOOTLOADER2:
	case CH_DEVICE_MODE_BOOTLOADER_PLUS:
	case CH_DEVICE_MODE_BOOTLOADER_ALS:
		g_debug ("Added ColorHug Bootloader: %s",
			 g_usb_device_get_platform_id (device));
		if (priv->device != NULL)
			g_object_unref (priv->device);
		priv->device = g_object_ref (device);
		ch_assemble_got_device_bl (priv);
		break;
	case CH_DEVICE_MODE_FIRMWARE:
	case CH_DEVICE_MODE_FIRMWARE2:
	case CH_DEVICE_MODE_FIRMWARE_PLUS:
	case CH_DEVICE_MODE_FIRMWARE_ALS:
		g_debug ("Added ColorHug Firmware: %s",
			 g_usb_device_get_platform_id (device));
		if (priv->device != NULL)
			g_object_unref (priv->device);
		priv->device = g_object_ref (device);
		ch_assemble_got_device_fw (priv);
		break;
	default:
		break;
	}
}

/**
 * ch_assemble_device_removed_cb:
 **/
static void
ch_assemble_device_removed_cb (GUsbContext *ctx,
			       GUsbDevice *device,
			       ChAssemblePrivate *priv)
{
	if (ch_device_is_colorhug (device)) {
		g_debug ("Removed ColorHug: %s",
			 g_usb_device_get_platform_id (device));
		ch_assemble_removed_device (priv);
	}
}

/**
 * ch_assemble_ignore_cb:
 **/
static void
ch_assemble_ignore_cb (const gchar *log_domain, GLogLevelFlags log_level,
		       const gchar *message, gpointer user_data)
{
}

/**
 * main:
 **/
int
main (int argc, char **argv)
{
	ChAssemblePrivate *priv;
	gboolean ret;
	gboolean verbose = FALSE;
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

	/* TRANSLATORS: A program to assemble calibrate the hardware */
	context = g_option_context_new (_("ColorHug assembly tester"));
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

	priv = g_new0 (ChAssemblePrivate, 1);
	priv->sample_widget = CD_SAMPLE_WIDGET (cd_sample_widget_new ());
	priv->usb_ctx = g_usb_context_new (NULL);
	priv->device_queue = ch_device_queue_new ();
	g_signal_connect (priv->usb_ctx, "device-added",
			  G_CALLBACK (ch_assemble_device_added_cb), priv);
	g_signal_connect (priv->usb_ctx, "device-removed",
			  G_CALLBACK (ch_assemble_device_removed_cb), priv);

	/* ensure single instance */
	priv->application = gtk_application_new ("com.hughski.ColorHug.Assemble", 0);
	g_signal_connect (priv->application, "startup",
			  G_CALLBACK (ch_assemble_startup_cb), priv);
	g_signal_connect (priv->application, "activate",
			  G_CALLBACK (ch_assemble_activate_cb), priv);
	/* set verbose? */
	if (verbose) {
		g_setenv ("COLORHUG_VERBOSE", "1", FALSE);
	} else {
		g_log_set_handler (G_LOG_DOMAIN, G_LOG_LEVEL_DEBUG,
				   ch_assemble_ignore_cb, NULL);
	}

	/* wait */
	status = g_application_run (G_APPLICATION (priv->application), argc, argv);

	g_object_unref (priv->application);
	if (priv->device_queue != NULL)
		g_object_unref (priv->device_queue);
	if (priv->usb_ctx != NULL)
		g_object_unref (priv->usb_ctx);
	if (priv->builder != NULL)
		g_object_unref (priv->builder);
	g_free (priv);
	return status;
}
