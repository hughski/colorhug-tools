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
#include <colorhug.h>

typedef struct {
	ChDeviceQueue	*device_queue;
	GtkApplication	*application;
	GtkBuilder	*builder;
	GUsbContext	*usb_ctx;
	GUsbDeviceList	*device_list;
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
	GdkPixbuf *pixbuf;
	gint height;
	gint i;
	gint width;
	GtkWidget *widget;
	guchar *data;
	guchar *pixels;

	/* get the widget size */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "window_assemble"));
	gtk_window_get_size (GTK_WINDOW (widget), &width, &height);
	width -= 100;
	height -= 100;
	if (width < 950)
		width = 950;
	if (height < 460)
		height = 460;

	/* if no pixbuf, create it */
	g_debug ("setting RGB: %f, %f, %f", red, green, blue);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "image_success"));
	pixbuf = gtk_image_get_pixbuf (GTK_IMAGE (widget));
	if (pixbuf == NULL) {
		data = g_new0 (guchar, width * height * 3);
		pixbuf = gdk_pixbuf_new_from_data (data,
						   GDK_COLORSPACE_RGB,
						   FALSE,
						   8,
						   width,
						   height,
						   width * 3,
						   (GdkPixbufDestroyNotify) g_free,
						   NULL);
		gtk_image_set_from_pixbuf (GTK_IMAGE (widget),
					   pixbuf);
	}

	/* get the pixbuf size */
	height = gdk_pixbuf_get_height (pixbuf);
	width = gdk_pixbuf_get_width (pixbuf);

	/* set the pixel array */
	pixels = gdk_pixbuf_get_pixels (pixbuf);
	for (i = 0; i < width * height * 3; i += 3) {
		pixels[i+0] = (guchar) (red * 255.0f);
		pixels[i+1] = (guchar) (green * 255.0f);
		pixels[i+2] = (guchar) (blue * 255.0f);
	}

	/* force redraw */
	gtk_widget_set_visible (widget, FALSE);
	gtk_widget_set_visible (widget, TRUE);

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
	gboolean ret;
	GError *error = NULL;

	/* get result */
	ret = ch_device_queue_process_finish (priv->device_queue,
					      res,
					      &error);
	if (!ret) {
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
		g_error_free (error);
		return;
	}
	ch_assemble_set_label (priv, _("All okay!"));
	ch_assemble_set_color (priv, 0.0f, 1.0f, 0.0f);
}

/**
 * ch_assemble_got_device:
 **/
static void
ch_assemble_got_device (ChAssemblePrivate *priv, GUsbDevice *device)
{
	gboolean ret;

	/* open device */
	ret = ch_device_open (device, NULL);
	if (!ret) {
		/* TRANSLATORS: permissions error perhaps? */
		ch_assemble_set_label (priv, _("Failed to open device"));
		ch_assemble_set_color (priv, 0, 0, 0);
		return;
	}

	/* test the device */
	ch_assemble_set_label (priv, _("Testing device..."));
	ch_assemble_set_color (priv, 0.5f, 0.5f, 0.5f);
	ch_device_queue_self_test (priv->device_queue,
				   device);
	ch_device_queue_process_async (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       ch_assemble_self_test_cb,
				       priv);
}

/**
 * ch_assemble_removed_device:
 **/
static void
ch_assemble_removed_device (ChAssemblePrivate *priv, GUsbDevice *device)
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
	GError *error = NULL;
	gint retval;
	GtkWidget *main_window;

	/* get UI */
	priv->builder = gtk_builder_new ();
	retval = gtk_builder_add_from_file (priv->builder,
					    CH_DATA "/ch-assemble.ui",
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

	main_window = GTK_WIDGET (gtk_builder_get_object (priv->builder, "window_assemble"));
	gtk_application_add_window (priv->application, GTK_WINDOW (main_window));
	gtk_widget_set_size_request (main_window, 600, 400);

	/* Set initial state */
	ch_assemble_set_label (priv, _("No device detected"));
	ch_assemble_set_color (priv, 0.5f, 0.5f, 0.5f);

	/* is the colorhug already plugged in? */
	g_usb_device_list_coldplug (priv->device_list);

	/* show main UI */
	gtk_window_maximize (GTK_WINDOW (main_window));
	gtk_widget_show (main_window);
out:
	return;
}

/**
 * ch_assemble_device_added_cb:
 **/
static void
ch_assemble_device_added_cb (GUsbDeviceList *list,
			     GUsbDevice *device,
			     ChAssemblePrivate *priv)
{
	if (ch_device_is_colorhug (device)) {
		g_debug ("Added ColorHug: %s",
			 g_usb_device_get_platform_id (device));
		ch_assemble_got_device (priv, device);
	}
}

/**
 * ch_assemble_device_removed_cb:
 **/
static void
ch_assemble_device_removed_cb (GUsbDeviceList *list,
			       GUsbDevice *device,
			       ChAssemblePrivate *priv)
{
	if (ch_device_is_colorhug (device)) {
		g_debug ("Removed ColorHug: %s",
			 g_usb_device_get_platform_id (device));
		ch_assemble_removed_device (priv, device);
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
	priv->usb_ctx = g_usb_context_new (NULL);
	priv->device_queue = ch_device_queue_new ();
	priv->device_list = g_usb_device_list_new (priv->usb_ctx);
	g_signal_connect (priv->device_list, "device-added",
			  G_CALLBACK (ch_assemble_device_added_cb), priv);
	g_signal_connect (priv->device_list, "device-removed",
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
	if (priv->device_list != NULL)
		g_object_unref (priv->device_list);
	if (priv->device_queue != NULL)
		g_object_unref (priv->device_queue);
	if (priv->usb_ctx != NULL)
		g_object_unref (priv->usb_ctx);
	if (priv->builder != NULL)
		g_object_unref (priv->builder);
	g_free (priv);
	return status;
}
