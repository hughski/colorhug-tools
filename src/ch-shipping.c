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
#include <stdlib.h>
#include <lcms2.h>
#include <canberra-gtk.h>

#include "ch-cleanup.h"
#include "ch-cell-renderer-date.h"
#include "ch-cell-renderer-postage.h"
#include "ch-cell-renderer-uint32.h"
#include "ch-cell-renderer-order-status.h"
#include "ch-database.h"
#include "ch-shipping-common.h"

typedef struct {
	GSettings	*settings;
	GtkApplication	*application;
	GtkBuilder	*builder;
	ChDatabase	*database;
	GMainLoop	*loop;
	guint32		 order_to_print;
} ChFactoryPrivate;

enum {
	COLUMN_CHECKBOX,
	COLUMN_ORDER_ID,
	COLUMN_NAME,
	COLUMN_ADDRESS,
	COLUMN_EMAIL,
	COLUMN_FILENAME,
	COLUMN_TRACKING,
	COLUMN_SHIPPED,
	COLUMN_POSTAGE,
	COLUMN_DEVICE_IDS,
	COLUMN_COMMENT,
	COLUMN_ORDER_STATE,
	COLUMN_LAST
};

/**
 * ch_shipping_error_dialog:
 **/
static void
ch_shipping_error_dialog (ChFactoryPrivate *priv,
			  const gchar *title,
			  const gchar *message)
{
	GtkWindow *window;
	GtkWidget *dialog;

	window = GTK_WINDOW(gtk_builder_get_object (priv->builder, "dialog_shipping"));
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
 * ch_shipping_activate_cb:
 **/
static void
ch_shipping_activate_cb (GApplication *application, ChFactoryPrivate *priv)
{
	GtkWindow *window;
	window = GTK_WINDOW (gtk_builder_get_object (priv->builder, "dialog_shipping"));
	gtk_window_present (window);
}

/**
 * ch_shipping_close_button_cb:
 **/
static void
ch_shipping_close_button_cb (GtkWidget *widget, ChFactoryPrivate *priv)
{
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "dialog_shipping"));
	gtk_widget_destroy (widget);
}

/**
 * ch_shipping_find_by_id:
 **/
static gboolean
ch_shipping_find_by_id (GtkTreeModel *model,
		        GtkTreeIter *iter_found,
		        guint order_id)
{
	gboolean ret;
	GtkTreeIter iter;
	guint order_id_tmp;

	ret = gtk_tree_model_get_iter_first (model, &iter);
	while (ret) {
		gtk_tree_model_get (model, &iter,
				    COLUMN_ORDER_ID, &order_id_tmp,
				    -1);
		if (order_id_tmp == order_id) {
			*iter_found = iter;
			break;
		}
		ret = gtk_tree_model_iter_next (model, &iter);
	}
	return ret;
}

/**
 * ch_shipping_refresh_status:
 **/
static void
ch_shipping_refresh_status (ChFactoryPrivate *priv)
{
	GtkWidget *widget;
	guint ch1s;
	guint ch2s;
	_cleanup_error_free_ GError *error = NULL;
	_cleanup_free_ gchar *label = NULL;

	/* update status */
	ch1s = ch_database_device_get_number (priv->database, CH_DEVICE_STATE_CALIBRATED, 1, &error);
	if (ch1s == G_MAXUINT) {
		ch_shipping_error_dialog (priv, "Failed to get number of devices", error->message);
		return;
	}
	ch2s = ch_database_device_get_number (priv->database, CH_DEVICE_STATE_CALIBRATED, 2, &error);
	if (ch2s == G_MAXUINT) {
		ch_shipping_error_dialog (priv, "Failed to get number of devices", error->message);
		return;
	}
	label = g_strdup_printf ("%i ColorHug and %i ColorHug2 remaining to be sold", ch1s, ch2s);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_status"));
	gtk_label_set_text (GTK_LABEL (widget), label);
}

/**
 * ch_shipping_order_get_device_ids:
 **/
static gchar *
ch_shipping_order_get_device_ids (ChFactoryPrivate *priv,
				  guint32 order_id,
				  GError **error)
{
	GArray *array;
	gchar *tmp = NULL;
	GString *string;
	guint32 device_id;
	guint i;

	/* get device IDs */
	array = ch_database_order_get_device_ids (priv->database,
						  order_id,
						  error);
	if (array == NULL)
		goto out;

	/* make into a string */
	string = g_string_new ("");
	for (i = 0; i < array->len; i++) {
		device_id = g_array_index (array, guint32, i);
		g_string_append_printf (string, "%04i,", device_id);
	}
	if (string->len > 0)
		g_string_set_size (string, string->len - 1);
	tmp = g_string_free (string, FALSE);
out:
	if (array != NULL)
		g_array_unref (array);
	return tmp;
}

/* hack */
static void ch_shipping_print_invoice (ChFactoryPrivate *priv, GtkTreeModel *model, GtkTreeIter *iter);
static void ch_shipping_print_label (ChFactoryPrivate *priv, GtkTreeModel *model, GtkTreeIter *iter);
static void ch_shipping_print_cn22 (ChFactoryPrivate *priv, GtkTreeModel *model, GtkTreeIter *iter);
static void ch_shipping_email_send_email (ChFactoryPrivate *priv, GtkTreeModel *model, GtkTreeIter *iter);

/**
 * ch_shipping_refresh_orders:
 **/
static void
ch_shipping_refresh_orders (ChFactoryPrivate *priv)
{
	ChDatabaseOrder *order;
	gboolean ret;
	gchar *name_tmp;
	GError *error = NULL;
	GPtrArray *array;
	GtkListStore *list_store;
	GtkTreeIter iter;
	gchar *device_ids = NULL;
	guint i;
	guint order_id_next = 0;

	list_store = GTK_LIST_STORE (gtk_builder_get_object (priv->builder, "liststore_orders"));
	array = ch_database_get_all_orders (priv->database, &error);
	if (array == NULL) {
		ch_shipping_error_dialog (priv, "Failed to get all orders", error->message);
		g_error_free (error);
		goto out;
	}
	for (i = 0; i < array->len; i++) {
		order = g_ptr_array_index (array, i);

		/* verify we've not skipped any */
		if (order_id_next == 0)
			order_id_next = order->order_id;
		if (order->order_id != order_id_next)
			g_warning ("missing order %i", order_id_next - 1);
		order_id_next = order->order_id - 1;

		ret = ch_shipping_find_by_id (GTK_TREE_MODEL (list_store), &iter, order->order_id);
		if (!ret)
			gtk_list_store_append (list_store, &iter);

		/* get the device IDs if we can */
		device_ids = ch_shipping_order_get_device_ids (priv,
							       order->order_id,
							       NULL);
		if (device_ids == NULL)
			device_ids = g_strdup ("-");
		name_tmp = g_markup_escape_text (order->name, -1);
		gtk_list_store_set (list_store, &iter,
				    COLUMN_ORDER_ID, order->order_id,
				    COLUMN_NAME, name_tmp,
				    COLUMN_EMAIL, order->email,
				    COLUMN_ADDRESS, order->address,
				    COLUMN_TRACKING, order->tracking_number,
				    COLUMN_SHIPPED, order->sent_date,
				    COLUMN_POSTAGE, order->postage,
				    COLUMN_COMMENT, order->comment,
				    COLUMN_ORDER_STATE, order->state,
				    COLUMN_DEVICE_IDS, device_ids,
				    COLUMN_CHECKBOX, FALSE,
				    -1);

		if (order->order_id == priv->order_to_print) {
			ch_shipping_print_label (priv, GTK_TREE_MODEL (list_store), &iter);
			ch_shipping_print_invoice (priv, GTK_TREE_MODEL (list_store), &iter);
			ch_shipping_print_cn22 (priv, GTK_TREE_MODEL (list_store), &iter);
			priv->order_to_print = G_MAXUINT32;
		}

		g_free (device_ids);
		g_free (name_tmp);
	}

	/* and also status */
	ch_shipping_refresh_status (priv);
out:
	if (array != NULL)
		g_ptr_array_unref (array);
}

/**
 * ch_shipping_refund_button_cb:
 **/
static void
ch_shipping_refund_button_cb (GtkWidget *widget, ChFactoryPrivate *priv)
{
	GArray *array = NULL;
	gboolean ret;
	GError *error = NULL;
	GtkTreeIter iter;
	GtkTreeModel *model;
	GtkTreeSelection *selection;
	GtkTreeView *treeview;
	guint32 device_id;
	guint32 order_id;
	guint i;

	/* get selected item */
	treeview = GTK_TREE_VIEW (gtk_builder_get_object (priv->builder, "treeview_orders"));
	selection = gtk_tree_view_get_selection (treeview);
	ret = gtk_tree_selection_get_selected (selection, &model, &iter);
	if (!ret)
		goto out;
	gtk_tree_model_get (model, &iter,
			    COLUMN_ORDER_ID, &order_id,
			    -1);

	/* set the order state */
	ret = ch_database_order_set_state (priv->database,
					   order_id,
					   CH_ORDER_STATE_REFUNDED,
					   &error);

	if (!ret) {
		ch_shipping_error_dialog (priv, "Failed to set order state as refunded",
					  error->message);
		g_error_free (error);
		goto out;
	}

	/* get the devices for this order */
	array = ch_database_order_get_device_ids (priv->database,
						  order_id,
						  &error);
	if (array == NULL) {
		ch_shipping_error_dialog (priv, "Failed to set get device ids for order",
					  error->message);
		g_error_free (error);
		goto out;
	}

	/* re-allocate devices */
	for (i = 0; i < array->len; i++) {
		device_id = g_array_index (array, guint32, i);
		ret = ch_database_device_set_state (priv->database,
						    device_id,
						    CH_DEVICE_STATE_CALIBRATED,
						    &error);
		if (!ret) {
			ch_shipping_error_dialog (priv, "Failed to reset device state",
						  error->message);
			g_error_free (error);
			goto out;
		}
	}
	ch_shipping_refresh_orders (priv);
out:
	if (array != NULL)
		g_array_unref (array);
}

/**
 * ch_shipping_strreplace:
 **/
static gchar *
ch_shipping_strreplace (const gchar *haystack, const gchar *needle, const gchar *replace)
{
	gchar *new;
	gchar **split = NULL;

	if (g_strstr_len (haystack, -1, needle) == NULL) {
		new = g_strdup (haystack);
		goto out;
	}
	split = g_strsplit (haystack, needle, -1);
	new = g_strjoinv (replace, split);
out:
	g_strfreev (split);
	return new;
}

/**
 * ch_shipping_print_cn22:
 **/
static void
ch_shipping_print_cn22 (ChFactoryPrivate *priv, GtkTreeModel *model, GtkTreeIter *iter)
{
	ChShippingKind postage;
	gboolean ret;
	gchar *address = NULL;
	GError *error = NULL;
	GString *str = NULL;

	gtk_tree_model_get (model, iter,
			    COLUMN_POSTAGE, &postage,
			    COLUMN_ADDRESS, &address,
			    -1);

	if (postage != CH_SHIPPING_KIND_CH2_WORLD_SIGNED &&
//	    postage != CH_SHIPPING_KIND_CH2_WORLD &&
	    postage != CH_SHIPPING_KIND_CH1_WORLD &&
	    postage != CH_SHIPPING_KIND_CH1_WORLD_SIGNED &&
	    postage != CH_SHIPPING_KIND_STRAP_WORLD &&
	    postage != CH_SHIPPING_KIND_ALS_WORLD &&
	    g_strstr_len (address, -1, "Russia") == NULL &&
	    g_strstr_len (address, -1, "RUSSIA") == NULL) {
		goto out;
	}

	str = ch_shipping_string_load (CH_DATA "/cn22.tex", NULL);
	if (postage == CH_SHIPPING_KIND_STRAP_WORLD) {
		ch_shipping_string_replace (str, "$IMAGE$", "/home/hughsie/Code/ColorHug/Documents/cn22-strap.png");
	} else {
		if (ch_shipping_device_to_price (postage) == 60 ||
		    ch_shipping_device_to_price (postage) == 85) {
			ch_shipping_string_replace (str, "$IMAGE$", "/home/hughsie/Code/ColorHug/Documents/shipping60.png");
		} else if (ch_shipping_device_to_price (postage) == 20) {
			ch_shipping_string_replace (str, "$IMAGE$", "/home/hughsie/Code/ColorHug/Documents/shipping20.png");
		} else {
			ch_shipping_string_replace (str, "$IMAGE$", "/home/hughsie/Code/ColorHug/Documents/shipping48.png");
		}
	}
	g_string_append (str, "\\end{document}");

	/* print */
	ret = ch_shipping_print_latex_doc (str->str, "LP2844", &error);
	if (!ret) {
		ch_shipping_error_dialog (priv, "failed to save file: %s", error->message);
		g_error_free (error);
		goto out;
	}
out:
	g_free (address);
	if (str != NULL)
		g_string_free (str, TRUE);
}

/**
 * ch_shipping_print_invoice:
 **/
static void
ch_shipping_print_invoice (ChFactoryPrivate *priv, GtkTreeModel *model, GtkTreeIter *iter)
{
	ChShippingKind postage;
	gboolean ret;
	const gchar *live_media = NULL;
	const gchar *device_name = NULL;
	gchar *address = NULL;
	gchar *device_ids = NULL;
	gchar **address_split = NULL;
	gchar *name = NULL;
	GError *error = NULL;
	GString *str;
	guint32 order_id;
	gdouble postage_price;
	guint device_price;

	gtk_tree_model_get (model, iter,
			    COLUMN_ORDER_ID, &order_id,
			    COLUMN_DEVICE_IDS, &device_ids,
			    COLUMN_POSTAGE, &postage,
			    COLUMN_NAME, &name,
			    COLUMN_ADDRESS, &address,
			    -1);

	/* replace escaped chars */
	address = ch_shipping_strreplace (address, "$", "\\$");
	address = ch_shipping_strreplace (address, "%", "\\%");
	address = ch_shipping_strreplace (address, "_", "\\_");
	address = ch_shipping_strreplace (address, "}", "\\}");
	address = ch_shipping_strreplace (address, "{", "\\{");
	address = ch_shipping_strreplace (address, "&", "\\&");
	address = ch_shipping_strreplace (address, "#", "\\#");

	address_split = g_strsplit (address, "|", -1);
	postage_price = ch_shipping_kind_to_price (postage);
	device_price = ch_shipping_device_to_price (postage);

	switch (postage) {
	case CH_SHIPPING_KIND_CH2_UK_SIGNED:
	case CH_SHIPPING_KIND_CH2_EUROPE_SIGNED:
	case CH_SHIPPING_KIND_CH2_WORLD_SIGNED:
		live_media = "LiveUSB";
		device_name = "ColorHug2";
		break;
	default:
		live_media = "LiveCD";
		device_name = "ColorHug";
		break;
	}

	if (postage == CH_SHIPPING_KIND_STRAP_UK ||
	    postage == CH_SHIPPING_KIND_STRAP_EUROPE ||
	    postage == CH_SHIPPING_KIND_STRAP_WORLD) {
		str = ch_shipping_string_load (CH_DATA "/invoice-straps.tex", NULL);
	} else if (postage == CH_SHIPPING_KIND_ALS_UK ||
		   postage == CH_SHIPPING_KIND_ALS_EUROPE ||
		   postage == CH_SHIPPING_KIND_ALS_WORLD) {
		str = ch_shipping_string_load (CH_DATA "/invoice-als.tex", NULL);
	} else {
		str = ch_shipping_string_load (CH_DATA "/invoice.tex", NULL);
	}
	ch_shipping_string_replace (str, "$NAME$", name);
	ch_shipping_string_replace (str, "$ADDRESS1$", address_split[0]);
	ch_shipping_string_replace (str, "$ADDRESS2$", address_split[1]);
	ch_shipping_string_replace (str, "$ADDRESS3$", address_split[2]);
	ch_shipping_string_replace (str, "$ADDRESS4$", address_split[3]);
	ch_shipping_string_replace (str, "$ADDRESS5$", address_split[4]);
	ch_shipping_string_replace (str, "$ORDER$", g_strdup_printf ("%04i-1", order_id));
	ch_shipping_string_replace (str, "$DEVICES$", device_ids);
	ch_shipping_string_replace (str, "$DEVICE_PRICE$", g_strdup_printf ("%i.00", device_price));
	ch_shipping_string_replace (str, "$POSTAGE_PRICE$", g_strdup_printf ("%.2f", postage_price));
	ch_shipping_string_replace (str, "$TOTAL_PRICE$", g_strdup_printf ("%.2f", device_price + postage_price));
	ch_shipping_string_replace (str, "$POSTAGE_TYPE$", ch_shipping_kind_to_string (postage));
	ch_shipping_string_replace (str, "$LIVE_MEDIA$", live_media);
	ch_shipping_string_replace (str, "$DEVICE_NAME$", device_name);

	/* print */
	ret = ch_shipping_print_latex_doc (str->str, NULL, &error);
	if (!ret) {
		ch_shipping_error_dialog (priv, "failed to save file: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* change the state to printed */
	gtk_list_store_set (GTK_LIST_STORE (model), iter,
			    COLUMN_ORDER_STATE, CH_ORDER_STATE_PRINTED,
			    -1);
	ret = ch_database_order_set_state (priv->database,
					   order_id,
					   CH_ORDER_STATE_PRINTED,
					   &error);
	if (!ret) {
		ch_shipping_error_dialog (priv, "Failed to update order state",
					  error->message);
		g_error_free (error);
		goto out;
	}
out:
	g_free (name);
	g_free (address);
	g_free (device_ids);
	g_strfreev (address_split);
	g_string_free (str, TRUE);
}

/**
 * ch_shipping_print_invoices_button_cb:
 **/
static void
ch_shipping_print_invoices_button_cb (GtkWidget *widget, ChFactoryPrivate *priv)
{
	gboolean ret;
	GtkTreeIter iter;
	gboolean checkbox;
	GtkTreeModel *model;
	GtkTreeView *treeview;

	treeview = GTK_TREE_VIEW (gtk_builder_get_object (priv->builder, "treeview_orders"));
	model = gtk_tree_view_get_model (treeview);
	ret = gtk_tree_model_get_iter_first (model, &iter);
	while (ret) {
		gtk_tree_model_get (model, &iter,
				    COLUMN_CHECKBOX, &checkbox,
				    -1);
		if (checkbox)
			ch_shipping_print_invoice (priv, model, &iter);
		ret = gtk_tree_model_iter_next (model, &iter);
	}
}

/**
 * ch_shipping_print_cn22_button_cb:
 **/
static void
ch_shipping_print_cn22_button_cb (GtkWidget *widget, ChFactoryPrivate *priv)
{
	gboolean checkbox;
	gboolean ret;
	GtkTreeIter iter;
	GtkTreeModel *model;
	GtkTreeView *treeview;

	treeview = GTK_TREE_VIEW (gtk_builder_get_object (priv->builder, "treeview_orders"));
	model = gtk_tree_view_get_model (treeview);
	ret = gtk_tree_model_get_iter_first (model, &iter);
	while (ret) {
		gtk_tree_model_get (model, &iter,
				    COLUMN_CHECKBOX, &checkbox,
				    -1);
		if (checkbox)
			ch_shipping_print_cn22 (priv, model, &iter);
		ret = gtk_tree_model_iter_next (model, &iter);
	}
}

/**
 * ch_shipping_mark_shipped_button_cb:
 **/
static void
ch_shipping_mark_shipped_button_cb (GtkWidget *widget, ChFactoryPrivate *priv)
{
	gboolean checkbox;
	gboolean ret;
	GtkTreeIter iter;
	GtkTreeModel *model;
	GtkTreeView *treeview;

	treeview = GTK_TREE_VIEW (gtk_builder_get_object (priv->builder, "treeview_orders"));
	model = gtk_tree_view_get_model (treeview);
	ret = gtk_tree_model_get_iter_first (model, &iter);
	while (ret) {
		gtk_tree_model_get (model, &iter,
				    COLUMN_CHECKBOX, &checkbox,
				    -1);
		if (checkbox)
			ch_shipping_email_send_email (priv, model, &iter);
		ret = gtk_tree_model_iter_next (model, &iter);
	}

	/* refresh state */
	ch_shipping_refresh_orders (priv);
}

/**
 * ch_shipping_print_manifest:
 **/
static void
ch_shipping_print_manifest (ChFactoryPrivate *priv, GString *str, GtkTreeModel *model, GtkTreeIter *iter, guint cnt)
{
	ChShippingKind postage;
	gchar *address = NULL;
	gchar **address_split = NULL;
	gchar *device_ids = NULL;
	gchar *name = NULL;
	gchar *tmp;
	gchar *tracking;
	guint32 order_id;
	guint device_price;
	guint i;

	gtk_tree_model_get (model, iter,
			    COLUMN_ORDER_ID, &order_id,
			    COLUMN_DEVICE_IDS, &device_ids,
			    COLUMN_POSTAGE, &postage,
			    COLUMN_NAME, &name,
			    COLUMN_ADDRESS, &address,
			    COLUMN_TRACKING, &tracking,
			    -1);

	/* replace escaped chars */
//	address = ch_shipping_strreplace (address, "&", "\\&");

	address_split = g_strsplit (address, "|", -1);
	device_price = ch_shipping_device_to_price (postage);

	i = g_strv_length (address_split);
	if (g_strcmp0 (address_split[i-1], " ") == 0)
		i--;
	if (g_strcmp0 (address_split[i-1], " ") == 0)
		i--;
	if (g_strcmp0 (address_split[i-1], " ") == 0)
		i--;

	tmp = g_strdup_printf ("$SERVICE%02i$", cnt);
	ch_shipping_string_replace (str, tmp, ch_shipping_kind_to_service (postage));
	g_free (tmp);
	tmp = g_strdup_printf ("$POSTCODE%02i$", cnt);
	ch_shipping_string_replace (str, tmp, g_strdup_printf ("%s, %s", address_split[i-2], address_split[i-1]));
	g_free (tmp);
	tmp = g_strdup_printf ("$BUILDINGNAME%02i$", cnt);
	ch_shipping_string_replace (str, tmp, address_split[0]);
	g_free (tmp);
	tmp = g_strdup_printf ("$VALUE%02i$", cnt);
	ch_shipping_string_replace (str, tmp, g_strdup_printf ("£%i", device_price));
	g_free (tmp);
	tmp = g_strdup_printf ("$BARCODE%02i$", cnt);
	ch_shipping_string_replace (str, tmp, tracking[0] != '\0' ? tracking : "n/a");
	g_free (tmp);

	g_free (name);
	g_free (tracking);
	g_free (address);
	g_free (device_ids);
	g_strfreev (address_split);
}

/**
 * ch_shipping_print_manifest_button_cb:
 **/
static void
ch_shipping_print_manifest_button_cb (GtkWidget *widget, ChFactoryPrivate *priv)
{
	/* find any orders in the to-be-printed state */
	gboolean ret;
	GtkTreeIter iter;
	gboolean checkbox;
	GtkTreeModel *model;
	GtkTreeView *treeview;
	GString *str;
	guint cnt = 0;
	guint i;
	GError *error = NULL;
	GDateTime *date;
	gchar *tmp;

	/* load svg data */
	str = ch_shipping_string_load (CH_DATA "/template.svg", &error);
	if (str == NULL) {
		ch_shipping_error_dialog (priv, "failed to open file: %s", error->message);
		g_error_free (error);
		goto out;
	}
	str->allocated_len = str->len + 1;

	/* do replacements */
	date = g_date_time_new_now_local ();
	tmp = g_strdup_printf ("%02i/%02i/%04i",
				g_date_time_get_day_of_month (date),
				g_date_time_get_month (date),
				g_date_time_get_year (date));
	ch_shipping_string_replace (str, "$DATE$", tmp);
	g_free (tmp);

	treeview = GTK_TREE_VIEW (gtk_builder_get_object (priv->builder, "treeview_orders"));
	model = gtk_tree_view_get_model (treeview);
	ret = gtk_tree_model_get_iter_first (model, &iter);
	while (ret) {
		gtk_tree_model_get (model, &iter,
				    COLUMN_CHECKBOX, &checkbox,
				    -1);
		if (checkbox) {
			ch_shipping_print_manifest (priv, str, model, &iter, cnt + 1);
			cnt++;
		}
		ret = gtk_tree_model_iter_next (model, &iter);
	}

	/* blank out the others */
	for (i = cnt + 1; i < 16; i++) {
		tmp = g_strdup_printf ("$SERVICE%02i$", i);
		ch_shipping_string_replace (str, tmp, "");
		g_free (tmp);
		tmp = g_strdup_printf ("$POSTCODE%02i$", i);
		ch_shipping_string_replace (str, tmp, "");
		g_free (tmp);
		tmp = g_strdup_printf ("$BUILDINGNAME%02i$", i);
		ch_shipping_string_replace (str, tmp, "");
		g_free (tmp);
		tmp = g_strdup_printf ("$VALUE%02i$", i);
		ch_shipping_string_replace (str, tmp, "");
		g_free (tmp);
		tmp = g_strdup_printf ("$BARCODE%02i$", i);
		ch_shipping_string_replace (str, tmp, "");
		g_free (tmp);
	}

	/* print pdf of svg */
	ret = ch_shipping_print_svg_doc (str->str, NULL, &error);
	if (!ret) {
		ch_shipping_error_dialog (priv, "failed to print file: %s", error->message);
		g_error_free (error);
		goto out;
	}
out:
	g_string_free (str, TRUE);
}



/**
 * ch_shipping_print_label:
 **/
static void
ch_shipping_print_label (ChFactoryPrivate *priv, GtkTreeModel *model, GtkTreeIter *iter)
{
	ChShippingKind postage;
	gboolean ret;
	gchar *address = NULL;
	gchar *comment = NULL;
	gchar **address_split = NULL;
	gchar *device_ids = NULL;
	GString *address_escaped = NULL;
	gchar *name = NULL;
	GError *error = NULL;
	guint32 order_id;
	GString *str = NULL;

	/* get selected item */
	gtk_tree_model_get (model, iter,
			    COLUMN_ORDER_ID, &order_id,
			    COLUMN_NAME, &name,
			    COLUMN_ADDRESS, &address,
			    COLUMN_POSTAGE, &postage,
			    COLUMN_COMMENT, &comment,
			    COLUMN_DEVICE_IDS, &device_ids,
			    -1);

	/* replace escaped chars */
	address = ch_shipping_strreplace (address, "$", "\\$");
	address = ch_shipping_strreplace (address, "%", "\\%");
	address = ch_shipping_strreplace (address, "_", "\\_");
	address = ch_shipping_strreplace (address, "}", "\\}");
	address = ch_shipping_strreplace (address, "{", "\\{");
	address = ch_shipping_strreplace (address, "&", "\\&");
	address = ch_shipping_strreplace (address, "#", "\\#");

	address_split = g_strsplit (address, "|", -1);

	/* update order status */
	ret = ch_database_order_set_state (priv->database,
					   order_id,
					   CH_ORDER_STATE_TO_BE_PRINTED,
					   &error);
	if (!ret) {
		ch_shipping_error_dialog (priv, "Failed to update order state",
					  error->message);
		g_error_free (error);
		goto out;
	}

	str = ch_shipping_string_load (CH_DATA "/shipping-label.tex", &error);
	if (str == NULL) {
		ch_shipping_error_dialog (priv, "Failed to lad shipping label",
					  error->message);
		g_error_free (error);
		goto out;
	}

	ch_shipping_string_replace (str, "$LETTER_CLASS$", "SMALL PACKAGE");
	ch_shipping_string_replace (str, "$NAME$", name);
	ch_shipping_string_replace (str, "$ADDRESS1$", address_split[0]);
	ch_shipping_string_replace (str, "$ADDRESS2$", address_split[1]);
	ch_shipping_string_replace (str, "$ADDRESS3$", address_split[2]);
	ch_shipping_string_replace (str, "$ADDRESS4$", address_split[3]);
	ch_shipping_string_replace (str, "$ADDRESS5$", address_split[4]);
	ch_shipping_string_replace (str, "$ORDER$", g_strdup_printf ("%04i", order_id));
	ch_shipping_string_replace (str, "$DEVICES$", device_ids);
	ch_shipping_string_replace (str, "$SHIPPING$", ch_shipping_kind_to_string (postage));

	ret = ch_shipping_print_latex_doc (str->str, "LP2844", &error);
	if (!ret) {
		ch_shipping_error_dialog (priv, "Failed to print label",
					  error->message);
		g_error_free (error);
		goto out;
	}
out:
	if (str != NULL)
		g_string_free (str, TRUE);
	if (address_escaped != NULL)
		g_string_free (address_escaped, TRUE);
	g_strfreev (address_split);
	g_free (device_ids);
	g_free (comment);
	g_free (address);
	g_free (name);
}

/**
 * ch_shipping_print_labels_button_cb:
 **/
static void
ch_shipping_print_labels_button_cb (GtkWidget *widget, ChFactoryPrivate *priv)
{
	gboolean checkbox;
	gboolean ret;
	GtkTreeIter iter;
	GtkTreeModel *model;
	GtkTreeView *treeview;

	treeview = GTK_TREE_VIEW (gtk_builder_get_object (priv->builder, "treeview_orders"));
	model = gtk_tree_view_get_model (treeview);
	ret = gtk_tree_model_get_iter_first (model, &iter);
	while (ret) {
		gtk_tree_model_get (model, &iter,
				    COLUMN_CHECKBOX, &checkbox,
				    -1);
		if (checkbox)
			ch_shipping_print_label (priv, model, &iter);
		ret = gtk_tree_model_iter_next (model, &iter);
	}
	ch_shipping_refresh_orders (priv);

	/* refresh status */
	ch_shipping_refresh_status (priv);
}

/**
 * ch_shipping_order_button_cb:
 **/
static void
ch_shipping_order_button_cb (GtkWidget *widget, ChFactoryPrivate *priv)
{
	GDateTime *date;

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_order_add"));
	gtk_widget_set_sensitive (widget, FALSE);

	/* set to defaults */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_name"));
	gtk_entry_set_text (GTK_ENTRY (widget), "");
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_email"));
	gtk_entry_set_text (GTK_ENTRY (widget), "");
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_addr1"));
	gtk_entry_set_text (GTK_ENTRY (widget), "");
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_addr2"));
	gtk_entry_set_text (GTK_ENTRY (widget), "");
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_addr3"));
	gtk_entry_set_text (GTK_ENTRY (widget), "");
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_addr4"));
	gtk_entry_set_text (GTK_ENTRY (widget), "");
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_addr5"));
	gtk_entry_set_text (GTK_ENTRY (widget), "");
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_tracking"));
	gtk_entry_set_text (GTK_ENTRY (widget), "");
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "spinbutton_order_devices"));
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (widget), 1.0f);

	date = g_date_time_new_now_local ();
	switch (g_date_time_get_day_of_week (date)) {
	case G_DATE_MONDAY:
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "radiobutton_sending_tuesday"));
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), TRUE);
		break;
	case G_DATE_TUESDAY:
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "radiobutton_sending_wednesday"));
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), TRUE);
		break;
	case G_DATE_WEDNESDAY:
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "radiobutton_sending_thursday"));
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), TRUE);
		break;
	case G_DATE_THURSDAY:
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "radiobutton_sending_friday"));
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), TRUE);
		break;
	default:
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "radiobutton_sending_monday"));
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), TRUE);
		break;
	}

	/* show the modal window */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "dialog_order"));
	gtk_widget_show (widget);
	g_date_time_unref (date);
}

/**
 * ch_shipping_order_cancel_button_cb:
 **/
static void
ch_shipping_order_cancel_button_cb (GtkWidget *widget, ChFactoryPrivate *priv)
{
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "dialog_order"));
	gtk_widget_hide (widget);
}

/**
 * ch_shipping_comment_button_cb:
 **/
static void
ch_shipping_comment_button_cb (GtkWidget *widget, ChFactoryPrivate *priv)
{
	gboolean ret;
	gchar *comment = NULL;
	GError *error = NULL;
	GtkTreeIter iter;
	GtkTreeModel *model;
	GtkTreeSelection *selection;
	GtkTreeView *treeview;
	guint32 order_id;

	/* get the order id of the selected item */
	treeview = GTK_TREE_VIEW (gtk_builder_get_object (priv->builder, "treeview_orders"));
	selection = gtk_tree_view_get_selection (treeview);
	ret = gtk_tree_selection_get_selected (selection, &model, &iter);
	if (!ret)
		goto out;

	/* set the text box to have the existing comment */
	gtk_tree_model_get (model, &iter,
			    COLUMN_ORDER_ID, &order_id,
			    -1);
	comment = ch_database_order_get_comment (priv->database, order_id, &error);
	if (comment == NULL) {
		ch_shipping_error_dialog (priv, "Failed to get comment form order", error->message);
		g_error_free (error);
		goto out;
	}
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_comment_text"));
	gtk_entry_set_text (GTK_ENTRY (widget), comment);

	/* show UI */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "dialog_comment"));
	gtk_widget_show (widget);
out:
	g_free (comment);
}

/**
 * ch_shipping_comment_cancel_button_cb:
 **/
static void
ch_shipping_comment_cancel_button_cb (GtkWidget *widget, ChFactoryPrivate *priv)
{
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "dialog_comment"));
	gtk_widget_hide (widget);
}

/**
 * ch_shipping_refresh_cb:
 **/
static void
ch_shipping_refresh_cb (GtkWidget *widget, ChFactoryPrivate *priv)
{
	ch_shipping_refresh_orders (priv);
}

/**
 * ch_shipping_order_get_radio_postage:
 **/
static ChShippingKind
ch_shipping_order_get_radio_postage (ChFactoryPrivate *priv)
{
	GtkWidget *widget;
	ChShippingKind postage = CH_SHIPPING_KIND_LAST;
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "radiobutton_shipping4"));
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget)))
		postage = CH_SHIPPING_KIND_CH2_UK_SIGNED;
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "radiobutton_shipping5"));
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget)))
		postage = CH_SHIPPING_KIND_CH2_EUROPE_SIGNED;
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "radiobutton_shipping6"));
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget)))
		postage = CH_SHIPPING_KIND_CH2_WORLD_SIGNED;
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "radiobutton_shipping7"));
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget)))
		postage = CH_SHIPPING_KIND_CH1_UK;
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "radiobutton_shipping8"));
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget)))
		postage = CH_SHIPPING_KIND_CH1_EUROPE;
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "radiobutton_shipping9"));
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget)))
		postage = CH_SHIPPING_KIND_CH1_WORLD;
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "radiobutton_shipping10"));
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget)))
		postage = CH_SHIPPING_KIND_CH1_UK_SIGNED;
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "radiobutton_shipping11"));
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget)))
		postage = CH_SHIPPING_KIND_CH1_EUROPE_SIGNED;
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "radiobutton_shipping12"));
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget)))
		postage = CH_SHIPPING_KIND_CH1_WORLD_SIGNED;
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "radiobutton_shipping13"));
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget)))
		postage = CH_SHIPPING_KIND_STRAP_UK;
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "radiobutton_shipping14"));
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget)))
		postage = CH_SHIPPING_KIND_STRAP_EUROPE;
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "radiobutton_shipping15"));
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget)))
		postage = CH_SHIPPING_KIND_STRAP_WORLD;
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "radiobutton_shipping16"));
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget)))
		postage = CH_SHIPPING_KIND_ALS_UK;
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "radiobutton_shipping17"));
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget)))
		postage = CH_SHIPPING_KIND_ALS_EUROPE;
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "radiobutton_shipping18"));
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget)))
		postage = CH_SHIPPING_KIND_ALS_WORLD;
	return postage;
}

/**
 * ch_shipping_order_add_button_cb:
 **/
static void
ch_shipping_order_add_button_cb (GtkWidget *widget, ChFactoryPrivate *priv)
{
	ChShippingKind postage = CH_SHIPPING_KIND_LAST;
	const gchar *email = NULL;
	const gchar *name = NULL;
	const gchar *sending_day = NULL;
	const gchar *tracking = NULL;
	gboolean ret;
	gchar *from = NULL;
	GDateTime *date = NULL;
	GError *error = NULL;
	GString *addr = g_string_new ("");
	GString *str = NULL;
	guint32 device_id;
	guint32 hw_ver;
	guint32 order_id;
	guint i;
	guint number_of_devices = 0;

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_paypal"));
	name = gtk_entry_get_text (GTK_ENTRY (widget));
	if (name != NULL && name[0] != '\0') {
		/* import data */
		goto skip;
	}

	/* get name */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_name"));
	name = gtk_entry_get_text (GTK_ENTRY (widget));

	/* get email */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_email"));
	email = gtk_entry_get_text (GTK_ENTRY (widget));

	/* add address */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_addr1"));
	g_string_append_printf (addr, "%s|", gtk_entry_get_text (GTK_ENTRY (widget)));
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_addr2"));
	g_string_append_printf (addr, "%s|", gtk_entry_get_text (GTK_ENTRY (widget)));
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_addr3"));
	g_string_append_printf (addr, "%s|", gtk_entry_get_text (GTK_ENTRY (widget)));
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_addr4"));
	g_string_append_printf (addr, "%s|", gtk_entry_get_text (GTK_ENTRY (widget)));
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_addr5"));
	g_string_append (addr, gtk_entry_get_text (GTK_ENTRY (widget)));

	/* get postage */
	postage = ch_shipping_order_get_radio_postage (priv);
	switch (postage) {
	case CH_SHIPPING_KIND_CH2_UK_SIGNED:
	case CH_SHIPPING_KIND_CH2_EUROPE_SIGNED:
	case CH_SHIPPING_KIND_CH2_WORLD_SIGNED:
		hw_ver = 2;
		break;
	case CH_SHIPPING_KIND_CH1_UK:
	case CH_SHIPPING_KIND_CH1_EUROPE:
	case CH_SHIPPING_KIND_CH1_WORLD:
	case CH_SHIPPING_KIND_CH1_UK_SIGNED:
	case CH_SHIPPING_KIND_CH1_EUROPE_SIGNED:
	case CH_SHIPPING_KIND_CH1_WORLD_SIGNED:
		hw_ver = 1;
		break;
	default:
		hw_ver = 0;
		break;
	}

	/* get number of devices */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "spinbutton_order_devices"));
	if (postage != CH_SHIPPING_KIND_STRAP_UK &&
	    postage != CH_SHIPPING_KIND_STRAP_EUROPE &&
	    postage != CH_SHIPPING_KIND_STRAP_WORLD &&
	    postage != CH_SHIPPING_KIND_ALS_UK &&
	    postage != CH_SHIPPING_KIND_ALS_EUROPE &&
	    postage != CH_SHIPPING_KIND_ALS_WORLD) {
		number_of_devices = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (widget));
	}
skip:
	/* add to database */
	order_id = ch_database_add_order (priv->database, name, addr->str, email, postage, &error);
	if (order_id == G_MAXUINT32) {
		ch_shipping_error_dialog (priv, "Failed to add order", error->message);
		g_error_free (error);
		goto out;
	}

	/* write message body */
	str = g_string_new ("");
	if (number_of_devices == 0) {
		g_string_append_printf (str, "ColorHug order %04i has been created ", order_id);
	} else if (number_of_devices == 1) {
		g_string_append_printf (str, "ColorHug order %04i has been created and allocated device number ",
					order_id);
	} else {
		g_string_append_printf (str, "ColorHug order %04i has been created and allocated device numbers ",
					order_id);
	}

	/* get the oldest devices we've got calibrated */
	for (i = 0; i < number_of_devices; i++) {
		device_id = ch_database_device_find_oldest (priv->database,
							    CH_DEVICE_STATE_CALIBRATED,
							    hw_ver,
							    &error);
		if (device_id == G_MAXUINT32) {
			ch_shipping_error_dialog (priv,
						  "Failed to get device",
						  error->message);
			g_error_free (error);
			goto out;
		}

		/* add this device to the order */
		ret = ch_database_device_set_order_id (priv->database,
						       device_id,
						       order_id,
						       &error);
		if (!ret) {
			ch_shipping_error_dialog (priv,
						  "Failed to assign device to order",
						  error->message);
			g_error_free (error);
			goto out;
		}

		/* mark the device as allocated */
		ret = ch_database_device_set_state (priv->database,
						    device_id,
						    CH_DEVICE_STATE_ALLOCATED,
						    &error);
		if (!ret) {
			ch_shipping_error_dialog (priv,
						  "Failed to allocate device",
						  error->message);
			g_error_free (error);
			goto out;
		}

		/* add the device ID */
		g_string_append_printf (str, "%05i ",
					device_id);
	}
	if (str->len > 0) {
		g_string_set_size (str, str->len - 1);
		g_string_append (str, ".\n");
	}

	/* get the day */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "radiobutton_sending_monday"));
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget)))
		sending_day = "Monday";
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "radiobutton_sending_tuesday"));
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget)))
		sending_day = "Tuesday";
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "radiobutton_sending_wednesday"));
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget)))
		sending_day = "Wednesday";
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "radiobutton_sending_thursday"));
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget)))
		sending_day = "Thursday";
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "radiobutton_sending_friday"));
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget)))
		sending_day = "Friday";
	if (sending_day != NULL) {
		g_string_append_printf (str, "The invoice will be printed and the package will be sent on %s.\n",
					sending_day);
	} else {
		g_string_append (str, "The invoice will be printed and the package will be sent when the payment has completed.\n");
	}

	/* print when we will send the item */
	if (postage == CH_SHIPPING_KIND_CH2_UK_SIGNED ||
	    postage == CH_SHIPPING_KIND_CH2_EUROPE_SIGNED ||
	    postage == CH_SHIPPING_KIND_CH2_WORLD_SIGNED ||
	    postage == CH_SHIPPING_KIND_CH1_UK_SIGNED ||
	    postage == CH_SHIPPING_KIND_CH1_EUROPE_SIGNED ||
	    postage == CH_SHIPPING_KIND_CH1_WORLD_SIGNED) {
		if (number_of_devices == 1) {
			g_string_append (str, "Once the device has been posted we will email again with the tracking number.\n");
		} else {
			g_string_append (str, "Once the devices have been posted we will email again with the tracking number.\n");
		}
	} else if (postage == CH_SHIPPING_KIND_STRAP_UK ||
		   postage == CH_SHIPPING_KIND_STRAP_EUROPE ||
		   postage == CH_SHIPPING_KIND_STRAP_WORLD) {
		g_string_append (str, "Once the strap and gasket upgrade has been posted a confirmation email will be sent.\n");
	} else {
		g_string_append (str, "Once the parcel has been posted a confirmation email will be sent.\n");
	}
	g_string_append (str, "\n");
	g_string_append (str, "Thanks again for your support for this exciting project.\n");
	g_string_append (str, "\n");
	g_string_append (str, "Ania Hughes\n");

	/* get tracking number */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_tracking"));
	tracking = gtk_entry_get_text (GTK_ENTRY (widget));

	/* save to the database */
	ret = ch_database_order_set_tracking (priv->database,
					      order_id,
					      tracking,
					      &error);
	if (!ret) {
		ch_shipping_error_dialog (priv, "Failed to add tracking to order", error->message);
		g_error_free (error);
		goto out;
	}

	/* actually send the email */
	from = g_settings_get_string (priv->settings, "invoice-sender");
	ret = ch_shipping_send_email (from,
				      email,
				      "Your ColorHug order has been received",
				      str->str,
				      g_settings_get_string (priv->settings, "invoice-auth"),
				      &error);
	if (!ret) {
		ch_shipping_error_dialog (priv, "Failed to send email", error->message);
		g_error_free (error);
		goto out;
	}

	/* refresh state */
	priv->order_to_print = order_id;
	ch_shipping_refresh_orders (priv);
out:
	if (date != NULL)
		g_date_time_unref (date);
	if (str != NULL)
		g_string_free (str, TRUE);
	g_free (from);

	/* buttons */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "dialog_order"));
	gtk_widget_hide (widget);
}

/**
 * ch_shipping_email_send_email:
 **/
static void
ch_shipping_email_send_email (ChFactoryPrivate *priv, GtkTreeModel *model, GtkTreeIter *iter)
{
	ChShippingKind postage;
	const gchar *device_name = NULL;
	const gchar *tracking_number = NULL;
	gboolean ret;
	gchar *cmd = NULL;
	gchar *device_ids = NULL;
	gchar *email = NULL;
	gchar *from = NULL;
	GError *error = NULL;
	GString *str = NULL;
	guint32 order_id;

	switch (postage) {
	case CH_SHIPPING_KIND_CH2_UK_SIGNED:
	case CH_SHIPPING_KIND_CH2_EUROPE_SIGNED:
	case CH_SHIPPING_KIND_CH2_WORLD_SIGNED:
		device_name = "ColorHug2";
		break;
	default:
		device_name = "ColorHug";
		break;
	}

	/* get the order id of the selected item */
	gtk_tree_model_get (model, iter,
			    COLUMN_ORDER_ID, &order_id,
			    COLUMN_POSTAGE, &postage,
			    COLUMN_EMAIL, &email,
			    COLUMN_DEVICE_IDS, &device_ids,
			    COLUMN_TRACKING, &tracking_number,
			    -1);

	/* write email */
	str = g_string_new ("");
	from = g_settings_get_string (priv->settings, "invoice-sender");
	if (device_ids[0] == '-') {
		g_string_append (str, "I'm pleased to tell your accessory has been dispatched.\n");
	} else if (tracking_number[0] == '\0' || g_strcmp0 (tracking_number, "n/a") == 0) {
		g_string_append_printf (str, "I'm pleased to tell you %s #%s has been dispatched.\n", device_name, device_ids);
	} else {
		g_string_append_printf (str, "I'm pleased to tell you %s #%s has been dispatched with tracking ID: %s\n", device_name, device_ids, tracking_number);
		g_string_append (str, "You will be able to track this item here: http://track2.royalmail.com/portal/rm/trackresults\n");
	}
	g_string_append (str, "\n");
	if (postage == CH_SHIPPING_KIND_STRAP_UK ||
	    postage == CH_SHIPPING_KIND_ALS_UK ||
//	    postage == CH_SHIPPING_KIND_CH2_UK ||
	    postage == CH_SHIPPING_KIND_CH2_UK_SIGNED ||
	    postage == CH_SHIPPING_KIND_CH1_UK ||
	    postage == CH_SHIPPING_KIND_CH1_UK_SIGNED) {
		g_string_append (str, "Please allow up to two weeks for delivery, although most items are delivered within 4 days.\n");
	} else if (postage == CH_SHIPPING_KIND_CH2_EUROPE_SIGNED ||
//		   postage == CH_SHIPPING_KIND_CH2_EUROPE ||
		   postage == CH_SHIPPING_KIND_STRAP_EUROPE ||
		   postage == CH_SHIPPING_KIND_ALS_EUROPE ||
		   postage == CH_SHIPPING_KIND_CH1_EUROPE ||
		   postage == CH_SHIPPING_KIND_CH1_EUROPE_SIGNED) {
		g_string_append (str, "Please allow up to two weeks for delivery, although most items are delivered within 8 days.\n");
	} else if (postage == CH_SHIPPING_KIND_STRAP_WORLD ||
		   postage == CH_SHIPPING_KIND_ALS_WORLD ||
//		   postage == CH_SHIPPING_KIND_CH2_WORLD ||
		   postage == CH_SHIPPING_KIND_CH2_WORLD_SIGNED ||
		   postage == CH_SHIPPING_KIND_CH1_WORLD ||
		   postage == CH_SHIPPING_KIND_CH1_WORLD_SIGNED) {
		g_string_append (str, "Please allow up to three weeks for delivery, although most items are delivered within 10 days.\n");
	}
	g_string_append (str, "\n");
	g_string_append (str, "Thanks again for your support for this project.\n");
	g_string_append (str, "\n");
	g_string_append (str, "Ania Hughes\n");

	/* actually send the email */
	ret = ch_shipping_send_email (from,
				      email,
				      "Your order has been dispatched!",
				      str->str,
				      g_settings_get_string (priv->settings, "invoice-auth"),
				      &error);
	if (!ret) {
		ch_shipping_error_dialog (priv, "Failed to send email", error->message);
		g_error_free (error);
		goto out;
	}

	/* save to the database */
	ret = ch_database_order_set_tracking (priv->database,
					      order_id,
					      tracking_number,
					      &error);
	if (!ret) {
		ch_shipping_error_dialog (priv, "Failed to mark order as shipped", error->message);
		g_error_free (error);
		goto out;
	}

	/* update order status */
	ret = ch_database_order_set_state (priv->database,
					   order_id,
					   CH_ORDER_STATE_SENT,
					   &error);
	if (!ret) {
		ch_shipping_error_dialog (priv, "Failed to update order state",
					  error->message);
		g_error_free (error);
		goto out;
	}
out:
	g_free (device_ids);
	g_free (from);
	g_free (email);
	g_free (cmd);
	if (str != NULL)
		g_string_free (str, TRUE);
}

/**
 * ch_shipping_comment_edit_button_cb:
 **/
static void
ch_shipping_comment_edit_button_cb (GtkWidget *widget, ChFactoryPrivate *priv)
{
	const gchar *comment = NULL;
	gboolean ret;
	GError *error = NULL;
	GtkTreeIter iter;
	GtkTreeModel *model;
	GtkTreeSelection *selection;
	GtkTreeView *treeview;
	guint32 order_id;

	/* get the order id of the selected item */
	treeview = GTK_TREE_VIEW (gtk_builder_get_object (priv->builder, "treeview_orders"));
	selection = gtk_tree_view_get_selection (treeview);
	ret = gtk_tree_selection_get_selected (selection, &model, &iter);
	if (!ret)
		goto out;
	gtk_tree_model_get (model, &iter,
			    COLUMN_ORDER_ID, &order_id,
			    -1);

	/* get tracking number */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_comment_text"));
	comment = gtk_entry_get_text (GTK_ENTRY (widget));

	/* save to the database */
	ret = ch_database_order_set_comment (priv->database,
					     order_id,
					     comment,
					     &error);
	if (!ret) {
		ch_shipping_error_dialog (priv, "Failed to add comment to order", error->message);
		g_error_free (error);
		goto out;
	}

	/* refresh state */
	ch_shipping_refresh_orders (priv);
out:
	/* buttons */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "dialog_comment"));
	gtk_widget_hide (widget);
}

/**
 * ch_shipping_tracking_button_cb:
 **/
static void
ch_shipping_tracking_button_cb (GtkWidget *widget, ChFactoryPrivate *priv)
{
	gchar *tracking_number = NULL;
	GError *error = NULL;

	/* get from the database */
	tracking_number = ch_database_get_next_tracking_number (priv->database,
								&error);
	if (tracking_number == NULL) {
		ch_shipping_error_dialog (priv, "Failed to get next tracking number", error->message);
		g_error_free (error);
		goto out;
	}

	/* get tracking number */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_tracking"));
	gtk_entry_set_text (GTK_ENTRY (widget), tracking_number);
out:
	g_free (tracking_number);
}

/**
 * ch_shipping_row_activated_cb:
 **/
static void
ch_shipping_row_activated_cb (GtkTreeView *treeview,
			      GtkTreePath *path,
			      GtkTreeViewColumn *col,
			      ChFactoryPrivate *priv)
{
	gboolean ret;
	GtkTreeIter iter;
	GtkTreeModel *model;
	guint32 id = 0;
	ChOrderState state;

	/* get selection */
	model = gtk_tree_view_get_model (treeview);
	ret = gtk_tree_model_get_iter (model, &iter, path);
	if (!ret) {
		g_warning ("failed to get selection");
		goto out;
	}

	/* get data */
	gtk_tree_model_get (model, &iter,
			    COLUMN_ORDER_ID, &id,
			    COLUMN_ORDER_STATE, &state,
			    -1);
	if (id == G_MAXUINT32)
		goto out;
	g_debug ("activated: %i", id);

	/* do something smart */
	if (state == CH_ORDER_STATE_NEW) {
		//ch_shipping_queue_button_cb (NULL, priv);
	} else {
		ch_shipping_comment_button_cb (NULL, priv);
	}
out:
	return;
}

/**
 * gpk_application_packages_installed_clicked_cb:
 **/
static void
gpk_application_packages_installed_clicked_cb (GtkCellRendererToggle *cell, gchar *path_str, ChFactoryPrivate *priv)
{
	GtkTreeView *treeview;
	GtkTreeModel *model;
	GtkTreeIter iter;
	GtkTreePath *path;
	gboolean checkbox;

	treeview = GTK_TREE_VIEW (gtk_builder_get_object (priv->builder, "treeview_orders"));
	model = gtk_tree_view_get_model (treeview);
	path = gtk_tree_path_new_from_string (path_str);

	/* get toggled iter */
	gtk_tree_model_get_iter (model, &iter, path);
	gtk_tree_model_get (model, &iter,
			    COLUMN_CHECKBOX, &checkbox,
			    -1);
	gtk_list_store_set (GTK_LIST_STORE (model), &iter,
			    COLUMN_CHECKBOX, !checkbox,
			    -1);
	gtk_tree_path_free (path);
}

/**
 * ch_shipping_treeview_add_columns:
 **/
static void
ch_shipping_treeview_add_columns (ChFactoryPrivate *priv)
{
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;
	GtkTreeView *treeview;

	treeview = GTK_TREE_VIEW (gtk_builder_get_object (priv->builder, "treeview_orders"));
	g_signal_connect (treeview, "row-activated",
			  G_CALLBACK (ch_shipping_row_activated_cb), priv);


	/* column for installed status */
	renderer = gtk_cell_renderer_toggle_new ();
	g_signal_connect (renderer, "toggled", G_CALLBACK (gpk_application_packages_installed_clicked_cb), priv);
	column = gtk_tree_view_column_new_with_attributes (NULL, renderer,
							   "active", COLUMN_CHECKBOX,
							   NULL);
	gtk_tree_view_append_column (treeview, column);

	/* column for order_id */
	column = gtk_tree_view_column_new ();
	renderer = ch_cell_renderer_uint32_new ();
	gtk_tree_view_column_pack_start (column, renderer, FALSE);
	gtk_tree_view_column_add_attribute (column, renderer, "value", COLUMN_ORDER_ID);
	gtk_tree_view_column_set_title (column, "Order");
	gtk_tree_view_append_column (treeview, column);
	gtk_tree_view_column_set_sort_column_id (column, COLUMN_ORDER_ID);

	/* column for images */
	column = gtk_tree_view_column_new ();
	renderer = ch_cell_renderer_order_status_new ();
	g_object_set (renderer, "stock-size", GTK_ICON_SIZE_BUTTON, NULL);
	gtk_tree_view_column_pack_start (column, renderer, FALSE);
	gtk_tree_view_column_add_attribute (column, renderer, "value", COLUMN_ORDER_STATE);
	gtk_tree_view_column_set_title (column, "State");
	gtk_tree_view_append_column (treeview, column);

	/* column for name */
	column = gtk_tree_view_column_new ();
	renderer = gtk_cell_renderer_text_new ();
	gtk_tree_view_column_pack_start (column, renderer, FALSE);
	gtk_tree_view_column_add_attribute (column, renderer, "markup", COLUMN_NAME);
	gtk_tree_view_column_set_title (column, "Name");
	gtk_tree_view_append_column (treeview, column);

	/* column for tracking */
	column = gtk_tree_view_column_new ();
	renderer = gtk_cell_renderer_text_new ();
	gtk_tree_view_column_pack_start (column, renderer, FALSE);
	gtk_tree_view_column_add_attribute (column, renderer, "markup", COLUMN_TRACKING);
	gtk_tree_view_column_set_title (column, "Tracking");
	gtk_tree_view_append_column (treeview, column);

	/* column for shipped */
	column = gtk_tree_view_column_new ();
	renderer = ch_cell_renderer_date_new ();
	gtk_tree_view_column_pack_start (column, renderer, FALSE);
	gtk_tree_view_column_add_attribute (column, renderer, "value", COLUMN_SHIPPED);
	gtk_tree_view_column_set_title (column, "Shipped");
	gtk_tree_view_append_column (treeview, column);

	/* column for postage */
	column = gtk_tree_view_column_new ();
	renderer = ch_cell_renderer_postage_new ();
	gtk_tree_view_column_pack_start (column, renderer, FALSE);
	gtk_tree_view_column_add_attribute (column, renderer, "value", COLUMN_POSTAGE);
	gtk_tree_view_column_set_title (column, "Postage");
	gtk_tree_view_append_column (treeview, column);

	/* column for device_id */
	column = gtk_tree_view_column_new ();
	renderer = gtk_cell_renderer_text_new ();
	gtk_tree_view_column_pack_start (column, renderer, FALSE);
	gtk_tree_view_column_add_attribute (column, renderer, "markup", COLUMN_DEVICE_IDS);
	gtk_tree_view_column_set_title (column, "Devices");
	gtk_tree_view_append_column (treeview, column);
	gtk_tree_view_column_set_sort_column_id (column, COLUMN_ORDER_ID);

	/* column for comment */
	column = gtk_tree_view_column_new ();
	renderer = gtk_cell_renderer_text_new ();
	gtk_tree_view_column_pack_start (column, renderer, FALSE);
	gtk_tree_view_column_add_attribute (column, renderer, "markup", COLUMN_COMMENT);
	gtk_tree_view_column_set_title (column, "Comments");
	gtk_tree_view_append_column (treeview, column);
}

/**
 * ch_shipping_paypal_entry_changed_cb:
 **/
static void
ch_shipping_paypal_entry_changed_cb (GtkWidget *widget, GParamSpec *param_spec, ChFactoryPrivate *priv)
{
	const gchar *value;
	gboolean is_address = FALSE;
	gchar **lines = NULL;
	guint cnt = 0;
	guint i;

	/* get import text */
	value = gtk_entry_get_text (GTK_ENTRY (widget));
	if (value[0] == '\0')
		goto out;
	lines = g_strsplit (value, "\n", -1);

	/* parse block of text */
	for (i = 0; lines[i] != NULL; i++) {
		value = lines[i];

		/* get email */
		if (g_strstr_len (value, -1, "@") != NULL &&
		    g_strstr_len (value, -1, "info@hughski.com") == NULL) {
			widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_email"));
			gtk_entry_set_text (GTK_ENTRY (widget), value);
			continue;
		}

		/* get postage */
		if (g_strstr_len (value, -1, "Amount:")) {
			if (g_strstr_len (value, -1, "55")) {
				widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "radiobutton_shipping4"));
				gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), TRUE);
			} else if (g_strstr_len (value, -1, "56")) {
				widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "radiobutton_shipping5"));
				gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), TRUE);
			} else if (g_strstr_len (value, -1, "57")) {
				widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "radiobutton_shipping6"));
				gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), TRUE);
			} else if (g_strstr_len (value, -1, "62")) {
				widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "radiobutton_shipping7"));
				gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), TRUE);
			} else if (g_strstr_len (value, -1, "63")) {
				widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "radiobutton_shipping8"));
				gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), TRUE);
			} else if (g_strstr_len (value, -1, "64")) {
				widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "radiobutton_shipping9"));
				gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), TRUE);
			} else if (g_strstr_len (value, -1, "67")) {
				widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "radiobutton_shipping10"));
				gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), TRUE);
			} else if (g_strstr_len (value, -1, "68")) {
				widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "radiobutton_shipping11"));
				gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), TRUE);
			} else if (g_strstr_len (value, -1, "69")) {
				widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "radiobutton_shipping12"));
				gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), TRUE);
			} else {
				g_warning ("no postage %s", value);
				g_assert_not_reached ();
			}
			continue;
		}

		/* get address */
		if (g_strstr_len (value, -1, "end-to address") != NULL ||
		    g_strstr_len (value, -1, "ostal address") != NULL ||
		    g_strstr_len (value, -1, "hipping address") != NULL) {
			is_address = TRUE;
			continue;
		}
		if (is_address) {
			if (value[0] == '\0') {
				is_address = FALSE;
				continue;
			}
			if (cnt == 0) {
				widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_name"));
				gtk_entry_set_text (GTK_ENTRY (widget), value);
			} else if (cnt == 1) {
				widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_addr1"));
				gtk_entry_set_text (GTK_ENTRY (widget), value);
			} else if (cnt == 2) {
				widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_addr2"));
				gtk_entry_set_text (GTK_ENTRY (widget), value);
			} else if (cnt == 3) {
				widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_addr3"));
				gtk_entry_set_text (GTK_ENTRY (widget), value);
			} else if (cnt == 4) {
				widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_addr4"));
				gtk_entry_set_text (GTK_ENTRY (widget), value);
			} else if (cnt == 5) {
				widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_addr5"));
				gtk_entry_set_text (GTK_ENTRY (widget), value);
			} else {
				g_warning ("address[%i] = %s", cnt, value);
				g_assert_not_reached ();
			}
			cnt++;
			continue;
		}
	}

	/* clear this */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_paypal"));
	gtk_entry_set_text (GTK_ENTRY (widget), "");
out:
	g_strfreev (lines);
}

/**
 * ch_shipping_order_entry_changed_cb:
 **/
static void
ch_shipping_order_entry_changed_cb (GtkWidget *widget, GParamSpec *param_spec, ChFactoryPrivate *priv)
{
	ChShippingKind postage;
	const gchar *value;
	gboolean ret = FALSE;

	/* set to defaults */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_name"));
	value = gtk_entry_get_text (GTK_ENTRY (widget));
	if (value[0] == '\0')
		goto out;
	if (g_strstr_len (value, -1, "\n") != NULL)
		goto out;
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_email"));
	value = gtk_entry_get_text (GTK_ENTRY (widget));
	if (value[0] == '\0')
		goto out;
	if (g_strstr_len (value, -1, "\n") != NULL)
		goto out;
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_addr1"));
	value = gtk_entry_get_text (GTK_ENTRY (widget));
	if (value[0] == '\0')
		goto out;
	if (g_strstr_len (value, -1, "\n") != NULL)
		goto out;
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_addr2"));
	value = gtk_entry_get_text (GTK_ENTRY (widget));
	if (value[0] == '\0')
		goto out;
	if (g_strstr_len (value, -1, "\n") != NULL)
		goto out;
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_addr3"));
	value = gtk_entry_get_text (GTK_ENTRY (widget));
	if (value[0] == '\0')
		goto out;
	if (g_strstr_len (value, -1, "\n") != NULL)
		goto out;
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_addr4"));
	value = gtk_entry_get_text (GTK_ENTRY (widget));
	if (value[0] == '\0')
		goto out;
	if (g_strstr_len (value, -1, "\n") != NULL)
		goto out;
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_addr5"));
	value = gtk_entry_get_text (GTK_ENTRY (widget));
	if (value[0] == '\0')
		goto out;
	if (g_strstr_len (value, -1, "\n") != NULL)
		goto out;

	/* check we have a tracking number */
	postage = ch_shipping_order_get_radio_postage (priv);
	if (postage == CH_SHIPPING_KIND_CH2_UK_SIGNED ||
	    postage == CH_SHIPPING_KIND_CH2_EUROPE_SIGNED ||
	    postage == CH_SHIPPING_KIND_CH2_WORLD_SIGNED ||
	    postage == CH_SHIPPING_KIND_CH1_UK_SIGNED ||
	    postage == CH_SHIPPING_KIND_CH1_EUROPE_SIGNED ||
	    postage == CH_SHIPPING_KIND_CH1_WORLD_SIGNED) {
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_tracking"));
		value = gtk_entry_get_text (GTK_ENTRY (widget));
		if (value[0] == '\0')
			goto out;
	}

	/* woohoo */
	ret = TRUE;
out:
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_order_add"));
	gtk_widget_set_sensitive (widget, ret);
}

/**
 * ch_shipping_radio_shippping_changed_cb:
 **/
static void
ch_shipping_radio_shippping_changed_cb (GtkToggleButton *button, ChFactoryPrivate *priv)
{
	GtkWidget *widget;
	ChShippingKind postage;
	if (!gtk_toggle_button_get_active (button))
		return;
	postage = ch_shipping_order_get_radio_postage (priv);
	if (postage == CH_SHIPPING_KIND_CH1_UK ||
	    postage == CH_SHIPPING_KIND_CH1_EUROPE ||
	    postage == CH_SHIPPING_KIND_CH1_WORLD) {
//	    postage == CH_SHIPPING_KIND_CH2_EUROPE ||
//	    postage == CH_SHIPPING_KIND_CH2_UK ||
//	    postage == CH_SHIPPING_KIND_CH2_WORLD) {
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_tracking"));
		gtk_entry_set_text (GTK_ENTRY (widget), "n/a");
		gtk_widget_set_sensitive (widget, FALSE);
	} else {
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_tracking"));
		gtk_entry_set_text (GTK_ENTRY (widget), "");
		gtk_widget_set_sensitive (widget, TRUE);
	}
	ch_shipping_order_entry_changed_cb (NULL, NULL, priv);
}

/**
 * ch_shipping_startup_cb:
 **/
static void
ch_shipping_startup_cb (GApplication *application, ChFactoryPrivate *priv)
{
	gchar *filename = NULL;
	GError *error = NULL;
	gint retval;
	GtkWidget *main_window;
	GtkWidget *widget;
	GtkStyleContext *context;
	GtkTreeSortable *sortable;

	/* get UI */
	priv->builder = gtk_builder_new ();
	retval = gtk_builder_add_from_file (priv->builder,
					    CH_DATA "/ch-shipping.ui",
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

	main_window = GTK_WIDGET (gtk_builder_get_object (priv->builder, "dialog_shipping"));
	gtk_application_add_window (priv->application, GTK_WINDOW (main_window));
	gtk_widget_set_size_request (main_window, 600, 400);

	/* Hide window first so that the dialogue resizes itself without redrawing */
	gtk_widget_hide (main_window);

	/* setup treeview */
	ch_shipping_treeview_add_columns (priv);

	/* sorted */
	sortable = GTK_TREE_SORTABLE (gtk_builder_get_object (priv->builder, "liststore_orders"));
	gtk_tree_sortable_set_sort_column_id (sortable,
					      COLUMN_ORDER_ID, GTK_SORT_DESCENDING);

	/* buttons */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_close"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (ch_shipping_close_button_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_print_labels"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (ch_shipping_print_labels_button_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_print_invoices"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (ch_shipping_print_invoices_button_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_print_cn22"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (ch_shipping_print_cn22_button_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_mark_shipped"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (ch_shipping_mark_shipped_button_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_print_manifest"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (ch_shipping_print_manifest_button_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "toolbutton_refund"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (ch_shipping_refund_button_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_order"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (ch_shipping_order_button_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "toolbutton_comment"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (ch_shipping_comment_button_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_order_cancel"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (ch_shipping_order_cancel_button_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_order_add"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (ch_shipping_order_add_button_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_comment_cancel"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (ch_shipping_comment_cancel_button_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_comment_edit"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (ch_shipping_comment_edit_button_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_tracking"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (ch_shipping_tracking_button_cb), priv);

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_refresh"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (ch_shipping_refresh_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "radiobutton_shipping4"));
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (ch_shipping_radio_shippping_changed_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "radiobutton_shipping5"));
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (ch_shipping_radio_shippping_changed_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "radiobutton_shipping6"));
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (ch_shipping_radio_shippping_changed_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "radiobutton_shipping7"));
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (ch_shipping_radio_shippping_changed_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "radiobutton_shipping8"));
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (ch_shipping_radio_shippping_changed_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "radiobutton_shipping9"));
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (ch_shipping_radio_shippping_changed_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "radiobutton_shipping10"));
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (ch_shipping_radio_shippping_changed_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "radiobutton_shipping11"));
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (ch_shipping_radio_shippping_changed_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "radiobutton_shipping12"));
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (ch_shipping_radio_shippping_changed_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "radiobutton_shipping13"));
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (ch_shipping_radio_shippping_changed_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "radiobutton_shipping14"));
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (ch_shipping_radio_shippping_changed_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "radiobutton_shipping15"));
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (ch_shipping_radio_shippping_changed_cb), priv);

	/* don't allow to add without details */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_name"));
	g_signal_connect (widget, "notify::text",
			  G_CALLBACK (ch_shipping_order_entry_changed_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_addr1"));
	g_signal_connect (widget, "notify::text",
			  G_CALLBACK (ch_shipping_order_entry_changed_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_addr2"));
	g_signal_connect (widget, "notify::text",
			  G_CALLBACK (ch_shipping_order_entry_changed_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_addr3"));
	g_signal_connect (widget, "notify::text",
			  G_CALLBACK (ch_shipping_order_entry_changed_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_addr4"));
	g_signal_connect (widget, "notify::text",
			  G_CALLBACK (ch_shipping_order_entry_changed_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_addr5"));
	g_signal_connect (widget, "notify::text",
			  G_CALLBACK (ch_shipping_order_entry_changed_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_email"));
	g_signal_connect (widget, "notify::text",
			  G_CALLBACK (ch_shipping_order_entry_changed_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_tracking"));
	g_signal_connect (widget, "notify::text",
			  G_CALLBACK (ch_shipping_order_entry_changed_cb), priv);

	/* parse the paypal email */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_paypal"));
	g_signal_connect (widget, "notify::text",
			  G_CALLBACK (ch_shipping_paypal_entry_changed_cb), priv);

	/* disable buttons based on the order state */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "treeview_orders"));

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

	/* coldplug from database */
	ch_shipping_refresh_orders (priv);

	/* show main UI */
	gtk_widget_show (main_window);
out:
	g_free (filename);
}

/**
 * ch_shipping_ignore_cb:
 **/
static void
ch_shipping_ignore_cb (const gchar *log_domain, GLogLevelFlags log_level,
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

	/* TRANSLATORS: A program to shipping calibrate the hardware */
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
	priv->loop = g_main_loop_new (NULL, FALSE);
	priv->order_to_print = G_MAXUINT32;
	priv->database = ch_database_new ();
	priv->settings = g_settings_new ("com.hughski.colorhug-tools");

	/* set the database location */
	database_uri = g_settings_get_string (priv->settings, "database-uri");
	ch_database_set_uri (priv->database, database_uri);

	/* ensure single instance */
	priv->application = gtk_application_new ("com.hughski.ColorHug.Shipping", 0);
	g_signal_connect (priv->application, "startup",
			  G_CALLBACK (ch_shipping_startup_cb), priv);
	g_signal_connect (priv->application, "activate",
			  G_CALLBACK (ch_shipping_activate_cb), priv);
	/* set verbose? */
	if (verbose) {
		g_setenv ("COLORHUG_VERBOSE", "1", FALSE);
	} else {
		g_log_set_handler (G_LOG_DOMAIN, G_LOG_LEVEL_DEBUG,
				   ch_shipping_ignore_cb, NULL);
	}

	/* wait */
	status = g_application_run (G_APPLICATION (priv->application), argc, argv);

	g_main_loop_unref (priv->loop);
	g_object_unref (priv->application);
	if (priv->builder != NULL)
		g_object_unref (priv->builder);
	if (priv->settings != NULL)
		g_object_unref (priv->settings);
	if (priv->database != NULL)
		g_object_unref (priv->database);
	g_free (database_uri);
	g_free (priv);
	return status;
}
