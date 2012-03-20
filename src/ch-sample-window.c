/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009-2010 Richard Hughes <richard@hughsie.com>
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

#include <glib-object.h>
#include "ch-sample-window.h"

static void     ch_sample_window_finalize	(GObject     *object);

#define CH_SAMPLE_WINDOW_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CH_TYPE_SAMPLE_WINDOW, ChSampleWindowPrivate))
#define CH_SAMPLE_WINDOW_PULSE_DELAY		80	/* ms */

/**
 * ChSampleWindowPrivate:
 *
 * Private #ChSampleWindow data
 **/
struct _ChSampleWindowPrivate
{
	GtkWidget			*image;
	GtkWidget			*progress_bar;
	guint				 pulse_id;
};

G_DEFINE_TYPE (ChSampleWindow, ch_sample_window, GTK_TYPE_WINDOW)

/**
 * ch_sample_window_pulse_cb:
 **/
static gboolean
ch_sample_window_pulse_cb (ChSampleWindow *sample_window)
{
	gtk_progress_bar_pulse (GTK_PROGRESS_BAR (sample_window->priv->progress_bar));
	return TRUE;
}

/**
 * ch_sample_window_set_fraction:
 * @sample_window: a valid #ChSampleWindow instance
 * @percentage: the fraction value to show, or -1 for pulsing.
 *
 * Sets the percentage value on the window.
 **/
void
ch_sample_window_set_fraction (ChSampleWindow *sample_window, gdouble fraction)
{
	ChSampleWindowPrivate *priv = sample_window->priv;

	/* make pulse */
	if (fraction == -1) {
		if (priv->pulse_id == 0) {
			priv->pulse_id = g_timeout_add (CH_SAMPLE_WINDOW_PULSE_DELAY,
							(GSourceFunc) ch_sample_window_pulse_cb,
							sample_window);
		}
		return;
	}

	/* no more pulsing */
	if (priv->pulse_id != 0) {
		g_source_remove (priv->pulse_id);
		priv->pulse_id = 0;
	}

	/* set static value */
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (sample_window->priv->progress_bar), fraction);
}

/**
 * ch_sample_window_set_color:
 * @sample_window: a valid #ChSampleWindow instance
 * @red: the red color value
 * @green: the green color value
 * @blue: the blue color value
 *
 * Sets the window to a specific color.
 **/
void
ch_sample_window_set_color (ChSampleWindow *sample_window, const CdColorRGB *color)
{
	GdkPixbuf *pixbuf;
	gint width;
	gint height;
	gint i;
	guchar *data;
	guchar *pixels;
	GtkWindow *window = GTK_WINDOW (sample_window);

	/* get the window size */
	gtk_window_get_size (window, &width, &height);

	/* if no pixbuf, create it */
	g_debug ("setting RGB: %f, %f, %f", color->R, color->G, color->B);
	pixbuf = gtk_image_get_pixbuf (GTK_IMAGE (sample_window->priv->image));
	if (pixbuf == NULL) {
		data = g_new0 (guchar, width*height*3);
		pixbuf = gdk_pixbuf_new_from_data (data, GDK_COLORSPACE_RGB, FALSE, 8, width, height, width*3, (GdkPixbufDestroyNotify) g_free, NULL);
		gtk_image_set_from_pixbuf (GTK_IMAGE (sample_window->priv->image), pixbuf);
	}

	/* get the pixbuf size */
	height = gdk_pixbuf_get_height (pixbuf);
	width = gdk_pixbuf_get_width (pixbuf);

	/* set the pixel array */
	pixels = gdk_pixbuf_get_pixels (pixbuf);
	for (i=0; i<width*height*3; i+=3) {
		pixels[i+0] = (guchar) (color->R * 255.0f);
		pixels[i+1] = (guchar) (color->G * 255.0f);
		pixels[i+2] = (guchar) (color->B * 255.0f);
	}

	/* force redraw */
	gtk_widget_set_visible (sample_window->priv->image, FALSE);
	gtk_widget_set_visible (sample_window->priv->image, TRUE);
}

/**
 * ch_sample_window_class_init:
 **/
static void
ch_sample_window_class_init (ChSampleWindowClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = ch_sample_window_finalize;
	g_type_class_add_private (klass, sizeof (ChSampleWindowPrivate));
}

/**
 * ch_sample_window_enter_notify_cb:
 **/
static gboolean
ch_sample_window_enter_notify_cb (GtkWidget *widget, GdkEventCrossing *event, ChSampleWindow *sample_window)
{
	GdkCursor *cursor;

	/* hide cursor */
	cursor = gdk_cursor_new_for_display (gdk_display_get_default (), GDK_BLANK_CURSOR);
	gdk_window_set_cursor (gtk_widget_get_window (widget), cursor);
	g_object_unref (cursor);

	return FALSE;
}

/**
 * ch_sample_window_leave_notify_cb:
 **/
static gboolean
ch_sample_window_leave_notify_cb (GtkWidget *widget, GdkEventCrossing *event,
				   ChSampleWindow *sample_window)
{
	/* restore cursor */
	gdk_window_set_cursor (gtk_widget_get_window (widget), NULL);
	return FALSE;
}

static gboolean
ch_sample_window_visibility_notify_cb (GtkWidget *widget, GdkEventVisibility *event,
					ChSampleWindow *sample_window)
{
	/* reshow it */
	gtk_window_present (GTK_WINDOW (widget));
	return TRUE;
}

/**
 * ch_sample_window_init:
 **/
static void
ch_sample_window_init (ChSampleWindow *sample_window)
{
	GtkWindow *window = GTK_WINDOW (sample_window);
	GtkWidget *vbox;
	sample_window->priv = CH_SAMPLE_WINDOW_GET_PRIVATE (sample_window);
	sample_window->priv->image = gtk_image_new ();
	sample_window->priv->progress_bar = gtk_progress_bar_new ();

	/* pack in two widgets into the window */
	vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
	gtk_container_add (GTK_CONTAINER (sample_window), vbox);
	gtk_box_pack_start (GTK_BOX (vbox), sample_window->priv->image, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), sample_window->priv->progress_bar, FALSE, FALSE, 0);
	gtk_widget_set_size_request (sample_window->priv->image, 400, 400);
	gtk_widget_show_all (vbox);

	/* be clever and allow the colorimeter to do it's job */
	g_signal_connect (window, "enter-notify-event",
			  G_CALLBACK(ch_sample_window_enter_notify_cb),
			  sample_window);
	g_signal_connect (window, "leave-notify-event",
			  G_CALLBACK(ch_sample_window_leave_notify_cb),
			  sample_window);
	g_signal_connect (window, "visibility-notify-event",
			  G_CALLBACK(ch_sample_window_visibility_notify_cb),
			  sample_window);

	/* show on all virtual desktops */
	gtk_window_stick (window);
}

/**
 * ch_sample_window_finalize:
 **/
static void
ch_sample_window_finalize (GObject *object)
{
	ChSampleWindow *sample_window = CH_SAMPLE_WINDOW (object);
	ChSampleWindowPrivate *priv = sample_window->priv;

	if (priv->pulse_id != 0)
		g_source_remove (priv->pulse_id);

	G_OBJECT_CLASS (ch_sample_window_parent_class)->finalize (object);
}

/**
 * ch_sample_window_new:
 *
 * Return value: a new #ChSampleWindow object.
 **/
GtkWindow *
ch_sample_window_new (void)
{
	ChSampleWindow *sample_window;
	sample_window = g_object_new (CH_TYPE_SAMPLE_WINDOW,
				      "accept-focus", FALSE,
				      "decorated", FALSE,
				      "default-height", 400,
				      "default-width", 400,
				      "deletable", FALSE,
				      "destroy-with-parent", TRUE,
				      "icon-name", "icc-profile",
				      "resizable", FALSE,
				      "skip-pager-hint", TRUE,
				      "skip-taskbar-hint", TRUE,
				      "title", "calibration square",
				      "type-hint", GDK_WINDOW_TYPE_HINT_SPLASHSCREEN,
				      "urgency-hint", TRUE,
				      NULL);
	return GTK_WINDOW (sample_window);
}

