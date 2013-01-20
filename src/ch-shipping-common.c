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

#include <glib.h>
#include <string.h>

#include "ch-shipping-common.h"

/**
 * ch_shipping_string_load:
 **/
GString *
ch_shipping_string_load (const gchar *filename, GError **error)
{
	gboolean ret;
	gchar *data_tmp;
	gsize len;
	GString *str = NULL;

	/* open the file */
	ret = g_file_get_contents (filename,
				   &data_tmp,
				   &len,
				   error);
	if (!ret)
		goto out;

	/* steal the char data */
	str = g_slice_new (GString);
	str->str = data_tmp,
	str->len = len;
	str->allocated_len = str->len + 1;
	str = g_string_new (data_tmp);
out:
	return str;
}

guint
ch_shipping_string_replace (GString *string, const gchar *search, const gchar *replace)
{
	gchar *tmp;
	guint cnt = 0;
	guint replace_len;
	guint search_len;

	search_len = strlen (search);
	replace_len = strlen (replace);

	do {
		tmp = g_strstr_len (string->str, -1, search);
		if (tmp == NULL)
			goto out;

		/* reallocate the string if required */
		if (search_len > replace_len) {
			g_string_erase (string,
					tmp - string->str,
					search_len - replace_len);
		}
		if (search_len < replace_len) {
			g_string_insert_len (string,
					     tmp - string->str,
					     search,
					     replace_len - search_len);
		}

		/* just memcmp in the new string */
		memcpy (tmp, replace, replace_len);
		cnt++;
	} while (TRUE);
out:
	return cnt;
}

/**
 * ch_shipping_print_latex_doc:
 **/
gboolean
ch_shipping_print_latex_doc (const gchar *str, const gchar *printer, GError **error)
{
	const gchar *argv_latex[] = { "pdflatex", "/tmp/temp.tex", NULL };
	gboolean ret;
	gint exit_status = 0;
	GPtrArray *argv_lpr = NULL;

	/* save */
	ret = g_file_set_contents ("/tmp/temp.tex", str, -1, error);
	if (!ret)
		goto out;

	/* convert to pdf */
	ret = g_spawn_sync ("/tmp", (gchar **) argv_latex, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL, &exit_status, error);
	if (!ret)
		goto out;
	if (exit_status != 0) {
		ret = FALSE;
		g_set_error_literal (error, 1, 0, "Failed to prepare latex document");
		goto out;
	}

	/* send to the printer */
	argv_lpr = g_ptr_array_new_with_free_func (g_free);
	g_ptr_array_add (argv_lpr, g_strdup ("lpr"));
	if (printer != NULL)
		g_ptr_array_add (argv_lpr, g_strdup_printf ("-P%s", printer));
	g_ptr_array_add (argv_lpr, g_strdup ("/tmp/temp.pdf"));
	g_ptr_array_add (argv_lpr, NULL);
	ret = g_spawn_sync ("/tmp", (gchar **) argv_lpr->pdata, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL, NULL, error);
	if (!ret)
		goto out;
out:
	if (argv_lpr != NULL)
		g_ptr_array_unref (argv_lpr);
	return ret;
}

/**
 * ch_shipping_print_svg_doc:
 **/
gboolean
ch_shipping_print_svg_doc (const gchar *str, const gchar *printer, GError **error)
{
	const gchar *argv_latex[] = { "rsvg-convert",
				      "--zoom=0.8",
				      "--format=pdf",
				      "--output=/tmp/temp-svg.pdf",
				      "/tmp/temp.svg", NULL };
	gboolean ret;
	gint exit_status = 0;
	GPtrArray *argv_lpr = NULL;

	/* save */
	ret = g_file_set_contents ("/tmp/temp.svg", str, -1, error);
	if (!ret)
		goto out;

	/* convert to pdf */
	ret = g_spawn_sync ("/tmp", (gchar **) argv_latex, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL, &exit_status, error);
	if (!ret)
		goto out;
	if (exit_status != 0) {
		ret = FALSE;
		g_set_error_literal (error, 1, 0, "Failed to prepare latex document");
		goto out;
	}

	/* send to the printer */
	argv_lpr = g_ptr_array_new_with_free_func (g_free);
	g_ptr_array_add (argv_lpr, g_strdup ("lpr"));
	if (printer != NULL)
		g_ptr_array_add (argv_lpr, g_strdup_printf ("-P%s", printer));
	g_ptr_array_add (argv_lpr, g_strdup ("/tmp/temp-svg.pdf"));
	g_ptr_array_add (argv_lpr, NULL);
	ret = g_spawn_sync ("/tmp", (gchar **) argv_lpr->pdata, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL, NULL, error);
	if (!ret)
		goto out;
out:
	if (argv_lpr != NULL)
		g_ptr_array_unref (argv_lpr);
	return ret;
}

/**
 * ch_shipping_postage_to_price:
 **/
gdouble
ch_shipping_postage_to_price (ChShippingPostage postage)
{
	switch (postage) {
	case CH_SHIPPING_POSTAGE_UNKNOWN:
		return 0;
		break;
	case CH_SHIPPING_POSTAGE_UK:
	case CH_SHIPPING_POSTAGE_XUK:
		return 2.0f;
		break;
	case CH_SHIPPING_POSTAGE_EUROPE:
	case CH_SHIPPING_POSTAGE_XEUROPE:
		return 3.0f;
		break;
	case CH_SHIPPING_POSTAGE_WORLD:
	case CH_SHIPPING_POSTAGE_XWORLD:
		return 4.0f;
		break;
	case CH_SHIPPING_POSTAGE_UK_SIGNED:
	case CH_SHIPPING_POSTAGE_XUK_SIGNED:
		return 7.0f;
		break;
	case CH_SHIPPING_POSTAGE_EUROPE_SIGNED:
	case CH_SHIPPING_POSTAGE_XEUROPE_SIGNED:
		return 8.0f;
		break;
	case CH_SHIPPING_POSTAGE_WORLD_SIGNED:
	case CH_SHIPPING_POSTAGE_XWORLD_SIGNED:
		return 9.0f;
		break;
	case CH_SHIPPING_POSTAGE_AUK:
	case CH_SHIPPING_POSTAGE_AEUROPE:
	case CH_SHIPPING_POSTAGE_AWORLD:
		return 3.5f;
		break;
	default:
		g_assert_not_reached ();
		break;
	}
	return 0;
}

/**
 * ch_shipping_postage_to_service:
 **/
const gchar *
ch_shipping_postage_to_service (ChShippingPostage postage)
{
	switch (postage) {
	case CH_SHIPPING_POSTAGE_UNKNOWN:
		return "xxx";
		break;
	case CH_SHIPPING_POSTAGE_UK:
	case CH_SHIPPING_POSTAGE_XUK:
		return "1c LL";
		break;
	case CH_SHIPPING_POSTAGE_EUROPE:
	case CH_SHIPPING_POSTAGE_XEUROPE:
		return "A sm pkt 145g";
		break;
	case CH_SHIPPING_POSTAGE_WORLD:
	case CH_SHIPPING_POSTAGE_XWORLD:
		return "A sm pkt 145g";
		break;
	case CH_SHIPPING_POSTAGE_UK_SIGNED:
	case CH_SHIPPING_POSTAGE_XUK_SIGNED:
		return "1RSF 145g";
		break;
	case CH_SHIPPING_POSTAGE_EUROPE_SIGNED:
	case CH_SHIPPING_POSTAGE_XEUROPE_SIGNED:
		return "AISF 145g";
		break;
	case CH_SHIPPING_POSTAGE_WORLD_SIGNED:
	case CH_SHIPPING_POSTAGE_XWORLD_SIGNED:
		return "AISF 145g";
		break;
	case CH_SHIPPING_POSTAGE_AUK:
		return "1c LL";
		break;
	case CH_SHIPPING_POSTAGE_AEUROPE:
		return "A sm pkt 65g";
		break;
	case CH_SHIPPING_POSTAGE_AWORLD:
		return "A sm pkt 65g";
		break;
	default:
		g_assert_not_reached ();
		break;
	}
	return NULL;
}

/**
 * ch_shipping_device_to_price:
 **/
guint
ch_shipping_device_to_price (ChShippingPostage postage)
{
	switch (postage) {
	case CH_SHIPPING_POSTAGE_UNKNOWN:
		return 0;
		break;
	case CH_SHIPPING_POSTAGE_UK:
	case CH_SHIPPING_POSTAGE_EUROPE:
	case CH_SHIPPING_POSTAGE_WORLD:
	case CH_SHIPPING_POSTAGE_UK_SIGNED:
	case CH_SHIPPING_POSTAGE_EUROPE_SIGNED:
	case CH_SHIPPING_POSTAGE_WORLD_SIGNED:
		return 48;
		break;
	case CH_SHIPPING_POSTAGE_XUK:
	case CH_SHIPPING_POSTAGE_XEUROPE:
	case CH_SHIPPING_POSTAGE_XWORLD:
	case CH_SHIPPING_POSTAGE_XUK_SIGNED:
	case CH_SHIPPING_POSTAGE_XEUROPE_SIGNED:
	case CH_SHIPPING_POSTAGE_XWORLD_SIGNED:
		return 60;
		break;
	case CH_SHIPPING_POSTAGE_AUK:
	case CH_SHIPPING_POSTAGE_AEUROPE:
	case CH_SHIPPING_POSTAGE_AWORLD:
		return 0;
		break;
	default:
		g_assert_not_reached ();
		break;
	}
	return 0;
}

/**
 * ch_shipping_postage_to_string:
 **/
const gchar *
ch_shipping_postage_to_string (ChShippingPostage postage)
{
	if (postage == CH_SHIPPING_POSTAGE_UNKNOWN)
		return "n/a";
	if (postage == CH_SHIPPING_POSTAGE_UK)
		return "UK";
	if (postage == CH_SHIPPING_POSTAGE_EUROPE)
		return "EUR";
	if (postage == CH_SHIPPING_POSTAGE_WORLD)
		return "WOR";
	if (postage == CH_SHIPPING_POSTAGE_UK_SIGNED)
		return "UK-S";
	if (postage == CH_SHIPPING_POSTAGE_EUROPE_SIGNED)
		return "EUR-S";
	if (postage == CH_SHIPPING_POSTAGE_WORLD_SIGNED)
		return "WOR-S";
	if (postage == CH_SHIPPING_POSTAGE_XUK)
		return "XUK";
	if (postage == CH_SHIPPING_POSTAGE_XEUROPE)
		return "XEUR";
	if (postage == CH_SHIPPING_POSTAGE_XWORLD)
		return "XWOR";
	if (postage == CH_SHIPPING_POSTAGE_XUK_SIGNED)
		return "XUK-S";
	if (postage == CH_SHIPPING_POSTAGE_XEUROPE_SIGNED)
		return "XEUR-S";
	if (postage == CH_SHIPPING_POSTAGE_XWORLD_SIGNED)
		return "XWOR-S";
	if (postage == CH_SHIPPING_POSTAGE_AUK)
		return "AUK";
	if (postage == CH_SHIPPING_POSTAGE_AEUROPE)
		return "AEUR";
	if (postage == CH_SHIPPING_POSTAGE_AWORLD)
		return "AWOR";
	return NULL;
}

/**
 * ch_shipping_send_email:
 **/
gboolean
ch_shipping_send_email (const gchar *sender,
			const gchar *recipient,
			const gchar *subject,
			const gchar *body,
			GError **error)
{
	gboolean ret;
	gchar *cmd = NULL;
	GString *str = NULL;
	gint retval = 0;
	gchar *spawn_stderr = NULL;

	/* write email */
	str = g_string_new ("");
	g_string_append_printf (str, "From: %s\n", sender);
	g_string_append_printf (str, "To: %s\n", recipient);
	g_string_append_printf (str, "Subject: %s\n", subject);
	g_string_append_printf (str, "\n%s\n", body);

	/* dump the email to file */
	ret = g_file_set_contents ("/tmp/email.txt", str->str, -1, error);
	if (!ret)
		goto out;

	/* actually send the email */
	cmd = g_strdup_printf ("curl -n --ssl-reqd --mail-from \"%s\" "
			       "--mail-rcpt \"%s\" "
			       "--url smtps://smtp.gmail.com:465 -T "
			       "/tmp/email.txt", sender, recipient);
	ret = g_spawn_command_line_sync (cmd, NULL, &spawn_stderr, &retval, error);
	if (!ret)
		goto out;
	if (retval != 0) {
		ret = FALSE;
		g_set_error (error, 1, 0, "Failed to send email to %s: %s",
			     recipient, spawn_stderr);
		goto out;
	}
out:
	g_free (spawn_stderr);
	g_free (cmd);
	if (str != NULL)
		g_string_free (str, TRUE);
	return ret;
}

