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

#include "ch-shipping-common.h"

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

