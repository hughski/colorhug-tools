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
 * ch_shipping_postage_to_price:
 **/
guint
ch_shipping_postage_to_price (ChShippingPostage postage)
{
	switch (postage) {
	case CH_SHIPPING_POSTAGE_UNKNOWN:
		return 0;
		break;
	case CH_SHIPPING_POSTAGE_UK:
	case CH_SHIPPING_POSTAGE_XUK:
		return 2;
		break;
	case CH_SHIPPING_POSTAGE_EUROPE:
	case CH_SHIPPING_POSTAGE_XEUROPE:
		return 3;
		break;
	case CH_SHIPPING_POSTAGE_WORLD:
	case CH_SHIPPING_POSTAGE_XWORLD:
		return 4;
		break;
	case CH_SHIPPING_POSTAGE_UK_SIGNED:
	case CH_SHIPPING_POSTAGE_XUK_SIGNED:
		return 7;
		break;
	case CH_SHIPPING_POSTAGE_EUROPE_SIGNED:
	case CH_SHIPPING_POSTAGE_XEUROPE_SIGNED:
		return 8;
		break;
	case CH_SHIPPING_POSTAGE_WORLD_SIGNED:
	case CH_SHIPPING_POSTAGE_XWORLD_SIGNED:
		return 9;
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
		return "A sm pkt 100g";
		break;
	case CH_SHIPPING_POSTAGE_WORLD:
	case CH_SHIPPING_POSTAGE_XWORLD:
		return "A sm pkt 100g";
		break;
	case CH_SHIPPING_POSTAGE_UK_SIGNED:
	case CH_SHIPPING_POSTAGE_XUK_SIGNED:
		return "1RSF 100g";
		break;
	case CH_SHIPPING_POSTAGE_EUROPE_SIGNED:
	case CH_SHIPPING_POSTAGE_XEUROPE_SIGNED:
		return "AISF 100g";
		break;
	case CH_SHIPPING_POSTAGE_WORLD_SIGNED:
	case CH_SHIPPING_POSTAGE_XWORLD_SIGNED:
		return "AISF 100g";
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

