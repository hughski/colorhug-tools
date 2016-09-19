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
#include <unistd.h>

#include "ch-shipping-common.h"

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

	g_return_val_if_fail (string != NULL, 0);
	g_return_val_if_fail (search != NULL, 0);
	g_return_val_if_fail (replace != NULL, 0);

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

gboolean
ch_shipping_print_latex_doc (const gchar *str, const gchar *printer, GError **error)
{
	gboolean ret;
	gchar *filename = NULL;
	gint exit_status = 0;
	gint fd = -1;
	GPtrArray *argv_lpr = NULL;
	GPtrArray *argv = NULL;
	guint len;

	/* save */
	filename = g_build_filename (g_get_tmp_dir (), "ch-shipping-XXXXXX.tex", NULL);
	fd = g_mkstemp (filename);
	ret = g_file_set_contents (filename, str, -1, error);
	if (!ret)
		goto out;

	/* convert to pdf */
	argv = g_ptr_array_new_with_free_func (g_free);
	g_ptr_array_add (argv, g_strdup ("pdflatex"));
	g_ptr_array_add (argv, g_strdup (filename));
	g_ptr_array_add (argv, NULL);
	ret = g_spawn_sync (g_get_tmp_dir (),
			    (gchar **) argv->pdata,
			    NULL, G_SPAWN_SEARCH_PATH,
			    NULL, NULL, NULL, NULL,
			    &exit_status, error);
	if (!ret)
		goto out;
	if (exit_status != 0) {
		ret = FALSE;
		g_set_error_literal (error, 1, 0, "Failed to prepare latex document");
		goto out;
	}

	/* change the extension */
	len = strlen (filename);
	filename[len - 3] = 'p';
	filename[len - 2] = 'd';
	filename[len - 1] = 'f';

	/* send to the printer */
	argv_lpr = g_ptr_array_new_with_free_func (g_free);
	g_ptr_array_add (argv_lpr, g_strdup ("lpr"));
	if (printer != NULL)
		g_ptr_array_add (argv_lpr, g_strdup_printf ("-P%s", printer));
	g_ptr_array_add (argv_lpr, g_strdup (filename));
	g_ptr_array_add (argv_lpr, NULL);
	ret = g_spawn_sync (g_get_tmp_dir (), (gchar **) argv_lpr->pdata, NULL,
			    G_SPAWN_SEARCH_PATH, NULL, NULL, NULL,
			    NULL, NULL, error);
	if (!ret)
		goto out;
out:
	if (fd > 0)
		close (fd);
	g_free (filename);
	if (argv != NULL)
		g_ptr_array_unref (argv);
	if (argv_lpr != NULL)
		g_ptr_array_unref (argv_lpr);
	return ret;
}

gboolean
ch_shipping_print_svg_doc (const gchar *str, const gchar *printer, GError **error)
{
	gboolean ret;
	gint exit_status = 0;
	GPtrArray *argv = NULL;
	GPtrArray *argv_lpr = NULL;
	gchar *filename = NULL;
	gchar *filename_out = NULL;
	gint fd = -1;
	gint fd_out = -1;

	/* save */
	filename = g_build_filename (g_get_tmp_dir (), "ch-shipping-XXXXXX.svg", NULL);
	fd = g_mkstemp (filename);
	filename_out = g_build_filename (g_get_tmp_dir (), "ch-shipping-XXXXXX.pdf", NULL);
	fd_out = g_mkstemp (filename_out);
	ret = g_file_set_contents (filename, str, -1, error);
	if (!ret)
		goto out;

	/* convert to pdf */
	argv = g_ptr_array_new_with_free_func (g_free);
	g_ptr_array_add (argv, g_strdup ("inkscape"));
	g_ptr_array_add (argv, g_strdup_printf ("--file=%s", filename));
	g_ptr_array_add (argv, g_strdup_printf ("--export-pdf=%s", filename_out));
#if 0
	g_ptr_array_add (argv, g_strdup ("rsvg-convert"));
	g_ptr_array_add (argv, g_strdup ("--zoom=0.8"));
	g_ptr_array_add (argv, g_strdup ("--format=pdf"));
	g_ptr_array_add (argv, g_strdup_printf ("--output=%s", filename_out));
	g_ptr_array_add (argv, g_strdup (filename));
#endif
	g_ptr_array_add (argv, NULL);
	ret = g_spawn_sync (g_get_tmp_dir (), (gchar **) argv->pdata, NULL,
			    G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL,
			    &exit_status, error);
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
	g_ptr_array_add (argv_lpr, g_strdup (filename_out));
	g_ptr_array_add (argv_lpr, NULL);
	ret = g_spawn_sync (g_get_tmp_dir (), (gchar **) argv_lpr->pdata,
			    NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL,
			    NULL, error);
	if (!ret)
		goto out;
out:
	if (fd > 0)
		close (fd);
	if (fd_out > 0)
		close (fd_out);
	g_free (filename);
	g_free (filename_out);
	if (argv != NULL)
		g_ptr_array_unref (argv);
	if (argv_lpr != NULL)
		g_ptr_array_unref (argv_lpr);
	return ret;
}

gdouble
ch_shipping_kind_to_price (ChShippingKind postage)
{
	switch (postage) {
	case CH_SHIPPING_KIND_UNKNOWN:
		return 0;
		break;
	case CH_SHIPPING_KIND_CH1_UK:
		return 2.2f;
		break;
	case CH_SHIPPING_KIND_CH1_EUROPE:
		return 3.5f;
		break;
	case CH_SHIPPING_KIND_CH1_WORLD:
		return 4.7f;
		break;
	case CH_SHIPPING_KIND_CH1_UK_SIGNED:
		return 4.6f;
		break;
	case CH_SHIPPING_KIND_CH1_EUROPE_SIGNED:
		return 8.8f;
		break;
	case CH_SHIPPING_KIND_CH1_WORLD_SIGNED:
		return 9.9f;
		break;
	case CH_SHIPPING_KIND_STRAP_UK:
	case CH_SHIPPING_KIND_STRAP_EUROPE:
	case CH_SHIPPING_KIND_STRAP_WORLD:
		return 3.5f;
		break;
	case CH_SHIPPING_KIND_ALS_UK:
	case CH_SHIPPING_KIND_ALS_EUROPE:
	case CH_SHIPPING_KIND_ALS_WORLD:
//	case CH_SHIPPING_KIND_CH2_UK:
//	case CH_SHIPPING_KIND_CH2_EUROPE:
//	case CH_SHIPPING_KIND_CH2_WORLD:
	case CH_SHIPPING_KIND_CH2_UK_SIGNED:
	case CH_SHIPPING_KIND_CH2_EUROPE_SIGNED:
	case CH_SHIPPING_KIND_CH2_WORLD_SIGNED:
		return 0.0f;
		break;
	default:
		g_assert_not_reached ();
		break;
	}
	return 0;
}

const gchar *
ch_shipping_kind_to_service (ChShippingKind postage)
{
	switch (postage) {
	case CH_SHIPPING_KIND_UNKNOWN:
		return "xxx";
		break;
//	case CH_SHIPPING_KIND_CH2_UK:
	case CH_SHIPPING_KIND_CH1_UK:
		return "P 145g";
		break;
//	case CH_SHIPPING_KIND_CH2_EUROPE:
	case CH_SHIPPING_KIND_CH1_EUROPE:
		return "A sm pkt 145g";
		break;
//	case CH_SHIPPING_KIND_CH2_WORLD:
	case CH_SHIPPING_KIND_CH1_WORLD:
		return "A sm pkt 145g";
		break;
	case CH_SHIPPING_KIND_CH2_UK_SIGNED:
	case CH_SHIPPING_KIND_CH1_UK_SIGNED:
		return "1RSF 145g";
		break;
	case CH_SHIPPING_KIND_CH2_EUROPE_SIGNED:
	case CH_SHIPPING_KIND_CH1_EUROPE_SIGNED:
		return "AISF 145g";
		break;
	case CH_SHIPPING_KIND_CH2_WORLD_SIGNED:
	case CH_SHIPPING_KIND_CH1_WORLD_SIGNED:
		return "AISF 145g";
		break;
	case CH_SHIPPING_KIND_STRAP_UK:
		return "A sm pkt 65g";
		break;
	case CH_SHIPPING_KIND_STRAP_EUROPE:
		return "A sm pkt 65g";
		break;
	case CH_SHIPPING_KIND_STRAP_WORLD:
		return "A sm pkt 65g";
		break;
	case CH_SHIPPING_KIND_ALS_UK:
		return "A sm pkt 20g";
		break;
	case CH_SHIPPING_KIND_ALS_EUROPE:
		return "A sm pkt 20g";
		break;
	case CH_SHIPPING_KIND_ALS_WORLD:
		return "A sm pkt 20g";
		break;
	default:
		g_assert_not_reached ();
		break;
	}
	return NULL;
}

guint
ch_shipping_device_to_price (ChShippingKind postage)
{
	switch (postage) {
	case CH_SHIPPING_KIND_UNKNOWN:
		return 0;
		break;
	case CH_SHIPPING_KIND_CH1_UK:
	case CH_SHIPPING_KIND_CH1_EUROPE:
	case CH_SHIPPING_KIND_CH1_WORLD:
	case CH_SHIPPING_KIND_CH1_UK_SIGNED:
	case CH_SHIPPING_KIND_CH1_EUROPE_SIGNED:
	case CH_SHIPPING_KIND_CH1_WORLD_SIGNED:
		return 60;
		break;
//	case CH_SHIPPING_KIND_CH2_UK:
//	case CH_SHIPPING_KIND_CH2_EUROPE:
//	case CH_SHIPPING_KIND_CH2_WORLD:
	case CH_SHIPPING_KIND_CH2_UK_SIGNED:
	case CH_SHIPPING_KIND_CH2_EUROPE_SIGNED:
	case CH_SHIPPING_KIND_CH2_WORLD_SIGNED:
		return 85;
		break;
	case CH_SHIPPING_KIND_STRAP_UK:
	case CH_SHIPPING_KIND_STRAP_EUROPE:
	case CH_SHIPPING_KIND_STRAP_WORLD:
		return 0;
		break;
	case CH_SHIPPING_KIND_ALS_UK:
	case CH_SHIPPING_KIND_ALS_EUROPE:
	case CH_SHIPPING_KIND_ALS_WORLD:
		return 20;
		break;
	default:
		g_assert_not_reached ();
		break;
	}
	return 0;
}

const gchar *
ch_shipping_kind_to_string (ChShippingKind postage)
{
	if (postage == CH_SHIPPING_KIND_UNKNOWN)
		return "n/a";
//	if (postage == CH_SHIPPING_KIND_CH2_UK)
//		return "CH2-UK";
//	if (postage == CH_SHIPPING_KIND_CH2_EUROPE)
//		return "CH2-EUR";
//	if (postage == CH_SHIPPING_KIND_CH2_WORLD)
//		return "CH2-WOR";
	if (postage == CH_SHIPPING_KIND_CH2_UK_SIGNED)
		return "CH2-UK-S";
	if (postage == CH_SHIPPING_KIND_CH2_EUROPE_SIGNED)
		return "CH2-EUR-S";
	if (postage == CH_SHIPPING_KIND_CH2_WORLD_SIGNED)
		return "CH2-WOR-S";
	if (postage == CH_SHIPPING_KIND_CH1_UK)
		return "CH1-UK";
	if (postage == CH_SHIPPING_KIND_CH1_EUROPE)
		return "CH1-EUR";
	if (postage == CH_SHIPPING_KIND_CH1_WORLD)
		return "CH1-WOR";
	if (postage == CH_SHIPPING_KIND_CH1_UK_SIGNED)
		return "CH1-UK-S";
	if (postage == CH_SHIPPING_KIND_CH1_EUROPE_SIGNED)
		return "CH1-EUR-S";
	if (postage == CH_SHIPPING_KIND_CH1_WORLD_SIGNED)
		return "CH1-WOR-S";
	if (postage == CH_SHIPPING_KIND_STRAP_UK)
		return "ACC-UK";
	if (postage == CH_SHIPPING_KIND_STRAP_EUROPE)
		return "ACC-EUR";
	if (postage == CH_SHIPPING_KIND_STRAP_WORLD)
		return "ACC-WOR";
	if (postage == CH_SHIPPING_KIND_ALS_UK)
		return "ALS-UK";
	if (postage == CH_SHIPPING_KIND_ALS_EUROPE)
		return "ALS-EUR";
	if (postage == CH_SHIPPING_KIND_ALS_WORLD)
		return "ALS-WOR";
	return NULL;
}

gboolean
ch_shipping_send_email (const gchar *sender,
			const gchar *recipient,
			const gchar *subject,
			const gchar *body,
			const gchar *authtoken,
			GError **error)
{
	gboolean ret;
	gchar *cmd = NULL;
	gchar *filename = NULL;
	gchar *spawn_stderr = NULL;
	gint fd = -1;
	gint retval = 0;
	GString *str = NULL;

	/* write email */
	str = g_string_new ("");
	g_string_append_printf (str, "From: %s\n", sender);
	g_string_append_printf (str, "To: %s\n", recipient);
	g_string_append_printf (str, "Subject: %s\n", subject);
	g_string_append_printf (str, "\n%s\n", body);

	/* dump the email to file */
	filename = g_build_filename (g_get_tmp_dir (), "ch-shipping-XXXXXX.txt", NULL);
	fd = g_mkstemp (filename);
	ret = g_file_set_contents (filename, str->str, -1, error);
	if (!ret)
		goto out;

	/* actually send the email */
	cmd = g_strdup_printf ("curl -n --ssl-reqd --mail-from \"%s\" "
			       "--mail-rcpt \"%s\" "
			       "--url smtps://smtp.gmail.com:465 -T "
			       "%s -u %s",
			       sender, recipient, filename, authtoken);
	g_debug ("Using '%s' to send email", cmd);
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
	if (fd > 0)
		close (fd);
	g_free (filename);
	g_free (spawn_stderr);
	g_free (cmd);
	if (str != NULL)
		g_string_free (str, TRUE);
	return ret;
}

